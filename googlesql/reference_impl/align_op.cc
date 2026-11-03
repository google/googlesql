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

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/interval_value.h"
#include "googlesql/public/value.h"
#include "googlesql/reference_impl/evaluation.h"
#include "googlesql/reference_impl/operator.h"
#include "googlesql/reference_impl/tuple.h"
#include "googlesql/reference_impl/tuple_comparator.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "absl/container/btree_map.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "googlesql/base/status_macros.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"

namespace googlesql {

namespace {

// TODO: Replace timestamp +- ToDuration(interval) with
// functions::AddTimestamp to catch underflow/overflow.
absl::Duration ToDuration(const IntervalValue& interval) {
  return absl::Microseconds(interval.GetAsMicros()) +
         absl::Nanoseconds(interval.get_nano_fractions());
}

// Returns the earliest timestamp greater than or equal to `t` of the form
// `origin + N * period` for some integer `N`.
//
// 1. Computes the offset from origin: `delta = t - origin`.
// 2. Uses `absl::Ceil(delta, period)` to round `delta` up to the nearest
//    multiple of `period`.
// 3. Adds this rounded duration back to `origin`.
//
// Example:
//   If origin = 00:01:00 and period = 5m:
//   - For t = 00:04:00: delta = 3m -> Ceil(3m, 5m) = 5m -> returns 00:06:00.
//   - For t = 00:11:00: delta = 10m -> Ceil(10m, 5m) = 10m -> returns 00:11:00
//     (inclusive).
absl::Time NextAlignedTimestampInclusive(absl::Time t,
                                         const IntervalValue& period,
                                         absl::Time origin) {
  return origin + absl::Ceil(t - origin, ToDuration(period));
}

}  // namespace

absl::StatusOr<std::unique_ptr<WithinBoundExprArg>> WithinBoundExprArg::Create(
    ResolvedWithinBoundExpr::BoundKind bound_kind,
    std::unique_ptr<ValueExpr> expr) {
  return absl::WrapUnique(new WithinBoundExprArg(bound_kind, std::move(expr)));
}

WithinBoundExprArg::WithinBoundExprArg(
    ResolvedWithinBoundExpr::BoundKind bound_kind,
    std::unique_ptr<ValueExpr> expr)
    : AlgebraArg(VariableId(), std::move(expr)), bound_kind_(bound_kind) {}

std::string WithinBoundExprArg::DebugInternal(const std::string& indent,
                                              bool verbose) const {
  std::string str = ResolvedWithinBoundExpr::BoundKindToString(bound_kind_);
  if (has_value_expr()) {
    return absl::StrCat(str, "(", value_expr()->DebugString(verbose), ")");
  }
  return str;
}

absl::Status WithinBoundExprArg::SetSchemasForEvaluation(
    absl::Span<const TupleSchema* const> params_schemas) {
  if (has_value_expr()) {
    return mutable_value_expr()->SetSchemasForEvaluation(params_schemas);
  }
  return absl::OkStatus();
}

bool WithinBoundExprArg::IsAbsolute() const {
  return bound_kind_ == ResolvedWithinBoundExpr::UNBOUNDED_PRECEDING ||
         bound_kind_ == ResolvedWithinBoundExpr::UNBOUNDED_FOLLOWING ||
         bound_kind_ == ResolvedWithinBoundExpr::TIMESTAMP;
}

bool WithinBoundExprArg::IsRelative() const { return !IsAbsolute(); }

absl::StatusOr<absl::Time> WithinBoundExprArg::EvalAbsolute(
    absl::Span<const TupleData* const> params,
    EvaluationContext* context) const {
  if (!IsAbsolute()) {
    return absl::InternalError(
        absl::StrCat("EvalAbsolute called on non-absolute bound kind: ",
                     ResolvedWithinBoundExpr::BoundKindToString(bound_kind_)));
  }
  switch (bound_kind_) {
    case ResolvedWithinBoundExpr::UNBOUNDED_PRECEDING:
      return absl::InfinitePast();
    case ResolvedWithinBoundExpr::UNBOUNDED_FOLLOWING:
      return absl::InfiniteFuture();
    case ResolvedWithinBoundExpr::TIMESTAMP: {
      GOOGLESQL_RET_CHECK(has_value_expr());
      TupleSlot slot;
      absl::Status status;
      if (!value_expr()->EvalSimple(params, context, &slot, &status)) {
        return status;
      }
      const Value& val = slot.value();
      if (val.is_null()) {
        return absl::OutOfRangeError(
            "ALIGN bound expression evaluated to NULL");
      }
      GOOGLESQL_RET_CHECK(val.type()->IsTimestamp());
      return val.ToTime();
    }
    default:
      GOOGLESQL_RET_CHECK_FAIL() << "Unexpected bound kind: "
                       << ResolvedWithinBoundExpr::BoundKindToString(
                              bound_kind_);
  }
}

absl::StatusOr<IntervalValue> WithinBoundExprArg::EvalOffset(
    const IntervalValue& period, absl::Span<const TupleData* const> params,
    EvaluationContext* context) const {
  if (IsAbsolute() ||
      bound_kind_ == ResolvedWithinBoundExpr::ANCHOR_TIMESTAMP) {
    return IntervalValue();
  }

  GOOGLESQL_RET_CHECK(has_value_expr());
  TupleSlot slot;
  absl::Status status;
  if (!value_expr()->EvalSimple(params, context, &slot, &status)) {
    return status;
  }
  const Value& val = slot.value();
  if (val.is_null()) {
    return absl::OutOfRangeError("ALIGN bound expression evaluated to NULL");
  }

  switch (bound_kind_) {
    case ResolvedWithinBoundExpr::INTERVAL_PRECEDING:
    case ResolvedWithinBoundExpr::INTERVAL_FOLLOWING: {
      GOOGLESQL_RET_CHECK(val.type()->IsInterval());
      const IntervalValue& interval = val.interval_value();
      if (interval <= IntervalValue()) {
        return absl::OutOfRangeError("ALIGN bound interval must be positive");
      }
      return (bound_kind_ == ResolvedWithinBoundExpr::INTERVAL_PRECEDING)
                 ? -interval
                 : interval;
    }
    case ResolvedWithinBoundExpr::PERIOD_PRECEDING:
    case ResolvedWithinBoundExpr::PERIOD_FOLLOWING: {
      IntervalValue offset;
      if (val.type()->IsInteger()) {
        int64_t multiplier = val.ToInt64();
        if (multiplier <= 0) {
          return absl::OutOfRangeError(
              "ALIGN PERIOD multiplier must be positive");
        }
        GOOGLESQL_ASSIGN_OR_RETURN(offset, period * multiplier);
      } else if (val.type()->IsDouble()) {
        double multiplier = val.ToDouble();
        if (std::isnan(multiplier) || multiplier <= 0) {
          return absl::OutOfRangeError(
              "ALIGN PERIOD multiplier must be positive");
        }
        GOOGLESQL_ASSIGN_OR_RETURN(offset, period * multiplier);
      } else {
        return absl::OutOfRangeError(
            "PERIOD expression must be integer or double");
      }
      return (bound_kind_ == ResolvedWithinBoundExpr::PERIOD_PRECEDING)
                 ? -offset
                 : offset;
    }
    default:
      GOOGLESQL_RET_CHECK_FAIL() << "Unexpected bound kind: "
                       << ResolvedWithinBoundExpr::BoundKindToString(
                              bound_kind_);
  }
}

absl::StatusOr<absl::Time> WithinBoundExprArg::EvalRelative(
    absl::Time anchor_timestamp, const IntervalValue& period,
    absl::Span<const TupleData* const> params,
    EvaluationContext* context) const {
  if (!IsRelative()) {
    return absl::InternalError(
        absl::StrCat("EvalRelative called on non-relative bound kind: ",
                     ResolvedWithinBoundExpr::BoundKindToString(bound_kind_)));
  }

  GOOGLESQL_ASSIGN_OR_RETURN(IntervalValue offset, EvalOffset(period, params, context));
  return anchor_timestamp + ToDuration(offset);
}

// Evaluates any bound kind (absolute or relative) to an absl::Time,
// delegating to EvalAbsolute() for absolute bounds and EvalRelative() for
// relative bounds. Useful when the bound kind is not statically known.
absl::StatusOr<absl::Time> WithinBoundExprArg::Eval(
    absl::Time anchor_timestamp, const IntervalValue& period,
    absl::Span<const TupleData* const> params,
    EvaluationContext* context) const {
  if (IsAbsolute()) {
    return EvalAbsolute(params, context);
  }
  return EvalRelative(anchor_timestamp, period, params, context);
}

absl::StatusOr<std::unique_ptr<WithinBoundsArg>> WithinBoundsArg::Create(
    std::unique_ptr<WithinBoundExprArg> lower_bound,
    std::unique_ptr<WithinBoundExprArg> upper_bound) {
  return absl::WrapUnique(
      new WithinBoundsArg(std::move(lower_bound), std::move(upper_bound)));
}

WithinBoundsArg::WithinBoundsArg(
    std::unique_ptr<WithinBoundExprArg> lower_bound,
    std::unique_ptr<WithinBoundExprArg> upper_bound)
    : AlgebraArg(VariableId(), nullptr),
      lower_bound_(std::move(lower_bound)),
      upper_bound_(std::move(upper_bound)) {}

absl::Status WithinBoundsArg::SetSchemasForEvaluation(
    absl::Span<const TupleSchema* const> params_schemas) {
  GOOGLESQL_RETURN_IF_ERROR(lower_bound_->SetSchemasForEvaluation(params_schemas));
  GOOGLESQL_RETURN_IF_ERROR(upper_bound_->SetSchemasForEvaluation(params_schemas));
  return absl::OkStatus();
}

std::string WithinBoundsArg::DebugInternal(const std::string& indent,
                                           bool verbose) const {
  return absl::StrCat(
      "WITHIN ", "(lower_bound=", lower_bound_->DebugString(verbose),
      ", upper_bound=", upper_bound_->DebugString(verbose), ")");
}

EstimatorArg::EstimatorArg(std::unique_ptr<AggregateArg> aggregate_arg,
                           std::unique_ptr<WithinBoundsArg> within_bounds)
    : ExprArg(aggregate_arg->variable(), aggregate_arg->type()),
      aggregate_arg_(std::move(aggregate_arg)),
      within_bounds_(std::move(within_bounds)) {}

absl::Status EstimatorArg::SetSchemasForEvaluation(
    const TupleSchema& input_schema,
    absl::Span<const TupleSchema* const> params_schemas,
    const TupleSchema& aligned_timestamp_schema) {
  std::vector<const TupleSchema*> agg_params_schemas =
      ConcatSpans(params_schemas, {&aligned_timestamp_schema});
  GOOGLESQL_RETURN_IF_ERROR(aggregate_arg_->SetSchemasForEvaluation(
      input_schema, agg_params_schemas,
      /*grouping_keys_schema=*/nullptr));
  GOOGLESQL_RETURN_IF_ERROR(within_bounds_->SetSchemasForEvaluation(params_schemas));
  return absl::OkStatus();
}

std::string EstimatorArg::DebugInternal(const std::string& indent,
                                        bool verbose) const {
  return absl::StrCat(aggregate_arg_->DebugString(verbose), " ",
                      within_bounds_->DebugString(verbose));
}

absl::StatusOr<std::unique_ptr<AlignOp>> AlignOp::Create(
    std::unique_ptr<RelationalOp> input, const VariableId& timestamp_var,
    std::unique_ptr<ValueExpr> period, std::unique_ptr<ValueExpr> origin,
    std::unique_ptr<WithinBoundsArg> output_within,
    std::vector<std::unique_ptr<KeyArg>> partition_keys,
    const VariableId& aligned_timestamp_var,
    std::vector<std::unique_ptr<EstimatorArg>> estimators) {
  return absl::WrapUnique(new AlignOp(
      std::move(input), timestamp_var, std::move(period), std::move(origin),
      std::move(output_within), std::move(partition_keys),
      aligned_timestamp_var, std::move(estimators)));
}

AlignOp::AlignOp(std::unique_ptr<RelationalOp> input,
                 const VariableId& timestamp_var,
                 std::unique_ptr<ValueExpr> period,
                 std::unique_ptr<ValueExpr> origin,
                 std::unique_ptr<WithinBoundsArg> output_within,
                 std::vector<std::unique_ptr<KeyArg>> partition_keys,
                 const VariableId& aligned_timestamp_var,
                 std::vector<std::unique_ptr<EstimatorArg>> estimators)
    : timestamp_var_(timestamp_var),
      aligned_timestamp_var_(aligned_timestamp_var) {
  SetArg(kInput, std::make_unique<RelationalArg>(std::move(input)));
  SetArg(kPeriod, std::make_unique<ExprArg>(std::move(period)));
  if (origin != nullptr) {
    SetArg(kOrigin, std::make_unique<ExprArg>(std::move(origin)));
  }
  SetArg(kOutputWithin, std::move(output_within));
  SetArgs<KeyArg>(kPartitionKey, std::move(partition_keys));
  SetArgs<EstimatorArg>(kEstimator, std::move(estimators));
}

const RelationalOp* AlignOp::input() const {
  return GetArg(kInput)->relational_op();
}
RelationalOp* AlignOp::mutable_input() {
  return GetMutableArg(kInput)->mutable_relational_op();
}

const ValueExpr* AlignOp::period() const {
  return GetArg(kPeriod)->value_expr();
}
ValueExpr* AlignOp::mutable_period() {
  return GetMutableArg(kPeriod)->mutable_value_expr();
}

const ValueExpr* AlignOp::origin() const {
  const AlgebraArg* arg = GetArg(kOrigin);
  return arg ? arg->value_expr() : nullptr;
}
ValueExpr* AlignOp::mutable_origin() {
  AlgebraArg* arg = GetMutableArg(kOrigin);
  return arg ? arg->mutable_value_expr() : nullptr;
}

const WithinBoundsArg* AlignOp::output_within() const {
  return static_cast<const WithinBoundsArg*>(GetArg(kOutputWithin));
}
WithinBoundsArg* AlignOp::mutable_output_within() {
  return static_cast<WithinBoundsArg*>(GetMutableArg(kOutputWithin));
}

absl::Span<const KeyArg* const> AlignOp::partition_keys() const {
  return GetArgs<KeyArg>(kPartitionKey);
}
absl::Span<KeyArg* const> AlignOp::mutable_partition_keys() {
  return GetMutableArgs<KeyArg>(kPartitionKey);
}

absl::Span<const EstimatorArg* const> AlignOp::estimators() const {
  return GetArgs<EstimatorArg>(kEstimator);
}
absl::Span<EstimatorArg* const> AlignOp::mutable_estimators() {
  return GetMutableArgs<EstimatorArg>(kEstimator);
}

absl::Status AlignOp::SetSchemasForEvaluation(
    absl::Span<const TupleSchema* const> params_schemas) {
  GOOGLESQL_RETURN_IF_ERROR(mutable_input()->SetSchemasForEvaluation(params_schemas));
  GOOGLESQL_RETURN_IF_ERROR(mutable_period()->SetSchemasForEvaluation(params_schemas));
  if (mutable_origin() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(mutable_origin()->SetSchemasForEvaluation(params_schemas));
  }
  GOOGLESQL_RETURN_IF_ERROR(
      mutable_output_within()->SetSchemasForEvaluation(params_schemas));

  const std::unique_ptr<const TupleSchema> input_schema =
      input()->CreateOutputSchema();
  for (KeyArg* key : mutable_partition_keys()) {
    // Partition key expressions can reference variables from the outer query
    // parameters (params_schemas) and the input scan (input_schema).
    GOOGLESQL_RETURN_IF_ERROR(key->mutable_value_expr()->SetSchemasForEvaluation(
        ConcatSpans(params_schemas, {input_schema.get()})));
    if (key->collation_name() != nullptr) {
      GOOGLESQL_RETURN_IF_ERROR(key->mutable_collation_name()->SetSchemasForEvaluation(
          params_schemas));
    }
  }

  TupleSchema aligned_timestamp_schema({aligned_timestamp_var_});
  for (EstimatorArg* estimator : mutable_estimators()) {
    GOOGLESQL_RETURN_IF_ERROR(estimator->SetSchemasForEvaluation(
        *input_schema, params_schemas, aligned_timestamp_schema));
  }

  return absl::OkStatus();
}

std::unique_ptr<TupleSchema> AlignOp::CreateOutputSchema() const {
  std::vector<VariableId> variables;
  variables.reserve(partition_keys().size() + estimators().size() + 1);
  for (const KeyArg* key : partition_keys()) {
    variables.push_back(key->variable());
  }
  for (const auto* estimator : estimators()) {
    variables.push_back(estimator->aggregate_arg()->variable());
  }
  variables.push_back(aligned_timestamp_var_);
  return std::make_unique<TupleSchema>(variables);
}

std::string AlignOp::IteratorDebugString() const {
  return absl::StrCat("AlignIterator(", input()->IteratorDebugString(), ")");
}

std::string AlignOp::DebugInternal(const std::string& indent,
                                   bool verbose) const {
  return absl::StrCat(
      "AlignOp(",
      ArgDebugString({"input", "period", "origin", "output_within",
                      "partition_keys", "estimators"},
                     {k1, k1, kOpt, kOpt, kN, kN}, indent, verbose),
      ")");
}

namespace {

class AlignTupleIterator : public TupleIterator {
 public:
  AlignTupleIterator(absl::Span<const TupleData* const> params,
                     const VariableId& timestamp_var,
                     const ValueExpr* period_expr, const ValueExpr* origin_expr,
                     const WithinBoundsArg* output_within,
                     absl::Span<const KeyArg* const> partition_keys,
                     const VariableId& aligned_timestamp_var,
                     absl::Span<const EstimatorArg* const> estimators,
                     std::unique_ptr<TupleIterator> input_iter,
                     std::unique_ptr<TupleComparator> partition_comparator,
                     std::unique_ptr<TupleSchema> output_schema,
                     int num_extra_slots, EvaluationContext* context)
      : params_(params.begin(), params.end()),
        timestamp_var_(timestamp_var),
        period_expr_(period_expr),
        origin_expr_(origin_expr),
        output_within_(output_within),
        partition_keys_(partition_keys.begin(), partition_keys.end()),
        aligned_timestamp_var_(aligned_timestamp_var),
        estimators_(estimators.begin(), estimators.end()),
        input_iter_(std::move(input_iter)),
        partition_comparator_(std::move(partition_comparator)),
        output_schema_(std::move(output_schema)),
        buffered_outputs_(context->memory_accountant()),
        num_extra_slots_(num_extra_slots),
        context_(context) {
    timestamp_slot_ =
        input_iter_->Schema().FindIndexForVariable(timestamp_var_).value();
    partition_key_input_slots_.reserve(partition_keys_.size());
    for (const KeyArg* key : partition_keys_) {
      partition_key_input_slots_.push_back(
          input_iter_->Schema().FindIndexForVariable(key->variable()).value());
    }
  }

