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

// This file contains the code for evaluating aggregate functions.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/common/status_payload_utils.h"
#include "googlesql/common/thread_stack.h"
#include "googlesql/public/collator.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/value.h"
#include "googlesql/reference_impl/common.h"
#include "googlesql/reference_impl/evaluation.h"
#include "googlesql/reference_impl/function.h"
#include "googlesql/reference_impl/operator.h"
#include "googlesql/reference_impl/tuple.h"
#include "googlesql/reference_impl/tuple_comparator.h"
#include "googlesql/reference_impl/variable_id.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_collation.h"
#include "absl/base/nullability.h"
#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/flags/flag.h"
#include "googlesql/base/check.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status.h"
#include "google/rpc/status.pb.h"
#include "googlesql/base/status_macros.h"

using googlesql::types::EmptyStructType;
using googlesql::values::Bool;

namespace googlesql {

namespace {

static constexpr int64_t NoGroupingSetId() { return -1; }

// A struct holding the accumulator and other additional information about the
// current aggregate arg.
struct AggregateArgAccumulatorParam {
  // The accumulator for the current aggregate_arg.
  std::unique_ptr<AggregateArgAccumulator> accumulator;
  bool stop_bit;
  // Whether the current aggregate_arg is a grouping function call.
  bool is_grouping_function;
};

// The bool is true if we should stop accumulation for the corresponding
// accumulator.
using AccumulatorList = std::vector<AggregateArgAccumulatorParam>;

// The data associated with a grouping key during aggregation.
class GroupValue {
 public:
  // Reserves bytes for 'key' with 'accountant' and returns a new GroupValue.
  static absl::StatusOr<std::unique_ptr<GroupValue>> Create(
      std::unique_ptr<TupleData> key, MemoryAccountant* accountant) {
    const int64_t bytes_size = key->GetPhysicalByteSize();
    absl::Status status;
    if (!accountant->RequestBytes(bytes_size, &status)) {
      return status;
    }
    return absl::WrapUnique(
        new GroupValue(std::move(key), bytes_size, accountant));
  }

  GroupValue(const GroupValue&) = delete;
  GroupValue& operator=(const GroupValue&) = delete;

  ~GroupValue() { ConsumeKey(); }

  // Unregisters the key with the 'accountant' and returns it.
  std::unique_ptr<TupleData> ConsumeKey() {
    if (key_ != nullptr) {
      accountant_->ReturnBytes(key_physical_byte_size_);
    }
    return std::move(key_);
  }

  AccumulatorList* mutable_accumulator_list() { return &accumulator_list_; }

  const absl::Status& accumulator_error(int i) {
    return accumulator_errors_[i];
  }

  std::vector<absl::Status>& mutable_accumulator_errors() {
    return accumulator_errors_;
  }

  const std::vector<absl::Status>& accumulator_errors() const {
    return accumulator_errors_;
  }

 private:
  GroupValue(std::unique_ptr<TupleData> key, int64_t key_physical_byte_size,
             MemoryAccountant* accountant)
      : key_(std::move(key)),
        key_physical_byte_size_(key_physical_byte_size),
        accountant_(accountant) {}

  std::unique_ptr<TupleData> key_;
  int64_t key_physical_byte_size_ = 0;
  MemoryAccountant* accountant_ = nullptr;
  AccumulatorList accumulator_list_;
  std::vector<absl::Status> accumulator_errors_;
};

// When array values without known orders are used as grouping keys, we can't
// know if two rows that have arrays with the same bag of values will group
// together (by that key) or not. The query is non-deterministic. Tracking
// collisions of bags of values is expensive. Tracking that within nested
// values (structs with arrays etc) is super exensive. On the other end of the
// spectrum, setting the non-determinism signal the first time we see an
// unordered array as a group by key disqualifies a lot of useful queries.
//
// This data structure is helping us find an inexpensive -- somewhat arbitrary
// -- middle ground. For each grouping key, we find all of the array values
// in the nested value. If there is even a collision where an array without
// known order co-exists in the same column as any other array (known or
// unknown order) that has the same length, then we will set the
// non-determinism signal.
struct UnorderedArrayCollisionTracker {
  struct PerArrayLengthData {
    int total_examples_seen = 0;
    bool unordered_example_seen = false;
  };
  using PerKeyData =
      absl::flat_hash_map</*array_length*/ int, PerArrayLengthData>;
  absl::flat_hash_map</*key_index*/ int, PerKeyData> tracking;

  // Finds all the arrays nested in the value and records presence of those
  // array lengths and whether they are associated with an unordered array.
  bool CouldIndicateNondetermisticGrouping(int key_index, const Value& value) {
    absl::flat_hash_set<int> array_lengths;
    std::vector<const Value*> pending_values;
    pending_values.push_back(&value);
    while (!pending_values.empty()) {
      const Value& current_value = *pending_values.back();
      pending_values.pop_back();
      if (current_value.is_null()) {
        continue;
      }
      if (current_value.type()->IsArray()) {
        array_lengths.insert(current_value.num_elements());
        if (current_value.type()->AsArray()->element_type()->IsStruct()) {
          for (int i = 0; i < current_value.num_elements(); ++i) {
            pending_values.push_back(&current_value.element(i));
          }
        }
      }
      if (current_value.type()->IsStruct()) {
        for (int i = 0; i < current_value.num_fields(); ++i) {
          if (current_value.type()->IsArray() ||
              current_value.type()->IsStruct()) {
            pending_values.push_back(&current_value.field(i));
          }
        }
      }
    }

    bool contains_unordered_array =
        InternalValue::ContainsArrayWithUncertainOrder(value);
    for (int array_length : array_lengths) {
      PerKeyData& key_index_data = tracking[key_index];
      PerArrayLengthData& array_length_data = key_index_data[array_length];
      array_length_data.total_examples_seen++;
      if (contains_unordered_array) {
        array_length_data.unordered_example_seen = true;
      }
      if (array_length_data.unordered_example_seen &&
          array_length_data.total_examples_seen > 1) {
        return true;
      }
    }
    return false;
  }
};

// Prepare collators for each KeyArg in `keys`.
absl::StatusOr<CollatorList> SetupCollators(
    absl::Span<const TupleData* const> params,
    absl::Span<const KeyArg* const> keys, EvaluationContext* context) {
  CollatorList collators;
  for (const KeyArg* key : keys) {
    if (key->collation() == nullptr) {
      collators.push_back(nullptr);
      continue;
    }
    TupleSlot collation_slot;
    absl::Status status;
    if (!key->collation()->EvalSimple(params, context, &collation_slot,
                                      &status)) {
      return status;
    }

    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const GoogleSqlCollator> collator,
        GetCollatorFromResolvedCollationValue(collation_slot.value()));
    collators.push_back(std::move(collator));
  }
  return collators;
}

bool IsGroupingFunction(const AggregateFunctionCallExpr* func_expr) {
  ABSL_DCHECK(func_expr != nullptr);
  const BuiltinAggregateFunction* builtin_func =
      dynamic_cast<const BuiltinAggregateFunction*>(func_expr->function());
  if (builtin_func != nullptr) {
    return builtin_func->kind() == FunctionKind::kGrouping;
  }
  return false;
}

// TODO: Extend to support Array and Struct later.
// Returns Bytes value which represents the sort key for input <value> with
// given <collator>. If the input value is null, NullBytes() is returned.
absl::StatusOr<Value> GetValueSortKey(const Value& value,
                                      const GoogleSqlCollator& collator) {
  GOOGLESQL_RET_CHECK(value.type()->IsString())
      << "Cannot get sort key for value in non-String type: "
      << value.type()->DebugString();

  if (value.is_null()) {
    return values::NullBytes();
  }
  absl::Cord sort_key;
  GOOGLESQL_RETURN_IF_ERROR(collator.GetSortKeyUtf8(value.string_value(), &sort_key));
  return values::Bytes(sort_key);
}

// `AccumulateTuple` performs core aggregation logic for a single input tuple.
// TODO: Refactor this logic into a class (maybe make `group_map`
// a separate class and refactor logic into it).
absl::Status AccumulateTuple(
    const TupleData* /*absl_nonnull*/ tuple,
    absl::Span<const TupleData* const> params,
    absl::Span<const int64_t> grouping_sets, const CollatorList& collators,
    int key_size, int grouping_key_size, absl::Span<const KeyArg* const> keys,
    absl::Span<const AggregateArg* const> aggregators,
    EvaluationContext* context,
    UnorderedArrayCollisionTracker& unordered_array_collision_tracker,
    absl::flat_hash_map<TupleDataPtr, std::unique_ptr<GroupValue>>& group_map,
    std::vector<std::unique_ptr<TupleData>>& group_map_keys_memory,
    bool& stop_reading_input) {
  stop_reading_input = false;
  for (int offset = 0; offset < grouping_sets.size(); ++offset) {
    int64_t grouping_set = grouping_sets[offset];
    // This means the grouping set id is from a grouping sets/rollup/cube
    // query, rather than a regular query.
    bool is_grouping_set = grouping_set != NoGroupingSetId();
    // Determine the key to 'group_to_accumulator_map'.
    const std::vector<const TupleData*> params_and_input_tuple =
        ConcatSpans(params, {tuple});
    // When it's a grouping set query,  We also need to group by an additional
    // grouping set offset to allow duplicated grouping sets in the query. In
    // this case, it's guaranteed the last key is always the offset.
    auto key_data = std::make_unique<TupleData>(grouping_key_size);
    // If collator is present for <key_data[i]>, <collated_key_data[i]> is
    // collation_key for value of <key_data[i]>. Otherwise,
    // <collated_key_data[i]> is the same as <key_data[i]>.
    auto collated_key_data = std::make_unique<TupleData>(grouping_key_size);
    // If GROUPING function call is present in the AggregateOp, it needs a
    // special input data with only 0 or 1 when conducting aggregation, rather
    // than the original input rows. grouping_value_data[i] is 0 if the key at
    // index is in the current grouping set, otherwise its value is 1. The
    // grouping accumulator will calculate the output of the GROUPING function
    // with this input data and its argument key index. Basically the
    // accumulator just returns grouping_value_data.slot(key_index).
    auto grouping_value_data = std::make_unique<TupleData>(grouping_key_size);
    for (int i = 0; i < key_size; ++i) {
      TupleSlot* slot = key_data->mutable_slot(i);
      const KeyArg* key = keys[i];

      absl::Status status;
      if (!key->value_expr()->EvalSimple(params_and_input_tuple, context, slot,
                                         &status)) {
        return status;
      }
      // If the group by key is not in the current grouping set, then the
      // value contributed to aggregation will be NULL, and the grouping call
      // output is 1. The logic here assumes the key orders won't be changed.
      if (is_grouping_set && (grouping_set & (1ull << i)) == 0) {
        // The current grouping set doesn't contains keys[i].
        slot->SetValue(Value::Null(key->type()));
        grouping_value_data->mutable_slot(i)->SetValue(Value::Int64(1));
      } else {
        // The current grouping set contains keys[i] or it's a regular group
        // by query.
        grouping_value_data->mutable_slot(i)->SetValue(Value::Int64(0));
      }

      if (  // Once we know the query is known to be non-deterministic, we
            // short-circuit to avoid any overhead from non-determinism
            // detection.
          context->IsDeterministicOutput() &&
          unordered_array_collision_tracker.CouldIndicateNondetermisticGrouping(
              i, slot->value()) &&
          // On the first row we know it is not real non-determinism yet.
          !group_map.empty()) {
        context->SetNonDeterministicOutput();
      }

      Value* collated_slot_value =
          collated_key_data->mutable_slot(i)->mutable_value();
      if (collators[i] == nullptr) {
        *collated_slot_value = slot->value();
      } else {
        GOOGLESQL_ASSIGN_OR_RETURN(*collated_slot_value,
                         GetValueSortKey(slot->value(), *(collators[i])));
      }
    }

    if (is_grouping_set) {
      GOOGLESQL_RET_CHECK_EQ(grouping_key_size, key_size + 1);
      // Add the offset to the group by key list.
      key_data->mutable_slot(key_size)->SetValue(Value::Int32(offset));
      collated_key_data->mutable_slot(key_size)->SetValue(Value::Int32(offset));
    }

    // Look up the value in 'group_to_accumulator_map', initializing a new
    // one if necessary.
    AccumulatorList* accumulators = nullptr;
    std::vector<absl::Status>* accumulator_errors = nullptr;
    std::unique_ptr<GroupValue>* found_group_value =
        googlesql_base::FindOrNull(group_map, TupleDataPtr(collated_key_data.get()));
    if (found_group_value == nullptr) {
      // Create the new GroupValue.
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<GroupValue> inserted_group_value,
                       GroupValue::Create(std::move(key_data),
                                          context->memory_accountant()));

      // Initialize the accumulators.
      accumulator_errors = &inserted_group_value->mutable_accumulator_errors();
      *accumulator_errors = std::vector<absl::Status>(aggregators.size());

      accumulators = inserted_group_value->mutable_accumulator_list();
      accumulators->reserve(aggregators.size());
      for (const AggregateArg* aggregator : aggregators) {
        AggregateArgAccumulatorParam accumulator_param;
        GOOGLESQL_ASSIGN_OR_RETURN(accumulator_param.accumulator,
                         aggregator->CreateAccumulator(params, context));
        accumulator_param.is_grouping_function =
            IsGroupingFunction(aggregator->aggregate_function());
        accumulator_param.stop_bit = false;
        accumulators->push_back(std::move(accumulator_param));
      }

      // Insert the new GroupValue.
      GOOGLESQL_RET_CHECK(group_map
                    .emplace(TupleDataPtr(collated_key_data.get()),
                             std::move(inserted_group_value))
                    .second);
      group_map_keys_memory.push_back(std::move(collated_key_data));
    } else {
      accumulators = (*found_group_value)->mutable_accumulator_list();
      accumulator_errors = &(*found_group_value)->mutable_accumulator_errors();
      key_data.reset();
      collated_key_data.reset();
      grouping_value_data.reset();
    }

    // Accumulate.
    GOOGLESQL_RET_CHECK_EQ(accumulators->size(), aggregators.size());
    bool all_accumulators_stopped = true;
    for (int i = 0; i < accumulators->size(); ++i) {
      auto& accumulator_param = (*accumulators)[i];
      bool& stop_bit = accumulator_param.stop_bit;
      if (stop_bit) continue;
      // When GROUPING function call is present in the current AggregateOp, we
      // need to replace the input rows to the special grouping_value_data
      // which contains either 0 or 1 for each key index.
      const TupleData* actual_next_input = tuple;
      if (accumulator_param.is_grouping_function) {
        actual_next_input = grouping_value_data.get();
      }
      if (!accumulator_param.accumulator->Accumulate(
              *actual_next_input, &stop_bit, &(*accumulator_errors)[i])) {
        if (!aggregators[i]->side_effects_variable().is_valid()) {
          // Error doesn't need to be deferred to an operator like IF() later,
          // so we can return it directly.
          // The variable is set only when this aggregation was deferred.
          return (*accumulator_errors)[i];
        }
        stop_bit = true;
      }
      if (!stop_bit) all_accumulators_stopped = false;
    }

    if (all_accumulators_stopped && keys.empty()) {
      // We are doing full aggregation and all the accumulators have stopped,
      // we can stop reading the input.
      stop_reading_input = true;
      break;
    }
  }
  return absl::OkStatus();
}

