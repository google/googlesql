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

#include "googlesql/reference_impl/function.h"

#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "google/protobuf/timestamp.pb.h"
#include "google/protobuf/wrappers.pb.h"
#include "google/type/date.pb.h"
#include "google/type/timeofday.pb.h"
#include "googlesql/common/evaluator_registration_utils.h"
#include "googlesql/common/internal_value.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/cast.h"
#include "googlesql/public/civil_time.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/functions/date_time_util.h"
#include "googlesql/public/interval_value.h"
#include "googlesql/public/json_value.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/proto/vector_encoding_id.pb.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/array_type.h"
#include "googlesql/public/types/declarative_type.h"
#include "googlesql/public/types/extended_type.h"
#include "googlesql/public/types/map_type.h"
#include "googlesql/public/types/proto_type.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/types/type_modifiers.h"
#include "googlesql/public/types/vector_type_util.h"
#include "googlesql/public/types/vector_value_codec.h"
#include "googlesql/public/value.h"
#include "googlesql/reference_impl/common.h"
#include "googlesql/reference_impl/evaluation.h"
#include "googlesql/reference_impl/functions/graph.h"
#include "googlesql/reference_impl/functions/hash.h"
#include "googlesql/reference_impl/functions/json.h"
#include "googlesql/reference_impl/operator.h"
#include "googlesql/reference_impl/tuple.h"
#include "googlesql/reference_impl/variable_id.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_collation.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "googlesql/testing/test_value.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/hash/hash.h"
#include "googlesql/base/check.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/types/span.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/dynamic_message.h"
#include "googlesql/base/optional_ref.h"

namespace googlesql {

using ::absl_testing::IsOkAndHolds;
using ::testing::HasSubstr;
using ::absl_testing::StatusIs;

namespace {
absl::StatusOr<std::unique_ptr<ScalarFunctionCallExpr>> CreateCastExpr(
    const LanguageOptions& language_options, const Type* output_type,
    std::unique_ptr<ValueExpr> argument) {
  return BuiltinScalarFunction::CreateCast(
      language_options, output_type, std::move(argument),
      /*format=*/nullptr, /*time_zone=*/nullptr, TypeModifiers(),
      /*return_null_on_error=*/false,
      ResolvedFunctionCallBase::DEFAULT_ERROR_MODE,
      /*extended_cast_evaluator=*/nullptr);
}
}  // namespace

TEST(SafeInvokeUnary, DoesNotLeakStatus) {
  ArithmeticFunction unary_minus_fn(FunctionKind::kSafeNegate,
                                    types::Int64Type());

  std::vector<const TupleData*> params;
  EvaluationContext context{/*options=*/{}};
  Value result;
  absl::Status status;
  EXPECT_TRUE(unary_minus_fn.Eval(
      /*params=*/{},
      /*args=*/{Value::Int64(std::numeric_limits<int64_t>::lowest())}, &context,
      &result, &status));

  EXPECT_TRUE(result.is_null());
  GOOGLESQL_EXPECT_OK(status);
}

TEST(ReplaceFields, InputWithoutRequiredFields) {
  TypeFactory factory;
  const ProtoType* proto_type = nullptr;

  const google::protobuf::Descriptor* descriptor =
      googlesql_test::KitchenSinkPB::descriptor();
  GOOGLESQL_ASSERT_OK(factory.MakeProtoType(descriptor, &proto_type));

  ReplaceFieldsFunction replace_fields_fn(
      proto_type,
      {ReplaceFieldsFunction::StructAndProtoPath(
          /*input_struct_index_path=*/{}, /*input_field_descriptor_path=*/{
              descriptor->FindFieldByName("int64_key_1")})});

  EvaluationContext context{/*options=*/{}};
  EXPECT_THAT(
      replace_fields_fn.Eval(
          /*params=*/{},
          /*args=*/{Value::Proto(proto_type, absl::Cord("")), Value::Int64(1)},
          &context),
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr("REPLACE_FIELDS() cannot be used on a proto with "
                         "missing fields: int64_key_1, int64_key_2")));
}

TEST(SafeInvokeBinary, DoesNotLeakStatus) {
  ArithmeticFunction safe_divide_fn(FunctionKind::kSafeDivide,
                                    types::DoubleType());

  std::vector<const TupleData*> params;
  EvaluationContext context{/*options=*/{}};
  Value result;
  absl::Status status;
  EXPECT_TRUE(
      safe_divide_fn.Eval(/*params=*/{},
                          /*args=*/{Value::Double(1.0), Value::Double(0.0)},
                          &context, &result, &status));
  EXPECT_TRUE(result.is_null());
  GOOGLESQL_EXPECT_OK(status);
}

TEST(NonDeterministicEvaluationContextTest, ArrayFilterTransformFunctionTest) {
  TypeFactory factory;
  const ArrayType* array_type;
  const Type* element_type = factory.get_int64();
  GOOGLESQL_EXPECT_OK(factory.MakeArrayType(element_type, &array_type));

  std::unique_ptr<ConstExpr> lambda_body =
      ConstExpr::Create(Value::Int64(3)).value();
  std::vector<VariableId> lambda_arg_vars = {VariableId("e")};
  std::unique_ptr<InlineLambdaExpr> lambda_algebra =
      InlineLambdaExpr::Create(lambda_arg_vars, std::move(lambda_body));

  ArrayTransformFunction trans_fn(FunctionKind::kArrayTransform, array_type,
                                  lambda_algebra.get());

  EvaluationContext context{/*options=*/{}};
  EXPECT_TRUE(context.IsDeterministicOutput());
  GOOGLESQL_EXPECT_OK(
      trans_fn
          .Eval(/*params=*/{},
                /*args=*/
                {Value::Array(array_type, {Value::Int64(1), Value::Int64(2)})},
                &context)
          .status());
  EXPECT_TRUE(context.IsDeterministicOutput());

  GOOGLESQL_EXPECT_OK(trans_fn
                .Eval(/*params=*/{},
                      /*args=*/
                      {InternalValue::Array(array_type,
                                            {Value::Int64(1), Value::Int64(2)},
                                            InternalValue::kIgnoresOrder)},
                      &context)
                .status());
  EXPECT_FALSE(context.IsDeterministicOutput());
}

TEST(NonDeterministicEvaluationContextTest, ArrayReduceFunctionTest) {
  TypeFactory factory;
  const ArrayType* array_type;
  const Type* element_type = factory.get_int64();
  GOOGLESQL_EXPECT_OK(factory.MakeArrayType(element_type, &array_type));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<ConstExpr> lambda_body,
                       ConstExpr::Create(Value::Int64(3)));
  std::vector<VariableId> lambda_arg_vars = {VariableId("acc"),
                                             VariableId("e")};
  std::unique_ptr<InlineLambdaExpr> lambda_algebra =
      InlineLambdaExpr::Create(lambda_arg_vars, std::move(lambda_body));

  ArrayReduceFunction reduce_fn(FunctionKind::kArrayReduce, element_type,
                                lambda_algebra.get());

  EvaluationContext context{/*options=*/{}};
  EXPECT_TRUE(context.IsDeterministicOutput());
  GOOGLESQL_EXPECT_OK(
      reduce_fn
          .Eval(/*params=*/{},
                /*args=*/
                {Value::Array(array_type, {Value::Int64(1), Value::Int64(2)}),
                 Value::Int64(0), Value::Bool(false)},
                &context)
          .status());
  EXPECT_TRUE(context.IsDeterministicOutput());

  GOOGLESQL_EXPECT_OK(reduce_fn
                .Eval(/*params=*/{},
                      /*args=*/
                      {InternalValue::Array(array_type,
                                            {Value::Int64(1), Value::Int64(2)},
                                            InternalValue::kIgnoresOrder),
                       Value::Int64(0), Value::Bool(false)},
                      &context)
                .status());
  EXPECT_FALSE(context.IsDeterministicOutput());
}

TEST(NonDeterministicEvaluationContextTest,
     ArrayReduceFunctionWithoutInitialValueTest) {
  TypeFactory factory;
  const ArrayType* array_type;
  const Type* element_type = factory.get_int64();
  GOOGLESQL_EXPECT_OK(factory.MakeArrayType(element_type, &array_type));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<ConstExpr> lambda_body,
                       ConstExpr::Create(Value::Int64(3)));
  std::vector<VariableId> lambda_arg_vars = {VariableId("acc"),
                                             VariableId("e")};
  std::unique_ptr<InlineLambdaExpr> lambda_algebra =
      InlineLambdaExpr::Create(lambda_arg_vars, std::move(lambda_body));

  ArrayReduceFunction reduce_fn(FunctionKind::kArrayReduce, element_type,
                                lambda_algebra.get());

  EvaluationContext context{/*options=*/{}};
  EXPECT_TRUE(context.IsDeterministicOutput());
  GOOGLESQL_EXPECT_OK(
      reduce_fn
          .Eval(/*params=*/{},
                /*args=*/
                {Value::Array(array_type, {Value::Int64(1), Value::Int64(2)}),
                 Value::Bool(false)},
                &context)
          .status());
  EXPECT_TRUE(context.IsDeterministicOutput());

  GOOGLESQL_EXPECT_OK(reduce_fn
                .Eval(/*params=*/{},
                      /*args=*/
                      {InternalValue::Array(array_type,
                                            {Value::Int64(1), Value::Int64(2)},
                                            InternalValue::kIgnoresOrder),
                       Value::Bool(false)},
                      &context)
                .status());
  EXPECT_FALSE(context.IsDeterministicOutput());
}

TEST(NonDeterministicEvaluationContextTest,
     ArrayMinMaxDistinguishableTiesStringTest) {
  // This setup overwrites the CollatorRegistration::CreateFromCollationNameFn
  // for current process, so that case insensitive collation name can be used.
  internal::EnableFullEvaluatorFeatures();

  // String with collation introduces non-determinism against distinguishable
  // ties.
  TypeFactory factory;
  const ArrayType* array_type;
  const Type* element_type = factory.get_string();
  GOOGLESQL_EXPECT_OK(factory.MakeArrayType(element_type, &array_type));

  std::vector<ResolvedCollation> collation_list;
  collation_list.push_back(ResolvedCollation::MakeScalar("unicode:ci"));

  // ARRAY_MIN
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(CollatorList collator_list_min,
                       MakeCollatorList(collation_list));

  ArrayMinMaxFunction arr_min_fn(FunctionKind::kArrayMin, element_type,
                                 std::move(collator_list_min));
  EvaluationContext context_min{/*options=*/{}};
  EXPECT_TRUE(context_min.IsDeterministicOutput());
  GOOGLESQL_EXPECT_OK(arr_min_fn
                .Eval(/*params=*/{},
                      /*args=*/
                      {InternalValue::Array(
                          array_type, {Value::String("a"), Value::String("A")},
                          InternalValue::kIgnoresOrder)},
                      &context_min)
                .status());
  EXPECT_FALSE(context_min.IsDeterministicOutput());

  // ARRAY_MAX
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(CollatorList collator_list_max,
                       MakeCollatorList(collation_list));

  ArrayMinMaxFunction arr_max_fn(FunctionKind::kArrayMax, element_type,
                                 std::move(collator_list_max));
  EvaluationContext context_max{/*options=*/{}};
  EXPECT_TRUE(context_max.IsDeterministicOutput());
  GOOGLESQL_EXPECT_OK(arr_max_fn
                .Eval(/*params=*/{},
                      /*args=*/
                      {InternalValue::Array(
                          array_type, {Value::String("a"), Value::String("A")},
                          InternalValue::kIgnoresOrder)},
                      &context_max)
                .status());
  EXPECT_FALSE(context_max.IsDeterministicOutput());
}

