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

#include "googlesql/tools/execute_query/output_query_result.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "googlesql/common/box_glyphs.h"
#include "googlesql/public/functions/string.h"
#include "googlesql/public/strings.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/reference_impl/type_helpers.h"
#include "googlesql/base/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

namespace {

enum class SeparatorPosition { kTop = 0, kMiddle = 1, kBottom = 2 };

size_t EstimateGlyphWidth(absl::string_view input) {
  // input will be a unicode string. What we really want here is the
  // sum width of the glyphs used to represent the given string.
  // Unfortunately, this is system/font dependent, and we don't have
  // that information available. However, if we just use 'byte length'
  // as a proxy, we will consistently _overcount_ the width of glyphs.
  // Instead, normalize in a few different ways, and take the minimum size
  // since this is likely to match how it is usually rendered.
  std::string nfkc_str;
  std::string nfc_str;
  std::vector<absl::string_view> strs;
  absl::Status error;
  if (googlesql::functions::Normalize(input, functions::NormalizeMode::NFC,
                                      /*is_casefold=*/false, &nfc_str,
                                      &error)) {
    strs.push_back(nfc_str);
  }
  error = {};
  if (googlesql::functions::Normalize(input, functions::NormalizeMode::NFKC,
                                      /*is_casefold=*/false, &nfkc_str,
                                      &error)) {
    strs.push_back(nfkc_str);
  }
  size_t estimate = input.length();
  for (absl::string_view str : strs) {
    error = {};
    int64_t len = 0;
    if (googlesql::functions::LengthUtf8(str, &len, &error)) {
      estimate = std::min(estimate, static_cast<size_t>(len));
    }
  }
  return estimate;
}

absl::StatusOr<const Table*> GetTableForDMLStatement(
    const ResolvedStatement* resolved_stmt) {
  const ResolvedTableScan* scan = nullptr;
  switch (resolved_stmt->node_kind()) {
    case RESOLVED_DELETE_STMT:
      scan = resolved_stmt->GetAs<ResolvedDeleteStmt>()->table_scan();
      break;
    case RESOLVED_UPDATE_STMT:
      scan = resolved_stmt->GetAs<ResolvedUpdateStmt>()->table_scan();
      break;
    case RESOLVED_INSERT_STMT:
      scan = resolved_stmt->GetAs<ResolvedInsertStmt>()->table_scan();
      break;
    case RESOLVED_MERGE_STMT:
      scan = resolved_stmt->GetAs<ResolvedMergeStmt>()->table_scan();
      break;
    default:
      GOOGLESQL_RET_CHECK_FAIL()
          << "GetTableForDMLStatement() does not support node kind "
          << ResolvedNodeKind_Name(resolved_stmt->node_kind());
  }

  return scan->table();
}

// Populates the following, assuming 'resolved_stmt' is a statement, and
// 'value' is the corresponding result:
// - 'num_rows_modified' with the number of rows modified
//   (only set for DML statements).
// - 'is_value_table' with true if 'resolved_stmt' operates on a value table.
// - 'result_table' with the array representing the output of a query, or the
//   full contents of the modified table after a DML statement.
// - 'column_names' with the names of the columns of the rows in 'result_table'.
absl::Status GetOutputColumnInfo(const ResolvedStatement* resolved_stmt,
                                 const Value& result,
                                 std::optional<int64_t>* num_rows_modified,
                                 bool* is_value_table, Value* result_table,
                                 std::vector<std::string>* column_names) {
  switch (resolved_stmt->node_kind()) {
    case RESOLVED_QUERY_STMT: {
      const ResolvedQueryStmt* resolved_query =
          resolved_stmt->GetAs<ResolvedQueryStmt>();
      *is_value_table = resolved_query->is_value_table();
      *result_table = result;
      for (const auto& output_column : resolved_query->output_column_list()) {
        column_names->push_back(output_column->name());
      }
      break;
    }
    case RESOLVED_DELETE_STMT:
    case RESOLVED_UPDATE_STMT:
    case RESOLVED_INSERT_STMT:
    case RESOLVED_MERGE_STMT: {
      GOOGLESQL_RET_CHECK(result.type()->IsStruct());
      const StructType* result_type = result.type()->AsStruct();

      GOOGLESQL_RET_CHECK_EQ(2, result_type->num_fields());
      GOOGLESQL_RET_CHECK_EQ(kDMLOutputNumRowsModifiedColumnName,
                   result_type->field(0).name);
      GOOGLESQL_RET_CHECK_EQ(kDMLOutputAllRowsColumnName, result_type->field(1).name);

      *num_rows_modified = result.field(0).int64_value();
      *result_table = result.field(1);
      GOOGLESQL_RET_CHECK(result_table->type()->IsArray());

      GOOGLESQL_ASSIGN_OR_RETURN(const Table* table,
                       GetTableForDMLStatement(resolved_stmt));
      *is_value_table = table->IsValueTable();

      const Type* element_type =
          result_table->type()->AsArray()->element_type();
      if (*is_value_table) {
        column_names->push_back("value");
      } else {
        GOOGLESQL_RET_CHECK(element_type->IsStruct());
        for (const StructField& field : element_type->AsStruct()->fields()) {
          column_names->push_back(field.name);
        }
      }
      break;
    }

    case RESOLVED_CREATE_CONSTANT_STMT:
    case RESOLVED_CREATE_FUNCTION_STMT:
    case RESOLVED_CREATE_TABLE_FUNCTION_STMT:
    case RESOLVED_CREATE_TABLE_AS_SELECT_STMT: {
      GOOGLESQL_RET_CHECK(result.type()->IsStruct());
      const StructType* result_type = result.type()->AsStruct();

      GOOGLESQL_RET_CHECK_EQ(2, result_type->num_fields());
      GOOGLESQL_RET_CHECK_EQ(kCreatedObjectType, result_type->field(0).name);
      GOOGLESQL_RET_CHECK_EQ(kCreatedObjectName, result_type->field(1).name);

      TypeFactory type_factory;
      const ArrayType* array_type;
      GOOGLESQL_RETURN_IF_ERROR(type_factory.MakeArrayType(result_type, &array_type));
      GOOGLESQL_ASSIGN_OR_RETURN(*result_table, Value::MakeArray(array_type, {result}));
      *is_value_table = false;

      for (const StructField& field : result_type->fields()) {
        column_names->push_back(field.name);
      }
      break;
    }

    default:
      GOOGLESQL_RET_CHECK_FAIL()
          << "GetOutputColumnInfo() does not support resolved node kind "
          << ResolvedNodeKind_Name(resolved_stmt->node_kind());
  }
  return absl::OkStatus();
}

// Given a vector of column strings, returns a vector of lines, where each line
// consists of a vector of portions of the respective column string.
//
// For example, the nth line of the output consists of the nth line of the first
// string, followed by the nth line of the second string, etc., substituting the
// empty string for any non-existent lines.  Line separators ("\n") are not
// included in any of the column strings.
//
// The length of the result vector is the maximum number of lines that appears
// in any column string.
std::vector<std::vector<std::string>> SplitColumnStringsIntoLines(
    absl::Span<const std::string> column_strings) {
  // First, break down each column string into a vector of distinct lines.
  std::vector<std::vector<std::string>> columns_by_line;
  columns_by_line.reserve(column_strings.size());
  for (const std::string& column_string : column_strings) {
    columns_by_line.push_back(absl::StrSplit(column_string, '\n'));
  }

  // Find out the maximum number of lines present in any column strings. This
  // is the number of lines we will need to return.
  size_t max_lines = 0;
  for (const std::vector<std::string>& lines_of_column : columns_by_line) {
    max_lines = std::max(max_lines, lines_of_column.size());
  }

  // Now, compute the column texts, organized by line.
  std::vector<std::vector<std::string>> lines_by_column;
  lines_by_column.reserve(max_lines);
  for (size_t i = 0; i < max_lines; ++i) {
    std::vector<std::string>& cur_line = lines_by_column.emplace_back();
    for (const std::vector<std::string>& column : columns_by_line) {
      cur_line.push_back(i < column.size() ? column.at(i) : "");
    }
  }
  return lines_by_column;
}

// Helper function to produce a string representation of a 'row',
// given the input column string values and column buffer lengths.
std::string GenerateRowStringFromSingleLineColumns(
    absl::Span<const std::string> column_strings,
    absl::Span<const size_t> column_buffer_lengths, const BoxGlyphs& boxchars) {
  ABSL_CHECK_EQ(column_strings.size(), column_buffer_lengths.size());
  std::string row_string = std::string(boxchars.vertical);
  for (int col_idx = 0; col_idx < column_strings.size(); ++col_idx) {
    // One space is added before and after the value.  The value is
    // left-justified, and padded to the related column_buffer_lengths value.
    absl::StrAppend(&row_string, " ");
    absl::StrAppend(&row_string, column_strings[col_idx]);
    int pad_size =
        static_cast<int>(column_buffer_lengths[col_idx] -
                         EstimateGlyphWidth(column_strings[col_idx]));
    // The column_buffer_lengths are the max lengths of all the column
    // values for each column, so this column length should not be bigger
    // than the related max length.
    ABSL_CHECK_GE(pad_size, 0);
    if (pad_size > 0) {
      absl::StrAppend(&row_string, std::string(pad_size, ' '));
    }
    absl::StrAppend(&row_string, " ", boxchars.vertical);
  }
  absl::StrAppend(&row_string, "\n");
  return row_string;
}

std::string GenerateRowStringFromColumns(
    absl::Span<const std::string> column_strings,
    absl::Span<const size_t> column_buffer_lengths, const BoxGlyphs& boxchars) {
  std::vector<std::vector<std::string>> lines_by_column =
      SplitColumnStringsIntoLines(column_strings);

  std::string row_string;
  for (const std::vector<std::string>& line : lines_by_column) {
    absl::StrAppend(&row_string, GenerateRowStringFromSingleLineColumns(
                                     line, column_buffer_lengths, boxchars));
  }
  return row_string;
}

std::string GetEscapedString(const Value& value) {
  ABSL_DCHECK(value.type()->IsString());
  std::string literal = value.GetSQLLiteral();

  // GetSQLLiteral() returns a quoted string.  Strip the enclosing quotes.
  ABSL_DCHECK_GE(literal.length(), 2);
  ABSL_DCHECK(literal[0] == '\"' || literal[0] == '\'');
  ABSL_DCHECK_EQ(literal[0], literal[literal.length() - 1]);
  return literal.substr(1, literal.length() - 2);
}

// Returns a string to separate rows in an output table.  A "+" indicates
// a column boundary, and "-" appears above or below a column.
//
// Example:
// +------+----+-----+------------+------------------------+---+---+
std::string GetRowSeparator(absl::Span<const size_t> max_column_lengths,
                            const BoxGlyphs& boxchars,
                            SeparatorPosition position) {
  const struct {
    absl::string_view left, mid, right;
  } sep_chars[] = {
      {boxchars.top_left, boxchars.horizontal_down, boxchars.top_right},
      {boxchars.vertical_right, boxchars.cross, boxchars.vertical_left},
      {boxchars.bottom_left, boxchars.horizontal_up, boxchars.bottom_right},
  };
  ABSL_DCHECK(static_cast<int>(SeparatorPosition::kBottom) < std::size(sep_chars));
  const auto& [left, mid, right] = sep_chars[static_cast<int>(position)];

  std::string separator = std::string(left);
  for (int col_idx = 0; col_idx < max_column_lengths.size(); ++col_idx) {
    for (int i = 0; i < max_column_lengths[col_idx] + 2; ++i) {
      absl::StrAppend(&separator, boxchars.horizontal);
    }
    absl::StrAppend(&separator,
                    col_idx == max_column_lengths.size() - 1 ? right : mid);
  }
  absl::StrAppend(&separator, "\n");
  return separator;
}

}  // namespace