  const TupleSchema& Schema() const override { return *output_schema_; }

  TupleData* Next() override {
    status_ = Init();
    if (!status_.ok()) {
      return nullptr;
    }

    while (buffered_outputs_.IsEmpty() && !end_of_partitions_) {
      status_ = LoadAndProcessNextPartition();
      if (!status_.ok()) {
        return nullptr;
      }
    }

    if (!buffered_outputs_.IsEmpty()) {
      current_ = buffered_outputs_.PopFront();
      return current_.get();
    }

    return nullptr;
  }

  absl::Status Status() const override { return status_; }

  std::string DebugString() const override {
    return absl::StrCat("AlignIterator(", input_iter_->DebugString(), ")");
  }

 private:
  absl::Time GetRowTimestamp(const TupleData& row) const {
    const Value& val = row.slot(timestamp_slot_).value();
    return val.ToTime();
  }

  absl::Status Init() {
    if (initialized_) return absl::OkStatus();

    // Read all rows from the input iterator.
    std::vector<std::unique_ptr<TupleData>> all_rows;
    while (true) {
      const TupleData* input_data = input_iter_->Next();
      if (input_data == nullptr) {
        break;
      }
      const Value& val = input_data->slot(timestamp_slot_).value();
      if (val.is_null()) {
        return absl::OutOfRangeError(
            "Timestamp value in a time-series cannot be NULL");
      }
      GOOGLESQL_RET_CHECK(val.type()->IsTimestamp());
      all_rows.push_back(std::make_unique<TupleData>(*input_data));
    }
    GOOGLESQL_RETURN_IF_ERROR(input_iter_->Status());

    // Evaluate period.
    TupleSlot period_slot;
    absl::Status status;
    if (!period_expr_->EvalSimple(params_, context_, &period_slot, &status)) {
      return status;
    }
    const Value& period_val = period_slot.value();
    if (period_val.is_null()) {
      return absl::OutOfRangeError("ALIGN PERIOD evaluated to NULL");
    }
    GOOGLESQL_RET_CHECK(period_val.type()->IsInterval());
    period_ = period_val.interval_value();
    if (period_ <= IntervalValue()) {
      return absl::OutOfRangeError("ALIGN PERIOD must be positive");
    }

    // Evaluate OUTPUT WITHIN bounds.
    absl::Time current_timestamp =
        absl::FromUnixMicros(context_->GetCurrentTimestamp());
    GOOGLESQL_ASSIGN_OR_RETURN(output_lower_bound_,
                     output_within_->lower_bound()->Eval(
                         /*anchor_timestamp=*/current_timestamp, period_,
                         params_, context_));
    GOOGLESQL_ASSIGN_OR_RETURN(output_upper_bound_,
                     output_within_->upper_bound()->Eval(
                         /*anchor_timestamp=*/current_timestamp, period_,
                         params_, context_));
    if (output_lower_bound_ >= output_upper_bound_) {
      return absl::OutOfRangeError(absl::StrCat(
          "OUTPUT WITHIN range is empty: ", output_within_->DebugString()));
    }

    // Evaluate origin. The origin is:
    // 1. The value of timestamp_expr, if the clause is ORIGIN timestamp_expr.
    // 2. The upper bound (implicit or explicit) of the OUTPUT WITHIN clause, if
    // finite.
    // 3. The lower bound (implicit or explicit) of the OUTPUT WITHIN clause, if
    // finite.
    // 4. The GoogleSQL Epoch.
    if (origin_expr_ != nullptr) {
      TupleSlot origin_slot;
      if (!origin_expr_->EvalSimple(params_, context_, &origin_slot, &status)) {
        return status;
      }
      const Value& origin_val = origin_slot.value();
      if (origin_val.is_null()) {
        return absl::OutOfRangeError("ALIGN ORIGIN evaluated to NULL");
      }
      GOOGLESQL_RET_CHECK(origin_val.type()->IsTimestamp());
      origin_ = origin_val.ToTime();
    } else if (output_upper_bound_ != absl::InfiniteFuture()) {
      origin_ = output_upper_bound_;
    } else if (output_lower_bound_ != absl::InfinitePast()) {
      origin_ = output_lower_bound_;
    } else {
      origin_ = absl::UnixEpoch();
    }

    // Partition the input rows using partition_comparator_.
    struct MapComp {
      const TupleComparator* comp;
      bool operator()(const TupleData* a, const TupleData* b) const {
        return (*comp)(a, b);
      }
    };
    MapComp comp{partition_comparator_.get()};
    absl::btree_map<const TupleData*, std::vector<std::unique_ptr<TupleData>>,
                    MapComp>
        grouped_rows(comp);
    for (auto& row : all_rows) {
      const TupleData* key = row.get();
      grouped_rows[key].push_back(std::move(row));
    }
    partitions_.reserve(grouped_rows.size());
    for (auto& [key, rows] : grouped_rows) {
      partitions_.push_back(std::move(rows));
    }
    grouped_rows.clear();

    // Sort each partition by timestamp and check for duplicates.
    for (auto& partition : partitions_) {
      std::sort(partition.begin(), partition.end(),
                [this](const std::unique_ptr<TupleData>& a,
                       const std::unique_ptr<TupleData>& b) {
                  return GetRowTimestamp(*a) < GetRowTimestamp(*b);
                });

      auto duplicate_it = std::adjacent_find(
          partition.begin(), partition.end(),
          [this](const std::unique_ptr<TupleData>& a,
                 const std::unique_ptr<TupleData>& b) {
            return GetRowTimestamp(*a) == GetRowTimestamp(*b);
          });
      if (duplicate_it != partition.end()) {
        return absl::OutOfRangeError(
            "Timestamp values must be unique within a partition");
      }
    }

    current_partition_idx_ = 0;
    initialized_ = true;
    return absl::OkStatus();
  }