TEST(NonDeterministicEvaluationContextTest,
     ArrayMinMaxDistinguishableTiesIntervalTest) {
  // Interval with distinguishable ties could also trigger the deterministic
  // switch.
  TypeFactory factory;
  const ArrayType* array_type;
  const Type* element_type = factory.get_interval();
  GOOGLESQL_EXPECT_OK(factory.MakeArrayType(element_type, &array_type));

  // ARRAY_MIN
  ArrayMinMaxFunction arr_min_fn(FunctionKind::kArrayMin, element_type);
  EvaluationContext context_min{/*options=*/{}};
  EXPECT_TRUE(context_min.IsDeterministicOutput());
  GOOGLESQL_EXPECT_OK(
      arr_min_fn
          .Eval(/*params=*/{},
                /*args=*/
                {InternalValue::Array(
                    array_type,
                    {Value::Interval(IntervalValue::FromDays(30).value()),
                     Value::Interval(IntervalValue::FromMonths(1).value())},
                    InternalValue::kIgnoresOrder)},
                &context_min)
          .status());
  EXPECT_FALSE(context_min.IsDeterministicOutput());

  // ARRAY_MAX
  ArrayMinMaxFunction arr_max_fn(FunctionKind::kArrayMax, element_type);
  EvaluationContext context_max{/*options=*/{}};
  EXPECT_TRUE(context_max.IsDeterministicOutput());
  GOOGLESQL_EXPECT_OK(
      arr_max_fn
          .Eval(/*params=*/{},
                /*args=*/
                {InternalValue::Array(
                    array_type,
                    {Value::Interval(IntervalValue::FromDays(30).value()),
                     Value::Interval(IntervalValue::FromMonths(1).value())},
                    InternalValue::kIgnoresOrder)},
                &context_max)
          .status());
  EXPECT_FALSE(context_max.IsDeterministicOutput());
}

TEST(NonDeterministicEvaluationContextTest, ArraySumAvgFloatingPointTypeTest) {
  // Array input with floating-point type element introduces indeterminism for
  // ARRAY_SUM and ARRAY_AVG because floating point addition is not associative.
  TypeFactory factory;
  const ArrayType* array_type;
  const Type* element_type = factory.get_double();
  GOOGLESQL_EXPECT_OK(factory.MakeArrayType(element_type, &array_type));

  // ARRAY_SUM
  ArraySumAvgFunction arr_sum_fn(FunctionKind::kArraySum, factory.get_double());
  EvaluationContext context_sum{/*options=*/{}};
  EXPECT_TRUE(context_sum.IsDeterministicOutput());
  GOOGLESQL_EXPECT_OK(arr_sum_fn
                .Eval(/*params=*/{},
                      /*args=*/
                      {InternalValue::Array(
                          array_type, {Value::Double(4.1), Value::Double(-3.5)},
                          InternalValue::kIgnoresOrder)},
                      &context_sum)
                .status());
  EXPECT_FALSE(context_sum.IsDeterministicOutput());

  // ARRAY_AVG
  element_type = factory.get_float();
  GOOGLESQL_EXPECT_OK(factory.MakeArrayType(element_type, &array_type));
  ArraySumAvgFunction arr_avg_fn(FunctionKind::kArrayAvg, factory.get_double());
  EvaluationContext context_avg{/*options=*/{}};
  EXPECT_TRUE(context_avg.IsDeterministicOutput());
  GOOGLESQL_EXPECT_OK(arr_avg_fn
                .Eval(/*params=*/{},
                      /*args=*/
                      {InternalValue::Array(
                          array_type, {Value::Float(4.1), Value::Float(-3.5)},
                          InternalValue::kIgnoresOrder)},
                      &context_avg)
                .status());
  EXPECT_FALSE(context_avg.IsDeterministicOutput());
}

TEST(NonDeterministicEvaluationContextTest, ArraySumAvgUnsignedIntTypeTest) {
  // Array input with signed or unsigned integer type element introduces
  // indeterminism for ARRAY_AVG but not for ARRAY_SUM.
  TypeFactory factory;
  const ArrayType* array_type;
  const Type* element_type = factory.get_uint64();
  GOOGLESQL_EXPECT_OK(factory.MakeArrayType(element_type, &array_type));

  // ARRAY_SUM
  ArraySumAvgFunction arr_sum_fn(FunctionKind::kArraySum, factory.get_uint64());
  EvaluationContext context_sum{/*options=*/{}};
  EXPECT_TRUE(context_sum.IsDeterministicOutput());
  GOOGLESQL_EXPECT_OK(arr_sum_fn
                .Eval(/*params=*/{},
                      /*args=*/
                      {InternalValue::Array(
                          array_type, {Value::Uint64(40), Value::Uint64(20)},
                          InternalValue::kIgnoresOrder)},
                      &context_sum)
                .status());
  EXPECT_TRUE(context_sum.IsDeterministicOutput());

  // ARRAY_AVG
  ArraySumAvgFunction arr_avg_fn(FunctionKind::kArrayAvg, factory.get_double());
  EvaluationContext context_avg{/*options=*/{}};
  EXPECT_TRUE(context_avg.IsDeterministicOutput());
  GOOGLESQL_EXPECT_OK(arr_avg_fn
                .Eval(/*params=*/{},
                      /*args=*/
                      {InternalValue::Array(
                          array_type, {Value::Uint64(40), Value::Uint64(20)},
                          InternalValue::kIgnoresOrder)},
                      &context_avg)
                .status());
  EXPECT_FALSE(context_avg.IsDeterministicOutput());
}

TEST(NonDeterministicEvaluationContextTest,
     ArrayOffsetDistinguishableStringTest) {
  // Array input with collated STRING type element introduces indeterminism for
  // ARRAY_OFFSET(array<T>, T [, mode]) -> INT64
  // but not for
  // ARRAY_OFFSETS(array<T>, T) -> ARRAY<INT64>.

  // This setup overwrites the CollatorRegistration::CreateFromCollationNameFn
  // for current process, so that case insensitive collation name can be used.
  internal::EnableFullEvaluatorFeatures();

  TypeFactory factory;
  const ArrayType* input_type;
  const Type* element_type = factory.get_string();
  GOOGLESQL_EXPECT_OK(factory.MakeArrayType(element_type, &input_type));
  const ArrayType* int64_array_type;
  GOOGLESQL_EXPECT_OK(factory.MakeArrayType(factory.get_int64(), &int64_array_type));

  std::vector<ResolvedCollation> collation_list = {
      ResolvedCollation::MakeScalar("unicode:ci")};
  const Value array_find_mode_first =
      Value::Enum(types::ArrayFindModeEnumType(), 1);

  // ARRAY_OFFSET
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(CollatorList collator_list_offset,
                       MakeCollatorList(collation_list));

  ArrayFindFunctions offset_fn(FunctionKind::kArrayOffset, factory.get_int64(),
                               std::move(collator_list_offset));
  {
    EvaluationContext context_offset{/*options=*/{}};
    EXPECT_TRUE(context_offset.IsDeterministicOutput());
    GOOGLESQL_EXPECT_OK(
        offset_fn
            .Eval(/*params=*/{},
                  /*args=*/
                  {InternalValue::Array(
                       input_type, {Value::String("a"), Value::String("A")},
                       InternalValue::kIgnoresOrder),
                   Value::String("a"), array_find_mode_first},
                  &context_offset)
            .status());
    EXPECT_FALSE(context_offset.IsDeterministicOutput());
  }

  // If no matching element is found, the result is deterministic.
  {
    EvaluationContext context_offset{/*options=*/{}};
    EXPECT_TRUE(context_offset.IsDeterministicOutput());
    GOOGLESQL_EXPECT_OK(
        offset_fn
            .Eval(/*params=*/{},
                  /*args=*/
                  {InternalValue::Array(
                       input_type, {Value::String("a"), Value::String("A")},
                       InternalValue::kIgnoresOrder),
                   Value::String("b"), array_find_mode_first},
                  &context_offset)
            .status());
    EXPECT_TRUE(context_offset.IsDeterministicOutput());
  }

  // If input array has length smaller or equal to 1, the result is
  // deterministic.
  {
    EvaluationContext context_offset{/*options=*/{}};
    EXPECT_TRUE(context_offset.IsDeterministicOutput());
    GOOGLESQL_EXPECT_OK(offset_fn
                  .Eval(/*params=*/{},
                        /*args=*/
                        {InternalValue::Array(input_type, {Value::String("a")},
                                              InternalValue::kIgnoresOrder),
                         Value::String("A"), array_find_mode_first},
                        &context_offset)
                  .status());
    EXPECT_TRUE(context_offset.IsDeterministicOutput());
  }

  // ARRAY_OFFSETS
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(CollatorList collator_list_offsets,
                       MakeCollatorList(collation_list));

  ArrayFindFunctions offsets_fn(FunctionKind::kArrayOffsets, int64_array_type,
                                std::move(collator_list_offsets));
  {
    EvaluationContext context_offsets{/*options=*/{}};
    EXPECT_TRUE(context_offsets.IsDeterministicOutput());
    GOOGLESQL_EXPECT_OK(
        offsets_fn
            .Eval(/*params=*/{},
                  /*args=*/
                  {InternalValue::Array(
                       input_type, {Value::String("a"), Value::String("A")},
                       InternalValue::kIgnoresOrder),
                   Value::String("a")},
                  &context_offsets)
            .status());
    EXPECT_FALSE(context_offsets.IsDeterministicOutput());
  }

  // However, if no matching element is found, the result is deterministic.
  {
    EvaluationContext context_offsets{/*options=*/{}};
    EXPECT_TRUE(context_offsets.IsDeterministicOutput());
    GOOGLESQL_EXPECT_OK(
        offsets_fn
            .Eval(/*params=*/{},
                  /*args=*/
                  {InternalValue::Array(
                       input_type, {Value::String("a"), Value::String("A")},
                       InternalValue::kIgnoresOrder),
                   Value::String("b")},
                  &context_offsets)
            .status());
    EXPECT_TRUE(context_offsets.IsDeterministicOutput());
  }

  // If input array has length smaller or equal to 1, the result is
  // deterministic.
  {
    EvaluationContext context_offsets{/*options=*/{}};
    EXPECT_TRUE(context_offsets.IsDeterministicOutput());
    GOOGLESQL_EXPECT_OK(offsets_fn
                  .Eval(/*params=*/{},
                        /*args=*/
                        {InternalValue::Array(input_type, {Value::String("a")},
                                              InternalValue::kIgnoresOrder),
                         Value::String("A")},
                        &context_offsets)
                  .status());
    EXPECT_TRUE(context_offsets.IsDeterministicOutput());
  }
}

TEST(NonDeterministicEvaluationContextTest,
     ArrayFindDistinguishableTiesStringTest) {
  // Array input with collated STRING type element introduces indeterminism for
  // ARRAY_FIND(array<T>, T [, mode]) -> T.
  // Only when the number of ties is larger than 1, will the indeterministic
  // mark be set.

  // This setup overwrites the CollatorRegistration::CreateFromCollationNameFn
  // for current process, so that case insensitive collation name can be used.
  internal::EnableFullEvaluatorFeatures();

  TypeFactory factory;
  const ArrayType* input_type;
  const Type* element_type = factory.get_string();
  GOOGLESQL_EXPECT_OK(factory.MakeArrayType(element_type, &input_type));

  std::vector<ResolvedCollation> collation_list = {
      ResolvedCollation::MakeScalar("unicode:ci")};
  const Value array_find_mode_first =
      Value::Enum(types::ArrayFindModeEnumType(), 1);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(CollatorList collator_list,
                       MakeCollatorList(collation_list));
  ArrayFindFunctions find_fn_with_collation(
      FunctionKind::kArrayFind, factory.get_string(), std::move(collator_list));

  // ARRAY_FIND with one found element
  {
    EvaluationContext context1{/*options=*/{}};
    EXPECT_TRUE(context1.IsDeterministicOutput());
    GOOGLESQL_EXPECT_OK(
        find_fn_with_collation
            .Eval(/*params=*/{},
                  /*args=*/
                  {InternalValue::Array(
                       input_type, {Value::String("b"), Value::String("A")},
                       InternalValue::kIgnoresOrder),
                   Value::String("a"), array_find_mode_first},
                  &context1)
            .status());
    EXPECT_TRUE(context1.IsDeterministicOutput());
  }

  // ARRAY_FIND with two found elements that are distinguishable ties
  {
    EvaluationContext context2{/*options=*/{}};
    EXPECT_TRUE(context2.IsDeterministicOutput());
    GOOGLESQL_EXPECT_OK(
        find_fn_with_collation
            .Eval(/*params=*/{},
                  /*args=*/
                  {InternalValue::Array(input_type,
                                        {Value::String("b"), Value::String("a"),
                                         Value::String("A")},
                                        InternalValue::kIgnoresOrder),
                   Value::String("a"), array_find_mode_first},
                  &context2)
            .status());
    EXPECT_FALSE(context2.IsDeterministicOutput());
  }

  // ARRAY_FIND with lambda argument on case sensitive inputs.
  {
    std::unique_ptr<ConstExpr> lambda_body =
        ConstExpr::Create(Value::Bool(true)).value();
    std::vector<VariableId> lambda_arg_vars = {VariableId("e")};
    std::unique_ptr<InlineLambdaExpr> lambda_algebra =
        InlineLambdaExpr::Create(lambda_arg_vars, std::move(lambda_body));

    ArrayFindFunctions find_fn_with_lambda(
        FunctionKind::kArrayFind, factory.get_string(),
        /*collator_list=*/{}, lambda_algebra.get());
    EvaluationContext context{/*options=*/{}};
    EXPECT_TRUE(context.IsDeterministicOutput());
    GOOGLESQL_EXPECT_OK(
        find_fn_with_lambda
            .Eval(/*params=*/{},
                  /*args=*/
                  {InternalValue::Array(input_type,
                                        {Value::String("b"), Value::String("a"),
                                         Value::String("A")},
                                        InternalValue::kIgnoresOrder),
                   array_find_mode_first},
                  &context)
            .status());
    EXPECT_FALSE(context.IsDeterministicOutput());
  }
}

