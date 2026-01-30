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

#include "googlesql/tools/formatter/internal/range_utils.h"

#include <algorithm>
#include <set>
#include <vector>

#include "googlesql/public/formatter_options.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/tools/formatter/internal/token.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/status_macros.h"

namespace googlesql::formatter::internal {

absl::Status ExpandByteRangesToFullStatements(
    std::vector<FormatterRange>* sorted_ranges, absl::string_view sql,
    std::set<FormatterRange> allowed_ranges) {
  FormatterRange prev_statement({0, 0});
  FormatterRange statement = FindNextStatementOrComment(sql, 0);
  const int first_statement_start = statement.start;
  if (!sorted_ranges->empty() &&
      sorted_ranges->back().end <= first_statement_start) {
    // All ranges are within blank lines before the first statement starts.
    sorted_ranges->clear();
    return absl::OkStatus();
  }
  if (allowed_ranges.empty()) {
    allowed_ranges = {{.start = 0, .end = static_cast<int>(sql.size())}};
  }
  auto range = sorted_ranges->begin();
  // Largest allowed range after expansion. The range shouldn't be expanded
  // beyond the component it belongs to.
  while (statement.start < sql.size() && range != sorted_ranges->end()) {
    // Skip statements that appear before current range.
    while (statement.end <= range->start && statement.start < sql.size()) {
      prev_statement = statement;
      statement = FindNextStatementOrComment(sql, statement.end);
    }
    // Remaining range(s) are within blank lines after the last statement.
    if (statement.start == sql.size()) {
      sorted_ranges->erase(range, sorted_ranges->end());
      break;
    }
    if (statement.start >= range->end) {
      // Range is in blank lines between statements.
      range = sorted_ranges->erase(range);
      continue;
    }
    auto component_iter = allowed_ranges.upper_bound(*range);
    // If the first allowed range is returned, range.start is smaller than any
    // allowed range start, so the range stays unmodified.
    if (component_iter == allowed_ranges.begin()) {
      ++range;
      continue;
    }
    --component_iter;
    // If the range starts after the end of the previous allowed range, the
    // range also stays unmodified.
    if (range->start >= component_iter->end) {
      ++range;
      continue;
    }
    //  Move the range start up to include the beginning of the statement.
    if (statement.start < range->start &&
        component_iter->start <= range->start) {
      range->start = std::max(statement.start, component_iter->start);
      // ..or move the range start down to exclude empty lines before the
      // statement start.
    } else if (statement.start > range->start && prev_statement.end > 0 &&
               prev_statement.end <= range->start) {
      range->start = statement.start;
    }
    // Merge range with previous if the previous includes the previous
    // statement.
    if (range != sorted_ranges->begin() && prev_statement.end > 0 &&
        prev_statement.end == (range - 1)->end) {
      range->start = (range - 1)->start;
      range = sorted_ranges->erase(range - 1);
    }
    // Find the last statement within the range.
    while (statement.end < range->end) {
      prev_statement = statement;
      statement = FindNextStatementOrComment(sql, statement.end);
    }
    // Expand the range to include the remaining of the statement.
    if (statement.start < range->end && statement.end > range->end &&
        range->end < component_iter->end) {
      range->end = std::min(statement.end, component_iter->end);
      // ..or exclude trailing blank lines from the range.
    } else if (statement.start >= range->end && statement.start < sql.size() &&
               prev_statement.end < range->end) {
      range->end = prev_statement.end;
    }
    ++range;
    // Merge ranges that touch the same statement.
    while (range != sorted_ranges->end() && range->end <= component_iter->end &&
           statement.end > range->start) {
      if (range->end <= statement.end) {
        range = sorted_ranges->erase(range);
      } else {
        range->start = (range - 1)->start;
        range = sorted_ranges->erase(range - 1);
        break;
      }
    }
  }

  // If there is a single range left and there are only spaces before and after
  // - expand the range to the entire input.
  if (sorted_ranges->size() == 1 &&
      sorted_ranges->front().start == first_statement_start) {
    int next_stmt_start = sorted_ranges->front().end;
    if (next_stmt_start < sql.size()) {
      next_stmt_start = FindNextStatementOrComment(sql, next_stmt_start).start;
    }
    if (next_stmt_start == sql.size()) {
      sorted_ranges->front().start = 0;
      sorted_ranges->front().end = static_cast<int>(sql.size());
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<std::vector<FormatterRange>> ConvertLineRangesToSortedByteRanges(
    const std::vector<FormatterRange>& line_ranges, absl::string_view sql,
    const ParseLocationTranslator& location_translator) {
  if (line_ranges.empty()) {
    return line_ranges;
  }
  std::vector<FormatterRange> sorted_ranges(line_ranges);
  std::sort(sorted_ranges.begin(), sorted_ranges.end(),
            [](const FormatterRange& a, const FormatterRange& b) {
              return a.start < b.start;
            });
  // Validate line ranges.
  const FormatterRange* prev = nullptr;
  for (auto it = sorted_ranges.cbegin(); it != sorted_ranges.cend(); ++it) {
    if (it->start > it->end) {
      return absl::InvalidArgumentError(absl::StrFormat(
          "invalid line range: start line should be <= end line; got: [%d, %d]",
          it->start, it->end));
    }
    if (prev != nullptr && prev->end >= it->start) {
      return absl::InvalidArgumentError(
          absl::StrFormat("overlapping line ranges are not allowed; found: "
                          "[%d, %d] and [%d, %d]",
                          prev->start, prev->end, it->start, it->end));
    }
    prev = &(*it);
  }
  // Convert to byte ranges.
  std::vector<FormatterRange> byte_ranges;
  byte_ranges.reserve(sorted_ranges.size());
  for (const auto& range : sorted_ranges) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        const int start,
        location_translator.GetByteOffsetFromLineAndColumn(range.start, 1),
        _ << absl::StrFormat("invalid line range: [%d, %d]", range.start,
                             range.end));

    GOOGLESQL_ASSIGN_OR_RETURN(
        int end,
        location_translator.GetByteOffsetFromLineAndColumn(range.end, 1),
        _ << absl::StrFormat("invalid line range: [%d, %d]", range.start,
                             range.end));
    GOOGLESQL_ASSIGN_OR_RETURN(const absl::string_view end_line_view,
                     location_translator.GetLineText(range.end),
                     _ << absl::StrFormat("invalid line range: [%d, %d]",
                                          range.start, range.end));
    end += static_cast<int>(end_line_view.size());
    // Include the line break into the range (any of: '\r', '\n' or '\r\n').
    if (end < sql.size() && (sql[end] == '\r' || sql[end] == '\n')) {
      ++end;
    }

    if (!byte_ranges.empty() && byte_ranges.back().end == start) {
      // Merge adjacent ranges.
      byte_ranges.back().end = end;
    } else {
      byte_ranges.push_back({start, end});
    }
  }
  return byte_ranges;
}

}  // namespace googlesql::formatter::internal
