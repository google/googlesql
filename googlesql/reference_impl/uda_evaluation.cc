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

#include "googlesql/reference_impl/uda_evaluation.h"

#include <memory>
#include <utility>
#include <vector>

#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/function.h"
#include "googlesql/public/functions/differential_privacy.pb.h"
#include "googlesql/public/type.h"
#include "googlesql/public/value.h"
#include "googlesql/reference_impl/evaluation.h"
#include "googlesql/reference_impl/function.h"
#include "googlesql/reference_impl/operator.h"
#include "googlesql/reference_impl/tuple.h"
#include "googlesql/reference_impl/type_helpers.h"
#include "googlesql/resolved_ast/resolved_ast_enums.pb.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "googlesql/resolved_ast/serialization.pb.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

class UserDefinedAggregateFunctionEvaluator
    : public SqlDefinedAggregateFunctionEvaluator {
 public:
  UserDefinedAggregateFunctionEvaluator(
      std::unique_ptr<RelationalOp> algebrized_tree,
      std::vector<UdaArgumentInfo> argument_infos)
      : algebrized_tree_(std::move(algebrized_tree)),
        argument_infos_(std::move(argument_infos)) {}
  ~UserDefinedAggregateFunctionEvaluator() override = default;

  void SetEvaluationContext(
      EvaluationContext* context,
      absl::Span<const TupleData* const> params) override {
    eval_context_ = context;
    params_ = std::vector<const TupleData*>(params.begin(), params.end());
  }

  absl::Status Reset() override {
    memory_accountant_ = std::make_unique<MemoryAccountant>(
        EvaluationOptions().max_intermediate_byte_size);
    inputs_ = std::make_unique<TupleDataDeque>(memory_accountant_.get());
    return absl::OkStatus();
  }

  absl::Status Accumulate(absl::Span<const Value*> args,
                          bool* stop_accumulation) override {
    // Accumulate each input row into `inputs` for use in GetFinalResult().
    std::vector<TupleSlot> tuple_slots;
    for (const Value* arg : args) {
      TupleSlot tuple_slot;
      tuple_slot.SetValue(*arg);
      tuple_slots.push_back(std::move(tuple_slot));
    }
    absl::Status status;
    bool ok = inputs_->PushBack(
        std::make_unique<TupleData>(std::move(tuple_slots)), &status);
    GOOGLESQL_RET_CHECK(ok == status.ok());
    return status;
  }

  absl::StatusOr<Value> GetFinalResult() override {
    GOOGLESQL_RET_CHECK(eval_context_ != nullptr)
        << "UserDefinedAggregateFunctionEvaluator must have EvaluationContext "
        << "set before calling GetFinalResult().";
    // Create a local context to evaluate the UDA function body on the
    // accumulated rows.
    std::unique_ptr<EvaluationContext> local_context =
        eval_context_->MakeChildContext();
    local_context->set_active_group_rows(inputs_.get());

    std::shared_ptr<TupleSlot::SharedProtoState> shared_state =
        std::make_shared<TupleSlot::SharedProtoState>();
    for (const auto& info : argument_infos_) {
      if (info.is_aggregate) {
        continue;
      }
      Value arg;
      VirtualTupleSlot virtual_slot(&arg, &shared_state);
      absl::Status status;
      GOOGLESQL_RET_CHECK(info.expr != nullptr);
      info.expr->Eval(params_, eval_context_, &virtual_slot, &status);
      GOOGLESQL_RETURN_IF_ERROR(status);
      GOOGLESQL_RETURN_IF_ERROR(
          local_context->AddFunctionArgumentRef(info.argument_name, arg));
    }

    GOOGLESQL_RETURN_IF_ERROR(
        algebrized_tree_->SetSchemasForEvaluation(/*params_schemas=*/{}));
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<TupleIterator> iter,
        algebrized_tree_->Eval(/*params=*/{},
                               /*num_extra_slots=*/0, local_context.get()));
    Value result;
    while (true) {
      const TupleData* next_input = iter->Next();
      if (next_input == nullptr) {
        GOOGLESQL_RETURN_IF_ERROR(iter->Status());
        break;
      }
      GOOGLESQL_RET_CHECK_GE(next_input->num_slots(), 1);
      // ComputeOp stores the result of the function expression evaluation
      // in the last slot.
      result = next_input->slot(next_input->num_slots() - 1).value();
    }
    return result;
  }

 private:
  std::unique_ptr<RelationalOp> algebrized_tree_;
  std::vector<UdaArgumentInfo> argument_infos_;
  std::vector<const TupleData*> params_;
  std::unique_ptr<MemoryAccountant> memory_accountant_;
  std::unique_ptr<TupleDataDeque> inputs_;
  EvaluationContext* eval_context_;
};

std::unique_ptr<AggregateFunctionEvaluator>
MakeUserDefinedAggregateFunctionEvaluator(
    std::unique_ptr<RelationalOp> algebrized_tree,
    std::vector<UdaArgumentInfo> argument_infos) {
  return std::make_unique<UserDefinedAggregateFunctionEvaluator>(
      std::move(algebrized_tree), std::move(argument_infos));
}

}  // namespace googlesql