// `GatherTuples` gets aggregated tuples from `group_map` and returns them as a
// `TupleDataDeque`.
// TODO: Refactor this logic into a class (maybe make `group_map`
// a separate class and refactor logic into it).
absl::StatusOr<std::unique_ptr<TupleDataDeque>> GatherTuples(
    int num_extra_slots, int key_size, int grouping_key_size,
    bool has_grouping_sets, absl::Span<const TupleData* const> params,
    absl::Span<const KeyArg* const> keys,
    absl::Span<const AggregateArg* const> aggregators,
    absl::flat_hash_map<TupleDataPtr, std::unique_ptr<GroupValue>>& group_map,
    std::vector<std::unique_ptr<TupleData>>& group_map_keys_memory,
    EvaluationContext* context) {
  auto tuples = std::make_unique<TupleDataDeque>(context->memory_accountant());
  for (auto& entry : group_map) {
    // Destruction of the 'group_value' will clear all memory used by its
    // members.
    std::unique_ptr<GroupValue> group_value = std::move(entry.second);
    AccumulatorList& accumulators = *group_value->mutable_accumulator_list();

    std::unique_ptr<TupleData> tuple = group_value->ConsumeKey();
    size_t num_slots_to_add = num_extra_slots + aggregators.size();
    for (const auto& aggregator : aggregators) {
      if (aggregator->side_effects_variable().is_valid()) {
        num_slots_to_add++;
      }
    }
    tuple->AddSlots(static_cast<int>(num_slots_to_add));

    int seen_side_effects_vars = 0;
    for (int i = 0; i < accumulators.size(); ++i) {
      AggregateArgAccumulator& accumulator = *accumulators[i].accumulator;
      absl::StatusOr<Value> value =
          !group_value->accumulator_error(i).ok()
              ? group_value->accumulator_error(i)
              : accumulator.GetFinalResult(
                    /*inputs_in_defined_order=*/false);
      bool has_side_effects_variable =
          aggregators[i]->side_effects_variable().is_valid();
      if (value.ok()) {
        tuple->mutable_slot(grouping_key_size + i)->SetValue(value.value());
        if (has_side_effects_variable) {
          int slot_index = grouping_key_size +
                           static_cast<int>(accumulators.size()) +
                           seen_side_effects_vars++;
          // No side effects, so set the side effect value to NULL.
          tuple->mutable_slot(slot_index)->SetValue(Value::NullBytes());
        }
      } else if (!has_side_effects_variable ||
                 !ShouldSuppressError(
                     value.status(),
                     ResolvedFunctionCallBase::SAFE_ERROR_MODE)) {
        // Error not handled by any scope, we can return it directly
        return value.status();
      } else {
        // Delay failures until the target operator sees it and decides what to
        // do with it
        tuple->mutable_slot(grouping_key_size + i)->SetValue(Value::Invalid());

        // Represent the status as a BYTES value. The standard SIDE_EFFECT type
        // does not have literals or values defined.
        ::google::rpc::Status status_proto;
        internal::SaveStatusToProto(value.status(), &status_proto);
        int slot_index = grouping_key_size +
                         static_cast<int>(accumulators.size()) +
                         seen_side_effects_vars++;
        tuple->mutable_slot(slot_index)
            ->SetValue(Value::Bytes(status_proto.SerializeAsCord()));
      }
    }
    // This can free up considerable memory. E.g., for STRING_AGG.
    accumulators.clear();

    absl::Status status;
    if (!tuples->PushBack(std::move(tuple), &status)) {
      return status;
    }
  }

  // Clears <group_map_keys_memory> and <group_map> to reclaim the memory since
  // they are not used anymore.
  group_map_keys_memory.clear();
  group_map.clear();

  if (tuples->IsEmpty()) {
    if (keys.empty()) {
      // We are doing full aggregation over empty input, so we must compute
      // trivial values for the aggregators.
      auto tuple =
          std::make_unique<TupleData>(aggregators.size() + num_extra_slots);
      for (int i = 0; i < aggregators.size(); ++i) {
        const AggregateArg* aggregator = aggregators[i];
        GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<AggregateArgAccumulator> accumulator,
                         aggregator->CreateAccumulator(params, context));
        GOOGLESQL_ASSIGN_OR_RETURN(Value value, accumulator->GetFinalResult(
                                          /*inputs_in_defined_order=*/true));
        tuple->mutable_slot(i)->SetValue(value);
      }
      absl::Status status;
      if (!tuples->PushBack(std::move(tuple), &status)) {
        return status;
      }
    }
  } else {
    for (const KeyArg* key : keys) {
      if (key->type()->IsFloatingPoint()) {
        context->SetNonDeterministicOutput();
      }
    }
  }

  // Sort the tuples by key as described above.
  //
  // TODO: Consider eliminating this sort. The downside is that
  // AggregationTupleIterator will then give a non-deterministic ordering of
  // groups, which can break the reference implementation compliance tests
  // (which are based on purely textual matching). It can also break some user
  // tests.
  std::vector<int> slots_for_keys;
  slots_for_keys.reserve(key_size);
  for (int i = 0; i < key_size; ++i) {
    slots_for_keys.push_back(i);
  }
  std::vector<int> extra_slots_for_keys;
  // If the AggregateOp contains grouping sets, then also sort the tuple with
  // the extra grouping set offset value. The slot index is key_size.
  if (has_grouping_sets) {
    extra_slots_for_keys.push_back(key_size);
  }
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<TupleComparator> tuple_comparator,
      TupleComparator::Create(keys, slots_for_keys, extra_slots_for_keys,
                              params, context));
  tuples->Sort(*tuple_comparator, context->options().always_use_stable_sort);

  // We need to remove the extra grouping key "offset" from the tuples. It's
  // only used internally for grouping and sorting, won't expose to other
  // operators.
  if (has_grouping_sets) {
    int64_t tuple_size = tuples->GetSize();
    for (int64_t i = 0; i < tuple_size; ++i) {
      std::unique_ptr<TupleData> tuple = tuples->PopFront();
      // The extra grouping key is at index key_size.
      tuple->RemoveSlotAt(key_size);
      absl::Status status;
      if (!tuples->PushBack(std::move(tuple), &status)) {
        return status;
      }
    }
  }
  return tuples;
}

}  // namespace

// -------------------------------------------------------
// AggregateArg
// -------------------------------------------------------

// TODO: b/430036320 - Remove group_rows_subquery arg if no longer needed with
//                     GROUP ROWS simplified.
absl::StatusOr<std::unique_ptr<AggregateArg>> AggregateArg::Create(
    const VariableId& variable,
    std::unique_ptr<const AggregateFunctionBody> function,
    std::vector<std::unique_ptr<ValueExpr>> arguments, Distinctness distinct,
    std::unique_ptr<ValueExpr> having_expr,
    const HavingModifierKind having_modifier_kind,
    std::vector<std::unique_ptr<KeyArg>> order_by_keys,
    std::unique_ptr<ValueExpr> limit,
    std::unique_ptr<RelationalOp> group_rows_subquery,
    std::vector<std::unique_ptr<KeyArg>> inner_grouping_keys,
    std::vector<std::unique_ptr<AggregateArg>> inner_aggregators,
    ResolvedFunctionCallBase::ErrorMode error_mode,
    std::unique_ptr<ValueExpr> filter, std::unique_ptr<ValueExpr> having_filter,
    const std::vector<ResolvedCollation>& collation_list,
    const VariableId& side_effects_variable) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<AggregateFunctionCallExpr> aggregate_expr,
                   AggregateFunctionCallExpr::Create(std::move(function),
                                                     std::move(arguments)));
  return absl::WrapUnique(new AggregateArg(
      variable, std::move(aggregate_expr), distinct, std::move(having_expr),
      having_modifier_kind, std::move(order_by_keys), std::move(limit),
      std::move(group_rows_subquery), std::move(inner_grouping_keys),
      std::move(inner_aggregators), error_mode, std::move(filter),
      std::move(having_filter), collation_list, side_effects_variable));
}

absl::Status AggregateArg::SetSchemasForEvaluation(
    const TupleSchema& group_schema,
    absl::Span<const TupleSchema* const> params_schemas) {
  std::vector<const TupleSchema*> params_and_group_schemas =
      ConcatSpans(params_schemas, {&group_schema});
  std::unique_ptr<TupleSchema> group_rows_schema;
  if (group_rows_subquery_ != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(
        group_rows_subquery_->SetSchemasForEvaluation(params_schemas));
    group_rows_schema = group_rows_subquery_->CreateOutputSchema();
    params_and_group_schemas =
        ConcatSpans(params_schemas, {group_rows_schema.get()});
  }

  // Filter is evaluated before multi-level aggregation, so it uses the
  // unmodified `params_and_group_schemas`.
  if (filter() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(
        mutable_filter()->SetSchemasForEvaluation(params_and_group_schemas));
  }

  // Create a schema representing the inner grouping keys and inner aggregators,
  // and set the schemas for evaluating inner grouping keys and aggregates.
  std::unique_ptr<TupleSchema> inner_aggregation_output_schema;
  if (!inner_grouping_keys_.empty()) {
    std::vector<VariableId> vars;
    vars.reserve(inner_grouping_keys_.size() + 2 * inner_aggregators_.size());
    for (const auto& key : inner_grouping_keys_) {
      GOOGLESQL_RETURN_IF_ERROR(key->mutable_value_expr()->SetSchemasForEvaluation(
          params_and_group_schemas));
      vars.push_back(key->variable());
    }
    for (const auto& aggregator : inner_aggregators_) {
      GOOGLESQL_RETURN_IF_ERROR(
          aggregator->SetSchemasForEvaluation(group_schema, params_schemas));
      vars.push_back(aggregator->variable());
    }
    for (const auto& aggregator : inner_aggregators_) {
      if (aggregator->side_effects_variable().is_valid()) {
        vars.push_back(aggregator->side_effects_variable());
      }
    }
    inner_aggregation_output_schema = std::make_unique<TupleSchema>(vars);
  }

  if (inner_aggregation_output_schema) {
    params_and_group_schemas =
        ConcatSpans(params_schemas, {inner_aggregation_output_schema.get()});
  }

  if (having_modifier_kind() != AggregateArg::kHavingNone &&
      having_expr() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(mutable_having_expr()->SetSchemasForEvaluation(
        params_and_group_schemas));
  }

  if (having_filter_ != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(
        having_filter_->SetSchemasForEvaluation(params_and_group_schemas));
  }

  for (int i = 0; i < input_field_list_size(); ++i) {
    GOOGLESQL_RETURN_IF_ERROR(mutable_input_field(i)->SetSchemasForEvaluation(
        params_and_group_schemas));
  }

  if (limit() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(mutable_limit()->SetSchemasForEvaluation(params_schemas));
  }

  for (int i = 0; i < parameter_list_size(); ++i) {
    GOOGLESQL_RETURN_IF_ERROR(
        mutable_parameter(i)->SetSchemasForEvaluation(params_schemas));
  }
  if (inner_aggregation_output_schema) {
    group_schema_ = std::make_unique<const TupleSchema>(
        inner_aggregation_output_schema->variables());
  } else {
    group_schema_ =
        std::make_unique<const TupleSchema>(group_schema.variables());
  }

  if (!order_by_keys().empty()) {
    // If any of the order by keys are in the params then they won't affect the
    // sort order and should be ignored since params are constant in the group.
    std::vector<std::unique_ptr<KeyArg>> keep;
    for (std::unique_ptr<KeyArg>& key : order_by_keys_) {
      GOOGLESQL_RET_CHECK(key->has_value_expr());
      GOOGLESQL_RET_CHECK(key->has_variable());
      bool in_group_schema = true;
      for (const TupleSchema* param_schema : params_schemas) {
        if (param_schema->FindIndexForVariable(key->variable()).has_value()) {
          in_group_schema = false;
          break;
        }
      }
      if (in_group_schema) {
        keep.push_back(std::move(key));
      }
    }
    order_by_keys_ = std::move(keep);
  }
  return absl::OkStatus();
}

