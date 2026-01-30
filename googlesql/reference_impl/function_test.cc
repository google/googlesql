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

#include "googlesql/common/evaluator_registration_utils.h"
#include "googlesql/common/internal_value.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/functions/date_time_util.h"
#include "googlesql/public/interval_value.h"
#include "googlesql/public/json_value.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/types/array_type.h"
#include "googlesql/public/types/map_type.h"
#include "googlesql/public/types/proto_type.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/reference_impl/common.h"
#include "googlesql/reference_impl/evaluation.h"
#include "googlesql/reference_impl/functions/graph.h"
#include "googlesql/reference_impl/functions/hash.h"
#include "googlesql/reference_impl/operator.h"
#include "googlesql/reference_impl/tuple.h"
#include "googlesql/reference_impl/variable_id.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_collation.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "googlesql/testing/test_value.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "googlesql/base/check.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/types/span.h"
#include "googlesql/base/optional_ref.h"

namespace googlesql {

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

  bool kDeterministic = true;
  bool kNonDeterministic = false;
  // Deterministic cases.
  params.push_back({"StringToDouble", Value::String("1.23"),
                    types::DoubleType(), kDeterministic, type_factory});
  params.push_back({"Int64ToString", Value::Int64(123), types::StringType(),
                    kDeterministic, type_factory});
  params.push_back({"StringStringStructToStringDoubleStruct",
                    string_string_struct_value, string_double_struct_type,
                    kDeterministic, type_factory});

  // Non-deterministic cases.
  params.push_back({"DoubleToString", Value::Double(1.23), types::StringType(),
                    kNonDeterministic, type_factory});
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

  return params;
}

INSTANTIATE_TEST_SUITE_P(
    CastDeterminismTestSuite, CastDeterminismTest,
    ::testing::ValuesIn(GetCastDeterminismTestCases()),
    [](const ::testing::TestParamInfo<CastDeterminismTestParams>& info) {
      return info.param.test_name;
    });

}  // namespace

}  // namespace googlesql
