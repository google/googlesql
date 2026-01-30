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

#include "googlesql/tools/formatter/internal/layout.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <deque>
#include <iterator>
#include <memory>
#include <ostream>
#include <queue>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/formatter_options.h"
#include "googlesql/tools/formatter/internal/chunk.h"
#include "googlesql/tools/formatter/internal/parsed_file.h"
#include "googlesql/tools/formatter/internal/token.h"
#include "absl/container/btree_set.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/flat_set.h"
#include "googlesql/base/status_macros.h"

namespace googlesql::formatter::internal {

namespace {

// Builds FileLayout from ParsedFile by visiting each part of the
// ParsedFile.
class FileLayoutBuilder : public ParsedFileVisitor {
 public:
  explicit FileLayoutBuilder(FileLayout* layout) : layout_(layout) {}

  absl::Status VisitUnparsedRegion(const UnparsedRegion& file_part) override {
    layout_->AddPart(std::make_unique<UnparsedLayout>(file_part));
    return absl::OkStatus();
  }

  absl::Status VisitTokenizedStmt(const TokenizedStmt& file_part) override {
    if (!file_part.ShouldFormat()) {
      layout_->AddPart(std::make_unique<UnparsedLayout>(file_part));
    } else {
      layout_->AddPart(
          std::make_unique<StmtLayout>(file_part, layout_->FormatterOptions()));
    }
    return absl::OkStatus();
  }

  absl::Status VisitParsedStmt(const ParsedStmt& file_part) override {
    // Formatter is not using GoogleSQL parser AST.
    return VisitTokenizedStmt(file_part.GetTokenizedStmt());
  }