namespace {

// Variant of the aggregator accumulator interfaces that is able to look at both
// an input row and its corresponding Value. The Value corresponding to an input
// row contains everything that the aggregate function aggregates. If that
// involves more than one thing, it is a struct with the appropriate number of
// fields. (The AggregateFunctionBody knows what type to expect because it knows
// the number of fields involved in the aggregation.) The usage is essentially
// the same as AggregateAccumulator,
class IntermediateAggregateAccumulator {
 public:
  virtual ~IntermediateAggregateAccumulator() = default;

  virtual absl::Status Reset() = 0;

  virtual bool Accumulate(const TupleData& input_row, const Value& input_value,
                          bool* stop_accumulation, absl::Status* status) = 0;

  virtual absl::StatusOr<Value> GetFinalResult(
      bool inputs_in_defined_order) = 0;
};

// Adapts AggregateAccumulator to IntermediateAggregateAccumulator.
class AggregateAccumulatorAdaptor : public IntermediateAggregateAccumulator {
 public:
  AggregateAccumulatorAdaptor(const Type* output_type,
                              ResolvedFunctionCallBase::ErrorMode error_mode,
                              std::unique_ptr<AggregateAccumulator> accumulator)
      : output_type_(output_type),
        error_mode_(error_mode),
        accumulator_(std::move(accumulator)) {}

  AggregateAccumulatorAdaptor(const AggregateAccumulatorAdaptor&) = delete;
  AggregateAccumulatorAdaptor& operator=(const AggregateAccumulatorAdaptor&) =
      delete;

  absl::Status Reset() override {
    // We call Reset() when we find a new extreme value. If there was previously
    // a safe-convertible error while accumulating for a less extreme value, we
    // don't need that anymore. Set `safe_result_` to invalid to ensure it does
    // not interfere with GetFinalResult.
    safe_result_ = Value();
    return accumulator_->Reset();
  }

  bool Accumulate(const TupleData& input_row, const Value& input_value,
                  bool* stop_accumulation, absl::Status* status) override {
    absl::Status error;
    if (!accumulator_->Accumulate(input_value, stop_accumulation, &error)) {
      if (ShouldSuppressError(error, error_mode_)) {
        safe_result_ = Value::Null(output_type_);
        *stop_accumulation = true;
        return true;
      }
      *status = error;
      return false;
    }
    return true;
  }

  absl::StatusOr<Value> GetFinalResult(bool inputs_in_defined_order) override {
    if (safe_result_.is_valid()) return safe_result_;

    const absl::StatusOr<Value> status_or_value =
        accumulator_->GetFinalResult(inputs_in_defined_order);
    if (!status_or_value.ok()) {
      const absl::Status& error = status_or_value.status();
      if (ShouldSuppressError(error, error_mode_)) {
        return Value::Null(output_type_);
      }
      return error;
    }
    return status_or_value.value();
  }

 private:
  const Type* output_type_;
  const ResolvedFunctionCallBase::ErrorMode error_mode_;
  std::unique_ptr<AggregateAccumulator> accumulator_;
  Value safe_result_;
};

// Accumulator that enforces LIMITs on the list of input rows to aggregate.
class LimitAccumulator : public IntermediateAggregateAccumulator {
 public:
  LimitAccumulator(
      int64_t limit,
      std::unique_ptr<IntermediateAggregateAccumulator> accumulator,
      EvaluationContext* context)
      : limit_(limit),
        accumulator_(std::move(accumulator)),
        context_(context) {}

  absl::Status Reset() override {
    num_rows_accumulated_ = 0;
    return accumulator_->Reset();
  }

  LimitAccumulator(const LimitAccumulator&) = delete;
  LimitAccumulator& operator=(const LimitAccumulator&) = delete;

  bool Accumulate(const TupleData& input_row, const Value& value,
                  bool* stop_accumulation, absl::Status* status) override {
    // Handles the LIMIT 0 case.
    if (num_rows_accumulated_ >= limit_) {
      *stop_accumulation = true;
      return true;
    }

    if (!accumulator_->Accumulate(input_row, value, stop_accumulation,
                                  status)) {
      return false;
    }
    ++num_rows_accumulated_;

    if (num_rows_accumulated_ >= limit_) {
      nonzero_limit_reached_ = true;
      *stop_accumulation = true;
      return true;
    }
    return true;
  }

  absl::StatusOr<Value> GetFinalResult(bool inputs_in_defined_order) override {
    // Note that the current code is overly aggressive in labeling
    // non-deterministic output. For instance, in the case when the limit
    // exactly equals the number of input rows to this accumulator. This is
    // currently challenging because of the present accumulator API
    // which provides no "peek" utility for input rows.
    // TODO: Address the edge case where limit is exactly equal to
    // the number of input rows.
    // TODO: Address the edge case where all input rows are
    // identical and the result is deterministic anyway.
    if (nonzero_limit_reached_ && !inputs_in_defined_order) {
      context_->SetNonDeterministicOutput();
    }
    return accumulator_->GetFinalResult(inputs_in_defined_order);
  }

 private:
  const int64_t limit_;
  bool nonzero_limit_reached_ = false;
  std::unique_ptr<IntermediateAggregateAccumulator> accumulator_;
  EvaluationContext* context_;
  int64_t num_rows_accumulated_ = 0;
};

static std::vector<const KeyArg*> UniquePtrSpanToPtrVector(
    absl::Span<const std::unique_ptr<KeyArg>> keys) {
  std::vector<const KeyArg*> ptrs;
  ptrs.reserve(keys.size());
  for (int i = 0; i < keys.size(); ++i) {
    ptrs.push_back(keys[i].get());
  }
  return ptrs;
}

// Accumulator that orders the list of input rows to aggregate.
class OrderByAccumulator : public IntermediateAggregateAccumulator {
 public:
  OrderByAccumulator(
      absl::Span<const std::unique_ptr<KeyArg>> keys,
      absl::Span<const int> slots_for_keys,
      absl::Span<const int> slots_for_values,
      absl::Span<const TupleData* const> params,
      std::unique_ptr<IntermediateAggregateAccumulator> accumulator,
      EvaluationContext* context)
      : keys_(keys),
        slots_for_keys_(slots_for_keys.begin(), slots_for_keys.end()),
        slots_for_values_(slots_for_values.begin(), slots_for_values.end()),
        params_(params.begin(), params.end()),
        accumulator_(std::move(accumulator)),
        context_(context),
        inputs_(context_->memory_accountant()) {}

  OrderByAccumulator(const OrderByAccumulator&) = delete;
  OrderByAccumulator& operator=(const OrderByAccumulator&) = delete;

  absl::Status Reset() override {
    inputs_.Clear();
    return absl::OkStatus();
  }

  bool Accumulate(const TupleData& input_row, const Value& value,
                  bool* stop_accumulation, absl::Status* status) override {
    *stop_accumulation = false;
    auto input = std::make_unique<TupleData>(input_row);
    input->AddSlots(1);
    input->mutable_slot(input->num_slots() - 1)->SetValue(value);
    return inputs_.PushBack(std::move(input), status);
  }

  absl::StatusOr<Value> GetFinalResult(
      bool /* inputs_in_defined_order */) override {
    GOOGLESQL_ASSIGN_OR_RETURN(
        auto tuple_comparator,
        TupleComparator::Create(UniquePtrSpanToPtrVector(keys_),
                                slots_for_keys_, params_, context_));
    inputs_.Sort(*tuple_comparator, context_->options().always_use_stable_sort);

    const bool inputs_in_defined_order = tuple_comparator->IsUniquelyOrdered(
        inputs_.GetTuplePtrs(), slots_for_values_);

    GOOGLESQL_RETURN_IF_ERROR(accumulator_->Reset());

    bool stop_accumulation;
    absl::Status status;
    while (!inputs_.IsEmpty()) {
      std::unique_ptr<TupleData> input_row = inputs_.PopFront();
      GOOGLESQL_RET_CHECK(!input_row->slots().empty());
      const Value value = input_row->slots().back().value();
      input_row->RemoveSlots(1);

      if (!accumulator_->Accumulate(*input_row, value, &stop_accumulation,
                                    &status)) {
        return status;
      }
      if (stop_accumulation) break;
    }
    return accumulator_->GetFinalResult(inputs_in_defined_order);
  }

 private:
  const absl::Span<const std::unique_ptr<KeyArg>> keys_;
  const std::vector<int> slots_for_keys_;
  const std::vector<int> slots_for_values_;
  const std::vector<const TupleData*> params_;
  std::unique_ptr<IntermediateAggregateAccumulator> accumulator_;
  EvaluationContext* context_;
  // The last slot of each TupleData here is the Value passed to the
  // corresponding call to Accumulate().
  TupleDataDeque inputs_;
};

// Accumulator that keeps the top N values and accumulates them in
// order. Functionally equivalent to LimitAccumulator(OrderByAccumulator) but
// uses less memory.
class TopNAccumulator : public IntermediateAggregateAccumulator {
 public:
  TopNAccumulator(const int64_t n,
                  std::unique_ptr<TupleComparator> tuple_comparator,
                  std::unique_ptr<IntermediateAggregateAccumulator> accumulator,
                  EvaluationContext* context)
      : n_(n),
        tuple_comparator_(std::move(tuple_comparator)),
        top_n_(*tuple_comparator_, context->memory_accountant()),
        accumulator_(std::move(accumulator)) {}

  absl::Status Reset() override {
    top_n_.Clear();
    return absl::OkStatus();
  }

  TopNAccumulator(const TopNAccumulator&) = delete;
  TopNAccumulator& operator=(const TopNAccumulator&) = delete;

  bool Accumulate(const TupleData& input_row, const Value& value,
                  bool* stop_accumulation, absl::Status* status) override {
    *stop_accumulation = false;

    auto input = std::make_unique<TupleData>(input_row);
    input->AddSlots(1);
    input->mutable_slot(input->num_slots() - 1)->SetValue(value);

    if (!top_n_.Insert(std::move(input), status)) return false;

    // 'if' should be good enough; 'while' is for paranoia.
    while (top_n_.GetSize() > n_) {
      top_n_.PopBack();
    }

    return true;
  }

  absl::StatusOr<Value> GetFinalResult(
      bool /* inputs_in_defined_order */) override {
    bool stop_accumulation;
    absl::Status status;
    while (!top_n_.IsEmpty()) {
      std::unique_ptr<TupleData> input_row = top_n_.PopFront();
      GOOGLESQL_RET_CHECK(!input_row->slots().empty());
      const Value value = input_row->slots().back().value();
      input_row->RemoveSlots(1);

      if (!accumulator_->Accumulate(*input_row, value, &stop_accumulation,
                                    &status)) {
        return status;
      }
      if (stop_accumulation) break;
    }

    // The value of 'inputs_in_defined_order' does not matter because this class
    // is never used for compliance or random query testing, as it is an
    // optimization. If that changes, passing true here will cause us to be too
    // aggressive in declaring test failures (which is better than silently
    // dropping results).
    return accumulator_->GetFinalResult(/*inputs_in_defined_order=*/true);
  }

