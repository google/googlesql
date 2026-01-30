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

// This file contains the implementation code for evaluating analytic functions.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/common/string_util.h"
#include "googlesql/common/thread_stack.h"
#include "googlesql/public/numeric_value.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/value.h"
#include "googlesql/reference_impl/evaluation.h"
#include "googlesql/reference_impl/operator.h"
#include "googlesql/reference_impl/tuple.h"
#include "googlesql/reference_impl/tuple_comparator.h"
#include "googlesql/reference_impl/variable_id.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "absl/flags/flag.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

// -------------------------------------------------------
// WindowFrameBoundaryArg
// -------------------------------------------------------

std::string WindowFrameBoundaryArg::DebugInternal(const std::string& indent,
                                                  bool verbose) const {
  std::string result("WindowFrameBoundary");
  absl::StrAppend(&result, "(", indent,
                  "  boundary_type=", GetBoundaryTypeString(boundary_type_));
  if (boundary_offset_expr_ != nullptr) {
    absl::StrAppend(&result, ",", indent, "  boundary_offset_expr=",
                    boundary_offset_expr_->DebugInternal(indent, verbose));
  }
  absl::StrAppend(&result, indent, ")");
  return result;
}

absl::StatusOr<std::unique_ptr<WindowFrameBoundaryArg>>
WindowFrameBoundaryArg::Create(BoundaryType boundary_type,
                               std::unique_ptr<ValueExpr> expr) {
  if (boundary_type == kOffsetPreceding || boundary_type == kOffsetFollowing) {
    if (expr == nullptr) {
      return ::googlesql_base::InvalidArgumentErrorBuilder()
             << "Boundary expression required for boundary type "
             << GetBoundaryTypeString(boundary_type);
    }
  } else if (expr != nullptr) {
    return ::googlesql_base::InvalidArgumentErrorBuilder()
           << "Boundary expression not allowed for "
           << GetBoundaryTypeString(boundary_type);
  }

  return absl::WrapUnique(
      new WindowFrameBoundaryArg(boundary_type, std::move(expr)));
}

absl::Status WindowFrameBoundaryArg::SetSchemasForEvaluation(
    absl::Span<const TupleSchema* const> params_schemas) {
  if (boundary_offset_expr_ != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(
        boundary_offset_expr_->SetSchemasForEvaluation(params_schemas));
  }

  params_schemas_.clear();
  params_schemas_.reserve(params_schemas.size());
  for (const TupleSchema* schema : params_schemas) {
    params_schemas_.push_back(
        std::make_unique<const TupleSchema>(schema->variables()));
  }

  return absl::OkStatus();
}

// These operators used by std::plus and std::minus functors to compute the
// RANGE window boundaries. Since the implementation below checks for overflow
// before running the actual computations these operators are guaranteed to be
// supplied with values that would always produce correct results.
NumericValue operator+(NumericValue lh, NumericValue rh) {
  return lh.Add(rh).value();
}
NumericValue operator-(NumericValue lh, NumericValue rh) {
  return lh.Subtract(rh).value();
}

BigNumericValue operator+(BigNumericValue lh, BigNumericValue rh) {
  return lh.Add(rh).value();
}
BigNumericValue operator-(BigNumericValue lh, BigNumericValue rh) {
  return lh.Subtract(rh).value();
}

namespace {

// Applies the Functor to the left and the right value.
template <template <typename> class Functor>
Value DoOperation(const Value& left, const Value& right) {
  ABSL_DCHECK_EQ(left.type_kind(), right.type_kind());
  switch (left.type_kind()) {
    case TYPE_INT32:
      return Value::Int32(
          Functor<int32_t>()(left.int32_value(), right.int32_value()));
    case TYPE_INT64:
      return Value::Int64(
          Functor<int64_t>()(left.int64_value(), right.int64_value()));
    case TYPE_UINT32:
      return Value::Uint32(
          Functor<uint32_t>()(left.uint32_value(), right.uint32_value()));
    case TYPE_UINT64:
      return Value::Uint64(
          Functor<uint64_t>()(left.uint64_value(), right.uint64_value()));
    case TYPE_NUMERIC:
      return Value::Numeric(
          Functor<NumericValue>()(left.numeric_value(), right.numeric_value()));
    case TYPE_BIGNUMERIC:
      return Value::BigNumeric(Functor<BigNumericValue>()(
          left.bignumeric_value(), right.bignumeric_value()));
    case TYPE_FLOAT:
      return Value::Float(
          Functor<float>()(left.float_value(), right.float_value()));
    case TYPE_DOUBLE:
      return Value::Double(
          Functor<double>()(left.double_value(), right.double_value()));
    default:
      ABSL_LOG(FATAL) << left.type()->DebugString() << " not supported";
  }
}

// Returns a Value of the given <type_kind> with a value equal to the maximum
// value of the data type.
Value GetMaxValue(TypeKind type_kind) {
  switch (type_kind) {
    case TYPE_INT32:
      return Value::Int32(std::numeric_limits<int32_t>::max());
    case TYPE_INT64:
      return Value::Int64(std::numeric_limits<int64_t>::max());
    case TYPE_UINT32:
      return Value::Uint32(std::numeric_limits<uint32_t>::max());
    case TYPE_UINT64:
      return Value::Uint64(std::numeric_limits<uint64_t>::max());
    case TYPE_NUMERIC:
      return Value::Numeric(NumericValue::MaxValue());
    case TYPE_BIGNUMERIC:
      return Value::BigNumeric(BigNumericValue::MaxValue());
    case TYPE_FLOAT:
      return Value::Float(std::numeric_limits<float>::max());
    case TYPE_DOUBLE:
      return Value::Double(std::numeric_limits<double>::max());
    default:
      ABSL_LOG(FATAL) << Type::TypeKindToString(type_kind, PRODUCT_INTERNAL)
                 << " not supported";
  }
}

// Returns a Value of the given <type_kind> with a value equal to the minimum
// value of the data type.
Value GetMinValue(TypeKind type_kind) {
  switch (type_kind) {
    case TYPE_INT32:
      return Value::Int32(std::numeric_limits<int32_t>::lowest());
    case TYPE_INT64:
      return Value::Int64(std::numeric_limits<int64_t>::lowest());
    case TYPE_UINT32:
      return Value::Uint32(std::numeric_limits<uint32_t>::lowest());
    case TYPE_UINT64:
      return Value::Uint64(std::numeric_limits<uint64_t>::lowest());
    case TYPE_NUMERIC:
      return Value::Numeric(NumericValue::MinValue());
    case TYPE_BIGNUMERIC:
      return Value::BigNumeric(BigNumericValue::MinValue());
    case TYPE_FLOAT:
      return Value::Float(std::numeric_limits<float>::lowest());
    case TYPE_DOUBLE:
      return Value::Double(std::numeric_limits<double>::lowest());
    default:
      ABSL_LOG(FATAL) << Type::TypeKindToString(type_kind, PRODUCT_INTERNAL)
                 << " not supported";
  }
}

// Returns true if the data type of <value> has NaN and it has the value equal
// to NaN.
bool IsNaN(const Value& value) {
  if (value.is_null()) {
    // A value of NULL is not not-a-number (since it's not a number).
    return false;
  }
  switch (value.type_kind()) {
    case TYPE_FLOAT:
      return std::isnan(value.float_value());
    case TYPE_DOUBLE:
      return std::isnan(value.double_value());
    default:
      return false;
  }
}

}  // namespace

absl::Status WindowFrameBoundaryArg::GetOffsetValue(
    absl::Span<const TupleData* const> params, EvaluationContext* context,
    Value* range_offset_value) const {
  TupleSlot slot;
  absl::Status status;
  if (!boundary_offset_expr_->EvalSimple(params, context, &slot, &status)) {
    return status;
  }
  *range_offset_value = std::move(*slot.mutable_value());

  if (range_offset_value->is_null()) {
    return ::googlesql_base::OutOfRangeErrorBuilder()
           << "The boundary offset value cannot be null";
  }

  if (range_offset_value->type()->IsSignedInteger()) {
    const int64_t offset_value = range_offset_value->ToInt64();
    if (offset_value < 0) {
      return ::googlesql_base::OutOfRangeErrorBuilder()
             << "Window frame offset for PRECEDING or FOLLOWING "
                "must be non-negative, but was "
             << offset_value;
    }
  } else if (range_offset_value->type()->IsFloat()) {
    const float offset_value = range_offset_value->float_value();
    if (offset_value < 0 || std::isnan(offset_value)) {
      return ::googlesql_base::OutOfRangeErrorBuilder()
             << "Window frame offset for PRECEDING or FOLLOWING "
                "must be non-negative, but was "
             << RoundTripFloatToString(offset_value);
    }
  } else if (range_offset_value->type()->IsDouble()) {
    const double offset_value = range_offset_value->double_value();
    if (offset_value < 0 || std::isnan(offset_value)) {
      return ::googlesql_base::OutOfRangeErrorBuilder()
             << "Window frame offset for PRECEDING or FOLLOWING "
                "must be non-negative, but was "
             << RoundTripDoubleToString(offset_value);
    }
  }

  return absl::OkStatus();
}

absl::Status WindowFrameBoundaryArg::GetRowsBasedWindowBoundaries(
    bool is_end_boundary, int partition_size,
    absl::Span<const TupleData* const> params, EvaluationContext* context,
    std::vector<int>* window_boundaries) const {
  GOOGLESQL_RET_CHECK(window_boundaries != nullptr);
  switch (boundary_type_) {
    case kUnboundedPreceding: {
      window_boundaries->assign(partition_size, 0);
      return absl::OkStatus();
    }
    case kUnboundedFollowing: {
      window_boundaries->assign(partition_size, partition_size - 1);
      return absl::OkStatus();
    }
    case kCurrentRow: {
      for (int tuple_id = 0; tuple_id < partition_size; ++tuple_id) {
        window_boundaries->emplace_back(tuple_id);
      }
      return absl::OkStatus();
    }
    case kOffsetPreceding: {
      Value boundary_offset;
      GOOGLESQL_RETURN_IF_ERROR(GetOffsetValue(params, context, &boundary_offset));
      GOOGLESQL_RET_CHECK(boundary_offset.type()->IsInt64());

      // Number of boundaries that logically precedes the first tuple.
      const int num_boundaries_before_first_tuple =
          std::min<int64_t>(partition_size, boundary_offset.int64_value());

      if (is_end_boundary) {
        // Set the end window boundary position that precedes the beginning of
        // the partition to be negative so that we can identify that the
        // associated window is empty.
        window_boundaries->assign(num_boundaries_before_first_tuple, -1);
      } else {
        window_boundaries->assign(num_boundaries_before_first_tuple, 0);
      }

      const int last_boundary_tuple_id =
          partition_size - num_boundaries_before_first_tuple;
      for (int boundary_tuple_id = 0;
           boundary_tuple_id < last_boundary_tuple_id; ++boundary_tuple_id) {
        window_boundaries->emplace_back(boundary_tuple_id);
      }
      return absl::OkStatus();
    }
    case kOffsetFollowing: {
      Value boundary_offset;
      GOOGLESQL_RETURN_IF_ERROR(GetOffsetValue(params, context, &boundary_offset));
      GOOGLESQL_RET_CHECK(boundary_offset.type()->IsInt64());

      for (int64_t boundary_tuple_id = boundary_offset.int64_value();
           boundary_tuple_id < partition_size; ++boundary_tuple_id) {
        window_boundaries->emplace_back(boundary_tuple_id);
      }

      // Number of boundaries that logically follows the last tuple.
      const int num_boundaries_after_last_tuple =
          std::min<int64_t>(partition_size, boundary_offset.int64_value());
      if (is_end_boundary) {
        window_boundaries->insert(window_boundaries->end(),
                                  num_boundaries_after_last_tuple,
                                  partition_size - 1);
      } else {
        // Set the start window boundary position that follows the end of
        // the partition to be partition_size so that we can identify that the
        // associated window is empty.
        window_boundaries->insert(window_boundaries->end(),
                                  num_boundaries_after_last_tuple,
                                  partition_size);
      }

      return absl::OkStatus();
    }
  }
}

