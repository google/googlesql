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

#include "googlesql/public/input_argument_type.h"

#include <algorithm>
#include <string>
#include <vector>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/type.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_join.h"
#include "absl/types/span.h"

namespace googlesql {
namespace {

using ::testing::Optional;

static std::string ArgumentDebugStrings(
    absl::Span<const InputArgumentType> arguments) {
  std::vector<std::string> argument_strings;
  for (const InputArgumentType& argument : arguments) {
    argument_strings.push_back(argument.DebugString(true /* verbose */));
  }
  return absl::StrJoin(argument_strings, ", ");
}

static void TestExpectedArgumentTypeLessPair(const InputArgumentType& type1,
                                             const InputArgumentType& type2) {
  const std::vector<InputArgumentType> expected_arguments = {type1, type2};
  std::vector<InputArgumentType> arguments = {type1, type2};
  std::sort(arguments.begin(), arguments.end(), InputArgumentTypeLess());
  EXPECT_EQ(expected_arguments, arguments)
      << "expected: " << ArgumentDebugStrings(expected_arguments)
      << "\nactual: " << ArgumentDebugStrings(arguments);
  arguments = {type2, type1};
  std::sort(arguments.begin(), arguments.end(), InputArgumentTypeLess());
  EXPECT_EQ(expected_arguments, arguments)
      << "expected: " << ArgumentDebugStrings(expected_arguments)
      << "\nactual: " << ArgumentDebugStrings(arguments);
}

TEST(InputArgumentTypeTests, TestInputArgumentTypeLess) {
  Value null_int64_value = Value::NullInt64();
  Value null_bool_value = Value::NullBool();
  Value literal_int64_value_1 = Value::Int64(1);
  Value literal_int64_value_2 = Value::Int64(2);
  Value literal_bool_value = Value::Bool(true);

  InputArgumentType untyped_null;
  InputArgumentType null_int64(null_int64_value);
  InputArgumentType literal_int64_1(literal_int64_value_1);
  InputArgumentType literal_int64_2(literal_int64_value_2);
  InputArgumentType parameter_int64(
      types::Int64Type(), true /* is_parameter */);
  InputArgumentType non_literal_int64(types::Int64Type());

  InputArgumentType literal_bool(literal_bool_value);

  // Different type kinds order the same regardless of non-literal, literal,
  // null, etc.
  TestExpectedArgumentTypeLessPair(non_literal_int64, literal_bool);
  TestExpectedArgumentTypeLessPair(parameter_int64,   literal_bool);
  TestExpectedArgumentTypeLessPair(literal_int64_1,   literal_bool);
  TestExpectedArgumentTypeLessPair(null_int64,        literal_bool);
  TestExpectedArgumentTypeLessPair(untyped_null,      literal_bool);

  // For a single type kind, non-literals order before literals and nulls.
  TestExpectedArgumentTypeLessPair(non_literal_int64, literal_int64_1);
  TestExpectedArgumentTypeLessPair(parameter_int64,   literal_int64_1);
  TestExpectedArgumentTypeLessPair(non_literal_int64, null_int64);
  TestExpectedArgumentTypeLessPair(parameter_int64,   null_int64);
  TestExpectedArgumentTypeLessPair(non_literal_int64, untyped_null);
  TestExpectedArgumentTypeLessPair(parameter_int64,   untyped_null);

  // Literals before nulls.
  TestExpectedArgumentTypeLessPair(literal_int64_1,   null_int64);
  TestExpectedArgumentTypeLessPair(literal_int64_1,   untyped_null);

  // Non-literals order together (both parameters and non-parameters).
  // Neither is less than the other.
  EXPECT_FALSE(
      InputArgumentTypeLess()(parameter_int64, non_literal_int64));
  EXPECT_FALSE(
      InputArgumentTypeLess()(non_literal_int64, parameter_int64));

  // Literals with different values order together.
  EXPECT_FALSE(
      InputArgumentTypeLess()(literal_int64_1, literal_int64_2));
  EXPECT_FALSE(
      InputArgumentTypeLess()(literal_int64_2, literal_int64_1));

  // Nulls order together (both typed and untyped).
  EXPECT_FALSE(
      InputArgumentTypeLess()(null_int64, untyped_null));
  EXPECT_FALSE(
      InputArgumentTypeLess()(untyped_null, null_int64));

  // InputArgumentTypes are not less than themselves.
  EXPECT_FALSE(
      InputArgumentTypeLess()(non_literal_int64, non_literal_int64));
  EXPECT_FALSE(
      InputArgumentTypeLess()(parameter_int64, parameter_int64));
  EXPECT_FALSE(
      InputArgumentTypeLess()(literal_int64_1, literal_int64_1));
  EXPECT_FALSE(
      InputArgumentTypeLess()(null_int64, null_int64));
  EXPECT_FALSE(
      InputArgumentTypeLess()(untyped_null, untyped_null));

  // Two complex types with the same kind and in the same equivalence
  // class sort via DebugString().
  TypeFactory type_factory;
  const EnumType* enum_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeEnumType(
      googlesql_test::TestEnum_descriptor(), &enum_type));
  const Value enum_value(values::Enum(enum_type, 1));
  const EnumType* another_enum_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeEnumType(
      googlesql_test::AnotherTestEnum_descriptor(), &another_enum_type));
  const Value another_enum_value(values::Enum(another_enum_type, 1));