TEST(NonDeterministicEvaluationContextTest, EncodeVectorFunctionTest) {
  TypeFactory type_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* float_array_type,
                       type_factory.MakeArrayType(types::FloatType()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* vector_type, MakeVectorType(&type_factory));

  EncodeVectorFunction encode_vector_fn(FunctionKind::kEncodeVector,
                                        vector_type);

  // Test 1: Deterministic array (unordered, 1 element)
  {
    EvaluationContext context{/*options=*/{}};
    Value unordered_arr = InternalValue::ArrayChecked(
        float_array_type->AsArray(), InternalValue::kIgnoresOrder,
        {Value::Float(1.0)});

    absl::StatusOr<Value> result =
        encode_vector_fn.Eval({}, {unordered_arr}, &context);
    GOOGLESQL_ASSERT_OK(result);
    EXPECT_TRUE(context.IsDeterministicOutput());
  }

  // Test 2: Unordered array, identical elements (deterministic generation)
  {
    EvaluationContext context{/*options=*/{}};
    Value unordered_arr = InternalValue::ArrayChecked(
        float_array_type->AsArray(), InternalValue::kIgnoresOrder,
        {Value::Float(1.0), Value::Float(1.0), Value::Float(1.0)});

    absl::StatusOr<Value> result =
        encode_vector_fn.Eval({}, {unordered_arr}, &context);
    GOOGLESQL_ASSERT_OK(result);
    // Even if elements are identical, consistent with other functions,
    // unordered arrays larger than 1 element are currently considered
    // non-deterministic.
    EXPECT_FALSE(context.IsDeterministicOutput());
  }

  // Test 3: Unordered array, different elements (non-deterministic generation)
  {
    EvaluationContext context{/*options=*/{}};
    Value unordered_arr = InternalValue::ArrayChecked(
        float_array_type->AsArray(), InternalValue::kIgnoresOrder,
        {Value::Float(1.0), Value::Float(2.0), Value::Float(3.0)});

    absl::StatusOr<Value> result =
        encode_vector_fn.Eval({}, {unordered_arr}, &context);
    GOOGLESQL_ASSERT_OK(result);
    EXPECT_FALSE(context.IsDeterministicOutput());
  }
}

TEST(DynamicPropertyEqualsTest, DynamicPropertyEquals) {
  std::vector<Value> args;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const JSONValue json_value,
                       JSONValue::ParseJSONString(R"json(
  {
    "p_bool": true,
    "p_string": "hello",
    "p_double": 123.34,
    "p_int": 123,
    "p_int_overflow": 18446744073709551616,
    "p_int_array": [1,2,3],
    "p_double_array": [1.0,2.0,3.0],
    "p_string_array": ["hello","world"],
    "p_bool_array": [true, false, false]
  })json"));

  Value node = test_values::DynamicGraphNode(
      {"graph_name"}, "id", /*static_properties=*/{},
      /*dynamic_properties=*/json_value.GetConstRef(),
      /*static_labels=*/{}, /*dynamic_labels=*/{},
      /*element_table_name=*/"ElementTable");

  struct TestCase {
    const Value& node;
    std::string property;
    Value target_value;
    Value expected_result;
  };

  const Value kTrue = Value::Bool(true);
  const Value kFalse = Value::Bool(false);
  const Value kNull = Value::NullBool();

  std::vector<TestCase> test_cases = {
      {node, "p_unknown", Value::Bool(true), kNull},
      {node, "p_unknown", Value::Int64(123), kNull},
      {node, "p_unknown", Value::String("hello"), kNull},
      {node, "p_unknown", Value::Double(123.34), kNull},
      {node, "p_bool", Value::Bool(true), kTrue},
      {node, "p_bool", Value::Bool(false), kFalse},
      {node, "p_string", Value::String("hello"), kTrue},
      {node, "p_string", Value::String("world"), kFalse},
      {node, "p_double", Value::Double(123.34), kTrue},
      {node, "p_double", Value::Double(123.33), kFalse},
      {node, "p_int", Value::Int64(123), kTrue},
      {node, "p_bool", Value::String("hello"), kNull},
      {node, "p_int", Value::String("hello"), kNull},
      {node, "p_string", Value::Int64(1), kNull},
      {node, "p_string", Value::Bool(true), kNull},
      {node, "p_double", Value::String("hello"), kNull},
      {node, "p_double", Value::Int64(123), kNull},
      {node, "p_int_overflow",
       Value::Uint64(std::numeric_limits<uint64_t>::max()), kNull},
      {node, "p_int", Value::Int64(122), kFalse},
      {node, "p_bool_array",
       Value::MakeArray(
           types::BoolArrayType(),
           {Value::Bool(true), Value::Bool(false), Value::Bool(false)})
           .value(),
       kTrue},
      {node, "p_int_array",
       Value::MakeArray(types::Int64ArrayType(),
                        {Value::Int64(1), Value::Int64(2), Value::Int64(3)})
           .value(),
       kTrue},
      {node, "p_int_array",
       Value::MakeArray(types::Uint64ArrayType(),
                        {Value::Uint64(1), Value::Uint64(2), Value::Uint64(3)})
           .value(),
       kTrue},
      {node, "p_int_array",
       Value::MakeArray(types::Int32ArrayType(),
                        {Value::Int32(1), Value::Int32(2), Value::Int32(3)})
           .value(),
       kTrue},
      {node, "p_int_array",
       Value::MakeArray(types::Uint32ArrayType(),
                        {Value::Uint32(1), Value::Uint32(2), Value::Uint32(3)})
           .value(),
       kTrue},
      {node, "p_double_array",
       Value::MakeArray(
           types::FloatArrayType(),
           {Value::Float(1.0), Value::Float(2.0), Value::Float(3.0)})
           .value(),
       kTrue},
      {node, "p_double_array",
       Value::MakeArray(
           types::DoubleArrayType(),
           {Value::Double(1.0), Value::Double(2.0), Value::Double(3.0)})
           .value(),
       kTrue},
      {node, "p_double_array",
       Value::MakeArray(types::Int64ArrayType(),
                        {Value::Int64(1), Value::Int64(2), Value::Int64(3)})
           .value(),
       kTrue},
      {node, "p_string_array",
       Value::MakeArray(types::StringArrayType(),
                        {Value::String("hello"), Value::String("world")})
           .value(),
       kTrue},
      {node, "p_bool_array",
       Value::MakeArray(
           types::BoolArrayType(),
           {Value::Bool(false), Value::Bool(false), Value::Bool(false)})
           .value(),
       kFalse},
      {node, "p_int_array",
       Value::MakeArray(types::Int64ArrayType(),
                        {Value::Int64(1), Value::Int64(2), Value::Int64(2)})
           .value(),
       kFalse},
      {node, "p_int_array",
       Value::MakeArray(types::Uint64ArrayType(),
                        {Value::Uint64(1), Value::Uint64(2), Value::Uint64(2)})
           .value(),
       kFalse},
      {node, "p_int_array",
       Value::MakeArray(types::Int32ArrayType(),
                        {Value::Int32(1), Value::Int32(2), Value::Int32(2)})
           .value(),
       kFalse},
      {node, "p_int_array",
       Value::MakeArray(types::Uint32ArrayType(),
                        {Value::Uint32(1), Value::Uint32(2), Value::Uint32(2)})
           .value(),
       kFalse},
      {node, "p_double_array",
       Value::MakeArray(
           types::FloatArrayType(),
           {Value::Float(1.0), Value::Float(2.0), Value::Float(3.1)})
           .value(),
       kFalse},
      {node, "p_double_array",
       Value::MakeArray(
           types::DoubleArrayType(),
           {Value::Double(1.0), Value::Double(2.0), Value::Double(3.1)})
           .value(),
       kFalse},
      {node, "p_string_array",
       Value::MakeArray(types::StringArrayType(),
                        {Value::String("hello"), Value::String("world1")})
           .value(),
       kFalse},
      {node, "p_bool_array",
       Value::MakeArray(types::StringArrayType(),
                        {Value::String("true"), Value::String("false"),
                         Value::String("false")})
           .value(),
       kNull},
      {node, "p_int_array",
       Value::MakeArray(
           types::StringArrayType(),
           {Value::String("1"), Value::String("2"), Value::String("3")})
           .value(),
       kNull},
      {node, "p_string_array",
       Value::MakeArray(types::Int64ArrayType(),
                        {Value::Int64(1), Value::Int64(2), Value::Int64(3)})
           .value(),
       kNull},
  };

  RegisterBuiltinGraphFunctions();
  for (const auto& test_case : test_cases) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        BuiltinScalarFunction * property_equals_fn,
        BuiltinFunctionRegistry::GetScalarFunction(
            FunctionKind::kDynamicPropertyEquals, types::BoolType(), {}));
    auto fn = absl::WrapUnique<BuiltinScalarFunction>(property_equals_fn);
    EvaluationContext context{/*options=*/{}};
    Value result;
    absl::Status status;
    ASSERT_TRUE(fn->Eval(/*params=*/{},
                         {test_case.node, Value::String(test_case.property),
                          test_case.target_value},
                         &context, &result, &status));
    EXPECT_TRUE(result.Equals(test_case.expected_result))
        << "property: " << test_case.property
        << ", target_value: " << test_case.target_value.DebugString()
        << ", expected output: " << test_case.expected_result.DebugString()
        << ", actual output: " << result.DebugString();
  }
}

namespace {

class BasicTestFunction : public SimpleBuiltinScalarFunction {
 public:
  BasicTestFunction(FunctionKind kind, const Type* output_type)
      : SimpleBuiltinScalarFunction(kind, output_type) {}
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override {
    return absl::UnimplementedError("Not implemented");
  }

  std::vector<googlesql_base::optional_ref<AlgebraArg>> non_value_args_for_testing()
      const {
    return extended_args();
  }
};
}  // namespace

std::unique_ptr<InlineLambdaExpr> CreateLambdaExprForTesting() {
  return InlineLambdaExpr::Create(
      /*arguments=*/{VariableId("e")},
      /*body=*/ConstExpr::Create(Value::Bool(true)).value());
}

TEST(BuiltinFunctionRegistryTest, ScalarFunctionRegistrationAndLookup) {
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kIsNull}, [](FunctionKind kind, const Type* output_type) {
        return new BasicTestFunction(kind, output_type);
      });

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(BuiltinScalarFunction * unowned_test_fn,
                       BuiltinFunctionRegistry::GetScalarFunction(
                           FunctionKind::kIsNull, types::BoolType(), {}));
  auto test_fn = absl::WrapUnique<BuiltinScalarFunction>(unowned_test_fn);
  EXPECT_EQ(test_fn->kind(), FunctionKind::kIsNull);
  EXPECT_EQ(test_fn->output_type(), types::BoolType());
}