absl::Status WindowFrameBoundaryArg::GetRangeBasedWindowBoundaries(
    bool is_end_boundary, const TupleSchema& schema,
    absl::Span<const TupleData* const> partition,
    absl::Span<const KeyArg* const> order_keys,
    absl::Span<const TupleData* const> params, EvaluationContext* context,
    std::vector<int>* window_boundaries) const {
  if (partition.empty()) {
    return absl::OkStatus();
  }

  GOOGLESQL_RET_CHECK(window_boundaries != nullptr);
  GOOGLESQL_RET_CHECK(window_boundaries->empty());

  // Concatenate every tuple with parameters because the order keys used
  // for computing range-based window boundaries can be correlated
  // column values that are stored in parameters.
  std::unique_ptr<TupleSchema> schema_with_params;
  std::vector<std::unique_ptr<TupleData>> tuples_with_params_owners;
  for (const TupleData* tuple : partition) {
    std::vector<Tuple> tuples_to_concat;
    tuples_to_concat.reserve(1 + params.size());
    GOOGLESQL_RET_CHECK_EQ(params_schemas_.size(), params.size());
    for (int i = 0; i < params_schemas_.size(); ++i) {
      tuples_to_concat.push_back(Tuple(params_schemas_[i].get(), params[i]));
    }
    tuples_to_concat.push_back(Tuple(&schema, tuple));

    std::unique_ptr<TupleSchema> new_schema;
    std::unique_ptr<TupleData> new_data;
    ConcatTuples(tuples_to_concat, &new_schema, &new_data);
    if (schema_with_params == nullptr) {
      schema_with_params = std::move(new_schema);
    } else {
      GOOGLESQL_RET_CHECK_EQ(schema_with_params->num_variables(),
                   new_schema->num_variables());
    }
    tuples_with_params_owners.push_back(std::move(new_data));
  }
  std::vector<const TupleData*> tuples_with_params;
  tuples_with_params.reserve(tuples_with_params_owners.size());
  for (const std::unique_ptr<TupleData>& tuple : tuples_with_params_owners) {
    tuples_with_params.push_back(tuple.get());
  }

  std::vector<int> order_key_slot_idxs;
  GOOGLESQL_RETURN_IF_ERROR(GetSlotsForKeysAndValues(*schema_with_params, order_keys,
                                           &order_key_slot_idxs,
                                           /*slots_for_values=*/nullptr));

  window_boundaries->reserve(tuples_with_params.size());
  switch (boundary_type_) {
    case kUnboundedPreceding: {
      window_boundaries->assign(tuples_with_params.size(), 0);
      return absl::OkStatus();
    }
    case kUnboundedFollowing: {
      window_boundaries->assign(tuples_with_params.size(),
                                tuples_with_params.size() - 1);
      return absl::OkStatus();
    }
    case kCurrentRow: {
      GOOGLESQL_RET_CHECK(!order_key_slot_idxs.empty());
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<TupleComparator> comparator,
                       TupleComparator::Create(order_keys, order_key_slot_idxs,
                                               params, context));

      if (is_end_boundary) {
        // We compute the end window boundaries backwards from the last tuple
        // to the first tuple.

        window_boundaries->resize(tuples_with_params.size());
        // The end window boundary for the last tuple is itself.
        (*window_boundaries)[tuples_with_params.size() - 1] =
            tuples_with_params.size() - 1;

        int boundary_tid = tuples_with_params.size() - 1;
        for (int tuple_id = tuples_with_params.size() - 2; tuple_id >= 0;
             --tuple_id) {
          if ((*comparator)(tuples_with_params[boundary_tid],
                            tuples_with_params[tuple_id]) ||
              (*comparator)(tuples_with_params[tuple_id],
                            tuples_with_params[boundary_tid])) {
            boundary_tid = tuple_id;
          }
          (*window_boundaries)[tuple_id] = boundary_tid;
        }
      } else {
        // The start window boundary for the first tuple is itself.
        window_boundaries->emplace_back(0);
        int boundary_tid = 0;
        for (int tuple_id = 1; tuple_id < tuples_with_params.size();
             ++tuple_id) {
          if ((*comparator)(tuples_with_params[boundary_tid],
                            tuples_with_params[tuple_id]) ||
              (*comparator)(tuples_with_params[tuple_id],
                            tuples_with_params[boundary_tid])) {
            boundary_tid = tuple_id;
          }
          window_boundaries->emplace_back(boundary_tid);
        }
      }

      return absl::OkStatus();
    }
    case kOffsetPreceding: {
      GOOGLESQL_RET_CHECK_EQ(order_keys.size(), 1);
      const KeyArg* order_key = order_keys[0];
      GOOGLESQL_RET_CHECK_EQ(order_key_slot_idxs.size(), 1);
      const int order_key_slot_idx = order_key_slot_idxs[0];

      Value offset_value;
      GOOGLESQL_RETURN_IF_ERROR(GetOffsetValue(params, context, &offset_value));
      GOOGLESQL_RET_CHECK(offset_value.type()->Equals(order_key->type()));

      if (order_key->is_descending()) {
        return GetOffsetPrecedingRangeBoundariesDesc(
            is_end_boundary, *schema_with_params, tuples_with_params,
            order_key_slot_idx, offset_value, order_key->null_order(),
            window_boundaries);
      } else {
        return GetOffsetPrecedingRangeBoundariesAsc(
            is_end_boundary, *schema_with_params, tuples_with_params,
            order_key_slot_idx, offset_value, order_key->null_order(),
            window_boundaries);
      }
    }
    case kOffsetFollowing: {
      GOOGLESQL_RET_CHECK_EQ(order_keys.size(), 1);
      const KeyArg* order_key = order_keys[0];
      GOOGLESQL_RET_CHECK_EQ(order_key_slot_idxs.size(), 1);
      const int order_key_slot_idx = order_key_slot_idxs[0];

      Value offset_value;
      GOOGLESQL_RETURN_IF_ERROR(GetOffsetValue(params, context, &offset_value));
      GOOGLESQL_RET_CHECK(offset_value.type()->Equals(order_key->type()));

      if (order_key->is_descending()) {
        return GetOffsetFollowingRangeBoundariesDesc(
            is_end_boundary, *schema_with_params, tuples_with_params,
            order_key_slot_idx, offset_value, order_key->null_order(),
            window_boundaries);
      } else {
        return GetOffsetFollowingRangeBoundariesAsc(
            is_end_boundary, *schema_with_params, tuples_with_params,
            order_key_slot_idx, offset_value, order_key->null_order(),
            window_boundaries);
      }
    }
  }
}

struct WindowFrameBoundaryArg::GroupBoundary {
  GroupBoundary(int start_tuple_id_in, int end_tuple_id_in, int boundary_in)
      : start_tuple_id(start_tuple_id_in),
        end_tuple_id(end_tuple_id_in),
        boundary(boundary_in) {}

  // The 0-based position of the first tuple in the group.
  int start_tuple_id;
  // The 0-based position of the last tuple in the group
  int end_tuple_id;
  // The position of the window boundary for the entire group.
  int boundary;
};

absl::Status WindowFrameBoundaryArg::SetGroupBoundaries(
    absl::Span<const GroupBoundary> group_boundaries,
    std::vector<int>* window_boundaries) const {
  for (const GroupBoundary& group_boundary : group_boundaries) {
    GOOGLESQL_RET_CHECK_GE(group_boundary.start_tuple_id, 0);
    GOOGLESQL_RET_CHECK_LT(group_boundary.end_tuple_id,
                 static_cast<int>(window_boundaries->size()));
    for (int tuple_id = group_boundary.start_tuple_id;
         tuple_id <= group_boundary.end_tuple_id; ++tuple_id) {
      (*window_boundaries)[tuple_id] = group_boundary.boundary;
    }
  }
  return absl::OkStatus();
}

