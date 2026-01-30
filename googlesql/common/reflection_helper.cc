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

#include "googlesql/common/reflection_helper.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "googlesql/common/reflection.pb.h"
#include "googlesql/public/strings.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "google/protobuf/repeated_ptr_field.h"

namespace googlesql {
namespace reflection {

// Try to limit column lists to this width, but don't add newlines
// inside column names.
static const size_t kMaxColumnListWidth = 80;

// Quote names in the formatted output.
// Empty string is used for missing names and shouldn't be quoted as ``.
// Some special marker names with "<...>" also shouldn't get quoted.
static std::string QuoteName(absl::string_view name) {
  if (name.empty() || name == "<unnamed>" || name == "<value>")
    return std::string(name);
  return ToIdentifierLiteral(name);
}

// Quote names with `QuoteName` and return them as a comma-separated list.
// Add newlines to try to keep width under kMaxColumnListWidth.
static std::string QuoteNameList(
    const google::protobuf::RepeatedPtrField<std::string>& names) {
  std::string output;
  std::string next_line;
  for (int i = 0; i < names.size(); ++i) {
    std::string quoted_name = QuoteName(names[i]);
    if (i != names.size() - 1) {
      absl::StrAppend(&quoted_name, ", ");
    }
    if (!next_line.empty() &&
        next_line.size() + quoted_name.size() >= kMaxColumnListWidth) {
      next_line.pop_back();  // Remove the trailing space.
      absl::StrAppend(&output, next_line, "\n");
      next_line.clear();
    }
    absl::StrAppend(&next_line, quoted_name);
  }
  if (!next_line.empty()) {
    absl::StrAppend(&output, next_line);
  }
  return output;
}

// The absl helper for trimming only works on string_view.
static void StripTrailingSpaces(std::string* str) {
  while (!str->empty() && str->back() == ' ') {
    str->pop_back();
  }
}

// Format the header line above a table, including the newline.
static std::string FormatHeader(absl::string_view header) {
  return absl::StrCat("**", header, "**:\n");
}

// Helper function to format a vector of string into a table, with a header.
// The size of `header` dictates the number of columns to print.
// Missing or extra column values in `rows[k]` are ignored.
// When cell values include newlines, rows are wrapped onto multiple lines.
static std::string FormatTableHeaderAndRows(
    absl::Span<const std::string> header,
    absl::Span<const std::vector<std::string>> rows) {
  std::string output;

  // Wrap rows in multiple lines if cells have newlines.
  // Each row from `rows` becomes one or more in `rows_wrapped`.
  std::vector<std::vector<std::string>> rows_wrapped;
  for (const auto& row : rows) {
    // Split each cell in the row by newline.
    std::vector<std::vector<std::string>> cells_and_lines;
    size_t max_lines = 0;
    for (int i = 0; i < std::min(header.size(), row.size()); ++i) {
      cells_and_lines.push_back(absl::StrSplit(row[i], '\n'));
      max_lines = std::max(max_lines, cells_and_lines.back().size());
    }

    if (max_lines == 1) {
      rows_wrapped.push_back(row);
    } else {
      // Build wrapped rows, padding so we add `max_lines` rows for each cell.
      for (int line_num = 0; line_num < max_lines; ++line_num) {
        std::vector<std::string> partial_row;
        for (const auto& cell_lines : cells_and_lines) {
          if (line_num < cell_lines.size()) {
            partial_row.push_back(cell_lines[line_num]);
          } else {
            partial_row.push_back("");
          }
        }
        rows_wrapped.push_back(partial_row);
      }
    }
  }

  // Calculate column widths based on wrapped rows.
  std::vector<size_t> wrapped_widths(header.size());
  for (int i = 0; i < header.size(); ++i) {
    wrapped_widths[i] = header[i].size();
  }
  for (const auto& row : rows_wrapped) {
    for (int i = 0; i < std::min(header.size(), row.size()); ++i) {
      wrapped_widths[i] = std::max(wrapped_widths[i], row[i].size());
    }
  }

  // Format headers.
  for (int i = 0; i < header.size(); ++i) {
    absl::StrAppend(&output,
                    absl::StrFormat("%-*s  ", wrapped_widths[i], header[i]));
  }
  StripTrailingSpaces(&output);
  absl::StrAppend(&output, "\n");

  // Format separator line.
  for (int i = 0; i < wrapped_widths.size(); ++i) {
    absl::StrAppend(&output,
                    absl::StrFormat("%-*s  ", wrapped_widths[i],
                                    std::string(wrapped_widths[i], '-')));
  }
  StripTrailingSpaces(&output);
  absl::StrAppend(&output, "\n");

  // Print the wrapped rows.
  for (const auto& wrapped_row : rows_wrapped) {
    for (int i = 0; i < std::min(header.size(), wrapped_row.size()); ++i) {
      absl::StrAppend(&output, absl::StrFormat("%-*s  ", wrapped_widths[i],
                                               wrapped_row[i]));
    }
    StripTrailingSpaces(&output);
    absl::StrAppend(&output, "\n");
  }

  return output;
}

std::string FormatResultTable(const reflection::ResultTable& result_table,
                              bool include_table_schema) {
  std::string output;

  // Make the tables for Columns and Pseudo-columns.
  // Only include Table Alias column if any exist.
  bool has_table_aliases = !result_table.table_alias().empty();
  for (const reflection::Column& column : result_table.column()) {
    if (!column.table_alias().empty()) {
      has_table_aliases = true;
      break;
    }
  }

  std::vector<std::string> columns_header = {"Column Name", "Type"};
  if (has_table_aliases) {
    columns_header.insert(columns_header.begin(), "Table Alias");
  }

  auto MakeColumnsRow = [has_table_aliases](const reflection::Column& column) {
    std::vector<std::string> row;
    if (has_table_aliases) {
      row.push_back(QuoteName(column.table_alias()));
    }
    row.push_back(column.column_name().empty()
                      ? "<unnamed>"
                      : QuoteName(column.column_name()));
    row.emplace_back(column.type());
    return row;
  };

  std::vector<std::vector<std::string>> columns_rows;
  for (const reflection::Column& column : result_table.column()) {
    columns_rows.push_back(MakeColumnsRow(column));
  }

  std::vector<std::vector<std::string>> pseudo_columns_rows;
  for (const reflection::Column& pseudo_column : result_table.pseudo_column()) {
    pseudo_columns_rows.push_back(MakeColumnsRow(pseudo_column));
  }

  auto MakeTableAliasRows = [](const auto& table_alias_list,
                               std::vector<std::string>* header) {
    bool has_pseudo_columns = false;
    std::vector<std::vector<std::string>> table_aliases_rows;
    for (const TableAlias& table_alias : table_alias_list) {
      std::vector<std::string> quoted_column_names;
      for (const std::string& column_name : table_alias.column_name()) {
        quoted_column_names.push_back(QuoteName(column_name));
      }
      std::vector<std::string> quoted_pseudo_column_names;
      for (const std::string& column_name : table_alias.pseudo_column_name()) {
        quoted_column_names.push_back(QuoteName(column_name));
        has_pseudo_columns = true;
      }

      table_aliases_rows.push_back(
          {QuoteName(table_alias.name()),
           QuoteNameList(table_alias.column_name()),
           QuoteNameList(table_alias.pseudo_column_name())});
    }
    // Don't print the "Pseudo-columns" column if there aren't any.
    if (!has_pseudo_columns) {
      header->pop_back();
    }
    return table_aliases_rows;
  };

  // Make the table for Table Aliases.
  std::vector<std::string> table_aliases_header = {"Table Alias", "Columns",
                                                   "Pseudo-columns"};
  std::vector<std::vector<std::string>> table_aliases_rows =
      MakeTableAliasRows(result_table.table_alias(), &table_aliases_header);

  if (include_table_schema) {
    if (!result_table.column().empty()) {
      absl::StrAppend(&output, FormatHeader("Columns"),
                      FormatTableHeaderAndRows(columns_header, columns_rows));
    }

    if (!result_table.pseudo_column().empty()) {
      absl::StrAppend(
          &output, "\n", FormatHeader("Pseudo-columns"),
          FormatTableHeaderAndRows(columns_header, pseudo_columns_rows));
    }

    if (has_table_aliases && !result_table.table_alias().empty()) {
      absl::StrAppend(
          &output, "\n", FormatHeader("Table Aliases"),
          FormatTableHeaderAndRows(table_aliases_header, table_aliases_rows));
    }
  }

  if (!result_table.common_table_expression().empty()) {
    // Make the table for Table Aliases.
    std::vector<std::string> header = {"Name", "Columns", "Pseudo-columns"};
    std::vector<std::vector<std::string>> rows =
        MakeTableAliasRows(result_table.common_table_expression(), &header);

    absl::StrAppend(&output, "\n", FormatHeader("Common table expressions"),
                    FormatTableHeaderAndRows(header, rows));
  }

  // Add other description lines below the tables.
  if (result_table.is_value_table() || result_table.is_ordered()) {
    absl::StrAppend(&output, "\n");
    if (include_table_schema && result_table.is_value_table()) {
      absl::StrAppend(&output, "Result is a value table.\n");
    }
    if (result_table.is_ordered()) {
      absl::StrAppend(&output, "Result is ordered.\n");
    }
  }

  return output;
}

}  // namespace reflection
}  // namespace googlesql