TEST(BuiltinFunctionRegistryTest, CreateCallPopulatesNonValueArgsInFunction) {
  BuiltinFunctionRegistry::RegisterScalarFunction(
      // Must be a function type that uses BuiltinFunctionRegistry.
      {FunctionKind::kMapFromArray},
      [](FunctionKind kind, const Type* output_type) {
        return new BasicTestFunction(kind, output_type);
      });

  // Create a call with a const string and a lambda argument.
  std::vector<std::unique_ptr<AlgebraArg>> args;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<ValueExpr> expr,
                       ConstExpr::Create(Value::StringValue("foo")));
  args.push_back(std::make_unique<ExprArg>(std::move(expr)));
  args.push_back(
      std::make_unique<InlineLambdaArg>(CreateLambdaExprForTesting()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<ScalarFunctionCallExpr> call,
                       BuiltinScalarFunction::CreateCall(
                           FunctionKind::kMapFromArray, LanguageOptions(),
                           types::BoolType(), std::move(args)));

  std::function<void(std::string*, googlesql_base::optional_ref<AlgebraArg>)>
      algebra_arg_formatter =
          [](std::string* out, googlesql_base::optional_ref<AlgebraArg> arg) {
            absl::StrAppend(out, arg.has_value() ? arg->DebugString() : "null");
          };

  EXPECT_THAT(
      absl::StrJoin(static_cast<const BasicTestFunction*>(call->function())
                        ->non_value_args_for_testing(),
                    ", ", algebra_arg_formatter),
      absl::StrCat("null, ", CreateLambdaExprForTesting()->DebugString()));
}

TEST(TimestampScaleTest, TimestampScale) {
  LanguageOptions options;
  EXPECT_EQ(GetTimestampScale(options),
            functions::TimestampScale::kMicroseconds);
  EXPECT_EQ(GetTimestampScale(options, /*support_picos=*/true),
            functions::TimestampScale::kMicroseconds);

  options.EnableLanguageFeature(FEATURE_TIMESTAMP_NANOS);
  EXPECT_EQ(GetTimestampScale(options),
            functions::TimestampScale::kNanoseconds);
  EXPECT_EQ(GetTimestampScale(options, /*support_picos=*/true),
            functions::TimestampScale::kNanoseconds);

  options.EnableLanguageFeature(FEATURE_TIMESTAMP_PICOS);
  EXPECT_EQ(GetTimestampScale(options),
            functions::TimestampScale::kNanoseconds);
  EXPECT_EQ(GetTimestampScale(options, /*support_picos=*/true),
            functions::TimestampScale::kPicoseconds);

  options.DisableLanguageFeature(FEATURE_TIMESTAMP_NANOS);
  EXPECT_EQ(GetTimestampScale(options),
            functions::TimestampScale::kMicroseconds);
  EXPECT_EQ(GetTimestampScale(options, /*support_picos=*/true),
            functions::TimestampScale::kPicoseconds);
}

namespace {
struct CastDeterminismTestParams {
  std::string test_name;
  Value input_value;
  const Type* output_type;
  bool expected_is_deterministic;
  std::shared_ptr<TypeFactory> type_factory = nullptr;
};

using CastDeterminismTest = ::testing::TestWithParam<CastDeterminismTestParams>;

TEST_P(CastDeterminismTest, CastDeterminism) {
  const auto& params = GetParam();
  Value result;
  absl::Status status;
  std::shared_ptr<TupleSlot::SharedProtoState> shared_proto_state;
  VirtualTupleSlot slot(&result, &shared_proto_state);
  EvaluationContext context{/*options=*/{}};
  LanguageOptions options;
  options.EnableMaximumLanguageFeaturesForDevelopment();
  context.SetLanguageOptions(options);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto argument, ConstExpr::Create(params.input_value));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto cast,
                       CreateCastExpr(context.GetLanguageOptions(),
                                      params.output_type, std::move(argument)));
  EXPECT_TRUE(cast->Eval({}, &context, &slot, &status));
  GOOGLESQL_ASSERT_OK(status);
  EXPECT_EQ(context.IsDeterministicOutput(), params.expected_is_deterministic);
}

std::vector<CastDeterminismTestParams> GetCastDeterminismTestCases() {
  std::vector<CastDeterminismTestParams> params;
  std::shared_ptr<TypeFactory> type_factory = std::make_shared<TypeFactory>();
  LanguageOptions options;
  options.EnableMaximumLanguageFeaturesForDevelopment();

  // Proto types
  const ProtoType* proto_type;
  GOOGLESQL_CHECK_OK(type_factory->MakeProtoType(
      googlesql_test::KitchenSinkPB::descriptor(), &proto_type));
  googlesql_test::KitchenSinkPB kitchen_sink;
  kitchen_sink.set_int64_key_1(1);
  kitchen_sink.set_int64_key_2(2);
  kitchen_sink.set_double_val(1.23);
  Value kitchen_sink_value =
      Value::Proto(proto_type, kitchen_sink.SerializeAsCord());

  // Struct types
  const StructType* double_struct_type;
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType({{"a", types::DoubleType()}},
                                        &double_struct_type));
  const StructType* string_struct_type;
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType({{"a", types::StringType()}},
                                        &string_struct_type));
  const StructType* string_double_struct_type;
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType(
      {{"key", types::StringType()}, {"value", types::DoubleType()}},
      &string_double_struct_type));
  const StructType* double_string_struct_type;
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType(
      {{"key", types::DoubleType()}, {"value", types::StringType()}},
      &double_string_struct_type));
  const StructType* string_string_struct_type;
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType(
      {{"key", types::StringType()}, {"value", types::StringType()}},
      &string_string_struct_type));

  // Array types.
  const ArrayType* string_double_struct_array_type;
  GOOGLESQL_CHECK_OK(type_factory->MakeArrayType(string_double_struct_type,
                                       &string_double_struct_array_type));
  const ArrayType* double_string_struct_array_type;
  GOOGLESQL_CHECK_OK(type_factory->MakeArrayType(double_string_struct_type,
                                       &double_string_struct_array_type));
  const ArrayType* string_string_struct_array_type;
  GOOGLESQL_CHECK_OK(type_factory->MakeArrayType(string_string_struct_type,
                                       &string_string_struct_array_type));

  // Map types.
  const Type* map_string_double_type = *type_factory->MakeMapType(
      types::StringType(), types::DoubleType(), options);
  const Type* map_string_string_type = *type_factory->MakeMapType(
      types::StringType(), types::StringType(), options);
  const Type* map_double_string_type = *type_factory->MakeMapType(
      types::DoubleType(), types::StringType(), options);

  // Input values.
  Value double_struct_value =
      *Value::MakeStruct(double_struct_type, {Value::Double(1.23)});
  Value string_double_struct_value = *Value::MakeStruct(
      string_double_struct_type, {Value::String("1.1"), Value::Double(1.23)});
  Value string_string_struct_value = *Value::MakeStruct(
      string_string_struct_type, {Value::String("1.1"), Value::String("1.1")});
  Value string_double_struct_array_value = *Value::MakeArray(
      string_double_struct_array_type, {string_double_struct_value});
  Value map_string_double_value = *Value::MakeMap(
      map_string_double_type, {{Value::String("1.1"), Value::Double(1.23)}});

  Value ordered_array = values::Int64Array({1, 2});
  Value unordered_array = InternalValue::ArrayNotChecked(
      types::Int64ArrayType(), InternalValue::kIgnoresOrder,
      {Value::Int64(1), Value::Int64(2)});
  Value unordered_identical_array = InternalValue::ArrayNotChecked(
      types::Int64ArrayType(), InternalValue::kIgnoresOrder,
      {Value::Int64(1), Value::Int64(1)});
  const StructType* array_struct_type;
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType({{"arr", types::Int64ArrayType()}},
                                        &array_struct_type));
  const StructType* json_struct_type;
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType({{"arr", types::JsonType()}},
                                        &json_struct_type));
  absl::StatusOr<Value> unordered_array_struct =
      Value::MakeStruct(array_struct_type, {unordered_array});
  GOOGLESQL_CHECK_OK(unordered_array_struct);
  absl::StatusOr<Value> ordered_node = test_values::GraphNode(
      {"g"}, "n1", {{"arr", ordered_array}}, {"L1"}, "T", type_factory.get());
  GOOGLESQL_CHECK_OK(ordered_node);
  absl::StatusOr<Value> unordered_node = test_values::GraphNode(
      {"g"}, "n1", {{"arr", unordered_array}}, {"L1"}, "T", type_factory.get());
  GOOGLESQL_CHECK_OK(unordered_node);
  absl::StatusOr<Value> edge_val =
      test_values::GraphEdge({"g"}, "e1", {{"a", Value::Int64(1)}}, {"L1"}, "T",
                             "n1", "n1", type_factory.get());
  GOOGLESQL_CHECK_OK(edge_val);
  const GraphPathType* path_type = test_values::MakeGraphPathType(
      ordered_node->type()->AsGraphElement(),
      edge_val->type()->AsGraphElement(), type_factory.get());
  absl::StatusOr<Value> ordered_path = Value::MakeGraphPath(
      path_type, {*ordered_node, *edge_val, *ordered_node});
  GOOGLESQL_CHECK_OK(ordered_path);
  absl::StatusOr<Value> unordered_path = Value::MakeGraphPath(
      path_type, {*unordered_node, *edge_val, *ordered_node});
  GOOGLESQL_CHECK_OK(unordered_path);
  const ArrayType* array_struct_array_type;
  GOOGLESQL_CHECK_OK(
      type_factory->MakeArrayType(array_struct_type, &array_struct_array_type));
  const ArrayType* json_struct_array_type;
  GOOGLESQL_CHECK_OK(
      type_factory->MakeArrayType(json_struct_type, &json_struct_array_type));
  absl::StatusOr<Value> unordered_array_struct_array =
      Value::MakeArray(array_struct_array_type, {*unordered_array_struct});
  GOOGLESQL_CHECK_OK(unordered_array_struct_array);
  absl::StatusOr<const Type*> map_string_array_type = type_factory->MakeMapType(
      types::StringType(), types::Int64ArrayType(), options);
  GOOGLESQL_CHECK_OK(map_string_array_type);
  absl::StatusOr<const Type*> map_string_json_type = type_factory->MakeMapType(
      types::StringType(), types::JsonType(), options);
  GOOGLESQL_CHECK_OK(map_string_json_type);
  absl::StatusOr<Value> unordered_array_map = Value::MakeMap(
      *map_string_array_type, {{Value::String("k"), unordered_array}});
  GOOGLESQL_CHECK_OK(unordered_array_map);
  Value variant_double = test_values::Variant(Value::Double(1.23));

  bool kDeterministic = true;
  bool kNonDeterministic = false;
  // Deterministic cases.
  params.push_back({"StringToDouble", Value::String("1.23"),
                    types::DoubleType(), kDeterministic, type_factory});
  params.push_back({"Int64ToString", Value::Int64(123), types::StringType(),
                    kDeterministic, type_factory});
  params.push_back({"DoubleToVariant", Value::Double(1.23),
                    types::VariantType(), kDeterministic, type_factory});
  params.push_back({"VariantDoubleToDouble", variant_double,
                    types::DoubleType(), kDeterministic, type_factory});
  params.push_back({"StringStringStructToStringDoubleStruct",
                    string_string_struct_value, string_double_struct_type,
                    kDeterministic, type_factory});
  params.push_back({"Int64ToJson", Value::Int64(123), types::JsonType(),
                    kDeterministic, type_factory});
  params.push_back({"ArrayInt64ToJson", values::Int64Array({1, 2, 3}),
                    types::JsonType(), kDeterministic, type_factory});
  params.push_back({"UncertainOrderIdenticalElementsArrayToJson",
                    unordered_identical_array, types::JsonType(),
                    kDeterministic, type_factory});
  params.push_back({"ArrayDoubleToJson", values::DoubleArray({1.23, 4.56}),
                    types::JsonType(), kDeterministic, type_factory});
  params.push_back({"StructArrayWithDoubleToJson",
                    string_double_struct_array_value, types::JsonType(),
                    kDeterministic, type_factory});
  params.push_back({"GraphNodeToJson", *ordered_node, types::JsonType(),
                    kDeterministic, type_factory});
  params.push_back({"GraphPathToJson", *ordered_path, types::JsonType(),
                    kDeterministic, type_factory});

  // Non-deterministic cases.
  params.push_back({"DoubleToString", Value::Double(1.23), types::StringType(),
                    kNonDeterministic, type_factory});
  params.push_back({"VariantDoubleToString", variant_double,
                    types::StringType(), kNonDeterministic, type_factory});
  params.push_back({"FloatToString", Value::Float(1.23f), types::StringType(),
                    kNonDeterministic, type_factory});
  params.push_back({"ProtoToString", kitchen_sink_value, types::StringType(),
                    kNonDeterministic, type_factory});
  params.push_back({"ArrayDoubleToString", values::DoubleArray({1.23}),
                    types::StringArrayType(), kNonDeterministic, type_factory});
  params.push_back({"StructDoubleToString", double_struct_value,
                    string_struct_type, kNonDeterministic, type_factory});
  params.push_back({"MapStringDoubleToStringString", map_string_double_value,
                    map_string_string_type, kNonDeterministic, type_factory});
  params.push_back({"ArrayStructStringDoubleToArrayStructStringString",
                    string_double_struct_array_value,
                    string_string_struct_array_type, kNonDeterministic,
                    type_factory});
  // Successfully detected non-determinism, and doesn't get fooled by the
  // unrelated float in the target type.
  params.push_back({"ArrayStructStringDoubleToArrayStructDoubleString",
                    string_double_struct_array_value,
                    double_string_struct_array_type, kNonDeterministic,
                    type_factory});
  params.push_back({"MapStringDoubleToDoubleString", map_string_double_value,
                    map_double_string_type, kNonDeterministic, type_factory});
  params.push_back({"UncertainOrderArrayToJson", unordered_array,
                    types::JsonType(), kNonDeterministic, type_factory});
  params.push_back({"StructWithUncertainOrderArrayToStructJson",
                    *unordered_array_struct, json_struct_type,
                    kNonDeterministic, type_factory});
  params.push_back({"ArrayOfStructWithUncertainOrderArrayToArrayOfStructJson",
                    *unordered_array_struct_array, json_struct_array_type,
                    kNonDeterministic, type_factory});
  params.push_back({"MapWithUncertainOrderArrayValueToMapJson",
                    *unordered_array_map, *map_string_json_type,
                    kNonDeterministic, type_factory});
  params.push_back({"GraphNodeWithUncertainOrderArrayToJson", *unordered_node,
                    types::JsonType(), kNonDeterministic, type_factory});
  params.push_back({"GraphPathWithUncertainOrderArrayToJson", *unordered_path,
                    types::JsonType(), kNonDeterministic, type_factory});

  return params;
}