absl::Status WindowFrameBoundaryArg::GetOffsetPrecedingRangeBoundariesAsc(
    bool is_end_boundary, const TupleSchema& schema,
    absl::Span<const TupleData* const> partition, int order_key_slot_idx,
    const Value& offset_value, KeyArg::NullOrder null_order,
    std::vector<int>* window_boundaries) const {
  // The partition is divided into 6 groups, depending on the null ordering
  // Nulls first:
  // --------------------------------------------------------------------
  // Group            Position                             Order key
  // ------------  -----------------------------     --------------------
  //  1(null)      [0, end_null]                             NULL
  //  2(nan)       (end_null, end_nan]                       NaN
  //  3(neg inf)   (end_nan, end_neg_inf]               negative infinity
  //  4(underflow) (end_neg_inf, start_safe_tuple)     [min, min + offset)
  //  5(safe)      [start_safe_tuple, start_pos_inf)   [min + offset, max]
  //  6(pos inf)   [start_pos_inf, partition size)      positive infinity
  // --------------------------------------------------------------------
  // Nulls last:
  // --------------------------------------------------------------------
  // Group            Position                             Order key
  // ------------  -----------------------------     --------------------
  //  1(nan)       [0, end_nan]                                NaN
  //  2(neg inf)   (end_nan, end_neg_inf]               negative infinity
  //  3(underflow) (end_neg_inf, start_safe_tuple)     [min, min + offset)
  //  4(safe)      [start_safe_tuple, start_pos_inf)   [min + offset, max]
  //  5(pos inf)   [start_pos_inf, start_null)         positive infinity
  //  6(null)      [start_null, partition size]                NULL
  // --------------------------------------------------------------------
  //
  // Case 1: The offset is not positive infinity, nulls first
  // --------------------------------------------------------------------
  // Group              Start window boundary        End window boundary
  // ------------      ----------------------       ---------------------
  //  1(null)                   0                         end_null
  //  2(nan)               end_null + 1                   end_nan
  //  3(neg inf)           end_nan + 1                   end_neg_inf
  //  4(underflow)       end_neg_inf + 1                 end_neg_inf
  //  5(safe)          $(order_key - offset)         ^(order_key - offset)
  //  6(pos inf)          start_pos_inf               partition size - 1
  // --------------------------------------------------------------------
  // Case 1: The offset is not positive infinity, nulls last
  // --------------------------------------------------------------------
  // Group              Start window boundary        End window boundary
  // ------------      ----------------------       ---------------------
  //  1(nan)                   0                           end_nan
  //  2(neg inf)           end_nan + 1                   end_neg_inf
  //  3(underflow)       end_neg_inf + 1                 end_neg_inf
  //  4(safe)          $(order_key - offset)         ^(order_key - offset)
  //  5(pos inf)          start_pos_inf                start_null - 1
  //  6(null)              start_null                 partition size - 1
  // --------------------------------------------------------------------
  // '$(order_key - offset)' refers to the position of the first tuple with
  // an order key >= (order_key - offset), while '^(
  // order_key - offset)' refers to the position of the last tuple with
  // an order key >= (order_key - offset).  'order_key' is the order key of
  // the row for which we are computing the window boundary.
  //
  // Case 2: The offset is positive infinity, nulls first
  // --------------------------------------------------------------------
  // Group              Start window boundary        End window boundary
  // ------------      ----------------------       ---------------------
  //  1(null)                   0                        end_null
  //  2(nan)               end_null + 1                  end_nan
  //  3(neg inf)           end_nan + 1                 end_neg_inf
  //  4(underflow)         end_nan + 1                 end_neg_inf
  //  5(safe)              end_nan + 1                 end_neg_inf
  //  6(pos inf)              ERROR                       ERROR
  // --------------------------------------------------------------------
  // Case 2: The offset is positive infinity, nulls last
  // --------------------------------------------------------------------
  // Group              Start window boundary        End window boundary
  // ------------      ----------------------       ---------------------
  //  1(nan)                    0                        end_nan
  //  2(neg inf)           end_nan + 1                 start_null - 1
  //  3(underflow)         end_nan + 1                 start_null - 1
  //  4(safe)              end_nan + 1                 start_null - 1
  //  5(pos inf)              ERROR                       ERROR
  //  6(null)               start_null              partition size - 1
  // --------------------------------------------------------------------
  // ERROR is reported for Group 6 with positive infinity, because
  // we want the boundary to move in one direction.

  window_boundaries->resize(partition.size());

  int end_null, end_nan, end_neg_inf, start_pos_inf, start_null;
  if (null_order != KeyArg::kNullsLast) {
    DivideAscendingPartition(schema, partition, order_key_slot_idx,
                             /*nulls_last=*/false, &end_null, &end_nan,
                             &end_neg_inf, &start_pos_inf, &start_null);
  } else {
    DivideAscendingPartition(schema, partition, order_key_slot_idx,
                             /*nulls_last=*/true, &end_null, &end_nan,
                             &end_neg_inf, &start_pos_inf, &start_null);
  }

  const int last_tuple_id = partition.size() - 1;
  if (IsPosInf(offset_value)) {
    // Check if Group 6 is empty.
    if ((null_order != KeyArg::kNullsLast &&
         start_pos_inf < partition.size()) ||
        (null_order == KeyArg::kNullsLast && start_pos_inf < start_null)) {
      return ::googlesql_base::OutOfRangeErrorBuilder()
             << "Offset value cannot be positive infinity when "
                "there exists a positive infinity order key "
                "for an offset PRECEDING on an ascending partition";
    }

    if (!is_end_boundary) {
      if (null_order != KeyArg::kNullsLast) {
        GOOGLESQL_RETURN_IF_ERROR(SetGroupBoundaries(
            {
                // {start_tuple_id, end_tuple_id, boundary}
                {0, end_null, 0},                           // Group 1
                {end_null + 1, end_nan, end_null + 1},      // Group 2
                {end_nan + 1, last_tuple_id, end_nan + 1},  // Group 3-5
            },
            window_boundaries));
      } else {
        GOOGLESQL_RETURN_IF_ERROR(SetGroupBoundaries(
            {
                // {start_tuple_id, end_tuple_id, boundary}
                {0, end_nan, 0},                             // Group 1
                {end_nan + 1, start_null - 1, end_nan + 1},  // Group 2-4
                {start_null, last_tuple_id, start_null},     // Group 6
            },
            window_boundaries));
      }
    } else {
      if (null_order != KeyArg::kNullsLast) {
        GOOGLESQL_RETURN_IF_ERROR(SetGroupBoundaries(
            {
                // {start_tuple_id, end_tuple_id, boundary}
                {0, end_null, end_null},                    // Group 1
                {end_null + 1, end_nan, end_nan},           // Group 2
                {end_nan + 1, last_tuple_id, end_neg_inf},  // Group 3-5
            },
            window_boundaries));
      } else {
        GOOGLESQL_RETURN_IF_ERROR(SetGroupBoundaries(
            {
                // {start_tuple_id, end_tuple_id, boundary}
                {0, end_nan, end_nan},                       // Group 1
                {end_nan + 1, end_neg_inf, end_neg_inf},     // Group 2-4
                {start_null, last_tuple_id, last_tuple_id},  // Group 6
            },
            window_boundaries));
      }
    }

    return absl::OkStatus();
  }

  // Find the first tuple with a "safe" order key not less than
  // (min + offset).
  const Value underflow_threshold = DoOperation<std::plus>(
      GetMinValue(offset_value.type_kind()), offset_value);
  int start_safe_tuple = end_neg_inf + 1;
  while (start_safe_tuple < start_pos_inf &&
         partition[start_safe_tuple]
             ->slot(order_key_slot_idx)
             .value()
             .LessThan(underflow_threshold)) {
    ++start_safe_tuple;
  }

  // Set boundaries for Group 5 with "safe" keys on which no overflow
  // is possible when computing the bound for a boundary.
  int boundary_tid = end_neg_inf + 1;
  for (int tuple_id = start_safe_tuple; tuple_id < start_pos_inf; ++tuple_id) {
    const Value bound_threshold = DoOperation<std::minus>(
        partition[tuple_id]->slot(order_key_slot_idx).value(), offset_value);
    // Find the first tuple with a key >= 'bound_threshold'.
    while (partition[boundary_tid]
               ->slot(order_key_slot_idx)
               .value()
               .LessThan(bound_threshold)) {
      ++boundary_tid;
    }
    if (is_end_boundary) {
      if (partition[boundary_tid]
              ->slot(order_key_slot_idx)
              .value()
              .Equals(bound_threshold)) {
        // Continue until reaching the beginning of the next ordering group.
        ++boundary_tid;
        while (boundary_tid < start_pos_inf && partition[boundary_tid]
                                                   ->slot(order_key_slot_idx)
                                                   .value()
                                                   .Equals(bound_threshold)) {
          ++boundary_tid;
        }
      }
      // We are at the beginning of the next ordering group, so we
      // need to move one tuple back.
      --boundary_tid;
    }

    (*window_boundaries)[tuple_id] = boundary_tid;

    // An end window boundary may precede Group 4.
    // Reset the boundary tuple id.
    if (boundary_tid < end_neg_inf + 1) {
      boundary_tid = end_neg_inf + 1;
    }
  }

  if (!is_end_boundary) {
    if (null_order != KeyArg::kNullsLast) {
      GOOGLESQL_RETURN_IF_ERROR(SetGroupBoundaries(
          {
              // {start_tuple_id, end_tuple_id, boundary}
              {0, end_null, 0},                         // Group 1
              {end_null + 1, end_nan, end_null + 1},    // Group 2
              {end_nan + 1, end_neg_inf, end_nan + 1},  // Group 3
              {end_neg_inf + 1, start_safe_tuple - 1,
               end_neg_inf + 1},                             // Group 4
              {start_pos_inf, last_tuple_id, start_pos_inf}  // Group 6
          },
          window_boundaries));
    } else {
      GOOGLESQL_RETURN_IF_ERROR(SetGroupBoundaries(
          {
              // {start_tuple_id, end_tuple_id, boundary}
              {0, end_nan, 0},                          // Group 1
              {end_nan + 1, end_neg_inf, end_nan + 1},  // Group 2
              {end_neg_inf + 1, start_safe_tuple - 1,
               end_neg_inf + 1},                               // Group 3
              {start_pos_inf, start_null - 1, start_pos_inf},  // Group 5
              {start_null, last_tuple_id, start_null}          // Group 6
          },
          window_boundaries));
    }
  } else {
    if (null_order != KeyArg::kNullsLast) {
      GOOGLESQL_RETURN_IF_ERROR(SetGroupBoundaries(
          {
              // {start_tuple_id, end_tuple_id, boundary}
              {0, end_null, end_null},                               // Group 1
              {end_null + 1, end_nan, end_nan},                      // Group 2
              {end_nan + 1, end_neg_inf, end_neg_inf},               // Group 3
              {end_neg_inf + 1, start_safe_tuple - 1, end_neg_inf},  // Group 4
              {start_pos_inf, last_tuple_id, last_tuple_id}          // Group 6
          },
          window_boundaries));
    } else {
      GOOGLESQL_RETURN_IF_ERROR(SetGroupBoundaries(
          {
              // {start_tuple_id, end_tuple_id, boundary}
              {0, end_nan, end_nan},                                 // Group 1
              {end_nan + 1, end_neg_inf, end_neg_inf},               // Group 2
              {end_neg_inf + 1, start_safe_tuple - 1, end_neg_inf},  // Group 3
              {start_pos_inf, start_null, start_null},               // Group 5
              {start_null, last_tuple_id, last_tuple_id}             // Group 6
          },
          window_boundaries));
    }
  }

  return absl::OkStatus();
}