 private:
  const int64_t n_;
  const std::unique_ptr<TupleComparator> tuple_comparator_;
  // The last slot of each TupleData in this queue is the Value passed to the
  // corresponding call to Accumulate().
  TupleDataOrderedQueue top_n_;
  std::unique_ptr<IntermediateAggregateAccumulator> accumulator_;
};

// Accumulator that only passes through distinct values.
class DistinctAccumulator : public IntermediateAggregateAccumulator {
 public:
  DistinctAccumulator(
      std::unique_ptr<IntermediateAggregateAccumulator> accumulator,
      EvaluationContext* context,
      std::unique_ptr<const GoogleSqlCollator> collator)
      : distinct_values_(context->memory_accountant()),
        accumulator_(std::move(accumulator)),
        collator_(std::move(collator)) {}

  absl::Status Reset() override {
    distinct_values_.Clear();
    return accumulator_->Reset();
  }

  DistinctAccumulator(const DistinctAccumulator&) = delete;
  DistinctAccumulator& operator=(const DistinctAccumulator&) = delete;

  bool Accumulate(const TupleData& input_row, const Value& value,
                  bool* stop_accumulation, absl::Status* status) override {
    *stop_accumulation = false;

    bool distinct;

    Value value_to_insert;
    if (collator_ == nullptr) {
      value_to_insert = value;
    } else {
      absl::StatusOr<Value> collated_distinct_key =
          GetValueSortKey(value, *(collator_));
      if (!collated_distinct_key.ok()) {
        *status = collated_distinct_key.status();
        return false;
      }
      value_to_insert = collated_distinct_key.value();
    }

    if (!distinct_values_.Insert(value_to_insert, &distinct, status)) {
      return false;
    }

    if (distinct) {
      if (!accumulator_->Accumulate(input_row, value, stop_accumulation,
                                    status)) {
        return false;
      }
    }
    return true;
  }

  absl::StatusOr<Value> GetFinalResult(bool inputs_in_defined_order) override {
    return accumulator_->GetFinalResult(inputs_in_defined_order);
  }

 private:
  ValueHashSet distinct_values_;
  std::unique_ptr<IntermediateAggregateAccumulator> accumulator_;
  const std::unique_ptr<const GoogleSqlCollator> collator_;
};

// Accumulator that discards NULL values.
class IgnoresNullAccumulator : public IntermediateAggregateAccumulator {
 public:
  // If 'use_compound_values' is true, then the values passed to Accumulate()
  // are non-NULL structs and should be discarded if any of their fields are
  // NULL.
  IgnoresNullAccumulator(
      bool use_compound_values,
      std::unique_ptr<IntermediateAggregateAccumulator> accumulator)
      : use_compound_values_(use_compound_values),
        accumulator_(std::move(accumulator)) {}

  IgnoresNullAccumulator(const IgnoresNullAccumulator&) = delete;
  IgnoresNullAccumulator& operator=(const IgnoresNullAccumulator&) = delete;

  absl::Status Reset() override { return accumulator_->Reset(); }

  bool Accumulate(const TupleData& input_row, const Value& value,
                  bool* stop_accumulation, absl::Status* status) override {
    *stop_accumulation = false;

    bool ignore = false;
    if (use_compound_values_) {
      for (const Value& field_value : value.fields()) {
        if (field_value.is_null()) {
          ignore = true;
          break;
        }
      }
    } else {
      ignore = value.is_null();
    }

    if (ignore) return true;
    return accumulator_->Accumulate(input_row, value, stop_accumulation,
                                    status);
  }

  absl::StatusOr<Value> GetFinalResult(bool inputs_in_defined_order) override {
    return accumulator_->GetFinalResult(inputs_in_defined_order);
  }

 private:
  const bool use_compound_values_;
  std::unique_ptr<IntermediateAggregateAccumulator> accumulator_;
};

// Accumulator that filters out rows that don't have the extremal value of some
// expression. It does this in one pass over the input by keeping track of the
// current extremal value and the aggregation for that value, and resetting the
// aggregation whenever it finds a new extremal value.
//
// As an example, consider COUNT(* HAVING MAX foo) for these input rows:
//   foo: 10 -> saw new max, the count is now 1, and max is 10
//   foo: 10 -> saw current max, the count is now 2 and max is still 10
//   foo: 5  -> less than the current max, the count is still 2, and max is 10
//   foo: 20 -> saw new max, reset count to 1 and max to 20
//   foo: 20 -> saw current max, the count is now 2 and max is still 20
class HavingExtremalValueAccumulator : public IntermediateAggregateAccumulator {
 public:
  HavingExtremalValueAccumulator(
      absl::Span<const TupleData* const> params, const ValueExpr* having_expr,
      bool use_max,
      std::unique_ptr<IntermediateAggregateAccumulator> accumulator,
      EvaluationContext* context)
      : params_(params.begin(), params.end()),
        having_expr_(having_expr),
        use_max_(use_max),
        accumulator_(std::move(accumulator)),
        context_(context) {}

  HavingExtremalValueAccumulator(const HavingExtremalValueAccumulator&) =
      delete;
  HavingExtremalValueAccumulator& operator=(
      const HavingExtremalValueAccumulator&) = delete;

  absl::Status Reset() override {
    extremal_having_value_ = Value::Invalid();
    status_ = absl::OkStatus();
    return accumulator_->Reset();
  }

  bool Accumulate(const TupleData& input_row, const Value& value,
                  bool* stop_accumulation, absl::Status* status) override {
    *stop_accumulation = false;

    TupleSlot slot;
    if (!having_expr_->EvalSimple(
            ConcatSpans(absl::Span<const TupleData* const>(params_),
                        {&input_row}),
            context_, &slot, status)) {
      return false;
    }
    const Value& having_value = slot.value();

    // Compute the new extremal having value. If we hit an error in that value
    // itself, or comparing with the current extremal value, we can't continue
    // accumulation. So we pass the given `status` and return the error right
    // away.
    Value new_extremal_having_value;
    if (!extremal_having_value_.is_valid()) {
      new_extremal_having_value = having_value;
    } else {
      // We use a BuiltinAggregateFunction here to stay consistent with all of
      // the SQL semantics regarding MAX/MIN in the presence of NULLs and NaNs.
      const BuiltinAggregateFunction max_function(
          use_max_ ? FunctionKind::kMax : FunctionKind::kMin,
          having_value.type(), /*num_input_fields=*/1, having_value.type());
      auto status_or_accumulator = max_function.CreateAccumulator(
          /*args=*/{}, /*params=*/{}, /*collator_list=*/{}, context_);
      if (!status_or_accumulator.ok()) {
        *status = status_or_accumulator.status();
        return false;
      }
      std::unique_ptr<AggregateAccumulator>& accumulator =
          status_or_accumulator.value();
      *status = accumulator->Reset();
      if (!status->ok()) return false;
      bool dummy_stop_accumulation;
      if (!accumulator->Accumulate(extremal_having_value_,
                                   &dummy_stop_accumulation, status)) {
        return false;
      }
      if (!accumulator->Accumulate(having_value, &dummy_stop_accumulation,
                                   status)) {
        return false;
      }
      auto status_or_value = accumulator->GetFinalResult(
          /*inputs_in_defined_order=*/false);
      if (!status_or_value.ok()) {
        *status = status_or_value.status();
        return false;
      }
      new_extremal_having_value = std::move(status_or_value).value();
    }

    // We update 'extremal_having_value_' and reset the accumulation if
    // 'new_extremal_having_value' is not the same as
    // 'extremal_having_value_'. We use ComparisonFunction because it handles
    // NaNs correctly.
    bool reset;
    if (!extremal_having_value_.is_valid()) {
      reset = true;
    } else {
      const ComparisonFunction equals_function(FunctionKind::kEqual,
                                               types::BoolType());
      Value equals_result;
      if (!equals_function.Eval(
              params_, {extremal_having_value_, new_extremal_having_value},
              context_, &equals_result, status)) {
        return false;
      }
      reset = !(equals_result == Bool(true));
    }
    if (reset) {
      extremal_having_value_ = new_extremal_having_value;
      *status = accumulator_->Reset();
      if (!status->ok()) {
        return false;
      }
      // Discard any error that may have accumulated from prior extremal values.
      // An engine that encountered this error may have failed the statement
      // eagerly when the error was encountered, so we signal non-determinism
      // in this case.
      if (!status_.ok()) {
        context_->SetNonDeterministicOutput();
      }
      status_ = absl::OkStatus();
    }

    // Accumulate if 'having_value' equals 'extremal_having_value_'.  NaN is not
    // equal to itself, so this will not accumulate for NaN even if it triggered
    // a reset. For this reason, it's important to use the ComparisonFunction
    // here and not Value::Equals().
    const ComparisonFunction equals_function(FunctionKind::kEqual,
                                             types::BoolType());
    Value equals_result;
    if (!equals_function.Eval(params_, {extremal_having_value_, having_value},
                              context_, &equals_result, status)) {
      return false;
    }
    const bool accumulate = (equals_result == Bool(true));
    if (!accumulate) {
      return true;
    }

    // We can never stop accumulating because we might see another MAX/MIN
    // later. If there's an error, store the status temporarily, because we
    // may reset the value later after all, if it turns out this is not the
    // extremal value.
    // Note that we pass `status_`, not `status` here. We store the status to
    // return it at GetFinalResult(), if indeed the current extremal value turns
    // out to be the final one. Until then, we want to let accumulation continue
    // anyway.

    bool dummy_stop_accumulation;
    accumulator_->Accumulate(input_row, value, &dummy_stop_accumulation,
                             &status_);

    return true;
  }

  absl::StatusOr<Value> GetFinalResult(bool inputs_in_defined_order) override {
    // Return the pending error if we hit any while accumulating the current
    // extremal value. We couldn't return it when we first saw it because we
    // could not know at the time that the extremal value won't change later.
    if (!status_.ok()) {
      return status_;
    }
    return accumulator_->GetFinalResult(inputs_in_defined_order);
  }

 private:
  const std::vector<const TupleData*> params_;
  const ValueExpr* having_expr_;
  const bool use_max_;
  Value extremal_having_value_;
  std::unique_ptr<IntermediateAggregateAccumulator> accumulator_;
  EvaluationContext* context_;
  absl::Status status_;
};

// Accumulator which runs an inner accumulator on only the subset of rows for
// which a filter expression evaluates to true.
class FilteredArgAccumulator : public IntermediateAggregateAccumulator {
 public:
  FilteredArgAccumulator(
      absl::Span<const TupleData* const> params,
      std::unique_ptr<IntermediateAggregateAccumulator> accumulator,
      const ValueExpr* filter, EvaluationContext* context)
      : params_(params.begin(), params.end()),
        accumulator_(std::move(accumulator)),
        filter_(filter),
        context_(context) {}

  absl::Status Reset() override { return accumulator_->Reset(); }

  bool Accumulate(const TupleData& input_row, const Value& input_value,
                  bool* stop_accumulation, absl::Status* status) override {
    *stop_accumulation = false;

    TupleSlot slot;
    if (!filter_->EvalSimple(
            ConcatSpans(absl::Span<const TupleData* const>(params_),
                        {&input_row}),
            context_, &slot, status)) {
      return false;
    }
    const Value& filter_value = slot.value();

    if (filter_value.is_null() || !filter_value.bool_value()) {
      // Row is skipped
      return true;
    }

    return accumulator_->Accumulate(input_row, input_value, stop_accumulation,
                                    status);
  }

  absl::StatusOr<Value> GetFinalResult(bool inputs_in_defined_order) override {
    return accumulator_->GetFinalResult(inputs_in_defined_order);
  }

 private:
  const std::vector<const TupleData*> params_;

  // Underlying accumulator that runs on input rows that satisfy the filter
  std::unique_ptr<IntermediateAggregateAccumulator> accumulator_;

  // Filter expression to be applied to each row. Must return BOOL type.
  // A value of TRUE indicates that the row should be processed by
  // <accumulator_>. A value of FALSE or NULL indicates that the row should be
  // skipped.
  const ValueExpr* filter_;

  // EvaluationContext for evaluating the filter.
  EvaluationContext* context_;
};

class WithGroupRowsAccumulator : public IntermediateAggregateAccumulator {
 public:
  WithGroupRowsAccumulator(
      absl::Span<const TupleData* const> params,
      const RelationalOp* group_rows_subquery,
      std::vector<const ValueExpr*> agg_fn_input_fields,
      const Type* agg_fn_input_type,
      std::unique_ptr<IntermediateAggregateAccumulator> accumulator,
      EvaluationContext* context)
      : params_(params),
        group_rows_subquery_(group_rows_subquery),
        agg_fn_input_fields_(agg_fn_input_fields),
        agg_fn_input_type_(agg_fn_input_type),
        inputs_(context->memory_accountant()),
        accumulator_(std::move(accumulator)),
        context_(context) {}

