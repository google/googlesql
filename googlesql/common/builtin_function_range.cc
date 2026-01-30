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

#include <vector>

#include "googlesql/common/builtin_function_internal.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/input_argument_type.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "absl/status/status.h"
#include "absl/types/span.h"

namespace googlesql {

absl::Status RangeFunctionPreResolutionArgumentConstraint(
    absl::Span<const InputArgumentType> args,
    const LanguageOptions& language_options) {
  if (args.size() != 2) {
    return MakeSqlError() << "RANGE() must take exactly two arguments";
  }
  if (args[0].is_null() && args[1].is_null()) {
    return MakeSqlError() << "at least one of RANGE() arguments must be typed";
  }
  if (args[0].type() != args[1].type() && !args[0].is_null() &&
      !args[1].is_null()) {
    return MakeSqlError() << "RANGE() arguments must be of the same type";
  }

  int element_type_idx = 0;
  if (args[0].is_null()) {
    element_type_idx = 1;
  }
  if (args[element_type_idx].type() == nullptr ||
      !RangeType::IsValidElementType(args[element_type_idx].type())) {
    return MakeSqlError() << args[element_type_idx].UserFacingName(
                                 language_options.product_mode())
                          << " is not supported by RANGE()";
  }
  return absl::OkStatus();
}

// Checks that the signature doesn't have untyped NULL values in place of
// RANGE values, which are prohibited.
absl::Status PreResolutionArgConstraintForUntypedNullOneRangeInput(
    absl::Span<const InputArgumentType> args,
    const LanguageOptions& language_options) {
  // If the argument is an untyped NULL for RANGE, we return an error
  // message for now until RANGE supports other types like INT64.
  if (!args.empty() && args[0].is_untyped_null()) {
    // TODO: Find a way to access the error message generation
    // helpers on the Function object to generate the best error message here
    return MakeSqlError()
           << "A literal NULL argument cannot be accepted in place of RANGE. "
           << "Add a CAST to specify the type like CAST(NULL AS RANGE<DATE>). "
           << "Range supports DATE, DATETIME, and TIMESTAMP";
  }
  return absl::OkStatus();
}

// Checks that the signature doesn't have untyped NULL values in place of
// RANGE values, which are prohibited.
absl::Status PreResolutionArgConstraintForUntypedNullTwoRangeInputs(
    absl::Span<const InputArgumentType> args,
    const LanguageOptions& language_options) {
  // If the argument is an untyped NULL for RANGE, we return an error
  // message for now until RANGE supports other types like INT64.
  if (args.size() >= 2 &&
      (args[0].is_untyped_null() || args[1].is_untyped_null())) {
    // TODO: Find a way to access the error message generation
    // helpers on the Function object to generate the best error message here
    return MakeSqlError()
           << "A literal NULL argument cannot be accepted in place of RANGE. "
           << "Add a CAST to specify the type like CAST(NULL AS RANGE<DATE>). "
           << "Range supports DATE, DATETIME, and TIMESTAMP";
  }
  return absl::OkStatus();
}

void GetRangeFunctions(TypeFactory* type_factory,
                       const GoogleSQLBuiltinFunctionOptions& options,
                       NameToFunctionMap* functions) {
  const Type* bool_type = type_factory->get_bool();
  const Type* interval_type = type_factory->get_interval();
  const Type* date_range_type = types::DateRangeType();
  const Type* datetime_range_type = types::DatetimeRangeType();
  const Type* timestamp_range_type = types::TimestampRangeType();

  const Type* date_range_array_type;
  GOOGLESQL_CHECK_OK(  // Crash OK
      type_factory->MakeArrayType(date_range_type, &date_range_array_type));
  const Type* datetime_range_array_type;
  GOOGLESQL_CHECK_OK(  // Crash OK
      type_factory->MakeArrayType(datetime_range_type,
                                  &datetime_range_array_type));
  const Type* timestamp_range_array_type;
  GOOGLESQL_CHECK_OK(  // Crash OK
      type_factory->MakeArrayType(timestamp_range_type,
                                  &timestamp_range_array_type));

  static constexpr FunctionArgumentType::ArgumentCardinality OPTIONAL =
      FunctionArgumentType::OPTIONAL;

  FunctionOptions range_options =
      FunctionOptions().AddRequiredLanguageFeature(FEATURE_RANGE_TYPE);
  InsertFunction(functions, options, "range", Function::SCALAR,
                 {{
                     ARG_RANGE_TYPE_ANY_1,
                     {ARG_TYPE_ANY_1, ARG_TYPE_ANY_1},
                     FN_RANGE,
                 }},
                 FunctionOptions(range_options)
                     .set_pre_resolution_argument_constraint(
                         &RangeFunctionPreResolutionArgumentConstraint));
  InsertFunction(
      functions, options, "range_is_start_unbounded", Function::SCALAR,
      {{
          bool_type,
          {ARG_RANGE_TYPE_ANY_1},
          FN_RANGE_IS_START_UNBOUNDED,
      }},
      FunctionOptions(range_options)
          .set_pre_resolution_argument_constraint(
              &PreResolutionArgConstraintForUntypedNullOneRangeInput));
  InsertFunction(
      functions, options, "range_is_end_unbounded", Function::SCALAR,
      {{
          bool_type,
          {ARG_RANGE_TYPE_ANY_1},
          FN_RANGE_IS_END_UNBOUNDED,
      }},
      FunctionOptions(range_options)
          .set_pre_resolution_argument_constraint(
              &PreResolutionArgConstraintForUntypedNullOneRangeInput));
  InsertFunction(
      functions, options, "range_start", Function::SCALAR,
      {{
          ARG_TYPE_ANY_1,
          {ARG_RANGE_TYPE_ANY_1},
          FN_RANGE_START,
      }},
      FunctionOptions(range_options)
          .set_pre_resolution_argument_constraint(
              &PreResolutionArgConstraintForUntypedNullOneRangeInput));
  InsertFunction(
      functions, options, "range_end", Function::SCALAR,
      {{
          ARG_TYPE_ANY_1,
          {ARG_RANGE_TYPE_ANY_1},
          FN_RANGE_END,
      }},
      FunctionOptions(range_options)
          .set_pre_resolution_argument_constraint(
              &PreResolutionArgConstraintForUntypedNullOneRangeInput));
  InsertFunction(
      functions, options, "range_overlaps", Function::SCALAR,
      {{
          bool_type,
          {ARG_RANGE_TYPE_ANY_1, ARG_RANGE_TYPE_ANY_1},
          FN_RANGE_OVERLAPS,
      }},
      FunctionOptions(range_options)
          .set_pre_resolution_argument_constraint(
              &PreResolutionArgConstraintForUntypedNullTwoRangeInputs));
  InsertFunction(
      functions, options, "range_intersect", Function::SCALAR,
      {{
          ARG_RANGE_TYPE_ANY_1,
          {ARG_RANGE_TYPE_ANY_1, ARG_RANGE_TYPE_ANY_1},
          FN_RANGE_INTERSECT,
      }},
      FunctionOptions(range_options)
          .set_pre_resolution_argument_constraint(
              &PreResolutionArgConstraintForUntypedNullOneRangeInput));
  // Note that for GENERATE_RANGE_ARRAY we don't use any templates, since
  // depending on the element type in RANGE, the second, "step" argument will
  // have a different type as well.
  //
  // For example, DATE, DATETIME and TIMESTAMP overloads have "step" defined
  // as INTERVAL. For RANGE with numeric element types the type of "step"
  // argument will likely match the element type itself.
  InsertFunction(
      functions, options, "generate_range_array", Function::SCALAR,
      {{
           timestamp_range_array_type,
           {timestamp_range_type,
            interval_type,
            {bool_type, FunctionArgumentTypeOptions(OPTIONAL).set_default(
                            Value::Bool(true))}},
           FN_GENERATE_TIMESTAMP_RANGE_ARRAY,
       },
       {
           date_range_array_type,
           {date_range_type,
            interval_type,
            {bool_type, FunctionArgumentTypeOptions(OPTIONAL).set_default(
                            Value::Bool(true))}},
           FN_GENERATE_DATE_RANGE_ARRAY,
       },
       {
           datetime_range_array_type,
           {datetime_range_type,
            interval_type,
            {bool_type, FunctionArgumentTypeOptions(OPTIONAL).set_default(
                            Value::Bool(true))}},
           FN_GENERATE_DATETIME_RANGE_ARRAY,
           FunctionSignatureOptions().AddRequiredLanguageFeature(
               FEATURE_CIVIL_TIME),
       }},
      FunctionOptions(range_options)
          .set_pre_resolution_argument_constraint(
              &PreResolutionArgConstraintForUntypedNullOneRangeInput));

  InsertFunction(
      functions, options, "range_contains", Function::SCALAR,
      {{
           bool_type,
           {ARG_RANGE_TYPE_ANY_1, ARG_RANGE_TYPE_ANY_1},
           FN_RANGE_CONTAINS_RANGE,
       },
       {
           bool_type,
           {ARG_RANGE_TYPE_ANY_1, ARG_TYPE_ANY_1},
           FN_RANGE_CONTAINS_ELEMENT,
       }},
      FunctionOptions(range_options)
          .set_pre_resolution_argument_constraint(
              &PreResolutionArgConstraintForUntypedNullOneRangeInput));
}

}  // namespace googlesql