  TestExpectedArgumentTypeLessPair(InputArgumentType(enum_type),
                                   InputArgumentType(enum_value));
  TestExpectedArgumentTypeLessPair(InputArgumentType(another_enum_type),
                                   InputArgumentType(enum_type));
  TestExpectedArgumentTypeLessPair(InputArgumentType(another_enum_value),
                                   InputArgumentType(enum_value));

  const StructType* struct_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeStructType(
      {{"a", type_factory.get_string()}, {"b", type_factory.get_int32()}},
      &struct_type));
  const Value struct_value(
      values::Struct(struct_type, {values::String("x"), values::Int32(1)}));
  const StructType* another_struct_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeStructType(
      {{"c", type_factory.get_int32()}, {"d", type_factory.get_string()}},
      &another_struct_type));
  const Value another_struct_value(
      values::Struct(another_struct_type,
                     {values::Int32(1), values::String("x")}));

  TestExpectedArgumentTypeLessPair(InputArgumentType(struct_type),
                                   InputArgumentType(struct_value));
  TestExpectedArgumentTypeLessPair(InputArgumentType(struct_type),
                                   InputArgumentType(another_struct_type));
  TestExpectedArgumentTypeLessPair(InputArgumentType(struct_value),
                                   InputArgumentType(another_struct_value));

  const ArrayType* array_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeArrayType(type_factory.get_int64(), &array_type));
  const Value array_value(values::Array(array_type, {values::Int64(1)}));
  const ArrayType* another_array_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeArrayType(type_factory.get_int32(),
                                       &another_array_type));
  const Value another_array_value(values::Array(another_array_type,
                                                {values::Int32(1)}));

  TestExpectedArgumentTypeLessPair(InputArgumentType(array_type),
                                   InputArgumentType(array_value));
  TestExpectedArgumentTypeLessPair(InputArgumentType(another_array_type),
                                   InputArgumentType(array_type));
  TestExpectedArgumentTypeLessPair(InputArgumentType(another_array_value),
                                   InputArgumentType(array_value));

  const ProtoType* proto_type;
  GOOGLESQL_ASSERT_OK(
      type_factory.MakeProtoType(googlesql_test::KitchenSinkPB::descriptor(),
                                 &proto_type));

  const Value proto_value(values::Proto(proto_type, absl::Cord("a")));

  const ProtoType* another_proto_type;
  GOOGLESQL_ASSERT_OK(
      type_factory.MakeProtoType(googlesql_test::TestExtraPB::descriptor(),
                                 &another_proto_type));
  const Value another_proto_value(
      values::Proto(another_proto_type, absl::Cord("a")));

  TestExpectedArgumentTypeLessPair(InputArgumentType(proto_type),
                                   InputArgumentType(proto_value));
  TestExpectedArgumentTypeLessPair(InputArgumentType(proto_type),
                                   InputArgumentType(another_proto_type));
  TestExpectedArgumentTypeLessPair(InputArgumentType(proto_value),
                                   InputArgumentType(another_proto_value));
}