INSTANTIATE_TEST_SUITE_P(
    CastDeterminismTestSuite, CastDeterminismTest,
    ::testing::ValuesIn(GetCastDeterminismTestCases()),
    [](const ::testing::TestParamInfo<CastDeterminismTestParams>& info) {
      return info.param.test_name;
    });

TEST(CastFunctionTest, SafeCastWithConstraintViolation) {
  TypeFactory factory;
  const Type* string_type = types::StringType();

  StringTypeParametersProto string_params_proto;
  string_params_proto.set_max_length(2);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      TypeParameters type_params,
      TypeParameters::MakeStringTypeParameters(string_params_proto));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto arg, ConstExpr::Create(Value::String("123")));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto cast_expr,
      BuiltinScalarFunction::CreateCast(
          LanguageOptions(), string_type, std::move(arg),
          /*format=*/nullptr, /*time_zone=*/nullptr,
          TypeModifiers::MakeTypeModifiers(type_params, Collation()),
          /*return_null_on_error=*/true,
          ResolvedFunctionCallBase::DEFAULT_ERROR_MODE,
          /*extended_cast_evaluator=*/nullptr));

  EvaluationContext context{/*options=*/{}};
  Value result_value;
  absl::Status status;
  std::shared_ptr<TupleSlot::SharedProtoState> shared_proto_state;
  VirtualTupleSlot slot(&result_value, &shared_proto_state);
  EXPECT_TRUE(cast_expr->Eval({}, &context, &slot, &status));
  GOOGLESQL_ASSERT_OK(status);
  EXPECT_TRUE(result_value.is_null());
}

TEST(CastFunctionTest, CastWithConstraintViolation) {
  TypeFactory factory;
  const Type* string_type = types::StringType();

  StringTypeParametersProto string_params_proto;
  string_params_proto.set_max_length(2);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      TypeParameters type_params,
      TypeParameters::MakeStringTypeParameters(string_params_proto));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto arg, ConstExpr::Create(Value::String("123")));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto cast_expr,
      BuiltinScalarFunction::CreateCast(
          LanguageOptions(), string_type, std::move(arg),
          /*format=*/nullptr, /*time_zone=*/nullptr,
          TypeModifiers::MakeTypeModifiers(type_params, Collation()),
          /*return_null_on_error=*/false,
          ResolvedFunctionCallBase::DEFAULT_ERROR_MODE,
          /*extended_cast_evaluator=*/nullptr));

  EvaluationContext context{/*options=*/{}};
  Value result_value;
  absl::Status status;
  std::shared_ptr<TupleSlot::SharedProtoState> shared_proto_state;
  VirtualTupleSlot slot(&result_value, &shared_proto_state);
  EXPECT_FALSE(cast_expr->Eval({}, &context, &slot, &status));
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kOutOfRange));
}

// SAFE_CAST must only suppress errors caused by the input value. Internal
// errors (e.g. GOOGLESQL_RET_CHECK failures from mismatched type parameters, which can
// only come from a malformed plan) must be propagated, not turned into NULL.
TEST(CastFunctionTest, SafeCastPropagatesInternalConstraintError) {
  const Type* int64_type = types::Int64Type();

  // STRING type parameters are not valid for INT64, so ApplyConstraints fails
  // with an internal error from ValidateResolvedTypeParameters.
  StringTypeParametersProto string_params_proto;
  string_params_proto.set_max_length(2);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      TypeParameters type_params,
      TypeParameters::MakeStringTypeParameters(string_params_proto));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto arg, ConstExpr::Create(Value::Int64(123)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto cast_expr,
      BuiltinScalarFunction::CreateCast(
          LanguageOptions(), int64_type, std::move(arg),
          /*format=*/nullptr, /*time_zone=*/nullptr,
          TypeModifiers::MakeTypeModifiers(type_params, Collation()),
          /*return_null_on_error=*/true,
          ResolvedFunctionCallBase::DEFAULT_ERROR_MODE,
          /*extended_cast_evaluator=*/nullptr));

  EvaluationContext context{/*options=*/{}};
  Value result_value;
  absl::Status status;
  std::shared_ptr<TupleSlot::SharedProtoState> shared_proto_state;
  VirtualTupleSlot slot(&result_value, &shared_proto_state);
  EXPECT_FALSE(cast_expr->Eval({}, &context, &slot, &status));
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kInternal));
}

// Same as above, but for an internal error raised by the cast itself rather
// than by type-parameter enforcement. An invalid Value trips a GOOGLESQL_RET_CHECK at
// the top of CastContext::CastValue.
TEST(CastFunctionTest, SafeCastPropagatesInternalCastError) {
  CastFunction cast_function(types::Int64Type(), types::StringType(),
                             /*extended_cast_evaluator=*/nullptr,
                             TypeParameters());
  EvaluationContext context{/*options=*/{}};
  EXPECT_THAT(
      cast_function.Eval(/*params=*/{},
                         {Value(), /*return_null_on_error=*/Value::Bool(true)},
                         &context),
      StatusIs(absl::StatusCode::kInternal));
}

namespace {

// Minimal extended type used only by SafeCastPropagatesUnimplementedCastError
// to route a cast through an extended conversion evaluator, so that SAFE_CAST
// handling of kUnimplemented errors can be tested.
class TestExtendedType : public ExtendedType {
 public:
  explicit TestExtendedType(const TypeFactory* factory)
      : ExtendedType(factory) {}

  bool ValueContentEquals(
      const ValueContent& x, const ValueContent& y,
      const ValueEqualityCheckOptions& options) const override {
    return x.GetAs<int64_t>() == y.GetAs<int64_t>();
  }
  bool ValueContentLess(const ValueContent& x, const ValueContent& y,
                        const Type* other_type) const override {
    return x.GetAs<int64_t>() < y.GetAs<int64_t>();
  }
  absl::HashState HashValueContent(const ValueContent& value,
                                   absl::HashState state) const override {
    return absl::HashState::combine(std::move(state), value.GetAs<int64_t>());
  }
  std::string TypeName(ProductMode mode) const override {
    return "TestExtendedType";
  }
  absl::HashState HashTypeParameter(absl::HashState state) const override {
    return absl::HashState::combine(std::move(state),
                                    reinterpret_cast<uintptr_t>(this));
  }
  absl::Status SerializeToProtoAndDistinctFileDescriptorsImpl(
      const BuildFileDescriptorSetMapOptions& options, TypeProto* type_proto,
      FileDescriptorSetMap* file_descriptor_set_map) const override {
    type_proto->set_type_kind(TYPE_EXTENDED);
    type_proto->set_extended_type_name(TypeName(ProductMode::PRODUCT_EXTERNAL));
    return absl::OkStatus();
  }
  absl::Status SerializeValueContent(const ValueContent& value,
                                     ValueProto* value_proto) const override {
    return absl::UnimplementedError("Unimplemented");
  }
  absl::Status DeserializeValueContent(const ValueProto& value_proto,
                                       ValueContent* value) const override {
    return absl::UnimplementedError("Unimplemented");
  }
  int64_t GetEstimatedOwnedMemoryBytesSize() const override {
    return sizeof(*this);
  }
  bool EqualsForSameKind(const Type* that, bool equivalent) const override {
    return this == that;
  }
  void DebugStringImpl(bool details, TypeOrStringVector* stack,
                       std::string* debug_string) const override {
    *debug_string = TypeName(ProductMode::PRODUCT_EXTERNAL);
  }
  std::string FormatValueContent(
      const ValueContent& value,
      const FormatValueContentOptions& options) const override {
    return absl::StrCat("TestExtended(", value.GetAs<int64_t>(), ")");
  }
};

}  // namespace

// SAFE_CAST only suppresses input-data errors (kInvalidArgument and
// kOutOfRange). Other non-internal codes, such as kUnimplemented from an
// extended conversion function, must also be propagated.
TEST(CastFunctionTest, SafeCastPropagatesUnimplementedCastError) {
  TypeFactory factory;
  TestExtendedType extended_type(&factory);

  // An extended conversion INT64 -> TestExtendedType whose evaluator always
  // fails with kUnimplemented.
  Function conversion_function(
      "test_conversion", "TestGroup", Function::SCALAR,
      {ConversionEvaluator::GetFunctionSignature(types::Int64Type(),
                                                 &extended_type)},
      FunctionOptions().set_evaluator(
          [](absl::Span<const Value> args) -> absl::StatusOr<Value> {
            return absl::UnimplementedError("Conversion not implemented");
          }));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      ConversionEvaluator conversion_evaluator,
      ConversionEvaluator::Create(types::Int64Type(), &extended_type,
                                  &conversion_function));
  auto extended_cast_evaluator =
      std::make_unique<ExtendedCompositeCastEvaluator>(
          std::vector<ConversionEvaluator>{conversion_evaluator});

  CastFunction cast_function(types::Int64Type(), &extended_type,
                             std::move(extended_cast_evaluator),
                             TypeParameters());
  EvaluationContext context{/*options=*/{}};
  EXPECT_THAT(cast_function.Eval(
                  /*params=*/{},
                  {Value::Int64(1), /*return_null_on_error=*/Value::Bool(true)},
                  &context),
              StatusIs(absl::StatusCode::kUnimplemented));
}

struct CrossDescriptorTestCase {
  // Name of the test case.
  std::string name;
  // GoogleSQL Type after conversion (e.g., TimestampType()).
  const Type* output_type;
  // Static C++ proto descriptor (from generated_pool()) used as the source
  // schema template to rebuild in the dynamic DescriptorPool.
  const google::protobuf::Descriptor* static_descriptor;
  // Function to populate fields on the dynamic proto message.
  std::function<void(google::protobuf::Message*, const google::protobuf::Descriptor*)>
      populate_message;
  // Expected GoogleSQL Value produced by FROM_PROTO evaluation.
  Value expected_value;
};

class FromProtoCrossDescriptorTest
    : public ::testing::TestWithParam<CrossDescriptorTestCase> {};