absl::Status WindowFrameBoundaryArg::GetOffsetPrecedingRangeBoundariesDesc(
    bool is_end_boundary, const TupleSchema& schema,
    absl::Span<const TupleData* const> partition, int order_key_slot_idx,
    const Value& offset_value, KeyArg::NullOrder null_order,
    std::vector<int>* window_boundaries) const {
  // Nulls last
  // --------------------------------------------------------------------
  // Group            Position                             Order key
  // ------------  -----------------------------     --------------------
  //  1(pos inf)   [0, end_pos_inf]                    positive infinity
  //  2(overflow)  (end_pos_inf, start_safe_tuple]     (max - offset, max]
  //  3(safe)      [start_safe_tuple, start_neg_inf)   [min, max - offset]
  //  4(neg inf)   [start_neg_inf, start_nan)          negative infinity
  //  5(nan)       [start_nan, start_null)                   NaN
  //  6(null)      [start_null, partition size)              NULL
  // --------------------------------------------------------------------
  // Nulls first
  // --------------------------------------------------------------------
  // Group            Position                             Order key
  // ------------  -----------------------------     --------------------
  //  1(null)      [0, end_null]                             NULL
  //  2(pos inf)   (end_null, end_pos_inf]              positive infinity
  //  3(overflow)  (end_pos_inf, start_safe_tuple]     (max - offset, max]
  //  4(safe)      [start_safe_tuple, start_neg_inf)   [min, max - offset]
  //  5(neg inf)   [start_neg_inf, start_nan)          negative infinity
  //  6(nan)       [start_nan, partition size)               NaN
  // --------------------------------------------------------------------
  //
  // Case 1: The offset is not positive infinity, nulls last
  // --------------------------------------------------------------------
  // Group              Start window boundary        End window boundary
  // ------------      ----------------------       ---------------------
  //  1(pos inf)                0                       end_pos_inf
  //  2(overflow)        end_pos_inf + 1                end_pos_inf
  //  3(safe)          $(order_key + offset)        ^(order_key + offset)
  //  4(neg inf)           start_neg_inf               start_nan - 1
  //  5(nan)               start_nan                   start_null - 1
  //  6(null)              start_null                partition size - 1
  // --------------------------------------------------------------------
  // nulls first
  // --------------------------------------------------------------------
  // Group              Start window boundary        End window boundary
  // ------------      ----------------------       ---------------------
  //  1(null)                   0                         end_null
  //  2(pos inf)          end_null + 1                  end_pos_inf
  //  3(overflow)        end_pos_inf + 1                end_pos_inf
  //  4(safe)          $(order_key + offset)        ^(order_key + offset)
  //  5(neg inf)           start_neg_inf               start_nan - 1
  //  6(nan)               start_nan                partition size - 1
  // --------------------------------------------------------------------
  // '$(order_key + offset)' refers to the position of the first tuple
  // with an order key <= (order_key + offset), while '^(order_key + offset)'
  // refers to the position of the last tuple with an order
  // key <= (order_key + offset). 'order_key' is the order key of the row
  // for which we are computing the window boundary.
  //
  // Case 2: The offset is positive infinity, nulls last
  // --------------------------------------------------------------------
  // Group              Start window boundary        End window boundary
  // ------------      ----------------------       ---------------------
  //  1(pos inf)                0                        end_pos_inf
  //  2(overflow)               0                        end_pos_inf
  //  3(safe)                   0                        end_pos_inf
  //  4(neg inf)              ERROR                        ERROR
  //  5(nan)                start_nan                   start_null - 1
  //  6(null)               start_null               partition size - 1
  // -------------------------------------------------------------------
  // nulls first
  // --------------------------------------------------------------------
  // Group              Start window boundary        End window boundary
  // ------------      ----------------------       ---------------------
  //  1(null)                   0                         end_null
  //  2(pos inf)            end_null + 1                  end_pos_inf
  //  3(overflow)           end_null + 1                  end_pos_inf
  //  4(safe)               end_null + 1                  end_pos_inf
  //  5(neg inf)              ERROR                        ERROR
  //  6(nan)                start_nan               partition size - 1
  // -------------------------------------------------------------------
  // ERROR is reported for Group 4 with negative infinity, because
  // the boundary does not "precede" GROUP 4.

  window_boundaries->resize(partition.size());

  int end_null, end_pos_inf, start_neg_inf, start_nan, start_null;
  if (null_order != KeyArg::kNullsFirst) {
    DivideDescendingPartition(schema, partition, order_key_slot_idx,
                              /*nulls_last=*/true, &end_null, &end_pos_inf,
                              &start_neg_inf, &start_nan, &start_null);
  } else {
    DivideDescendingPartition(schema, partition, order_key_slot_idx,
                              /*nulls_last=*/false, &end_null, &end_pos_inf,
                              &start_neg_inf, &start_nan, &start_null);
  }

  const int last_tuple_id = partition.size() - 1;
  if (IsPosInf(offset_value)) {
    // Check if Group 4 is empty.
    if (start_neg_inf < start_nan) {
      return ::googlesql_base::OutOfRangeErrorBuilder()
             << "Offset value cannot be positive infinity when "
                "there exists a negative infinity order key "
                "for an offset PRECEDING on a descending partition";
    }

    if (!is_end_boundary) {
      if (null_order != KeyArg::kNullsFirst) {
        GOOGLESQL_RETURN_IF_ERROR(SetGroupBoundaries(
            {
                // {start_tuple_id, end_tuple_id, boundary}
                {0, start_neg_inf - 1, 0},               // Group 1-3
                {start_nan, start_null - 1, start_nan},  // Group 5
                {start_null, last_tuple_id, start_null}  // Group 6
            },
            window_boundaries));
      } else {
        GOOGLESQL_RETURN_IF_ERROR(SetGroupBoundaries(
            {
                // {start_tuple_id, end_tuple_id, boundary}
                {0, end_null, 0},                                 // Group 1
                {end_null + 1, start_neg_inf - 1, end_null + 1},  // Group 2-4
                {start_nan, last_tuple_id, start_nan},            // Group 6
            },
            window_boundaries));
      }
    } else {
      if (null_order != KeyArg::kNullsFirst) {
        GOOGLESQL_RETURN_IF_ERROR(SetGroupBoundaries(
            {
                // {start_tuple_id, end_tuple_id, boundary}
                {0, start_neg_inf - 1, end_pos_inf},          // Group 1-3
                {start_nan, start_null - 1, start_null - 1},  // Group 5
                {start_null, last_tuple_id, last_tuple_id}    // Group 6
            },
            window_boundaries));
      } else {
        GOOGLESQL_RETURN_IF_ERROR(SetGroupBoundaries(
            {
                // {start_tuple_id, end_tuple_id, boundary}
                {0, end_null, end_null},                         // Group 1
                {end_null + 1, start_neg_inf - 1, end_pos_inf},  // Group 2-4
                {start_nan, last_tuple_id, last_tuple_id},       // Group 6
            },
            window_boundaries));
      }
    }

    return absl::OkStatus();
  }

  // Find the position of the first tuple with a "safe" order
  // key < (max - offset).
  const Value overflow_threshold = DoOperation<std::minus>(
      GetMaxValue(offset_value.type_kind()), offset_value);
  int start_safe_tuple = end_pos_inf + 1;
  while (start_safe_tuple < start_neg_inf &&
         overflow_threshold.LessThan(
             partition[start_safe_tuple]->slot(order_key_slot_idx).value())) {
    ++start_safe_tuple;
  }

  // Set the boundaries for Group 3 with "safe" keys on which no overflow
  // is possible when computing the bound for a boundary.
  int boundary_tid = end_pos_inf + 1;
  for (int tuple_id = start_safe_tuple; tuple_id < start_neg_inf; ++tuple_id) {
    const Value bound_threshold = DoOperation<std::plus>(
        partition[tuple_id]->slot(order_key_slot_idx).value(), offset_value);
    // Find the first tuple with an order key <= 'bound_threshold'.
    while (bound_threshold.LessThan(
        partition[boundary_tid]->slot(order_key_slot_idx).value())) {
      ++boundary_tid;
    }
    if (is_end_boundary) {
      if (bound_threshold.Equals(
              partition[boundary_tid]->slot(order_key_slot_idx).value())) {
        // Continue until reaching the beginning of the next ordering group.
        ++boundary_tid;
        while (boundary_tid < start_neg_inf && partition[boundary_tid]
                                                   ->slot(order_key_slot_idx)
                                                   .value()
                                                   .Equals(bound_threshold)) {
          ++boundary_tid;
        }
      }
      // We are at the beginning of the next ordering group, so we
      // need to move one tuple back.
      --boundary_tid;
    }

    (*window_boundaries)[tuple_id] = boundary_tid;

    // An end window boundary may precede Group 2.
    // Reset the boundary tuple id.
    if (boundary_tid < end_pos_inf + 1) {
      boundary_tid = end_pos_inf + 1;
    }
  }

  if (!is_end_boundary) {
    if (null_order != KeyArg::kNullsFirst) {
      GOOGLESQL_RETURN_IF_ERROR(SetGroupBoundaries(
          {
              // {start_tuple_id, end_tuple_id, boundary}
              {0, end_pos_inf, 0},  // Group 1
              {end_pos_inf + 1, start_safe_tuple - 1,
               end_pos_inf + 1},                              // Group 2
              {start_neg_inf, start_nan - 1, start_neg_inf},  // Group 4
              {start_nan, start_null - 1, start_nan},         // Group 5
              {start_null, last_tuple_id, start_null}         // Group 6
          },
          window_boundaries));
    } else {
      GOOGLESQL_RETURN_IF_ERROR(SetGroupBoundaries(
          {
              // {start_tuple_id, end_tuple_id, boundary}
              {0, end_null, 0},                           // Group 1
              {end_null + 1, end_pos_inf, end_null + 1},  // Group 2
              {end_pos_inf + 1, start_safe_tuple - 1,
               end_pos_inf + 1},                              // Group 3
              {start_neg_inf, start_nan - 1, start_neg_inf},  // Group 5
              {start_nan, last_tuple_id, start_nan}           // Group 6
          },
          window_boundaries));
    }
  } else {
    if (null_order != KeyArg::kNullsFirst) {
      GOOGLESQL_RETURN_IF_ERROR(SetGroupBoundaries(
          {
              // {start_tuple_id, end_tuple_id, boundary}
              {0, end_pos_inf, end_pos_inf},                         // Group 1
              {end_pos_inf + 1, start_safe_tuple - 1, end_pos_inf},  // Group 2
              {start_neg_inf, start_nan - 1, start_nan - 1},         // Group 4
              {start_nan, start_null - 1, start_null - 1},           // Group 5
              {start_null, last_tuple_id, last_tuple_id}             // Group 6
          },
          window_boundaries));
    } else {
      GOOGLESQL_RETURN_IF_ERROR(SetGroupBoundaries(
          {
              // {start_tuple_id, end_tuple_id, boundary}
              {0, end_null, end_null},                               // Group 1
              {end_null + 1, end_pos_inf, end_pos_inf},              // Group 2
              {end_pos_inf + 1, start_safe_tuple - 1, end_pos_inf},  // Group 3
              {start_neg_inf, start_nan - 1, start_nan - 1},         // Group 5
              {start_nan, last_tuple_id, last_tuple_id},             // Group 6
          },
          window_boundaries));
    }
  }

  return absl::OkStatus();
}

absl::Status WindowFrameBoundaryArg::GetOffsetFollowingRangeBoundariesAsc(
    bool is_end_boundary, const TupleSchema& schema,
    absl::Span<const TupleData* const> partition, const int order_key_slot_idx,
    const Value& offset_value, KeyArg::NullOrder null_order,
    std::vector<int>* window_boundaries) const {
  // The partition is divided into 6 groups.
  // nulls first
  // --------------------------------------------------------------------
  // Group            Position                             Order key
  // ------------  -----------------------------     --------------------
  //  1(null)      [0, end_null]                             NULL
  //  2(nan)       (end_null, end_nan]                       NaN
  //  3(neg inf)   (end_nan, end_neg_inf]               negative infinity
  //  4(safe)      (end_neg_inf, end_safe_tuple]     [min, max - offset]
  //  5(overflow)  (end_safe_tuple, start_pos_inf)   (max - offset, max]
  //  6(pos inf)   [start_pos_inf, partition size)      positive infinity
  // --------------------------------------------------------------------
  // nulls last
  // --------------------------------------------------------------------
  // Group            Position                             Order key
  // ------------  -----------------------------     --------------------
  //  1(nan)       [0, end_nan]                             NaN
  //  2(neg inf)   (end_nan, end_neg_inf]               negative infinity
  //  3(safe)      (end_neg_inf, end_safe_tuple]     [min, max - offset]
  //  4(overflow)  (end_safe_tuple, start_pos_inf)   (max - offset, max]
  //  5(pos inf)   [start_pos_inf, start_null)      positive infinity
  //  6(null)      [start_null, partition size)             NULL
  // --------------------------------------------------------------------
  //
  // Case 1: The offset is not positive infinity, nulls first
  // --------------------------------------------------------------------
  // Group              Start window boundary        End window boundary
  // ------------      ----------------------       ---------------------
  //  1(null)                   0                         end_null
  //  2(nan)               end_null + 1                   end_nan
  //  3(neg inf)           end_nan + 1                  end_neg_inf
  //  4(safe)          $(order_key + offset)         ^(order_key + offset)
  //  5(overflow)         start_pos_inf               start_pos_inf - 1
  //  6(pos inf)          start_pos_inf               partition size - 1
  // --------------------------------------------------------------------
  // nulls last
  // --------------------------------------------------------------------
  // Group              Start window boundary        End window boundary
  // ------------      ----------------------       ---------------------
  //  1(nan)                    0                         end_nan
  //  2(neg inf)           end_nan + 1                  end_neg_inf
  //  3(safe)          $(order_key + offset)         ^(order_key + offset)
  //  4(overflow)         start_pos_inf               start_pos_inf - 1
  //  5(pos inf)          start_pos_inf               start_null - 1
  //  6(null)               start_null              partition size - 1
  // --------------------------------------------------------------------
  // '$(order_key + offset)' refers to the position of the first tuple with
  // an order key <= (order_key + offset), while '^(
  // order_key + offset)' refers to the position of the last tuple with
  // an order key <= (order_key + offset).  'order_key' is the order key of
  // the row for which we are computing the window boundary.
  //
  // Case 2: The offset is positive infinity, nulls first
  // --------------------------------------------------------------------
  // Group              Start window boundary        End window boundary
  // ------------      ----------------------       ---------------------
  //  1(null)                   0                        end_null
  //  2(nan)               end_null + 1                  end_nan
  //  3(neg inf)               ERROR                     ERROR
  //  4(safe)             start_pos_inf             partition size - 1
  //  5(overflow)         start_pos_inf             partition size - 1
  //  6(pos inf)          start_pos_inf             partition size - 1
  // --------------------------------------------------------------------
  // nulls last
  // --------------------------------------------------------------------
  // Group              Start window boundary        End window boundary
  // ------------      ----------------------       ---------------------
  //  1(nan)                    0                         end_nan
  //  2(neg inf)              ERROR                        ERROR
  //  3(safe)             start_pos_inf                 start_null - 1
  //  4(overflow)         start_pos_inf                 start_null - 1
  //  5(pos inf)          start_pos_inf                 start_null - 1
  //  6(null)              start_null               partition size - 1
  // --------------------------------------------------------------------
  // ERROR is reported for Group 3 with negative infinity, because
  // the boundary for Group 3 does not "follow" Group 3.

  window_boundaries->resize(partition.size());

  int end_null, end_nan, end_neg_inf, start_pos_inf, start_null;
  if (null_order != KeyArg::kNullsLast) {
    DivideAscendingPartition(schema, partition, order_key_slot_idx,
                             /*nulls_last=*/false, &end_null, &end_nan,
                             &end_neg_inf, &start_pos_inf, &start_null);
  } else {
    DivideAscendingPartition(schema, partition, order_key_slot_idx,
                             /*nulls_last=*/true, &end_null, &end_nan,
                             &end_neg_inf, &start_pos_inf, &start_null);
  }

  const int last_tuple_id = partition.size() - 1;
  if (IsPosInf(offset_value)) {
    // Check if Group 3 is empty.
    if (end_neg_inf > end_nan) {
      return ::googlesql_base::OutOfRangeErrorBuilder()
             << "Offset value cannot be positive infinity when "
                "there exists a negative infinity order key "
                "for an offset FOLLOWING on an ascending partition";
    }

    if (!is_end_boundary) {
      if (null_order != KeyArg::kNullsLast) {
        GOOGLESQL_RETURN_IF_ERROR(SetGroupBoundaries(
            {
                // {start_tuple_id, end_tuple_id, boundary}
                {0, end_null, 0},                            // Group 1
                {end_null + 1, end_nan, end_null + 1},       // Group 2
                {end_nan + 1, last_tuple_id, start_pos_inf}  // Group 4-6
            },
            window_boundaries));
      } else {
        GOOGLESQL_RETURN_IF_ERROR(SetGroupBoundaries(
            {
                // {start_tuple_id, end_tuple_id, boundary}
                {0, end_nan, 0},                               // Group 1
                {end_nan + 1, start_null - 1, start_pos_inf},  // Group 3-5
                {start_null, last_tuple_id, start_null},       // Group 6
            },
            window_boundaries));
      }
    } else {
      if (null_order != KeyArg::kNullsLast) {
        GOOGLESQL_RETURN_IF_ERROR(SetGroupBoundaries(
            {
                // {start_tuple_id, end_tuple_id, boundary}
                {0, end_null, end_null},                     // Group 1
                {end_null + 1, end_nan, end_nan},            // Group 2
                {end_nan + 1, last_tuple_id, last_tuple_id}  // Group 4-6
            },
            window_boundaries));
      } else {
        GOOGLESQL_RETURN_IF_ERROR(SetGroupBoundaries(
            {
                // {start_tuple_id, end_tuple_id, boundary}
                {0, end_nan, end_nan},                          // Group 1
                {end_nan + 1, start_null - 1, start_null - 1},  // Group 3-5
                {start_null, last_tuple_id, last_tuple_id},     // Group 6
            },
            window_boundaries));
      }
    }

    return absl::OkStatus();
  }

  // Find the position of the last tuple with a safe key.
  const Value overflow_threshold = DoOperation<std::minus>(
      GetMaxValue(offset_value.type_kind()), offset_value);
  int end_safe_tuple = start_pos_inf - 1;
  while (end_safe_tuple > end_neg_inf &&
         overflow_threshold.LessThan(
             partition[end_safe_tuple]->slot(order_key_slot_idx).value())) {
    --end_safe_tuple;
  }

  // Set window boundaries for Group 4.
  int boundary_tid = end_neg_inf + 1;
  for (int tuple_id = boundary_tid; tuple_id <= end_safe_tuple; ++tuple_id) {
    // No overflow is possible for Group 4.
    const Value bound_threshold = DoOperation<std::plus>(
        partition[tuple_id]->slot(order_key_slot_idx).value(), offset_value);
    // Find the first following tuple with an order key >= 'bound_threshold'.
    while (boundary_tid < start_pos_inf && partition[boundary_tid]
                                               ->slot(order_key_slot_idx)
                                               .value()
                                               .LessThan(bound_threshold)) {
      ++boundary_tid;
    }
    if (is_end_boundary) {
      if (boundary_tid < start_pos_inf && partition[boundary_tid]
                                              ->slot(order_key_slot_idx)
                                              .value()
                                              .Equals(bound_threshold)) {
        // Continue until reaching the beginning of the next ordering group.
        ++boundary_tid;
        while (boundary_tid < start_pos_inf && partition[boundary_tid]
                                                   ->slot(order_key_slot_idx)
                                                   .value()
                                                   .Equals(bound_threshold)) {
          ++boundary_tid;
        }
      }
      // We are at the beginning of the next ordering group, so we
      // need to move one tuple back.
      --boundary_tid;
    }

    (*window_boundaries)[tuple_id] = boundary_tid;
  }

  if (!is_end_boundary) {
    if (null_order != KeyArg::kNullsLast) {
      GOOGLESQL_RETURN_IF_ERROR(SetGroupBoundaries(
          {
              // {start_tuple_id, end_tuple_id, boundary}
              {0, end_null, 0},                         // Group 1
              {end_null + 1, end_nan, end_null + 1},    // Group 2
              {end_nan + 1, end_neg_inf, end_nan + 1},  // Group 3
              {end_safe_tuple + 1, start_pos_inf - 1,
               start_pos_inf},                               // Group 4
              {start_pos_inf, last_tuple_id, start_pos_inf}  // Group 6
          },
          window_boundaries));
    } else {
      GOOGLESQL_RETURN_IF_ERROR(SetGroupBoundaries(
          {
              // {start_tuple_id, end_tuple_id, boundary}
              {0, end_nan, 0},                          // Group 1
              {end_nan + 1, end_neg_inf, end_nan + 1},  // Group 2
              {end_safe_tuple + 1, start_pos_inf - 1,
               start_pos_inf},                                 // Group 4
              {start_pos_inf, start_null - 1, start_pos_inf},  // Group 5
              {start_null, last_tuple_id, start_null}          // Group 6
          },
          window_boundaries));
    }
  } else {
    if (null_order != KeyArg::kNullsLast) {
      GOOGLESQL_RETURN_IF_ERROR(SetGroupBoundaries(
          {
              // {start_tuple_id, end_tuple_id, boundary}
              {0, end_null, end_null},                  // Group 1
              {end_null + 1, end_nan, end_nan},         // Group 2
              {end_nan + 1, end_neg_inf, end_neg_inf},  // Group 3
              {end_safe_tuple + 1, start_pos_inf - 1,
               start_pos_inf - 1},                           // Group 4
              {start_pos_inf, last_tuple_id, last_tuple_id}  // Group 6
          },
          window_boundaries));
    } else {
      GOOGLESQL_RETURN_IF_ERROR(SetGroupBoundaries(
          {
              // {start_tuple_id, end_tuple_id, boundary}
              {0, end_nan, end_nan},                    // Group 1
              {end_nan + 1, end_neg_inf, end_neg_inf},  // Group 2
              {end_safe_tuple + 1, start_pos_inf - 1,
               start_pos_inf - 1},                              // Group 4
              {start_pos_inf, start_null - 1, start_null - 1},  // Group 5
              {start_null, last_tuple_id, last_tuple_id}        // Group 6
          },
          window_boundaries));
    }
  }

  return absl::OkStatus();
}

