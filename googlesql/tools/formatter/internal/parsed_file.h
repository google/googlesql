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

#ifndef GOOGLESQL_TOOLS_FORMATTER_INTERNAL_PARSED_FILE_H_
#define GOOGLESQL_TOOLS_FORMATTER_INTERNAL_PARSED_FILE_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/parser/parser.h"
#include "googlesql/public/formatter_options.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/public/parse_resume_location.h"
#include "googlesql/tools/formatter/internal/chunk.h"
#include "googlesql/tools/formatter/internal/token.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace googlesql::formatter::internal {

// Forward declaration for Visitor pattern.
class ParsedFileVisitor;

// Contains a part of input file.
class FilePart {
 public:
  // Creates file part corresponding to the given offsets within `sql`.
  // FilePart stores the reference to `sql` it was created from, so underlying
  // string must outlive this object.
  FilePart(absl::string_view sql, int start_offset, int end_offset);

  virtual ~FilePart() = default;
  // Prints internal representation of the file part. Used for debugging only.
  virtual std::string DebugString() const = 0;
  // Passes itself to the visitor object.
  virtual absl::Status Accept(ParsedFileVisitor* visitor) const = 0;

  virtual bool IsEmpty() const { return start_offset_ == end_offset_; }

  // Returns the entire sql this file part is part of.
  absl::string_view Sql() const { return sql_; }

  // Returns the part of original sql this FilePart corresponds to.
  absl::string_view Image() const;

  // Returns start offset of this file part.
  int StartOffset() const { return start_offset_; }
  // Returns end offset of this file part.
  int EndOffset() const { return end_offset_; }

 private:
  absl::string_view sql_;
  int start_offset_;
  int end_offset_;
};

// Represents an unparsed region of input file.
class UnparsedRegion : public FilePart {
 public:
  using FilePart::FilePart;

  // See documentation in the parent class (FilePart).
  std::string DebugString() const override;
  absl::Status Accept(ParsedFileVisitor* visitor) const override;
};

// Stores all parts of a tokenized sql statement: tokens, chunks and chunk block
// tree.
class TokenizedStmt : public FilePart {
 public:
  // Parses sql using given `parse_location` and `location_translator`.
  static absl::StatusOr<std::unique_ptr<TokenizedStmt>> ParseFrom(
      ParseResumeLocation* parse_location,
      const ParseLocationTranslator& location_translator,
      const FormatterOptions& options);

  // Creates a non-initialized TokenizedStmt object. Use the factory function
  // above instead.
  TokenizedStmt(absl::string_view sql, int start_offset, int end_offset,
                std::vector<Token> tokens, const FormatterOptions& options);

  // Disable copy (and move) semantics.
  TokenizedStmt(const TokenizedStmt&) = delete;
  TokenizedStmt& operator=(const TokenizedStmt&) = delete;

  // Returns all tokens from the current statement.
  const TokensView& Tokens() const { return tokens_view_; }

  // Returns all chunks from the current statement. A chunk is a group of 1 or
  // more tokens that should never split.
  const std::vector<Chunk>& Chunks() const { return chunks_; }

  // Returns the chunk block tree of the current statement. Each block can
  // contain either a single chunk or several child blocks.
  const ChunkBlock* BlockTree() const { return block_factory_.Top(); }

  // Used for testing only. Allows modifying chunks and blocks belonging to the
  // statement.
  ChunkBlock* BlockTree() { return block_factory_.Top(); }

  // Returns false if the current statement should not be formatted and should
  // be left verbatim in the formatter output.
  bool ShouldFormat() const;

  // See documentation in the parent class (FilePart).
  std::string DebugString() const override;
  absl::Status Accept(ParsedFileVisitor* visitor) const override;
  bool IsEmpty() const override { return tokens_.empty(); }

 private:
  // Groups tokens into chunks and chunks into block tree.
  absl::Status BuildChunksAndBlocks(
      const ParseLocationTranslator& location_translator,
      const FormatterOptions& options);

  std::vector<Token> tokens_;
  TokensView tokens_view_;
  std::vector<Chunk> chunks_;
  ChunkBlockFactory block_factory_;
};

// Stores GoogleSQL parsed AST in addition to formatter tokens.
class ParsedStmt : public FilePart {
 public:
  ParsedStmt(std::unique_ptr<TokenizedStmt> tokenized_stmt,
             std::unique_ptr<googlesql::ParserOutput> parser_output)
      : FilePart(*tokenized_stmt),
        tokenized_stmt_(std::move(tokenized_stmt)),
        parser_output_(std::move(parser_output)) {}

  // Returns tokenized statement for the same file part.
  const TokenizedStmt& GetTokenizedStmt() const { return *tokenized_stmt_; }

  // Returns GoogleSQL parsed AST.
  const googlesql::ParserOutput* GetParserOutput() const {
    return parser_output_.get();
  }

