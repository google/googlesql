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

#include "googlesql/reference_impl/functions/range.h"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "googlesql/common/errors.h"
#include "googlesql/public/civil_time.h"
#include "googlesql/public/functions/range.h"
#include "googlesql/public/interval_value.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/reference_impl/evaluation.h"
#include "googlesql/reference_impl/function.h"
#include "googlesql/reference_impl/tuple.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "googlesql/base/source_location.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace {

// RANGE's start and end being null are treated as -inf and +inf, respectively
bool DoTwoRangesOverlap(const Value& range1, const Value& range2) {
  const bool is_range1_start_before_range2_end =
      range1.start().is_null() || range2.end().is_null() ||
      range1.start().LessThan(range2.end());
  const bool is_range2_start_before_range1_end =
      range2.start().is_null() || range1.end().is_null() ||
      range2.start().LessThan(range1.end());
  return is_range1_start_before_range2_end && is_range2_start_before_range1_end;
}

// start being null/unbounded represents -inf
// s1 and s2 represent RANGE's start values
// Returns a boolean whether s1 is less than s2
bool IsStartLessThan(const Value& s1, const Value& s2) {
  if (s1.is_null() && s2.is_null()) return false;
  if (s1.is_null()) return true;
  if (s2.is_null()) return false;
  return s1.LessThan(s2);
}

// end being null/unbounded represents +inf
// e1 and e2 represent RANGE's end values
// Returns a boolean whether e1 is less than e2
bool IsEndLessThan(const Value& e1, const Value& e2) {
  if (e1.is_null() && e2.is_null()) return false;
  if (e1.is_null()) return false;
  if (e2.is_null()) return true;
  return e1.LessThan(e2);
}

template <typename LogicalType>
LogicalType ExtractRangeBoundary(const Value& boundary);

template <>
int32_t ExtractRangeBoundary(const Value& boundary) {
  return boundary.date_value();
}

template <>
DatetimeValue ExtractRangeBoundary(const Value& boundary) {
  return boundary.datetime_value();
}

template <>
absl::Time ExtractRangeBoundary(const Value& boundary) {
  return boundary.ToTime();
}

Value MakeRangeBoundary(int32_t boundary) { return Value::Date(boundary); }

Value MakeRangeBoundary(const DatetimeValue& boundary) {
  return Value::Datetime(boundary);
}

Value MakeRangeBoundary(const absl::Time boundary) {
  return Value::Timestamp(boundary);
}

template <typename GeneratorType>
absl::StatusOr<GeneratorType> CreateRangeArrayGenerator(
    const IntervalValue& step, bool last_partial_range,
    EvaluationContext* context);

template <>
absl::StatusOr<functions::DateRangeArrayGenerator> CreateRangeArrayGenerator(
    const IntervalValue& step, bool last_partial_range,
    EvaluationContext* context) {
  return functions::DateRangeArrayGenerator::Create(step, last_partial_range);
}

template <>
absl::StatusOr<functions::DatetimeRangeArrayGenerator>
CreateRangeArrayGenerator(const IntervalValue& step, bool last_partial_range,
                          EvaluationContext* context) {
  return functions::DatetimeRangeArrayGenerator::Create(
      step, last_partial_range,
      GetTimestampScale(context->GetLanguageOptions()));
}

template <>
absl::StatusOr<functions::TimestampRangeArrayGenerator>
CreateRangeArrayGenerator(const IntervalValue& step, bool last_partial_range,
                          EvaluationContext* context) {
  return functions::TimestampRangeArrayGenerator::Create(
      step, last_partial_range,
      GetTimestampScale(context->GetLanguageOptions()));
}

