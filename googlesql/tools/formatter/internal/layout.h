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

#ifndef GOOGLESQL_TOOLS_FORMATTER_INTERNAL_LAYOUT_H_
#define GOOGLESQL_TOOLS_FORMATTER_INTERNAL_LAYOUT_H_

#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "googlesql/public/formatter_options.h"
#include "googlesql/tools/formatter/internal/chunk.h"
#include "googlesql/tools/formatter/internal/parsed_file.h"
#include "absl/container/btree_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace googlesql::formatter::internal {

// A layout is all information needed to print the input on screen.
//
// The layout objects must be kept cheap to copy, so that we can explore many
// layouts at once if we choose to do so.
class Layout {
 public:
  virtual ~Layout() = default;

  // Transforms the layout into the best layout we can find at the moment.
  virtual void BestLayout() = 0;

  // Returns a string that can be directly displayed to a human.
  virtual std::string PrintableString() const = 0;

  // Returns the first keyword in the current layout if any.
  virtual absl::string_view FirstKeyword() const { return ""; }

  // Returns true if the current layout requires a blank line after the previous
  // one.
  virtual bool RequiresBlankLineBefore() const { return false; }

  // Returns true if the current layout must follow the previous one without a
  // blank line.
  virtual bool ForbidsAddingBlankLineBefore() const { return true; }

  // Returns true if the current layout requires a blank line after it.
  virtual bool RequiresBlankLineAfter() const { return false; }

  // Returns true if the current layout must be followed by the next one without
  // a blank line.
  virtual bool ForbidsAddingBlankLineAfter() const { return true; }
};

// Represents a layout for an unparsed part of the input file.
class UnparsedLayout : public Layout {
 public:
  explicit UnparsedLayout(const FilePart& part) : image_(part.Image()) {}

  // See documentation in Layout class above.
  void BestLayout() override {}
  std::string PrintableString() const override { return std::string(image_); }

 private:
  const absl::string_view image_;
};

// StmtLayout contains all information needed to print a single SQL statement.
//
// A layout is fully defined as a collection of Chunks with their ChunkBlocks
// and the positions of newlines.
class StmtLayout : public Layout {
 public:
  // A line is defined by two indices, <start> and <stop> into the Chunk vector.
  struct Line {
   public:
    // Start of the line.
    int start;

    // End of the line.
    int end;

    // Returns the amount of chunks this line consists of.
    int LengthInChunks() const { return end - start; }

    // Needed for absl Swissfamily containers.
    template <typename H>
    friend H AbslHashValue(H h, const Line& l) {
      return H::combine(std::move(h), l.start, l.end);
    }
    inline friend bool operator==(const Line& lhs, const Line& rhs) {
      return lhs.start == rhs.start && lhs.end == rhs.end;
    }
    inline friend bool operator!=(const Line& lhs, const Line& rhs) {
      return !(lhs == rhs);
    }
    friend bool operator<(const Line& lhs, const Line& rhs) {
      return lhs.start < rhs.start ||
             (lhs.start == rhs.start && lhs.end < rhs.end);
    }

    friend std::ostream& operator<<(std::ostream& os, const Line& l);

   private:
    // Constructs a new line starting at <start> up to, but not including <end>.
    // Made private to make sure new lines are only created within a layout
    // context.
    Line(int start, int end) : start(start), end(end) {}
    // Allow StmtLayout class to create new lines.
    friend class StmtLayout;
  };

  using LinesT = absl::btree_set<Line>;

  // Creates a new layout for the given `parsed_sql`.
  //
  // An empty layout has exactly 1 line that contains all chunks from the
  // `parsed_sql` and has exactly 1 newline at the very end if printed.
  //
  // The printout will follow the options specified in `options`.
  StmtLayout(const TokenizedStmt& parsed_sql, const FormatterOptions& options);

  // Returns true if the layout is for an empty (or blank) input.
  bool Empty() const;

  // See documentation in Layout class above.
  void BestLayout() override;
  std::string PrintableString() const override;
  absl::string_view FirstKeyword() const override;
  bool RequiresBlankLineBefore() const override;
  bool ForbidsAddingBlankLineBefore() const override;
  bool RequiresBlankLineAfter() const override;
  bool ForbidsAddingBlankLineAfter() const override { return false; }