// Converts 'value' to an output string and returns it.
std::string ValueToOutputString(const Value& value, bool escape_strings) {
  if (value.is_null()) return "NULL";
  if (value.type()->IsStruct()) {
    std::vector<std::string> field_results;
    for (int i = 0; i < value.type()->AsStruct()->num_fields(); ++i) {
      std::string field_result =
          ValueToOutputString(value.field(i), /*escape_strings=*/true);
      absl::string_view name = value.type()->AsStruct()->field(i).name;
      if (!name.empty()) {
        absl::StrAppend(&field_result, " ", name);
      }
      field_results.push_back(field_result);
    }
    return absl::StrCat("{", absl::StrJoin(field_results, ", "), "}");
  } else if (value.type()->IsArray()) {
    return absl::StrCat(
        "[",
        absl::StrJoin(value.elements(), ", ",
                      [](std::string* out, const googlesql::Value& value) {
                        absl::StrAppend(
                            out, ValueToOutputString(value,
                                                     /*escape_strings=*/true));
                      }),
        "]");
  } else if (value.type()->IsBytes()) {
    return EscapeBytes(value.bytes_value());
  } else if (value.type()->IsString()) {
    if (escape_strings) {
      return GetEscapedString(value);
    } else {
      return value.string_value();
    }
  } else {
    return value.DebugString();
  }
}