absl::Status WindowFrameBoundaryArg::GetOffsetFollowingRangeBoundariesDesc(
    bool is_end_boundary, const TupleSchema& schema,
    absl::Span<const TupleData* const> partition, int order_key_slot_idx,
    const Value& offset_value, KeyArg::NullOrder null_order,
    std::vector<int>* window_boundaries) const {
  // The partition is divided into 6 groups. nulls last
  // --------------------------------------------------------------------
  //     Group               Position                     Order key
  // ------------  -----------------------------     --------------------
  //  1(pos inf)   [0, end_pos_inf]                    positive infinity
  //  2(safe)      (end_pos_inf, end_safe_tuple]      [min + offset, max]
  //  3(underflow) (end_safe_tuple, start_neg_inf)    [min, min + offset)
  //  4(neg inf)   [start_neg_inf, start_nan)          negative infinity
  //  5(NaN)       [start_nan, start_null)                  NaN
  //  6(NULL)      [start_null, partition size)             NULL
  // ----------------------------------------------------------------------
  // nulls first
  // --------------------------------------------------------------------
  //     Group               Position                     Order key
  // ------------  -----------------------------     --------------------
  //  1(NULL)      [0, end_null]                              NULL
  //  2(pos inf)   (end_null, end_pos_inf]              positive infinity
  //  3(safe)      (end_pos_inf, end_safe_tuple]      [min + offset, max]
  //  4(underflow) (end_safe_tuple, start_neg_inf)    [min, min + offset)
  //  5(neg inf)   [start_neg_inf, start_nan)          negative infinity
  //  6(NaN)       [start_nan, partition size)                NaN
  // ----------------------------------------------------------------------
  //
  // Case 1: The offset is not positive infinity, nulls last
  // --------------------------------------------------------------------
  // Group              Start window boundary        End window boundary
  // ------------      ----------------------       ---------------------
  //  1(pos inf)                0                        end_pos_inf
  //  2(safe)          $(order_key - offset)        ^(order_key - offset)
  //  3(underflow)        start_neg_inf                start_neg_inf - 1
  //  4(neg inf)          start_neg_inf                 start_nan - 1
  //  5(NaN)               start_nan                   start_null - 1
  //  6(NULL)              start_null                partition size - 1
  // --------------------------------------------------------------------
  // nulls first
  // --------------------------------------------------------------------
  // Group              Start window boundary        End window boundary
  // ------------      ----------------------       ---------------------
  //  1(NULL)                   0                         end_null
  //  2(pos inf)            end_null + 1                end_pos_inf
  //  3(safe)          $(order_key - offset)        ^(order_key - offset)
  //  4(underflow)        start_neg_inf                start_neg_inf - 1
  //  5(neg inf)          start_neg_inf                 start_nan - 1
  //  6(NaN)               start_nan                  partition size - 1
  // --------------------------------------------------------------------
  // '$(order_key - offset)' refers to the position of the first tuple with
  // an order key <= (order_key - offset), while '^(order_key + offset)' refers
  // to the position of the last tuple with an order
  // key <= (order_key - offset).  'order_key' is the order key of the row for
  // which we are computing the window boundary.
  //
  // Case 2: The offset is positive infinity, nulls last
  // --------------------------------------------------------------------
  // Group              Start window boundary        End window boundary
  // ------------      ----------------------       ---------------------
  //  1(pos inf)               ERROR                        ERROR
  //  2(safe)              start_neg_inf                start_nan - 1
  //  3(underflow)         start_neg_inf                start_nan - 1
  //  4(neg inf)           start_neg_inf                start_nan - 1
  //  5(NaN)                start_nan                   start_null - 1
  //  6(NULL)               start_null                partition size - 1
  // --------------------------------------------------------------------
  // nulls first
  // --------------------------------------------------------------------
  // Group              Start window boundary        End window boundary
  // ------------      ----------------------       ---------------------
  //  1(NULL)                   0                         end_null
  //  2(pos inf)              ERROR                        ERROR
  //  3(safe)              start_neg_inf                start_nan - 1
  //  4(underflow)         start_neg_inf                start_nan - 1
  //  5(neg inf)           start_neg_inf                start_nan - 1
  //  6(NaN)                start_nan                 partition size - 1
  // --------------------------------------------------------------------
  // ERROR is reported for Group 1 with positive infinity, because
  // we want to make the boundaries move in one direction.

  window_boundaries->resize(partition.size());

  int end_null, end_pos_inf, start_neg_inf, start_nan, start_null;
  if (null_order != KeyArg::kNullsFirst) {
    DivideDescendingPartition(schema, partition, order_key_slot_idx,
                              /*nulls_last=*/true, &end_null, &end_pos_inf,
                              &start_neg_inf, &start_nan, &start_null);
  } else {
    DivideDescendingPartition(schema, partition, order_key_slot_idx,
                              /*nulls_last=*/false, &end_null, &end_pos_inf,
                              &start_neg_inf, &start_nan, &start_null);
  }

  const int last_tuple_id = partition.size() - 1;
  if (IsPosInf(offset_value)) {
    // Check if Group 1 is not empty.
    if ((null_order != KeyArg::kNullsFirst && end_pos_inf >= 0) ||
        (null_order == KeyArg::kNullsFirst && end_pos_inf > end_null)) {
      return ::googlesql_base::OutOfRangeErrorBuilder()
             << "Offset value cannot be positive infinity when "
                "there exists a positive infinity order key "
                "for an offset FOLLOWING on a descending partition";
    }

    if (!is_end_boundary) {
      if (null_order != KeyArg::kNullsFirst) {
        GOOGLESQL_RETURN_IF_ERROR(SetGroupBoundaries(
            {
                // {start_tuple_id, end_tuple_id, boundary}
                {0, start_nan - 1, start_neg_inf},       // Group 2-4
                {start_nan, start_null - 1, start_nan},  // Group 5
                {start_null, last_tuple_id, start_null}  // Group 6
            },
            window_boundaries));
      } else {
        GOOGLESQL_RETURN_IF_ERROR(SetGroupBoundaries(
            {
                // {start_tuple_id, end_tuple_id, boundary}
                {0, end_null, 0},                              // Group 1
                {end_null + 1, start_nan - 1, start_neg_inf},  // Group 3-5
                {start_nan, last_tuple_id, start_nan},         // Group 6
            },
            window_boundaries));
      }
    } else {
      if (null_order != KeyArg::kNullsFirst) {
        GOOGLESQL_RETURN_IF_ERROR(SetGroupBoundaries(
            {
                // {start_tuple_id, end_tuple_id, boundary}
                {0, start_nan - 1, start_nan - 1},            // Group 2-4
                {start_nan, start_null - 1, start_null - 1},  // Group 5
                {start_null, last_tuple_id, last_tuple_id}    // Group 6
            },
            window_boundaries));
      } else {
        GOOGLESQL_RETURN_IF_ERROR(SetGroupBoundaries(
            {
                // {start_tuple_id, end_tuple_id, boundary}
                {0, end_null, end_null},                       // Group 1
                {end_null + 1, start_nan - 1, start_nan - 1},  // Group 3-5
                {start_nan, last_tuple_id, last_tuple_id}      // Group 6
            },
            window_boundaries));
      }
    }

    return absl::OkStatus();
  }

  // Find the position of the last tuple with a safe key not less than
  // (min + offset) in backward.
  const Value underflow_threshold = DoOperation<std::plus>(
      GetMinValue(offset_value.type_kind()), offset_value);
  int end_safe_tuple = start_neg_inf - 1;
  while (end_safe_tuple > end_pos_inf && partition[end_safe_tuple]
                                             ->slot(order_key_slot_idx)
                                             .value()
                                             .LessThan(underflow_threshold)) {
    --end_safe_tuple;
  }

  // Set boundaries for Group 2.
  int boundary_tid = end_pos_inf + 1;
  for (int tuple_id = boundary_tid; tuple_id <= end_safe_tuple; ++tuple_id) {
    // No overflow is possible for Group 2.
    const Value bound_threshold = DoOperation<std::minus>(
        partition[tuple_id]->slot(order_key_slot_idx).value(), offset_value);
    // Find the first following tuple <= 'bound_threshold'.
    while (boundary_tid < start_neg_inf &&
           bound_threshold.LessThan(
               partition[boundary_tid]->slot(order_key_slot_idx).value())) {
      ++boundary_tid;
    }
    if (is_end_boundary) {
      if (boundary_tid < start_neg_inf &&
          bound_threshold.Equals(
              partition[boundary_tid]->slot(order_key_slot_idx).value())) {
        // Continue until reaching the beginning of the next ordering group.
        ++boundary_tid;
        while (boundary_tid < start_neg_inf &&
               bound_threshold.Equals(
                   partition[boundary_tid]->slot(order_key_slot_idx).value())) {
          ++boundary_tid;
        }
      }
      // We are at the beginning of the next ordering group, so we
      // need to move one tuple back.
      --boundary_tid;
    }

    (*window_boundaries)[tuple_id] = boundary_tid;
  }

  if (!is_end_boundary) {
    if (null_order != KeyArg::kNullsFirst) {
      GOOGLESQL_RETURN_IF_ERROR(SetGroupBoundaries(
          {
              // {start_tuple_id, end_tuple_id, boundary}
              {0, end_pos_inf, 0},  // Group 1
              {end_safe_tuple + 1, start_neg_inf - 1,
               start_neg_inf},                                // Group 3
              {start_neg_inf, start_nan - 1, start_neg_inf},  // Group 4
              {start_nan, start_null - 1, start_nan},         // Group 5
              {start_null, last_tuple_id, start_null}         // Group 6
          },
          window_boundaries));
    } else {
      GOOGLESQL_RETURN_IF_ERROR(SetGroupBoundaries(
          {
              // {start_tuple_id, end_tuple_id, boundary}
              {0, end_null, 0},                           // Group 1
              {end_null + 1, end_pos_inf, end_null + 1},  // Group 2
              {end_safe_tuple + 1, start_neg_inf - 1,
               start_neg_inf},                                // Group 3
              {start_neg_inf, start_nan - 1, start_neg_inf},  // Group 5
              {start_nan, last_tuple_id, start_nan}           // Group 6
          },
          window_boundaries));
    }
  } else {
    if (null_order != KeyArg::kNullsFirst) {
      GOOGLESQL_RETURN_IF_ERROR(SetGroupBoundaries(
          {
              // {start_tuple_id, end_tuple_id, boundary}
              {0, end_pos_inf, end_pos_inf},  // Group 1
              {end_safe_tuple + 1, start_neg_inf - 1,
               start_neg_inf - 1},                            // Group 3
              {start_neg_inf, start_nan - 1, start_nan - 1},  // Group 4
              {start_nan, start_null - 1, start_null - 1},    // Group 5
              {start_null, last_tuple_id, last_tuple_id}      // Group 6
          },
          window_boundaries));
    } else {
      GOOGLESQL_RETURN_IF_ERROR(SetGroupBoundaries(
          {
              // {start_tuple_id, end_tuple_id, boundary}
              {0, end_null, end_null},                   // Group 1
              {end_null + 1, end_pos_inf, end_pos_inf},  // Group 2
              {end_safe_tuple + 1, start_neg_inf - 1,
               start_neg_inf - 1},                            // Group 3
              {start_neg_inf, start_nan - 1, start_nan - 1},  // Group 5
              {start_nan, last_tuple_id, last_tuple_id}       // Group 6
          },
          window_boundaries));
    }
  }

  return absl::OkStatus();
}

