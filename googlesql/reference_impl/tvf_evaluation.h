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

#ifndef GOOGLESQL_REFERENCE_IMPL_TVF_EVALUATION_H_
#define GOOGLESQL_REFERENCE_IMPL_TVF_EVALUATION_H_

#include <memory>
#include <string>
#include <vector>

#include "googlesql/public/evaluator_table_iterator.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/reference_impl/evaluation.h"
#include "googlesql/reference_impl/operator.h"
#include "googlesql/reference_impl/type_helpers.h"
#include "googlesql/resolved_ast/resolved_ast_enums.pb.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "googlesql/resolved_ast/serialization.pb.h"
#include "absl/status/statusor.h"

namespace googlesql {

enum class TvfArgKind {
  kUndefined = 0,
  kScalar = 1,
  kRelation = 2,
  kModel = 3,
  kConnection = 4,
  kDescriptor = 5,
  kGraph = 6,
};

// Information about a TVF argument.
struct TvfArgumentInfo {
  std::string name;
  TvfArgKind kind;
};

// Creates an iterator for the algebrized body of a TVF to evaluate an
// invocation. Owns the `eval_context` to ensure it's a child context,
// not used by anyone else.
absl::StatusOr<std::unique_ptr<EvaluatorTableIterator>> CreateIterator(
    std::unique_ptr<RelationalOp> algebrized_body,
    std::vector<TVFSchemaColumn> output_columns,
    std::vector<int> output_column_indices, int num_extra_slots,
    std::unique_ptr<EvaluationContext> eval_context);

// Returns the relation materialized as an array of structs.
// The struct type corresponds to the schema as viewed from *inside* the
// function body. The actual relation arg may have extra columns and have a
// different order, which is why the API is an EvaluatorTableIterator, where we
// retrieve the values through GetValue(i).
absl::StatusOr<Value> MaterializeRelationAsArray(EvaluatorTableIterator* iter,
                                                 TypeFactory* type_factory);

}  // namespace googlesql

#endif  // GOOGLESQL_REFERENCE_IMPL_TVF_EVALUATION_H_