// Verifies that FROM_PROTO correctly converts proto messages whose ProtoType
// descriptor comes from a custom dynamic DescriptorPool distinct from
// DescriptorPool::generated_pool().
TEST_P(FromProtoCrossDescriptorTest, ConvertsFromCrossDescriptorPool) {
  const CrossDescriptorTestCase& test_case = GetParam();
  TypeFactory factory;

  // Create a dynamic DescriptorPool.
  google::protobuf::DescriptorPool dynamic_pool;
  std::function<void(const google::protobuf::FileDescriptor*)> add_file_to_pool =
      [&](const google::protobuf::FileDescriptor* file_desc) {
        for (int i = 0; i < file_desc->dependency_count(); ++i) {
          add_file_to_pool(file_desc->dependency(i));
        }
        if (dynamic_pool.FindFileByName(file_desc->name()) == nullptr) {
          google::protobuf::FileDescriptorProto f_proto;
          file_desc->CopyTo(&f_proto);
          dynamic_pool.BuildFile(f_proto);
        }
      };

  add_file_to_pool(test_case.static_descriptor->file());

  const google::protobuf::Descriptor* dynamic_desc = dynamic_pool.FindMessageTypeByName(
      test_case.static_descriptor->full_name());
  ASSERT_NE(dynamic_desc, nullptr);
  ASSERT_NE(dynamic_desc, test_case.static_descriptor);

  // Instantiate a dynamic proto message using DynamicMessageFactory.
  google::protobuf::DynamicMessageFactory dynamic_factory;
  const google::protobuf::Message* prototype = dynamic_factory.GetPrototype(dynamic_desc);
  std::unique_ptr<google::protobuf::Message> dynamic_message(prototype->New());

  test_case.populate_message(dynamic_message.get(), dynamic_desc);

  // Create a GoogleSQL Value from the dynamic proto message.
  const ProtoType* proto_type = nullptr;
  GOOGLESQL_ASSERT_OK(factory.MakeProtoType(dynamic_desc, &proto_type));
  absl::Cord cord;
  dynamic_message->SerializeToString(&cord);
  Value dynamic_proto_val = Value::Proto(proto_type, cord);

  // Evaluate FromProtoFunction for the test case output type.
  FromProtoFunction from_proto_fn(FunctionKind::kFromProto,
                                  test_case.output_type);

  EvaluationContext context{/*options=*/{}};
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value result, from_proto_fn.Eval(/*params=*/{},
                                       /*args=*/{dynamic_proto_val}, &context));

  EXPECT_EQ(result, test_case.expected_value);
}

INSTANTIATE_TEST_SUITE_P(
    AllTypes, FromProtoCrossDescriptorTest,
    ::testing::Values(
        CrossDescriptorTestCase{
            .name = "Timestamp",
            .output_type = types::TimestampType(),
            .static_descriptor = google::protobuf::Timestamp::descriptor(),
            .populate_message =
                [](google::protobuf::Message* msg, const google::protobuf::Descriptor* desc) {
                  const google::protobuf::FieldDescriptor* field =
                      desc->FindFieldByName("seconds");
                  msg->GetReflection()->SetInt64(msg, field, 123456);
                },
            .expected_value = Value::TimestampFromUnixMicros(123456000000LL),
        },
        CrossDescriptorTestCase{
            .name = "Date",
            .output_type = types::DateType(),
            .static_descriptor = google::type::Date::descriptor(),
            .populate_message =
                [](google::protobuf::Message* msg, const google::protobuf::Descriptor* desc) {
                  msg->GetReflection()->SetInt32(
                      msg, desc->FindFieldByName("year"), 2026);
                  msg->GetReflection()->SetInt32(
                      msg, desc->FindFieldByName("month"), 7);
                  msg->GetReflection()->SetInt32(
                      msg, desc->FindFieldByName("day"), 22);
                },
            .expected_value =
                []() {
                  int32_t d;
                  GOOGLESQL_CHECK_OK(functions::ConstructDate(2026, 7, 22, &d));
                  return Value::Date(d);
                }(),
        },
        CrossDescriptorTestCase{
            .name = "TimeOfDay",
            .output_type = types::TimeType(),
            .static_descriptor = google::type::TimeOfDay::descriptor(),
            .populate_message =
                [](google::protobuf::Message* msg, const google::protobuf::Descriptor* desc) {
                  msg->GetReflection()->SetInt32(
                      msg, desc->FindFieldByName("hours"), 12);
                  msg->GetReflection()->SetInt32(
                      msg, desc->FindFieldByName("minutes"), 30);
                  msg->GetReflection()->SetInt32(
                      msg, desc->FindFieldByName("seconds"), 45);
                },
            .expected_value = Value::Time(TimeValue::FromHMSAndMicros(12, 30,
                                                                      45, 0)),
        },
        CrossDescriptorTestCase{
            .name = "DoubleValue",
            .output_type = types::DoubleType(),
            .static_descriptor = google::protobuf::DoubleValue::descriptor(),
            .populate_message =
                [](google::protobuf::Message* msg, const google::protobuf::Descriptor* desc) {
                  msg->GetReflection()->SetDouble(
                      msg, desc->FindFieldByName("value"), 3.14159);
                },
            .expected_value = Value::Double(3.14159),
        },
        CrossDescriptorTestCase{
            .name = "FloatValue",
            .output_type = types::FloatType(),
            .static_descriptor = google::protobuf::FloatValue::descriptor(),
            .populate_message =
                [](google::protobuf::Message* msg, const google::protobuf::Descriptor* desc) {
                  msg->GetReflection()->SetFloat(
                      msg, desc->FindFieldByName("value"), 2.718f);
                },
            .expected_value = Value::Float(2.718f),
        },
        CrossDescriptorTestCase{
            .name = "Int64Value",
            .output_type = types::Int64Type(),
            .static_descriptor = google::protobuf::Int64Value::descriptor(),
            .populate_message =
                [](google::protobuf::Message* msg, const google::protobuf::Descriptor* desc) {
                  msg->GetReflection()->SetInt64(
                      msg, desc->FindFieldByName("value"), 100000LL);
                },
            .expected_value = Value::Int64(100000LL),
        },
        CrossDescriptorTestCase{
            .name = "UInt64Value",
            .output_type = types::Uint64Type(),
            .static_descriptor = google::protobuf::UInt64Value::descriptor(),
            .populate_message =
                [](google::protobuf::Message* msg, const google::protobuf::Descriptor* desc) {
                  msg->GetReflection()->SetUInt64(
                      msg, desc->FindFieldByName("value"), 200000ULL);
                },
            .expected_value = Value::Uint64(200000ULL),
        },
        CrossDescriptorTestCase{
            .name = "Int32Value",
            .output_type = types::Int32Type(),
            .static_descriptor = google::protobuf::Int32Value::descriptor(),
            .populate_message =
                [](google::protobuf::Message* msg, const google::protobuf::Descriptor* desc) {
                  msg->GetReflection()->SetInt32(
                      msg, desc->FindFieldByName("value"), 12345);
                },
            .expected_value = Value::Int32(12345),
        },
        CrossDescriptorTestCase{
            .name = "UInt32Value",
            .output_type = types::Uint32Type(),
            .static_descriptor = google::protobuf::UInt32Value::descriptor(),
            .populate_message =
                [](google::protobuf::Message* msg, const google::protobuf::Descriptor* desc) {
                  msg->GetReflection()->SetUInt32(
                      msg, desc->FindFieldByName("value"), 54321);
                },
            .expected_value = Value::Uint32(54321),
        },
        CrossDescriptorTestCase{
            .name = "BoolValue",
            .output_type = types::BoolType(),
            .static_descriptor = google::protobuf::BoolValue::descriptor(),
            .populate_message =
                [](google::protobuf::Message* msg, const google::protobuf::Descriptor* desc) {
                  msg->GetReflection()->SetBool(
                      msg, desc->FindFieldByName("value"), true);
                },
            .expected_value = Value::Bool(true),
        },
        CrossDescriptorTestCase{
            .name = "BytesValue",
            .output_type = types::BytesType(),
            .static_descriptor = google::protobuf::BytesValue::descriptor(),
            .populate_message =
                [](google::protobuf::Message* msg, const google::protobuf::Descriptor* desc) {
                  msg->GetReflection()->SetString(
                      msg, desc->FindFieldByName("value"), "hello bytes");
                },
            .expected_value = Value::Bytes("hello bytes"),
        },
        CrossDescriptorTestCase{
            .name = "StringValue",
            .output_type = types::StringType(),
            .static_descriptor = google::protobuf::StringValue::descriptor(),
            .populate_message =
                [](google::protobuf::Message* msg, const google::protobuf::Descriptor* desc) {
                  msg->GetReflection()->SetString(
                      msg, desc->FindFieldByName("value"), "hello string");
                },
            .expected_value = Value::String("hello string"),
        }),
    [](const ::testing::TestParamInfo<CrossDescriptorTestCase>& info) {
      return info.param.name;
    });

TEST(FromProtoCrossDescriptorTest, IncompatibleDescriptorParseFailure) {
  TypeFactory factory;
  google::protobuf::DescriptorPool dynamic_pool;
  // Build a custom dynamic FileDescriptorProto for google.protobuf.Timestamp
  // where field 1 ('seconds') is defined as TYPE_STRING instead of TYPE_INT64.
  google::protobuf::FileDescriptorProto file_proto;
  file_proto.set_name("google/protobuf/timestamp.proto");
  file_proto.set_package("google.protobuf");
  google::protobuf::DescriptorProto* msg_proto = file_proto.add_message_type();
  msg_proto->set_name("Timestamp");
  google::protobuf::FieldDescriptorProto* field_seconds = msg_proto->add_field();
  field_seconds->set_name("seconds");
  field_seconds->set_number(1);
  field_seconds->set_label(google::protobuf::FieldDescriptorProto::LABEL_OPTIONAL);
  field_seconds->set_type(google::protobuf::FieldDescriptorProto::TYPE_STRING);
  const google::protobuf::FileDescriptor* file_desc = dynamic_pool.BuildFile(file_proto);
  ASSERT_NE(file_desc, nullptr);
  const google::protobuf::Descriptor* dynamic_desc =
      file_desc->FindMessageTypeByName("Timestamp");
  ASSERT_NE(dynamic_desc, nullptr);
  // Instantiate dynamic message and set 'seconds' field to a string.
  google::protobuf::DynamicMessageFactory dynamic_factory;
  const google::protobuf::Message* prototype = dynamic_factory.GetPrototype(dynamic_desc);
  std::unique_ptr<google::protobuf::Message> dynamic_message(prototype->New());
  const google::protobuf::FieldDescriptor* field =
      dynamic_desc->FindFieldByName("seconds");
  dynamic_message->GetReflection()->SetString(dynamic_message.get(), field,
                                              "incompatible_string_value");
  // Create GoogleSQL Proto Value.
  const ProtoType* proto_type = nullptr;
  GOOGLESQL_ASSERT_OK(factory.MakeProtoType(dynamic_desc, &proto_type));
  absl::Cord cord;
  dynamic_message->SerializeToString(&cord);
  Value dynamic_proto_val = Value::Proto(proto_type, cord);
  // Evaluate FromProtoFunction for Timestamp.
  FromProtoFunction from_proto_fn(FunctionKind::kFromProto,
                                  types::TimestampType());
  EvaluationContext context{/*options=*/{}};
  absl::Status status =
      from_proto_fn.Eval(/*params=*/{}, /*args=*/{dynamic_proto_val}, &context)
          .status();
  EXPECT_EQ(status.code(), absl::StatusCode::kOutOfRange);
  EXPECT_THAT(
      status.message(),
      ::testing::HasSubstr(
          "Failed to parse proto message from google.protobuf.Timestamp "
          "to google.protobuf.Timestamp"));
}

