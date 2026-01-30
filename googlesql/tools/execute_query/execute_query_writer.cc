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

#include "googlesql/tools/execute_query/execute_query_writer.h"

#include <iostream>
#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/evaluator_table_iterator.h"
#include "googlesql/public/types/array_type.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/tools/execute_query/output_query_result.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

// Prints the result of executing a query. Currently requires loading all the
// results into memory to format pretty output.
absl::Status PrintResults(std::unique_ptr<EvaluatorTableIterator> iter,
                          std::ostream& out, bool use_box_glyphs) {
  TypeFactory type_factory;

  std::vector<StructField> struct_fields;
  struct_fields.reserve(iter->NumColumns());
  for (int i = 0; i < iter->NumColumns(); ++i) {
    struct_fields.emplace_back(iter->GetColumnName(i), iter->GetColumnType(i));
  }

  const StructType* struct_type;
  GOOGLESQL_RETURN_IF_ERROR(type_factory.MakeStructType(struct_fields, &struct_type));

  std::vector<Value> rows;
  while (true) {
    if (!iter->NextRow()) {
      GOOGLESQL_RETURN_IF_ERROR(iter->Status());
      break;
    }

    std::vector<Value> fields;
    fields.reserve(iter->NumColumns());
    for (int i = 0; i < iter->NumColumns(); ++i) {
      fields.push_back(iter->GetValue(i));
    }

    rows.push_back(Value::Struct(struct_type, fields));
  }

  const ArrayType* array_type;
  GOOGLESQL_RETURN_IF_ERROR(type_factory.MakeArrayType(struct_type, &array_type));

  const Value result = Value::Array(array_type, rows);

  std::vector<std::string> column_names;
  column_names.reserve(iter->NumColumns());
  for (int i = 0; i < iter->NumColumns(); ++i) {
    column_names.push_back(iter->GetColumnName(i));
  }

  out << ToPrettyOutputStyle(result,
                             /*is_value_table=*/false, column_names,
                             use_box_glyphs)
      << '\n';

  return absl::OkStatus();
}

ExecuteQueryStreamWriter::ExecuteQueryStreamWriter(std::ostream& out,
                                                   bool use_box_glyphs)
    : stream_{out}, use_box_glyphs_(use_box_glyphs) {}

absl::Status ExecuteQueryStreamWriter::resolved(const ResolvedNode& ast,
                                                bool post_rewrite) {
  if (post_rewrite) {
    stream_ << "\nResolved AST rewritten to:\n";
  }
  stream_ << ast.DebugString(ResolvedNode::DebugStringConfig{
                 .use_box_glyphs = use_box_glyphs_})
          << '\n';
  return absl::OkStatus();
}

absl::Status ExecuteQueryStreamWriter::explained(const ResolvedNode& ast,
                                                 absl::string_view explain) {
  stream_ << explain << '\n';
  return absl::OkStatus();
}

absl::Status ExecuteQueryStreamWriter::executed(
    const ResolvedNode& ast, std::unique_ptr<EvaluatorTableIterator> iter) {
  return PrintResults(std::move(iter), stream_, use_box_glyphs_);
}

absl::Status ExecuteQueryStreamWriter::executed_multi(
    const ResolvedNode& ast,
    std::vector<absl::StatusOr<std::unique_ptr<EvaluatorTableIterator>>>
        results) {
  for (auto& result : results) {
    if (result.ok()) {
      GOOGLESQL_RETURN_IF_ERROR(
          PrintResults(std::move(*result), stream_, use_box_glyphs_));
    } else {
      stream_ << "Error: " << result.status().message() << "\n";
    }
  }
  return absl::OkStatus();
}

absl::Status ExecuteQueryStreamWriter::ExecutedExpression(
    const ResolvedNode& ast, const Value& value) {
  stream_ << OutputPrettyStyleExpressionResult(value, /*include_box=*/false);
  return absl::OkStatus();
}

void ExecuteQueryStreamWriter::FlushStatement(bool at_end,
                                              std::string error_msg) {
  if (!error_msg.empty()) {
    stream_ << "ERROR: " << error_msg << "\n";
  }
}

}  // namespace googlesql