  absl::Status Reset() override {
    inputs_.Clear();
    return accumulator_->Reset();
  }

  WithGroupRowsAccumulator(const WithGroupRowsAccumulator&) = delete;
  WithGroupRowsAccumulator& operator=(const WithGroupRowsAccumulator&) = delete;

  bool Accumulate(const TupleData& input_row, const Value& value,
                  bool* stop_accumulation, absl::Status* status) override {
    *stop_accumulation = false;

    auto input = std::make_unique<TupleData>(input_row);
    return inputs_.PushBack(std::move(input), status);
  }

  absl::StatusOr<Value> GetFinalResult(bool inputs_in_defined_order) override {
    // Set inputs_ on context to make it available for GROUP_ROWS scan;
    // Fetch all rows from the subquery, running accumulator_ through them;
    // compute the argument expression values for the aggregate function
    // (similar to what IntermediateAggregateAccumulatorAdaptor does).

    context_->set_active_group_rows(&inputs_);
    auto cleanup = absl::MakeCleanup(
        [this]() { context_->set_active_group_rows(nullptr); });

    // TODO: don't we need to pass different params_ here? params_
    // seem to be for the aggregate function itself, not for the subquery, on
    // the other hand the same params are passed to all aggregates. Reference:
    // https://github.com/google/googlesql/blob/master/googlesql/reference_impl/aggregate_op.cc?l=1089&rcl=327634640
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<TupleIterator> input_iter,
                     group_rows_subquery_->CreateIterator(
                         params_, /*num_extra_slots=*/0, context_));
    absl::Status status;
    while (true) {
      const TupleData* next_input = input_iter->Next();
      if (next_input == nullptr) {
        GOOGLESQL_RETURN_IF_ERROR(input_iter->Status());
        break;
      }
      std::vector<Value> values(agg_fn_input_fields_.size());
      for (int i = 0; i < agg_fn_input_fields_.size(); ++i) {
        const ValueExpr* value_expr = agg_fn_input_fields_[i];

        std::shared_ptr<TupleSlot::SharedProtoState> shared_state;
        VirtualTupleSlot slot(&values[i], &shared_state);

        if (!value_expr->Eval(
                ConcatSpans(absl::Span<const TupleData* const>(params_),
                            {next_input}),
                context_, &slot, &status)) {
          return status;
        }
      }

      Value value;
      if (values.size() == 1) {
        value = std::move(values[0]);
      } else {
        value = Value::UnsafeStruct(agg_fn_input_type_->AsStruct(),
                                    std::move(values));
      }
      bool stop_accumulation;
      if (!accumulator_->Accumulate(*next_input, value, &stop_accumulation,
                                    &status)) {
        return status;
      }
      if (stop_accumulation) {
        break;
      }
    }

    return accumulator_->GetFinalResult(inputs_in_defined_order);
  }

 private:
  absl::Span<const TupleData* const> params_;
  const RelationalOp* group_rows_subquery_;
  std::vector<const ValueExpr*> agg_fn_input_fields_;
  const Type* agg_fn_input_type_;
  TupleDataDeque inputs_;
  std::unique_ptr<IntermediateAggregateAccumulator> accumulator_;
  EvaluationContext* context_;
};

// Adapts IntermediateAggregateAccumulator to AggregateArgAccumulator.
class IntermediateAggregateAccumulatorAdaptor : public AggregateArgAccumulator {
 public:
  IntermediateAggregateAccumulatorAdaptor(
      absl::Span<const TupleData* const> params,
      absl::Span<const ValueExpr* const> value_exprs, const Type* input_type,
      std::unique_ptr<IntermediateAggregateAccumulator> accumulator,
      EvaluationContext* context)
      : params_(params.begin(), params.end()),
        value_exprs_(value_exprs.begin(), value_exprs.end()),
        input_type_(input_type),
        accumulator_(std::move(accumulator)),
        context_(context) {}

  IntermediateAggregateAccumulatorAdaptor(
      const IntermediateAggregateAccumulatorAdaptor&) = delete;
  IntermediateAggregateAccumulatorAdaptor& operator=(
      const IntermediateAggregateAccumulatorAdaptor&) = delete;

  bool Accumulate(const TupleData& input_row, bool* stop_accumulation,
                  absl::Status* status) override {
    std::vector<Value> values(value_exprs_.size());
    for (int i = 0; i < value_exprs_.size(); ++i) {
      const ValueExpr* value_expr = value_exprs_[i];

      std::shared_ptr<TupleSlot::SharedProtoState> shared_state;
      VirtualTupleSlot slot(&values[i], &shared_state);

      if (!value_expr->Eval(
              ConcatSpans(absl::Span<const TupleData* const>(params_),
                          {&input_row}),
              context_, &slot, status)) {
        return false;
      }
    }

    Value value;
    if (values.size() == 1) {
      value = std::move(values[0]);
    } else {
      value = Value::UnsafeStruct(input_type_->AsStruct(), std::move(values));
    }

    return accumulator_->Accumulate(input_row, value, stop_accumulation,
                                    status);
  }

  absl::StatusOr<Value> GetFinalResult(bool inputs_in_defined_order) override {
    return accumulator_->GetFinalResult(inputs_in_defined_order);
  }

 private:
  const std::vector<const TupleData*> params_;
  const std::vector<const ValueExpr*> value_exprs_;
  const Type* input_type_;
  std::unique_ptr<IntermediateAggregateAccumulator> accumulator_;
  EvaluationContext* context_;
};

// An accumulator for GROUPING function.
// Regular aggregate functions only consume the evaluated arguments, which is an
// implementation of AggregateAccumulator. The argument of GROUPING function is
// just a const key index, it also needs to consume the input rows to calculate
// the final result 0 or 1, so it's an implementation of the interface
// IntermediateAggregateAccumulator. The input_row of
// GroupingAggregateArgAccumulator is a specially-built TupleData, where
// input_row[i] represents the result of grouping call at index i.
class GroupingAggregateArgAccumulator
    : public IntermediateAggregateAccumulator {
 public:
  GroupingAggregateArgAccumulator() = default;

  absl::Status Reset() override {
    grouping_value_ = -1;
    return absl::OkStatus();
  }

  bool Accumulate(const TupleData& input_row, const Value& value,
                  bool* stop_accumulation, absl::Status* status) override {
    // The argument of GROUPING function is the key index of the grouping key.
    ABSL_DCHECK_EQ(value.type(), types::Int64Type());
    int key_index = static_cast<int>(value.int64_value());
    ABSL_DCHECK_LT(key_index, input_row.num_slots());
    ABSL_DCHECK_GE(key_index, 0);

    grouping_value_ = input_row.slot(key_index).value().int64_value();
    // Sanity check. The output of grouping function can be either 0 or 1.
    ABSL_DCHECK(grouping_value_ == 0 || grouping_value_ == 1);
    // The output of GROUPING function has the same value within the same group,
    // so we can skip the following accumulations.
    *stop_accumulation = true;
    return true;
  }

  absl::StatusOr<Value> GetFinalResult(bool inputs_in_defined_order) override {
    // Sanity check.
    if (grouping_value_ == 0 || grouping_value_ == 1) {
      return Value::Int64(grouping_value_);
    }
    return absl::InternalError(
        absl::StrFormat("Unexpected grouping_value: %d", grouping_value_));
  }

 private:
  // set an initial invalid value for the grouping result.
  int64_t grouping_value_ = -1;
};

}  // namespace

static absl::Status PopulateSlotsForKeysAndValues(
    const TupleSchema& schema,
    absl::Span<const std::unique_ptr<KeyArg>> order_by_keys,
    std::vector<int>* slots_for_keys, std::vector<int>* slots_for_values) {
  // First populate 'slots_for_keys'.
  slots_for_keys->reserve(order_by_keys.size());
  for (const std::unique_ptr<KeyArg>& order_by_key : order_by_keys) {
    const std::optional<int> slot_idx =
        schema.FindIndexForVariable(order_by_key->variable());
    GOOGLESQL_RET_CHECK(slot_idx.has_value()).EmitStackTrace()
        << order_by_key->DebugString()
        << " order_by_key->variable()=" << order_by_key->variable();
    slots_for_keys->push_back(slot_idx.value());
  }
  absl::flat_hash_set<int> slots_for_keys_set(slots_for_keys->begin(),
                                              slots_for_keys->end());

  // The other slots contain values.
  slots_for_values->reserve(schema.num_variables() - slots_for_keys_set.size());
  for (int i = 0; i < schema.num_variables(); ++i) {
    if (!slots_for_keys_set.contains(i)) {
      slots_for_values->push_back(i);
    }
  }

  return absl::OkStatus();
}

namespace {

// `MultiLevelAggregateAccumulator` aggregates input tuples using the input
// inner grouping keys and inner aggregators. The resulting aggregate tuples are
// then passed to the underlying `accumulator` for further aggregation.
class MultiLevelAggregateAccumulator : public IntermediateAggregateAccumulator {
 public:
  static absl::StatusOr<std::unique_ptr<MultiLevelAggregateAccumulator>> Create(
      absl::Span<const TupleData* const> params,
      const Type* aggregate_function_input_type,
      std::vector<const ValueExpr*> aggregate_function_input_fields,
      std::vector<const KeyArg*> inner_grouping_keys,
      std::vector<const AggregateArg*> inner_aggregators,
      std::unique_ptr<IntermediateAggregateAccumulator> accumulator,
      EvaluationContext* context) {
    GOOGLESQL_ASSIGN_OR_RETURN(CollatorList collators,
                     SetupCollators(params, inner_grouping_keys, context));
    return absl::WrapUnique(new MultiLevelAggregateAccumulator(
        params, aggregate_function_input_type, aggregate_function_input_fields,
        std::move(inner_grouping_keys), std::move(inner_aggregators),
        std::move(accumulator), std::move(collators), context));
  }

  MultiLevelAggregateAccumulator(const MultiLevelAggregateAccumulator&) =
      delete;
  MultiLevelAggregateAccumulator& operator=(
      const MultiLevelAggregateAccumulator&) = delete;

  absl::Status Reset() override {
    accumulator_.reset();
    group_map_.clear();
    group_map_keys_memory_.clear();
    return absl::OkStatus();
  }

  bool Accumulate(const TupleData& input_row, const Value& input_value,
                  bool* stop_accumulation, absl::Status* status) override {
    *status = AccumulateTuple(
        &input_row, params_, grouping_sets_, collators_,
        static_cast<int>(inner_grouping_keys_.size()),
        static_cast<int>(inner_grouping_keys_.size()), inner_grouping_keys_,
        inner_aggregators_, context_, unordered_array_collision_tracker_,
        group_map_, group_map_keys_memory_, *stop_accumulation);
    if (!status->ok()) {
      return false;
    }
    return true;
  }

  absl::StatusOr<Value> GetFinalResult(bool inputs_in_defined_order) override {
    // Grab the inner aggregated tuples.
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<TupleDataDeque> tuples,
        GatherTuples(/*num_extra_slots=*/0,
                     static_cast<int>(inner_grouping_keys_.size()),
                     static_cast<int>(inner_grouping_keys_.size()),
                     /*has_grouping_sets=*/false, params_, inner_grouping_keys_,
                     inner_aggregators_, group_map_, group_map_keys_memory_,
                     context_));

    // Call `Accumulate` on the underlying `accumulator_` with the inner
    // aggregated tuples.
    while (!tuples->IsEmpty()) {
      auto tuple = tuples->PopFront();
      absl::Status status;

      std::vector<Value> values(aggregate_function_input_fields_.size());
      for (int i = 0; i < aggregate_function_input_fields_.size(); ++i) {
        const ValueExpr* value_expr = aggregate_function_input_fields_[i];

        std::shared_ptr<TupleSlot::SharedProtoState> shared_state;
        VirtualTupleSlot slot(&values[i], &shared_state);

        if (!value_expr->Eval(
                ConcatSpans(absl::Span<const TupleData* const>(params_),
                            {tuple.get()}),
                context_, &slot, &status)) {
          return status;
        }
      }

      Value value;
      if (values.size() == 1) {
        value = std::move(values[0]);
      } else {
        GOOGLESQL_ASSIGN_OR_RETURN(value, Value::MakeStructFromValidatedInputs(
                                    aggregate_function_input_type_->AsStruct(),
                                    std::move(values)));
      }

      bool stop_accumulation = false;
      if (!accumulator_->Accumulate(*tuple, value, &stop_accumulation,
                                    &status)) {
        return status;
      }
      if (stop_accumulation) {
        break;
      }
    }

