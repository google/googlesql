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

#include "googlesql/public/parse_tokens.h"

#include <ctype.h>

#include <cctype>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/arena.h"
#include "googlesql/common/errors.h"
#include "googlesql/parser/keywords.h"
#include "googlesql/parser/lookahead_transformer.h"
#include "googlesql/parser/parser_mode.h"
#include "googlesql/parser/tm_token.h"
#include "googlesql/parser/token_with_location.h"
#include "googlesql/public/error_helpers.h"
#include "googlesql/public/functions/convert_string.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/public/parse_resume_location.h"
#include "googlesql/public/strings.h"
#include "googlesql/public/value.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

namespace {

// State of the tokenizer between tokens, for cases where tokens need to be
// interpreted differently depending on context.
enum class TokenizerState { kNone, kIdentifier, kIdentifierDot };

}  // namespace

using Token = parser::Token;

// Returns a syntax error status with message 'error_message', located at byte
// offset 'error_offset' into 'location'.
static absl::Status MakeSyntaxErrorAtLocationOffset(
    const ParseLocationRange& location, int error_offset,
    absl::string_view error_message) {
  absl::string_view filename = location.start().filename();
  const int total_error_offset =
      location.start().GetByteOffset() + error_offset;
  return MakeSqlErrorAtPoint(
      ParseLocationPoint::FromByteOffset(filename, total_error_offset))
      << "Syntax error: " << error_message;
}

static absl::Status ConvertBisonToken(parser::Token bison_token,
                                      bool is_adjacent_to_prior_token,
                                      const ParseLocationRange& location,
                                      std::string image,
                                      std::vector<ParseToken>* parse_tokens) {
  switch (bison_token) {
    case Token::EOI:
      parse_tokens->emplace_back(location, is_adjacent_to_prior_token,
                                 std::move(image), ParseToken::END_OF_INPUT);
      break;

    case parser::Token::SEMICOLON: {
      // The tokenizer may include some whitespace in the ; token.
      // We don't want to include that in the image.
      ParseLocationRange adjusted_location = location;
      adjusted_location.set_end(ParseLocationPoint::FromByteOffset(
          location.start().filename(), location.start().GetByteOffset() + 1));
      parse_tokens->emplace_back(adjusted_location, is_adjacent_to_prior_token,
                                 ";", ParseToken::KEYWORD);
      break;
    }

    case parser::Token::STRING_LITERAL: {
      std::string parsed_value;
      int error_offset;
      std::string error_message;
      const absl::Status parse_status = ParseStringLiteral(
          image, &parsed_value, &error_message, &error_offset);
      if (!parse_status.ok()) {
        return MakeSyntaxErrorAtLocationOffset(location, error_offset,
                                               error_message);
      }
      parse_tokens->emplace_back(location, is_adjacent_to_prior_token,
                                 std::move(image), ParseToken::VALUE,
                                 Value::String(parsed_value));
      break;
    }

    case parser::Token::BYTES_LITERAL: {
      std::string parsed_value;
      int error_offset;
      std::string error_message;
      const absl::Status parse_status = ParseBytesLiteral(
          image, &parsed_value, &error_message, &error_offset);
      if (!parse_status.ok()) {
        return MakeSyntaxErrorAtLocationOffset(location, error_offset,
                                               error_message);
      }
      parse_tokens->emplace_back(location, is_adjacent_to_prior_token,
                                 std::move(image), ParseToken::VALUE,
                                 Value::Bytes(parsed_value));
      break;
    }

    case parser::Token::FLOATING_POINT_LITERAL: {
      double double_value;
      if (!functions::StringToNumeric(image, &double_value, nullptr)) {
        return MakeSqlErrorAtPoint(location.start())
               << "Invalid floating point literal: " << image;
      }
      parse_tokens->emplace_back(location, is_adjacent_to_prior_token,
                                 std::move(image), ParseToken::VALUE,
                                 Value::Double(double_value));
      break;
    }

    case parser::Token::INTEGER_LITERAL: {
      Value parsed_value;
      if (!Value::ParseInteger(image, &parsed_value)) {
        return MakeSqlErrorAtPoint(location.start())
               << "Invalid integer literal: " << image;
      }
      parse_tokens->emplace_back(location, is_adjacent_to_prior_token,
                                 std::move(image), ParseToken::VALUE,
                                 parsed_value);
      break;
    }

    case parser::Token::COMMENT: {
      std::string comment(image);
      if (comment[0] != '/') {
        // The lexer rule for the comment will match trailing whitespaces. The
        // input might contain '\n', '\r' or combination of the two, so we strip
        // the original whitespace and then add back a '\n' to normalize the
        // token.
        // We need to add a newline to prevent errors in case someone tries to
        // reconstruct the query by simply concatenating the tokens with a space
        // between each token. If we did not add a newline and the stream
        // contained a line comment everything after the line comment would be
        // commented out.
        absl::StripAsciiWhitespace(&comment);
        absl::StrAppend(&comment, "\n");
      }
      parse_tokens->emplace_back(location, is_adjacent_to_prior_token,
                                 std::move(image), ParseToken::COMMENT,
                                 Value::String(comment));
      break;
    }

    case parser::Token::IDENTIFIER:
      if (image[0] == '`') {
        std::string identifier;
        int error_offset;
        std::string error_message;
        const absl::Status parse_status = ParseGeneralizedIdentifier(
            image, &identifier, &error_message, &error_offset);
        if (!parse_status.ok()) {
          return MakeSyntaxErrorAtLocationOffset(location, error_offset,
                                                 error_message);
        }
        parse_tokens->emplace_back(location, is_adjacent_to_prior_token,
                                   std::move(image), ParseToken::IDENTIFIER,
                                   Value::String(identifier));
      } else if (isdigit(image[0])) {
        // Values that start with digits also never can be keywords, so they are
        // returned as IDENTIFIER.
        parse_tokens->emplace_back(location, is_adjacent_to_prior_token,
                                   std::move(image), ParseToken::IDENTIFIER,
                                   Value::String(image));
      } else {
        if (parser::IsKeywordInTokenizer(image)) {
          // For consistency and backward compatibility, we force some
          // words to be keywords. Some words are treated as regular identifiers
          // in the Bison parser while historically they need to be keywords in
          // the GetParseTokens() API. Others are only recognized as keywords
          // with certain context, e.g. with or without a trailing "(", but here
          // we return them as keywords always.
          parse_tokens->emplace_back(location, is_adjacent_to_prior_token,
                                     std::move(image), ParseToken::KEYWORD);
        } else {
          parse_tokens->emplace_back(
              location, is_adjacent_to_prior_token, std::move(image),
              ParseToken::IDENTIFIER_OR_KEYWORD, Value::String(image));
        }
      }
      break;

    case Token::ATTACHED_ALIAS: {
      // This token looks like an alias but it was attached to the preceding
      // token. In the production parser, this will generate an error. Re-attach
      // it so that tools like formatters don't accidentally insert a space
      // formatting an error query into a valid query.
      GOOGLESQL_RET_CHECK(!parse_tokens->empty());
      ParseToken previous = parse_tokens->back();
      parse_tokens->pop_back();
      ParseLocationRange combined_location(previous.GetLocationRange().start(),
                                           location.end());
      std::string combined_image = absl::StrCat(previous.GetImage(), image);
      parse_tokens->emplace_back(
          combined_location, previous.IsAdjacentToPreviousToken(),
          std::move(combined_image), ParseToken::KEYWORD);
      break;
    }

    default:
      // All keywords and symbols become KEYWORD.
      parse_tokens->emplace_back(location, is_adjacent_to_prior_token,
                                 std::move(image), ParseToken::KEYWORD);
      break;
  }
  return absl::OkStatus();
}