TEST(InputArgumentTypeTest, TypeNameAndDebugString) {
  TypeFactory type_factory;
  const Value int64_value = values::Int64(5);
  const googlesql::StructType* struct_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory.MakeStructType({{"x", types::Int64Type()},
                                         {"y", types::StringType()},
                                         {"z", types::DoubleType()}},
                                        &struct_type));

  struct TypeAndOutputs {
    InputArgumentType argument_type;
    std::string expected_external_type_name;
    std::string expected_debug_string;
  };
  const std::vector<TypeAndOutputs> test_cases = {
      {InputArgumentType::UntypedNull(), "NULL", "NULL"},
      {InputArgumentType(int64_value), "INT64", "literal INT64"},
      {InputArgumentType(types::DoubleType()), "FLOAT64", "DOUBLE"},
      {InputArgumentType(types::DoubleType(), true /* is_query_parameter */),
       "FLOAT64", "DOUBLE"},
      {InputArgumentType::LambdaInputArgumentType(), "LAMBDA", "LAMBDA"},
  };

  for (const auto& test_case : test_cases) {
    const InputArgumentType& argument_type = test_case.argument_type;
    SCOPED_TRACE(argument_type.DebugString());

    EXPECT_EQ(test_case.expected_external_type_name,
              argument_type.UserFacingName(PRODUCT_EXTERNAL));
    EXPECT_EQ(test_case.expected_debug_string, argument_type.DebugString());
  }
}

TEST(InputArgumentTypeTest, LambdaIsLambda) {
  EXPECT_TRUE(InputArgumentType::LambdaInputArgumentType().is_lambda());
}

TEST(InputArgumentTypeTest, LongArgumentsString) {
  TypeFactory type_factory;
  const googlesql::StructType* struct_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory.MakeStructType({{"x", types::Int64Type()},
                                         {"y", types::StringType()},
                                         {"z", types::DoubleType()}},
                                        &struct_type));
  std::vector<InputArgumentType> argument_types;
  for (int i = 0; i < 500; ++i) {
    argument_types.push_back(InputArgumentType(struct_type));
  }
  const std::string argument_type_string =
      InputArgumentType::ArgumentsToString(argument_types);
  EXPECT_LT(argument_type_string.size(), argument_types.size() * 10);
  EXPECT_THAT(argument_type_string, testing::EndsWith("..."));
}

TEST(InputArgumentTypeTest, ArgumentAlias) {
  InputArgumentType input_argument_type;
  IdString alias = IdString::MakeGlobal("alias");
  input_argument_type.set_argument_alias(alias);
  EXPECT_THAT(input_argument_type.argument_alias(), Optional(alias));
}

TEST(InputArgumentTypeTest, ChainedInput) {
  InputArgumentType arg(types::Int64Type());
  EXPECT_EQ("INT64", arg.DebugString());
  EXPECT_EQ(InputArgumentType::ArgumentsToString({arg, arg}, PRODUCT_EXTERNAL,
                                                 {"", "named"}),
            "INT64, named => INT64");

  arg.set_is_chained_function_call_input();

  EXPECT_EQ("chained_function_call_input INT64", arg.DebugString());
  EXPECT_EQ(InputArgumentType::ArgumentsToString({arg, arg}, PRODUCT_EXTERNAL,
                                                 {"", "named"}),
            "INT64 (from chained function call input), "
            "named => INT64 (from chained function call input)");
}

TEST(InputArgumentTypeTest, Relation) {
  TVFRelation relation(/*columns=*/{});
  InputArgumentType arg =
      InputArgumentType::RelationInputArgumentType(relation);
  EXPECT_TRUE(arg.is_relation());
  EXPECT_TRUE(arg.has_relation_input_schema());
  EXPECT_EQ(arg.relation_input_schema(), relation);
  EXPECT_FALSE(arg.is_pipe_input_table());
  EXPECT_EQ(arg.DebugString(), "RELATION");
  EXPECT_EQ(arg.UserFacingName(PRODUCT_EXTERNAL), "TABLE<>");

  InputArgumentType arg2 = InputArgumentType::RelationInputArgumentType(
      relation, /*is_pipe_input_table=*/true);
  EXPECT_TRUE(arg2.is_relation());
  EXPECT_TRUE(arg2.has_relation_input_schema());
  EXPECT_EQ(arg2.relation_input_schema(), relation);
  EXPECT_TRUE(arg2.is_pipe_input_table());
  EXPECT_EQ(arg2.DebugString(), "RELATION(is_pipe_input_table)");
  EXPECT_EQ(arg2.UserFacingName(PRODUCT_EXTERNAL), "TABLE<>");

  EXPECT_EQ(InputArgumentType::ArgumentsToString({arg, arg2}, PRODUCT_EXTERNAL,
                                                 {"", ""}),
            "TABLE<>, TABLE<> (from pipe input)");
  EXPECT_EQ(InputArgumentType::ArgumentsToString({arg, arg2}, PRODUCT_EXTERNAL,
                                                 {"n1", "n2"}),
            "n1 => TABLE<>, n2 => TABLE<> (from pipe input)");
}

}  // namespace
}  // namespace googlesql