TEST(JaroWinklerSimilarityFunctionTest, ReferenceImplEval) {
  LanguageOptions language_options;
  EvaluationContext context{/*options=*/{}};

  // Test STRING version
  {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto arg0_val,
                         ConstExpr::Create(Value::String("A\xf0\x9f\x98\x80"
                                                         "B")));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto arg1_val,
                         ConstExpr::Create(Value::String("A\xf0\x9f\x98\x81"
                                                         "B")));

    std::vector<std::unique_ptr<ValueExpr>> args;
    args.push_back(std::move(arg0_val));
    args.push_back(std::move(arg1_val));

    GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto call_expr,
                         BuiltinScalarFunction::CreateCall(
                             FunctionKind::kJaroWinklerSimilarity,
                             language_options, types::DoubleType(),
                             ConvertValueExprsToAlgebraArgs(std::move(args))));

    Value result;
    absl::Status status;
    std::shared_ptr<TupleSlot::SharedProtoState> shared_proto_state;
    VirtualTupleSlot slot(&result, &shared_proto_state);
    ASSERT_TRUE(call_expr->Eval({}, &context, &slot, &status));
    GOOGLESQL_ASSERT_OK(status);
    EXPECT_NEAR(result.double_value(), 0.8, 1e-6);
  }

  // Test BYTES version
  {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto arg0_val,
                         ConstExpr::Create(Value::Bytes("A\xf0\x9f\x98\x80"
                                                        "B")));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto arg1_val,
                         ConstExpr::Create(Value::Bytes("A\xf0\x9f\x98\x81"
                                                        "B")));

    std::vector<std::unique_ptr<ValueExpr>> args;
    args.push_back(std::move(arg0_val));
    args.push_back(std::move(arg1_val));

    GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto call_expr,
                         BuiltinScalarFunction::CreateCall(
                             FunctionKind::kJaroWinklerSimilarity,
                             language_options, types::DoubleType(),
                             ConvertValueExprsToAlgebraArgs(std::move(args))));

    Value result;
    absl::Status status;
    std::shared_ptr<TupleSlot::SharedProtoState> shared_proto_state;
    VirtualTupleSlot slot(&result, &shared_proto_state);
    ASSERT_TRUE(call_expr->Eval({}, &context, &slot, &status));
    GOOGLESQL_ASSERT_OK(status);
    EXPECT_NEAR(result.double_value(), 0.933333, 1e-6);
  }

  // Test custom parameters (4 arguments)
  {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto arg0_val,
                         ConstExpr::Create(Value::String("MARHTA")));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto arg1_val,
                         ConstExpr::Create(Value::String("MARTHA")));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto arg2_val, ConstExpr::Create(Value::Double(0.2)));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto arg3_val, ConstExpr::Create(Value::Double(0.90)));

    std::vector<std::unique_ptr<ValueExpr>> args;
    args.push_back(std::move(arg0_val));
    args.push_back(std::move(arg1_val));
    args.push_back(std::move(arg2_val));
    args.push_back(std::move(arg3_val));

    GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto call_expr,
                         BuiltinScalarFunction::CreateCall(
                             FunctionKind::kJaroWinklerSimilarity,
                             language_options, types::DoubleType(),
                             ConvertValueExprsToAlgebraArgs(std::move(args))));

    Value result;
    absl::Status status;
    std::shared_ptr<TupleSlot::SharedProtoState> shared_proto_state;
    VirtualTupleSlot slot(&result, &shared_proto_state);
    ASSERT_TRUE(call_expr->Eval({}, &context, &slot, &status));
    GOOGLESQL_ASSERT_OK(status);
    EXPECT_NEAR(result.double_value(), 0.977778, 1e-5);
  }

  // Test custom parameters (3 arguments: prefix_scaling_factor)
  {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto arg0_val,
                         ConstExpr::Create(Value::String("MARHTA")));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto arg1_val,
                         ConstExpr::Create(Value::String("MARTHA")));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto arg2_val, ConstExpr::Create(Value::Double(0.2)));

    std::vector<std::unique_ptr<ValueExpr>> args;
    args.push_back(std::move(arg0_val));
    args.push_back(std::move(arg1_val));
    args.push_back(std::move(arg2_val));

    GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto call_expr,
                         BuiltinScalarFunction::CreateCall(
                             FunctionKind::kJaroWinklerSimilarity,
                             language_options, types::DoubleType(),
                             ConvertValueExprsToAlgebraArgs(std::move(args))));

    Value result;
    absl::Status status;
    std::shared_ptr<TupleSlot::SharedProtoState> shared_proto_state;
    VirtualTupleSlot slot(&result, &shared_proto_state);
    ASSERT_TRUE(call_expr->Eval({}, &context, &slot, &status));
    GOOGLESQL_ASSERT_OK(status);
    EXPECT_NEAR(result.double_value(), 0.977778, 1e-5);
  }

  // Test custom parameters (4 arguments: prefix_scaling_factor is null, custom
  // threshold)
  {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto arg0_val,
                         ConstExpr::Create(Value::String("MARHTA")));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto arg1_val,
                         ConstExpr::Create(Value::String("MARTHA")));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto arg2_val, ConstExpr::Create(Value::NullDouble()));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto arg3_val, ConstExpr::Create(Value::Double(0.90)));

    std::vector<std::unique_ptr<ValueExpr>> args;
    args.push_back(std::move(arg0_val));
    args.push_back(std::move(arg1_val));
    args.push_back(std::move(arg2_val));
    args.push_back(std::move(arg3_val));

    GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto call_expr,
                         BuiltinScalarFunction::CreateCall(
                             FunctionKind::kJaroWinklerSimilarity,
                             language_options, types::DoubleType(),
                             ConvertValueExprsToAlgebraArgs(std::move(args))));

    Value result;
    absl::Status status;
    std::shared_ptr<TupleSlot::SharedProtoState> shared_proto_state;
    VirtualTupleSlot slot(&result, &shared_proto_state);
    ASSERT_TRUE(call_expr->Eval({}, &context, &slot, &status));
    GOOGLESQL_ASSERT_OK(status);
    EXPECT_TRUE(result.is_null());
  }

  // Test invalid parameters range error
  {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto arg0_val,
                         ConstExpr::Create(Value::String("MARHTA")));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto arg1_val,
                         ConstExpr::Create(Value::String("MARTHA")));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto arg2_val, ConstExpr::Create(Value::Double(0.26)));

    std::vector<std::unique_ptr<ValueExpr>> args;
    args.push_back(std::move(arg0_val));
    args.push_back(std::move(arg1_val));
    args.push_back(std::move(arg2_val));

    GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto call_expr,
                         BuiltinScalarFunction::CreateCall(
                             FunctionKind::kJaroWinklerSimilarity,
                             language_options, types::DoubleType(),
                             ConvertValueExprsToAlgebraArgs(std::move(args))));

    Value result;
    absl::Status status;
    std::shared_ptr<TupleSlot::SharedProtoState> shared_proto_state;
    VirtualTupleSlot slot(&result, &shared_proto_state);
    EXPECT_FALSE(call_expr->Eval({}, &context, &slot, &status));
    EXPECT_THAT(status, StatusIs(absl::StatusCode::kOutOfRange));
  }
}

// Evaluates the builtin scalar function `kind` with return type `output_type`
// on `args` in `context`.
absl::StatusOr<Value> EvalBuiltinScalarFunction(FunctionKind kind,
                                                const Type* output_type,
                                                EvaluationContext* context,
                                                absl::Span<const Value> args) {
  absl::StatusOr<BuiltinScalarFunction*> unowned_fn =
      BuiltinFunctionRegistry::GetScalarFunction(kind, output_type, {});
  if (!unowned_fn.ok()) {
    return unowned_fn.status();
  }
  std::unique_ptr<BuiltinScalarFunction> fn = absl::WrapUnique(*unowned_fn);
  Value result;
  absl::Status status;
  if (!fn->Eval(/*params=*/{}, args, context, &result, &status)) {
    return status;
  }
  return result;
}

TEST(FunctionEvaluationTest, JsonExistsFunctionEval) {
  RegisterBuiltinJsonFunctions();
  EvaluationContext context{/*options=*/{}};

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(JSONValue json_obj, JSONValue::ParseJSONString(
                                               R"json({"a": 1, "b": 2})json"));
  const Value json_val = Value::Json(std::move(json_obj));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value single_path_array,
      Value::MakeArray(types::StringArrayType(), {Value::String("$.a")}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value int64_array,
      Value::MakeArray(types::Int64ArrayType(), {Value::Int64(1)}));

  auto eval = [&](FunctionKind kind,
                  absl::Span<const Value> args) -> absl::StatusOr<Value> {
    return EvalBuiltinScalarFunction(kind, types::BoolType(), &context, args);
  };

  // 1. Argument count check (GOOGLESQL_RET_CHECK_EQ(args.size(), 2)).
  //
  // kJsonExists
  EXPECT_THAT(eval(FunctionKind::kJsonExists, {}),
              StatusIs(absl::StatusCode::kInternal));
  EXPECT_THAT(eval(FunctionKind::kJsonExists, {json_val}),
              StatusIs(absl::StatusCode::kInternal));
  EXPECT_THAT(eval(FunctionKind::kJsonExists,
                   {json_val, Value::String("$.a"), Value::String("extra")}),
              StatusIs(absl::StatusCode::kInternal));

  // kJsonExistsAny
  EXPECT_THAT(eval(FunctionKind::kJsonExistsAny, {}),
              StatusIs(absl::StatusCode::kInternal));
  EXPECT_THAT(eval(FunctionKind::kJsonExistsAny, {json_val}),
              StatusIs(absl::StatusCode::kInternal));
  EXPECT_THAT(eval(FunctionKind::kJsonExistsAny,
                   {json_val, single_path_array, Value::String("extra")}),
              StatusIs(absl::StatusCode::kInternal));

  // kJsonExistsAll
  EXPECT_THAT(eval(FunctionKind::kJsonExistsAll, {}),
              StatusIs(absl::StatusCode::kInternal));
  EXPECT_THAT(eval(FunctionKind::kJsonExistsAll, {json_val}),
              StatusIs(absl::StatusCode::kInternal));
  EXPECT_THAT(eval(FunctionKind::kJsonExistsAll,
                   {json_val, single_path_array, Value::String("extra")}),
              StatusIs(absl::StatusCode::kInternal));

  // 2. Argument type check (unreachable from SQL due to analyzer checks).
  //
  // kJsonExists
  EXPECT_THAT(
      eval(FunctionKind::kJsonExists, {Value::Int64(1), Value::String("$.a")}),
      StatusIs(absl::StatusCode::kInternal));
  EXPECT_THAT(eval(FunctionKind::kJsonExists, {json_val, Value::Int64(1)}),
              StatusIs(absl::StatusCode::kInternal));
  EXPECT_THAT(eval(FunctionKind::kJsonExists, {json_val, single_path_array}),
              StatusIs(absl::StatusCode::kInternal));

  // kJsonExistsAny
  EXPECT_THAT(
      eval(FunctionKind::kJsonExistsAny, {Value::Int64(1), single_path_array}),
      StatusIs(absl::StatusCode::kInternal));
  EXPECT_THAT(
      eval(FunctionKind::kJsonExistsAny, {json_val, Value::String("$.a")}),
      StatusIs(absl::StatusCode::kInternal));
  EXPECT_THAT(eval(FunctionKind::kJsonExistsAny, {json_val, int64_array}),
              StatusIs(absl::StatusCode::kInternal));

  // kJsonExistsAll
  EXPECT_THAT(
      eval(FunctionKind::kJsonExistsAll, {Value::Int64(1), single_path_array}),
      StatusIs(absl::StatusCode::kInternal));
  EXPECT_THAT(
      eval(FunctionKind::kJsonExistsAll, {json_val, Value::String("$.a")}),
      StatusIs(absl::StatusCode::kInternal));
  EXPECT_THAT(eval(FunctionKind::kJsonExistsAll, {json_val, int64_array}),
              StatusIs(absl::StatusCode::kInternal));

  // 3. Unvalidated JSON string parsing error handling.
  const Value unvalidated_invalid_json =
      Value::UnvalidatedJsonString("invalid json");
  EXPECT_THAT(eval(FunctionKind::kJsonExists,
                   {unvalidated_invalid_json, Value::String("$.a")}),
              StatusIs(absl::StatusCode::kOutOfRange));
  EXPECT_THAT(eval(FunctionKind::kJsonExistsAny,
                   {unvalidated_invalid_json, single_path_array}),
              StatusIs(absl::StatusCode::kOutOfRange));
  EXPECT_THAT(eval(FunctionKind::kJsonExistsAll,
                   {unvalidated_invalid_json, single_path_array}),
              StatusIs(absl::StatusCode::kOutOfRange));
}