std::string ToPrettyOutputStyle(const googlesql::Value& result,
                                bool is_value_table,
                                absl::Span<const std::string> column_names,
                                bool use_box_glyphs) {
  // The 'result' Value is expected to be a non-NULL array of struct values,
  // if it is not as expected then return an error string.
  if (result.is_null()) return "<null result>";
  const ArrayType* array_type = result.type()->AsArray();
  if (array_type == nullptr) return "<non-array result>";
  const int32_t num_result_rows = result.num_elements();

  int num_result_columns;
  if (is_value_table) {
    num_result_columns = 1;
  } else {
    const StructType* struct_row_type = array_type->element_type()->AsStruct();
    if (struct_row_type == nullptr) return "<non-array-of-struct result>";
    num_result_columns = struct_row_type->num_fields();
  }

  if (num_result_columns != column_names.size()) {
    return absl::StrCat("<mismatched column count: got ", num_result_columns,
                        ", expected ", column_names.size(), ">");
  }

  std::vector<size_t> max_column_lengths(num_result_columns, 0);
  for (int idx = 0; idx < num_result_columns; ++idx) {
    max_column_lengths[idx] = std::max(max_column_lengths[idx],
                                       EstimateGlyphWidth(column_names[idx]));
  }

  std::vector<std::vector<std::string>> row_values;
  for (int row_idx = 0; row_idx < num_result_rows; ++row_idx) {
    std::vector<std::string> column_values(num_result_columns);
    for (int col_idx = 0; col_idx < num_result_columns; ++col_idx) {
      const Value& row_value = result.element(row_idx);
      const Value& column_value =
          is_value_table ? row_value : row_value.field(col_idx);

      // Top-level values are printed in pretty-style, avoiding quotes around
      // strings and show multi-line strings over multiple lines.  Values within
      // an array or struct are displayed using the SQL syntax to produce a
      // literal of that value.
      column_values[col_idx] =
          ValueToOutputString(column_value, /*escape_strings=*/false);
      size_t max_line_length_of_column = 0;
      for (absl::string_view line :
           absl::StrSplit(column_values[col_idx], '\n')) {
        max_line_length_of_column =
            std::max(max_line_length_of_column, EstimateGlyphWidth(line));
      }
      max_column_lengths[col_idx] =
          std::max(max_column_lengths[col_idx], max_line_length_of_column);
    }
    row_values.push_back(column_values);
  }

  const BoxGlyphs& boxchars =
      use_box_glyphs ? kUnicodeBoxGlyphs : kAsciiBoxGlyphs;

  // Construct the row separator.
  std::string top_separator =
      GetRowSeparator(max_column_lengths, boxchars, SeparatorPosition::kTop);
  std::string mid_separator =
      GetRowSeparator(max_column_lengths, boxchars, SeparatorPosition::kMiddle);
  std::string bot_separator =
      GetRowSeparator(max_column_lengths, boxchars, SeparatorPosition::kBottom);

  std::string output = top_separator;
  // Add the column names. (Value tables do not have column names.)
  if (!is_value_table) {
    absl::StrAppend(&output,
                    GenerateRowStringFromColumns(column_names,
                                                 max_column_lengths, boxchars),
                    mid_separator);
  }
  for (int row_idx = 0; row_idx < num_result_rows; ++row_idx) {
    absl::StrAppend(&output,
                    GenerateRowStringFromColumns(row_values[row_idx],
                                                 max_column_lengths, boxchars));
  }
  absl::StrAppend(&output, bot_separator);

  return output;
}