class RangeFunction : public SimpleBuiltinScalarFunction {
 public:
  explicit RangeFunction(const Type* output_type)
      : SimpleBuiltinScalarFunction(FunctionKind::kRangeCtor, output_type) {}
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

absl::StatusOr<Value> RangeFunction::Eval(
    absl::Span<const TupleData* const> params, absl::Span<const Value> args,
    EvaluationContext* context) const {
  GOOGLESQL_RET_CHECK_EQ(args.size(), 2);
  GOOGLESQL_RETURN_IF_ERROR(ValidateMicrosPrecision(args[0], context));
  GOOGLESQL_RETURN_IF_ERROR(ValidateMicrosPrecision(args[1], context));
  GOOGLESQL_ASSIGN_OR_RETURN(Value range_value, Value::MakeRange(args[0], args[1]));
  return range_value;
}

class RangeIsStartUnboundedFunction : public SimpleBuiltinScalarFunction {
 public:
  explicit RangeIsStartUnboundedFunction(const Type* output_type)
      : SimpleBuiltinScalarFunction(FunctionKind::kRangeIsStartUnbounded,
                                    output_type) {}
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

absl::StatusOr<Value> RangeIsStartUnboundedFunction::Eval(
    absl::Span<const TupleData* const> params, absl::Span<const Value> args,
    EvaluationContext* context) const {
  GOOGLESQL_RET_CHECK_EQ(args.size(), 1);
  GOOGLESQL_RETURN_IF_ERROR(ValidateMicrosPrecision(args[0], context));
  return args[0].is_null() ? Value::NullBool()
                           : Value::Bool(args[0].start().is_null());
}

class RangeIsEndUnboundedFunction : public SimpleBuiltinScalarFunction {
 public:
  explicit RangeIsEndUnboundedFunction(const Type* output_type)
      : SimpleBuiltinScalarFunction(FunctionKind::kRangeIsEndUnbounded,
                                    output_type) {}
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

absl::StatusOr<Value> RangeIsEndUnboundedFunction::Eval(
    absl::Span<const TupleData* const> params, absl::Span<const Value> args,
    EvaluationContext* context) const {
  GOOGLESQL_RET_CHECK_EQ(args.size(), 1);
  GOOGLESQL_RETURN_IF_ERROR(ValidateMicrosPrecision(args[0], context));
  return args[0].is_null() ? Value::NullBool()
                           : Value::Bool(args[0].end().is_null());
}

class RangeStartFunction : public SimpleBuiltinScalarFunction {
 public:
  explicit RangeStartFunction(const Type* output_type)
      : SimpleBuiltinScalarFunction(FunctionKind::kRangeStart, output_type) {}
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

absl::StatusOr<Value> RangeStartFunction::Eval(
    absl::Span<const TupleData* const> params, absl::Span<const Value> args,
    EvaluationContext* context) const {
  GOOGLESQL_RET_CHECK_EQ(args.size(), 1);
  GOOGLESQL_RETURN_IF_ERROR(ValidateMicrosPrecision(args[0], context));
  return args[0].is_null()
             ? Value::Null(args[0].type()->AsRange()->element_type())
             : args[0].start();
}

class RangeEndFunction : public SimpleBuiltinScalarFunction {
 public:
  explicit RangeEndFunction(const Type* output_type)
      : SimpleBuiltinScalarFunction(FunctionKind::kRangeEnd, output_type) {}
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

absl::StatusOr<Value> RangeEndFunction::Eval(
    absl::Span<const TupleData* const> params, absl::Span<const Value> args,
    EvaluationContext* context) const {
  GOOGLESQL_RET_CHECK_EQ(args.size(), 1);
  GOOGLESQL_RETURN_IF_ERROR(ValidateMicrosPrecision(args[0], context));
  return args[0].is_null()
             ? Value::Null(args[0].type()->AsRange()->element_type())
             : args[0].end();
}

class RangeOverlapsFunction : public SimpleBuiltinScalarFunction {
 public:
  explicit RangeOverlapsFunction(const Type* output_type)
      : SimpleBuiltinScalarFunction(FunctionKind::kRangeOverlaps, output_type) {
  }
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

absl::StatusOr<Value> RangeOverlapsFunction::Eval(
    absl::Span<const TupleData* const> params, absl::Span<const Value> args,
    EvaluationContext* context) const {
  GOOGLESQL_RET_CHECK_EQ(args.size(), 2);

  if (HasNulls(args)) {
    return Value::NullBool();
  }
  GOOGLESQL_RETURN_IF_ERROR(ValidateMicrosPrecision(args[0], context));
  GOOGLESQL_RETURN_IF_ERROR(ValidateMicrosPrecision(args[1], context));
  return Value::Bool(DoTwoRangesOverlap(args[0], args[1]));
}

class RangeIntersectFunction : public SimpleBuiltinScalarFunction {
 public:
  explicit RangeIntersectFunction(const Type* output_type)
      : SimpleBuiltinScalarFunction(FunctionKind::kRangeIntersect,
                                    output_type) {}
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

absl::StatusOr<Value> RangeIntersectFunction::Eval(
    absl::Span<const TupleData* const> params, absl::Span<const Value> args,
    EvaluationContext* context) const {
  GOOGLESQL_RET_CHECK_EQ(args.size(), 2);

  if (HasNulls(args)) {
    return Value::Null(types::RangeTypeFromSimpleTypeKind(
        args[0].type()->AsRange()->element_type()->kind()));
  }
  GOOGLESQL_RETURN_IF_ERROR(ValidateMicrosPrecision(args[0], context));
  GOOGLESQL_RETURN_IF_ERROR(ValidateMicrosPrecision(args[1], context));
  if (!DoTwoRangesOverlap(args[0], args[1])) {
    return MakeEvalError()
           << "Provided RANGE inputs: " << args[0].Format() << " and "
           << args[1].Format() << " do not overlap. "
           << "Please check RANGE_OVERLAPS before calling RANGE_INTERSECT";
  }

  // range intersection: [max(s1, s2), min(e1, e2))
  const Value intersect_start =
      IsStartLessThan(args[0].start(), args[1].start()) ? args[1].start()
                                                        : args[0].start();
  const Value intersect_end = IsEndLessThan(args[0].end(), args[1].end())
                                  ? args[0].end()
                                  : args[1].end();
  return Value::MakeRange(intersect_start, intersect_end);
}

class GenerateRangeArrayFunction : public SimpleBuiltinScalarFunction {
 public:
  explicit GenerateRangeArrayFunction(const Type* output_type)
      : SimpleBuiltinScalarFunction(FunctionKind::kGenerateRangeArray,
                                    output_type) {}