    // `GetFinalResult` from the underlying `accumulator_`.
    // Inputs to the underlying `accumulator_` are never in any defined order,
    // since the inner grouping operation never preserves order.
    return accumulator_->GetFinalResult(/*inputs_in_defined_order=*/false);
  }

 private:
  MultiLevelAggregateAccumulator(
      absl::Span<const TupleData* const> params,
      const Type* aggregate_function_input_type,
      std::vector<const ValueExpr*> aggregate_function_input_fields,
      std::vector<const KeyArg*> inner_grouping_keys,
      std::vector<const AggregateArg*> inner_aggregators,
      std::unique_ptr<IntermediateAggregateAccumulator> accumulator,
      CollatorList collators, EvaluationContext* context)
      : params_(params),
        aggregate_function_input_type_(aggregate_function_input_type),
        aggregate_function_input_fields_(aggregate_function_input_fields),
        inner_grouping_keys_(inner_grouping_keys),
        inner_aggregators_(inner_aggregators),
        accumulator_(std::move(accumulator)),
        collators_(std::move(collators)),
        context_(context) {}

  absl::Span<const TupleData* const> params_;
  const Type* aggregate_function_input_type_;
  std::vector<const ValueExpr*> aggregate_function_input_fields_;
  std::vector<const KeyArg*> inner_grouping_keys_;
  std::vector<const AggregateArg*> inner_aggregators_;
  std::unique_ptr<IntermediateAggregateAccumulator> accumulator_;

  std::vector<int64_t> grouping_sets_ = {NoGroupingSetId()};
  absl::flat_hash_map<TupleDataPtr, std::unique_ptr<GroupValue>> group_map_;
  std::vector<std::unique_ptr<TupleData>> group_map_keys_memory_;
  CollatorList collators_;
  UnorderedArrayCollisionTracker unordered_array_collision_tracker_;

  EvaluationContext* context_;
};

// Create a vector of raw pointers from a vector of unique pointers.
template <typename T>
std::vector<const T*> RawPtrVector(
    const std::vector<std::unique_ptr<T>>& unique_ptr_vector) {
  std::vector<const T*> raw_ptr_vector;
  raw_ptr_vector.reserve(unique_ptr_vector.size());
  std::transform(
      unique_ptr_vector.begin(), unique_ptr_vector.end(),
      std::back_inserter(raw_ptr_vector),
      [](const std::unique_ptr<T>& unique_ptr) { return unique_ptr.get(); });
  return raw_ptr_vector;
}

}  // namespace

absl::StatusOr<std::unique_ptr<AggregateArgAccumulator>>
AggregateArg::CreateAccumulator(absl::Span<const TupleData* const> params,
                                EvaluationContext* context) const {
  // Build the underlying AggregateAccumulator.
  std::vector<Value> args(parameter_list_size());
  for (int i = 0; i < parameter_list_size(); ++i) {
    std::shared_ptr<TupleSlot::SharedProtoState> shared_state;
    VirtualTupleSlot slot(&args[i], &shared_state);
    absl::Status status;
    if (!parameter(i)->Eval(params, context, &slot, &status)) return status;
  }
  GOOGLESQL_ASSIGN_OR_RETURN(CollatorList collator_list,
                   MakeCollatorList(collation_list()));

  std::unique_ptr<IntermediateAggregateAccumulator> accumulator;

  // Creates a special accumulator for GROUPING function aggregator. Other
  // aggregators use the AggregateAccumulator as underlying accumulator which
  // only accumulates the argument value. GROUPING function is different, its
  // argument is just a key index, and the aggregated result will be calculated
  // based on the both input rows and the key index. So we need create a special
  // accumulator for it.
  if (IsGroupingFunction(aggregate_function())) {
    accumulator = std::make_unique<GroupingAggregateArgAccumulator>();
  } else {
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<AggregateAccumulator> underlying_accumulator,
        aggregate_function()->function()->CreateAccumulator(
            args, params, std::move(collator_list), context));
    // Adapt the underlying AggregateAccumulator to the
    // IntermediateAggregateAccumulator interface so that we can stack other
    // intermediate accumulators on top of it.
    accumulator = std::make_unique<AggregateAccumulatorAdaptor>(
        aggregate_function()->output_type(), error_mode_,
        std::move(underlying_accumulator));
  }

  const TupleSchema* agg_fn_input_schema = group_schema_.get();
  std::unique_ptr<const TupleSchema> group_rows_schema;
  if (group_rows_subquery_ != nullptr) {
    group_rows_schema = group_rows_subquery_->CreateOutputSchema();
    agg_fn_input_schema = group_rows_schema.get();
  }
  // LIMIT support.
  bool consumed_order_by = false;
  if (limit() != nullptr) {
    TupleSlot limit_slot;
    absl::Status status;
    if (!limit()->EvalSimple(params, context, &limit_slot, &status))
      return status;
    const Value& limit_val = limit_slot.value();
    if (limit_val.int64_value() < 0) {
      return ::googlesql_base::OutOfRangeErrorBuilder()
             << "Limit requires non-negative count";
    }
    if (context->options().use_top_n_accumulator_when_possible &&
        !order_by_keys().empty()) {
      // Optimization: to save memory, use TopNAccumulator instead of
      // LimitAccumulator(OrderByAccumulator).
      std::vector<int> slots_for_keys;
      std::vector<int> slots_for_values;
      GOOGLESQL_RETURN_IF_ERROR(
          PopulateSlotsForKeysAndValues(*agg_fn_input_schema, order_by_keys(),
                                        &slots_for_keys, &slots_for_values));
      GOOGLESQL_ASSIGN_OR_RETURN(
          auto tuple_comparator,
          TupleComparator::Create(UniquePtrSpanToPtrVector(order_by_keys()),
                                  slots_for_keys, params, context));
      accumulator = std::make_unique<TopNAccumulator>(
          limit_val.int64_value(), std::move(tuple_comparator),
          std::move(accumulator), context);
      consumed_order_by = true;
      context->set_used_top_n_accumulator(true);
    } else {
      accumulator = std::make_unique<LimitAccumulator>(
          limit_val.int64_value(), std::move(accumulator), context);
    }
  }

  // ORDER BY support.
  if (!consumed_order_by && !order_by_keys().empty()) {
    std::vector<int> slots_for_keys;
    std::vector<int> slots_for_values;
    GOOGLESQL_RETURN_IF_ERROR(
        PopulateSlotsForKeysAndValues(*agg_fn_input_schema, order_by_keys(),
                                      &slots_for_keys, &slots_for_values));

    accumulator = std::make_unique<OrderByAccumulator>(
        order_by_keys(), slots_for_keys, slots_for_values, params,
        std::move(accumulator), context);
  }

  // DISTINCT support.
  if (distinct()) {
    GOOGLESQL_ASSIGN_OR_RETURN(CollatorList collator_list,
                     MakeCollatorList(collation_list()));
    GOOGLESQL_RET_CHECK_LE(collator_list.size(), 1);
    accumulator = std::make_unique<DistinctAccumulator>(
        std::move(accumulator), context,
        collator_list.empty() ? nullptr : std::move(collator_list[0]));
  }

  // Support for aggregation functions that ignore NULLs.
  if (ignores_null()) {
    const bool use_compound_values = (num_input_fields() > 1);
    accumulator = std::make_unique<IgnoresNullAccumulator>(
        use_compound_values, std::move(accumulator));
  }

  // HAVING MAX/MIN support.
  if (having_modifier_kind() != AggregateArg::kHavingNone) {
    GOOGLESQL_RET_CHECK(having_expr() != nullptr);
    const bool use_max = (having_modifier_kind() == AggregateArg::kHavingMax);
    accumulator = std::make_unique<HavingExtremalValueAccumulator>(
        params, having_expr(), use_max, std::move(accumulator), context);
  }

  // Adapt 'accumulator' to the AggregateArgAccumulator interface.
  const Type* type = input_type();
  std::vector<const ValueExpr*> input_fields;
  input_fields.reserve(input_field_list_size());
  for (int i = 0; i < input_field_list_size(); ++i) {
    input_fields.push_back(input_field(i));
  }
  if (group_rows_subquery_ != nullptr) {
    GOOGLESQL_RET_CHECK(inner_grouping_keys_.empty());
    // Create accumulator that knows the subquery and how to accumulate the
    // aggregate function in the end. At iteration if would interact with
    // GroupRowsOp.
    accumulator = std::make_unique<WithGroupRowsAccumulator>(
        params, group_rows_subquery_.get(), input_fields, type,
        std::move(accumulator), context);
    type = EmptyStructType();
    input_fields.clear();
  }

  // HAVING filter support.
  if (having_filter_ != nullptr) {
    GOOGLESQL_RET_CHECK(!inner_grouping_keys_.empty());
    accumulator = std::make_unique<FilteredArgAccumulator>(
        params, std::move(accumulator), having_filter_.get(), context);
  }

  // GROUP BY support (Multi-level aggregation).
  if (!inner_grouping_keys_.empty()) {
    std::vector<const KeyArg*> inner_grouping_keys_ptrs =
        RawPtrVector(inner_grouping_keys_);
    std::vector<const AggregateArg*> inner_aggregators_ptrs =
        RawPtrVector(inner_aggregators_);
    GOOGLESQL_ASSIGN_OR_RETURN(accumulator, MultiLevelAggregateAccumulator::Create(
                                      params, type, input_fields,
                                      std::move(inner_grouping_keys_ptrs),
                                      std::move(inner_aggregators_ptrs),
                                      std::move(accumulator), context));
    // Always clear input fields and the type, because we don't want
    // `IntermediateAggregateAccumulatorAdaptor` to directly evaluate the inputs
    // if multi-level aggregation is present.
    type = EmptyStructType();
    input_fields.clear();
  }

  // WHERE filter support.
  if (filter() != nullptr) {
    accumulator = std::make_unique<FilteredArgAccumulator>(
        params, std::move(accumulator), filter(), context);
  }

  // Adapt 'accumulator' to the AggregateArgAccumulator interface.
  return std::make_unique<IntermediateAggregateAccumulatorAdaptor>(
      params, input_fields, type, std::move(accumulator), context);
}

absl::StatusOr<Value> AggregateArg::EvalAgg(
    absl::Span<const TupleData* const> group,
    absl::Span<const TupleData* const> params,
    EvaluationContext* context) const {
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<AggregateArgAccumulator> accumulator,
                   CreateAccumulator(params, context));

  bool stop_aggregation = false;
  absl::Status status;
  for (const TupleData* row : group) {
    if (!accumulator->Accumulate(*row, &stop_aggregation, &status)) {
      return status;
    }
    if (stop_aggregation) break;
  }

  const absl::StatusOr<Value> status_or_result = accumulator->GetFinalResult(
      /*inputs_in_defined_order=*/false);
  if (!status_or_result.ok()) {
    if (ShouldSuppressError(status_or_result.status(), error_mode_)) {
      return Value::Null(type());
    }
    return status_or_result.status();
  }
  return status_or_result.value();
}

std::string AggregateArg::DebugInternal(const std::string& indent,
                                        bool verbose) const {
  std::string result;
  absl::StrAppend(&result, "$", variable().ToString());
  if (verbose) {
    absl::StrAppend(&result, "[", type()->DebugString(), "]");
  }
  absl::StrAppend(&result, " := ");

  std::vector<std::string> order_keys_strs;
  for (const std::unique_ptr<KeyArg>& order_key : order_by_keys()) {
    order_keys_strs.push_back(
        order_key->DebugInternal("" /* indent */, verbose));
  }

  absl::StrAppend(
      &result, distinct() == kDistinct ? "DISTINCT " : "",
      aggregate_function()->DebugInternal(indent, verbose),
      having_expr_ != nullptr
          ? absl::StrCat(
                " HAVING ",
                (having_modifier_kind_ == kHavingMax ? "MAX " : "MIN "),
                having_expr_->DebugInternal("" /* indent */, verbose))
          : "",
      !inner_grouping_keys_.empty()
          ? absl::StrCat(" GROUP BY ",
                         absl::StrJoin(inner_grouping_keys_, ",",
                                       [](std::string* out,
                                          const std::unique_ptr<KeyArg>& key) {
                                         absl::StrAppend(out,
                                                         key->DebugString());
                                       }))
          : "",
      !order_by_keys().empty()
          ? absl::StrCat(" ORDER BY ", absl::StrJoin(order_keys_strs, ","))
          : "",
      limit_ != nullptr ? absl::StrCat(" LIMIT ", limit_->DebugInternal(
                                                      "" /* indent */, verbose))
                        : "",
      !ignores_null() ? " [ignores_null = false]" : "",
      filter_ != nullptr ? absl::StrCat(" FILTER ", filter_->DebugInternal(
                                                        /*indent=*/"", verbose))
                         : "",
      !collation_list_.empty()
          ? absl::StrCat(" ", ResolvedCollation::ToString(collation_list_))
          : "");
  if (group_rows_subquery_ != nullptr) {
    std::string indent_child = indent + AggregateOp::kIndentSpace;
    absl::StrAppend(&result, indent_child, AggregateOp::kIndentFork,
                    "with_group_rows_subquery: ",
                    group_rows_subquery_->DebugInternal(
                        indent_child + AggregateOp::kIndentSpace, verbose));
  }
  return result;
}

