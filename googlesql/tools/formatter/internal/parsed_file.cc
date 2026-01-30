//
// Copyright 2019 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "googlesql/tools/formatter/internal/parsed_file.h"

#include <stdint.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/common/utf_util.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parser.h"
#include "googlesql/public/error_helpers.h"
#include "googlesql/public/formatter_options.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/public/parse_resume_location.h"
#include "googlesql/tools/formatter/internal/chunk.h"
#include "googlesql/tools/formatter/internal/chunk_grouping_strategy.h"
#include "googlesql/tools/formatter/internal/range_utils.h"
#include "googlesql/tools/formatter/internal/token.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "unicode/unistr.h"
#include "unicode/utypes.h"
#include "googlesql/base/status_macros.h"

namespace googlesql::formatter::internal {

namespace {

// Detects the type of newlines used in <input>. If input contains more than one
// type of newlines, the first type is returned. If input doesn't contain any
// newlines, then "\n" is returned.
std::string DetectNewlineType(absl::string_view input) {
  std::string newline = std::string(FormatterOptions::kUnixNewLineType);
  for (int i = 0; i < input.size(); ++i) {
    if (input[i] == '\n') {
      if (i + 1 < input.size() && input[i + 1] == '\r') {
        newline = "\n\r";
      } else {
        newline = "\n";
      }
      break;
    } else if (input[i] == '\r') {
      if (i + 1 < input.size() && input[i + 1] == '\n') {
        newline = "\r\n";
      } else {
        newline = "\r";
      }
      break;
    }
  }
  return newline;
}

std::unique_ptr<ParsedFile> CreateParsedFile(absl::string_view sql,
                                             FormatterOptions options) {
  // Formatter expects interchange valid UTF-8 string as an input.
  icu::UnicodeString utf(sql.data(), static_cast<int32_t>(sql.size()));
  const UChar32 null_char[1] = {'\0'};
  const UChar32 unicode_replacement[1] = {'?'};
  utf.findAndReplace(icu::UnicodeString::fromUTF32(null_char, 1),
                     icu::UnicodeString::fromUTF32(unicode_replacement, 1));
  std::string pre_processed_sql;
  utf.toUTF8String<std::string>(pre_processed_sql);
  pre_processed_sql = googlesql::CoerceToWellFormedUTF8(pre_processed_sql);

  // Fix up options.
  if (options.IsLineTypeDetectionEnabled()) {
    options.SetNewLineType(DetectNewlineType(sql));
  }
  if (options.LineLengthLimit() <= 0) {
    options.SetLineLengthLimit(
        ::googlesql::FormatterOptions().LineLengthLimit());
  }
  return std::make_unique<ParsedFile>(std::move(pre_processed_sql),
                                      std::move(options));
}

std::string LineAndColumnStringFromByteOffset(
    int byte_offset, const ParseLocationTranslator& location_translator) {
  const ParseLocationPoint location =
      ParseLocationPoint::FromByteOffset(byte_offset);
  absl::StatusOr<std::pair<int, int>> error_pos_or =
      location_translator.GetLineAndColumnAfterTabExpansion(location);
  if (!error_pos_or.ok()) {
    return "";
  }
  const auto& [line, column] = error_pos_or.value();
  return absl::StrCat(" [at ", line, ":", column, "]");
}

absl::StatusOr<std::unique_ptr<googlesql::ParserOutput>> ParseTokenizedStmt(
    const TokenizedStmt& tokenized_stmt,
    const ParseLocationTranslator& location_translator) {
  const std::vector<formatter::internal::Token*>& tokens =
      tokenized_stmt.Tokens().WithoutComments();

  ParseResumeLocation location =
      ParseResumeLocation::FromStringView(tokenized_stmt.Sql());
  location.set_byte_position(
      tokens.front()->GetLocationRange().start().GetByteOffset());

  std::unique_ptr<ParserOutput> parser_output;
  bool unused;
  LanguageOptions language_options;
  language_options.EnableMaximumLanguageFeaturesForDevelopment();
  ParserOptions parser_options(language_options);
  GOOGLESQL_RETURN_IF_ERROR(ParseNextScriptStatement(&location, parser_options,
                                           &parser_output, &unused));

  if (location.byte_position() <
      tokens.back()->GetLocationRange().end().GetByteOffset()) {
    return absl::InternalError(absl::StrCat(
        "Parser stopped before parsing the entire statement",
        LineAndColumnStringFromByteOffset(location.byte_position(),
                                          location_translator),
        ". "
        "Remaining unparsed piece is:\n",
        tokenized_stmt.Sql().substr(
            location.byte_position(),
            tokens.back()->GetLocationRange().end().GetByteOffset() -
                location.byte_position())));
  }

  return parser_output;
}

}  // namespace

FilePart::FilePart(absl::string_view sql, int start_offset, int end_offset)
    : sql_(sql), start_offset_(start_offset), end_offset_(end_offset) {}

absl::string_view FilePart::Image() const {
  return sql_.substr(start_offset_, end_offset_ - start_offset_);
}

std::string UnparsedRegion::DebugString() const {
  return absl::StrCat("Unparsed:", Image());
}

absl::Status UnparsedRegion::Accept(ParsedFileVisitor* visitor) const {
  return visitor->VisitUnparsedRegion(*this);
}

TokenizedStmt::TokenizedStmt(absl::string_view sql, int start_offset,
                             int end_offset, std::vector<Token> tokens,
                             const FormatterOptions& options)
    : FilePart(sql, start_offset, end_offset),
      tokens_(std::move(tokens)),
      block_factory_(options.IndentationSpaces()) {
  tokens_view_ = TokensView::FromTokens(&tokens_);
}

absl::StatusOr<std::unique_ptr<TokenizedStmt>> TokenizedStmt::ParseFrom(
    ParseResumeLocation* parse_location,
    const ParseLocationTranslator& location_translator,
    const FormatterOptions& options) {
  const int start_offset = parse_location->byte_position();
  GOOGLESQL_ASSIGN_OR_RETURN(std::vector<Token> tokens,
                   TokenizeNextStatement(parse_location, options));
  auto parsed_sql = std::make_unique<TokenizedStmt>(
      parse_location->input(), start_offset, parse_location->byte_position(),
      std::move(tokens), options);
  GOOGLESQL_RETURN_IF_ERROR(
      parsed_sql->BuildChunksAndBlocks(location_translator, options));
  return parsed_sql;
}

absl::Status TokenizedStmt::BuildChunksAndBlocks(
    const ParseLocationTranslator& location_translator,
    const FormatterOptions& options) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::vector<Chunk> chunks,
                   ChunksFromTokens(Tokens(), location_translator, options));
  chunks_ = std::move(chunks);

  GOOGLESQL_RETURN_IF_ERROR(ComputeChunkBlocksForChunks(&block_factory_, &chunks_));
  return absl::OkStatus();
}