  struct AlignedTimestampRange {
    absl::Time min;
    absl::Time max;
  };
  // Computes the min and max aligned timestamp range for a partition based on
  // the partition's min/max timestamps and each estimator's WITHIN bounds.
  //
  // For each estimator's WITHIN clause, the range of aligned timestamps for
  // which ALIGN can produce meaningful output is computed, and the union of
  // these ranges (intersected later with the OUTPUT WITHIN range) forms the
  // result.
  //
  // Conventions used below:
  // - An input point is captured by a WITHIN clause at aligned timestamp `T`
  //   if it falls in the range (T + lower_offset, T + upper_offset], where
  //   the offsets are signed durations (PRECEDING is negative, FOLLOWING is
  //   positive). The range is open on the lower bound and closed on the upper
  //   bound. See the inline comments for details on how these offsets are
  //   calculated.
  // - `min_aligned_timestamp` is inclusive and `max_aligned_timestamp` is
  //   exclusive, i.e. the returned range is [min, max).
  // - `partition_min_timestamp` / `partition_max_timestamp` are the earliest /
  //   latest input points in the partition.
  absl::StatusOr<AlignedTimestampRange> ComputePartitionAlignedTimestampRange(
      absl::Span<const std::unique_ptr<TupleData>> partition_rows) const {
    GOOGLESQL_RET_CHECK(!partition_rows.empty());
    absl::Time partition_min_timestamp =
        GetRowTimestamp(*partition_rows.front());
    absl::Time partition_max_timestamp =
        GetRowTimestamp(*partition_rows.back());
    absl::Time min_aligned_timestamp = absl::InfiniteFuture();
    absl::Time max_aligned_timestamp = absl::InfinitePast();
    bool all_unbounded_or_timestamp_bounds = true;
    const absl::Duration period = ToDuration(period_);

    // The aligned timestamp range for each estimator is determined based on
    // 4 cases for its WITHIN bounds:
    // Case 1: Two relative bounds.
    // Case 2: One unbounded bound (and one relative bound).
    //   a: UNBOUNDED PRECEDING TO RELATIVE.
    //   b: RELATIVE TO UNBOUNDED FOLLOWING.
    // Case 3: One absolute TIMESTAMP bound (and one ALIGNED TIMESTAMP bound).
    //   a: TIMESTAMP TO ALIGNED TIMESTAMP.
    //   b: ALIGNED TIMESTAMP TO TIMESTAMP.
    // Case 4: Fixed WITHIN range (both bounds UNBOUNDED or both absolute
    //   TIMESTAMP).
    for (const auto* estimator : estimators_) {
      GOOGLESQL_RET_CHECK(estimator->within_bounds() != nullptr);
      const WithinBoundExprArg* lower_bound =
          estimator->within_bounds()->lower_bound();
      const WithinBoundExprArg* upper_bound =
          estimator->within_bounds()->upper_bound();
      GOOGLESQL_RET_CHECK(lower_bound != nullptr);
      GOOGLESQL_RET_CHECK(upper_bound != nullptr);

      // Case 4: Fixed WITHIN range.
      // A WITHIN range that is fixed (unbounded on both sides, or absolute
      // timestamps on both sides) does not depend on the aligned timestamp.
      // Such a range captures the same input points regardless of `T` and is
      // ignored for this computation.
      if ((lower_bound->bound_kind() ==
               ResolvedWithinBoundExpr::UNBOUNDED_PRECEDING &&
           upper_bound->bound_kind() ==
               ResolvedWithinBoundExpr::UNBOUNDED_FOLLOWING) ||
          (lower_bound->bound_kind() == ResolvedWithinBoundExpr::TIMESTAMP &&
           upper_bound->bound_kind() == ResolvedWithinBoundExpr::TIMESTAMP)) {
        continue;
      }
      all_unbounded_or_timestamp_bounds = false;

      absl::Time estimator_min = absl::InfiniteFuture();
      absl::Time estimator_max = absl::InfinitePast();

      if (lower_bound->IsRelative() && upper_bound->IsRelative()) {
        // Case 1: Two relative bounds.
        // Aligned timestamp range spans from earliest timestamp that would
        // capture an input point in its WITHIN to the latest timestamp
        // capturing a point.
        GOOGLESQL_ASSIGN_OR_RETURN(IntervalValue upper_offset,
                         upper_bound->EvalOffset(period_, params_, context_));
        GOOGLESQL_ASSIGN_OR_RETURN(IntervalValue lower_offset,
                         lower_bound->EvalOffset(period_, params_, context_));
        // The upper bound determines the earliest aligned timestamp
        // capturing an input point: partition_min <= T + upper_offset,
        // i.e. T >= partition_min - upper_offset.
        estimator_min = partition_min_timestamp - ToDuration(upper_offset);
        // The relative lower bound determines the latest aligned timestamp
        // capturing an input point: T + lower_offset < partition_max,
        // i.e. T < partition_max - lower_offset.
        estimator_max = partition_max_timestamp - ToDuration(lower_offset);
      } else if (lower_bound->bound_kind() ==
                 ResolvedWithinBoundExpr::UNBOUNDED_PRECEDING) {
        // Case 2a: Left bound is UNBOUNDED PRECEDING (right bound is relative).
        // With an unbounded lower bound, the window is (-inf, upper_bound]. It
        // captures all input points once T + upper_offset >= partition_max. The
        // extra period is required because the first aligned timestamp whose
        // WITHIN captures all input timestamps will fall in the range:
        // [partition_max - upper_offset, partition_max - upper_offset + period)
        GOOGLESQL_ASSIGN_OR_RETURN(IntervalValue offset,
                         upper_bound->EvalOffset(period_, params_, context_));
        const absl::Duration signed_upper_offset = ToDuration(offset);
        estimator_min = partition_min_timestamp - signed_upper_offset;
        estimator_max = partition_max_timestamp - signed_upper_offset + period;
      } else if (upper_bound->bound_kind() ==
                 ResolvedWithinBoundExpr::UNBOUNDED_FOLLOWING) {
        // Case 2b: Right bound is UNBOUNDED FOLLOWING (left bound is relative).
        // With an unbounded upper bound, the window is (lower_bound, +inf). It
        // captures all input points once T + lower_offset < partition_min. The
        // extra period is subtracted because the first aligned timestamp whose
        // WITHIN captures all input timestamps will fall in the range:
        // (partition_min - lower_offset - period, partition_min - lower_offset]
        GOOGLESQL_ASSIGN_OR_RETURN(IntervalValue offset,
                         lower_bound->EvalOffset(period_, params_, context_));
        const absl::Duration signed_lower_offset = ToDuration(offset);
        estimator_min = partition_min_timestamp - signed_lower_offset - period;
        estimator_max = partition_max_timestamp - signed_lower_offset;
      } else if (lower_bound->bound_kind() ==
                 ResolvedWithinBoundExpr::TIMESTAMP) {
        // Case 3a: Lower bound is absolute TIMESTAMP (right bound is ALIGNED
        // TIMESTAMP).
        // The range spans from the earliest aligned timestamp capturing a point
        // to the earliest one capturing all input points since the lower bound.
        GOOGLESQL_ASSIGN_OR_RETURN(absl::Time lower_bound_timestamp,
                         lower_bound->EvalAbsolute(params_, context_));
        estimator_min =
            std::max(partition_min_timestamp, lower_bound_timestamp);
        // If lower_bound_timestamp <= partition_max_timestamp: earliest aligned
        // timestamp capturing all input points since the lower bound will fall
        // in the range: [partition_max, partition_max + period).
        estimator_max =
            std::max(partition_max_timestamp + period, lower_bound_timestamp);

      } else if (upper_bound->bound_kind() ==
                 ResolvedWithinBoundExpr::TIMESTAMP) {
        // Case 3b: Upper bound is absolute TIMESTAMP (left bound is ALIGNED
        // TIMESTAMP).
        // The range spans from the latest aligned timestamp capturing all
        // points until the upper bound to the latest one capturing a point.
        GOOGLESQL_ASSIGN_OR_RETURN(absl::Time upper_bound_timestamp,
                         upper_bound->EvalAbsolute(params_, context_));
        // If upper_bound_timestamp >= partition_min_timestamp: latest aligned
        // timestamp capturing all input points until the upper bound will fall
        // in the range: (partition_min - period, partition_min].
        estimator_min =
            std::min(partition_min_timestamp - period, upper_bound_timestamp);
        estimator_max =
            std::min(partition_max_timestamp, upper_bound_timestamp);
      } else {
        GOOGLESQL_RET_CHECK_FAIL() << "Unexpected WITHIN bound combination: "
                         << ResolvedWithinBoundExpr::BoundKindToString(
                                lower_bound->bound_kind())
                         << " to "
                         << ResolvedWithinBoundExpr::BoundKindToString(
                                upper_bound->bound_kind());
      }

      if (estimator_min < estimator_max) {
        min_aligned_timestamp = std::min(min_aligned_timestamp, estimator_min);
        max_aligned_timestamp = std::max(max_aligned_timestamp, estimator_max);
      }
    }

    // If every estimator's WITHIN was fixed (skipped above), no bound depends
    // on the aligned timestamp, so the range falls back to the partition's
    // input data range. This range is intersected with OUTPUT WITHIN by the
    // caller.
    if (all_unbounded_or_timestamp_bounds) {
      min_aligned_timestamp = partition_min_timestamp;
      max_aligned_timestamp = partition_max_timestamp;
    }
    return AlignedTimestampRange{
        .min = min_aligned_timestamp,
        .max = max_aligned_timestamp,
    };
  }