// Returns 'order_keys' as a vector<const KeyArg*>. The caller owns the returned
// pointers.
static std::vector<const KeyArg*> ReleaseAllOrderKeys(
    std::vector<std::unique_ptr<KeyArg>> order_keys) {
  std::vector<const KeyArg*> ret;
  ret.reserve(order_keys.size());
  for (std::unique_ptr<KeyArg>& key : order_keys) {
    ret.push_back(key.release());
  }
  return ret;
}

AggregateArg::AggregateArg(
    const VariableId& variable,
    std::unique_ptr<AggregateFunctionCallExpr> function, Distinctness distinct,
    std::unique_ptr<ValueExpr> having_expr,
    const HavingModifierKind having_modifier_kind,
    std::vector<std::unique_ptr<KeyArg>> order_by_keys,
    std::unique_ptr<ValueExpr> limit,
    std::unique_ptr<RelationalOp> group_rows_subquery,
    std::vector<std::unique_ptr<KeyArg>> inner_grouping_keys,
    std::vector<std::unique_ptr<AggregateArg>> inner_aggregators,
    ResolvedFunctionCallBase::ErrorMode error_mode,
    std::unique_ptr<ValueExpr> filter, std::unique_ptr<ValueExpr> having_filter,
    const std::vector<ResolvedCollation>& collation_list,
    const VariableId& side_effects_variable)
    : ExprArg(variable, std::move(function)),
      distinct_(distinct),
      having_expr_(std::move(having_expr)),
      having_modifier_kind_(having_modifier_kind),
      order_by_keys_(std::move(order_by_keys)),
      limit_(std::move(limit)),
      group_rows_subquery_(std::move(group_rows_subquery)),
      inner_grouping_keys_(std::move(inner_grouping_keys)),
      inner_aggregators_(std::move(inner_aggregators)),
      error_mode_(error_mode),
      filter_(std::move(filter)),
      having_filter_(std::move(having_filter)),
      collation_list_(collation_list),
      side_effects_variable_(side_effects_variable) {}

const AggregateFunctionCallExpr* AggregateArg::aggregate_function() const {
  return static_cast<const AggregateFunctionCallExpr*>(value_expr());
}

AggregateFunctionCallExpr* AggregateArg::mutable_aggregate_function() {
  return static_cast<AggregateFunctionCallExpr*>(mutable_value_expr());
}

int AggregateArg::num_input_fields() const {
  return aggregate_function()->function()->num_input_fields();
}

const Type* AggregateArg::input_type() const {
  return aggregate_function()->function()->input_type();
}

bool AggregateArg::ignores_null() const {
  return aggregate_function()->function()->ignores_null();
}

const ValueExpr* AggregateArg::input_field(int i) const {
  return aggregate_function()->GetArgs()[i]->node()->AsValueExpr();
}

ValueExpr* AggregateArg::mutable_input_field(int i) {
  return mutable_aggregate_function()
      ->GetMutableArgs()[i]
      ->mutable_node()
      ->AsMutableValueExpr();
}

int AggregateArg::parameter_list_size() const {
  return aggregate_function()->GetArgs().size() - num_input_fields();
}

const ValueExpr* AggregateArg::parameter(int i) const {
  return aggregate_function()
      ->GetArgs()[i + num_input_fields()]
      ->node()
      ->AsValueExpr();
}

ValueExpr* AggregateArg::mutable_parameter(int i) {
  return mutable_aggregate_function()
      ->GetMutableArgs()[i + num_input_fields()]
      ->mutable_node()
      ->AsMutableValueExpr();
}

const ValueExpr* AggregateArg::filter() const { return filter_.get(); }
ValueExpr* AggregateArg::mutable_filter() { return filter_.get(); }

// -------------------------------------------------------
// AggregateOp
// -------------------------------------------------------

absl::string_view kAggregateTupleIteratorDebugName = "AggregationTupleIterator";

std::string AggregateOp::GetIteratorDebugString(
    absl::string_view input_iter_debug_string) {
  return absl::StrCat(kAggregateTupleIteratorDebugName, "(",
                      input_iter_debug_string, ")");
}

absl::StatusOr<std::unique_ptr<AggregateOp>> AggregateOp::Create(
    std::vector<std::unique_ptr<KeyArg>> keys,
    std::vector<std::unique_ptr<AggregateArg>> aggregators,
    std::unique_ptr<RelationalOp> input, std::vector<int64_t> grouping_sets) {
  for (auto& arg : keys) {
    GOOGLESQL_RET_CHECK(arg->type()->SupportsEquality());
  }
  return absl::WrapUnique(
      new AggregateOp(std::move(keys), std::move(aggregators), std::move(input),
                      std::move(grouping_sets)));
}

absl::Status AggregateOp::SetSchemasForEvaluation(
    absl::Span<const TupleSchema* const> params_schemas) {
  GOOGLESQL_RETURN_IF_ERROR(mutable_input()->SetSchemasForEvaluation(params_schemas));

  const std::unique_ptr<const TupleSchema> input_schema =
      mutable_input()->CreateOutputSchema();

  for (KeyArg* key : mutable_keys()) {
    GOOGLESQL_RETURN_IF_ERROR(key->mutable_value_expr()->SetSchemasForEvaluation(
        ConcatSpans(params_schemas, {input_schema.get()})));
  }

  for (AggregateArg* arg : mutable_aggregators()) {
    GOOGLESQL_RETURN_IF_ERROR(
        arg->SetSchemasForEvaluation(*input_schema, params_schemas));
  }

  return absl::OkStatus();
}

namespace {

// Outputs a pre-computed list of tuples.
class AggregateTupleIterator : public TupleIterator {
 public:
  AggregateTupleIterator(
      absl::Span<const TupleData* const> params,
      std::unique_ptr<TupleDataDeque> tuples,
      std::unique_ptr<TupleIterator> input_iter_for_debug_string,
      std::unique_ptr<TupleSchema> output_schema, EvaluationContext* context)
      : params_(params.begin(), params.end()),
        output_schema_(std::move(output_schema)),
        tuples_(std::move(tuples)),
        input_iter_for_debug_string_(std::move(input_iter_for_debug_string)),
        context_(context) {}

  AggregateTupleIterator(const AggregateTupleIterator&) = delete;
  AggregateTupleIterator& operator=(const AggregateTupleIterator&) = delete;

  const TupleSchema& Schema() const override { return *output_schema_; }

  TupleData* Next() override {
    if (tuples_->IsEmpty()) return nullptr;
    if (num_next_calls_ %
            absl::GetFlag(
                FLAGS_googlesql_call_verify_not_aborted_rows_period) ==
        0) {
      absl::Status status = context_->VerifyNotAborted();
      if (!status.ok()) {
        status_ = status;
        return nullptr;
      }
    }
    ++num_next_calls_;

    current_ = tuples_->PopFront();
    return current_.get();
  }

  absl::Status Status() const override { return status_; }

  std::string DebugString() const override {
    return absl::StrCat(kAggregateTupleIteratorDebugName, "(",
                        input_iter_for_debug_string_->DebugString(), ")");
  }

 private:
  const std::vector<const TupleData*> params_;
  const std::unique_ptr<TupleSchema> output_schema_;
  const std::vector<const AggregateArg*> aggregators_;
  const std::unique_ptr<TupleDataDeque> tuples_;
  // We store a TupleIterator instead of the debug string to avoid computing the
  // debug string unnecessarily.
  const std::unique_ptr<TupleIterator> input_iter_for_debug_string_;
  std::unique_ptr<TupleData> current_;
  EvaluationContext* context_;
  absl::Status status_;
  int64_t num_next_calls_ = 0;
};

}  // namespace

absl::StatusOr<std::unique_ptr<TupleIterator>> AggregateOp::CreateIterator(
    absl::Span<const TupleData* const> params, int num_extra_slots,
    EvaluationContext* context) const {
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<TupleIterator> input_iter,
      input()->CreateIterator(params, /*num_extra_slots=*/0, context));

  // The key is owned by the <group_map_keys_memory> defined below.
  absl::flat_hash_map<TupleDataPtr, std::unique_ptr<GroupValue>> group_map;
  std::vector<std::unique_ptr<TupleData>> group_map_keys_memory;
  GOOGLESQL_ASSIGN_OR_RETURN(CollatorList collators,
                   SetupCollators(params, keys(), context));
  UnorderedArrayCollisionTracker unordered_array_collision_tracker;

  std::vector<int64_t> grouping_sets = grouping_sets_;
  bool has_grouping_sets = !grouping_sets.empty();
  // The number of keys in AggregateOp.
  int key_size = static_cast<int>(keys().size());
  // The number of actual keys used for grouping. For grouping sets, we group by
  // an extra key - grouping set offset.
  int grouping_key_size = has_grouping_sets ? key_size + 1 : key_size;
  // To simplify the code below, when it's a regular group by query without
  // GROUPING SETS/CUBE/ROLLUP, we also convert the group-by keys to a grouping
  // set id with value -1. Theoretically we can use (1 << n) - 1 to represent
  // GROUP BY key1, key2, ..., keyn. However this will add a limitation to the
  // regular group by query that it can have at most 63 keys. It's guaranteed a
  // grouping set id from grouping set query will be always positive.
  if (grouping_sets.empty()) {
    grouping_sets.push_back(NoGroupingSetId());
  }
  while (true) {
    const TupleData* next_input = input_iter->Next();
    if (next_input == nullptr) {
      GOOGLESQL_RETURN_IF_ERROR(input_iter->Status());
      break;
    }
    bool stop_reading_input = false;
    GOOGLESQL_RETURN_IF_ERROR(
        AccumulateTuple(next_input, params, grouping_sets, collators, key_size,
                        grouping_key_size, keys(), aggregators(), context,
                        unordered_array_collision_tracker, group_map,
                        group_map_keys_memory, stop_reading_input));
    if (stop_reading_input) {
      break;
    }
  }

  // Build the tuples that the iterator should return.
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<TupleDataDeque> tuples,
      GatherTuples(num_extra_slots, key_size, grouping_key_size,
                   has_grouping_sets, params, keys(), aggregators(), group_map,
                   group_map_keys_memory, context));

  auto input_schema =
      std::make_unique<TupleSchema>(input_iter->Schema().variables());
  std::unique_ptr<TupleIterator> iter =
      std::make_unique<AggregateTupleIterator>(params, std::move(tuples),
                                               std::move(input_iter),
                                               CreateOutputSchema(), context);
  return MaybeReorder(std::move(iter), context);
}

std::unique_ptr<TupleSchema> AggregateOp::CreateOutputSchema() const {
  // Some aggregations may be deferred. To simplify the code, we just create a
  // side effect variable for each aggregation, even if it's not deferred.
  // However, only those that are deferred (signified by being valid and
  // corresponding to a resolved side-effects column) will make it to the output
  // schema.
  // The variable arrangement is:
  // 1. grouping_keys
  // 2. variables for aggregations
  // 3. side-effects variable for each aggregation.
  std::vector<VariableId> vars;
  vars.reserve(keys().size() + 2 * aggregators().size());
  for (const KeyArg* key : keys()) {
    vars.push_back(key->variable());
  }
  for (const AggregateArg* aggregator : aggregators()) {
    vars.push_back(aggregator->variable());
  }
  for (const AggregateArg* aggregator : aggregators()) {
    if (aggregator->side_effects_variable().is_valid()) {
      vars.push_back(aggregator->side_effects_variable());
    }
  }

  return std::make_unique<TupleSchema>(vars);
}

std::string AggregateOp::IteratorDebugString() const {
  return GetIteratorDebugString(input()->IteratorDebugString());
}

