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

// This file contains the code for evaluating UNPIVOT operators.

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/type.h"
#include "googlesql/public/value.h"
#include "googlesql/reference_impl/evaluation.h"
#include "googlesql/reference_impl/operator.h"
#include "googlesql/reference_impl/tuple.h"
#include "googlesql/reference_impl/variable_id.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "googlesql/base/status_macros.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/types/span.h"

namespace googlesql {

namespace {

class UnpivotTupleIterator : public TupleIterator {
 public:
  UnpivotTupleIterator(absl::Span<const ExprArg* const> projected_input_columns,
                       absl::Span<const VariableId> value_columns,
                       VariableId label_column,
                       absl::Span<const Value> label_values,
                       absl::Span<const ExprArg* const> unpivot_columns,
                       const RelationalOp* input,
                       absl::Span<const TupleData* const> params,
                       int num_extra_slots, bool include_nulls,
                       EvaluationContext* context)
      : projected_input_columns_(projected_input_columns),
        value_columns_(value_columns),
        label_column_(label_column),
        label_values_(label_values),
        unpivot_columns_(unpivot_columns),
        input_(input),
        params_(DeepCopyTupleDatas(params)),
        params_ptrs_(StripSharedPtrs(params_)),
        num_extra_slots_(num_extra_slots),
        include_nulls_(include_nulls),
        context_(context) {}

  bool PreservesOrder() const override { return false; }

  const TupleSchema& Schema() const override {
    if (schema_ == nullptr) {
      std::vector<VariableId> variables;
      for (const ExprArg* arg : projected_input_columns_) {
        variables.push_back(arg->variable());
      }
      for (const VariableId& var : value_columns_) {
        variables.push_back(var);
      }
      variables.push_back(label_column_);
      schema_ = std::make_unique<TupleSchema>(variables);
    }
    return *schema_;
  }

  TupleData* Next() override {
    if (!initialized_) {
      status_ = Initialize();
      if (!status_.ok()) {
        return nullptr;
      }
      initialized_ = true;
    }

    if (!status_.ok()) {
      return nullptr;
    }

    while (true) {
      if (current_input_row_ == nullptr ||
          current_label_index_ >= label_values_.size()) {
        current_input_row_ = input_iter_->Next();
        if (current_input_row_ == nullptr) return nullptr;
        current_label_index_ = 0;
      }

      int num_values = static_cast<int>(value_columns_.size());
      int label_index = current_label_index_++;

      std::vector<Value> unpivoted_values;
      unpivoted_values.reserve(num_values);
      bool all_null = true;

      std::vector<const TupleData*> extended_params_vec = params_ptrs_;
      extended_params_vec.push_back(current_input_row_);
      absl::Span<const TupleData* const> extended_params(extended_params_vec);

      for (int i = 0; i < num_values; ++i) {
        int idx = label_index * num_values + i;
        TupleSlot slot;
        absl::Status status;
        if (!unpivot_columns_[idx]->value_expr()->EvalSimple(
                extended_params, context_, &slot, &status)) {
          status_ = status;
          return nullptr;
        }
        if (all_null && !slot.value().is_null()) {
          all_null = false;
        }
        unpivoted_values.push_back(slot.value());
      }

      if (all_null && !include_nulls_) {
        continue;
      }

      auto output_row = std::make_unique<TupleData>(
          Schema().variables().size() + num_extra_slots_);
      int num_projected = static_cast<int>(projected_input_columns_.size());
      for (int i = 0; i < num_projected; ++i) {
        TupleSlot slot;
        absl::Status status;
        if (!projected_input_columns_[i]->value_expr()->EvalSimple(
                extended_params, context_, &slot, &status)) {
          status_ = status;
          return nullptr;
        }
        output_row->mutable_slot(i)->SetValue(slot.value());
      }
      for (int i = 0; i < num_values; ++i) {
        output_row->mutable_slot(num_projected + i)
            ->SetValue(unpivoted_values[i]);
      }
      output_row->mutable_slot(num_projected + num_values)
          ->SetValue(label_values_[label_index]);

      current_output_row_ = std::move(output_row);
      return current_output_row_.get();
    }
  }

  absl::Status Initialize() {
    GOOGLESQL_ASSIGN_OR_RETURN(
        input_iter_,
        input_->Eval(params_ptrs_, /*num_extra_slots=*/0, context_));
    return absl::OkStatus();
  }

  absl::Status Status() const override {
    GOOGLESQL_RETURN_IF_ERROR(status_);
    if (input_iter_ == nullptr) {
      return absl::OkStatus();
    }
    return input_iter_->Status();
  }

  std::string DebugString() const override { return "UnpivotTupleIterator"; }

 private:
  const absl::Span<const ExprArg* const> projected_input_columns_;
  const absl::Span<const VariableId> value_columns_;
  const VariableId label_column_;
  const absl::Span<const Value> label_values_;
  const absl::Span<const ExprArg* const> unpivot_columns_;

  const RelationalOp* input_;
  const std::vector<std::shared_ptr<const TupleData>> params_;
  const std::vector<const TupleData*> params_ptrs_;
  const int num_extra_slots_;
  const bool include_nulls_;
  EvaluationContext* context_;
  bool initialized_ = false;