void WindowFrameBoundaryArg::DivideAscendingPartition(
    const TupleSchema& schema, absl::Span<const TupleData* const> partition,
    int order_key_slot_idx, bool nulls_last, int* end_null, int* end_nan,
    int* end_neg_inf, int* start_pos_inf, int* start_null) const {
  int tuple_id = 0;
  if (!nulls_last) {
    while (tuple_id < partition.size() &&
           partition[tuple_id]->slot(order_key_slot_idx).value().is_null()) {
      ++tuple_id;
    }
    *end_null = tuple_id - 1;
  }

  while (tuple_id < partition.size() &&
         IsNaN(partition[tuple_id]->slot(order_key_slot_idx).value())) {
    ++tuple_id;
  }
  *end_nan = tuple_id - 1;

  while (tuple_id < partition.size() &&
         IsNegInf(partition[tuple_id]->slot(order_key_slot_idx).value())) {
    ++tuple_id;
  }
  *end_neg_inf = tuple_id - 1;

  tuple_id = partition.size() - 1;
  if (nulls_last) {
    while (tuple_id > *end_neg_inf &&
           partition[tuple_id]->slot(order_key_slot_idx).value().is_null()) {
      --tuple_id;
    }
    *start_null = tuple_id + 1;
  }
  while (tuple_id > *end_neg_inf &&
         IsPosInf(partition[tuple_id]->slot(order_key_slot_idx).value())) {
    --tuple_id;
  }
  *start_pos_inf = tuple_id + 1;
}

void WindowFrameBoundaryArg::DivideDescendingPartition(
    const TupleSchema& schema, absl::Span<const TupleData* const> partition,
    int order_key_slot_idx, bool nulls_last, int* end_null_key,
    int* end_pos_inf, int* start_neg_inf, int* start_nan_key,
    int* start_null_key) const {
  int tuple_id = 0;
  if (!nulls_last) {
    while (tuple_id < partition.size() &&
           partition[tuple_id]->slot(order_key_slot_idx).value().is_null()) {
      ++tuple_id;
    }
    *end_null_key = tuple_id - 1;
  }

  while (tuple_id < partition.size() &&
         IsPosInf(partition[tuple_id]->slot(order_key_slot_idx).value())) {
    ++tuple_id;
  }
  *end_pos_inf = tuple_id - 1;

  tuple_id = partition.size() - 1;
  if (nulls_last) {
    while (tuple_id > *end_pos_inf &&
           partition[tuple_id]->slot(order_key_slot_idx).value().is_null()) {
      --tuple_id;
    }
    *start_null_key = tuple_id + 1;
  }

  while (tuple_id > *end_pos_inf &&
         IsNaN(partition[tuple_id]->slot(order_key_slot_idx).value())) {
    --tuple_id;
  }
  *start_nan_key = tuple_id + 1;

  while (tuple_id > *end_pos_inf &&
         IsNegInf(partition[tuple_id]->slot(order_key_slot_idx).value())) {
    --tuple_id;
  }
  *start_neg_inf = tuple_id + 1;
}

WindowFrameBoundaryArg::WindowFrameBoundaryArg(
    BoundaryType boundary_type, std::unique_ptr<ValueExpr> boundary_offset_expr)
    : AlgebraArg(VariableId(), /*node=*/nullptr),
      boundary_type_(boundary_type),
      boundary_offset_expr_(std::move(boundary_offset_expr)) {}

std::string WindowFrameBoundaryArg::GetBoundaryTypeString(
    BoundaryType boundary_type) {
  switch (boundary_type) {
    case kUnboundedPreceding:
      return "UNBOUNDED PRECEDING";
    case kOffsetPreceding:
      return "OFFSET PRECEDING";
    case kCurrentRow:
      return "CURRENT ROW";
    case kUnboundedFollowing:
      return "UNBOUNDED FOLLOWING";
    case kOffsetFollowing:
      return "OFFSET FOLLOWING";
  }
}

// -------------------------------------------------------
// WindowFrameArg
// -------------------------------------------------------

std::string WindowFrameArg::DebugInternal(const std::string& indent,
                                          bool verbose) const {
  std::string result("WindowFrame");
  const std::string frame_unit_str =
      (window_frame_type_ == kRows ? "ROWS" : "RANGE");
  absl::StrAppend(&result, "(", indent, "  frame_unit=", frame_unit_str);
  absl::StrAppend(&result, ",", indent, "  start=",
                  start_boundary_arg_->DebugInternal(indent + "  ", verbose));
  absl::StrAppend(&result, ",", indent, "  end=",
                  end_boundary_arg_->DebugInternal(indent + "  ", verbose));
  absl::StrAppend(&result, indent, ")");
  return result;
}

absl::Status WindowFrameArg::SetSchemasForEvaluation(
    absl::Span<const TupleSchema* const> params_schemas) {
  GOOGLESQL_RETURN_IF_ERROR(start_boundary_arg_->SetSchemasForEvaluation(params_schemas));
  GOOGLESQL_RETURN_IF_ERROR(end_boundary_arg_->SetSchemasForEvaluation(params_schemas));
  return absl::OkStatus();
}

absl::Status WindowFrameArg::GetWindows(
    const TupleSchema& schema, absl::Span<const TupleData* const> partition,
    absl::Span<const KeyArg* const> order_keys,
    absl::Span<const TupleData* const> params, EvaluationContext* context,
    std::vector<AnalyticWindow>* windows, bool* is_deterministic) const {
  std::vector<int> start_window_boundaries;
  std::vector<int> end_window_boundaries;
  if (window_frame_type_ == kRows) {
    GOOGLESQL_RETURN_IF_ERROR(start_boundary_arg_->GetRowsBasedWindowBoundaries(
        false /* is_end_boundary */, partition.size(), params, context,
        &start_window_boundaries));
    GOOGLESQL_RETURN_IF_ERROR(end_boundary_arg_->GetRowsBasedWindowBoundaries(
        true /* is_end_boundary */, partition.size(), params, context,
        &end_window_boundaries));

    // Find the slots for the keys and values.
    std::vector<int> order_key_slot_idxs;
    std::vector<int> value_slot_idxs;
    GOOGLESQL_RETURN_IF_ERROR(GetSlotsForKeysAndValues(
        schema, order_keys, &order_key_slot_idxs, &value_slot_idxs));

    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<TupleComparator> comparator,
                     TupleComparator::Create(order_keys, order_key_slot_idxs,
                                             params, context));
    // A ROWS-based window is deterministic if
    // a) the order within each partition is deterministic, or
    // b) the window is either BETWEEN UNBOUNDED AND UNBOUNDED or BETWEEN
    //    CURRENT ROW AND CURRENT ROW, or
    // c) the window is statically empty.
    bool window_frame_is_empty;
    GOOGLESQL_RETURN_IF_ERROR(IsStaticallyEmpty(params, partition.size(), context,
                                      &window_frame_is_empty));
    if ((start_boundary_arg_->IsUnbounded() &&
         end_boundary_arg_->IsUnbounded()) ||
        (start_boundary_arg_->IsCurrentRow() &&
         end_boundary_arg_->IsCurrentRow()) ||
        window_frame_is_empty ||
        comparator->IsUniquelyOrdered(partition, value_slot_idxs)) {
      *is_deterministic = true;
    } else {
      *is_deterministic = false;
    }
  } else {
    GOOGLESQL_RETURN_IF_ERROR(start_boundary_arg_->GetRangeBasedWindowBoundaries(
        false /* is_end_boundary */, schema, partition, order_keys, params,
        context, &start_window_boundaries));
    GOOGLESQL_RETURN_IF_ERROR(end_boundary_arg_->GetRangeBasedWindowBoundaries(
        true /* is_end_boundary */, schema, partition, order_keys, params,
        context, &end_window_boundaries));

    // A RANGE-based window is deterministic.
    *is_deterministic = true;
  }

  GOOGLESQL_RET_CHECK_EQ(start_window_boundaries.size(), end_window_boundaries.size());
  GOOGLESQL_RET_CHECK_EQ(partition.size(), start_window_boundaries.size());

  for (int tuple_id = 0; tuple_id < partition.size(); ++tuple_id) {
    const int window_size =
        end_window_boundaries[tuple_id] - start_window_boundaries[tuple_id] + 1;
    if (window_size <= 0) {
      windows->emplace_back();
    } else {
      windows->emplace_back(start_window_boundaries[tuple_id], window_size);
    }
  }

  return absl::OkStatus();
}