std::string AggregateOp::DebugInternal(const std::string& indent,
                                       bool verbose) const {
  bool has_grouping_sets = !grouping_sets_.empty();
  std::string args_debug_string = ArgDebugString(
      {"keys", "aggregators", "input"}, {kN, kN, k1}, indent, verbose,
      /*more_children=*/has_grouping_sets);
  // Only append grouping_sets debug string to AggregateOp when it's not empty.
  std::string grouping_sets_debug_string = "";
  if (has_grouping_sets) {
    grouping_sets_debug_string = absl::StrCat(
        indent, kIndentFork, "grouping_sets: [",
        absl::StrJoin(grouping_sets_, ",",
                      [](std::string* out, int64_t grouping_set) {
                        absl::StrAppend(out, "0x", absl::Hex(grouping_set));
                      }),
        "]");
  }
  return absl::StrCat("AggregateOp(", args_debug_string,
                      grouping_sets_debug_string, ")");
}

AggregateOp::AggregateOp(std::vector<std::unique_ptr<KeyArg>> keys,
                         std::vector<std::unique_ptr<AggregateArg>> aggregators,
                         std::unique_ptr<RelationalOp> input,
                         std::vector<int64_t> grouping_sets) {
  SetArgs<KeyArg>(kKey, std::move(keys));
  SetArgs<AggregateArg>(kAggregator, std::move(aggregators));
  SetArg(kInput, std::make_unique<RelationalArg>(std::move(input)));
  grouping_sets_ = grouping_sets;
}

absl::Span<const KeyArg* const> AggregateOp::keys() const {
  return GetArgs<KeyArg>(kKey);
}

absl::Span<KeyArg* const> AggregateOp::mutable_keys() {
  return GetMutableArgs<KeyArg>(kKey);
}

absl::Span<const AggregateArg* const> AggregateOp::aggregators() const {
  return GetArgs<AggregateArg>(kAggregator);
}

absl::Span<AggregateArg* const> AggregateOp::mutable_aggregators() {
  return GetMutableArgs<AggregateArg>(kAggregator);
}

const RelationalOp* AggregateOp::input() const {
  return GetArg(kInput)->node()->AsRelationalOp();
}

RelationalOp* AggregateOp::mutable_input() {
  return GetMutableArg(kInput)->mutable_node()->AsMutableRelationalOp();
}

absl::Span<const int64_t> AggregateOp::grouping_sets() const {
  return absl::MakeSpan(grouping_sets_);
}

//  static
absl::StatusOr<std::unique_ptr<GroupRowsOp>> GroupRowsOp::Create(
    std::vector<std::unique_ptr<ExprArg>> columns) {
  return absl::WrapUnique(new GroupRowsOp(std::move(columns)));
}

GroupRowsOp::GroupRowsOp(std::vector<std::unique_ptr<ExprArg>> columns) {
  SetArgs<ExprArg>(kColumn, std::move(columns));
}

absl::Span<const ExprArg* const> GroupRowsOp::columns() const {
  return GetArgs<ExprArg>(kColumn);
}

std::unique_ptr<TupleSchema> GroupRowsOp::CreateOutputSchema() const {
  std::vector<VariableId> vars;
  vars.reserve(columns().size());
  for (const ExprArg* column : columns()) {
    vars.push_back(column->variable());
  }
  return std::make_unique<TupleSchema>(vars);
}

absl::Status GroupRowsOp::SetSchemasForEvaluation(
    absl::Span<const TupleSchema* const> params_schemas) {
  return absl::OkStatus();
}

std::string GroupRowsOp::DebugInternal(const std::string& indent,
                                       bool verbose) const {
  return absl::StrCat("GroupRowsOp(",
                      ArgDebugString({"columns"}, {kN}, indent, verbose), ")");
}

//  static
std::string GroupRowsOp::GetIteratorDebugString(
    absl::string_view input_iter_debug_string) {
  return absl::StrCat("GroupRowsTupleIterator(", input_iter_debug_string, ")");
}

std::string GroupRowsOp::IteratorDebugString() const {
  return GetIteratorDebugString("<outer_from_clause>");
}

namespace {

// Iterates over TupleDataDeque
// Outputs a pre-computed list of tuples.
class TupleDataDequeIterator : public TupleIterator {
 public:
  TupleDataDequeIterator(const TupleDataDeque& tuples, int num_extra_slots,
                         std::unique_ptr<TupleSchema> output_schema,
                         EvaluationContext* context)
      : tuples_(tuples.GetTuplePtrs()),
        output_schema_(std::move(output_schema)),
        num_extra_slots_(num_extra_slots),
        context_(context) {}

  TupleDataDequeIterator(const TupleDataDequeIterator&) = delete;
  TupleDataDequeIterator& operator=(const TupleDataDequeIterator&) = delete;

  const TupleSchema& Schema() const override { return *output_schema_; }

  TupleData* Next() override {
    if (current_input_tuple_ >= tuples_.size()) return nullptr;
    if (current_input_tuple_ %
            absl::GetFlag(
                FLAGS_googlesql_call_verify_not_aborted_rows_period) ==
        0) {
      absl::Status status = context_->VerifyNotAborted();
      if (!status.ok()) {
        status_ = status;
        return nullptr;
      }
    }
    const TupleData* input_tuple = tuples_[current_input_tuple_];
    current_ = std::make_unique<TupleData>(input_tuple->slots());
    current_->AddSlots(num_extra_slots_);
    current_input_tuple_++;
    return current_.get();
  }

  absl::Status Status() const override { return status_; }

  std::string DebugString() const override { return "GroupRowsIterator()"; }

 private:
  const std::vector<const TupleData*> tuples_;
  int64_t current_input_tuple_ = 0;
  const std::unique_ptr<TupleSchema> output_schema_;
  int num_extra_slots_;
  std::unique_ptr<TupleData> current_;
  EvaluationContext* context_;
  absl::Status status_;
};

}  // namespace

absl::StatusOr<std::unique_ptr<TupleIterator>> GroupRowsOp::CreateIterator(
    absl::Span<const TupleData* const> params, int num_extra_slots,
    EvaluationContext* context) const {
  if (context->active_group_rows() == nullptr) {
    return ::googlesql_base::OutOfRangeErrorBuilder()
           << "GROUP_ROWS() cannot read group rows data it the current context";
  }

  std::unique_ptr<TupleIterator> iter =
      std::make_unique<TupleDataDequeIterator>(*context->active_group_rows(),
                                               num_extra_slots,
                                               CreateOutputSchema(), context);
  return MaybeReorder(std::move(iter), context);
}

std::unique_ptr<RowsForUdaOp> RowsForUdaOp::Create(
    std::vector<VariableId> arguments) {
  return absl::WrapUnique(new RowsForUdaOp(std::move(arguments)));
}

RowsForUdaOp::RowsForUdaOp(std::vector<VariableId> arguments) {
  arguments_ = std::move(arguments);
}

std::unique_ptr<TupleSchema> RowsForUdaOp::CreateOutputSchema() const {
  return std::make_unique<TupleSchema>(arguments_);
}

absl::Status RowsForUdaOp::SetSchemasForEvaluation(
    absl::Span<const TupleSchema* const> params_schemas) {
  return absl::OkStatus();
}

std::string RowsForUdaOp::DebugInternal(const std::string& indent,
                                        bool verbose) const {
  return absl::StrCat(indent, "RowsForUDAOp");
}

std::string RowsForUdaOp::GetIteratorDebugString(
    absl::string_view input_iter_debug_string) {
  return absl::StrCat("RowsForUDA=TupleIterator(", input_iter_debug_string,
                      ")");
}

std::string RowsForUdaOp::IteratorDebugString() const {
  return GetIteratorDebugString("<outer_from_clause>");
}

absl::StatusOr<std::unique_ptr<TupleIterator>> RowsForUdaOp::CreateIterator(
    absl::Span<const TupleData* const> params, int num_extra_slots,
    EvaluationContext* context) const {
  if (context->active_group_rows() == nullptr) {
    return googlesql_base::OutOfRangeErrorBuilder()
           << "RowsForUdaOp: Cannot read rows from the current context";
  }

  auto iter = std::make_unique<TupleDataDequeIterator>(
      *context->active_group_rows(), num_extra_slots, CreateOutputSchema(),
      context);
  return MaybeReorder(std::move(iter), context);
}

// -------------------------------------------------------
// GrainLockingOp
// -------------------------------------------------------

std::unique_ptr<GrainLockingOp> GrainLockingOp::Create(
    VariableId measure_variable) {
  return absl::WrapUnique(new GrainLockingOp(std::move(measure_variable)));
}

absl::Status GrainLockingOp::SetSchemasForEvaluation(
    absl::Span<const TupleSchema* const> params_schemas) {
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<TupleIterator>> GrainLockingOp::CreateIterator(
    absl::Span<const TupleData* const> params, int num_extra_slots,
    EvaluationContext* context) const {
  if (context->active_group_rows() == nullptr) {
    return googlesql_base::OutOfRangeErrorBuilder()
           << "GrainLockingOp: Cannot read rows from the current context";
  }
  GOOGLESQL_RET_CHECK_EQ(num_extra_slots, 0);

  auto input_iter = std::make_unique<TupleDataDequeIterator>(
      *context->active_group_rows(), num_extra_slots, CreateOutputSchema(),
      context);
  // `grain_locking_keys_set` is used to deduplicate input tuples with the same
  // grain locking key values.
  absl::flat_hash_set<TupleDataPtr> grain_locking_keys_set;
  // `grain_locking_keys_memory` owns the memory referenced by
  // `grain_locking_keys_set`.
  std::vector<std::unique_ptr<TupleData>> grain_locking_keys_memory;
  // `grain_locked_tuples` are the tuples output by this operator.
  auto grain_locked_tuples =
      std::make_unique<TupleDataDeque>(context->memory_accountant());
  for (const TupleData* next_input = input_iter->Next(); next_input != nullptr;
       next_input = input_iter->Next()) {
    // Should be only one slot for the measure variable.
    GOOGLESQL_RET_CHECK_EQ(next_input->num_slots(), 1);
    const Value& measure_value = next_input->slot(0).value();
    // Measure value is NULL. This can only happen in 2 cases:
    //
    // 1. If the measure propagated past an OUTER JOIN. This case is currently
    //    blocked on implementation support for aggregate filtering. For this
    //    case, we simply skip the NULL measure value.
    //
    // 2. If the measure value is produced by scanning an empty relation (e.g.
    //    via LIMIT 0 or a WHERE condition that discards all rows).
    //
    // Either way, we simply skip the NULL measure value. Note that skipping
    // NULL measure values does not affect correctness of the underlying
    // aggregate functions invoked by the measure (i.e. It is still correct to
    // skip NULL measure values for measures like "ARRAY_AGG(X RESPECT NULLS)".
    if (measure_value.is_null()) {
      continue;
    }

    // Create a TupleData for the grain locking keys.
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::vector<int> grain_locking_indices,
        InternalValue::GetMeasureGrainLockingIndices(measure_value));
    auto grain_locking_keys_data =
        std::make_unique<TupleData>(grain_locking_indices.size());
    GOOGLESQL_ASSIGN_OR_RETURN(Value measure_value_as_struct,
                     InternalValue::GetMeasureAsStructValue(measure_value));
    for (int i = 0; i < grain_locking_indices.size(); ++i) {
      grain_locking_keys_data->mutable_slot(i)->SetValue(
          measure_value_as_struct.field(grain_locking_indices[i]));
    }

    // Insert the `grain_locking_keys_data` and `measure_value` into the map.
    auto [_, inserted] = grain_locking_keys_set.emplace(
        TupleDataPtr(grain_locking_keys_data.get()));
    grain_locking_keys_memory.push_back(std::move(grain_locking_keys_data));

    // If the grain locking keys are new, add the input tuple to
    // `grain_locked_tuples`.
    if (inserted) {
      absl::Status status;
      if (!grain_locked_tuples->PushBack(
              std::make_unique<TupleData>(*next_input), &status)) {
        return status;
      }
    }
  }
  GOOGLESQL_RETURN_IF_ERROR(input_iter->Status());

  // Return an iterator over the grain locked tuples.
  std::unique_ptr<TupleIterator> grain_locked_tuples_iter =
      std::make_unique<AggregateTupleIterator>(
          params, std::move(grain_locked_tuples), std::move(input_iter),
          CreateOutputSchema(), context);
  return MaybeReorder(std::move(grain_locked_tuples_iter), context);
}

// Returns the schema consisting of variables corresponding to the
// list of argument names passed to the constructor.
std::unique_ptr<TupleSchema> GrainLockingOp::CreateOutputSchema() const {
  return std::make_unique<TupleSchema>(
      std::vector<VariableId>{measure_variable_});
}

std::string GrainLockingOp::IteratorDebugString() const {
  return "GrainLocking=TupleIterator(<outer_from_clause>)";
}

std::string GrainLockingOp::DebugInternal(const std::string& indent,
                                          bool verbose) const {
  return absl::StrCat(indent, "GrainLockingOp");
}

GrainLockingOp::GrainLockingOp(VariableId measure_variable)
    : measure_variable_(measure_variable) {}

}  // namespace googlesql