 private:
  FileLayout* layout_;
};

// Function returns locations of the chunks that satisfy all following
// requirements:
// - The chunk is immediate child of the `parent_block`;
// - The chunk position is within given `line`;
// - `qualifier(chunk)` returns true.
template <typename F>
absl::btree_set<int> ChunkPositions(const ChunkBlock& parent_block,
                                    const StmtLayout::Line& line,
                                    F&& qualifier) {
  absl::btree_set<int> chunk_positions;
  for (auto it = parent_block.children().begin();
       it != parent_block.children().end(); ++it) {
    if (!(*it)->IsLeaf()) {
      continue;
    }
    const Chunk& chunk = *(*it)->Chunk();
    if (chunk.PositionInQuery() < line.start ||
        chunk.PositionInQuery() >= line.end) {
      break;
    }
    if (qualifier(chunk)) {
      chunk_positions.insert(chunk.PositionInQuery());
    }
  }
  return chunk_positions;
}

// Finds a position of join condition (ON, USING) or WITH OFFSET corresponding
// to the given JOIN chunk.
int FindJoinConditionPosition(const StmtLayout::Line& line,
                              const Chunk& join_chunk) {
  const ChunkBlock* join_clause =
      join_chunk.ChunkBlock()->FindNextSiblingBlock();
  if (join_clause == nullptr || join_clause->IsLeaf()) {
    return -1;
  }
  for (auto i = join_clause->children().begin();
       i != join_clause->children().end(); ++i) {
    if ((*i)->IsLeaf() &&
        (*i)->Chunk()->FirstToken().Is(Token::Type::JOIN_CONDITION_KEYWORD)) {
      return (*i)->Chunk()->PositionInQuery();
    }
  }
  return -1;
}

// Returns true if the given `chunk` starts an expression inside FROM or JOIN
// clause, e.g., a table name or a subselect.
bool IsExpressionInsideFromOrJoinClause(const Chunk& chunk) {
  // The function tries to locate the first token one level up in the block
  // tree to see if it is "FROM" or "JOIN" clause.
  //  root
  //  |
  //  +-"FROM"
  //  |
  //  +-block for expression inside FROM
  //     |
  //     +-"Table1,"   <- `chunk` can be here,
  //     |
  //     +-"Table2"    <- or there.
  const ChunkBlock* block = chunk.ChunkBlock()->Parent();
  if (block->IsTopLevel()) {
    return false;
  }
  block = block->FindPreviousSiblingBlock();
  while (block != nullptr && block->IsLeaf() &&
         block->Chunk()->IsCommentOnly()) {
    block = block->FindPreviousSiblingBlock();
  }
  return block != nullptr && block->IsLeaf() &&
         (block->Chunk()->IsFromKeyword() || block->Chunk()->IsJoin());
}

bool ShouldAddABlankLineNearComment(bool is_between_statements,
                                    const Chunk& prev_chunk,
                                    const Chunk& curr_chunk) {
  // 1. Preserve blank line before the comment if it existed in the original
  // query, e.g.:
  //     SELECT
  //     <blank line>
  //       -- comment
  //       -- block
  //       1;
  if (curr_chunk.IsCommentOnly() && !prev_chunk.IsCommentOnly()) {
    return curr_chunk.GetLineBreaksBefore() > 1;
  }

  // 2. Preserve blank line after comment between statements if it existed in
  // the original input, e.g.:
  //    ... previous statements ...
  //    -- comment
  //    <blank line>
  //    SELECT 1;
  if (is_between_statements && prev_chunk.IsCommentOnly() &&
      prev_chunk.GetLineBreaksAfter() > 1) {
    return true;
  }
  return false;
}

}  // namespace

std::ostream& operator<<(std::ostream& os, const StmtLayout::Line& l) {
  return os << '{' << l.start << ',' << l.end << '}';
}

StmtLayout::StmtLayout(const TokenizedStmt& parsed_sql,
                       const FormatterOptions& options)
    : chunks_(parsed_sql.Chunks()), options_(options) {
  // Explicit cast is ok, because if we have more than 2^32 chunks,
  // nothing will work anyhow.
  lines_.insert(Line(0, static_cast<int>(chunks_.size())));
}

bool StmtLayout::Empty() const { return chunks_.empty(); }

const Chunk& StmtLayout::ChunkAt(int index) const {
  // In debug mode: crash with readable message if the index is out of bounds.
  ABSL_DCHECK_GE(index, 0);
  ABSL_DCHECK_LT(index, chunks_.size());
  // Not in debug mode: try to recover.
  if (index < 0) {
    return chunks_.front();
  } else if (index >= chunks_.size()) {
    return chunks_.back();
  }
  return chunks_[index];
}

std::string StmtLayout::PrintableString() const {
  std::string result;

  if (Empty()) {
    return result;
  }

  absl::btree_set<int> newlines;
  for (auto&& line : lines_) {
    newlines.insert(line.end);
  }

  auto next_line_break = newlines.cbegin();
  result.append(chunks_[0].PrintableString(options_, 0, *next_line_break == 1));
  // Track comments that appear between statements.
  bool is_between_statements = chunks_[0].IsCommentOnly();
  for (int i = 1; i < chunks_.size(); ++i) {
    int spaces = 0;
    if (i == *next_line_break) {
      ++next_line_break;
      result.append(options_.NewLineType());
      if (ShouldAddABlankLineNearComment(is_between_statements, chunks_[i - 1],
                                         chunks_[i])) {
        result.append(options_.NewLineType());
      }
      spaces = chunks_[i].ChunkBlock()->Level() %
               // There is no sense to make indent > than the line length limit.
               // Instead we start from the beginning as if an editor wrapped
               // the line at the line length.
               options_.LineLengthLimit();
      result.append(std::string(spaces, ' '));
    } else if (chunks_[i].StartsWithSpace()) {
      result.append(" ");
    }
    result.append(chunks_[i].PrintableString(options_, spaces,
                                             i + 1 == *next_line_break));
    if (is_between_statements && !chunks_[i].IsCommentOnly()) {
      is_between_statements = false;
    }
  }
  result.append(options_.NewLineType());

  return result;
}

std::string StmtLayout::PrintableString(const Line& line) const {
  std::string result;
  for (int i = line.start; i < line.end; ++i) {
    if (chunks_[i].StartsWithSpace()) {
      result.append(" ");
    }
    absl::StrAppend(&result,
                    chunks_[i].PrintableString(options_, 1, i + 1 == line.end));
  }
  return result;
}

absl::string_view StmtLayout::FirstKeyword() const {
  for (const auto& chunk : chunks_) {
    if (!chunk.IsCommentOnly()) {
      return chunk.FirstKeyword();
    }
  }
  return "";
}

bool StmtLayout::ContainsBlankLinesAtTheStart() const {
  for (const auto& chunk : chunks_) {
    if (!chunk.IsCommentOnly()) {
      return false;
    }
    if (chunk.GetLineBreaksAfter() > 1) {
      return true;
    }
  }

  // All chunks are comments, so there is no actual statement in this layout.
  // This happens if there is a trailing comment line in the input. Pretend we
  // have empty line before the statement in this layout to allow trailing
  // comments preserve (no) blank lines before them.
  return true;
}

bool StmtLayout::IsMultilineStatement() const {
  // Count how many non-comment lines are in this layout.
  int non_comment_lines = 0;
  for (const auto& line : lines_) {
    if (!ChunkAt(line.start).IsCommentOnly() && ++non_comment_lines > 1) {
      return true;
    }
  }
  // Search for a multiline non-comment token.
  for (const auto& chunk : chunks_) {
    if (!chunk.IsCommentOnly() && chunk.IsMultiline()) {
      return true;
    }
  }
  return false;
}

bool StmtLayout::RequiresBlankLineBefore() const {
  // If the user already had a blank line before the statement, preserve it,
  // except for IMPORT statements.
  if (FirstKeyword() != "IMPORT" && ChunkAt(0).GetLineBreaksBefore() > 1) {
    return true;
  }
  // Already has a blank line before first non-comment line.
  if (ContainsBlankLinesAtTheStart()) {
    return false;
  }
  // Always require a blank line in front of SELECT.
  if (FirstKeyword() == "SELECT") {
    return true;
  }
  // Require a blank line before a multiline statement, except for SET
  // statements.  (This allows for compact configuration; note that SET bodies
  // are indented by 2.)
  if (IsMultilineStatement() && FirstKeyword() != "SET") {
    return true;
  }

  return false;
}

bool StmtLayout::ForbidsAddingBlankLineBefore() const {
  // Starts with a comment, which didn't have an empty line before in the
  // original query.
  if (ChunkAt(0).IsCommentOnly() && ChunkAt(0).GetLineBreaksBefore() <= 1) {
    // ..and already has a blank line before the actual statement, so no extra
    // lines required.
    if (ContainsBlankLinesAtTheStart()) {
      return true;
    }
  }

  return false;
}

bool StmtLayout::RequiresBlankLineAfter() const {
  return IsMultilineStatement() && FirstKeyword() != "SET";
}

StmtLayout::Line StmtLayout::NewLine(int start, int end) const {
  const absl::Status status = ValidateLine(&start, &end);
  // Crash in debug mode to surface the error.
  GOOGLESQL_DCHECK_OK(status);
  status.IgnoreError();
  return Line(start, end);
}

absl::Status StmtLayout::ValidateLine(int* start, int* end) const {
  std::vector<std::string> errors;
  const int chunks_size = static_cast<int>(chunks_.size());
  if (*start >= chunks_size) {
    errors.push_back(
        absl::StrFormat("start is too big (%d > %d)", *start, chunks_size));
    *start = chunks_size - 1;
  }
  if (*start < 0) {
    errors.push_back(absl::StrFormat("start is negative (%d)", *start));
    *start = 0;
  }
  if (*end > chunks_size) {
    errors.push_back(
        absl::StrFormat("end is too big (%d > %d)", *end, chunks_size));
    *end = chunks_size;
  } else if (*start >= *end) {
    errors.push_back(absl::StrFormat("start >= end (%d >= %d)", *start, *end));
    *end = *start + 1;
  }
  if (errors.empty()) {
    return absl::OkStatus();
  }
  return absl::InvalidArgumentError(
      absl::StrCat("Invalid line: ", absl::StrJoin(errors, "; ")));
}

int StmtLayout::PrintableLineLength(const Line& line) const {
  int i = line.start;
  int line_length = chunks_[i].ChunkBlock()->Level();
  line_length += chunks_[i].PrintableLength(i + 1 == line.end);

  while (++i < line.end) {
    if (chunks_[i].StartsWithSpace()) {
      line_length += 1;
    }
    line_length += chunks_[i].PrintableLength(i + 1 == line.end);
  }

  return line_length;
}

int StmtLayout::FirstChunkEndingAfterColumn(const Line& line,
                                            int column) const {
  // This function could be sped up and made O(1) if we use a pre-computed table
  // of distances from the start. But one would need to be super careful when
  // pre-computing because comments can include their own newline characters and
  // could throw this computation off. For now, the current implementation
  // formats 36MB (200k lines) of SQL in 26 seconds on a standard workstation,
  // so optimization is not needed yet.
  int i = line.start;
  int line_length = chunks_[i].ChunkBlock()->Level();
  line_length += chunks_[i].PrintableLength(i + 1 == line.end);

  while (line_length <= column && ++i < line.end) {
    if (chunks_[i].StartsWithSpace()) {
      line_length += 1;
    }
    line_length += chunks_[i].PrintableLength(i + 1 == line.end);
  }

  return i;
}

bool StmtLayout::IsLineLengthOverLimit(const Line& line) const {
  return FirstChunkEndingAfterColumn(line, options_.LineLengthLimit()) <
         line.end;
}

bool StmtLayout::ContainsLineBreaksInInput(const Line& line) const {
  for (int i = line.start + 1; i < line.end; ++i) {
    if (ChunkAt(i).GetLineBreaksBefore() > 0) {
      return true;
    }
  }
  return false;
}

absl::btree_set<int> StmtLayout::LineBreaksInInput(const Line& line) const {
  absl::btree_set<int> line_breaks;
  auto hint = line_breaks.cbegin();
  for (int i = line.start + 1; i < line.end; ++i) {
    if (ChunkAt(i).GetLineBreaksBefore() > 0) {
      hint = line_breaks.insert(hint, i);
    }
  }
  return line_breaks;
}

absl::StatusOr<std::unique_ptr<FileLayout>> FileLayout::FromParsedFile(
    const ParsedFile& file) {
  auto layout = std::make_unique<FileLayout>(file.GetOptions());
  FileLayoutBuilder layout_builder(layout.get());
  GOOGLESQL_RETURN_IF_ERROR(file.Accept(&layout_builder));
  return layout;
}

void FileLayout::AddPart(std::unique_ptr<Layout> layout_part) {
  layout_parts_.emplace_back(std::move(layout_part));
}

std::string FileLayout::PrintableString() const {
  std::string result;
  const Layout* previous = nullptr;
  for (const auto& layout : layout_parts_) {
    if (!layout->ForbidsAddingBlankLineBefore()) {
      if (previous != nullptr && !previous->ForbidsAddingBlankLineAfter()) {
        if (layout->RequiresBlankLineBefore() ||
            previous->RequiresBlankLineAfter()) {
          absl::StrAppend(&result, options_.NewLineType());
        } else {
          // This prevents adding a new line between:
          //
          //   $GRANT_TEAM_ACCESS(to_one_thing);
          //   $GRANT_CLIENTS_ACCESS(to_another_thing);
          //
          bool both_start_with_macro =
              absl::StartsWith(layout->FirstKeyword(), "$") &&
              absl::StartsWith(previous->FirstKeyword(), "$");
          if (layout->FirstKeyword() != previous->FirstKeyword() &&
              !both_start_with_macro) {
            absl::StrAppend(&result, options_.NewLineType());
          }
        }
      }
    }
    absl::StrAppend(&result, layout->PrintableString());
    previous = layout.get();
  }
  if (!result.empty() && !absl::EndsWith(result, options_.NewLineType())) {
    absl::StrAppend(&result, options_.NewLineType());
  }
  return result;
}

void FileLayout::BestLayout() {
  for (const auto& layout : Parts()) {
    layout->BestLayout();
  }
}

void StmtLayout::BestLayout() {
  if (Empty()) {
    return;
  }
  BreakComments();
  BreakOnMandatoryLineBreaks();
  BreakAllLongLines();
  if (options_.IsPreservingExistingLineBreaks()) {
    BreakLinesThatWereSplitByUser();
  }
  PruneLineBreaks();
}

void StmtLayout::BreakComments() {
  LinesT new_lines;

  for (auto line = Lines().begin(); line != Lines().end(); ++line) {
    absl::btree_set<int> breakpoints;

    for (int i = line->start; i < line->end; ++i) {
      // Add a line break before the comment chunk.
      if (i != line->start && ChunkAt(i).IsCommentOnly()) {
        breakpoints.insert(i);
      }
      // Add a line break after a comment, if there was a break in the original
      // input.
      if (i + 1 < line->end && ChunkAt(i).EndsWithComment() &&
          ChunkAt(i).GetLineBreaksAfter() > 0) {
        breakpoints.insert(i + 1);
      }
    }

    // If the comment lines appear in the middle of a query, make sure we add
    // consistent line breaks to the rest of the query. For instance:
    //   SELECT 1,
    //     -- comment
    //     2, 3 FROM t;
    // should become:
    //   SELECT
    //     1,
    //     --comment,
    //     2,
    //     3
    //   FROM t;
    new_lines.merge(ConsistentLinesForBreakpoints(*line, breakpoints));
  }

  SetLines(new_lines);
}

void StmtLayout::BreakOnMandatoryLineBreaks() {
  LinesT new_lines;

  std::queue<Line> queue;

  for (auto line = Lines().begin(); line != Lines().end(); ++line) {
    queue.push(*line);
  }

  while (!queue.empty()) {
    const Line line = queue.front();
    queue.pop();

    if (line.LengthInChunks() < 2) {
      // Line cannot be broken any further.
      new_lines.insert(line);
      continue;
    }

    absl::btree_set<int> breakpoints;
    for (int i = line.start; i < line.end; ++i) {
      const Chunk& chunk = ChunkAt(i);
      if (chunk.FirstKeyword() == "AS" &&
          chunk.FirstToken().Is(Token::Type::TOP_LEVEL_KEYWORD)) {
        // Top-level `AS` keywords always start on their own line.  (This does
        // *not* apply for `AS` within a query body, e.g. introducing a WITH
        // table definition.)
        if (i > line.start) {
          breakpoints.insert(i);
        }
        if (i + 1 < line.end) {
          // Add line break after top-level `AS` keywords as well.
          breakpoints.insert(i + 1);
        }
      } else if ((chunk.IsStartOfCreateStatement() ||
                  chunk.IsStartOfExportDataStatement()) &&
                 i + 1 < line.end &&
                 // Ignore constants and function signatures with (possibly
                 // empty) argument lists.
                 chunk.LastKeyword() != "=" &&
                 !chunk.OpensParenOrBracketBlock() &&
                 chunk.LastKeyword() != ")") {
        // For CREATE TABLE / EXPORT DATA statements, always add a new line
        // after the table name, even if the entire statement fits on one line.
        breakpoints.insert(i + 1);
      } else if (chunk.LastKeyword() == "CASE" && i + 1 < line.end) {
        // If there is more than one WHEN clause inside one CASE expression,
        // each WHEN should be on a separate line.
        const ChunkBlock* case_block =
            chunk.ChunkBlock()->FindNextSiblingBlock();
        if (case_block == nullptr) {
          // No CASE body block - most probably a malformed query.
          continue;
        }
        absl::btree_set<int> whens = ChunkPositions(
            *case_block, line,
            [](const Chunk& chunk) { return chunk.FirstKeyword() == "WHEN"; });
        if (whens.size() > 1) {
          breakpoints.merge(whens);
          break;
        }
      } else if (chunk.FirstToken().Is(
                     Token::Type::CHAINED_METHOD_DOT_SEPARATOR) &&
                 IsLineLengthOverLimit(line)) {
        // If the line length is over the limit break the line before each
        // method call in the chain.
        const ChunkBlock* parent_block = chunk.ChunkBlock()->Parent();
        // Find all chained method calls within the same parent block.
        absl::btree_set<int> chained_calls =
            ChunkPositions(*parent_block, line, [](const Chunk& chunk) {
              return chunk.FirstToken().Is(
                  Token::Type::CHAINED_METHOD_DOT_SEPARATOR);
            });
        breakpoints.merge(chained_calls);
        break;
      } else if ((chunk.IsTopLevelWith() || chunk.FirstKeyword() == "WINDOW") &&
                 i + 1 < line.end) {
        // If there is more than one table in WITH clause (or more than one
        // window), each table should be on a separate line.
        const ChunkBlock* with_block =
            chunk.ChunkBlock()->FindNextSiblingBlock();
        if (with_block == nullptr) {
          // No WITH body block - most probably a malformed query.
          continue;
        }
        absl::btree_set<int> commas = ChunkPositions(
            *with_block, line,
            [](const Chunk& chunk) { return chunk.LastKeyword() == ","; });

        if (!commas.empty()) {
          for (int comma : commas) {
            // Break the line after commas.
            if (comma + 1 < line.end) {
              breakpoints.insert(comma + 1);
            }
          }
          break;
        }
      } else if (i > line.start + 1 && i < line.end - 1 &&
                 ChunkAt(i - 1).LastKeyword() == "(" &&
                 chunk.FirstKeyword() == "(" &&
                 chunk.Tokens().WithoutComments().size() == 1 &&
                 ChunkAt(i + 1).IsStartOfNewQuery()) {
        // Whenever we have `((SELECT` or `((WITH`, add a mandatory line break
        // between the double parenthesis.  Sometimes, double parentheses are
        // used to explicitly separate a function call parenthesis from a code
        // block parenthesis, e.g.:
        //
        //   EXISTS(
        //     (
        //       SELECT * FROM Foo
        //     ))
        //
        // In this case, we do not want to put everything on one line like:
        //
        //   EXISTS((SELECT * FROM Foo))
        //
        // If the user wanted it on one line, they would have only used a single
        // parenthesis.
        breakpoints.insert(i);
      } else if (chunk.Tokens().WithoutComments().size() > 1 &&
                 chunk.Tokens().WithoutComments()[1]->Is(
                     Token::Type::ASSIGNMENT_OPERATOR)) {
        // If there is more than one option in OPTIONS clause, each option
        // should be on a separate line.
        const ChunkBlock& parent_block = *chunk.ChunkBlock()->Parent();
        const ChunkBlock* prev_sibling_block =
            parent_block.FindPreviousSiblingBlock();
        if (prev_sibling_block != nullptr && prev_sibling_block->IsLeaf() &&
            prev_sibling_block->Chunk()->FirstKeyword() == "RUN") {
          // RUN statement uses same syntax as OPTIONS clause, but unlike
          // OPTIONS it doesn't require each parameter to be on a separate line.
          break;
        }
        absl::btree_set<int> options =
            ChunkPositions(parent_block, line, [](const Chunk& chunk) {
              return chunk.Tokens().WithoutComments().size() > 1 &&
                     chunk.Tokens().WithoutComments()[1]->Is(
                         Token::Type::ASSIGNMENT_OPERATOR);
            });
        if (options.size() > 1) {
          breakpoints.merge(options);
          if (prev_sibling_block != nullptr && prev_sibling_block->IsLeaf() &&
              prev_sibling_block->Chunk()->LastKeywordsAre("OPTIONS", "(")) {
            breakpoints.insert(prev_sibling_block->Chunk()->PositionInQuery());
          }
          break;
        }
      } else if (chunk.IsStartOfDefineMacroStatement()) {
        // If a macro body is bigger than a single token, it should go to the
        // new line.
        const int macro_size = line.end - i - 1;
        if (macro_size > 2 ||
            // If macro body is in parentheses, then the second chunk of the
            // macro body should be closing parenthesis.
            (macro_size == 2 && !ChunkAt(i + 2).ClosesParenOrBracketBlock())) {
          breakpoints.insert(i + 1);
          break;
        }
      } else if (chunk.FirstKeyword() == "|>") {
        // Always add line breaks before the pipe operator '|>'.
        absl::btree_set<int> pipes = ChunkPositions(
            *chunk.ChunkBlock()->Parent(), line,
            [](const Chunk& chunk) { return chunk.FirstKeyword() == "|>"; });
        if (!pipes.empty()) {
          breakpoints.merge(pipes);
          break;
        }
      }
    }

    if (!breakpoints.empty() && *breakpoints.begin() == line.start) {
      breakpoints.erase(breakpoints.begin());
    }
    if (breakpoints.empty()) {
      // Line is not broken, add it to the result as is.
      new_lines.insert(line);
    } else {
      // Break line on found breakpoints.
      const LinesT lines = ConsistentLinesForBreakpoints(line, breakpoints);
      if (lines.size() == 1) {
        // Line was not split for some reason.
        new_lines.insert(*lines.begin());
      } else {
        for (auto l : lines) {
          queue.push(l);
        }
      }
    }
    breakpoints.clear();
  }

  SetLines(new_lines);
}

void StmtLayout::BreakAllLongLines() {
  LinesT new_lines;

  std::queue<Line> queue;

  for (auto line = Lines().begin(); line != Lines().end(); ++line) {
    if (IsLineLengthOverLimit(*line)) {
      queue.push(*line);
    } else {
      new_lines.insert(*line);
    }
  }

  while (!queue.empty()) {
    const Line line = queue.front();
    queue.pop();

    LinesT lines_after_break = BreakOneLine(line);

    // If the break returns a single line, that means it did not get broken
    // and cannot be.
    if (lines_after_break.size() <= 1) {
      new_lines.merge(lines_after_break);
      continue;
    }

    for (auto& line : lines_after_break) {
      if (IsLineLengthOverLimit(line)) {
        queue.push(line);
      } else {
        new_lines.insert(line);
      }
    }
  }

  SetLines(new_lines);
}

void StmtLayout::BreakLinesThatWereSplitByUser() {
  LinesT new_lines;

  std::queue<Line> queue;

  for (auto& line : Lines()) {
    if (ContainsLineBreaksInInput(line) &&
        !ChunkAt(line.start).ChunkBlock()->BreakCloseToLineLength()) {
      queue.push(line);
    } else {
      new_lines.insert(line);
    }
  }

  while (!queue.empty()) {
    const Line line = queue.front();
    queue.pop();

    if (line.LengthInChunks() <= 1) {
      new_lines.insert(line);
      continue;
    }

    absl::btree_set<int> breakpoints = Breakpoints(line);
    if (breakpoints.empty()) {
      // Cannot break the line any further.
      new_lines.insert(line);
      continue;
    }

    absl::btree_set<int> user_breakpoints = LineBreaksInInput(line);
    if (user_breakpoints.empty()) {
      // Should not normally happen.
      new_lines.insert(line);
      continue;
    }

    bool keep_breakpoints = false;
    while (breakpoints.size() <= (user_breakpoints.size() * 2)) {
      if (FormatterBreakpointsRoughlyMatchUsers(line, user_breakpoints,
                                                breakpoints)) {
        keep_breakpoints = true;
        break;
      }
      // Try to search for more breakpoints.
      Line line_to_break = FormatterLineWithMostMissingBreakpoints(
          line, user_breakpoints, breakpoints);
      if (line_to_break == line) {
        break;
      }
      if (line_to_break.LengthInChunks() <= 1) {
        break;
      }
      absl::btree_set<int> more_breakpoints = Breakpoints(line_to_break);
      if (more_breakpoints.empty()) {
        break;
      }
      breakpoints.merge(more_breakpoints);
    }

    if (keep_breakpoints) {
      // Keep the new breakpoints.
      for (auto& l : LinesFromSetOfBreakpoints(line, breakpoints)) {
        if (ContainsLineBreaksInInput(l)) {
          queue.push(l);
        } else {
          new_lines.insert(l);
        }
      }
    } else {
      // Discard the new breakpoints.
      new_lines.insert(line);
    }
  }

  SetLines(new_lines);
}

bool StmtLayout::FormatterBreakpointsRoughlyMatchUsers(
    const Line& line, const absl::btree_set<int>& user_breakpoints,
    const absl::btree_set<int>& formatter_breakpoints) const {
  int matched_breakpoints = 0;
  for (auto break_point : formatter_breakpoints) {
    if (user_breakpoints.contains(break_point)) {
      ++matched_breakpoints;
    }
  }

  // Special cases for hadling line breaks around parentheses (or brackets).

  // 1. User has line break after open parenthesis, but formatter breaks
  // before - accept new `formatter_breaks`. E.g.:
  // user:         original formatter:   with new `formatter_breaks`:
  // SELECT Foo(   SELECT Foo(1)         SELECT
  //   1)                                  Foo(1)
  if (matched_breakpoints == 0 && formatter_breakpoints.size() == 1 &&
      *formatter_breakpoints.cbegin() + 1 == *user_breakpoints.cbegin() &&
      ChunkAt(*formatter_breakpoints.cbegin()).OpensParenOrBracketBlock()) {
    return true;
  }

  if (matched_breakpoints < 2 &&
      *formatter_breakpoints.cbegin() == line.start + 1 &&
      IsOpenParenOrBracket(ChunkAt(line.start).FirstKeyword())) {
    // 2. Formatter places open and close parenthesis on separate lines, but
    // user did not - discard new `formatter_breaks`. E.g.:
    // user:        before `formatter_breaks`   with `formatter_breaks`:
    // SELECT (     SELECT                      SELECT
    //   SELECT 1)    (SELECT 1)                  (
    //                                              SELECT 1
    //                                            )
    if (formatter_breakpoints.size() > 1 &&
        ChunkAt(*formatter_breakpoints.crbegin()).ClosesParenOrBracketBlock()) {
      return false;
    }
    // 3. User places extra line break before (and may be after) one line
    // expression. Preserving first line break will result in unnecessary
    // dangling parenthesis - discard new `formatter_breaks`. E.g.:
    // user:        before `formatter_breaks`   with `formatter_breaks`:
    // SELECT (     SELECT                      SELECT
    //   1            (1)                         (
    // )                                            1)
    if (*formatter_breakpoints.cbegin() == *user_breakpoints.cbegin() &&
        (user_breakpoints.size() == 1 ||
         (ChunkAt(line.start).HasMatchingClosingChunk() &&
          ChunkAt(line.start).MatchingClosingChunk()->PositionInQuery() <=
              *++user_breakpoints.cbegin()))) {
      return false;
    }
  }

  // Default logic: all formatter breakpoints should match with the user's, no
  // matter if the user has more - we'll try to search for them in the next
  // iteration.
  return matched_breakpoints == formatter_breakpoints.size() ||
         // .. or at least 2/3 of breakpoints match.
         std::round(matched_breakpoints * 1.5) >=
             std::max(formatter_breakpoints.size(), user_breakpoints.size());
}

StmtLayout::Line StmtLayout::FormatterLineWithMostMissingBreakpoints(
    const StmtLayout::Line& line, const absl::btree_set<int>& user_breakpoints,
    const absl::btree_set<int>& formatter_breakpoints) const {
  int max_count = 0;
  StmtLayout::Line best_line = line;
  auto u = user_breakpoints.begin();
  auto f = formatter_breakpoints.begin();
  // Skip breapoints that are before the line start.
  while (u != user_breakpoints.end() && *u <= line.start) {
    ++u;
  }
  while (f != formatter_breakpoints.end() && *f <= line.start) {
    ++f;
  }
  if (u == user_breakpoints.end() || f == formatter_breakpoints.end()) {
    return best_line;
  }
  StmtLayout::Line current_line = NewLine(line.start, *f);
  while (u != user_breakpoints.end()) {
    int count = 0;
    if (*u == current_line.start) {
      ++u;
    }
    while (u != user_breakpoints.end() && *u < current_line.end) {
      ++count;
      ++u;
    }
    if (count > max_count) {
      max_count = count;
      best_line = current_line;
    }
    if (f == formatter_breakpoints.end() || *f >= line.end) {
      break;
    }
    ++f;
    current_line = NewLine(
        current_line.end,
        f == formatter_breakpoints.end() || *f > line.end ? line.end : *f);
  }
  return best_line;
}

void StmtLayout::PruneLineBreaks() {
  if (Lines().size() < 2) {
    return;
  }
  absl::btree_set<Line> ordered_lines;
  for (const auto& l : Lines()) {
    ordered_lines.insert(l);
  }

  LinesT pruned_lines;
  auto prev_line = ordered_lines.cbegin();
  bool try_merging_line = false;
  for (auto curr_line = ++ordered_lines.cbegin();
       curr_line != ordered_lines.cend(); ++curr_line, ++prev_line) {
    const Chunk& first_chunk = ChunkAt(curr_line->start);
    try_merging_line = false;
    // Formatter eagerly places 'AS smth' on a new line. Sometimes, this line
    // break can be removed.
    if (first_chunk.FirstKeyword() == "AS" &&
        !first_chunk.FirstToken().IsOneOf(
            {Token::Type::TOP_LEVEL_KEYWORD, Token::Type::CAST_AS})) {
      // Remove line break if user input didn't have it.
      if (options_.IsPreservingExistingLineBreaks() &&
          first_chunk.FirstToken().GetLineBreaksBefore() == 0) {
        try_merging_line = true;
      }
    }
    // Remove line break if without it the line remains the same length. E.g.:
    //  before:         after:
    //    ...             ...
    //  )               ) AS smth;
    //    AS smth;
    // The rule doesn't apply for map constructors: removing line break after {
    // keeps the indent for the next line, but that would also require removing
    // the line break before closing bracket, which increases the line length.
    // Example:
    // before:  {         # Removing line break here wouldn't change the
    //            a: 1    # indent for "a: 1".
    //          }
    // after:   { a: 1 }  # But it increases line length because of "}".
    //
    if (prev_line->LengthInChunks() == 1 &&
        !ChunkAt(prev_line->start)
             .LastToken()
             .Is(Token::Type::BRACED_CONSTR_OPEN_BRACKET)) {
      const Chunk& prev_chunk = ChunkAt(prev_line->start);
      const int curr_line_level = first_chunk.ChunkBlock()->Level();
      const int prev_line_level = prev_chunk.ChunkBlock()->Level();
      const int next_line_level =
          curr_line->end < chunks_.size()
              ? ChunkAt(curr_line->end).ChunkBlock()->Level()
              : 0;

      if (curr_line_level > prev_line_level &&
          // Next line should have smaller indent, otherwise we create indent
          // jump by 4.
          (curr_line_level >= next_line_level ||
           // We allow jump by 5 after the pipe operator, e.g.:
           // |> WHERE
           //      condition = FALSE
           prev_chunk.FirstKeyword() == "|>")) {
        size_t prev_chunk_length = prev_chunk.PrintableLength(false);
        if (first_chunk.StartsWithSpace()) {
          ++prev_chunk_length;
        }
        if (prev_chunk_length == (curr_line_level - prev_line_level)) {
          try_merging_line = true;
        }
      }
    }
    if (first_chunk.FirstKeyword() == "OPTIONS" &&
        prev_line->LengthInChunks() == 1) {
      // Remove line break in the following limited case:
      //
      // before:        after:
      //   ...            ...
      // )              ) OPTIONS (
      // OPTIONS (        ...
      //   ...
      //
      // This is only allowed in very limited situations where:
      // (1) prev line only has ')' and nothing else
      // (2) ')' and 'OPTIONS' are at same level (so removing line break will
      //     not cause an indent-by-4)
      const Chunk& prev_chunk = ChunkAt(prev_line->start);
      size_t prev_chunk_length = prev_chunk.DebugString().length();
      const int curr_line_level = first_chunk.ChunkBlock()->Level();
      const int prev_line_level = prev_chunk.ChunkBlock()->Level();
      if (prev_chunk.ClosesParenBlock() && prev_chunk_length == 1 &&
          curr_line_level == prev_line_level) {
        try_merging_line = true;
      }
    }

    if (try_merging_line) {
      Line merged_line = NewLine(prev_line->start, curr_line->end);
      const int merged_line_length = PrintableLineLength(merged_line);
      if (merged_line_length <= options_.LineLengthLimit() ||
          merged_line_length <= PrintableLineLength(*curr_line)) {
        pruned_lines.insert(merged_line);
        // Skip merged line.
        ++prev_line;
        if (++curr_line == ordered_lines.end()) {
          // We merged the last line - adjust prev_line once more to point to
          // `ordered_lines.end()` so that we skip it after the loop.
          ++prev_line;
          break;
        }
        continue;
      }
    }

    pruned_lines.insert(*prev_line);
  }

  if (prev_line != ordered_lines.end()) {
    pruned_lines.insert(*prev_line);
  }
  SetLines(pruned_lines);
}

StmtLayout::LinesT StmtLayout::ConsistentLinesForBreakpoints(
    const Line& line, const absl::btree_set<int>& breakpoints) const {
  LinesT new_lines;

  std::queue<std::pair<const Line, const absl::btree_set<int>>> queue;
  queue.emplace(std::make_pair(line, breakpoints));

  while (!queue.empty()) {
    const Line current_line = queue.front().first;
    const absl::btree_set<int> current_breakpoints = queue.front().second;
    queue.pop();

    if (current_breakpoints.empty()) {
      new_lines.emplace(current_line);
      continue;
    }

    LinesT expected_lines = BreakOneLine(current_line);
    if (expected_lines.size() <= 1) {
      // The line cannot be broken any further. Just split on breakpoints we
      // had.
      new_lines.merge(
          LinesFromSetOfBreakpoints(current_line, current_breakpoints));
      continue;
    }
    if (ChunkAt(expected_lines.begin()->start)
            .ChunkBlock()
            ->BreakCloseToLineLength()) {
      // Formatter breaks this line close to line length (aka, places as much
      // elements on a single line as possible). So we can get unlucky and
      // formatter will break the line where we want only when it places each
      // element on a line of its own. Instead we break the line where we want
      // it to be broken and let the formatter to break the resulting lines
      // close to line length later.
      new_lines.merge(
          LinesFromSetOfBreakpoints(current_line, current_breakpoints));
      continue;
    }
    for (auto&& expected_line : expected_lines) {
      absl::btree_set<int> breakpoints_in_line;
      for (auto breakpoint : current_breakpoints) {
        // Note that here we don't use ">=" because if breakpoint ==
        // line.start, then that breakpoint is already "solved" by this line.
        if (breakpoint > expected_line.start &&
            breakpoint < expected_line.end) {
          breakpoints_in_line.insert(breakpoint);
        }
      }
      queue.emplace(std::make_pair(expected_line, breakpoints_in_line));
    }
  }

  return new_lines;
}

StmtLayout::LinesT StmtLayout::LinesFromSetOfBreakpoints(
    const Line& line, const absl::btree_set<int>& breakpoints) const {
  LinesT lines;

  int break_point = line.start;
  for (auto&& b : breakpoints) {
    if (b <= break_point) {
      // This should not normally happen and indicates a malformed query.
      // Skip this breakpoint, there might be valid ones later.
      continue;
    }
    if (b >= line.end) {
      // This should not normally happen and indicates a malformed query.
      // Stop here to avoid producing invalid lines.
      break;
    }
    lines.emplace(NewLine(break_point, b));
    break_point = b;
  }
  lines.emplace(NewLine(break_point, line.end));

  return lines;
}

StmtLayout::LinesT StmtLayout::BreakOneLine(const Line& line) const {
  LinesT new_lines;

  if (line.LengthInChunks() == 0) {
    // Empty line; nothing to do.
    return new_lines;
  }
  // The line is a single chunk. We have nowhere to break it, so we just
  // return the line as is.
  if (line.LengthInChunks() == 1) {
    new_lines.insert(line);
    return new_lines;
  }

  absl::btree_set<int> breakpoints = Breakpoints(line);

  if (breakpoints.empty()) {
    new_lines.insert(line);
    return new_lines;
  }

  return LinesFromSetOfBreakpoints(line, breakpoints);
}

absl::btree_set<int> StmtLayout::Breakpoints(const Line& line) const {
  int break_point = BestBreakpoint(line);
  absl::btree_set<int> breakpoints;

  if (ShouldBreakCloseToLineLength(line, break_point)) {
    breakpoints = BreakpointsCloseToLineLength(
        line, ChunkAt(break_point).ChunkBlock()->Level());
  } else if (ShouldIncludeSiblings(line, break_point)) {
    breakpoints = FindSiblingBreakpoints(line, break_point);
  } else {
    breakpoints.insert(break_point);
  }

  // Remove breakpoints after chunks that should always stay with the next ones.
  for (auto it = breakpoints.begin(); it != breakpoints.end();) {
    if (*it == 0 || ChunkAt(*it - 1).ShouldNeverBeFollowedByNewline()) {
      it = breakpoints.erase(it);
    } else {
      ++it;
    }
  }

  return breakpoints;
}

int StmtLayout::BestBreakpoint(const Line& line) const {
  // If the line starts with a comment (not end-of-line and not in-line) - break
  // the line immediately after.
  if (ChunkAt(line.start).IsCommentOnly()) {
    return line.start + 1;
  }
  int start_level = ChunkAt(line.start).ChunkBlock()->Level();

  int chunk_after_line_limit =
      FirstChunkEndingAfterColumn(line, options_.LineLengthLimit());

  int best_chunk = line.start + 1;
  while (best_chunk < chunk_after_line_limit &&
         ChunkAt(best_chunk).ChunkBlock()->Level() == start_level) {
    // Advance breakpoint through the chunks with the same level.
    // Unless we meet
    // * a top level clause - e.g., break "SELECT FROM ..." which is a broken
    //   SQL, but we still want to put top level tokens separately.
    // * a comma - e.g., break "a, b, c, .." list if it is too long.
    if (ChunkAt(best_chunk).IsTopLevelClauseChunk() ||
        ChunkAt(best_chunk).IsSetOperator() ||
        ChunkAt(best_chunk - 1).LastKeyword() == "," ||
        ChunkAt(best_chunk).StartsWithChainableBooleanOperator()) {
      break;
    }
    ++best_chunk;
  }
  if (best_chunk >= line.end) {
    // Entire line consists of chunks with the same level but we still need to
    // break somewhere.
    return line.start + 1;
  }
  if (best_chunk == chunk_after_line_limit &&
      chunk_after_line_limit < line.end &&
      ChunkAt(chunk_after_line_limit).ClosesParenOrBracketBlock()) {
    // Entire line consists of chunks with the same level and the first chunk
    // that goes out of the line length limit is closing bracket.
    // At this stage, the closing bracket must stay together with the previous
    // chunk, so we need to break anywhere between the chunks on the same level.
    return line.start + 1;
  }

  // Jump over expressions in brackets if they fit on the current line.
  while (ChunkAt(best_chunk - 1).HasMatchingClosingChunk()) {
    const int first_chunk_after_bracket =
        ChunkAt(best_chunk - 1).MatchingClosingChunk()->PositionInQuery() + 1;
    if (first_chunk_after_bracket >= line.end ||
        first_chunk_after_bracket > chunk_after_line_limit) {
      break;
    }
    best_chunk = first_chunk_after_bracket;
  }

  // Avoid breaking before closing brackets. We decide if we break there only
  // when breaking on corresponding open brackets.
  while (best_chunk < line.end &&
         ChunkAt(best_chunk).ClosesParenOrBracketBlock()) {
    ++best_chunk;
  }
  if (best_chunk == line.end) {
    best_chunk = line.start + 1;
  }

  int best_level = ChunkAt(best_chunk).ChunkBlock()->Level();

  int skip_until = best_chunk;
  const ChunkBlock* block = ChunkAt(best_chunk).ChunkBlock()->Parent();
  while (block != nullptr) {
    for (auto i = block->children().begin(); i != block->children().end();
         ++i) {
      const Chunk* chunk =
          (*i)->IsLeaf() ? (*i)->Chunk() : (*i)->FirstChunkUnder();
      if (chunk == nullptr) {
        continue;
      }

      const int position = chunk->PositionInQuery();
      if (position <= skip_until) {
        continue;
      }
      if (position >= line.end) {
        return best_chunk;
      }
      const int level = (*i)->Level();

      // Closing parenthesis is weird. It's the only chunk that is on a level
      // that is one-higher (lower index value) than its "contents". So we deal
      // with them separately.
      if (chunk->ClosesParenOrBracketBlock()) {
        continue;
      }

      const Chunk* const previous_chunk = chunk->PreviousNonCommentChunk();
      if (level < best_level) {
        if (previous_chunk != nullptr &&
            previous_chunk->HasMatchingClosingChunk() &&
            previous_chunk->HasMatchingOpeningChunk()) {
          // We reached a chunk like ")[" which normally means accessing an
          // array element by index, e.g., Foo(bar)[0]. Try to keep this "[0]"
          // without breaking if it fits on the current line.
          skip_until =
              previous_chunk->MatchingClosingChunk()->PositionInQuery();
          continue;
        }
        best_level = level;
        best_chunk = position;
        skip_until = best_chunk;
      } else if (level == best_level) {
        if (best_level == start_level) {
          // TODO: Check if we can come up with some generic
          // solution rather than hard-coding each scenario when we prefer
          // keeping the chunks on the same line.
          if (previous_chunk->LastKeyword() == ",") {
            // We prefer commas to other chunks at the same level. But only if
            // we are not changing indentation. Consider "SELECT a.d, b". When
            // adding a newline after SELECT, the new line might now fit in the
            // line length limit. But if it doesn't, we then want to prefer to
            // break after the ",".
            best_chunk = position;
            skip_until = best_chunk;
          } else if (chunk->IsSetOperator()) {
            // Some keywords connect bigger parts of the query, but they are on
            // the same level with top level keywords. For instance:
            //  SELECT * FROM Table1
            //  UNION ALL
            //  SELECT * FROM Table2;
            // Here, UNION ALL separates two parts of the query, each might
            // having top level keywords. We prefer breaking on UNION ALL first
            // before deciding if we want to split the first SELECT statement
            // further.
            best_chunk = position;
            skip_until = best_chunk;
          } else if (chunk->FirstKeyword() == "|>") {
            // Prefer breaking query at pipe operators.
            best_chunk = position;
            skip_until = best_chunk;
          }
        } else if (chunk->FirstToken().Is(
                       Token::Type::JOIN_CONDITION_KEYWORD)) {
          best_chunk = position;
          skip_until = best_chunk;
        } else if (ChunkAt(best_chunk).LastKeywordsAre("@", "{") &&
                   !chunk->ClosesParenOrBracketBlock()) {
          // Prefer breaking after query hint if the following chunks are on the
          // same level:            instead of:
          // SELECT @{hint}             SELECT
          //   foo;                       @{hint}
          //                              foo;
          best_chunk = position;
          skip_until = best_chunk;
        }
      }
    }
    if (block->IsTopLevel() || block->Level() < start_level) {
      break;
    }
    block = block->Parent();
  }

  return best_chunk;
}

absl::btree_set<int> StmtLayout::BreakpointsCloseToLineLength(
    const Line& line, const int level) const {
  absl::btree_set<int> breakpoints;

  int length = ChunkAt(line.start).ChunkBlock()->Level();
  length += ChunkAt(line.start).PrintableLength(line.LengthInChunks() == 1);

  int last_before_length = -1;

  int after_first_element = line.start + 1;
  if (ChunkAt(line.start).HasMatchingClosingChunk()) {
    after_first_element =
        ChunkAt(line.start).MatchingClosingChunk()->PositionInQuery() + 1;
  }
  for (int i = after_first_element; i < line.end; ++i) {
    int l = ChunkAt(i).ChunkBlock()->Level();

    if (l == level) {
      if (last_before_length == -1 || length <= Options().LineLengthLimit()) {
        if (!ChunkAt(i).ClosesParenOrBracketBlock()) {
          // It's possible that we could do something smarter here with a
          // closing bracket, but for now we ignore them since they would
          // add more complexity and sometimes produce worse results. The
          // problem is that a newline before a closing bracket also implies
          // a newline after the matching opening bracket, and that is
          // almost always on a different level so it can produce quite ugly
          // results.
          last_before_length = i;
        }
      }
    }

    if (ChunkAt(i).StartsWithSpace()) {
      length += 1;
    }
    length += ChunkAt(i).PrintableLength(i + 1 == line.end);

    if (length > Options().LineLengthLimit()) {
      if (last_before_length != -1) {
        breakpoints.insert(last_before_length);
        length = l + ChunkAt(i).PrintableLength(i + 1 == line.end);
        last_before_length = -1;
      }
    }
  }
  if (breakpoints.empty() && last_before_length != -1) {
    breakpoints.insert(last_before_length);
  }

  if (breakpoints.empty()) {
    return breakpoints;
  }

  // Try to find what would happen if we break before each element in the
  // sequence. If the amount of breakpoints close to line length < elements/2,
  // it means some lines contain only one element and some have multiple, which
  // doesn't look great. For instance:
  //   we don't want:           but this is fine:
  //   a IN (                   a IN (
  //     '111111',                '111', '222',
  //     '2', '3',                '333', '444')
  //     '444444')
  const absl::btree_set<int> max_breakpoints =
      FindSiblingBreakpoints(line, *breakpoints.begin());
  if (breakpoints.size() * 2 >= max_breakpoints.size()) {
    return max_breakpoints;
  }

  // Check if the original input had each element on a separate line. If so,
  // preserve the original formatting.
  if (options_.IsPreservingExistingLineBreaks()) {
    const absl::btree_set<int> user_breakpoints = LineBreaksInInput(line);
    if (user_breakpoints.size() > breakpoints.size()) {
      if (FormatterBreakpointsRoughlyMatchUsers(line, user_breakpoints,
                                                max_breakpoints)) {
        return max_breakpoints;
      }
    }
  }
  // Mark all line starts to be ignored when preserving user line breaks - we
  // want to overwrite user breaks by placing as much elements on a single line
  // as possible.
  ChunkAt(line.start).ChunkBlock()->MarkAsBreakCloseToLineLength();
  for (const auto& b : breakpoints) {
    ChunkAt(b).ChunkBlock()->MarkAsBreakCloseToLineLength();
  }

  // Once we split the expression, check if the expression is followed by an
  // 'AS alias' clause. If it does, and there is more than 1 token on the last
  // line, add a line break before the alias. For instance:
  //   1 + 2 + 3
  //   + 4 + 5
  //     AS alias
  // but:
  //   1 + 2 + 3
  //   + 4 AS alias
  int alias_position = line.end - 1;
  while (alias_position > line.start &&
         // Line might end with closing parentheses - skip those, but only if
         // the entire expression is inside those parentheses. For instance,
         // do not proceed in expression like:
         // "CAST(a AS INT64) + CAST(b AS INT64)"
         ChunkAt(alias_position).HasMatchingOpeningChunk() &&
         ChunkAt(alias_position).MatchingOpeningChunk()->PositionInQuery() <
             line.start) {
    alias_position--;
  }
  if (alias_position - 1 > *breakpoints.crbegin() &&
      ChunkAt(alias_position).ChunkBlock()->Level() >
          ChunkAt(line.start).ChunkBlock()->Level() &&
      (ChunkAt(alias_position).FirstKeyword() == "AS" ||
       ChunkAt(alias_position).FirstToken().MayBeIdentifier())) {
    breakpoints.insert(alias_position);
  }

  return breakpoints;
}

absl::btree_set<int> StmtLayout::FindSiblingBreakpoints(const Line& line,
                                                        int break_point) const {
  absl::btree_set<int> result;
  // This is illegal input, but might as well protect from it.
  if (break_point <= line.start || break_point >= line.end) {
    return result;
  }

  ChunkBlock* break_point_block = ChunkAt(break_point).ChunkBlock();
  ChunkBlock* break_point_parent = break_point_block->Parent();

  const Chunk* previous_chunk = ChunkAt(break_point).PreviousNonCommentChunk();

  if (previous_chunk != nullptr) {
    if (previous_chunk->IsTypeDeclarationStart()) {
      // Prefer breaking a line right after the type declaration.
      Chunk* type_declaration_end = previous_chunk->MatchingClosingChunk();
      if (type_declaration_end != nullptr) {
        const absl::string_view keyword_after_type =
            type_declaration_end->LastKeyword();
        if (keyword_after_type == "(" || keyword_after_type == "[") {
          int b = type_declaration_end->PositionInQuery();
          if (b >= break_point && b + 1 < line.end) {
            break_point = b + 1;
            result.insert(break_point);
            previous_chunk = ChunkAt(break_point).PreviousNonCommentChunk();
          } else {
            result.insert(break_point);
            return result;
          }
        }
      }
    }
    if (previous_chunk->OpensParenOrBracketBlock()) {
      result.insert(break_point);
      // Check if we should add a breakpoint before closing parenthesis.
      // In general, we break before closing parenthesis if the expression
      // inside is a SELECT statement. Sometimes, SELECT statement is wrapped
      // into a macro, so we look at the token before "(" to find common
      // scenarios that start a sub-select.

      const Chunk* first_in_parentheses = &ChunkAt(break_point);
      if (first_in_parentheses->IsCommentOnly()) {
        first_in_parentheses = first_in_parentheses->NextNonCommentChunk();
      }

      const int closing_bracket = FindMatchingClosingBracket(line, break_point);
      if (closing_bracket <= 0) {
        // No closing parenthesis found.
        return result;
      }
      // See if the first chunk inside parentheses starts a sub-select.
      if ((first_in_parentheses != nullptr &&
           first_in_parentheses->IsStartOfNewQuery()) ||
          (previous_chunk != nullptr &&
           // Contents of "OVER (...)" is treated as a code block.
           (previous_chunk->FirstKeyword() == "OVER" ||
            // "AS (" start either table definition in WITH clause or a function
            // body.
            (previous_chunk->FirstKeyword() == "AS" &&
             previous_chunk->SecondKeyword() == "(") ||
            (previous_chunk->FirstKeyword() == "(" &&
             // Parenthesis goes right after UNION ALL or other set operator.
             ((previous_chunk->PreviousNonCommentChunk() != nullptr &&
               previous_chunk->PreviousNonCommentChunk()->IsSetOperator()) ||
              // Parenthesis starts expression in FROM or JOIN clauses.
              IsExpressionInsideFromOrJoinClause(*previous_chunk))))) ||
          // There is a single line comment at the end of the previous line -
          // we cannot put a closing bracket there.
          ChunkAt(closing_bracket - 1).EndsWithSingleLineComment() ||
          // Curly braced constructor `NEW Type { foo: 1 }`.
          ChunkAt(closing_bracket)
              .FirstToken()
              .Is(Token::Type::BRACED_CONSTR_CLOSE_BRACKET)) {
        // Add a line break before closing parenthesis.
        result.insert(closing_bracket);
      } else {
        // Shouldn't break before closing parenthesis. See if we should break
        // right after it.
        int t = FindMatchingClosingBracket(line, break_point);
        // Jump over sequences of brackets, e.g. "Foo(a)[0]".
        while (t > 0 && t < line.end - 1 &&
               ChunkAt(t).HasMatchingClosingChunk()) {
          t = FindMatchingClosingBracket(line, t + 1);
        }
        // Break after the closing parenthesis only if the following chunk is
        // not another closing bracket.
        if (t > 0 && t < line.end - 1 &&
            !ChunkAt(t + 1).ClosesParenOrBracketBlock()) {
          result.insert(t + 1);
        }
      }
      return result;
    }
  }

  if (ChunkAt(break_point).ClosesParenOrBracketBlock()) {
    // Ignore breakpoint before closing bracket: if we didn't add a line break
    // here when processing matching opening bracket, the closing bracket should
    // always stay on the same line.
    // The only exception if the previous line is a comment that had line break
    // in the input.
    if (previous_chunk != nullptr &&
        previous_chunk->EndsWithSingleLineComment()) {
      result.insert(break_point);
    }
    return result;
  }

  if (previous_chunk != nullptr && previous_chunk->IsJoin()) {
    // When an expression inside a JOIN has to be split on multiple lines,
    // make sure that corresponding JOIN condition goes to its own line.
    const int join_cond_pos = FindJoinConditionPosition(line, *previous_chunk);
    if (join_cond_pos > 0) {
      result.insert(join_cond_pos);
    }
    // Note that we don't return the result here to allow searching for further
    // breakpoints.
  }

  // If breakpoint is for a set operator (UNION ALL and co.), find all sibling
  // set operators but skip other top level keywords that happen to be on the
  // same level. E.g.:
  //  SELECT * FROM Foo
  //  UNION ALL
  //  SELECT * FROM Bar
  // We want to add breakpoints around UNION ALL but not before FROM clause if
  // it fits on one line.
  if (ChunkAt(break_point).IsSetOperator()) {
    for (auto sibling = break_point_parent->children().begin();
         sibling != break_point_parent->children().end(); ++sibling) {
      if (!(*sibling)->IsLeaf() || !(*sibling)->Chunk()->IsSetOperator()) {
        continue;
      }
      int t = (*sibling)->Chunk()->PositionInQuery();
      if (t <= line.start || t >= line.end) {
        continue;
      }
      // Break before and after the set operator, e.g.:
      // SELECT 1
      // <break>UNION ALL<break>
      // SELECT 2
      result.insert(t);
      if (t + 1 < line.end) {
        result.insert(t + 1);
      }
    }
    return result;
  }

  // Default logic: add line breaks before all chunks that are on the same level
  // with the breakpoint.
  for (auto sibling = break_point_parent->children().rbegin();
       sibling != break_point_parent->children().rend(); ++sibling) {
    if (!(*sibling)->IsLeaf()) {
      continue;
    }
    // Closing bracket is a sibling of the chunk immediately following
    // the matching open bracket. It's handled above.
    if ((*sibling)->Chunk()->ClosesParenOrBracketBlock()) {
      continue;
    }
    int t = (*sibling)->Chunk()->PositionInQuery();
    if (t <= line.start || t >= line.end) {
      continue;
    }
    if (previous_chunk != nullptr && previous_chunk->LastKeyword() == "," &&
        !ChunkAt(break_point).IsTopLevelClauseChunk()) {
      // Commas group together, but not with other things at the same
      // level. Unless this is a top level clause that follows a trailing
      // comma.
      const Chunk* prev_sibling = ChunkAt(t).PreviousNonCommentChunk();
      if (prev_sibling != nullptr && prev_sibling->LastKeyword() == ",") {
        result.insert(t);
      }
    } else if (previous_chunk != nullptr &&
               (previous_chunk->LastKeyword() == "CASE" ||
                previous_chunk->LastKeyword() == "WHEN") &&
               (*sibling)->Chunk()->FirstKeyword() != "WHEN" &&
               !result.empty()) {
      // If we add new lines in CASE statement or WHEN subclause, skip
      // breaking the very first line if we already have other breakpoints.
      // This way we allow:                    and:
      //   CASE foo  # no break before foo     ...
      //     WHEN ...                            WHEN 1  # no break before 1
      //     ELSE ...                              THEN ...
      //     END
      // but still add a break before foo if "CASE foo" doesn't fit on a
      // single line.
      const absl::string_view closest_breakpoint_keyword =
          ChunkAt(*result.begin()).FirstKeyword();
      if (closest_breakpoint_keyword == "WHEN" ||
          closest_breakpoint_keyword == "THEN") {
        continue;
      } else {
        result.insert(t);
      }
    } else if (ChunkAt(t).IsJoin()) {
      result.insert(t);
      // If we break before join, we should also break before join condition.
      const int join_cond_pos = FindJoinConditionPosition(line, ChunkAt(t));
      if (join_cond_pos > 0) {
        result.insert(join_cond_pos);
      }
    } else {
      result.insert(t);
    }
  }

  return result;
}

int StmtLayout::FindMatchingClosingBracket(const Line& line,
                                           const int break_point) const {
  if (break_point <= 0) {
    return -1;
  }

  const Chunk& opening_bracket = ChunkAt(break_point - 1);
  if (!opening_bracket.OpensParenOrBracketBlock() ||
      !opening_bracket.HasMatchingClosingChunk()) {
    return -1;
  }

  int closing_bracket_position =
      opening_bracket.MatchingClosingChunk()->PositionInQuery();
  if (closing_bracket_position <= break_point ||
      closing_bracket_position >= line.end) {
    // The query is malformed: matching parenthesis is outside of the current
    // line.
    return -1;
  }

  return closing_bracket_position;
}

bool StmtLayout::ShouldIncludeSiblings(const Line& line,
                                       const int break_point) const {
  if (break_point > 0) {
    const Chunk* previous_chunk =
        ChunkAt(break_point).PreviousNonCommentChunk();
    // Check if all chunks in this line before the breakpoint are comments.
    // If so, this breakpoint separates the comment at the top of a block from
    // the rest of sql - no need to split the rest of the line.
    if (previous_chunk == nullptr ||
        previous_chunk->PositionInQuery() < line.start) {
      return false;
    }

    if (previous_chunk->LastKeyword() == "," ||
        previous_chunk->OpensParenOrBracketBlock() ||
        previous_chunk->IsJoin() || previous_chunk->IsTypeDeclarationStart() ||
        previous_chunk->LastKeyword() == "CASE" ||
        previous_chunk->LastKeyword() == "WHEN") {
      return true;
    }
  }

  if (ChunkAt(break_point).FirstKeyword() == "AND" ||
      ChunkAt(break_point).FirstKeyword() == "BETWEEN" ||
      ChunkAt(break_point).FirstKeyword() == "OR" ||
      ChunkAt(break_point).FirstKeyword() == "WHEN" ||
      ChunkAt(break_point).FirstKeyword() == "ELSE" ||
      ChunkAt(break_point).FirstKeyword() == "END" ||
      ChunkAt(break_point).LastKeywordsAre("OPTIONS", "(")) {
    return true;
  }

  if (ChunkAt(break_point).ClosesParenOrBracketBlock()) {
    return true;
  }

  if (ChunkAt(break_point).ChunkBlock()->Level() !=
      ChunkAt(line.start).ChunkBlock()->Level()) {
    return false;
  }

  if (ChunkAt(break_point)
          .FirstToken()
          .Is(Token::Type::CHAINED_METHOD_DOT_SEPARATOR)) {
    return true;
  }
  if (ChunkAt(break_point).FirstKeyword() == ".") {
    return false;
  }

  if (ChunkAt(break_point).StartsWithRepeatableOperator()) {
    return false;
  }

  if (ChunkAt(break_point).IsStartOfNewQuery()) {
    return false;
  }

  return true;
}

bool StmtLayout::ShouldBreakCloseToLineLength(const Line& line,
                                              const int break_point) const {
  // The breakpoint after the comment.
  if (ChunkAt(break_point - 1).IsCommentOnly()) {
    return false;
  }
  // We want to preserve multiple elements on the same level on single line,
  // e.g. "1 + 2 + 3 + 4 + 5 ...". There are a number of properties such
  // sequences have:
  // There can't be a sequence of top level clauses on the same line;
  if (ChunkAt(break_point).IsTopLevelClauseChunk()) {
    return false;
  }

  // Check that the first chunk and the chunk at the break point are on the same
  // level, or chunk at the break point is on start level + 1, which happens if
  // the expression is a part of a list.
  const int start_level = ChunkAt(line.start).ChunkBlock()->Level();
  const int break_point_level = ChunkAt(break_point).ChunkBlock()->Level();
  if (!(break_point_level == start_level ||
        break_point_level == start_level + options_.IndentationSpaces())) {
    return false;
  }

  // Check if it is an expression like "1 + 2 + 3 + 4 + ..."
  if (ChunkAt(line.start).CanBePartOfExpression() &&
      ChunkAt(break_point).StartsWithRepeatableOperator()) {
    return !ShouldIncludeSiblings(line, break_point);
  }

  // Check if the line contains a list inside
  //  * array literal: [1, 2, 3, 4, ...]
  //  * IN operator: a IN (1, 2, 3, 4, ...)
  //  * GROUP/ORDER/... BY clause: GROUP BY 1, 2, 3, 4, ...
  static const auto* kByClauses = new googlesql_base::flat_set<absl::string_view>(
      {"CLUSTER", "GROUP", "ORDER", "PARTITION"});
  const Chunk* previous_chunk = ChunkAt(break_point).PreviousNonCommentChunk();
  if (previous_chunk != nullptr && previous_chunk->LastKeyword() == ",") {
    const ChunkBlock* prev_level_block =
        ChunkAt(break_point).ChunkBlock()->Parent()->FindPreviousSiblingBlock();
    if (prev_level_block != nullptr && prev_level_block->IsLeaf() &&
        (prev_level_block->Chunk()->LastKeyword() == "[" ||
         prev_level_block->Chunk()->LastKeywordsAre("IN", "(") ||
         (prev_level_block->Chunk()->LastKeyword() == "BY" &&
          kByClauses->contains(prev_level_block->Chunk()->FirstKeyword())))) {
      return true;
    }
  }

  return false;
}

}  // namespace googlesql::formatter::internal