absl::Status WindowFrameArg::IsStaticallyEmpty(
    absl::Span<const TupleData* const> params, int partition_size,
    EvaluationContext* context, bool* is_empty) const {
  if (window_frame_type_ != WindowFrameType::kRows) {
    *is_empty = false;
    return absl::OkStatus();
  }

  if (end_boundary_arg_->boundary_type() ==
      WindowFrameBoundaryArg::kOffsetPreceding) {
    Value end_offset_value;
    GOOGLESQL_RETURN_IF_ERROR(
        end_boundary_arg_->GetOffsetValue(params, context, &end_offset_value));

    // The window frame is empty if the end boundary precedes the first
    // tuple.
    GOOGLESQL_RET_CHECK(end_offset_value.type()->IsInt64());
    if (end_offset_value.int64_value() >= partition_size) {
      *is_empty = true;
      return absl::OkStatus();
    }

    // The window frame ROWS BETWEEN <m> PRECEDING AND <n> PRECEDING with
    // <m> less than <n> is statically empty, because the start boundary
    // does not precede the end boundary.
    if (start_boundary_arg_->boundary_type() ==
        WindowFrameBoundaryArg::kOffsetPreceding) {
      Value start_offset_value;
      GOOGLESQL_RETURN_IF_ERROR(start_boundary_arg_->GetOffsetValue(params, context,
                                                          &start_offset_value));
      *is_empty = start_offset_value.LessThan(end_offset_value);
      return absl::OkStatus();
    }
  }

  if (start_boundary_arg_->boundary_type() ==
      WindowFrameBoundaryArg::kOffsetFollowing) {
    Value start_offset_value;
    GOOGLESQL_RETURN_IF_ERROR(start_boundary_arg_->GetOffsetValue(params, context,
                                                        &start_offset_value));
    // The window frame is empty if the start boundary follows the last
    // tuple.
    GOOGLESQL_RET_CHECK(start_offset_value.type()->IsInt64());
    if (start_offset_value.int64_value() >= partition_size) {
      *is_empty = true;
      return absl::OkStatus();
    }

    // The window frame ROWS BETWEEN <m> FOLLOWING AND <n> FOLLOWING with
    // <m> greater than <n> is statically empty, because the start boundary
    // does not precede the end boundary.
    if (end_boundary_arg_->boundary_type() ==
        WindowFrameBoundaryArg::kOffsetFollowing) {
      Value end_offset_value;
      GOOGLESQL_RETURN_IF_ERROR(end_boundary_arg_->GetOffsetValue(params, context,
                                                        &end_offset_value));
      *is_empty = end_offset_value.LessThan(start_offset_value);
      return absl::OkStatus();
    }
  }

  *is_empty = false;
  return absl::OkStatus();
}

// -------------------------------------------------------
// AggregateAnalyticArg
// -------------------------------------------------------

absl::Status AggregateAnalyticArg::SetSchemasForEvaluation(
    const TupleSchema& partition_schema,
    absl::Span<const TupleSchema* const> params_schemas) {
  GOOGLESQL_RET_CHECK(window_frame_ != nullptr);
  GOOGLESQL_RETURN_IF_ERROR(window_frame_->SetSchemasForEvaluation(params_schemas));
  GOOGLESQL_RETURN_IF_ERROR(
      aggregator_->SetSchemasForEvaluation(partition_schema, params_schemas));
  partition_schema_ =
      std::make_unique<const TupleSchema>(partition_schema.variables());
  return absl::OkStatus();
}

absl::Status AggregateAnalyticArg::Eval(
    absl::Span<const TupleData* const> partition,
    absl::Span<const KeyArg* const> order_keys,
    absl::Span<const TupleData* const> params, EvaluationContext* context,
    std::vector<Value>* values) const {
  GOOGLESQL_RET_CHECK(values->empty());

  // Compute window frames for all tuples in the partition.
  std::vector<AnalyticWindow> windows;
  bool window_frame_is_deterministic;
  GOOGLESQL_RETURN_IF_ERROR(window_frame_->GetWindows(
      *partition_schema_, partition, order_keys, params, context, &windows,
      &window_frame_is_deterministic));

  uint64_t num_windows = 0;
  for (const AnalyticWindow& window : windows) {
    // Call AggregateArg::EvalAgg to evaluate the argument expressions and
    // compute the aggregate on each window.
    const absl::Span<const TupleData* const> window_tuples =
        partition.subspan(window.start_tuple_id, window.num_tuples);
    GOOGLESQL_ASSIGN_OR_RETURN(const Value agg_value,
                     aggregator_->EvalAgg(window_tuples, params, context));
    values->emplace_back(agg_value);
    GOOGLESQL_RETURN_IF_ERROR(PeriodicallyVerifyNotAborted(context, ++num_windows));
  }

  // We conservatively treat aggregation results as non-deterministic
  // if the windows are not deterministic.
  if (!window_frame_is_deterministic) {
    context->SetNonDeterministicOutput();
  }
  return absl::OkStatus();
}

std::string AggregateAnalyticArg::DebugInternal(const std::string& indent,
                                                bool verbose) const {
  return absl::StrCat("AggregateAnalyticArg(",
                      window_frame_->DebugInternal(indent, verbose), ", ",
                      aggregator_->DebugInternal(indent, verbose), ")");
}

// -------------------------------------------------------
// NonAggregateAnalyticArg
// -------------------------------------------------------

absl::StatusOr<std::unique_ptr<NonAggregateAnalyticArg>>
NonAggregateAnalyticArg::Create(
    const VariableId& variable_id, std::unique_ptr<WindowFrameArg> window_frame,
    std::unique_ptr<const AnalyticFunctionBody> function,
    std::vector<std::unique_ptr<ValueExpr>> non_const_arguments,
    std::vector<std::unique_ptr<ValueExpr>> const_arguments,
    ResolvedFunctionCallBase::ErrorMode error_mode) {
  const Type* function_output_type = function->output_type();
  GOOGLESQL_ASSIGN_OR_RETURN(auto function_call,
                   AnalyticFunctionCallExpr::Create(
                       std::move(function), std::move(non_const_arguments),
                       std::move(const_arguments)));
  return absl::WrapUnique(new NonAggregateAnalyticArg(
      variable_id, std::move(window_frame), function_output_type,
      std::move(function_call), error_mode));
}

absl::Status NonAggregateAnalyticArg::SetSchemasForEvaluation(
    const TupleSchema& partition_schema,
    absl::Span<const TupleSchema* const> params_schemas) {
  if (window_frame_ != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(window_frame_->SetSchemasForEvaluation(params_schemas));
  }
  for (ExprArg* arg : function_call_->mutable_non_const_arguments()) {
    GOOGLESQL_RETURN_IF_ERROR(arg->mutable_value_expr()->SetSchemasForEvaluation(
        ConcatSpans(params_schemas, {&partition_schema})));
  }
  for (ExprArg* arg : function_call_->mutable_const_arguments()) {
    GOOGLESQL_RETURN_IF_ERROR(
        arg->mutable_value_expr()->SetSchemasForEvaluation(params_schemas));
  }
  partition_schema_ =
      std::make_unique<const TupleSchema>(partition_schema.variables());
  return absl::OkStatus();
}

absl::Status NonAggregateAnalyticArg::Eval(
    absl::Span<const TupleData* const> partition,
    absl::Span<const KeyArg* const> order_keys,
    absl::Span<const TupleData* const> params, EvaluationContext* context,
    std::vector<Value>* values) const {
  GOOGLESQL_RET_CHECK(values->empty());

  // Evaluate non-constant argument expressions.
  std::vector<std::vector<Value>> arguments;
  for (const ExprArg* non_const_argument :
       function_call_->non_const_arguments()) {
    arguments.emplace_back();
    std::vector<Value>* argument_result = &arguments.back();
    for (const TupleData* tuple : partition) {
      TupleSlot slot;
      absl::Status status;
      if (!non_const_argument->value_expr()->EvalSimple(
              ConcatSpans(params, {tuple}), context, &slot, &status)) {
        return status;
      }
      argument_result->push_back(std::move(*slot.mutable_value()));
    }
  }

  // Evaluate constant argument expressions.
  for (const ExprArg* const_argument : function_call_->const_arguments()) {
    TupleSlot slot;
    absl::Status status;
    if (!const_argument->value_expr()->EvalSimple(params, context, &slot,
                                                  &status)) {
      return status;
    }

    arguments.emplace_back();
    arguments.back().push_back(std::move(*slot.mutable_value()));
  }

  // Create a TupleComparator if required.
  std::unique_ptr<TupleComparator> tuple_comparator;
  if (function_call_->function()->RequireTupleComparator()) {
    std::vector<int> order_key_slot_idxs;
    GOOGLESQL_RETURN_IF_ERROR(GetSlotsForKeysAndValues(*partition_schema_, order_keys,
                                             &order_key_slot_idxs,
                                             /*slots_for_values=*/nullptr));
    GOOGLESQL_ASSIGN_OR_RETURN(tuple_comparator,
                     TupleComparator::Create(order_keys, order_key_slot_idxs,
                                             params, context));
  }

  // Compute window frames for all tuples in the partition.
  std::vector<AnalyticWindow> windows;
  if (window_frame_ != nullptr) {
    bool windows_are_deterministic;
    GOOGLESQL_RETURN_IF_ERROR(window_frame_->GetWindows(
        *partition_schema_, partition, order_keys, params, context, &windows,
        &windows_are_deterministic));

    // We conservatively treat analytic function results as non-deterministic
    // if the windows are not deterministic.
    if (!windows_are_deterministic) {
      context->SetNonDeterministicOutput();
    }
  }

  // Evaluates the analytic function. It may set the output to be
  // non-deterministic, because an analytic function can be non-deterministic
  // even when the windows are deterministic or when the function does not
  // support windowing.
  return function_call_->function()->Eval(
      *partition_schema_, partition, arguments, windows, tuple_comparator.get(),
      error_mode_, context, values);
}

std::string NonAggregateAnalyticArg::DebugInternal(const std::string& indent,
                                                   bool verbose) const {
  std::string result("NonAggregateAnalyticArg(");
  if (window_frame_ != nullptr) {
    absl::StrAppend(&result, window_frame_->DebugInternal(indent, verbose),
                    ", ");
  }
  absl::StrAppend(&result, "$", variable().ToString());
  if (verbose) {
    absl::StrAppend(&result, "[", type()->DebugString(), "]");
  }
  absl::StrAppend(&result,
                  " := ", function_call_->DebugInternal(indent, verbose), ")");
  return result;
}

NonAggregateAnalyticArg::NonAggregateAnalyticArg(
    const VariableId& variable_id, std::unique_ptr<WindowFrameArg> window_frame,
    const Type* function_output_type,
    std::unique_ptr<AnalyticFunctionCallExpr> function_call,
    ResolvedFunctionCallBase::ErrorMode error_mode)
    : AnalyticArg(variable_id, function_output_type, std::move(window_frame),
                  error_mode),
      function_call_(std::move(function_call)) {}

// -------------------------------------------------------
// AnalyticOp
// -------------------------------------------------------

std::string AnalyticOp::GetIteratorDebugString(
    absl::string_view input_iter_debug_string) {
  return absl::StrCat("AnalyticTupleIterator(", input_iter_debug_string, ")");
}