std::string TokenizedStmt::DebugString() const {
  return BlockTree()->DebugString();
}

absl::Status TokenizedStmt::Accept(ParsedFileVisitor* visitor) const {
  return visitor->VisitTokenizedStmt(*this);
}

bool TokenizedStmt::ShouldFormat() const { return true; }

std::string ParsedStmt::DebugString() const {
  return parser_output_->statement()->DebugString();
}

absl::Status ParsedStmt::Accept(ParsedFileVisitor* visitor) const {
  return visitor->VisitParsedStmt(*this);
}

absl::StatusOr<std::unique_ptr<ParsedFile>> ParsedFile::ParseLineRanges(
    absl::string_view sql, const std::vector<FormatterRange>& line_ranges,
    const FormatterOptions& options, ParseAction parse_action) {
  std::unique_ptr<ParsedFile> file = CreateParsedFile(sql, options);
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::vector<FormatterRange> byte_ranges,
      ConvertLineRangesToSortedByteRanges(line_ranges, file->Sql(),
                                          file->GetLocationTranslator()));
  GOOGLESQL_RETURN_IF_ERROR(file->ParseByteRanges(std::move(byte_ranges), parse_action));
  return file;
}

absl::StatusOr<std::unique_ptr<ParsedFile>> ParsedFile::ParseByteRanges(
    absl::string_view sql, const std::vector<FormatterRange>& byte_ranges,
    const FormatterOptions& options, ParseAction parse_action) {
  std::unique_ptr<ParsedFile> file = CreateParsedFile(sql, options);
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::vector<FormatterRange> sorted_ranges,
      ValidateAndSortByteRanges(byte_ranges, file->Sql(), options));
  GOOGLESQL_RETURN_IF_ERROR(
      file->ParseByteRanges(std::move(sorted_ranges), parse_action));
  return file;
}

absl::StatusOr<std::unique_ptr<FilePart>> ParsedFile::ParseNextFilePart(
    ParseResumeLocation* parse_location, ParseAction parse_action) {
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<TokenizedStmt> tokenized_stmt,
      TokenizedStmt::ParseFrom(parse_location, location_translator_, options_));

  if (parse_action == ParseAction::kTokenize ||
      tokenized_stmt->Tokens().WithoutComments().empty()) {
    return tokenized_stmt;
  }

  absl::StatusOr<std::unique_ptr<googlesql::ParserOutput>> parser_output_or =
      ParseTokenizedStmt(*tokenized_stmt, location_translator_);
  if (parser_output_or.ok()) {
    return std::make_unique<ParsedStmt>(std::move(tokenized_stmt),
                                        std::move(parser_output_or.value()));
  } else {
    parse_errors_.push_back(parser_output_or.status());
    return tokenized_stmt;
  }
}