TEST(FunctionEvaluationTest, JsonPathWildcardsAndDescendantFeatureGating) {
  RegisterBuiltinJsonFunctions();

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      JSONValue json_obj,
      JSONValue::ParseJSONString(R"json({"a": 1, "b": 2, "c": [10, 20]})json"));
  const Value json_val = Value::Json(std::move(json_obj));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value wildcard_path_array,
      Value::MakeArray(types::StringArrayType(), {Value::String("$.*")}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value descendant_path_array,
      Value::MakeArray(types::StringArrayType(), {Value::String("$..a")}));

  EvaluationContext disabled_context{/*options=*/{}};

  EvaluationContext enabled_context{/*options=*/{}};
  LanguageOptions enabled_options;
  enabled_options.EnableLanguageFeature(
      FEATURE_JSONPATH_WILDCARDS_AND_DESCENDANT);
  enabled_context.SetLanguageOptions(enabled_options);

  struct JsonPathFunction {
    FunctionKind kind;
    const Type* output_type;
  };
  // SQL standard JSONPath functions, which support wildcard and descendant
  // operators only when FEATURE_JSONPATH_WILDCARDS_AND_DESCENDANT is enabled.
  const JsonPathFunction standard_functions[] = {
      {FunctionKind::kJsonExists, types::BoolType()},
      {FunctionKind::kJsonQuery, types::JsonType()},
      {FunctionKind::kJsonValue, types::StringType()},
      {FunctionKind::kJsonQueryArray, types::JsonArrayType()},
      {FunctionKind::kJsonValueArray, types::StringArrayType()},
  };
  // Legacy JSONPath functions, which never support wildcard and descendant
  // operators.
  const JsonPathFunction legacy_functions[] = {
      {FunctionKind::kJsonExtract, types::JsonType()},
      {FunctionKind::kJsonExtractScalar, types::StringType()},
      {FunctionKind::kJsonExtractArray, types::JsonArrayType()},
      {FunctionKind::kJsonExtractStringArray, types::StringArrayType()},
  };

  for (absl::string_view path : {"$.*", "$[*]", "$..a", "$..*", "$..[*]"}) {
    const Value args[] = {json_val, Value::String(path)};
    for (const JsonPathFunction& fn : standard_functions) {
      EXPECT_THAT(EvalBuiltinScalarFunction(fn.kind, fn.output_type,
                                            &disabled_context, args),
                  StatusIs(absl::StatusCode::kOutOfRange,
                           HasSubstr("Unsupported operator in JSONPath")))
          << BuiltinFunctionCatalog::GetDebugNameByKind(fn.kind) << ": "
          << path;
    }
    for (const JsonPathFunction& fn : legacy_functions) {
      EXPECT_THAT(EvalBuiltinScalarFunction(fn.kind, fn.output_type,
                                            &enabled_context, args),
                  StatusIs(absl::StatusCode::kOutOfRange,
                           HasSubstr("Unsupported operator in JSONPath")))
          << BuiltinFunctionCatalog::GetDebugNameByKind(fn.kind) << ": "
          << path;
    }
  }
  EXPECT_THAT(EvalBuiltinScalarFunction(FunctionKind::kJsonExistsAny,
                                        types::BoolType(), &disabled_context,
                                        {json_val, wildcard_path_array}),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Unsupported operator in JSONPath: *")));
  EXPECT_THAT(EvalBuiltinScalarFunction(FunctionKind::kJsonExistsAll,
                                        types::BoolType(), &disabled_context,
                                        {json_val, descendant_path_array}),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Unsupported operator in JSONPath: ..")));

  // When FEATURE_JSONPATH_WILDCARDS_AND_DESCENDANT is enabled, the SQL standard
  // JSONPath functions evaluate wildcard and descendant paths on JSON inputs,
  // while STRING inputs still reject wildcard and descendant paths.
  EXPECT_THAT(EvalBuiltinScalarFunction(FunctionKind::kJsonExists,
                                        types::BoolType(), &enabled_context,
                                        {json_val, Value::String("$.*")}),
              IsOkAndHolds(Value::Bool(true)));
  EXPECT_THAT(EvalBuiltinScalarFunction(FunctionKind::kJsonExists,
                                        types::BoolType(), &enabled_context,
                                        {json_val, Value::String("$..a")}),
              IsOkAndHolds(Value::Bool(true)));
  EXPECT_THAT(EvalBuiltinScalarFunction(FunctionKind::kJsonExistsAny,
                                        types::BoolType(), &enabled_context,
                                        {json_val, wildcard_path_array}),
              IsOkAndHolds(Value::Bool(true)));
  EXPECT_THAT(EvalBuiltinScalarFunction(FunctionKind::kJsonExistsAll,
                                        types::BoolType(), &enabled_context,
                                        {json_val, descendant_path_array}),
              IsOkAndHolds(Value::Bool(true)));
  EXPECT_THAT(EvalBuiltinScalarFunction(FunctionKind::kJsonQuery,
                                        types::JsonType(), &enabled_context,
                                        {json_val, Value::String("$..a")}),
              IsOkAndHolds(Value::Json(JSONValue(int64_t{1}))));
  EXPECT_THAT(EvalBuiltinScalarFunction(FunctionKind::kJsonValue,
                                        types::StringType(), &enabled_context,
                                        {json_val, Value::String("$..a")}),
              IsOkAndHolds(Value::String("1")));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Value expected_json_array,
                       Value::MakeArray(types::JsonArrayType(),
                                        {Value::Json(JSONValue(int64_t{10})),
                                         Value::Json(JSONValue(int64_t{20}))}));
  EXPECT_THAT(EvalBuiltinScalarFunction(
                  FunctionKind::kJsonQueryArray, types::JsonArrayType(),
                  &enabled_context, {json_val, Value::String("$..c")}),
              IsOkAndHolds(expected_json_array));
  EXPECT_THAT(EvalBuiltinScalarFunction(
                  FunctionKind::kJsonValueArray, types::StringArrayType(),
                  &enabled_context, {json_val, Value::String("$..c")}),
              IsOkAndHolds(values::StringArray({"10", "20"})));

  // Strict bracket index validation (rejecting negative indices and
  // underscores) applies when wildcards or descendants are present even with
  // FEATURE_JSONPATH_WILDCARDS_AND_DESCENDANT enabled, whereas linear paths
  // preserve legacy permissive bracket parsing.
  for (absl::string_view invalid_bracket_path :
       {"$.*[-1]", "$.*[-0]", "$.*[1_2]", "$..[-1]", "$..[-0]", "$..[1_2]"}) {
    const Value args[] = {json_val, Value::String(invalid_bracket_path)};
    for (const JsonPathFunction& fn : standard_functions) {
      EXPECT_THAT(EvalBuiltinScalarFunction(fn.kind, fn.output_type,
                                            &enabled_context, args),
                  StatusIs(absl::StatusCode::kOutOfRange,
                           HasSubstr("Invalid token in JSONPath at:")))
          << BuiltinFunctionCatalog::GetDebugNameByKind(fn.kind) << ": "
          << invalid_bracket_path;
    }
  }
  EXPECT_THAT(EvalBuiltinScalarFunction(FunctionKind::kJsonValue,
                                        types::StringType(), &enabled_context,
                                        {json_val, Value::String("$.c[-0]")}),
              IsOkAndHolds(Value::String("10")));
  EXPECT_THAT(EvalBuiltinScalarFunction(FunctionKind::kJsonValue,
                                        types::StringType(), &enabled_context,
                                        {json_val, Value::String("$.c[0_1]")}),
              IsOkAndHolds(values::NullString()));
  EXPECT_THAT(EvalBuiltinScalarFunction(FunctionKind::kJsonValue,
                                        types::StringType(), &enabled_context,
                                        {json_val, Value::String("$.c[-1]")}),
              IsOkAndHolds(values::NullString()));

  // STRING overloads of standard functions (e.g. JSON_QUERY(STRING, STRING))
  // do not support FEATURE_JSONPATH_WILDCARDS_AND_DESCENDANT.
  const Value str_val =
      Value::String(R"({"store": {"book": [{"price": 10}], "a": 1}})");
  const JsonPathFunction string_standard_functions[] = {
      {FunctionKind::kJsonQuery, types::StringType()},
      {FunctionKind::kJsonValue, types::StringType()},
      {FunctionKind::kJsonQueryArray, types::StringArrayType()},
      {FunctionKind::kJsonValueArray, types::StringArrayType()},
  };
  for (absl::string_view path :
       {"$.*", "$[*]", "$..a", "$.store.*", "$.store.book[*]"}) {
    const Value args[] = {str_val, Value::String(path)};
    for (const JsonPathFunction& fn : string_standard_functions) {
      EXPECT_THAT(EvalBuiltinScalarFunction(fn.kind, fn.output_type,
                                            &enabled_context, args),
                  StatusIs(absl::StatusCode::kOutOfRange,
                           HasSubstr("not supported for STRING input")))
          << BuiltinFunctionCatalog::GetDebugNameByKind(fn.kind) << ": "
          << path;
    }
  }

  // When FEATURE_JSON_QUERY_LAX is also enabled, non-lax wildcard and
  // descendant paths succeed in JSON_QUERY, while lax paths with wildcards or
  // descendants are still rejected.
  EvaluationContext lax_context{/*options=*/{}};
  LanguageOptions lax_options = enabled_options;
  lax_options.EnableLanguageFeature(FEATURE_JSON_QUERY_LAX);
  lax_context.SetLanguageOptions(lax_options);
  EXPECT_THAT(EvalBuiltinScalarFunction(FunctionKind::kJsonQuery,
                                        types::JsonType(), &lax_context,
                                        {json_val, Value::String("$..a")}),
              IsOkAndHolds(Value::Json(JSONValue(int64_t{1}))));
  EXPECT_THAT(EvalBuiltinScalarFunction(FunctionKind::kJsonQuery,
                                        types::JsonType(), &lax_context,
                                        {json_val, Value::String("$.c[*]")}),
              IsOkAndHolds(values::NullJson()));
  for (absl::string_view invalid_lax_path :
       {"lax $.*", "lax $[*]", "lax $..a", "lax recursive $.*",
        "lax recursive $..a"}) {
    EXPECT_THAT(EvalBuiltinScalarFunction(
                    FunctionKind::kJsonQuery, types::JsonType(), &lax_context,
                    {json_val, Value::String(invalid_lax_path)}),
                StatusIs(absl::StatusCode::kOutOfRange))
        << invalid_lax_path;
  }
}

// Pairs of (value, expected result of is_null()).
std::vector<std::pair<Value, bool>> GetSqlNullTestValues() {
  TypeFactory* type_factory = test_values::static_type_factory();
  const ArrayType* array_type = nullptr;
  GOOGLESQL_CHECK_OK(type_factory->MakeArrayType(types::Int64Type(), &array_type));
  const StructType* struct_type = nullptr;
  GOOGLESQL_CHECK_OK(
      type_factory->MakeStructType({{"a", types::Int64Type()}}, &struct_type));
  return {
      // Non-VARIANT values.
      {Value::NullInt64(), true},
      {Value::Int64(1), false},
      {Value::NullJson(), true},
      {Value::Json(JSONValue()), false},
      // SQL NULL VARIANTs.
      {Value::NullVariant(), true},
      {test_values::Variant(Value::NullInt64()), true},
      {test_values::Variant(Value::NullString()), true},
      {test_values::Variant(Value::NullJson()), true},
      {test_values::Variant(Value::Null(array_type)), true},
      {test_values::Variant(Value::Null(struct_type)), true},
      // Non-null VARIANTs.
      {test_values::Variant(Value::Int64(1)), false},
      {test_values::Variant(Value::String("")), false},
      // A Variant null is a value, not a SQL NULL.
      {Value::VariantNull(), false},
      // A VARIANT holding a struct with a NULL field is not a SQL NULL.
      {test_values::Variant(Value::Struct(struct_type, {Value::NullInt64()})),
       false},
  };
}

TEST(ValueIsNullTest, VariantAndNonVariantValues) {
  for (const auto& [value, expected] : GetSqlNullTestValues()) {
    EXPECT_EQ(value.is_null(), expected) << value.DebugString();
  }
}

TEST(IsFunctionTest, IsNullWithVariantValues) {
  IsFunction is_null_fn(FunctionKind::kIsNull, types::BoolType());
  EvaluationContext context{/*options=*/{}};
  for (const auto& [value, expected] : GetSqlNullTestValues()) {
    Value result;
    absl::Status status;
    ASSERT_TRUE(is_null_fn.Eval(/*params=*/{}, /*args=*/{value}, &context,
                                &result, &status))
        << value.DebugString();
    GOOGLESQL_ASSERT_OK(status);
    EXPECT_EQ(result, Value::Bool(expected)) << value.DebugString();
  }
}

}  // namespace

}  // namespace googlesql