absl::StatusOr<std::unique_ptr<AnalyticOp>> AnalyticOp::Create(
    std::vector<std::unique_ptr<KeyArg>> partition_keys,
    std::vector<std::unique_ptr<KeyArg>> order_keys,
    std::vector<std::unique_ptr<AnalyticArg>> analytic_args,
    std::unique_ptr<RelationalOp> input, bool preserves_order) {
  auto op = absl::WrapUnique(
      new AnalyticOp(std::move(partition_keys), std::move(order_keys),
                     std::move(analytic_args), std::move(input)));
  GOOGLESQL_RETURN_IF_ERROR(op->set_is_order_preserving(preserves_order));
  return op;
}

absl::Status AnalyticOp::SetSchemasForEvaluation(
    absl::Span<const TupleSchema* const> params_schemas) {
  GOOGLESQL_RETURN_IF_ERROR(mutable_input()->SetSchemasForEvaluation(params_schemas));
  const std::unique_ptr<const TupleSchema> input_schema =
      input()->CreateOutputSchema();

  for (KeyArg* key : mutable_partition_keys()) {
    GOOGLESQL_RETURN_IF_ERROR(key->mutable_value_expr()->SetSchemasForEvaluation(
        ConcatSpans(params_schemas, {input_schema.get()})));
  }

  for (KeyArg* key : mutable_order_keys()) {
    GOOGLESQL_RETURN_IF_ERROR(key->mutable_value_expr()->SetSchemasForEvaluation(
        ConcatSpans(params_schemas, {input_schema.get()})));

    ValueExpr* collation = key->mutable_collation();
    if (collation != nullptr) {
      GOOGLESQL_RETURN_IF_ERROR(collation->SetSchemasForEvaluation(params_schemas));
    }
  }

  for (AnalyticArg* arg : mutable_analytic_args()) {
    GOOGLESQL_RETURN_IF_ERROR(
        arg->SetSchemasForEvaluation(*input_schema, params_schemas));
  }

  return absl::OkStatus();
}

namespace {
// Partitions the tuples from 'input_iter' (which must have
// 'analytic_args.size()' extra slots by 'partition_keys'. Evaluates all of the
// 'analytic_args' on each partition and adds corresponding values to the
// tuples.
class AnalyticTupleIterator : public TupleIterator {
 public:
  AnalyticTupleIterator(absl::Span<const TupleData* const> params,
                        absl::Span<const KeyArg* const> partition_keys,
                        absl::Span<const KeyArg* const> order_keys,
                        absl::Span<const AnalyticArg* const> analytic_args,
                        std::unique_ptr<TupleIterator> input_iter,
                        std::unique_ptr<TupleComparator> partition_comparator,
                        std::unique_ptr<TupleSchema> output_schema,
                        EvaluationContext* context)
      : params_(params.begin(), params.end()),
        partition_keys_(partition_keys.begin(), partition_keys.end()),
        order_keys_(order_keys.begin(), order_keys.end()),
        analytic_args_(analytic_args.begin(), analytic_args.end()),
        input_iter_(std::move(input_iter)),
        partition_comparator_(std::move(partition_comparator)),
        output_schema_(std::move(output_schema)),
        remaining_current_partition_(context->memory_accountant()),
        context_(context) {}

  AnalyticTupleIterator(const AnalyticTupleIterator&) = delete;
  AnalyticTupleIterator& operator=(const AnalyticTupleIterator&) = delete;

  const TupleSchema& Schema() const override { return *output_schema_; }

  TupleData* Next() override {
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

    if (!remaining_current_partition_.IsEmpty()) {
      // We have loaded a partition and are consuming it.
      output_empty_ = false;
      current_ = remaining_current_partition_.PopFront();
      return current_.get();
    }

    if (is_last_partition_) {
      if (!output_empty_) {
        // Partitioning by a floating point type is a non-deterministic
        // operation unless the output is empty.
        for (const KeyArg* key : partition_keys_) {
          if (key->type()->IsFloatingPoint()) {
            context_->SetNonDeterministicOutput();
          }
        }
      }
      return nullptr;
    }

    // Load a new partition.
    std::unique_ptr<TupleData> first_tuple_in_current_partition;
    if (first_tuple_in_next_partition_ == nullptr) {
      // We are loading the first tuple of the first partition.
      const TupleData* input_data = input_iter_->Next();
      if (input_data == nullptr) {
        status_ = input_iter_->Status();
        return nullptr;
      }
      first_tuple_in_current_partition =
          std::make_unique<TupleData>(*input_data);
    } else {
      first_tuple_in_current_partition =
          std::move(first_tuple_in_next_partition_);
    }
    TupleData* first_tuple_in_current_partition_ptr =
        first_tuple_in_current_partition.get();
    if (!remaining_current_partition_.PushBack(
            std::move(first_tuple_in_current_partition), &status_)) {
      return nullptr;
    }

    // We have determined the first tuple of the next partition. Now load the
    // rest.
    while (true) {
      const TupleData* input_data = input_iter_->Next();
      if (input_data == nullptr) {
        status_ = input_iter_->Status();
        if (!status_.ok()) return nullptr;
        is_last_partition_ = true;
        break;
      }

      const bool comparator_equals =
          !(*partition_comparator_)(*first_tuple_in_current_partition_ptr,
                                    *input_data) &&
          !(*partition_comparator_)(*input_data,
                                    *first_tuple_in_current_partition_ptr);
      if (!comparator_equals) {
        // We are done loading the current partition. 'input_data' belongs in
        // the next partition.
        first_tuple_in_next_partition_ =
            std::make_unique<TupleData>(*input_data);
        break;
      }
      // 'input_data' belongs in the current partition (which we are still
      // loading).
      if (!remaining_current_partition_.PushBack(
              std::make_unique<TupleData>(*input_data), &status_)) {
        return nullptr;
      }
    }

    absl::Status status = PopulateAnalyticArgSlots();
    if (!status.ok()) {
      status_ = status;
      return nullptr;
    }

    output_empty_ = false;
    current_ = remaining_current_partition_.PopFront();
    return current_.get();
  }

  absl::Status Status() const override { return status_; }

  std::string DebugString() const override {
    return AnalyticOp::GetIteratorDebugString(input_iter_->DebugString());
  }

 private:
  // For each AnalyticArg in 'analytic_args', populates the corresponding slot
  // in all the rows in 'current_partition_'.
  absl::Status PopulateAnalyticArgSlots() {
    for (int arg_idx = 0; arg_idx < analytic_args_.size(); ++arg_idx) {
      const AnalyticArg* analytic_arg = analytic_args_[arg_idx];

      std::vector<const TupleData*> current_partition_ptrs =
          remaining_current_partition_.GetTuplePtrs();

      std::vector<Value> values;
      GOOGLESQL_RETURN_IF_ERROR(analytic_arg->Eval(current_partition_ptrs, order_keys_,
                                         params_, context_, &values));

      const int slot_idx = input_iter_->Schema().num_variables() + arg_idx;
      GOOGLESQL_RETURN_IF_ERROR(
          remaining_current_partition_.SetSlot(slot_idx, std::move(values)));
    }
    return absl::OkStatus();
  }

  const std::vector<const TupleData*> params_;
  const std::vector<const KeyArg*> partition_keys_;
  const std::vector<const KeyArg*> order_keys_;
  const std::vector<const AnalyticArg*> analytic_args_;
  std::unique_ptr<TupleIterator> input_iter_;
  std::unique_ptr<TupleComparator> partition_comparator_;
  std::unique_ptr<TupleSchema> output_schema_;
  // The last tuple returned. NULL if Next() has never been called.
  std::unique_ptr<TupleData> current_;
  // The partition we are currently consuming, augmented by the values
  // of the analytic arguments. Empty if Next() has never been called.
  TupleDataDeque remaining_current_partition_;
  // True if 'current_partition_' is the last one.
  bool is_last_partition_ = false;
  bool output_empty_ = true;
  // NULL if we haven't loaded any partitions yet or 'is_last_partition_' is
  // true.
  std::unique_ptr<TupleData> first_tuple_in_next_partition_;
  EvaluationContext* context_;
  absl::Status status_;
  int64_t num_next_calls_ = 0;
};
}  // namespace

absl::StatusOr<std::unique_ptr<TupleIterator>> AnalyticOp::CreateIterator(
    absl::Span<const TupleData* const> params, int num_extra_slots,
    EvaluationContext* context) const {
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<TupleIterator> iter,
      input()->CreateIterator(
          {params}, analytic_args().size() + num_extra_slots, context));

  std::vector<int> slots_for_partition_keys;
  slots_for_partition_keys.reserve(partition_keys().size());
  for (const KeyArg* partition_key : partition_keys()) {
    std::optional<int> slot =
        iter->Schema().FindIndexForVariable(partition_key->variable());
    GOOGLESQL_RET_CHECK(slot.has_value())
        << "Could not find variable " << partition_key->variable()
        << " in schema " << iter->Schema().DebugString();
    slots_for_partition_keys.push_back(slot.value());
  }

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<TupleComparator> partition_comparator,
      TupleComparator::Create(partition_keys(), slots_for_partition_keys,
                              params, context));

  iter = std::make_unique<AnalyticTupleIterator>(
      params, partition_keys(), order_keys(), analytic_args(), std::move(iter),
      std::move(partition_comparator), CreateOutputSchema(), context);
  if (is_order_preserving()) {
    return iter;
  } else {
    return MaybeReorder(std::move(iter), context);
  }
}

std::unique_ptr<TupleSchema> AnalyticOp::CreateOutputSchema() const {
  std::unique_ptr<TupleSchema> input_schema = input()->CreateOutputSchema();

  std::vector<VariableId> variables = input_schema->variables();
  variables.reserve(variables.size() + analytic_args().size());
  for (const AnalyticArg* arg : analytic_args()) {
    variables.push_back(arg->variable());
  }

  return std::make_unique<TupleSchema>(variables);
}

std::string AnalyticOp::IteratorDebugString() const {
  return GetIteratorDebugString(input()->IteratorDebugString());
}

std::string AnalyticOp::DebugInternal(const std::string& indent,
                                      bool verbose) const {
  return absl::StrCat(
      "AnalyticOp(",
      ArgDebugString({"partition_keys", "order_keys", "analytic_args", "input"},
                     {kN, kN, kN, k1}, indent, verbose),
      ")");
}

AnalyticOp::AnalyticOp(std::vector<std::unique_ptr<KeyArg>> partition_keys,
                       std::vector<std::unique_ptr<KeyArg>> order_keys,
                       std::vector<std::unique_ptr<AnalyticArg>> analytic_args,
                       std::unique_ptr<RelationalOp> input) {
  SetArgs<KeyArg>(kPartitionKey, std::move(partition_keys));
  SetArgs<KeyArg>(kOrderKey, std::move(order_keys));
  SetArgs<AnalyticArg>(kAnalytic, std::move(analytic_args));
  SetArg(kInput, std::make_unique<RelationalArg>(std::move(input)));
}

absl::Span<const KeyArg* const> AnalyticOp::partition_keys() const {
  return GetArgs<KeyArg>(kPartitionKey);
}

absl::Span<KeyArg* const> AnalyticOp::mutable_partition_keys() {
  return GetMutableArgs<KeyArg>(kPartitionKey);
}

absl::Span<const KeyArg* const> AnalyticOp::order_keys() const {
  return GetArgs<KeyArg>(kOrderKey);
}

absl::Span<KeyArg* const> AnalyticOp::mutable_order_keys() {
  return GetMutableArgs<KeyArg>(kOrderKey);
}

absl::Span<const AnalyticArg* const> AnalyticOp::analytic_args() const {
  return GetArgs<AnalyticArg>(kAnalytic);
}

absl::Span<AnalyticArg* const> AnalyticOp::mutable_analytic_args() {
  return GetMutableArgs<AnalyticArg>(kAnalytic);
}

const RelationalOp* AnalyticOp::input() const {
  return GetArg(kInput)->node()->AsRelationalOp();
}

RelationalOp* AnalyticOp::mutable_input() {
  return GetMutableArg(kInput)->mutable_node()->AsMutableRelationalOp();
}

}  // namespace googlesql
