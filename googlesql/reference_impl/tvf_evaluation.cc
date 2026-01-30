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

#include "googlesql/reference_impl/tvf_evaluation.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/evaluator_table_iterator.h"
#include "googlesql/public/function.h"
#include "googlesql/public/functions/differential_privacy.pb.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/type.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/reference_impl/evaluation.h"
#include "googlesql/reference_impl/operator.h"
#include "googlesql/reference_impl/tuple.h"
#include "googlesql/reference_impl/type_helpers.h"
#include "googlesql/resolved_ast/resolved_ast_enums.pb.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "googlesql/resolved_ast/serialization.pb.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace {

// Similar to InputRelationIterator, but for the output of a SQL TVF.
// Owns the evaluation context, which is usually a child context.
// Owns the algebrized body of the TVF, to ensure it outlives this iterator.
class OutputRelationIterator : public InputRelationIterator {
 public:
  OutputRelationIterator(
      std::vector<std::pair<std::string, const Type*>> columns,
      const std::vector<int> tuple_indexes,
      std::unique_ptr<EvaluationContext> context,
      std::unique_ptr<TupleIterator> iter,
      std::unique_ptr<RelationalOp> algebrized_body)
      : InputRelationIterator(std::move(columns), std::move(tuple_indexes),
                              context.get(), std::move(iter)),
        context_(std::move(context)),
        algebrized_body_(std::move(algebrized_body)) {}

 private:
  // Used only when providing the output of a SQL TVF. The algebrized body needs
  // to outlive the iterator
  std::unique_ptr<EvaluationContext> context_;
  std::unique_ptr<RelationalOp> algebrized_body_;
};

}  // namespace

absl::StatusOr<std::unique_ptr<EvaluatorTableIterator>> CreateIterator(
    std::unique_ptr<RelationalOp> algebrized_body,
    std::vector<TVFSchemaColumn> output_columns,
    std::vector<int> output_column_indices, int num_extra_slots,
    std::unique_ptr<EvaluationContext> eval_context) {
  // Note: we pass empty params_schemas and empty params, because the
  // TVF body should not allow parameter/sys var references.
  GOOGLESQL_RETURN_IF_ERROR(
      algebrized_body->SetSchemasForEvaluation(/*params_schemas=*/{}))
      << algebrized_body->DebugString();

  // Note: we pass empty params! Do not pass `params` here!
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<TupleIterator> tuple_iterator,
                   algebrized_body->Eval(/*params=*/{}, num_extra_slots,
                                         eval_context.get()));

  GOOGLESQL_RET_CHECK_EQ(output_columns.size(), output_column_indices.size());

  std::vector<std::pair<std::string, const Type*>> columns;
  columns.reserve(output_columns.size());
  for (int i = 0; i < output_columns.size(); ++i) {
    const TVFSchemaColumn& column = output_columns[i];
    columns.push_back({column.name, column.type});
  }

  return std::make_unique<OutputRelationIterator>(
      std::move(columns), std::move(output_column_indices),
      std::move(eval_context), std::move(tuple_iterator),
      std::move(algebrized_body));
}

absl::StatusOr<Value> MaterializeRelationAsArray(EvaluatorTableIterator* iter,
                                                 TypeFactory* type_factory) {
  std::vector<StructField> fields;
  fields.reserve(iter->NumColumns());
  for (int i = 0; i < iter->NumColumns(); ++i) {
    fields.emplace_back(iter->GetColumnName(i), iter->GetColumnType(i));
  }
  const StructType* struct_type;
  GOOGLESQL_RETURN_IF_ERROR(
      type_factory->MakeStructType(std::move(fields), &struct_type));

  std::vector<Value> rows_as_structs;
  while (iter->NextRow()) {
    std::vector<Value> row;
    row.reserve(iter->NumColumns());
    for (int i = 0; i < iter->NumColumns(); ++i) {
      row.push_back(iter->GetValue(i));
    }
    GOOGLESQL_ASSIGN_OR_RETURN(Value row_as_struct, Value::MakeStruct(struct_type, row));
    rows_as_structs.push_back(std::move(row_as_struct));
  }
  GOOGLESQL_RETURN_IF_ERROR(iter->Status());

  const ArrayType* array_type;
  GOOGLESQL_RETURN_IF_ERROR(type_factory->MakeArrayType(struct_type, &array_type));
  return Value::MakeArray(array_type, std::move(rows_as_structs));
}

}  // namespace googlesql