  // Returns a single line of the layout as a string.
  std::string PrintableString(const Line& line) const;

  // Validates given line boundaries. If they don't fit the current layout,
  // shrinks the line. Returns an error if <start> or <end> had to be updated.
  absl::Status ValidateLine(int* start, int* end) const;

  // Returns a new line. The line is not added to the layout, but only checked
  // that it fits the layout bounds. Crashes in debug mode if the line is out of
  // bounds.
  Line NewLine(int start, int end) const;

  // Returns the current lines in the layout.
  const LinesT& Lines() const { return lines_; }

  // Sets the current lines in the layout to <lines>.
  void SetLines(LinesT lines) { lines_.swap(lines); }

  // Returns the chunk at <index>.
  const Chunk& ChunkAt(int index) const;

  // Returns the options.
  const FormatterOptions& Options() const { return options_; }

  // Returns true if the given `line`, when printed, will go over the line
  // length limit.
  bool IsLineLengthOverLimit(const Line& line) const;

  // Returns the index of the chunk in the given `line`, which spans beyond the
  // `column` when the line is printed. Returns `line.end` if entire line ends
  // before the `column`.
  int FirstChunkEndingAfterColumn(const Line& line, int column) const;

  int PrintableLineLength(const Line& line) const;

  // Returns true if the given `line` has line breaks in the original sql. The
  // function only counts 'legit' line breaks: the ones that appear between
  // individual chunks. For instance, it wouldn't count a line break between
  // "DEFINE" and "TABLE".
  bool ContainsLineBreaksInInput(const Line& line) const;

  // Returns a set of breakpoints the `line` contains in the original sql. The
  // function only counts 'legit' line breaks: the ones that appear between
  // individual chunks. For instance, it wouldn't count a line break between
  // "DEFINE" and "TABLE".
  absl::btree_set<int> LineBreaksInInput(const Line& line) const;

  // Transforms the layout so that all comments that force line breaks are
  // correctly and consistently applied.
  void BreakComments();

  // Transforms layout by breaking lines in expressions that always require a
  // line break irrespective of the line length. For example,
  // WITH A AS (SELECT * FROM B), C AS (SELECT * FROM D)
  // can fit on one line, but nevertheless each table definition should always
  // start from a new line.
  void BreakOnMandatoryLineBreaks();

  // Transforms layout by breaking any lines longer than the line length limit
  // by finding the chunk or chunks with the smallest level and breaking the
  // line there.
  void BreakAllLongLines();

  // Transforms layout by trying to break all lines that were split in the
  // original input. Takes into account only those line breaks that formatter
  // would do itself if it decided to break the line.
  void BreakLinesThatWereSplitByUser();

  // Breaks the given <line> into multiple lines on given <breakpoints>.
  LinesT LinesFromSetOfBreakpoints(
      const Line& line, const absl::btree_set<int>& breakpoints) const;

  // Return a list of lines that can be added to layout such that <line> is
  // correctly broken into lines, so that all breakpoints in <breakpoints> are
  // respected and so that the layout is consistent. For example, all end of
  // line comments force a line break. Note that this might introduce lines that
  // break at more than just comments.  This can happen because comments can
  // force a mandatory newline anywhere in the tree.
  //
  // E.g. while the following statement "fits" on the screen, the layout is
  // visibly inconsistent because higher level nodes are not newlined.
  //
  // SELECT a * (
  //   3 + 5 -- A comment
  // ) FROM table
  //
  // The correct lines are:
  //
  // SELECT
  //   a * (
  //     3 + 5 -- A comment
  //   )
  // FROM table
  LinesT ConsistentLinesForBreakpoints(
      const Line& line, const absl::btree_set<int>& breakpoints) const;

  // Break <line> into two or more lines. If <line> cannot be broken, it is
  // returned as is.
  LinesT BreakOneLine(const Line& line) const;

  // Returns a list of all breakpoints, indexes of the chunks in layout that
  // are the best way to break up <line> into one or more new lines.
  //
  // If <line> cannot be broken, an empty set is returned.
  absl::btree_set<int> Breakpoints(const Line& line) const;

  // Returns the index of the chunk in layout that is the best break point
  // that splits <line> into two.
  int BestBreakpoint(const Line& line) const;