  // See documentation in the parent class (FilePart).
  std::string DebugString() const override;
  absl::Status Accept(ParsedFileVisitor* visitor) const override;

 private:
  std::unique_ptr<TokenizedStmt> tokenized_stmt_;
  std::unique_ptr<googlesql::ParserOutput> parser_output_;
};

// Controls which action to do when parsing a file.
enum class ParseAction { kTokenize, kParse };

// Stores entire input file, which might consist of multiple parsed and
// unparsed parts.
class ParsedFile : public FilePart {
 public:
  // Parses the given `sql` into a tree of chunk blocks.
  static absl::StatusOr<std::unique_ptr<ParsedFile>> ParseFrom(
      absl::string_view sql, const FormatterOptions& options,
      ParseAction parse_action = ParseAction::kTokenize) {
    return ParseByteRanges(sql, {}, options, parse_action);
  }
  // Parses given `byte_ranges` of the input `sql` into a tree of chunk blocks.
  // A range is open-ended interval: [start, end), where start and end are
  // 0-based byte offsets.
  static absl::StatusOr<std::unique_ptr<ParsedFile>> ParseByteRanges(
      absl::string_view sql, const std::vector<FormatterRange>& byte_ranges,
      const FormatterOptions& options,
      ParseAction parse_action = ParseAction::kTokenize);
  // Parses given `line_ranges` of the input `sql` into a tree of chunk blocks.
  // Both start and end in each range are inclusive and line indices are
  // 1-based.
  static absl::StatusOr<std::unique_ptr<ParsedFile>> ParseLineRanges(
      absl::string_view sql, const std::vector<FormatterRange>& line_ranges,
      const FormatterOptions& options,
      ParseAction parse_action = ParseAction::kTokenize);

  // Creates a ParsedFile object that owns input sql. Returned object is not
  // fully initialized. Use one of the factory functions above instead.
  explicit ParsedFile(std::string sql, FormatterOptions options)
      : FilePart(sql, 0, static_cast<int>(sql.size())),
        sql_(std::move(sql)),
        location_translator_(sql_),
        options_(std::move(options)) {}

  // Disable copy (and move) semantics.
  ParsedFile(const ParsedFile&) = delete;
  ParsedFile& operator=(const ParsedFile&) = delete;

  // Returns all file parts (parsed and unparsed).
  const std::vector<std::unique_ptr<const FilePart>>& Parts() const {
    return file_parts_;
  }

  // Returns the original sql this ParsedFile is built from.
  absl::string_view Sql() const { return sql_; }

  // Debug string containing all parts of this parsed file.
  std::string DebugString() const override;

  // Traverses all parts of this parsed file with the given `visitor`. Stops if
  // the visitor returns an error.
  absl::Status Accept(ParsedFileVisitor* visitor) const override;

  // Returns parse errors if any occurred during file parsing.
  const std::vector<absl::Status>& GetParseErrors() const {
    return parse_errors_;
  }

  // Returns location translator for the file (converts byte offset into
  // line,column location and back).
  const ParseLocationTranslator& GetLocationTranslator() const {
    return location_translator_;
  }

  // Returns the formatter options used to parse the file.
  const FormatterOptions& GetOptions() const { return options_; }

 private:
  absl::Status ParseByteRanges(std::vector<FormatterRange> byte_ranges,
                               ParseAction parse_action);

  absl::StatusOr<std::unique_ptr<FilePart>> ParseNextFilePart(
      ParseResumeLocation* parse_location, ParseAction parse_action);

  const std::string sql_;
  const ParseLocationTranslator location_translator_;
  std::vector<std::unique_ptr<const FilePart>> file_parts_;
  std::vector<absl::Status> parse_errors_;
  FormatterOptions options_;
};

// Visitor interface for traversing all ParsedFile parts.
// Implementations can override any of the Visit* functions to visit specific
// parts of the file.
class ParsedFileVisitor {
 public:
  virtual ~ParsedFileVisitor() = default;

  virtual absl::Status VisitUnparsedRegion(const UnparsedRegion& file_part) {
    return absl::OkStatus();
  }

  virtual absl::Status VisitTokenizedStmt(const TokenizedStmt& file_part) {
    return absl::OkStatus();
  }

  virtual absl::Status VisitParsedStmt(const ParsedStmt& file_part) {
    return absl::OkStatus();
  }
};

// Sorts the given `byte_ranges` and makes sure that:
// * each range is not empty and within `sql`;
// * there are no overlapping ranges.
absl::StatusOr<std::vector<FormatterRange>> ValidateAndSortByteRanges(
    const std::vector<FormatterRange>& byte_ranges, absl::string_view sql,
    const FormatterOptions& options);

}  // namespace googlesql::formatter::internal

#endif  // GOOGLESQL_TOOLS_FORMATTER_INTERNAL_PARSED_FILE_H_