  absl::Status LoadAndProcessNextPartition() {
    if (current_partition_idx_ >= partitions_.size()) {
      end_of_partitions_ = true;
      return absl::OkStatus();
    }
    std::vector<std::unique_ptr<TupleData>>& partition_rows =
        partitions_[current_partition_idx_++];

    absl::Duration period_duration = ToDuration(period_);
    absl::Status status;

    // TODO: This can return timestamps which are outside of the
    // valid timestamp range in GoogleSQL. Check if we should return an
    // overflow error in such cases or if the range should be clamped
    // to [kTimestampMin, kTimestampMax].
    GOOGLESQL_ASSIGN_OR_RETURN(AlignedTimestampRange aligned_timestamp_range,
                     ComputePartitionAlignedTimestampRange(partition_rows));
    absl::Time min_aligned_timestamp = aligned_timestamp_range.min;
    absl::Time max_aligned_timestamp = aligned_timestamp_range.max;

    // Intersect with OUTPUT WITHIN bounds.
    min_aligned_timestamp =
        std::max(min_aligned_timestamp, output_lower_bound_);
    max_aligned_timestamp =
        std::min(max_aligned_timestamp, output_upper_bound_);

    TupleData aligned_timestamp_param(/*num_slots=*/1);
    std::vector<const TupleData*> estimator_params = params_;
    estimator_params.push_back(&aligned_timestamp_param);

    // Generate aligned timestamps and evaluate estimators.
    absl::Time current_aligned_timestamp =
        NextAlignedTimestampInclusive(min_aligned_timestamp, period_, origin_);
    while (current_aligned_timestamp < max_aligned_timestamp) {
      aligned_timestamp_param.mutable_slot(0)->SetValue(
          Value::Timestamp(current_aligned_timestamp));
      std::vector<Value> estimator_results;
      estimator_results.reserve(estimators_.size());
      for (const auto* estimator : estimators_) {
        GOOGLESQL_RET_CHECK(estimator->within_bounds() != nullptr);
        // Compute the WITHIN boundaries for the estimator function at current
        // aligned timestamp.
        GOOGLESQL_ASSIGN_OR_RETURN(absl::Time within_start_time,
                         estimator->within_bounds()->lower_bound()->Eval(
                             /*anchor_timestamp=*/current_aligned_timestamp,
                             period_, params_, context_));
        GOOGLESQL_ASSIGN_OR_RETURN(absl::Time within_end_time,
                         estimator->within_bounds()->upper_bound()->Eval(
                             /*anchor_timestamp=*/current_aligned_timestamp,
                             period_, params_, context_));

        // It's an error if the WITHIN range is provably empty, that is the
        // lower bound is greater than or equal to the upper bound for any value
        // of aligned timestamp. For example, if the PERIOD is 1 MINUTE then the
        // range WITHIN(2 PERIOD PRECEDING to INTERVAL 3 MINUTE PRECEDING) will
        // always be empty. It's not an error if the WITHIN range is empty only
        // for some values of aligned timestamps; e.g. in case of "RANGE FROM
        // Timestamp t1" the within range will be empty only for aligned
        // timestamps <= t1. Currently such cases are limited to
        // ANCHOR_TIMESTAMP bound types.
        if (estimator->within_bounds()->lower_bound()->bound_kind() !=
                ResolvedWithinBoundExpr::ANCHOR_TIMESTAMP &&
            estimator->within_bounds()->upper_bound()->bound_kind() !=
                ResolvedWithinBoundExpr::ANCHOR_TIMESTAMP &&
            within_start_time >= within_end_time) {
          return absl::OutOfRangeError(
              absl::StrCat("WITHIN Range is empty: ",
                           estimator->within_bounds()->DebugString()));
        }

        std::vector<const TupleData*> window_rows;
        if (within_start_time < within_end_time) {
          // Find rows that fall within the window (within_start_time,
          // within_end_time]. Since `partition_rows` is sorted, we can use
          // binary search to get the range of matching rows.
          auto it_begin = std::upper_bound(
              partition_rows.begin(), partition_rows.end(), within_start_time,
              [this](absl::Time t, const std::unique_ptr<TupleData>& row) {
                return t < GetRowTimestamp(*row);
              });
          auto it_end = std::upper_bound(
              partition_rows.begin(), partition_rows.end(), within_end_time,
              [this](absl::Time t, const std::unique_ptr<TupleData>& row) {
                return t < GetRowTimestamp(*row);
              });

          window_rows.reserve(std::distance(it_begin, it_end));
          for (auto it = it_begin; it != it_end; ++it) {
            window_rows.push_back(it->get());
          }
        }
        // Evaluate the estimator function over the rows in the window.
        GOOGLESQL_ASSIGN_OR_RETURN(Value val,
                         estimator->aggregate_arg()->EvalAgg(
                             window_rows, estimator_params, context_));
        estimator_results.push_back(std::move(val));
      }

      // Prune output row if all estimators are NULL.
      bool all_null = !estimators_.empty();
      for (const auto& val : estimator_results) {
        if (!val.is_null()) {
          all_null = false;
          break;
        }
      }
      if (all_null) {
        current_aligned_timestamp += period_duration;
        continue;
      }

      // Construct the output tuple.
      // Schema: [partition_keys..., estimators..., aligned_timestamp]
      auto output_data = std::make_unique<TupleData>(
          partition_keys_.size() + estimators_.size() + 1 + num_extra_slots_);
      int slot_idx = 0;
      for (int i = 0; i < partition_keys_.size(); ++i) {
        output_data->mutable_slot(slot_idx++)
            ->SetValue(
                partition_rows[0]->slot(partition_key_input_slots_[i]).value());
      }
      for (int i = 0; i < estimators_.size(); ++i) {
        output_data->mutable_slot(slot_idx++)
            ->SetValue(std::move(estimator_results[i]));
      }
      output_data->mutable_slot(slot_idx++)
          ->SetValue(Value::Timestamp(current_aligned_timestamp));

      if (!buffered_outputs_.PushBack(std::move(output_data), &status)) {
        return status;
      }
      current_aligned_timestamp += period_duration;
    }

    return absl::OkStatus();
  }