  // Returns a set of breakpoints that break <line> in layout at level <level>
  // and makes the resulting lines as long as possible, i.e. the breakpoints are
  // as close to line length as possible.
  absl::btree_set<int> BreakpointsCloseToLineLength(const Line& line,
                                                    int level) const;

  // Returns all break points that should be used, if <break_point> is used in
  // layout to break up <line>, e.g. if we decide to break the line after a
  // comma, we should break after all other commas that are part of the same
  // "group". For example:
  //
  // SELECT a, b, c, d
  //
  // should be
  //
  // SELECT
  //   a,
  //   b,
  //   c,
  //   d
  //
  // and not
  //
  // SELECT a, b,
  //   c, d
  absl::btree_set<int> FindSiblingBreakpoints(const Line& line,
                                              int break_point) const;

  // Returns a sub-line within `line` between `formatter_breakpoints` that
  // contains the biggest amount of `user_breakpoints`.
  // Made public for testing.
  Line FormatterLineWithMostMissingBreakpoints(
      const Line& line, const absl::btree_set<int>& user_breakpoints,
      const absl::btree_set<int>& formatter_breakpoints) const;

 private:
  // Returns position of a chunk with the matching closing bracket, which is
  // expected to be on the same `line` with `break_point`.
  // Returns -1 if no matching bracket found in the current `line`.
  int FindMatchingClosingBracket(const Line& line, int break_point) const;

  // Returns true if given `break_point` requires looking for siblings. For
  // instance, if a breakpoint points to "FROM" in the query
  // "SELECT * FROM Foo WHERE bar", another breakpoint should be added before
  // "WHERE".
  bool ShouldIncludeSiblings(const Line& line, int break_point) const;

  // Returns true, if the given `break_point` may be moved to be as close to the
  // line length as possible.
  bool ShouldBreakCloseToLineLength(const Line& line, int break_point) const;

  // Returns true if in the original input the current layout contained blank
  // line(s) around comments before the actual statement.
  bool ContainsBlankLinesAtTheStart() const;

  // Returns true if the current layout contains multiple lines.
  bool IsMultilineStatement() const;

  // Returns true if the `formatter_breakpoints` roughly match the breakpoints
  // in the original input (`user_breakpoints`). E.g.:
  // user input #1:   user input #2:    formatter:
  // SELECT 1,        SELECT 1, 2,      SELECT
  //   2,               3                 1,
  //   3                                  2,
  //                                      3
  // Breakpoints in the input #1 ~match formatters (2 out of 3).
  // Breakpoints in the input #2 don't match (1 out of 3).
  bool FormatterBreakpointsRoughlyMatchUsers(
      const Line& line, const absl::btree_set<int>& user_breakpoints,
      const absl::btree_set<int>& formatter_breakpoints) const;

  // Transforms layout by removing line breaks that can be skipped.
  void PruneLineBreaks();

  const std::vector<Chunk>& chunks_;
  const FormatterOptions options_;

  LinesT lines_;
};

// Represents a layout for the entire input. May contain multiple layout parts,
// e.g., corresponding to multiple SQL statements.
class FileLayout : public Layout {
 public:
  // Creates layout for the given parsed `file`.
  static absl::StatusOr<std::unique_ptr<FileLayout>> FromParsedFile(
      const ParsedFile& file);

  // Creates a new empty layout. Use the factory function above instead.
  explicit FileLayout(const FormatterOptions& options) : options_(options) {};

  // Returns parts of the current layout.
  const std::vector<std::unique_ptr<Layout>>& Parts() const {
    return layout_parts_;
  }
  // Appends given `layout_part`. Used only when constructing a new layout.
  void AddPart(std::unique_ptr<Layout> layout_part);

  // Returns formatter options used in this layout.
  const class FormatterOptions& FormatterOptions() const { return options_; }

  // See documentation in Layout class above.
  void BestLayout() override;
  std::string PrintableString() const override;

 private:
  std::vector<std::unique_ptr<Layout>> layout_parts_;
  class FormatterOptions options_;
};

}  // namespace googlesql::formatter::internal

#endif  // GOOGLESQL_TOOLS_FORMATTER_INTERNAL_LAYOUT_H_