  template <typename LogicalType, typename GeneratorType>
  absl::StatusOr<std::vector<Value>> EvalForElement(
      const Value& range, const IntervalValue& step, bool last_partial_range,
      EvaluationContext* context) const;

  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

template <typename LogicalType, typename GeneratorType>
absl::StatusOr<std::vector<Value>> GenerateRangeArrayFunction::EvalForElement(
    const Value& range, const IntervalValue& step, bool last_partial_range,
    EvaluationContext* context) const {
  static_assert(
      (std::is_same_v<LogicalType, int32_t> &&
       std::is_same_v<GeneratorType, functions::DateRangeArrayGenerator>) ||
      (std::is_same_v<LogicalType, DatetimeValue> &&
       std::is_same_v<GeneratorType, functions::DatetimeRangeArrayGenerator>) ||
      (std::is_same_v<LogicalType, absl::Time> &&
       std::is_same_v<GeneratorType, functions::TimestampRangeArrayGenerator>));

  std::optional<LogicalType> range_start =
      range.start().is_null()
          ? std::nullopt
          : std::make_optional(
                ExtractRangeBoundary<LogicalType>(range.start()));
  std::optional<LogicalType> range_end =
      range.end().is_null()
          ? std::nullopt
          : std::make_optional(ExtractRangeBoundary<LogicalType>(range.end()));

  GOOGLESQL_ASSIGN_OR_RETURN(const GeneratorType& generator,
                   CreateRangeArrayGenerator<GeneratorType>(
                       step, last_partial_range, context));

  std::vector<Value> result;
  int64_t bytes_so_far = 0;
  GOOGLESQL_RETURN_IF_ERROR(generator.Generate(
      range_start, range_end,
      [&result, &bytes_so_far, context](
          const LogicalType& start, const LogicalType& end) -> absl::Status {
        GOOGLESQL_ASSIGN_OR_RETURN(Value range, Value::MakeRange(MakeRangeBoundary(start),
                                                       MakeRangeBoundary(end)));
        bytes_so_far += range.physical_byte_size();
        if (bytes_so_far > context->options().max_value_byte_size) {
          return MakeMaxArrayValueByteSizeExceededError(
              context->options().max_value_byte_size,
              googlesql_base::SourceLocation::current());
        }
        result.push_back(range);
        return absl::OkStatus();
      }));
  return result;
}

absl::StatusOr<Value> GenerateRangeArrayFunction::Eval(
    absl::Span<const TupleData* const> params, absl::Span<const Value> args,
    EvaluationContext* context) const {
  GOOGLESQL_RET_CHECK_GE(args.size(), 2);
  GOOGLESQL_RET_CHECK_LE(args.size(), 3);
  if (HasNulls(args)) {
    return Value::Null(output_type());
  }

  GOOGLESQL_RETURN_IF_ERROR(ValidateMicrosPrecision(args[0], context));
  GOOGLESQL_RET_CHECK(args[0].type()->IsRangeType());

  std::vector<Value> result;
  switch (args[0].type()->AsRange()->element_type()->kind()) {
    case TypeKind::TYPE_DATE: {
      GOOGLESQL_ASSIGN_OR_RETURN(
          result, (EvalForElement<int32_t, functions::DateRangeArrayGenerator>(
                      args[0], args[1].interval_value(), args[2].bool_value(),
                      context)));
      break;
    }
    case TypeKind::TYPE_DATETIME: {
      GOOGLESQL_ASSIGN_OR_RETURN(
          result, (EvalForElement<DatetimeValue,
                                  functions::DatetimeRangeArrayGenerator>(
                      args[0], args[1].interval_value(), args[2].bool_value(),
                      context)));
      break;
    }
    case TypeKind::TYPE_TIMESTAMP: {
      GOOGLESQL_ASSIGN_OR_RETURN(
          result,
          (EvalForElement<absl::Time, functions::TimestampRangeArrayGenerator>(
              args[0], args[1].interval_value(), args[2].bool_value(),
              context)));
      break;
    }
    default:
      return ::googlesql_base::UnimplementedErrorBuilder()
             << "Unsupported range element type for generate_range_array.";
  }

  return Value::MakeArray(output_type()->AsArray(), std::move(result));
}

class RangeContainsFunction : public SimpleBuiltinScalarFunction {
 public:
  explicit RangeContainsFunction(const Type* output_type)
      : SimpleBuiltinScalarFunction(FunctionKind::kRangeContains, output_type) {
  }
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

absl::StatusOr<Value> RangeContainsFunction::Eval(
    absl::Span<const TupleData* const> params, absl::Span<const Value> args,
    EvaluationContext* context) const {
  GOOGLESQL_RET_CHECK_EQ(args.size(), 2);
  if (HasNulls(args)) {
    return Value::NullBool();
  }

  GOOGLESQL_RETURN_IF_ERROR(ValidateMicrosPrecision(args[0], context));
  GOOGLESQL_RET_CHECK(args[0].type()->IsRangeType());

  if (args[1].type()->IsRangeType()) {
    // Fn 1. range_contains(range<T>, range<T>) -> bool
    GOOGLESQL_RETURN_IF_ERROR(ValidateMicrosPrecision(args[1], context));
    GOOGLESQL_RET_CHECK(args[0].type()->AsRange()->element_type()->Equals(
        args[1].type()->AsRange()->element_type()));

    return Value::Bool(!IsStartLessThan(args[1].start(), args[0].start()) &&
                       !IsEndLessThan(args[0].end(), args[1].end()));
  } else {
    // Fn 2. range_contains(range<T>, T) -> bool
    GOOGLESQL_RET_CHECK(
        args[0].type()->AsRange()->element_type()->Equals(args[1].type()));

    return Value::Bool(!IsStartLessThan(args[1], args[0].start()) &&
                       IsEndLessThan(args[1], args[0].end()));
  }
}

}  // namespace

void RegisterBuiltinRangeFunctions() {
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kRangeCtor},
      [](FunctionKind kind, const Type* output_type) {
        return new RangeFunction(output_type);
      });
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kRangeIsStartUnbounded},
      [](FunctionKind kind, const Type* output_type) {
        return new RangeIsStartUnboundedFunction(output_type);
      });
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kRangeIsEndUnbounded},
      [](FunctionKind kind, const Type* output_type) {
        return new RangeIsEndUnboundedFunction(output_type);
      });
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kRangeStart},
      [](FunctionKind kind, const Type* output_type) {
        return new RangeStartFunction(output_type);
      });
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kRangeEnd},
      [](FunctionKind kind, const Type* output_type) {
        return new RangeEndFunction(output_type);
      });
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kRangeOverlaps},
      [](FunctionKind kind, const Type* output_type) {
        return new RangeOverlapsFunction(output_type);
      });
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kRangeIntersect},
      [](FunctionKind kind, const Type* output_type) {
        return new RangeIntersectFunction(output_type);
      });
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kGenerateRangeArray},
      [](FunctionKind kind, const Type* output_type) {
        return new GenerateRangeArrayFunction(output_type);
      });
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kRangeContains},
      [](FunctionKind kind, const Type* output_type) {
        return new RangeContainsFunction(output_type);
      });
}

}  // namespace googlesql