  const std::vector<const TupleData*> params_;
  const VariableId timestamp_var_;
  const ValueExpr* period_expr_;
  const ValueExpr* origin_expr_;
  const WithinBoundsArg* output_within_;
  const std::vector<const KeyArg*> partition_keys_;
  const VariableId aligned_timestamp_var_;
  const std::vector<const EstimatorArg*> estimators_;
  std::unique_ptr<TupleIterator> input_iter_;
  std::unique_ptr<TupleComparator> partition_comparator_;
  std::unique_ptr<TupleSchema> output_schema_;
  TupleDataDeque buffered_outputs_;
  const int num_extra_slots_;
  EvaluationContext* context_;
  int timestamp_slot_;
  std::vector<int> partition_key_input_slots_;
  absl::Status status_;

  std::unique_ptr<TupleData> current_;
  bool initialized_ = false;
  bool end_of_partitions_ = false;
  std::vector<std::vector<std::unique_ptr<TupleData>>> partitions_;
  size_t current_partition_idx_ = 0;
  IntervalValue period_;
  absl::Time origin_;
  absl::Time output_lower_bound_;
  absl::Time output_upper_bound_;
};

}  // namespace

absl::StatusOr<std::unique_ptr<TupleIterator>> AlignOp::CreateIterator(
    absl::Span<const TupleData* const> params, int num_extra_slots,
    EvaluationContext* context) const {
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<TupleIterator> input_iter,
                   input()->CreateIterator(params, num_extra_slots, context));
  std::vector<int> slots_for_partition_keys;
  slots_for_partition_keys.reserve(partition_keys().size());
  for (const KeyArg* partition_key : partition_keys()) {
    std::optional<int> slot =
        input_iter->Schema().FindIndexForVariable(partition_key->variable());
    GOOGLESQL_RET_CHECK(slot.has_value())
        << "Could not find variable " << partition_key->variable()
        << " in schema " << input_iter->Schema().DebugString();
    slots_for_partition_keys.push_back(slot.value());
  }

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<TupleComparator> partition_comparator,
      TupleComparator::Create(partition_keys(), slots_for_partition_keys,
                              params, context));

  std::vector<const EstimatorArg*> estimator_args(estimators().begin(),
                                                  estimators().end());

  auto iter = std::make_unique<AlignTupleIterator>(
      params, timestamp_var_, period(), origin(), output_within(),
      partition_keys(), aligned_timestamp_var_, estimator_args,
      std::move(input_iter), std::move(partition_comparator),
      CreateOutputSchema(), num_extra_slots, context);
  return MaybeReorder(std::move(iter), context);
}

}  // namespace googlesql