absl::Status ParsedFile::ParseByteRanges(
    std::vector<FormatterRange> byte_ranges, ParseAction parse_action) {
  if (byte_ranges.empty() && !Sql().empty()) {
    byte_ranges.push_back({0, static_cast<int>(Sql().size())});
  } else if (options_.GetExpandRangesToFullStatements()) {
    GOOGLESQL_RETURN_IF_ERROR(ExpandByteRangesToFullStatements(&byte_ranges, Sql()));
  }
  int last_end = 0;
  for (const auto& range : byte_ranges) {
    if (range.start > last_end) {
      file_parts_.emplace_back(
          std::make_unique<UnparsedRegion>(Sql(), last_end, range.start));
    } else if (range.start < last_end) {
      // There are overlapping ranges: this should never happen.
      return absl::InternalError(absl::StrFormat(
          "found overlapping byte ranges (after range expansion?): {%s}",
          absl::StrJoin(byte_ranges, ", ",
                        [](std::string* out, const FormatterRange& range) {
                          absl::StrAppend(out, "(", range.start, ", ",
                                          range.end, ")");
                        })));
    }
    ParseResumeLocation parse_location =
        ParseResumeLocation::FromStringView(Sql().substr(0, range.end));
    parse_location.set_byte_position(range.start);

    while (parse_location.byte_position() < parse_location.input().size()) {
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<FilePart> parsed_stmt,
                       ParseNextFilePart(&parse_location, parse_action));
      if (!parsed_stmt->IsEmpty()) {
        file_parts_.emplace_back(std::move(parsed_stmt));
      }
    }
    last_end = range.end;
  }
  if (last_end < Sql().size()) {
    file_parts_.emplace_back(
        std::make_unique<UnparsedRegion>(Sql(), last_end, Sql().size()));
  }
  return absl::OkStatus();
}

std::string ParsedFile::DebugString() const {
  std::string result = absl::StrJoin(
      file_parts_, "\n",
      [](std::string* out, const std::unique_ptr<const FilePart>& part) {
        absl::StrAppend(out, part->DebugString());
      });
  if (!parse_errors_.empty()) {
    absl::StrAppend(
        &result, "\nParse errors:\n",
        absl::StrJoin(parse_errors_, "\n",
                      [this](std::string* out, const absl::Status& status) {
                        absl::StrAppend(
                            out, MaybeUpdateErrorFromPayload(
                                     ERROR_MESSAGE_MULTI_LINE_WITH_CARET,
                                     /*keep_error_location_payload=*/false,
                                     sql_, status)
                                     .message());
                      }));
  }
  return result;
}

absl::Status ParsedFile::Accept(ParsedFileVisitor* visitor) const {
  for (const auto& part : file_parts_) {
    GOOGLESQL_RETURN_IF_ERROR(part->Accept(visitor));
  }
  return absl::OkStatus();
}

absl::StatusOr<std::vector<FormatterRange>> ValidateAndSortByteRanges(
    const std::vector<FormatterRange>& byte_ranges, absl::string_view sql,
    const FormatterOptions& options) {
  if (byte_ranges.empty()) {
    return byte_ranges;
  }
  std::vector<FormatterRange> sorted_ranges(byte_ranges);
  std::sort(sorted_ranges.begin(), sorted_ranges.end(),
            [](const FormatterRange& a, const FormatterRange& b) {
              return a.start < b.start;
            });

  auto prev = sorted_ranges.end();
  for (auto range = sorted_ranges.begin(); range != sorted_ranges.end();
       ++range) {
    if (range->start < 0 || range->end <= 0 || range->start >= sql.size() ||
        range->end > sql.size()) {
      return absl::InvalidArgumentError(absl::StrFormat(
          "byte range is out of bounds: [%d, %d); input size: %d ",
          range->start, range->end, sql.size()));
    }
    if (range->start > range->end) {
      return absl::InvalidArgumentError(absl::StrFormat(
          "invalid byte range: start should be less than end; got: [%d, %d)",
          range->start, range->end));
    }
    if (prev != sorted_ranges.end()) {
      if (prev->end > range->start) {
        return absl::InvalidArgumentError(
            absl::StrFormat("overlapping byte ranges are not allowed; found: "
                            "[%d, %d) and [%d, %d)",
                            prev->start, prev->end, range->start, range->end));
      }
      // Adjancent ranges are only merged if expand_format_ranges is enabled.
      if (options.GetExpandRangesToFullStatements()) {
        if (prev->end == range->start) {
          range->start = prev->start;
          range = sorted_ranges.erase(prev);
        }
      }
    }
    prev = range;
  }
  return sorted_ranges;
}

}  // namespace googlesql::formatter::internal