  mutable std::unique_ptr<TupleSchema> schema_;
  absl::Status status_ = absl::OkStatus();
  std::unique_ptr<TupleIterator> input_iter_;
  const TupleData* current_input_row_ = nullptr;
  int current_label_index_ = 0;
  std::unique_ptr<TupleData> current_output_row_;
};

}  // namespace

UnpivotOp::UnpivotOp(
    std::vector<std::unique_ptr<ExprArg>> projected_input_columns,
    std::vector<VariableId> value_columns, VariableId label_column,
    std::vector<Value> label_values,
    std::vector<std::unique_ptr<ExprArg>> unpivot_columns,
    std::unique_ptr<RelationalOp> input, bool include_nulls)
    : value_columns_(std::move(value_columns)),
      label_column_(label_column),
      label_values_(std::move(label_values)),
      include_nulls_(include_nulls) {
  SetArgs(kProjectedInputColumn, std::move(projected_input_columns));
  SetArgs(kUnpivotColumn, std::move(unpivot_columns));
  SetArg(kInput, std::make_unique<RelationalArg>(std::move(input)));
  (void)set_is_order_preserving(false);
}

absl::StatusOr<std::unique_ptr<UnpivotOp>> UnpivotOp::Create(
    std::vector<std::unique_ptr<ExprArg>> projected_input_columns,
    std::vector<VariableId> value_columns, VariableId label_column,
    std::vector<Value> label_values,
    std::vector<std::unique_ptr<ExprArg>> unpivot_columns,
    std::unique_ptr<RelationalOp> input, bool include_nulls) {
  return absl::WrapUnique(new UnpivotOp(
      std::move(projected_input_columns), std::move(value_columns),
      label_column, std::move(label_values), std::move(unpivot_columns),
      std::move(input), include_nulls));
}

absl::StatusOr<std::unique_ptr<TupleIterator>> UnpivotOp::CreateIterator(
    absl::Span<const TupleData* const> params, int num_extra_slots,
    EvaluationContext* context) const {
  auto iter = std::make_unique<UnpivotTupleIterator>(
      GetArgs<ExprArg>(kProjectedInputColumn), value_columns_, label_column_,
      label_values_, GetArgs<ExprArg>(kUnpivotColumn),
      GetArg(kInput)->relational_op(), params, num_extra_slots, include_nulls_,
      context);
  return MaybeReorder(std::move(iter), context);
}

absl::Status UnpivotOp::SetSchemasForEvaluation(
    absl::Span<const TupleSchema* const> params_schemas) {
  GOOGLESQL_RETURN_IF_ERROR(
      GetMutableArg(kInput)->mutable_relational_op()->SetSchemasForEvaluation(
          params_schemas));

  std::unique_ptr<TupleSchema> input_schema =
      GetArg(kInput)->relational_op()->CreateOutputSchema();
  std::vector<const TupleSchema*> extended_params =
      ConcatSpans(params_schemas, {input_schema.get()});

  for (auto* col : GetMutableArgs<ExprArg>(kProjectedInputColumn)) {
    GOOGLESQL_RETURN_IF_ERROR(
        col->mutable_value_expr()->SetSchemasForEvaluation(extended_params));
  }
  for (auto* col : GetMutableArgs<ExprArg>(kUnpivotColumn)) {
    GOOGLESQL_RETURN_IF_ERROR(
        col->mutable_value_expr()->SetSchemasForEvaluation(extended_params));
  }
  return absl::OkStatus();
}

std::unique_ptr<TupleSchema> UnpivotOp::CreateOutputSchema() const {
  std::vector<VariableId> variables;
  for (const ExprArg* arg : GetArgs<ExprArg>(kProjectedInputColumn)) {
    variables.push_back(arg->variable());
  }
  for (const VariableId& var : value_columns_) {
    variables.push_back(var);
  }
  variables.push_back(label_column_);
  return std::make_unique<TupleSchema>(variables);
}

std::string UnpivotOp::IteratorDebugString() const {
  return absl::StrCat("UnpivotOp(",
                      GetArg(kInput)->relational_op()->IteratorDebugString(),
                      ")");
}

std::string UnpivotOp::DebugInternal(const std::string& indent,
                                     bool verbose) const {
  std::string result = absl::StrCat(
      "UnpivotOp(",
      ArgDebugString({"projected_input_columns", "unpivot_columns", "input"},
                     {kN, kN, k1}, indent, verbose, /*more_children=*/true));
  absl::StrAppend(&result, indent, kIndentFork, "value_columns: [",
                  absl::StrJoin(value_columns_, ", ",
                                [](std::string* out, const VariableId& var) {
                                  absl::StrAppend(out, var.ToString());
                                }),
                  "]");
  absl::StrAppend(&result, indent, kIndentFork,
                  "label_column: ", label_column_.ToString());
  absl::StrAppend(&result, indent, kIndentFork, "label_values: [",
                  absl::StrJoin(label_values_, ", ",
                                [](std::string* out, const Value& val) {
                                  absl::StrAppend(out, val.ShortDebugString());
                                }),
                  "]");
  absl::StrAppend(&result, indent, kIndentFork,
                  "include_nulls: ", include_nulls_ ? "true" : "false", ")");
  return result;
}

}  // namespace googlesql