absl::Status GetParseTokens(const ParseTokenOptions& options,
                            ParseResumeLocation* resume_location,
                            std::vector<ParseToken>* tokens) {
  if (!resume_location->allow_resume()) {
    return MakeSqlError()
           << "GetParseTokens() called on invalid ParseResumeLocation";
  }
  if (options.max_tokens > 0) {
    resume_location->DisallowResume();
  }
  GOOGLESQL_RETURN_IF_ERROR(resume_location->Validate());
  tokens->clear();

  auto mode = parser::ParserMode::kTokenizer;
  if (options.include_comments) {
    mode = parser::ParserMode::kTokenizerPreserveComments;
  }

  auto arena = std::make_unique<googlesql_base::UnsafeArena>(/*block_size=*/4096);
  parser::StackFrame::StackFrameFactory stack_frame_factory;
  GOOGLESQL_ASSIGN_OR_RETURN(
      auto tokenizer,
      parser::LookaheadTransformer::Create(
          mode, resume_location->filename(), resume_location->input(),
          resume_location->byte_position(), options.language_options,
          parser::MacroExpansionMode::kNone,
          /*macro_catalog=*/nullptr, arena.get(), stack_frame_factory));

  absl::Status status;
  ParseLocationRange previous_location;
  ParseLocationRange location;
  while (true) {
    parser::Token bison_token;
    status = ConvertInternalErrorPayloadsToExternal(
        tokenizer->GetNextToken(&location /* input and output */, &bison_token),
        resume_location->input());
    if (!status.ok()) {
      break;
    }
    bool is_adjacent_to_prior_token =
        previous_location.IsAdjacentlyFollowedBy(location);
    previous_location = location;

    std::string image(absl::ClippedSubstr(
        resume_location->input(), location.start().GetByteOffset(),
        location.end().GetByteOffset() - location.start().GetByteOffset()));
    status = ConvertInternalErrorPayloadsToExternal(
        ConvertBisonToken(bison_token, is_adjacent_to_prior_token, location,
                          std::move(image), tokens),
        resume_location->input());
    if (!status.ok()) {
      break;
    }

    if (options.max_tokens > 0 && tokens->size() >= options.max_tokens) {
      break;
    }
    if (bison_token == Token::EOI) {
      break;
    }
    if (options.stop_at_end_of_statement &&
        tokens->back().kind() == ParseToken::KEYWORD &&
        tokens->back().GetImage().substr(0, 1) == ";") {
      break;
    }
  }
  if (!status.ok() && tokens->empty()) {
    return status;
  }
  // Use the end of the last token returned by the tokenizer as the resume
  // location. We shorten the ";" token in ConvertBisonToken(), so we should
  // NOT use the token position directly from the tokenizer. Instead, we
  // take the position from the last token, which should always exist
  // because even if we have no real tokens, we always include an EOF token.
  GOOGLESQL_RET_CHECK(!tokens->empty());
  resume_location->set_byte_position(
      tokens->back().GetLocationRange().end().GetByteOffset());
  return status;
}