std::string OutputPrettyStyleQueryResult(const googlesql::Value& result,
                                         const ResolvedStatement* resolved_stmt,
                                         bool use_box_glyphs) {
  std::optional<int64_t> num_rows_modified;
  bool is_value_table;
  std::vector<std::string> column_names;
  Value result_table;
  GOOGLESQL_CHECK_OK(GetOutputColumnInfo(resolved_stmt, result, &num_rows_modified,
                               &is_value_table, &result_table, &column_names));

  const std::string result_table_string = ToPrettyOutputStyle(
      result_table, is_value_table, column_names, use_box_glyphs);
  std::string output_text;
  if (result_table_string.empty()) {
    absl::StrAppend(&output_text, "** Could not format output **\n");
    absl::StrAppend(&output_text, result_table.Format(), "\n");
  } else {
    absl::StrAppend(&output_text, result_table_string);  // Includes newline
  }
  if (num_rows_modified.has_value()) {
    absl::StrAppend(&output_text,
                    "Number of rows modified: ", num_rows_modified.value(),
                    "\n");
  }
  return output_text;
}

std::string OutputPrettyStyleExpressionResult(const googlesql::Value& result,
                                              bool include_box,
                                              bool use_box_glyphs) {
  std::string value_str = ValueToOutputString(result, /*escape_strings=*/false);
  if (!include_box) {
    return value_str;
  }

  const BoxGlyphs& boxchars =
      use_box_glyphs ? kUnicodeBoxGlyphs : kAsciiBoxGlyphs;

  std::string top_separator = GetRowSeparator(
      {EstimateGlyphWidth(value_str)}, boxchars, SeparatorPosition::kTop);
  std::string bot_separator = GetRowSeparator(
      {EstimateGlyphWidth(value_str)}, boxchars, SeparatorPosition::kBottom);

  std::string output = top_separator;
  absl::StrAppend(&output,
                  GenerateRowStringFromColumns(
                      {value_str}, {EstimateGlyphWidth(value_str)}, boxchars));
  absl::StrAppend(&output, bot_separator);
  return output;
}

}  // namespace googlesql
