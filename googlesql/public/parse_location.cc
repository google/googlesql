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

// ParseLocationTranslator is tested by public/error_helpers_test.cc and
// analyzer/testdata/parse_locations.test.

#include "googlesql/public/parse_location.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <utility>

#include "googlesql/base/logging.h"
#include "googlesql/proto/internal_error_location.pb.h"
#include "absl/status/statusor.h"
#include "unicode/umachine.h"
#include "unicode/utf8.h"
#include "googlesql/base/mathutil.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_builder.h"

namespace googlesql {

const int kTabWidth = 8;

InternalErrorLocation ParseLocationPoint::ToInternalErrorLocation() const {
  InternalErrorLocation error_location;
  if (!filename_.empty()) {
    error_location.set_filename(filename_);
  }
  error_location.set_byte_offset(byte_offset_);
  return error_location;
}

ParseLocationPoint ParseLocationPoint::FromInternalErrorLocation(
    const InternalErrorLocation& info) {
  return ParseLocationPoint::FromByteOffset(info.filename(),
                                            info.byte_offset());
}

ParseLocationTranslator::ParseLocationTranslator(absl::string_view input)
    : input_(input) {}

void ParseLocationTranslator::CalculateLineOffsets() const {
  if (line_offsets_.empty()) {
    line_offsets_.push_back(0);  // Line 1 starts at offset 0.
    int offset = 0;
    while (offset < input_.size()) {
      switch (input_[offset]) {
        case '\n':
          line_offsets_.push_back(++offset);
          break;
        case '\r':
          if (offset + 1 < input_.size() && input_[offset + 1] == '\n') {
            ++offset;
          }
          line_offsets_.push_back(++offset);
          break;
        default:
          ++offset;
          break;
      }
    }
  }
}

namespace {
// Helper function used when iterating through a line of text to advance one
// character.
//
// On input, <byte_offset> points to a byte offset within <current_line>
// and <column> points to the one-based column number of <byte_offset>.
//
// On output, advances <*byte_offset> and <*column> by the number of bytes
// and columns represented by the current character, respectively.
//
// In most cases, one byte <=> one char <=> one column, but not always.
// <current_line> is processed as UTF-8, and multi-byte characters map to just
// one column.  In addition, the tab character ('\t') advances the column number
// forward to the nearest multiple of kTabSize, so multiple columns contained
// within a single byte are also possible.
//
// Since <current_line> is assumed to represent a single line of text (not
// including the terminating newline character), we are assumed to never
// encounter newline characters within <current_line>.
//
// <stop_byte_offset> and <stop_column>, if present, specify a byte offset and
// column number which we will never advance past, even if <stop_byte_offset>
// is in the middle of a character, or <stop_column> is in the middle of a
// tab expansion.  If either of these constraints are hit, we return Ok and
// partially advance either the byte offset or the column, up to the limit
// provided.
//
// If the current byte offset points at an invalid utf-8 sequence, we advance
// one column.
//
// Returns a generic::internal error if <*byte_offset> is out of bounds with
// respect to <current_line>
absl::Status AdvanceOneChar(absl::string_view current_line,
                            std::optional<int> stop_byte_offset,
                            std::optional<int> stop_column, int* column,
                            int* byte_offset) {
  GOOGLESQL_RET_CHECK_GE(*byte_offset, 0) << "Negative byte offset";
  GOOGLESQL_RET_CHECK_LT(*byte_offset, current_line.length())
      << "Byte offset beyond the last column of line";

  if (current_line[*byte_offset] == '\t') {
    int new_column = googlesql_base::MathUtil::RoundUpTo(*column, kTabWidth) + 1;
    if (stop_column.has_value() && new_column > stop_column.value()) {
      // <stop_column> points to whitespace in the middle of tab expansion.
      *column = stop_column.value();
    } else {
      *column = new_column;
      ++*byte_offset;
    }
    return absl::OkStatus();
  }

  // Figure out the length of the current UTF-8 character.  Note that
  // <new_byte_offset> and <current_code_point> are passed by reference and
  // modified by the U8_NEXT() macro.  On output, <new_byte_offset> is the
  // byte offset of the end of the current character and <current_code_point>
  // is the code point of the current character, or a negative value in case of
  // error.
  int new_byte_offset = *byte_offset;
  UChar32 current_code_point;
  U8_NEXT(current_line.data(), new_byte_offset, current_line.length(),
          current_code_point);
  if (current_code_point < 0) {
    // The line contains invalid utf-8, so just fall back to advancing a
    // single byte.
    new_byte_offset = *byte_offset + 1;
  }
  if (stop_byte_offset.has_value() &&
      new_byte_offset > stop_byte_offset.value()) {
    // <*stop_byte_offset> represents a byte in the middle of the UTF-8
    // character.
    *byte_offset = stop_byte_offset.value();
  } else {
    ++*column;
    *byte_offset = new_byte_offset;
  }
  return absl::OkStatus();
}

absl::StatusOr<int> ColumnNumberFromLineLocalByteOffset(
    absl::string_view current_line, int desired_byte_offset) {
  int column = 1;  // Column numbers are one-based.
  int byte_offset = 0;

  while (byte_offset < desired_byte_offset) {
    GOOGLESQL_RETURN_IF_ERROR(AdvanceOneChar(
        current_line, /*stop_byte_offset=*/desired_byte_offset,
        /*stop_column=*/std::optional<int>(), &column, &byte_offset));
  }

  return column;
}

}  // namespace

absl::StatusOr<std::pair<int, int>>
ParseLocationTranslator::GetLineAndColumnFromByteOffset(int byte_offset) const {
  ABSL_DCHECK_GE(byte_offset, 0);
  ABSL_DCHECK_LE(byte_offset, input_.size());
  GOOGLESQL_RET_CHECK(byte_offset >= 0 &&
            byte_offset <= static_cast<int64_t>(input_.size()))
      << "Byte offset " << byte_offset << " out of bounds of input (size "
      << input_.size() << ")";
  CalculateLineOffsets();
  ABSL_DCHECK_EQ(line_offsets_[0], 0);
  ABSL_DCHECK(!line_offsets_.empty());
  ABSL_DCHECK_EQ(line_offsets_.front(), 0);
  auto ub_iter =
      std::upper_bound(line_offsets_.begin(), line_offsets_.end(), byte_offset);
  // ub_iter points at the beginning of the *next* line.
  --ub_iter;
  const int line_number =
      static_cast<int>(std::distance(line_offsets_.begin(), ub_iter) + 1);

  GOOGLESQL_ASSIGN_OR_RETURN(absl::string_view current_line, GetLineText(line_number));
  GOOGLESQL_ASSIGN_OR_RETURN(
      int column_number,
      ColumnNumberFromLineLocalByteOffset(
          current_line, byte_offset - line_offsets_[line_number - 1]),
      _ << "\nByte offset: " << byte_offset << "\nError in line " << line_number
        << ", which starts at byte offset " << line_offsets_[line_number - 1]);
  return std::make_pair(line_number, column_number);
}

absl::StatusOr<int> ParseLocationTranslator::GetByteOffsetFromLineAndColumn(
    int line, int column) const {
  GOOGLESQL_RET_CHECK_GE(line, 1);
  GOOGLESQL_RET_CHECK_GE(column, 1);
  CalculateLineOffsets();

  // Find the offset corresponding to the line number.
  GOOGLESQL_RET_CHECK_LE(line, line_offsets_.size())
      << "Query had " << line_offsets_.size() << " lines but line " << line
      << " was requested";

  GOOGLESQL_ASSIGN_OR_RETURN(absl::string_view current_line, GetLineText(line));
  ABSL_DCHECK_EQ(current_line.find('\r'), current_line.npos)
      << "GetLineText() returned string with newline characters";
  ABSL_DCHECK_EQ(current_line.find('\n'), current_line.npos)
      << "GetLineText() returned string with newline characters";

  int byte_offset = 0;
  int curr_column = 1;
  while (curr_column < column) {
    GOOGLESQL_RETURN_IF_ERROR(AdvanceOneChar(current_line,
                                   /*stop_byte_offset=*/std::optional<int>(),
                                   /*stop_column=*/column, &curr_column,
                                   &byte_offset));
  }

  return line_offsets_[line - 1] + byte_offset;
}

absl::StatusOr<std::pair<int, int>>
ParseLocationTranslator::GetLineAndColumnAfterTabExpansion(
    ParseLocationPoint point) const {
  return GetLineAndColumnFromByteOffset(point.GetByteOffset());
}

// Return <input> with tabs expanded to spaces, assuming kTabWidth-char tabs.
std::string ParseLocationTranslator::ExpandTabs(absl::string_view input) {
  std::string out;
  for (int i = 0; i < input.size(); ++i) {
    ABSL_DCHECK(input[i] != '\n' && input[i] != '\r');
    if (input[i] == '\t') {
      out += std::string(kTabWidth - (out.size() % kTabWidth), ' ');
    } else {
      out += input[i];
    }
  }
  return out;
}

absl::StatusOr<absl::string_view> ParseLocationTranslator::GetLineText(
    int line) const {
  CalculateLineOffsets();

  GOOGLESQL_RET_CHECK_GT(line, 0) << "Line number <= 0";
  GOOGLESQL_RET_CHECK_LE(line, line_offsets_.size())
      << "Query had " << line << " lines but line " << line_offsets_.size()
      << " was requested";

  const int line_index = line - 1;
  const int line_start_offset = line_offsets_[line_index];
  int line_end_offset;
  if (line_index == line_offsets_.size() - 1) {
    line_end_offset = static_cast<int>(input_.size());
  } else {
    line_end_offset = line_offsets_[line_index + 1] - 1;
  }

  // If the line ends with "\r\n", don't include the "\r" as part of the line.
  if (line_end_offset > 0 && line_end_offset < input_.size() &&
      input_[line_end_offset] == '\n' && input_[line_end_offset - 1] == '\r') {
    --line_end_offset;
  }

  return input_.substr(line_start_offset, line_end_offset - line_start_offset);
}

}  // namespace googlesql