std::string ParseToken::GetKeyword() const {
  if (kind_ == KEYWORD) {
    return absl::AsciiStrToUpper(GetImage());
  } else if (kind_ == IDENTIFIER_OR_KEYWORD) {
    return absl::AsciiStrToUpper(value_.string_value());
  } else {
    return "";
  }
}

std::string ParseToken::GetIdentifier() const {
  if (kind_ == IDENTIFIER || kind_ == IDENTIFIER_OR_KEYWORD) {
    return value_.string_value();
  } else {
    return "";
  }
}

Value ParseToken::GetValue() const {
  if (kind_ == VALUE) {
    return value_;
  } else {
    return Value();  // invalid value
  }
}

std::string ParseToken::GetSQL() const {
  switch (kind_) {
    case KEYWORD:
      return absl::AsciiStrToUpper(GetImage());
    case IDENTIFIER_OR_KEYWORD:
    case IDENTIFIER:
      return ToIdentifierLiteral(value_.string_value());
    case VALUE:
      if (value_.is_valid() && value_.type()->IsFloatingPoint()) {
        // Floating point literals like ".1" and "1." should not be printed as
        // "0.1" and "1.0". For example, "1.abc" should not be printed as
        // "1.0abc".
        return image_;
      }
      return value_.GetSQL();
    case COMMENT:
      return value_.string_value();
    case END_OF_INPUT:
      return "";
  }
}

absl::string_view ParseToken::GetImage() const { return image_; }

std::string ParseToken::DebugString() const {
  std::string kind_str;
  switch (kind_) {
    case KEYWORD:
      kind_str = "KEYWORD";
      break;
    case IDENTIFIER:
      kind_str = "IDENTIFIER";
      break;
    case IDENTIFIER_OR_KEYWORD:
      kind_str = "IDENTIFIER_OR_KEYWORD";
      break;
    case VALUE:
      kind_str = "VALUE";
      break;
    case COMMENT:
      kind_str = "COMMENT";
      break;
    case END_OF_INPUT:
      return "EOF";
  }
  return absl::StrCat(kind_str, ":", GetSQL());
}

ParseToken::ParseToken() : kind_(END_OF_INPUT) {}

ParseToken::ParseToken(ParseLocationRange location_range,
                       bool adjacent_to_prior_token, std::string image,
                       Kind kind)
    : kind_(kind),
      adjacent_to_prior_token_(adjacent_to_prior_token),
      image_(std::move(image)),
      location_range_(location_range) {}

ParseToken::ParseToken(ParseLocationRange location_range,
                       bool adjacent_to_prior_token, std::string image,
                       Kind kind, Value value)
    : kind_(kind),
      adjacent_to_prior_token_(adjacent_to_prior_token),
      image_(std::move(image)),
      location_range_(location_range),
      value_(std::move(value)) {
  ABSL_DCHECK(kind == IDENTIFIER || kind == IDENTIFIER_OR_KEYWORD || kind == VALUE ||
         kind == COMMENT);
  ABSL_DCHECK(!value_.is_null());
}

}  // namespace googlesql
