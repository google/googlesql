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

#include "googlesql/public/value.h"

#include <sys/types.h>
#include <time.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "google/protobuf/wrappers.pb.h"
#include "googlesql/public/simple_token_list.h"
#include "googlesql/common/float_margin.h"
#include "googlesql/common/internal_value.h"
#include "googlesql/common/testing/testing_proto_util.h"
#include "googlesql/compliance/functions_testlib_common.h"
#include "googlesql/public/analyzer.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/civil_time.h"
#include "googlesql/public/evaluator.h"
#include "googlesql/public/interval_value.h"
#include "googlesql/public/interval_value_test_util.h"
#include "googlesql/public/json_value.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/numeric_value.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/pico_time.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/timestamp_picos_value.h"
#include "googlesql/public/token_list_util.h"
#include "googlesql/public/type.h"
#include "googlesql/public/types/measure_type.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/types/value_equality_check_options.h"
#include "googlesql/public/uuid_value.h"
#include "googlesql/testdata/test_proto3.pb.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "googlesql/testing/test_value.h"
#include "googlesql/testing/using_test_value.cc"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/hash/hash.h"
#include "absl/hash/hash_testing.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/descriptor_database.h"
#include "google/protobuf/dynamic_message.h"
#include "google/protobuf/io/coded_stream.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"
#include "google/protobuf/io/zero_copy_stream_impl_lite.h"
#include "google/protobuf/text_format.h"
#include "google/protobuf/wire_format_lite.h"
#include "googlesql/common/testing/proto_matchers.h"
#include "googlesql/public/types/value_representations.h"
#include "googlesql/base/testing/status_matchers.h"

namespace googlesql {

namespace {

using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::google::protobuf::internal::WireFormatLite;
using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::EndsWith;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::Pair;

using interval_testing::Days;
using interval_testing::Micros;
using interval_testing::Months;
using interval_testing::MonthsDaysMicros;
using interval_testing::MonthsDaysNanos;
using interval_testing::Nanos;

void TestHashEqual(const Value& a, const Value& b) {
  EXPECT_EQ(a.HashCode(), b.HashCode()) << "\na: " << a << "\n"
                                        << "b: " << b;
  EXPECT_EQ(absl::Hash<Value>()(a), absl::Hash<Value>()(b))
      << "\na: " << a << "\n"
      << "b: " << b;
}
void TestHashNotEqual(const Value& a, const Value& b) {
  EXPECT_NE(a.HashCode(), b.HashCode()) << "\na: " << a << "\n"
                                        << "b: " << b;
  EXPECT_NE(absl::Hash<Value>()(a), absl::Hash<Value>()(b))
      << "\na: " << a << "\n"
      << "b: " << b;
}

MATCHER_P(ValueAndHashEq, other_value,
          absl::StrFormat("equal value and hash code to %s",
                          other_value.DebugString(true))) {
  *result_listener << absl::StrFormat("aka %s", arg.DebugString(true));
  return arg.Equals(other_value) && other_value.Equals(arg) &&
         arg.HashCode() == other_value.HashCode() &&
         absl::Hash<Value>()(arg) == absl::Hash<Value>()(other_value);
}

MATCHER_P(ValueAndHashNotEq, other_value,
          absl::StrFormat("value inequality and unequal hash code with %s",
                          other_value.DebugString())) {
  *result_listener << absl::StrFormat("aka %s", arg.DebugString(true));
  return !arg.Equals(other_value) && !other_value.Equals(arg) &&
         arg.HashCode() != other_value.HashCode() &&
         absl::Hash<Value>()(arg) != absl::Hash<Value>()(other_value);
}

absl::Time ParseTimeWithFormat(absl::string_view format,
                               absl::string_view time_literal) {
  absl::Time time;
  std::string error;
  const bool successful = absl::ParseTime(format, time_literal, &time, &error);
  ABSL_QCHECK(successful) << error;
  return time;
}

absl::Time ParseTimeHm(absl::string_view str) {
  return ParseTimeWithFormat("%H:%M", str);
}

MATCHER_P(MapEntriesWhere, matcher, "") {
  return ::testing::ExplainMatchResult(matcher, arg.map_entries(),
                                       result_listener);
}

}  // namespace

// Test that GetSQL returns a string that can be re-parsed as the Value.
// Returns <value> so this can be used as a chained call anywhere we
// construct a Value.
// Also tests that GetSQLLiteral returns a string that can be reparsed as
// a Value (which should be approximately equal to the original) where
// possible.
static Value TestGetSQL(const Value& value) {
  // Make all compiled-in proto type names visible.
  SimpleCatalog catalog("type_catalog");
  catalog.SetDescriptorPool(
      googlesql_test::KitchenSinkPB::descriptor()->file()->pool());

  AnalyzerOptions analyzer_options;
  analyzer_options.mutable_language()->EnableLanguageFeature(
      FEATURE_NUMERIC_TYPE);
  analyzer_options.mutable_language()->EnableLanguageFeature(
      FEATURE_BIGNUMERIC_TYPE);
  analyzer_options.mutable_language()->EnableLanguageFeature(FEATURE_JSON_TYPE);
  analyzer_options.mutable_language()->EnableLanguageFeature(
      FEATURE_INTERVAL_TYPE);
  analyzer_options.mutable_language()->EnableLanguageFeature(
      FEATURE_TOKENIZED_SEARCH);
  analyzer_options.mutable_language()->EnableLanguageFeature(
      FEATURE_RANGE_TYPE);
  analyzer_options.mutable_language()->EnableLanguageFeature(FEATURE_GEOGRAPHY);
  analyzer_options.mutable_language()->EnableLanguageFeature(
      FEATURE_EXTENDED_GEOGRAPHY_PARSERS);
  analyzer_options.mutable_language()->EnableLanguageFeature(
      FEATURE_NAMED_ARGUMENTS);
  analyzer_options.mutable_language()->EnableLanguageFeature(FEATURE_MAP_TYPE);

  catalog.AddBuiltinFunctions(
      BuiltinFunctionOptions(analyzer_options.language()));

  const bool testable_type = !value.type()->IsProto();
  // Test round tripping GetSQL for non-legacy types.
  if (testable_type) {
    const std::string sql = value.GetSQL();
    GOOGLESQL_VLOG(1) << "sql: " << sql;

    // We have to use the expression evaluator rather than just the analyzer
    // because the returned SQL may be a non-literal.
    PreparedExpression expr(sql);
    const absl::Status prepare_status =
        expr.Prepare(analyzer_options, &catalog);
    GOOGLESQL_EXPECT_OK(prepare_status)
        << "Value: " << value.DebugString() << "\nSQL: " << sql;

    if (prepare_status.ok()) {
      const absl::StatusOr<Value> result = expr.Execute();
      GOOGLESQL_EXPECT_OK(result.status()) << value.DebugString();
      if (result.ok()) {
        const Value& new_value = result.value();
        EXPECT_TRUE(value.Equals(new_value))
            << "Value: " << value.FullDebugString() << "\nSQL: " << sql
            << "\nRe-parsed: " << new_value.FullDebugString();
        EXPECT_EQ(value.DebugString(), new_value.DebugString());
      }
    }
  }

  // Test round tripping GetSQLLiteral for non-legacy types.
  if (testable_type) {
    const std::string sql = value.GetSQLLiteral();
    GOOGLESQL_VLOG(1) << "sql literal: " << sql;

    // For GetSQLLiteral, we re-parse without a catalog because we don't
    // expect any type names to show up.  e.g. For enums, we'll just get
    // a string.
    {
      PreparedExpression expr(sql);
      const absl::Status prepare_status = expr.Prepare(analyzer_options);
      GOOGLESQL_EXPECT_OK(prepare_status)
          << "Value: " << value.DebugString() << "\nSQL: " << sql;

      if (prepare_status.ok()) {
        const absl::StatusOr<Value> result = expr.Execute();
        GOOGLESQL_EXPECT_OK(result.status()) << value.DebugString();
        if (result.ok()) {
          const Value& new_value = result.value();
          GOOGLESQL_VLOG(1) << "New value: " << new_value.DebugString();
          if (value.type()->Equivalent(new_value.type())) {
            EXPECT_TRUE(value.Equals(new_value))
                << "Value: " << value.FullDebugString()
                << "\nSQL literal: " << sql
                << "\nRe-parsed: " << new_value.FullDebugString();
            // Note that for STRUCTs, GetSQLLiteral returns a string that
            // does not contain field name information.  So for STRUCTS,
            // we might get a new_value that has an equivalent type, but
            // not an Equals type.  In that case, the DebugString would not
            // match so we only compare DebugStrings if the Values are equals
            // and Types are Equals.
            if (value.type()->Equals(new_value.type())) {
              EXPECT_EQ(value.DebugString(), new_value.DebugString())
                  << "value: " << value.FullDebugString()
                  << "\nnew_value: " << new_value.FullDebugString();
            }
          }
        }
      }
    }

    bool is_bad_type = false;
    // Exclude incomplete protos, which we won't be able to cast from
    // string back to proto type.
    if (value.type()->IsProto() && !value.is_null()) {
      google::protobuf::DynamicMessageFactory message_factory;
      std::unique_ptr<google::protobuf::Message> m(value.ToMessage(&message_factory));
      if (!m->IsInitialized()) {
        is_bad_type = true;
      }
    }
    // Exclude arrays of non-widest-numeric types because we don't support
    // casts of arrays.
    if (value.type()->IsArray() && !value.is_null()) {
      const Type* element_type = value.type()->AsArray()->element_type();
      if (!(element_type->IsInt64() || element_type->IsString() ||
            element_type->IsBytes() || element_type->IsDouble())) {
        is_bad_type = true;
      }
    }

    // Now try parsing the SQL literal with a CAST to the appropriate type.
    // This should give back the original value.
    // Some types are excluded because the CASTs won't be legal.
    if (!is_bad_type) {
      const std::string cast_expr = absl::StrCat(
          "CAST(", sql, " AS ", value.type()->TypeName(PRODUCT_INTERNAL), ")");
      PreparedExpression expr(cast_expr);
      const absl::Status prepare_status =
          expr.Prepare(analyzer_options, &catalog);
      GOOGLESQL_EXPECT_OK(prepare_status)
          << "Value: " << value.DebugString() << "\nSQL: " << cast_expr;

      if (prepare_status.ok()) {
        const absl::StatusOr<Value> result = expr.Execute();
        GOOGLESQL_EXPECT_OK(result.status()) << value.DebugString();
        if (result.ok()) {
          const Value& new_value = result.value();
          EXPECT_TRUE(value.Equals(new_value))
              << "Value: " << value.FullDebugString()
              << "\nSQL literal: " << sql << "\nCAST expr: " << cast_expr
              << "\nRe-parsed: " << new_value.FullDebugString();
          EXPECT_EQ(value.DebugString(), new_value.DebugString());
        }
      }
    }
  }

  return value;
}

class ValueTest : public ::testing::Test {
 public:
  ValueTest() : type_factory_(TypeFactoryOptions().IgnoreValueLifeCycle()) {}
  ValueTest(const ValueTest&) = delete;
  ValueTest& operator=(const ValueTest&) = delete;
  ~ValueTest() override = default;

  const EnumType* GetTestEnumType() {
    const EnumType* enum_type;
    const google::protobuf::EnumDescriptor* enum_descriptor =
        googlesql_test::TestEnum_descriptor();
    GOOGLESQL_QCHECK_OK(type_factory_.MakeEnumType(enum_descriptor, &enum_type));
    return enum_type;
  }
  const EnumType* GetTestProto3EnumType() {
    const EnumType* enum_type;
    const google::protobuf::EnumDescriptor* enum_descriptor =
        googlesql_test::TestProto3Enum_descriptor();
    GOOGLESQL_QCHECK_OK(type_factory_.MakeEnumType(enum_descriptor, &enum_type));
    return enum_type;
  }
  const EnumType* GetOtherTestEnumType() {
    const EnumType* enum_type;
    const google::protobuf::EnumDescriptor* enum_descriptor =
        googlesql_test::AnotherTestEnum_descriptor();
    GOOGLESQL_QCHECK_OK(type_factory_.MakeEnumType(enum_descriptor, &enum_type));
    return enum_type;
  }

  const ArrayType* GetTestArrayType(const Type* element_type) {
    const ArrayType* array_type;
    GOOGLESQL_QCHECK_OK(type_factory_.MakeArrayType(element_type, &array_type));
    return array_type;
  }

  // Cannot just use MakeArrayType(GetTestEnumType()), because ArrayType needs
  // to be created by the same TypeFactory as EnumType.
  const ArrayType* GetTestArrayEnumType() {
    return GetTestArrayType(GetTestEnumType());
  }

  const ProtoType* GetTestProtoType() {
    googlesql_test::KitchenSinkPB kitchen_sink;
    return test_values::MakeProtoType(kitchen_sink.GetDescriptor());
  }

  const ProtoType* GetOtherTestProtoType() {
    googlesql_test::TestExtraPB test_extra;
    return test_values::MakeProtoType(test_extra.GetDescriptor());
  }

  const StructType* GetTestStructType(absl::Span<const StructField> fields) {
    const StructType* struct_type = nullptr;
    GOOGLESQL_CHECK_OK(type_factory_.MakeStructType(fields, &struct_type));
    return struct_type;
  }

  const MeasureType* GetTestMeasureType(const Type* result_type) {
    auto measure_type = type_factory_.MakeMeasureType(result_type);
    GOOGLESQL_CHECK_OK(measure_type);
    return measure_type.value()->AsMeasure();
  }

  void TestParameterizedValueAfterReleaseOfTypeFactory(
      bool keep_type_alive_while_referenced_from_value);

 private:
  TypeFactory type_factory_;
};

TEST_F(ValueTest, BoolFormatting) {
  EXPECT_EQ(Value::Bool(true).DebugString(/*verbose=*/true), "Bool(true)");
  EXPECT_EQ(Value::Bool(true).DebugString(), "true");
  EXPECT_EQ(Value::Bool(true).Format(), "Bool(true)");
  EXPECT_EQ(Value::Bool(true).Format(/*print_top_level_type=*/false), "true");
  EXPECT_EQ(Value::Bool(true).GetSQLLiteral(), "true");
  EXPECT_EQ(Value::Bool(true).GetSQL(), "true");
}

TEST_F(ValueTest, Int32Formatting) {
  EXPECT_EQ(Value::Int32(123).DebugString(/*verbose=*/true), "Int32(123)");
  EXPECT_EQ(Value::Int32(123).DebugString(), "123");
  EXPECT_EQ(Value::Int32(123).Format(), "Int32(123)");
  EXPECT_EQ(Value::Int32(123).Format(/*print_top_level_type=*/false), "123");
  EXPECT_EQ(Value::Int32(123).GetSQLLiteral(), "123");
  EXPECT_EQ(Value::Int32(123).GetSQL(), "CAST(123 AS INT32)");
}

TEST_F(ValueTest, Int64Null) {
  Value value = TestGetSQL(Value::NullInt64());
  EXPECT_EQ("INT64", value.type()->DebugString());
  EXPECT_TRUE(value.is_null());
  EXPECT_DEATH(value.int64_value(), "Null value");
  Value value_copy = value;
  EXPECT_EQ("INT64", value_copy.type()->DebugString());
  EXPECT_TRUE(value_copy.is_null());
}

TEST_F(ValueTest, Int64NonNull) {
  Value value = TestGetSQL(Value::Int64(3));
  EXPECT_EQ("INT64", value.type()->DebugString());
  EXPECT_TRUE(!value.is_null());
  EXPECT_EQ(3, value.int64_value());
  Value value_copy = value;
  EXPECT_EQ("INT64", value_copy.type()->DebugString());
  EXPECT_TRUE(!value_copy.is_null());
  EXPECT_EQ(3, value_copy.int64_value());
}

TEST_F(ValueTest, Int64Formatting) {
  EXPECT_EQ(Value::Int64(456).DebugString(/*verbose=*/true), "Int64(456)");
  EXPECT_EQ(Value::Int64(456).DebugString(), "456");
  EXPECT_EQ(Value::Int64(456).Format(), "Int64(456)");
  EXPECT_EQ(Value::Int64(456).Format(/*print_top_level_type=*/false), "456");
  EXPECT_EQ(Value::Int64(456).GetSQLLiteral(), "456");
  EXPECT_EQ(Value::Int64(456).GetSQL(), "456");
}

TEST_F(ValueTest, FloatNull) {
  Value value = TestGetSQL(Value::NullFloat());
  EXPECT_EQ("FLOAT", value.type()->DebugString());
  EXPECT_TRUE(value.is_null());
  EXPECT_DEATH(value.float_value(), "Null value");
  EXPECT_EQ("NULL", value.DebugString());
  EXPECT_EQ("Float(NULL)", value.FullDebugString());
  EXPECT_EQ("NULL", value.GetSQLLiteral());
  EXPECT_EQ("CAST(NULL AS FLOAT32)", value.GetSQL());
  EXPECT_EQ("CAST(NULL AS FLOAT)", value.GetSQL(PRODUCT_INTERNAL));
  EXPECT_EQ("CAST(NULL AS FLOAT)",
            value.GetSQL(PRODUCT_INTERNAL, /*use_external_float32=*/true));
  EXPECT_EQ("CAST(NULL AS FLOAT32)", value.GetSQL(PRODUCT_EXTERNAL));
  EXPECT_EQ("CAST(NULL AS FLOAT32)",
            value.GetSQL(PRODUCT_EXTERNAL, /*use_external_float32=*/true));
}

TEST_F(ValueTest, FloatNonNull) {
  TestGetSQL(Value::Float(3));
  TestGetSQL(Value::Float(3.5));
  TestGetSQL(Value::Float(3.0000004));
  TestGetSQL(Value::Float(.0000004));
  TestGetSQL(Value::Float(-55500000000));
  TestGetSQL(Value::Float(-0.000034634643));
  TestGetSQL(Value::Float(1.5e25));
  TestGetSQL(Value::Float(-1.5e25));
  TestGetSQL(Value::Float(std::numeric_limits<float>::quiet_NaN()));
  TestGetSQL(Value::Float(std::numeric_limits<float>::infinity()));
  TestGetSQL(Value::Float(-std::numeric_limits<float>::infinity()));
}

TEST_F(ValueTest, FloatFormatting) {
  EXPECT_EQ(Value::Float(123.456).DebugString(/*verbose=*/true),
            "Float(123.456)");
  EXPECT_EQ(Value::Float(123.456).DebugString(), "123.456");
  EXPECT_EQ(Value::Float(123.456).Format(), "Float(123.456)");
  EXPECT_EQ(Value::Float(123.456).Format(/*print_top_level_type=*/false),
            "123.456");
  EXPECT_EQ(Value::Float(123.456).GetSQLLiteral(), "123.456");
  EXPECT_EQ(Value::Float(123.456).GetSQL(), "CAST(123.456 AS FLOAT32)");
  EXPECT_EQ(Value::Float(123.456).GetSQL(PRODUCT_INTERNAL),
            "CAST(123.456 AS FLOAT)");
  EXPECT_EQ(Value::Float(123.456).GetSQL(PRODUCT_EXTERNAL,
                                         /*use_external_float32=*/true),
            "CAST(123.456 AS FLOAT32)");

  EXPECT_EQ(Value::Float(123.456).GetSQLLiteral(PRODUCT_INTERNAL), "123.456");
  EXPECT_EQ(Value::Float(123.456).GetSQLLiteral(PRODUCT_INTERNAL,
                                                /*use_external_float32=*/true),
            "123.456");
  EXPECT_EQ(Value::Float(123.456).GetSQLLiteral(PRODUCT_EXTERNAL,
                                                /*use_external_float32=*/true),
            "123.456");
}

TEST_F(ValueTest, DoubleNull) {
  Value value = TestGetSQL(Value::NullDouble());
  EXPECT_EQ("DOUBLE", value.type()->DebugString());
  EXPECT_TRUE(value.is_null());
  EXPECT_DEATH(value.double_value(), "Null value");
  EXPECT_EQ("NULL", value.DebugString());
  EXPECT_EQ("Double(NULL)", value.FullDebugString());
  EXPECT_EQ("CAST(NULL AS FLOAT64)", value.GetSQL());
  EXPECT_EQ("CAST(NULL AS FLOAT64)", value.GetSQL(PRODUCT_EXTERNAL));
  EXPECT_EQ("CAST(NULL AS DOUBLE)", value.GetSQL(PRODUCT_INTERNAL));
}

TEST_F(ValueTest, DoubleNonNull) {
  TestGetSQL(Value::Double(3));
  TestGetSQL(Value::Double(3.5));
  TestGetSQL(Value::Double(3.000000000004));
  TestGetSQL(Value::Double(.000000000004));
  TestGetSQL(Value::Double(-55500000000));
  TestGetSQL(Value::Double(-0.000034634643));
  TestGetSQL(Value::Double(1.5e50));
  TestGetSQL(Value::Double(-1.5e50));
  TestGetSQL(Value::Double(std::numeric_limits<double>::quiet_NaN()));
  TestGetSQL(Value::Double(std::numeric_limits<double>::infinity()));
  TestGetSQL(Value::Double(-std::numeric_limits<double>::infinity()));
}

TEST_F(ValueTest, DoubleFloatToString) {
  // Test that DebugString and GetSQL for doubles and floats returns values
  // that are clearly floating point, not integers (e.g. they have dots).
  const Value values[] = {
      Value::Double(0.0), Value::Double(1.0),  Value::Double(-1.0),
      Value::Double(1.5), Value::Double(-1.5), Value::Double(.5),
      Value::Double(-.5), Value::Double(1e20), Value::Double(-1e20),
      Value::Float(0.0),  Value::Float(1.0),   Value::Float(-1.0),
      Value::Float(1.5),  Value::Float(-1.5),  Value::Float(.5),
      Value::Float(-.5),  Value::Float(1e20),  Value::Float(-1e20)};
  for (const Value& value : values) {
    TestGetSQL(value);

    double d;
    int64_t i;
    const std::string str = value.GetSQL();
    // strtod will fail for float because GetSQL() will have a CAST.
    EXPECT_EQ(value.type_kind() == TYPE_DOUBLE, absl::SimpleAtod(str, &d))
        << str;
    // Neither float or double GetSQL() should return a valid integer string.
    EXPECT_FALSE(absl::SimpleAtoi(str, &i)) << str;
  }
}

TEST_F(ValueTest, DoubleFormatting) {
  EXPECT_EQ(Value::Double(123.456).DebugString(/*verbose=*/true),
            "Double(123.456)");
  EXPECT_EQ(Value::Double(123.456).DebugString(), "123.456");
  EXPECT_EQ(Value::Double(123.456).Format(), "Double(123.456)");
  EXPECT_EQ(Value::Double(123.456).Format(/*print_top_level_type=*/false),
            "123.456");
  EXPECT_EQ(Value::Double(123.456).GetSQLLiteral(), "123.456");
  EXPECT_EQ(Value::Double(123.456).GetSQL(), "123.456");
}

TEST_F(ValueTest, StringNull) {
  Value value = TestGetSQL(Value::NullString());
  EXPECT_EQ("STRING", value.type()->DebugString());
  EXPECT_TRUE(value.is_null());
  EXPECT_DEATH(value.string_value(), "Null value");
  Value value_copy = value;
  EXPECT_EQ("STRING", value_copy.type()->DebugString());
  EXPECT_TRUE(value_copy.is_null());
}

TEST_F(ValueTest, StringNonNull) {
  Value value = TestGetSQL(Value::String("foo"));
  EXPECT_EQ("STRING", value.type()->DebugString());
  EXPECT_TRUE(!value.is_null());
  EXPECT_EQ("foo", value.string_value());
  Value value_copy = value;
  EXPECT_EQ("STRING", value_copy.type()->DebugString());
  EXPECT_TRUE(!value_copy.is_null());
  EXPECT_EQ("foo", value_copy.string_value());
}

void disguised_move(Value& o1, Value& o2) {  // NOLINT
  o1 = std::move(o2);
}

TEST_F(ValueTest, CopyAssignMove) {
  Value expected_value = Value::String("foo");
  Value value = Value::String("foo");
  Value value_copy = value;
  Value another_copy(value_copy);
  Value moved_copy(Value::String("foo"));
  Value assigned_copy = Value::String("foo");
  value_copy = *&value_copy;  // self-copy
  Value moved_value(Value::String("foo"));
  Value moved_assigned_value = Value::String("foo");
  Value to_be_destroyed_value = Value::String("foo");
  Value stolen_value;
  stolen_value = std::move(to_be_destroyed_value);
  EXPECT_EQ(expected_value, value_copy);
  EXPECT_EQ(expected_value, another_copy);
  EXPECT_EQ(expected_value, moved_copy);
  EXPECT_EQ(expected_value, assigned_copy);
  EXPECT_EQ(expected_value, moved_value);
  EXPECT_EQ(expected_value, moved_assigned_value);
  EXPECT_EQ(expected_value, stolen_value);
  EXPECT_DEATH(to_be_destroyed_value.type(), "Uninitialized value");

  // Self-move of an rvalue reference should not crash.
  Value obj = Value::String("foo");
  disguised_move(obj, obj);
  EXPECT_FALSE(obj.is_valid());
}

TEST_F(ValueTest, StringDebugString) {
  // Strings and bytes get escaped as printable googlesql literals.
  EXPECT_EQ("\"abc\"", Value::String("abc").DebugString());
  EXPECT_EQ("'ab\"c'", Value::String("ab\"c").DebugString());
  EXPECT_EQ("\"ab'c\"", Value::String("ab'c").DebugString());
  EXPECT_EQ("\"ab\\x01 c\"",
            TestGetSQL(Value::String("ab\x01 c")).DebugString());
  EXPECT_EQ("\"ab\\x01 c\"",
            TestGetSQL(Value::String(std::string("ab\x01 c")))
                .DebugString());  // NOLINT

  EXPECT_EQ("b\"ab\\x01 c\"",
            TestGetSQL(Value::Bytes("ab\x01 c")).DebugString());
  EXPECT_EQ("b\"ab\\x01 c\"",
            TestGetSQL(Value::Bytes(std::string("ab\x01 c")))
                .DebugString());  // NOLINT
}

TEST_F(ValueTest, StringFormatting) {
  EXPECT_EQ(Value::String("hello").DebugString(/*verbose=*/true),
            "String(\"hello\")");
  EXPECT_EQ(Value::String("hello").DebugString(), "\"hello\"");
  EXPECT_EQ(Value::String("hello").Format(), "String(\"hello\")");
  EXPECT_EQ(Value::String("hello").Format(/*print_top_level_type=*/false),
            "\"hello\"");
  EXPECT_EQ(Value::String("hello").GetSQLLiteral(), "\"hello\"");
  EXPECT_EQ(Value::String("hello").GetSQL(), "\"hello\"");
}

TEST_F(ValueTest, SimpleRoundTrip) {
  EXPECT_EQ(-42, Value::Int32(-42).int32_value());
  EXPECT_EQ(-1 * (int64_t{42} << 42),
            Value::Int64(-1 * (int64_t{42} << 42)).int64_value());
  EXPECT_EQ(42u, Value::Uint32(42u).uint32_value());
  EXPECT_EQ(uint64_t{42} << 42,
            Value::Uint64(uint64_t{42} << 42).uint64_value());
  EXPECT_EQ(true, Value::Bool(true).bool_value());
  EXPECT_EQ(false, Value::Bool(false).bool_value());
  EXPECT_EQ(3.1415f, Value::Float(3.1415f).float_value());
  EXPECT_EQ(2.718281828459045, Value::Double(2.718281828459045).double_value());
  EXPECT_EQ("honorificabilitudinitatibus",
            Value::String("honorificabilitudinitatibus").string_value());
  EXPECT_EQ("honorificabilitudinitatibus",
            Value::Bytes("honorificabilitudinitatibus").bytes_value());
  EXPECT_EQ(123, Value::Date(123).date_value());
  int64_t min_micros = googlesql::types::kTimestampMin;
  EXPECT_EQ(min_micros, TimestampFromUnixMicros(min_micros).ToUnixMicros());
}

TEST_F(ValueTest, DateFormatting) {
  const Value date = Value::Date(19251);
  EXPECT_EQ(date.DebugString(/*verbose=*/true), "Date(2022-09-16)");
  EXPECT_EQ(date.DebugString(), "2022-09-16");
  EXPECT_EQ(date.Format(), "Date(2022-09-16)");
  EXPECT_EQ(date.Format(/*print_top_level_type=*/false), "2022-09-16");
  EXPECT_EQ(date.GetSQLLiteral(), R"sql(DATE "2022-09-16")sql");
  EXPECT_EQ(date.GetSQL(), R"sql(DATE "2022-09-16")sql");
}

TEST_F(ValueTest, DatetimeFormatting) {
  const Value datetime = Value::Datetime(
      DatetimeValue::FromYMDHMSAndNanos(2022, 9, 16, 13, 14, 11, 1));
  EXPECT_EQ(datetime.DebugString(/*verbose=*/true),
            "Datetime(2022-09-16 13:14:11.000000001)");
  EXPECT_EQ(datetime.DebugString(), "2022-09-16 13:14:11.000000001");
  EXPECT_EQ(datetime.Format(), "Datetime(2022-09-16 13:14:11.000000001)");
  EXPECT_EQ(datetime.Format(/*print_top_level_type=*/false),
            "2022-09-16 13:14:11.000000001");
  EXPECT_EQ(datetime.GetSQLLiteral(),
            R"sql(DATETIME "2022-09-16 13:14:11.000000001")sql");
  EXPECT_EQ(datetime.GetSQL(),
            R"sql(DATETIME "2022-09-16 13:14:11.000000001")sql");
}

TEST_F(ValueTest, Timestamp) {
  const absl::TimeZone utc = absl::UTCTimeZone();

  // Minimal and maximal representable timestamp value.
  absl::Time tmin =
      absl::FromCivil(absl::CivilSecond(00001, 01, 01, 00, 00, 00), utc);
  absl::Time tmax =
      absl::FromCivil(absl::CivilSecond(10000, 01, 01, 00, 00, 00), utc) -
      absl::Nanoseconds(1);
  EXPECT_EQ("TIMESTAMP", Value::Timestamp(tmin).type()->DebugString());
  EXPECT_EQ("0001-01-01 00:00:00+00", Value::Timestamp(tmin).DebugString());
  EXPECT_EQ("9999-12-31 23:59:59.999999999+00",
            Value::Timestamp(tmax).DebugString());
  EXPECT_EQ(googlesql::types::kTimestampMin,
            Value::Timestamp(tmin).ToUnixMicros());
  EXPECT_EQ(googlesql::types::kTimestampMax,
            Value::Timestamp(tmax).ToUnixMicros());

  EXPECT_EQ(tmin, Value::Timestamp(tmin).ToTime());
  EXPECT_EQ(tmax, Value::Timestamp(tmax).ToTime());

  // Valid timestamp before Unix epoch with non-empty nanos.
  absl::Time tmin_plus = tmin + absl::Nanoseconds(5);
  EXPECT_EQ(tmin_plus, Value::Timestamp(tmin_plus).ToTime());

  // Out of range values.
  absl::Time tmin_invalid = tmin - absl::Nanoseconds(1);
  absl::Time tmax_invalid = tmax + absl::Nanoseconds(1);
  EXPECT_DEATH(Value::Timestamp(tmin_invalid),
               "Check failed: functions::IsValidTime");
  EXPECT_DEATH(Value::Timestamp(tmax_invalid),
               "Check failed: functions::IsValidTime");

  // Values representable as Unix epoch nanoseconds.
  absl::Time tmin_nanos =
      absl::FromUnixNanos(std::numeric_limits<int64_t>::lowest());
  absl::Time tmax_nanos =
      absl::FromUnixNanos(std::numeric_limits<int64_t>::max());

  EXPECT_EQ("1677-09-21 00:12:43.145224192+00",
            Value::Timestamp(tmin_nanos).DebugString());
  EXPECT_EQ("2262-04-11 23:47:16.854775807+00",
            Value::Timestamp(tmax_nanos).DebugString());

  int64_t unix_nanos = 0;
  GOOGLESQL_EXPECT_OK(Value::TimestampFromUnixMicros(5).ToUnixNanos(&unix_nanos));
  EXPECT_EQ(5000, unix_nanos);

  // Values not representable as 64-bit Unix epoch nanoseconds.
  Value tmin_nanos_minus = Value::Timestamp(tmin_nanos - absl::Nanoseconds(1));
  Value tmax_nanos_plus = Value::Timestamp(tmax_nanos + absl::Nanoseconds(1));

  EXPECT_THAT(
      tmin_nanos_minus.ToUnixNanos(&unix_nanos),
      StatusIs(
          absl::StatusCode::kOutOfRange,
          HasSubstr("Timestamp value in Unix epoch nanoseconds exceeds 64 bit: "
                    "1677-09-21 00:12:43.145224191+00")));

  EXPECT_THAT(
      tmax_nanos_plus.ToUnixNanos(&unix_nanos),
      StatusIs(
          absl::StatusCode::kOutOfRange,
          HasSubstr("Timestamp value in Unix epoch nanoseconds exceeds 64 bit: "
                    "2262-04-11 23:47:16.854775808+00")));
}

TEST_F(ValueTest, ConvenienceDate) {
  // 1970-05-04 is 123 days after the Unix epoch. Date values are represented as
  // the number of days since the Unix epoch.
  EXPECT_EQ(Value::Date(123), values::Date(absl::CivilDay(1970, 5, 4)));
  EXPECT_EQ(Value::Date(123).ToCivilDay(), absl::CivilDay(1970, 5, 4));
  EXPECT_DEATH(Value::NullDate().ToCivilDay(), "Null value");
  EXPECT_DEATH(Value::Int32(0).ToCivilDay(), "Not a date value");
}

TEST_F(ValueTest, TimestampFormatting) {
  absl::TimeZone pst;
  ASSERT_TRUE(absl::LoadTimeZone("America/Los_Angeles", &pst));
  const Value ts = Value::Timestamp(
      absl::FromCivil(absl::CivilSecond(2022, 9, 16, 12, 43, 00), pst));
  EXPECT_EQ(ts.DebugString(/*verbose=*/true),
            "Timestamp(2022-09-16 19:43:00+00)");
  EXPECT_EQ(ts.DebugString(), "2022-09-16 19:43:00+00");
  EXPECT_EQ(ts.Format(), "Timestamp(2022-09-16 19:43:00+00)");
  EXPECT_EQ(ts.Format(/*print_top_level_type=*/false),
            "2022-09-16 19:43:00+00");
  EXPECT_EQ(ts.GetSQLLiteral(), R"sql(TIMESTAMP "2022-09-16 19:43:00+00")sql");
  EXPECT_EQ(ts.GetSQL(), R"sql(TIMESTAMP "2022-09-16 19:43:00+00")sql");
}

TEST_F(ValueTest, TimestampWithPicoseconds) {
  absl::TimeZone pst;
  ASSERT_TRUE(absl::LoadTimeZone("America/Los_Angeles", &pst));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto pico_time,
      PicoTime::Create(
          absl::FromCivil(absl::CivilSecond(2022, 9, 16, 12, 43, 0), pst),
          999999999999));
  const Value ts = Value::Timestamp(googlesql::TimestampPicosValue(pico_time));
  EXPECT_EQ(ts.DebugString(/*verbose=*/true),
            "Timestamp(2022-09-16 19:43:00.999999999999+00)");
  EXPECT_EQ(ts.DebugString(), "2022-09-16 19:43:00.999999999999+00");
  EXPECT_EQ(ts.Format(), "Timestamp(2022-09-16 19:43:00.999999999999+00)");
  EXPECT_EQ(ts.Format(/*print_top_level_type=*/false),
            "2022-09-16 19:43:00.999999999999+00");
  EXPECT_EQ(ts.GetSQLLiteral(),
            R"sql(TIMESTAMP "2022-09-16 19:43:00.999999999999+00")sql");
  EXPECT_EQ(ts.GetSQL(),
            R"sql(TIMESTAMP "2022-09-16 19:43:00.999999999999+00")sql");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto pico_time2,
      PicoTime::Create(
          absl::FromCivil(absl::CivilSecond(2022, 9, 16, 12, 43, 0), pst),
          999999999999));
  const Value ts2 =
      Value::Timestamp(googlesql::TimestampPicosValue(pico_time2));
  EXPECT_EQ(ts, ts2);
}

TEST_F(ValueTest, Interval) {
  EXPECT_TRUE(Value::NullInterval().is_null());
  EXPECT_EQ(TYPE_INTERVAL, Value::NullInterval().type_kind());

  IntervalValue interval;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(interval, IntervalValue::FromMicros(1));
  // Verify that there are no memory leaks.
  {
    Value v1(Value::Interval(interval));
    EXPECT_EQ(TYPE_INTERVAL, v1.type()->kind());
    EXPECT_FALSE(v1.is_null());
    Value v2(v1);
    EXPECT_EQ(TYPE_INTERVAL, v2.type()->kind());
    EXPECT_FALSE(v2.is_null());
  }

  // Test the assignment operator.
  {
    Value v1 = Value::Interval(interval);
    Value v2 = googlesql::values::Interval(interval);
    Value v3 = Value::NullInterval();
    v3 = v1;
    EXPECT_EQ(TYPE_INTERVAL, v1.type()->kind());
    EXPECT_EQ(TYPE_INTERVAL, v2.type()->kind());
    EXPECT_EQ(TYPE_INTERVAL, v3.type()->kind());
    EXPECT_FALSE(v3.is_null());
    EXPECT_EQ(interval, v1.interval_value());
    EXPECT_EQ(interval, v2.interval_value());
    EXPECT_EQ(interval, v3.interval_value());
  }

  // Equals
  {
    EXPECT_EQ(Value::Interval(Months(0)), Value::Interval(Nanos(0)));
    EXPECT_EQ(Value::Interval(Months(-5)), Value::Interval(Months(-5)));
    EXPECT_EQ(Value::Interval(Months(2)), Value::Interval(Days(60)));
    EXPECT_EQ(Value::Interval(Days(1)),
              Value::Interval(Micros(IntervalValue::kMicrosInDay)));
    EXPECT_EQ(Value::Interval(Months(-1)),
              Value::Interval(Nanos(-IntervalValue::kNanosInMonth)));
    EXPECT_EQ(Value::Interval(Micros(1)), Value::Interval(Nanos(1000)));

    EXPECT_EQ(Value::Interval(Days(45)),
              Value::Interval(MonthsDaysMicros(1, 15, 0)));
    EXPECT_EQ(Value::Interval(Days(45)),
              Value::Interval(MonthsDaysMicros(2, -15, 0)));
    EXPECT_EQ(Value::Interval(MonthsDaysMicros(1, 2, 3)),
              Value::Interval(MonthsDaysNanos(1, 2, 3000)));

    EXPECT_FALSE(Value::Interval(Months(1)) == Value::Interval(Days(31)));
    EXPECT_FALSE(Value::Interval(Months(12)) == Value::Interval(Days(365)));
    EXPECT_FALSE(Value::Interval(Micros(1)) == Value::Interval(Nanos(1)));
    EXPECT_FALSE(Value::Interval(Nanos(-1)) == Value::Interval(Nanos(1)));
  }

  {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        interval, IntervalValue::FromMonthsDaysMicros(14, 30, 3723456789));
    Value value(Value::Interval(interval));
    EXPECT_EQ("1-2 30 1:2:3.456789", value.DebugString());
    value = TestGetSQL(value);
    EXPECT_EQ("INTERVAL", value.type()->DebugString());
    EXPECT_EQ("1-2 30 1:2:3.456789", value.DebugString());
    EXPECT_FALSE(value.is_null());
    EXPECT_EQ(IntervalValue(interval), value.interval_value());
  }
}

TEST_F(ValueTest, IntervalFormatting) {
  const Value interval = Value::Interval(Days(45));
  EXPECT_EQ(interval.DebugString(/*verbose=*/true), "Interval(0-0 45 0:0:0)");
  EXPECT_EQ(interval.DebugString(), "0-0 45 0:0:0");
  EXPECT_EQ(interval.Format(), "Interval(0-0 45 0:0:0)");
  EXPECT_EQ(interval.Format(/*print_top_level_type=*/false), "0-0 45 0:0:0");
  EXPECT_EQ(interval.GetSQLLiteral(),
            R"sql(INTERVAL "0-0 45 0:0:0" YEAR TO SECOND)sql");
  EXPECT_EQ(interval.GetSQL(),
            R"sql(INTERVAL "0-0 45 0:0:0" YEAR TO SECOND)sql");
}

TEST_F(ValueTest, Geography) {
  EXPECT_TRUE(Value::NullGeography().is_null());
  EXPECT_TRUE(Value::NullGeography().type()->IsGeography());
}

TEST_F(ValueTest, Numeric) {
  EXPECT_TRUE(Value::NullNumeric().is_null());
  EXPECT_EQ(TYPE_NUMERIC, Value::NullNumeric().type_kind());

  // Verify that there are no memory leaks.
  {
    Value v1(Value::Numeric(NumericValue(int64_t{1})));
    EXPECT_EQ(TYPE_NUMERIC, v1.type()->kind());
    EXPECT_FALSE(v1.is_null());
    Value v2(v1);
    EXPECT_EQ(TYPE_NUMERIC, v2.type()->kind());
    EXPECT_FALSE(v2.is_null());
    EXPECT_EQ(1.0, v1.ToDouble());
  }

  // Test the assignment operator.
  {
    Value v1 = Value::Numeric(NumericValue(int64_t{100}));
    Value v2 = Value::NullNumeric();
    v2 = v1;
    EXPECT_EQ(TYPE_NUMERIC, v1.type()->kind());
    EXPECT_EQ(TYPE_NUMERIC, v2.type()->kind());
    EXPECT_FALSE(v2.is_null());
    EXPECT_EQ(NumericValue(int64_t{100}), v1.numeric_value());
    EXPECT_EQ(NumericValue(int64_t{100}), v2.numeric_value());
    v1 = Value::Int32(1);
    EXPECT_EQ(TYPE_NUMERIC, v2.type()->kind());
  }

  {
    Value value = TestGetSQL(Value::Numeric(NumericValue(int64_t{123})));
    EXPECT_EQ("NUMERIC", value.type()->DebugString());
    EXPECT_FALSE(value.is_null());
    EXPECT_EQ(NumericValue(int64_t{123}), value.numeric_value());
  }
}

TEST_F(ValueTest, NumericFormatting) {
  const Value numeric = Value::Numeric(NumericValue(int64_t{650}));
  EXPECT_EQ(numeric.DebugString(/*verbose=*/true), "Numeric(650)");
  EXPECT_EQ(numeric.DebugString(), "650");
  EXPECT_EQ(numeric.Format(), "Numeric(650)");
  EXPECT_EQ(numeric.Format(/*print_top_level_type=*/false), "650");
  EXPECT_EQ(numeric.GetSQLLiteral(), R"sql(NUMERIC "650")sql");
  EXPECT_EQ(numeric.GetSQL(), R"sql(NUMERIC "650")sql");
}

TEST_F(ValueTest, BigNumeric) {
  EXPECT_TRUE(Value::NullBigNumeric().is_null());
  EXPECT_EQ(TYPE_BIGNUMERIC, Value::NullBigNumeric().type_kind());

  // Verify that there are no memory leaks.
  {
    Value v1(Value::BigNumeric(BigNumericValue(int64_t{1})));
    EXPECT_EQ(TYPE_BIGNUMERIC, v1.type()->kind());
    EXPECT_FALSE(v1.is_null());
    Value v2(v1);
    EXPECT_EQ(TYPE_BIGNUMERIC, v2.type()->kind());
    EXPECT_FALSE(v2.is_null());
  }

  // Test the assignment operator.
  {
    Value v1 = Value::BigNumeric(BigNumericValue(int64_t{100}));
    Value v2 = googlesql::values::BigNumeric(int64_t{100});
    Value v3 = Value::NullBigNumeric();
    v3 = v1;
    EXPECT_EQ(TYPE_BIGNUMERIC, v1.type()->kind());
    EXPECT_EQ(TYPE_BIGNUMERIC, v2.type()->kind());
    EXPECT_EQ(TYPE_BIGNUMERIC, v3.type()->kind());
    EXPECT_FALSE(v3.is_null());
    EXPECT_EQ(BigNumericValue(int64_t{100}), v1.bignumeric_value());
    EXPECT_EQ(BigNumericValue(int64_t{100}), v2.bignumeric_value());
    EXPECT_EQ(BigNumericValue(int64_t{100}), v3.bignumeric_value());
    v1 = Value::Int32(1);
    EXPECT_EQ(TYPE_BIGNUMERIC, v3.type()->kind());
  }

  {
    Value value = TestGetSQL(Value::BigNumeric(BigNumericValue(int64_t{123})));
    EXPECT_EQ("BIGNUMERIC", value.type()->DebugString());
    EXPECT_FALSE(value.is_null());
    EXPECT_EQ(BigNumericValue(int64_t{123}), value.bignumeric_value());
  }
}

TEST_F(ValueTest, BigNumericFormatting) {
  const Value numeric =
      Value::BigNumeric(BigNumericValue(int64_t{5973600000000}));
  EXPECT_EQ(numeric.DebugString(/*verbose=*/true), "BigNumeric(5973600000000)");
  EXPECT_EQ(numeric.DebugString(), "5973600000000");
  EXPECT_EQ(numeric.Format(), "BigNumeric(5973600000000)");
  EXPECT_EQ(numeric.Format(/*print_top_level_type=*/false), "5973600000000");
  EXPECT_EQ(numeric.GetSQLLiteral(), R"sql(BIGNUMERIC "5973600000000")sql");
  EXPECT_EQ(numeric.GetSQL(), R"sql(BIGNUMERIC "5973600000000")sql");
}

TEST_F(ValueTest, JSON) {
  constexpr char kStringValue[] = "value";
  constexpr int64_t kIntValue = 1;

  {
    Value null_json = Value::NullJson();
    EXPECT_TRUE(null_json.is_null());
    EXPECT_EQ(null_json.type_kind(), TYPE_JSON);
    EXPECT_FALSE(null_json.is_unparsed_json());
    EXPECT_FALSE(null_json.is_validated_json());
    EXPECT_DEATH(null_json.json_value(), "Null value");
    EXPECT_DEATH(null_json.json_string(), "Null value");
    EXPECT_DEATH(null_json.json_value_unparsed(), "Null value");
  }

  // Verify that there are no memory leaks for validated JSON.
  {
    Value v1(Value::Json(JSONValue(kIntValue)));
    EXPECT_EQ(TYPE_JSON, v1.type()->kind());
    EXPECT_FALSE(v1.is_null());
    EXPECT_TRUE(v1.is_validated_json());

    Value v2(v1);
    EXPECT_EQ(TYPE_JSON, v2.type()->kind());
    EXPECT_FALSE(v2.is_null());
    EXPECT_TRUE(v2.is_validated_json());
    EXPECT_FALSE(v2.is_unparsed_json());
    EXPECT_EQ(v2.json_string(), absl::StrCat(kIntValue));
    EXPECT_DEATH(v2.json_value_unparsed(), "Not an unparsed json value");
  }

  // Verify that there are no memory leaks for unvalidated JSON.
  {
    Value v1(Value::UnvalidatedJsonString(kStringValue));
    EXPECT_EQ(TYPE_JSON, v1.type()->kind());
    EXPECT_FALSE(v1.is_null());
    EXPECT_FALSE(v1.is_validated_json());
    Value v2(v1);
    EXPECT_EQ(TYPE_JSON, v2.type()->kind());
    EXPECT_FALSE(v2.is_null());
    ASSERT_FALSE(v2.is_validated_json());
    ASSERT_TRUE(v2.is_unparsed_json());
    EXPECT_EQ(kStringValue, v2.json_value_unparsed());
    EXPECT_EQ(kStringValue, v2.json_string());
    EXPECT_DEATH(v2.json_value(), "Non a validated json value");
  }

  // Test the assignment operator for validated JSON.
  {
    Value v1 = Value::Json(JSONValue(kIntValue));
    Value v2 = Value::NullJson();
    v2 = v1;
    EXPECT_EQ(TYPE_JSON, v1.type()->kind());
    EXPECT_EQ(TYPE_JSON, v2.type()->kind());
    EXPECT_FALSE(v2.is_null());
    EXPECT_EQ(kIntValue, v1.json_value().GetInt64());
    EXPECT_EQ(kIntValue, v2.json_value().GetInt64());
  }

  // Test the assignment operator for unvalidated JSON.
  {
    Value v1 = Value::UnvalidatedJsonString(kStringValue);
    Value v2 = Value::NullJson();
    v2 = v1;
    EXPECT_EQ(TYPE_JSON, v1.type()->kind());
    EXPECT_EQ(TYPE_JSON, v2.type()->kind());
    EXPECT_FALSE(v2.is_null());
    ASSERT_FALSE(v2.is_validated_json());
    ASSERT_TRUE(v2.is_unparsed_json());
    EXPECT_EQ(kStringValue, v1.json_value_unparsed());
    EXPECT_EQ(kStringValue, v2.json_value_unparsed());
    EXPECT_EQ(kStringValue, v2.json_string());
  }

  {
    Value value = TestGetSQL(Value::Json(JSONValue(kIntValue)));
    EXPECT_EQ("JSON", value.type()->DebugString());
    EXPECT_FALSE(value.is_null());
    ASSERT_TRUE(value.is_validated_json());
    ASSERT_TRUE(value.json_value().IsInt64());
    EXPECT_EQ(kIntValue, value.json_value().GetInt64());
    EXPECT_EQ(absl::StrCat(kIntValue), value.json_string());
  }
}

TEST_F(ValueTest, JSONFormatting) {
  const Value json = Value::Json(
      JSONValue::ParseJSONString(
          R"json({"foo":[1, null, "bar"], "foo2": "hello", "foo3": true})json")
          .value());
  EXPECT_EQ(json.DebugString(/*verbose=*/true),
            R"(Json({"foo":[1,null,"bar"],"foo2":"hello","foo3":true}))");
  EXPECT_EQ(json.DebugString(),
            R"({"foo":[1,null,"bar"],"foo2":"hello","foo3":true})");
  EXPECT_EQ(json.Format(),
            R"(Json({"foo":[1,null,"bar"],"foo2":"hello","foo3":true}))");
  EXPECT_EQ(json.Format(/*print_top_level_type=*/false),
            R"({"foo":[1,null,"bar"],"foo2":"hello","foo3":true})");
  EXPECT_EQ(
      json.GetSQLLiteral(),
      R"sql(JSON '{"foo":[1,null,"bar"],"foo2":"hello","foo3":true}')sql");
  EXPECT_EQ(
      json.GetSQL(),
      R"sql(JSON '{"foo":[1,null,"bar"],"foo2":"hello","foo3":true}')sql");
}

namespace {
Value TokenListFromArray(std::vector<std::string> tokens) {
  return TokenListFromStringArray(std::move(tokens));
}

Value TokenListFromToken(std::string token) {
  return TokenListFromArray({token});
}
}  // namespace

TEST_F(ValueTest, TokenList) {
  EXPECT_TRUE(Value::NullTokenList().is_null());
  EXPECT_EQ(TYPE_TOKENLIST, Value::NullTokenList().type_kind());

  // Verify that there are no memory leaks.
  {
    Value v1(TokenListFromToken("test"));
    EXPECT_EQ(TYPE_TOKENLIST, v1.type()->kind());
    EXPECT_FALSE(v1.is_null());
    Value v2(v1);
    EXPECT_EQ(TYPE_TOKENLIST, v2.type()->kind());
    EXPECT_FALSE(v2.is_null());
  }

  // Test the assignment operator.
  Value tokenlist = TokenListFromStringArray({"test"});
  Value expected = TokenListFromStringArray({"test"});
  ASSERT_TRUE(
      tokenlist.tokenlist_value().EquivalentTo(expected.tokenlist_value()));
  {
    Value v1 = TokenListFromToken("test");
    Value v2 = tokenlist;
    EXPECT_NE("", v1.DebugString());
    EXPECT_EQ(v1.DebugString(), v2.DebugString());

    Value v3 = Value::NullTokenList();
    v3 = v1;
    EXPECT_EQ(TYPE_TOKENLIST, v1.type()->kind());
    EXPECT_EQ(TYPE_TOKENLIST, v2.type()->kind());
    EXPECT_EQ(TYPE_TOKENLIST, v3.type()->kind());
    EXPECT_FALSE(v3.is_null());
    EXPECT_TRUE(v1.tokenlist_value().EquivalentTo(expected.tokenlist_value()));
    EXPECT_TRUE(v2.tokenlist_value().EquivalentTo(expected.tokenlist_value()));
    EXPECT_TRUE(v3.tokenlist_value().EquivalentTo(expected.tokenlist_value()));
  }

  // Equals.
  {
    const Value v1 = TokenListFromToken("a");
    const Value v2 = TokenListFromToken("a");
    const Value v3 = TokenListFromToken("b");
    EXPECT_EQ(v1, v2);
    TestHashEqual(v1, v2);
    EXPECT_NE(v1, v3);
    TestHashNotEqual(v1, v3);
  }

  TestGetSQL(Value::NullTokenList());
  TestGetSQL(TokenListFromToken("tokenlist"));
}

TEST_F(ValueTest, Uuid) {
  EXPECT_TRUE(Value::NullUuid().is_null());
  EXPECT_EQ(TYPE_UUID, Value::NullUuid().type_kind());

  UuidValue uuid;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      uuid, UuidValue::FromString("9d3da3234c20360fbd9bec54feec54f0"));
  // Verify that there are no memory leaks.
  {
    Value v1(Value::Uuid(uuid));
    EXPECT_EQ(TYPE_UUID, v1.type()->kind());
    EXPECT_FALSE(v1.is_null());
    Value v2(v1);
    EXPECT_EQ(TYPE_UUID, v2.type()->kind());
    EXPECT_FALSE(v2.is_null());
  }

  // Test the assignment operator.
  {
    Value v1 = Value::Uuid(uuid);
    Value v2 = Value::NullUuid();
    Value v3 = googlesql::values::Uuid(uuid);
    v2 = v1;
    EXPECT_EQ(TYPE_UUID, v1.type()->kind());
    EXPECT_EQ(TYPE_UUID, v2.type()->kind());
    EXPECT_EQ(TYPE_UUID, v3.type()->kind());
    EXPECT_FALSE(v2.is_null());
    EXPECT_EQ(uuid, v1.uuid_value().value());
    EXPECT_EQ(uuid, v2.uuid_value().value());
    EXPECT_EQ(uuid, v3.uuid_value().value());
  }
}

TEST_F(ValueTest, UuidFormatting) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      UuidValue v, UuidValue::FromString("9d3da3234c20360fbd9bec54feec54f0"));
  const Value uuid = Value::Uuid(v);
  EXPECT_EQ(uuid.DebugString(/*verbose=*/true),
            "Uuid(9d3da323-4c20-360f-bd9b-ec54feec54f0)");
  EXPECT_EQ(uuid.DebugString(), "9d3da323-4c20-360f-bd9b-ec54feec54f0");
  EXPECT_EQ(uuid.Format(), "Uuid(9d3da323-4c20-360f-bd9b-ec54feec54f0)");
  EXPECT_EQ(uuid.Format(/*print_top_level_type=*/false),
            "9d3da323-4c20-360f-bd9b-ec54feec54f0");
  EXPECT_EQ(uuid.GetSQLLiteral(PRODUCT_EXTERNAL),
            R"sql(CAST('9d3da323-4c20-360f-bd9b-ec54feec54f0' AS UUID))sql");
  EXPECT_EQ(uuid.GetSQL(PRODUCT_EXTERNAL),
            R"sql(CAST('9d3da323-4c20-360f-bd9b-ec54feec54f0' AS UUID))sql");
}

TEST_F(ValueTest, GenericAccessors) {
  // Return types.
  static Value v;
  static_assert(std::is_same<decltype(v.Get<int32_t>()), int32_t>::value, "");
  static_assert(std::is_same<decltype(v.Get<int64_t>()), int64_t>::value, "");
  static_assert(std::is_same<decltype(v.Get<uint32_t>()), uint32_t>::value, "");
  static_assert(std::is_same<decltype(v.Get<uint64_t>()), uint64_t>::value, "");
  static_assert(std::is_same<decltype(v.Get<bool>()), bool>::value, "");
  static_assert(std::is_same<decltype(v.Get<float>()), float>::value, "");
  static_assert(std::is_same<decltype(v.Get<double>()), double>::value, "");
  // Explicit template instantiations.
  EXPECT_EQ(Value::Int32(1), Value::Make<int32_t>(1));
  EXPECT_EQ(Value::Int64(1), Value::Make<int64_t>(1));
  EXPECT_EQ(Value::Uint32(1), Value::Make<uint32_t>(1));
  EXPECT_EQ(Value::Uint64(1), Value::Make<uint64_t>(1));
  EXPECT_EQ(Value::Bool(true), Value::Make<bool>(true));
  EXPECT_EQ(Value::Float(1.3f), Value::Make<float>(1.3f));
  EXPECT_EQ(Value::Double(1.3), Value::Make<double>(1.3));
  EXPECT_EQ(1, Value::Make<int32_t>(1).Get<int32_t>());
  EXPECT_EQ(1, Value::Make<int64_t>(1).Get<int64_t>());
  EXPECT_EQ(1, Value::Make<uint32_t>(1).Get<uint32_t>());
  EXPECT_EQ(1, Value::Make<uint64_t>(1).Get<uint64_t>());
  EXPECT_EQ(true, Value::Make<bool>(true).Get<bool>());
  EXPECT_EQ(1.3f, Value::Make<float>(1.3f).Get<float>());
  EXPECT_EQ(1.3, Value::Make<double>(1.3).Get<double>());
  // Implicit template instantiations.
  int32_t int32_val = 1;
  int64_t int64_val = 2;
  uint32_t uint32_val = 3;
  uint64_t uint64_val = 4;
  bool bool_val = true;
  float float_val = 1.3f;
  double double_val = 1.5;
  EXPECT_EQ(int32_val, Value::Make(int32_val).Get<int32_t>());
  EXPECT_EQ(int64_val, Value::Make(int64_val).Get<int64_t>());
  EXPECT_EQ(uint32_val, Value::Make(uint32_val).Get<uint32_t>());
  EXPECT_EQ(uint64_val, Value::Make(uint64_val).Get<uint64_t>());
  EXPECT_EQ(bool_val, Value::Make(bool_val).Get<bool>());
  EXPECT_EQ(float_val, Value::Make(float_val).Get<float>());
  EXPECT_EQ(double_val, Value::Make(double_val).Get<double>());
  // MakeNull.
  EXPECT_EQ(Value::NullInt32(), Value::MakeNull<int32_t>());
  EXPECT_EQ(Value::NullInt64(), Value::MakeNull<int64_t>());
  EXPECT_EQ(Value::NullUint32(), Value::MakeNull<uint32_t>());
  EXPECT_EQ(Value::NullUint64(), Value::MakeNull<uint64_t>());
  EXPECT_EQ(Value::NullBool(), Value::MakeNull<bool>());
  EXPECT_EQ(Value::NullFloat(), Value::MakeNull<float>());
  EXPECT_EQ(Value::NullDouble(), Value::MakeNull<double>());
  // Type mismatches.
  EXPECT_DEATH(Value::Make<int32_t>(1).Get<int64_t>(), "Not an int64");
  EXPECT_DEATH(Value::Make<int32_t>(1).Get<bool>(), "Not a bool");
  EXPECT_DEATH(Value::Make<bool>(1).Get<int32_t>(), "Not an int32");
  EXPECT_DEATH(Value::Make<float>(1.3f).Get<double>(), "Not a double");
  EXPECT_DEATH(Value::Make<double>(1.3).Get<float>(), "Not a float");
}

// Verifies that HashCode() is consistent for the same value and not the same
// for all values of the same given type.
TEST_F(ValueTest, HashCode) {
  const absl::TimeZone utc = absl::UTCTimeZone();

  const EnumType* enum_type = GetTestEnumType();
  const EnumType* other_enum_type = GetOtherTestEnumType();

  const ProtoType* proto_type = GetTestProtoType();
  const ProtoType* other_proto_type = GetOtherTestProtoType();

  const ArrayType* array_enum_type = GetTestArrayType(enum_type);
  const ArrayType* array_other_enum_type = GetTestArrayType(other_enum_type);
  const ArrayType* array_proto_type = GetTestArrayType(proto_type);
  const ArrayType* array_other_proto_type = GetTestArrayType(other_proto_type);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* map_string_type,
      MakeMapType(types::StringType(), types::StringType()));

  // Include all simple types and some simple examples of complex types.
  // Complex types have additional testing in other tests.
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly({
      // Invalid value.
      Value(),
      // Typed nulls.
      Value::NullInt32(),
      Value::NullInt64(),
      Value::NullUint32(),
      Value::NullUint64(),
      Value::NullBool(),
      Value::NullFloat(),
      Value::NullDouble(),
      Value::NullBytes(),
      Value::NullString(),
      Value::NullDate(),
      Value::NullTimestamp(),
      Value::NullTime(),
      Value::NullDatetime(),
      Value::NullGeography(),
      Value::NullNumeric(),
      Value::NullBigNumeric(),
      Value::NullJson(),
      Value::Null(enum_type),
      Value::Null(other_enum_type),
      Value::Null(proto_type),
      Value::Null(other_proto_type),
      Value::Null(types::Int32ArrayType()),
      Value::Null(types::Int64ArrayType()),
      Value::Null(array_enum_type),
      Value::Null(array_other_enum_type),
      Value::Null(array_proto_type),
      Value::Null(array_other_proto_type),
      Value::Null(MakeRangeType(DateType())),
      Value::Null(MakeRangeType(DatetimeType())),
      Value::Null(MakeRangeType(TimestampType())),
      Value::Null(map_string_type),
      // Simple scalar types.
      Value::Int32(1001),
      Value::Int32(1002),
      Value::Int64(1001),
      Value::Int64(1002),
      Value::Uint32(1001),
      Value::Uint32(1002),
      Value::Uint64(1001),
      Value::Uint64(1002),
      Value::Bool(false),
      Value::Bool(true),
      Value::Float(1.3f),
      Value::Float(1.4f),
      Value::Double(1.3f),
      Value::Double(1.4f),
      Value::String("a"),
      Value::String("b"),
      Value::StringValue(std::string("a")),
      Value::StringValue(std::string("b")),
      Value::Bytes("a"),
      Value::Bytes("b"),
      Value::Bytes(std::string("a")),
      Value::Bytes(std::string("b")),
      Value::Date(1001),
      Value::Date(1002),
      Value::Timestamp(
          absl::FromCivil(absl::CivilSecond(2018, 2, 14, 16, 36, 11), utc) +
          absl::Nanoseconds(1)),
      Value::Timestamp(
          absl::FromCivil(absl::CivilSecond(2018, 2, 14, 16, 36, 11), utc) +
          absl::Nanoseconds(2)),
      Value::Time(TimeValue::FromHMSAndNanos(16, 36, 11, 1)),
      Value::Time(TimeValue::FromHMSAndNanos(16, 36, 11, 2)),
      Value::Datetime(
          DatetimeValue::FromYMDHMSAndNanos(2018, 2, 14, 16, 36, 11, 1)),
      Value::Datetime(
          DatetimeValue::FromYMDHMSAndNanos(2018, 2, 14, 16, 36, 11, 2)),
      Value::Numeric(NumericValue(int64_t{1001})),
      Value::Numeric(NumericValue(int64_t{1002})),
      Value::BigNumeric(BigNumericValue(int64_t{1001})),
      Value::BigNumeric(BigNumericValue(int64_t{1002})),
      Value::Json(JSONValue(int64_t{1})),
      Value::UnvalidatedJsonString("value"),
      Value::UnvalidatedJsonString("123"),
      TokenListFromToken("t1"),
      TokenListFromToken("t2"),
      // Enums of two different types.
      values::Enum(enum_type, 0),
      values::Enum(enum_type, 1),
      values::Enum(other_enum_type, 0),
      values::Enum(other_enum_type, 1),
      // Protos of two different types.
      Value::Proto(proto_type, {}),
      Value::Proto(other_proto_type, {}),
      // Arrays of various types.
      values::Int32Array({}),
      values::Int32Array({0, 1, 1, 2, 3, 5, 8, 13, 21}),
      values::Int32Array({0, 1, 1, 2, 3, 5, 8, 13, -1}),
      values::Int64Array({}),
      values::Int64Array({0, 1, 1, 2, 3, 5, 8, 13, 21}),
      values::Int64Array({0, 1, 1, 2, 3, 5, 8, 13, -1}),
      values::DoubleArray({0.0, 1.0, 1.0, 2.0, 3.0, 5.0, 8.0, 13.0, 21.0}),
      values::DoubleArray({0.0, 1.0, 1.0, 2.0, 3.0, 5.0, 8.0, 13.0, -1.0}),

      values::JsonArray({}),
      values::JsonArray({JSONValue(int64_t{1}),
                         JSONValue(std::string("\"foo\"")),
                         JSONValue::ParseJSONString("{\"a\": 10}").value()}),
      values::UnvalidatedJsonStringArray({}),
      values::UnvalidatedJsonStringArray({"1", "{\"a:}", "foo"}),

      values::EmptyArray(array_enum_type),
      values::Array(array_enum_type, {values::Enum(enum_type, 0)}),
      values::Array(array_enum_type,
                    {values::Enum(enum_type, 0), values::Enum(enum_type, 1)}),
      values::EmptyArray(array_other_enum_type),
      values::Array(array_other_enum_type, {values::Enum(other_enum_type, 0)}),
      values::Array(array_other_enum_type, {values::Enum(other_enum_type, 0),
                                            values::Enum(other_enum_type, 1)}),

      values::EmptyArray(array_proto_type),
      values::Array(array_proto_type, {Value::Proto(proto_type, {})}),
      values::Array(array_proto_type, {Value::Proto(proto_type, {}),
                                       Value::Proto(proto_type, {})}),
      values::EmptyArray(array_other_proto_type),
      values::Array(array_other_proto_type,
                    {Value::Proto(other_proto_type, {})}),
      values::Array(array_other_proto_type,
                    {Value::Proto(other_proto_type, {}),
                     Value::Proto(other_proto_type, {})}),
      // Ranges of dates
      Range(Value::Date(1), Value::Date(2)),
      Range(Value::Date(2), Value::Date(3)),
      Range(Value::UnboundedStartDate(), Value::Date(4)),
      Range(Value::Date(5), Value::UnboundedEndDate()),
      Range(Value::UnboundedStartDate(), Value::UnboundedEndDate()),
      // Ranges of datetimes
      Range(Value::Datetime(
                DatetimeValue::FromYMDHMSAndNanos(2020, 2, 3, 4, 5, 6, 7)),
            Value::Datetime(
                DatetimeValue::FromYMDHMSAndNanos(2021, 2, 3, 4, 5, 6, 7))),
      Range(Value::Datetime(
                DatetimeValue::FromYMDHMSAndNanos(2020, 3, 3, 4, 5, 6, 7)),
            Value::Datetime(
                DatetimeValue::FromYMDHMSAndNanos(2021, 4, 3, 4, 5, 6, 7))),
      Range(Value::Datetime(
                DatetimeValue::FromYMDHMSAndNanos(2020, 2, 3, 4, 5, 6, 7)),
            Value::UnboundedEndDatetime()),
      Range(Value::UnboundedStartDatetime(),
            Value::Datetime(
                DatetimeValue::FromYMDHMSAndNanos(2021, 2, 3, 4, 5, 6, 7))),
      Range(Value::UnboundedStartDatetime(), Value::UnboundedEndDatetime()),
      // Ranges of timestamps
      Range(
          Value::Timestamp(absl::FromCivil(
              absl::CivilSecond(2019, 01, 01, 0, 0, 0), absl::UTCTimeZone())),
          Value::Timestamp(absl::FromCivil(
              absl::CivilSecond(2020, 01, 01, 0, 0, 0), absl::UTCTimeZone()))),
      Range(
          Value::Timestamp(absl::FromCivil(
              absl::CivilSecond(2020, 01, 01, 0, 0, 0), absl::UTCTimeZone())),
          Value::Timestamp(absl::FromCivil(
              absl::CivilSecond(2021, 01, 01, 0, 0, 0), absl::UTCTimeZone()))),
      Range(Value::Timestamp(absl::FromCivil(
                absl::CivilSecond(2019, 01, 01, 0, 0, 0), absl::UTCTimeZone())),
            Value::UnboundedEndTimestamp()),
      Range(
          Value::UnboundedStartTimestamp(),
          Value::Timestamp(absl::FromCivil(
              absl::CivilSecond(2020, 01, 01, 0, 0, 0), absl::UTCTimeZone()))),
      Range(Value::UnboundedStartTimestamp(), Value::UnboundedEndTimestamp()),
      Map({{"a", Value::Int32(1)},
           {"b", Value::Int32(2)},
           {"c", Value::Int32(3)},
           {"d", Value::Int32(4)}}),
      Map({{Value::Int32(1), "a"},
           {Value::Int32(2), "b"},
           {Value::Int32(3), "c"},
           {Value::Int32(4), "d"}}),
  }));
}

TEST_F(ValueTest, InvalidValue) {
  Value invalid;
  EXPECT_EQ("Uninitialized value", invalid.DebugString());
  Value valid = Value::Int32(5);
  EXPECT_EQ("5", valid.DebugString());

  EXPECT_TRUE(invalid.Equals(Value()));
  EXPECT_TRUE(invalid.Equals(invalid));
  EXPECT_FALSE(invalid.Equals(valid));
  EXPECT_FALSE(valid.Equals(invalid));
  EXPECT_TRUE(valid.Equals(valid));

  Value valid_null = Value::NullInt32();
  TestHashEqual(invalid, invalid);
  TestHashNotEqual(invalid, valid);
  TestHashNotEqual(invalid, valid_null);
}

TEST_F(ValueTest, JsonValue) {
  Value x_value = Value::Json(JSONValue(double{1.0}));
  Value y_value = Value::Json(JSONValue(int64_t{1}));
  EXPECT_TRUE(x_value.Equals(y_value));

  // Invalid JSON values are not equal or less than to valid JSON values.
  Value invalid_x_value = Value::UnvalidatedJsonString("invalid_x");
  EXPECT_FALSE(invalid_x_value.Equals(x_value));
  EXPECT_FALSE(x_value.Equals(invalid_x_value));
  EXPECT_FALSE(invalid_x_value.LessThan(x_value));
  EXPECT_FALSE(x_value.LessThan(invalid_x_value));

  // Invalid JSON values are equal if they have the same raw string.
  Value invalid_y_value = Value::UnvalidatedJsonString("invalid_y");
  // Same as invalid_x_value.
  Value invalid_x_value2 = Value::UnvalidatedJsonString("invalid_x");
  EXPECT_FALSE(invalid_x_value.Equals(invalid_y_value));
  EXPECT_FALSE(invalid_y_value.Equals(invalid_x_value));
  EXPECT_TRUE(invalid_x_value.Equals(invalid_x_value2));
  EXPECT_TRUE(invalid_x_value2.Equals(invalid_x_value));

  // Invalid JSON values are not less than each other.
  EXPECT_FALSE(invalid_x_value.LessThan(invalid_y_value));
  EXPECT_FALSE(invalid_y_value.LessThan(invalid_x_value));
}

TEST_F(ValueTest, ConstructorTyping) {
  EXPECT_TRUE(Int32Type()->Equals(Value::Int32(-42).type()));
  EXPECT_TRUE(
      Int64Type()->Equals(Value::Int64(-1 * (int64_t{42} << 42)).type()));
  EXPECT_TRUE(Uint32Type()->Equals(Value::Uint32(42u).type()));
  EXPECT_TRUE(Uint64Type()->Equals(Value::Uint64(uint64_t{42} << 42).type()));
  EXPECT_TRUE(BoolType()->Equals(Value::Bool(true).type()));
  EXPECT_TRUE(BoolType()->Equals(Value::Bool(false).type()));
  EXPECT_TRUE(FloatType()->Equals(Value::Float(3.1415f).type()));
  EXPECT_TRUE(DoubleType()->Equals(Value::Double(2.718281828459045).type()));
  EXPECT_TRUE(StringType()->Equals(
      Value::StringValue(std::string("argh")).type()));  // NOLINT
  EXPECT_TRUE(StringType()->Equals(Value::String("argh").type()));
  EXPECT_TRUE(
      BytesType()->Equals(Value::Bytes(std::string("argh")).type()));  // NOLINT
  EXPECT_TRUE(BytesType()->Equals(Value::Bytes("argh").type()));
  EXPECT_TRUE(DateType()->Equals(Value::Date(0).type()));
}

TEST_F(ValueTest, CopyConstructor) {
  Value v0 = TestGetSQL(Value::Int32(-42));
  EXPECT_EQ(-42, v0.int32_value());
  EXPECT_EQ(-42, v0.ToDouble());
  EXPECT_EQ(-42, v0.ToInt64());

  Value v1 = TestGetSQL(Value::Int64(-1 * (int64_t{42} << 42)));
  EXPECT_EQ(-1 * (int64_t{42} << 42), v1.int64_value());
  EXPECT_EQ(-1 * (int64_t{42} << 42), v1.ToDouble());
  EXPECT_EQ(-1 * (int64_t{42} << 42), v1.ToInt64());

  Value v2 = TestGetSQL(Value::Uint32(42u));
  EXPECT_EQ(42u, v2.uint32_value());
  EXPECT_EQ(42, v2.ToDouble());
  EXPECT_EQ(42, v2.ToInt64());
  EXPECT_EQ(42u, v2.ToUint64());

  Value v3 = TestGetSQL(Value::Uint64(uint64_t{42} << 42));
  EXPECT_EQ(uint64_t{42} << 42, v3.uint64_value());
  EXPECT_EQ(uint64_t{42} << 42, v3.ToDouble());
  EXPECT_EQ(uint64_t{42} << 42, v3.ToUint64());

  Value v4 = TestGetSQL(Value::Bool(true));
  EXPECT_EQ(true, v4.bool_value());
  EXPECT_EQ(1, v4.ToDouble());
  EXPECT_EQ(1, v4.ToInt64());
  EXPECT_EQ(1, v4.ToUint64());
  EXPECT_EQ(True(), v4);

  Value v5 = TestGetSQL(Value::Bool(false));
  EXPECT_EQ(false, v5.bool_value());
  EXPECT_EQ(0, v5.ToDouble());
  EXPECT_EQ(0, v5.ToInt64());
  EXPECT_EQ(0, v5.ToUint64());
  EXPECT_EQ(False(), v5);

  Value v6 = TestGetSQL(Value::Float(3.1415f));
  EXPECT_EQ(3.1415f, v6.float_value());
  EXPECT_EQ(3.1415f, v6.ToDouble());
  EXPECT_EQ("3.1415", v6.DebugString());
  EXPECT_EQ("CAST(3.1415 AS FLOAT32)", v6.GetSQL());
  EXPECT_EQ("3.1415", v6.GetSQLLiteral());

  Value v6b = TestGetSQL(Value::Float(3.0f));
  EXPECT_EQ(3.0f, v6b.float_value());
  EXPECT_EQ(3.0, v6b.ToDouble());
  EXPECT_EQ("3", v6b.DebugString());
  EXPECT_EQ("CAST(3 AS FLOAT32)", v6b.GetSQL());
  EXPECT_EQ("3.0", v6b.GetSQLLiteral());

  Value v7 = TestGetSQL(Value::Double(2.7182818284590451));
  EXPECT_EQ(2.7182818284590451, v7.double_value());
  EXPECT_EQ(2.7182818284590451, v7.ToDouble());
  EXPECT_EQ("2.7182818284590451", v7.GetSQL());

  Value v7b = TestGetSQL(Value::Double(5.0));
  EXPECT_EQ(5.0, v7b.double_value());
  EXPECT_EQ(5.0, v7b.ToDouble());
  EXPECT_EQ("5", v7b.DebugString());
  EXPECT_EQ("5.0", v7b.GetSQL());
  EXPECT_EQ("5.0", v7b.GetSQLLiteral());

  Value v7c = TestGetSQL(Value::Double(-5.0));
  EXPECT_EQ(-5.0, v7c.double_value());
  EXPECT_EQ(-5.0, v7c.ToDouble());
  EXPECT_EQ("-5", v7c.DebugString());
  EXPECT_EQ("-5.0", v7c.GetSQL());
  EXPECT_EQ("-5.0", v7c.GetSQLLiteral());

  Value v7d = TestGetSQL(Value::Double(1e20));
  EXPECT_EQ(1e20, v7d.double_value());
  EXPECT_EQ(1e20, v7d.ToDouble());
  EXPECT_EQ("1e+20", v7d.DebugString());
  EXPECT_EQ("1e+20", v7d.GetSQL());         // No extra ".0" added.
  EXPECT_EQ("1e+20", v7d.GetSQLLiteral());  // No extra ".0" added.

  Value v7e = TestGetSQL(Value::Double(1e-20));
  EXPECT_EQ(1e-20, v7e.double_value());
  EXPECT_EQ(1e-20, v7e.ToDouble());
  double round_trip_double;
  ASSERT_TRUE(absl::SimpleAtod(v7e.DebugString(), &round_trip_double));
  EXPECT_EQ(1e-20, round_trip_double);
  ASSERT_TRUE(absl::SimpleAtod(v7e.GetSQLLiteral(), &round_trip_double));
  EXPECT_EQ(1e-20, round_trip_double);
  EXPECT_EQ("1e-20", v7e.DebugString());
  EXPECT_EQ("1e-20", v7e.GetSQLLiteral());  // No extra ".0" added.

  Value v8 = TestGetSQL(Value::String("honorificabilitudinitatibus"));
  EXPECT_EQ("honorificabilitudinitatibus", v8.string_value());
  EXPECT_EQ("honorificabilitudinitatibus", v8.ToCord());
  EXPECT_EQ("honorificabilitudinitatibus", v8.ToString());
  Value v9 = TestGetSQL(Value::Bytes("honorificabilitudinitatibus"));
  EXPECT_EQ("honorificabilitudinitatibus", v9.bytes_value());
  EXPECT_EQ("honorificabilitudinitatibus", v9.ToCord());
  EXPECT_EQ("honorificabilitudinitatibus", v9.ToString());

  Value v10 = TestGetSQL(Value::Date(12345));
  EXPECT_EQ(12345, v10.date_value());
  EXPECT_EQ(12345, v10.ToDouble());
  EXPECT_EQ(12345, v10.ToInt64());
  Value v11 = TestGetSQL(Value::Date(-12345));
  EXPECT_EQ(-12345, v11.date_value());
  EXPECT_EQ(-12345, v11.ToDouble());
  EXPECT_EQ(-12345, v11.ToInt64());
}

TEST_F(ValueTest, ToStringTests) {
  EXPECT_DEATH(Value::NullString().ToString(), "Null value");
  EXPECT_DEATH(Value::NullBytes().ToString(), "Null value");
  EXPECT_DEATH(Value::Date(0).ToString(), "");
}

TEST_F(ValueTest, DateTests) {
  Value value = TestGetSQL(Value::Date(0));
  EXPECT_EQ("DATE", value.type()->DebugString());
  EXPECT_TRUE(!value.is_null());
  EXPECT_EQ("1970-01-01", value.DebugString());
}

TEST_F(ValueTest, InvalidTimestamps) {
  // Valid values.
  Value::Date(types::kDateMax);
  Value::Date(types::kDateMin);

  // Out of range values.
  EXPECT_DEATH(Value::Date(types::kDateMax + 1),
               "Check failed: value <= types::kDateMax");
  EXPECT_DEATH(Value::Date(types::kDateMin - 1),
               "Check failed: value >= types::kDateMin");
}

TEST_F(ValueTest, NullDeath) {
  EXPECT_DEATH(Value::NullInt32().int32_value(), "Null value");
  EXPECT_DEATH(Value::NullInt64().int64_value(), "Null value");
  EXPECT_DEATH(Value::NullUint32().uint32_value(), "Null value");
  EXPECT_DEATH(Value::NullUint64().uint64_value(), "Null value");
  EXPECT_DEATH(Value::NullBool().bool_value(), "Null value");
  EXPECT_DEATH(Value::NullFloat().float_value(), "Null value");
  EXPECT_DEATH(Value::NullDouble().double_value(), "Null value");
  EXPECT_DEATH(Value::NullString().string_value(), "Null value");
  EXPECT_DEATH(Value::NullBytes().bytes_value(), "Null value");
  EXPECT_DEATH(Value::NullDate().date_value(), "Null value");
}

TEST_F(ValueTest, DistinctValues) {
  std::vector<Value> values = {
      Value::NullInt64(), Value::NullInt32(), Value::NullUint64(),
      Value::NullUint32(), Value::NullBool(), Value::NullFloat(),
      Value::NullDouble(), Value::NullString(), Value::NullBytes(),
      Value::NullDate(),
      Value::Int32(0), Value::Int64(0), Value::Uint32(0), Value::Uint64(0),
      Value::Bool(false), Value::Float(0), Value::Double(0), Value::String(""),
      Value::Bytes(""), Value::Date(0),
  };
  for (int i = 0; i < values.size(); i++) {
    TestGetSQL(values[i]);
    EXPECT_EQ(values[i], values[i]);
    for (int j = 0; j < values.size(); j++) {
      if (i != j) {
        EXPECT_NE(values[i], values[j]);
      }
    }
  }
}

// Returns a number that is larger than 'x' but should compare equal.
template <typename T>
T NextAlmostEqual(T x) {
  return x + 0.5 * 16 * FloatMargin::Ulp(x);
}

// Returns a number that is larger than 'x' and should not compare equal.
template <typename T>
T NextUnequal(T x) {
  return x + 1.5 * 16 * FloatMargin::Ulp(x);
}

TEST_F(ValueTest, AlmostEquals) {
  auto x = Value::Double(3.0);
  auto x_near = Value::Double(NextAlmostEqual(3.0));
  auto x_far = Value::Double(NextUnequal(3.0));
  auto y = Value::Float(7.0);
  auto y_near = Value::Float(NextAlmostEqual(7.0f));
  auto y_far = Value::Float(NextUnequal(7.0f));
  // double
  EXPECT_THAT(x, AlmostEqualsValue(x_near));
  EXPECT_THAT(x, Not(AlmostEqualsValue(x_far)));
  // float
  EXPECT_THAT(y, AlmostEqualsValue(y_near));
  EXPECT_THAT(y, Not(AlmostEqualsValue(y_far)));
  // struct<double>
  EXPECT_THAT(Struct({"v"}, {x}), AlmostEqualsValue(Struct({"v"}, {x_near})));
  EXPECT_THAT(Struct({"v"}, {x}),
              Not(AlmostEqualsValue(Struct({"v"}, {x_far}))));
  // struct<float>
  EXPECT_THAT(Struct({"v"}, {y}), AlmostEqualsValue(Struct({"v"}, {y_near})));
  EXPECT_THAT(Struct({"v"}, {y}),
              Not(AlmostEqualsValue(Struct({"v"}, {y_far}))));
  // array<double>
  EXPECT_THAT(Array({x, x_near}), AlmostEqualsValue(Array({x_near, x})));
  EXPECT_THAT(Array({x, x}), Not(AlmostEqualsValue(Array({x_far, x}))));
  // array<float>
  EXPECT_THAT(Array({y, y_near}), AlmostEqualsValue(Array({y_near, y})));
  EXPECT_THAT(Array({y, y}), Not(AlmostEqualsValue(Array({y_far, y}))));
  // Unordered array<double>
  EXPECT_THAT(
      Array({x, x_near}),
      AlmostEqualsValue(Array({x_near, x}, InternalValue::kIgnoresOrder)));
  // Unordered array<float>
  EXPECT_THAT(
      Array({y, y_near}),
      AlmostEqualsValue(Array({y_near, y}, InternalValue::kIgnoresOrder)));
  // array<string>
  EXPECT_THAT(
      Array({"x", "y"}, InternalValue::kIgnoresOrder),
      AlmostEqualsValue(Array({"y", "x"}, InternalValue::kIgnoresOrder)));
  EXPECT_THAT(
      Array({"x", "y"}, InternalValue::kIgnoresOrder),
      Not(AlmostEqualsValue(Array({"x", "y_"}, InternalValue::kIgnoresOrder))));

  // x and x_far differ by 5 ULP bits.
  int bits = 0;
  while (bits < 10 && !FloatMargin::UlpMargin(bits).Equal(
                          x.double_value(), x_far.double_value())) {
    bits++;
  }
  EXPECT_EQ(5, bits);

  // Values that are very close to zero compare as almost equal even though
  // they differ by orders of magnitude.
  double tiny1 = 1e-15;
  double tiny2 = 1e-150;
  EXPECT_THAT(Value::Double(tiny1), AlmostEqualsValue(Value::Double(tiny2)))
      << kDefaultFloatMargin.PrintError(tiny1, tiny2);

  // Make sure we don't infinite loop comparing recursive protos.
  TypeFactory factory;
  const ProtoType* proto_type;
  GOOGLESQL_ASSERT_OK(factory.MakeProtoType(googlesql_test::RecursivePB::descriptor(),
                                  &proto_type));
  EXPECT_THAT(Array({Value::Proto(proto_type, {})}),
              AlmostEqualsValue(Array({Value::Proto(proto_type, {})},
                                      InternalValue::kIgnoresOrder)));
}

absl::Cord BuildDoubleValueProto(double value) {
  google::protobuf::DoubleValue m;
  m.set_value(value);
  absl::Cord bytes;
  ABSL_CHECK(m.SerializeToCord(&bytes));
  return bytes;
}

TEST_F(ValueTest, AlmostEqualsMessageWithFloatingPointField) {
  TypeFactory factory;
  const ProtoType* proto_type;
  GOOGLESQL_ASSERT_OK(factory.MakeProtoType(google::protobuf::DoubleValue::descriptor(),
                                  &proto_type));
  auto x = Value::Proto(proto_type, BuildDoubleValueProto(3.0));
  auto x_near =
      Value::Proto(proto_type, BuildDoubleValueProto(NextAlmostEqual(3.0)));
  auto x_far = Value::Proto(proto_type, BuildDoubleValueProto(7.0));
  // AlmostEquals
  EXPECT_THAT(x, AlmostEqualsValue(x));
  EXPECT_THAT(x, AlmostEqualsValue(x_near));
  EXPECT_THAT(x, Not(AlmostEqualsValue(x_far)));
  // Equals
  EXPECT_THAT(x, EqualsValue(x));
  EXPECT_THAT(x, Not(EqualsValue(x_near)));
  EXPECT_THAT(x, Not(AlmostEqualsValue(x_far)));
}

TEST_F(ValueTest, AlmostEqualsStructArray) {
  auto x = Value::Double(3.0);
  auto x_near = Value::Double(NextAlmostEqual(3.0));
  // TODO: the current algorithm for multiset comparison in value.cc
  // has trouble dealing with non-transitive almost-equal elements. If x_far
  // below is replaced by Value::Double(NextUnequal(3.0)), x_far becomes
  // almost equal to x_near, x_near is almost equal to x, yet x_far is not
  // almost equal to x; and the test fails. Same is potentially true for
  // y_far.
  auto x_far = Value::Double(NextUnequal(NextUnequal(3.0)));
  auto y = Value::Float(7.0);
  auto y_near = Value::Float(NextAlmostEqual(7.0f));
  auto y_far = Value::Float(NextUnequal(NextUnequal(7.0f)));
  // Multiset [(x,      y), (x_near, y_far)] should be equal to
  // multiset [(x_near, y), (x,      y_far)]. If multiset equality were
  // implemented by ordering the elements, this test would fail because it'd
  // wrongly conclude that (x, y) should match (x, y_far).
  auto r1 = Struct({"x", "y"}, {x, y});
  auto r2 = Struct({"x", "y"}, {x_near, y_far});
  auto s1 = Struct({"x", "y"}, {x_near, y});
  auto s2 = Struct({"x", "y"}, {x, y_far});
  EXPECT_THAT(Array({r1, r2}),
              AlmostEqualsValue(Array({s2, s1}, InternalValue::kIgnoresOrder)));

  // Moreover, we can't fix the sort-based algorithm by first comparing
  // y-members then x-members. To prevent that, we add the symmetric structs:
  // [(x,      y), (x_far, y_near)],
  // [(x, y_near), (x_far, y     )].
  auto r3 = Struct({"x", "y"}, {x, y});
  auto r4 = Struct({"x", "y"}, {x_far, y_near});
  auto s3 = Struct({"x", "y"}, {x, y_near});
  auto s4 = Struct({"x", "y"}, {x_far, y});
  EXPECT_THAT(Array({r3, r4}),
              AlmostEqualsValue(Array({s4, s3}, InternalValue::kIgnoresOrder)));

  // And now we combine the multisets with x-distinct and y-distinct structs.
  EXPECT_THAT(
      Array({r1, r2, r3, r4}),
      AlmostEqualsValue(Array({s2, s1, s4, s3}, InternalValue::kIgnoresOrder)));
}

TEST_F(ValueTest, AlmostEqualsArrayOfProto) {
  TypeFactory factory;
  const ProtoType* proto_type;
  GOOGLESQL_ASSERT_OK(factory.MakeProtoType(google::protobuf::DoubleValue::descriptor(),
                                  &proto_type));
  auto x = Value::Proto(proto_type, BuildDoubleValueProto(3.0));
  auto x_near =
      Value::Proto(proto_type, BuildDoubleValueProto(NextAlmostEqual(3.0)));
  auto x_far = Value::Proto(proto_type, BuildDoubleValueProto(7.0));

  EXPECT_THAT(Array({x}),
              AlmostEqualsValue(Array({x_near}, InternalValue::kIgnoresOrder)));
  EXPECT_THAT(
      Array({x}),
      Not(AlmostEqualsValue(Array({x_far}, InternalValue::kIgnoresOrder))));
}

TEST_F(ValueTest, StructNotNull) {
  Value value = Struct({{"a", Value::Int64(1)}, {"b", Value::Int64(2)}});
  TestGetSQL(value);
  EXPECT_FALSE(value.is_null());
  EXPECT_DEATH(value.int64_value(), "Not an int64 value");
  EXPECT_EQ(2, value.num_fields());
  EXPECT_EQ(1, value.field(0).int64_value());
  EXPECT_EQ(2, value.field(1).int64_value());
  EXPECT_EQ(1, value.FindFieldByName("a").int64_value());
  EXPECT_EQ(2, value.FindFieldByName("b").int64_value());
  EXPECT_FALSE(value.FindFieldByName("junk").is_valid());
  EXPECT_FALSE(value.FindFieldByName("").is_valid());

  EXPECT_EQ("{a:1, b:2}", value.DebugString());

  Value value_copy = value;
  EXPECT_EQ("{a:1, b:2}", value_copy.DebugString());

  Value same_value = Struct({"a", "b"}, {Value::Int64(1), Value::Int64(2)});
  EXPECT_TRUE(value.Equals(same_value));
  EXPECT_EQ(value.DebugString(true), same_value.DebugString(true));
}

TEST_F(ValueTest, StructWithNull) {
  Value value = Struct({"a", "b"}, {Value::Int64(1), Value::Null(Int32Type())});
  Value same_value =
      Struct({"a", "b"}, {Value::Int64(1), Value::Null(Int32Type())});
  EXPECT_TRUE(value.Equals(same_value));
  EXPECT_TRUE(same_value.Equals(value));
  Value different_value =
      Struct({"a", "b"}, {Value::Int64(1), Value::Int32(2)});
  EXPECT_FALSE(value.Equals(different_value));
  EXPECT_FALSE(different_value.Equals(value));
}

TEST_F(ValueTest, StructNull) {
  Value value = TestGetSQL(Value::Null(MakeStructType({{"a", Int64Type()}})));
  EXPECT_TRUE(value.is_null());
  EXPECT_DEATH(value.int64_value(), "Not an int64 value");
  EXPECT_DEATH(value.num_fields(), "Null value");
  EXPECT_DEATH(value.FindFieldByName("junk"), "Null value");
}

TEST_F(ValueTest, StructInvalidConstruction) {
  const StructType* struct_type = MakeStructType({{"a", Int64Type()}});
  // No values
  std::vector<Value> as_vec;
  absl::Span<const Value> as_span;
  std::array<Value, 0> as_array;
  // Initializer list
  EXPECT_THAT(Value::MakeStruct(struct_type, {}),
              StatusIs(absl::StatusCode::kInternal));
  // Span
  EXPECT_THAT(Value::MakeStruct(struct_type, as_span),
              StatusIs(absl::StatusCode::kInternal));
  EXPECT_THAT(Value::MakeStruct(struct_type, as_vec),
              StatusIs(absl::StatusCode::kInternal));
  // Make sure span coercion works correctly.
  EXPECT_THAT(Value::MakeStruct(struct_type, as_array),
              StatusIs(absl::StatusCode::kInternal));
  const std::vector<Value>& as_vec_c = as_vec;
  EXPECT_THAT(Value::MakeStruct(struct_type, as_vec_c),
              StatusIs(absl::StatusCode::kInternal));

  // Move constructor
  EXPECT_THAT(Value::MakeStruct(struct_type, std::move(as_vec)),
              StatusIs(absl::StatusCode::kInternal));
#ifndef NDEBUG
  // When in debug mode, we check anyway, and expect an error.
  EXPECT_THAT(Value::MakeStructFromValidatedInputs(struct_type, {}),
              StatusIs(absl::StatusCode::kInternal));
#else
  // In production, we don't expect any validation, thus OK.
  GOOGLESQL_EXPECT_OK(Value::MakeStructFromValidatedInputs(struct_type, {}));
#endif

  // Deprecated API
  EXPECT_DEATH(Value::Struct(struct_type, {}), "(1 vs. 0)");
#ifndef NDEBUG
  EXPECT_DEATH(Value::UnsafeStruct(struct_type, {}), "(1 vs. 0)");
#endif

  // Type mismatch
  EXPECT_THAT(Value::MakeStruct(struct_type, {Value::String("abc")}),
              StatusIs(absl::StatusCode::kInternal));

  // We actually _do_ validate since we are in debug mode
#ifndef NDEBUG
  EXPECT_THAT(
      Value::MakeStructFromValidatedInputs(struct_type, {Value::String("abc")}),
      StatusIs(absl::StatusCode::kInternal));
#else
  GOOGLESQL_EXPECT_OK(Value::MakeStructFromValidatedInputs(struct_type,
                                                 {Value::String("abc")}));
#endif

  // Deprecated API
  EXPECT_DEATH(Value::Struct(struct_type, {Value::String("abc")}),
               "Field type: INT64");
#ifndef NDEBUG
  EXPECT_DEATH(Value::UnsafeStruct(struct_type, {Value::String("abc")}),
               "Field type: INT64");
#endif
}

TEST_F(ValueTest, StructWithOneAnonymousField) {
  Value value = TestGetSQL(Struct({std::string("")}, {Value::Int64(5)}));
  EXPECT_EQ(1, value.num_fields());
  EXPECT_EQ(5, value.field(0).int64_value());
  EXPECT_FALSE(value.FindFieldByName("").is_valid());
  EXPECT_FALSE(value.FindFieldByName("abc").is_valid());
}

TEST_F(ValueTest, StructWithTwoAnonymousFields) {
  Value value = TestGetSQL(
      Struct({std::string(""), ""}, {Value::Int64(5), Value::String("abc")}));
  EXPECT_EQ(2, value.num_fields());
  EXPECT_EQ(5, value.field(0).int64_value());
  EXPECT_EQ("abc", value.field(1).string_value());
  EXPECT_FALSE(value.FindFieldByName("").is_valid());
}

TEST_F(ValueTest, StructOfStringsFormatting) {
  const auto* string_struct_type =
      MakeStructType({{"a", StringType()}, {"b", StringType()}});
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value struct_of_strings,
      Value::MakeStruct(
          string_struct_type,
          {Value::String("12345678901234567890"), Value::String("abc")}));
  EXPECT_EQ(struct_of_strings.DebugString(/*verbose=*/true),
            R"(Struct{a:String("12345678901234567890"), b:String("abc")})");
  EXPECT_EQ(struct_of_strings.DebugString(),
            R"({a:"12345678901234567890", b:"abc"})");
  EXPECT_EQ(struct_of_strings.Format(), R"(STRUCT<a STRING, b STRING>{
  "12345678901234567890",
  "abc"
})");
  EXPECT_EQ(struct_of_strings.Format(/*print_top_level_type=*/false), R"({
  "12345678901234567890",
  "abc"
})");
  EXPECT_EQ(struct_of_strings.GetSQLLiteral(),
            R"(("12345678901234567890", "abc"))");
  EXPECT_EQ(struct_of_strings.GetSQL(),
            R"(STRUCT<a STRING, b STRING>("12345678901234567890", "abc"))");
}

TEST_F(ValueTest, StructOfArraysOfStringsFormatting) {
  const auto* string_array_type = MakeArrayType(StringType());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value array_of_strings,
      Value::MakeArray(string_array_type,
                       {Value::String("789010992827372"),
                        Value::String("quiteLongVeryBerryString")}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value struct_of_arrays_of_strings,
      Value::MakeStruct(
          MakeStructType({{"a", string_array_type}, {"b", string_array_type}}),
          {array_of_strings, array_of_strings}));
  EXPECT_EQ(
      struct_of_arrays_of_strings.DebugString(/*verbose=*/true),
      R"(Struct{a:Array[String("789010992827372"), String("quiteLongVeryBerryString")], )"
      R"(b:Array[String("789010992827372"), String("quiteLongVeryBerryString")]})");
  EXPECT_EQ(
      struct_of_arrays_of_strings.DebugString(),
      R"({a:["789010992827372", "quiteLongVeryBerryString"], b:["789010992827372", "quiteLongVeryBerryString"]})");
  EXPECT_EQ(struct_of_arrays_of_strings.Format(),
            R"(STRUCT<a ARRAY<>, b ARRAY<>>{
  ARRAY<STRING>[
    "789010992827372",
    "quiteLongVeryBerryString"
  ],
  ARRAY<STRING>[
    "789010992827372",
    "quiteLongVeryBerryString"
  ]
})");
  EXPECT_EQ(struct_of_arrays_of_strings.Format(/*print_top_level_type=*/false),
            R"({ARRAY<STRING>[
   "789010992827372",
   "quiteLongVeryBerryString"
 ],
 ARRAY<STRING>[
   "789010992827372",
   "quiteLongVeryBerryString"
 ]})");
  EXPECT_EQ(
      struct_of_arrays_of_strings.GetSQLLiteral(),
      R"((["789010992827372", "quiteLongVeryBerryString"], ["789010992827372", "quiteLongVeryBerryString"]))");
  EXPECT_EQ(
      struct_of_arrays_of_strings.GetSQL(),
      R"(STRUCT<a ARRAY<STRING>, b ARRAY<STRING>>(ARRAY<STRING>["789010992827372", "quiteLongVeryBerryString"], ARRAY<STRING>["789010992827372", "quiteLongVeryBerryString"]))");
}

TEST_F(ValueTest, StructWithNoFields) {
  Value value = TestGetSQL(Struct({}, {}));
  EXPECT_EQ(0, value.num_fields());
  EXPECT_FALSE(value.FindFieldByName("").is_valid());
  EXPECT_FALSE(value.FindFieldByName("abc").is_valid());

  EXPECT_EQ("STRUCT<>()", value.GetSQL());
  EXPECT_EQ("{}", value.DebugString());
  EXPECT_EQ("Struct{}", value.FullDebugString());
}

TEST_F(ValueTest, ArrayNotNull) {
  Value value = TestGetSQL(Int64Array({1, 2}));
  EXPECT_FALSE(value.is_null());
  EXPECT_DEATH(value.int64_value(), "Not an int64 value");
  EXPECT_EQ(2, value.num_elements());
  EXPECT_EQ(1, value.element(0).int64_value());
  EXPECT_EQ(2, value.element(1).int64_value());
  EXPECT_EQ("[1, 2]", value.DebugString());

  Value value_copy = value;
  EXPECT_EQ("[1, 2]", value_copy.DebugString());
}

TEST_F(ValueTest, ArrayNull) {
  Value value = TestGetSQL(Value::Null(MakeArrayType(Int64Type())));
  EXPECT_TRUE(value.is_null());
  EXPECT_DEATH(value.int64_value(), "Not an int64 value");
  EXPECT_DEATH(value.empty(), "Null value");
  EXPECT_DEATH(value.num_elements(), "Null value");
  EXPECT_DEATH(value.element(0), "Null value");
  EXPECT_EQ("NULL", value.DebugString());
  EXPECT_EQ("Array<INT64>(NULL)", value.FullDebugString());
  Value value_copy = value;
  EXPECT_TRUE(value_copy.is_null());
  EXPECT_TRUE(value.Equals(value_copy));
  Value value2 = TestGetSQL(Value::Null(MakeArrayType(StringType())));
  EXPECT_TRUE(value2.is_null());
  EXPECT_FALSE(value.Equals(value2));
}

TEST_F(ValueTest, ArrayConstruction) {
  const ArrayType* array_type = MakeArrayType(Int64Type());
  // No values
  std::vector<Value> as_vec;
  absl::Span<const Value> as_span;
  std::array<Value, 0> as_array;
  const std::vector<Value>& as_vec_c = as_vec;
  // Initializer list
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value result, Value::MakeArray(array_type, {}));
  EXPECT_THAT(result.elements(), IsEmpty());

  // Span
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(result, Value::MakeArray(array_type, as_span));
  EXPECT_THAT(result.elements(), IsEmpty());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(result, Value::MakeArray(array_type, as_vec));
  EXPECT_THAT(result.elements(), IsEmpty());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(result, Value::MakeArray(array_type, as_array));
  EXPECT_THAT(result.elements(), IsEmpty());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(result, Value::MakeArray(array_type, as_vec_c));
  EXPECT_THAT(result.elements(), IsEmpty());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(result,
                            Value::MakeArray(array_type, std::move(as_vec)));
  EXPECT_THAT(result.elements(), IsEmpty());
}

TEST_F(ValueTest, MakeArrayInvalidValue) {
  const ArrayType* array_type = MakeArrayType(Int64Type());
  Value invalid = Value();
  EXPECT_FALSE(invalid.is_valid());
  // Type mismatch, no crash
  EXPECT_THAT(Value::MakeArray(array_type, {invalid}),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_F(ValueTest, InvalidArrayConstruction) {
  const ArrayType* array_type = MakeArrayType(Int64Type());

  // Type mismatch
  EXPECT_THAT(Value::MakeArray(array_type, {Value::String("abc")}),
              StatusIs(absl::StatusCode::kInternal));

#ifndef NDEBUG
  // We actually _do_ validate since we are in debug mode
  EXPECT_THAT(
      Value::MakeArrayFromValidatedInputs(array_type, {Value::String("abc")}),
      StatusIs(absl::StatusCode::kInternal));
#else
  GOOGLESQL_EXPECT_OK(
      Value::MakeArrayFromValidatedInputs(array_type, {Value::String("abc")}));
#endif
  // Deprecated API
  EXPECT_DEATH(Value::Array(array_type, {Value::String("abc")}),
               "must be of type INT64");
#ifndef NDEBUG
  EXPECT_DEATH(Value::UnsafeArray(array_type, {Value::String("abc")}),
               "must be of type INT64");
#endif
}

TEST_F(ValueTest, NumericArray) {
  Value v = TestGetSQL(values::Int64Array({1, 2}));
  EXPECT_EQ("Array[Int64(1), Int64(2)]", v.FullDebugString());
  EXPECT_EQ("[1, 2]", v.ShortDebugString());
  v = TestGetSQL(values::Int32Array({1, 2}));
  EXPECT_EQ("Array[Int32(1), Int32(2)]", v.FullDebugString());
  EXPECT_EQ("[1, 2]", v.ShortDebugString());
  v = TestGetSQL(values::Uint64Array({1, 2}));
  EXPECT_EQ("Array[Uint64(1), Uint64(2)]", v.FullDebugString());
  EXPECT_EQ("[1, 2]", v.ShortDebugString());
  v = TestGetSQL(values::Uint32Array({1, 2}));
  EXPECT_EQ("Array[Uint32(1), Uint32(2)]", v.FullDebugString());
  EXPECT_EQ("[1, 2]", v.ShortDebugString());
  v = TestGetSQL(values::BoolArray({true, false}));
  EXPECT_EQ("Array[Bool(true), Bool(false)]", v.FullDebugString());
  EXPECT_EQ("[true, false]", v.ShortDebugString());
  v = TestGetSQL(values::FloatArray({1.1, 2.2}));
  EXPECT_EQ("Array[Float(1.1), Float(2.2)]", v.FullDebugString());
  EXPECT_EQ("[1.1, 2.2]", v.ShortDebugString());
  v = TestGetSQL(values::DoubleArray({1.1, 2.2}));
  EXPECT_EQ("Array[Double(1.1), Double(2.2)]", v.FullDebugString());
  EXPECT_EQ("[1.1, 2.2]", v.ShortDebugString());
}

TEST_F(ValueTest, StringArray) {
  Value v1 = TestGetSQL(StringArray({"foo", "bar"}));
  EXPECT_EQ("Array[String(\"foo\"), String(\"bar\")]", v1.FullDebugString());
  EXPECT_EQ("[\"foo\", \"bar\"]", v1.ShortDebugString());
  absl::Cord c1("foo"), c2("bar");
  Value v2 = StringArray({&c1, &c2});
  EXPECT_EQ(v1, v2);
}

TEST_F(ValueTest, BytesArray) {
  Value v1 = TestGetSQL(BytesArray({"foo", "bar"}));
  absl::Cord c1("foo"), c2("bar");
  Value v2 = BytesArray({&c1, &c2});
  EXPECT_EQ("Array[Bytes(b\"foo\"), Bytes(b\"bar\")]", v1.FullDebugString());
  EXPECT_EQ("[b\"foo\", b\"bar\"]", v1.ShortDebugString());
  EXPECT_EQ(v1, v2);
}

TEST_F(ValueTest, FloatArray) {
  Value v1 = TestGetSQL(
      FloatArray({1.5, 2.5, std::numeric_limits<double>::quiet_NaN()}));
  EXPECT_EQ("[1.5, 2.5, nan]", v1.DebugString());
  EXPECT_EQ(
      "ARRAY<FLOAT32>[CAST(1.5 AS FLOAT32), CAST(2.5 AS FLOAT32), "
      "CAST(\"nan\" AS FLOAT32)]",
      v1.GetSQL());
  EXPECT_EQ(
      "ARRAY<FLOAT>[CAST(1.5 AS FLOAT), CAST(2.5 AS FLOAT), "
      "CAST(\"nan\" AS FLOAT)]",
      v1.GetSQL(PRODUCT_INTERNAL));
  EXPECT_EQ(
      "ARRAY<FLOAT>[CAST(1.5 AS FLOAT), CAST(2.5 AS FLOAT), "
      "CAST(\"nan\" AS FLOAT)]",
      v1.GetSQL(PRODUCT_INTERNAL, /*use_external_float32=*/true));
  EXPECT_EQ(
      "ARRAY<FLOAT32>[CAST(1.5 AS FLOAT32), CAST(2.5 AS FLOAT32), "
      "CAST(\"nan\" AS FLOAT32)]",
      v1.GetSQL(PRODUCT_EXTERNAL));
  EXPECT_EQ(
      "ARRAY<FLOAT32>[CAST(1.5 AS FLOAT32), CAST(2.5 AS FLOAT32), "
      "CAST(\"nan\" AS FLOAT32)]",
      v1.GetSQL(PRODUCT_EXTERNAL, /*use_external_float32=*/true));
  EXPECT_EQ("[1.5, 2.5, CAST(\"nan\" AS FLOAT32)]", v1.GetSQLLiteral());
  EXPECT_EQ("[1.5, 2.5, CAST(\"nan\" AS FLOAT)]",
            v1.GetSQLLiteral(PRODUCT_INTERNAL));
  EXPECT_EQ("[1.5, 2.5, CAST(\"nan\" AS FLOAT)]",
            v1.GetSQLLiteral(PRODUCT_INTERNAL, /*use_external_float32=*/true));
  EXPECT_EQ("[1.5, 2.5, CAST(\"nan\" AS FLOAT32)]",
            v1.GetSQLLiteral(PRODUCT_EXTERNAL, /*use_external_float32=*/true));
}

TEST_F(ValueTest, DoubleArray) {
  Value v1 = TestGetSQL(DoubleArray({1.5, 2.5}));
  EXPECT_EQ("[1.5, 2.5]", v1.DebugString());
  EXPECT_EQ("ARRAY<FLOAT64>[1.5, 2.5]", v1.GetSQL(PRODUCT_EXTERNAL));
  EXPECT_EQ("ARRAY<DOUBLE>[1.5, 2.5]", v1.GetSQL(PRODUCT_INTERNAL));
  EXPECT_EQ("[1.5, 2.5]", v1.GetSQLLiteral());
  Value v2 = DoubleArray({1.5, std::numeric_limits<double>::infinity()});
  EXPECT_EQ("[1.5, inf]", v2.DebugString());
  EXPECT_EQ("ARRAY<FLOAT64>[1.5, CAST(\"inf\" AS FLOAT64)]",
            v2.GetSQL(PRODUCT_EXTERNAL));
  EXPECT_EQ("ARRAY<DOUBLE>[1.5, CAST(\"inf\" AS DOUBLE)]",
            v2.GetSQL(PRODUCT_INTERNAL));
  EXPECT_EQ("[1.5, CAST(\"inf\" AS FLOAT64)]",
            v2.GetSQLLiteral(PRODUCT_EXTERNAL));
  EXPECT_EQ("[1.5, CAST(\"inf\" AS DOUBLE)]",
            v2.GetSQLLiteral(PRODUCT_INTERNAL));
}

TEST_F(ValueTest, TimestampArray) {
  Value v1 = TestGetSQL(
      values::TimestampArray({ParseTimeHm("12:00"), ParseTimeHm("13:00")}));
  EXPECT_EQ(v1.DebugString(),
            "[1970-01-01 12:00:00+00, 1970-01-01 13:00:00+00]");
  EXPECT_EQ(v1.FullDebugString(),
            "Array[Timestamp(1970-01-01 12:00:00+00), "
            "Timestamp(1970-01-01 13:00:00+00)]");
  absl::Time t1 = ParseTimeHm("12:00"), t2 = ParseTimeHm("13:00");
  Value v2 = values::TimestampArray({t1, t2});
  EXPECT_EQ(v1, v2);
}

TEST_F(ValueTest, DateArray) {
  Value v1 = TestGetSQL(values::DateArray(
      {absl::CivilDay(1970, 1, 1), absl::CivilDay(1970, 5, 4)}));
  EXPECT_EQ(v1.DebugString(), "[1970-01-01, 1970-05-04]");
  EXPECT_EQ(v1.FullDebugString(), "Array[Date(1970-01-01), Date(1970-05-04)]");
}

TEST_F(ValueTest, TimestampWithPicosecondsArray) {
  Value v1 = values::TimestampArray(
      {*TimestampPicosValue::Create(ParseTimeHm("12:00")),
       *TimestampPicosValue::Create(ParseTimeHm("13:00"))});
  EXPECT_EQ(v1.DebugString(),
            "[1970-01-01 12:00:00+00, 1970-01-01 13:00:00+00]");
  EXPECT_EQ(v1.FullDebugString(),
            "Array[Timestamp(1970-01-01 12:00:00+00), "
            "Timestamp(1970-01-01 13:00:00+00)]");
  EXPECT_EQ(v1.Format(true),
            "ARRAY<TIMESTAMP>[\n"
            "  1970-01-01 12:00:00+00,\n"
            "  1970-01-01 13:00:00+00\n"
            "]");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(TimestampPicosValue t1,
                            TimestampPicosValue::Create(ParseTimeHm("12:00")));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(TimestampPicosValue t2,
                            TimestampPicosValue::Create(ParseTimeHm("13:00")));
  EXPECT_EQ(v1, values::TimestampArray({t1, t2}));
}

TEST_F(ValueTest, JsonArray) {
  Value v1 = TestGetSQL(JsonArray(
      {JSONValue(int64_t{10}), JSONValue(std::string(R"("foo")")),
       JSONValue::ParseJSONString(R"({"a": [1, true, null]})").value()}));
  EXPECT_EQ(R"(Array[Json(10), Json("\"foo\""), Json({"a":[1,true,null]})])",
            v1.FullDebugString());
  EXPECT_EQ(R"([10, "\"foo\"", {"a":[1,true,null]}])", v1.ShortDebugString());
}

TEST_F(ValueTest, ArrayBag) {
  std::vector<Value> values = {Int64(1), Int64(2), Int64(1), NullInt64(),
                               NullInt64()};
  Value bag = InternalValue::ArrayChecked(MakeArrayType(Int64Type()),
                                          InternalValue::kIgnoresOrder,
                                          std::move(values));
  values = {Int64(1), NullInt64(), Int64(1), NullInt64(), Int64(2)};
  Value array = Value::Array(MakeArrayType(Int64Type()), values);
  EXPECT_FALSE(bag == array);
  EXPECT_FALSE(array == bag);
  EXPECT_TRUE(InternalValue::Equals(bag, array));
  Value bag_struct = Struct({"foo"}, {bag});
  Value array_struct = Struct({"foo"}, {array});
  EXPECT_FALSE(bag_struct == array_struct);
  EXPECT_TRUE(InternalValue::Equals(bag_struct, array_struct));
  EXPECT_TRUE(InternalValue::Equals(array_struct, bag_struct));
  // Swap bag_struct and array_struct to exercise assignment operator.
  Value tmp;
  tmp = array_struct;
  array_struct = bag_struct;
  bag_struct = tmp;
  EXPECT_FALSE(bag_struct == array_struct);
  EXPECT_TRUE(InternalValue::Equals(bag_struct, array_struct));
  EXPECT_TRUE(InternalValue::Equals(array_struct, bag_struct));
}

TEST_F(ValueTest, NestedArrayBag) {
  std::vector<std::string> table_columns = {"bool_val", "double_val",
                                            "int64_val", "str_val"};
  auto nested_x = StructArray(table_columns,
                              {
                                  {True(), 0.1, int64_t{1}, "1"},
                                  {False(), 0.2, int64_t{2}, "2"},
                              },
                              InternalValue::kIgnoresOrder);
  auto nested_y = StructArray(table_columns,
                              {
                                  {False(), 0.2, int64_t{2}, "2"},
                                  {True(), 0.1, int64_t{1}, "1"},
                              },
                              InternalValue::kIgnoresOrder);
  auto nested_z =
      StructArray(table_columns,
                  {
                      {False(), 0.2, int64_t{2}, "2"},
                      {False(), 0.2, int64_t{2}, "2"},  // duplicate struct
                      {True(), 0.1, int64_t{1}, "1"},
                  },
                  InternalValue::kIgnoresOrder);
  auto array_x =
      StructArray({"col"}, {{nested_x}}, InternalValue::kIgnoresOrder);
  auto array_xx =
      StructArray({"col"}, {{nested_x}}, InternalValue::kIgnoresOrder);
  auto array_y =
      StructArray({"col"}, {{nested_y}}, InternalValue::kIgnoresOrder);
  auto array_z =
      StructArray({"col"}, {{nested_z}}, InternalValue::kIgnoresOrder);
  // Hash code is order-insensitive.
  TestHashEqual(array_x, array_y);
  TestHashEqual(array_x.element(0), array_y.element(0));
  // But operator== is order-sensitive.
  EXPECT_TRUE(array_x == array_xx);
  EXPECT_FALSE(array_x == array_y);
  std::string reason;
  EXPECT_TRUE(InternalValue::Equals(
      array_x, array_y, ValueEqualityCheckOptions{.reason = &reason}))
      << reason;
  EXPECT_TRUE(reason.empty());
  EXPECT_FALSE(InternalValue::Equals(
      array_x, array_z, ValueEqualityCheckOptions{.reason = &reason}));
  EXPECT_FALSE(reason.empty());
  ABSL_LOG(INFO) << "Reason: " << reason;
}

// This tests that InternalValue::Equals takes an overall view of
// kIgnoresOrder rather than a purely local view (e.g. b/22417506).
TEST_F(ValueTest, AsymmetricNestedArrayBag) {
  auto v1 = Value::Int64(1);
  auto v2 = Value::Int64(2);
  auto vN = Value::MakeNull<int64_t>();
  auto struct_ord1 = Struct({""}, {Array({v1, v2, vN}, kPreservesOrder)});
  auto struct_ord2 = Struct({""}, {Array({v2, vN, v1}, kPreservesOrder)});
  auto struct_unord1 = Struct({""}, {Array({v1, v2, vN}, kIgnoresOrder)});
  auto struct_unord2 = Struct({""}, {Array({vN, v1, v2}, kIgnoresOrder)});

  std::string why = "";

  // Positive tests.
  EXPECT_TRUE(InternalValue::Equals(
      Array({struct_ord1, struct_ord2}), Array({struct_unord1, struct_unord2}),
      ValueEqualityCheckOptions{.float_margin = FloatMargin::UlpMargin(0),
                                .reason = &why}))
      << why;

  EXPECT_TRUE(InternalValue::Equals(
      Array({struct_ord1, struct_ord2}, kPreservesOrder),
      Array({struct_unord1, struct_unord2}, kIgnoresOrder),
      ValueEqualityCheckOptions{.float_margin = FloatMargin::UlpMargin(0),
                                .reason = &why}))
      << why;

  EXPECT_TRUE(InternalValue::Equals(
      Array({struct_unord1, struct_unord2}), Array({struct_ord1, struct_ord2}),
      ValueEqualityCheckOptions{.float_margin = FloatMargin::UlpMargin(0),
                                .reason = &why}))
      << why;

  EXPECT_TRUE(InternalValue::Equals(
      Array({struct_unord1, struct_ord2}), Array({struct_ord1, struct_ord2}),
      ValueEqualityCheckOptions{.float_margin = FloatMargin::UlpMargin(0),
                                .reason = &why}))
      << why;

  EXPECT_TRUE(InternalValue::Equals(
      Array({struct_ord1, struct_ord1}), Array({struct_unord1, struct_unord2}),
      ValueEqualityCheckOptions{.float_margin = FloatMargin::UlpMargin(0),
                                .reason = &why}))
      << why;

  // Negative tests.
  EXPECT_FALSE(InternalValue::Equals(
      Array({struct_ord1, struct_ord1}), Array({struct_ord1, struct_ord2}),
      ValueEqualityCheckOptions{.float_margin = FloatMargin::UlpMargin(0),
                                .reason = &why}))
      << why;
}

// A test demonstrating the subtlety of orderedness considerations in
// InternalValue::Equals, based on the interpretation of DeepOrderKindSpec.
TEST_F(ValueTest, AsymmetricNestedArrayBagEqualityConsidersOrderednessBySlot) {
  auto ordered_a_b = Struct({""}, {Array({"a", "b"})});
  auto ordered_c_d = Struct({""}, {Array({"c", "d"})});
  auto ordered_d_c = Struct({""}, {Array({"d", "c"})});
  auto unordered_b_a =
      Struct({""}, {Array({"b", "a"}, InternalValue::kIgnoresOrder)});

  // This comparison is false using .Equals(), because this method does not
  // consider bags: [a, b] != [b, a]
  EXPECT_FALSE(Array({ordered_a_b, ordered_c_d})
                   .Equals(Array({unordered_b_a, ordered_c_d})));

  // The same values are found equal using InternalValue::Equals, which does
  // consider bags: [a, b](ord) == [b, a](bag)
  EXPECT_TRUE(InternalValue::Equals(Array({ordered_a_b, ordered_c_d}),
                                    Array({unordered_b_a, ordered_c_d})));

  // These arrays are not equal because [c, d] != [d, c]
  EXPECT_FALSE(InternalValue::Equals(Array({ordered_a_b, ordered_c_d}),
                                     Array({ordered_a_b, ordered_d_c})));

  // [a, b](ord) is changed to [b, a](bag) and the equality becomes true.
  // Orderedness here is considered based on struct "slot", but not array
  // position. Adding in [b, a](bag) results in all values at struct
  // property 0 being considered unordered, regardless of array index.
  EXPECT_TRUE(InternalValue::Equals(Array({unordered_b_a, ordered_c_d}),
                                    Array({ordered_a_b, ordered_d_c})));

  // This only applies to the specific struct index, though. Here, struct
  // property 0 is unordered, but struct property 1 is still ordered.
  EXPECT_FALSE(InternalValue::Equals(
      Array({Struct({"x", "y"}, {unordered_b_a, ordered_c_d})}),
      Array({Struct({"x", "y"}, {ordered_a_b, ordered_d_c})})));

  // Swapping the positions of structs in the ordered outer array is not equal,
  // regardless of the orderedness of inner values.
  EXPECT_FALSE(InternalValue::Equals(Array({unordered_b_a, ordered_c_d}),
                                     Array({ordered_d_c, ordered_a_b})));
}

TEST_F(ValueTest, ArrayOfInt64Formatting) {
  const auto* int_array_type = MakeArrayType(Int64Type());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value array_of_ints,
      Value::MakeArray(int_array_type, {Value::Int64(1), Value::NullInt64()}));
  EXPECT_EQ(array_of_ints.DebugString(/*verbose=*/true),
            "Array[Int64(1), Int64(NULL)]");
  EXPECT_EQ(array_of_ints.DebugString(), "[1, NULL]");
  EXPECT_EQ(array_of_ints.Format(), "ARRAY<INT64>[1, NULL]");
  EXPECT_EQ(array_of_ints.Format(/*print_top_level_type=*/false),
            "ARRAY<INT64>[1, NULL]");
  EXPECT_EQ(array_of_ints.GetSQLLiteral(), "[1, NULL]");
  EXPECT_EQ(array_of_ints.GetSQL(), "ARRAY<INT64>[1, CAST(NULL AS INT64)]");
}

TEST_F(ValueTest, ArrayOfStringFormatting) {
  const auto* string_array_type = MakeArrayType(StringType());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value array_of_strings,
      Value::MakeArray(
          string_array_type,
          {Value::String("12345678901234567890"), Value::String("abc")}));
  EXPECT_EQ(array_of_strings.DebugString(/*verbose=*/true),
            R"(Array[String("12345678901234567890"), String("abc")])");
  EXPECT_EQ(array_of_strings.DebugString(),
            R"(["12345678901234567890", "abc"])");
  EXPECT_EQ(array_of_strings.Format(), R"(ARRAY<STRING>[
  "12345678901234567890",
  "abc"
])");
  EXPECT_EQ(array_of_strings.Format(/*print_top_level_type=*/false),
            R"(ARRAY<STRING>[
  "12345678901234567890",
  "abc"
])");
  EXPECT_EQ(array_of_strings.GetSQLLiteral(),
            R"(["12345678901234567890", "abc"])");
  EXPECT_EQ(array_of_strings.GetSQL(),
            R"(ARRAY<STRING>["12345678901234567890", "abc"])");
}

TEST_F(ValueTest, ArrayOfStructsOfStringsFormatting) {
  const auto* string_struct_type =
      MakeStructType({{"a", StringType()}, {"b", StringType()}});
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value struct_of_strings,
      Value::MakeStruct(
          string_struct_type,
          {Value::String("5938"), Value::String("longFunctionInvocation, 2")}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value array_of_structs_of_strings,
      Value::MakeArray(MakeArrayType(string_struct_type),
                       {struct_of_strings, struct_of_strings,
                        Value::Null(string_struct_type)}));
  EXPECT_EQ(
      array_of_structs_of_strings.DebugString(/*verbose=*/true),
      R"(Array[Struct{a:String("5938"), b:String("longFunctionInvocation, 2")}, )"
      R"(Struct{a:String("5938"), b:String("longFunctionInvocation, 2")}, Struct(NULL)])");
  EXPECT_EQ(
      array_of_structs_of_strings.DebugString(),
      R"([{a:"5938", b:"longFunctionInvocation, 2"}, {a:"5938", b:"longFunctionInvocation, 2"}, NULL])");
  EXPECT_EQ(array_of_structs_of_strings.Format(),
            R"(ARRAY<STRUCT<a STRING, b STRING>>[
  {
    "5938",
    "longFunctionInvocation, 2"
  },
  {
    "5938",
    "longFunctionInvocation, 2"
  },
  NULL
])");
  EXPECT_EQ(array_of_structs_of_strings.Format(/*print_top_level_type=*/false),
            R"(ARRAY<STRUCT<a STRING, b STRING>>[
  {
    "5938",
    "longFunctionInvocation, 2"
  },
  {
    "5938",
    "longFunctionInvocation, 2"
  },
  NULL
])");
  EXPECT_EQ(
      array_of_structs_of_strings.GetSQLLiteral(),
      R"([("5938", "longFunctionInvocation, 2"), ("5938", "longFunctionInvocation, 2"), NULL])");
  EXPECT_EQ(
      array_of_structs_of_strings.GetSQL(),
      R"(ARRAY<STRUCT<a STRING, b STRING>>[STRUCT<a STRING, b STRING>("5938", "longFunctionInvocation, 2"), STRUCT<a STRING, b STRING>("5938", "longFunctionInvocation, 2"), CAST(NULL AS STRUCT<a STRING, b STRING>)])");
}

TEST_F(ValueTest, NestedStructContainerStringFormatting) {
  Value v = Array({Struct(
      {{"a", Struct({{"q", Int64(1)}})},
       {"b", Map({{Struct({{"a", Int64(1)}}),
                   Array({Struct({{"r", Range(Date(1), Date(2))}})})}})}})});

  EXPECT_EQ(v.DebugString(/*verbose=*/true),
            "Array[Struct{a:Struct{q:Int64(1)}, b:Map{Struct{a:Int64(1)}: "
            "Array[Struct{r:Range[Date(1970-01-02), Date(1970-01-03))}]}}]");
  EXPECT_EQ(v.DebugString(),
            R"([{a:{q:1}, b:{{a:1}: [{r:[1970-01-02, 1970-01-03)}]}}])");

  EXPECT_EQ(v.Format(),
            R"(ARRAY<STRUCT<
        a STRUCT<q INT64>,
        b MAP<STRUCT<a INT64>, ARRAY<STRUCT<r RANGE<DATE>>>>
      >>
[{
   {1},
   {{a:1}: [{r:[1970-01-02, 1970-01-03)}]}
 }])");
  EXPECT_EQ(v.Format(/*print_top_level_type=*/false),
            R"(ARRAY<STRUCT<
        a STRUCT<q INT64>,
        b MAP<STRUCT<a INT64>, ARRAY<STRUCT<r RANGE<DATE>>>>
      >>
[{
   {1},
   {{a:1}: [{r:[1970-01-02, 1970-01-03)}]}
 }])");

  EXPECT_EQ(
      v.GetSQLLiteral(PRODUCT_EXTERNAL),
      R"sql([(STRUCT(1), MAP_FROM_ARRAY([(STRUCT(1), [STRUCT(RANGE<DATE> "[1970-01-02, 1970-01-03)")])]))])sql");
  EXPECT_EQ(
      v.GetSQL(PRODUCT_EXTERNAL),
      R"sql(ARRAY<STRUCT<a STRUCT<q INT64>, b MAP<STRUCT<a INT64>, ARRAY<STRUCT<r RANGE<DATE>>>>>>[STRUCT<a STRUCT<q INT64>, b MAP<STRUCT<a INT64>, ARRAY<STRUCT<r RANGE<DATE>>>>>(STRUCT<q INT64>(1), MAP_FROM_ARRAY(ARRAY<STRUCT<STRUCT<a INT64>, ARRAY<STRUCT<r RANGE<DATE>>>>>[(STRUCT<a INT64>(1), ARRAY<STRUCT<r RANGE<DATE>>>[STRUCT<r RANGE<DATE>>(RANGE<DATE> "[1970-01-02, 1970-01-03)")])]))])sql");
}

TEST_F(ValueTest, NestedRangeContainerStringFormatting) {
  const Type* date_range_type = MakeRangeType(DateType());
  const auto* date_range_array_type = MakeArrayType(date_range_type);
  const auto* struct_of_date_range_array_type = MakeStructType(
      {{"a", date_range_array_type}, {"b", date_range_array_type}});
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value struct_of_date_range_array,
      Value::MakeStruct(struct_of_date_range_array_type,
                        {Array({Range(Date(1), Date(2))}),
                         Value::Null(date_range_array_type)}));
  EXPECT_EQ(struct_of_date_range_array.DebugString(/*verbose=*/true),
            "Struct{a:Array[Range[Date(1970-01-02), Date(1970-01-03))], "
            "b:Array<RANGE<DATE>>(NULL)}");
  EXPECT_EQ(struct_of_date_range_array.DebugString(),
            R"({a:[[1970-01-02, 1970-01-03)], b:NULL})");
  EXPECT_EQ(struct_of_date_range_array.Format(),
            R"(STRUCT<a ARRAY<>, b ARRAY<>>{
  ARRAY<RANGE<DATE>>[[1970-01-02, 1970-01-03)],
  ARRAY<RANGE<DATE>>(NULL)
})");
  EXPECT_EQ(struct_of_date_range_array.Format(/*print_top_level_type=*/false),
            R"({
  ARRAY<RANGE<DATE>>[[1970-01-02, 1970-01-03)],
  ARRAY<RANGE<DATE>>(NULL)
})");
  EXPECT_EQ(
      struct_of_date_range_array.GetSQLLiteral(ProductMode::PRODUCT_EXTERNAL),
      R"raw(([RANGE<DATE> "[1970-01-02, 1970-01-03)"], NULL))raw");
  EXPECT_EQ(
      struct_of_date_range_array.GetSQL(ProductMode::PRODUCT_EXTERNAL),
      R"raw(STRUCT<a ARRAY<RANGE<DATE>>, b ARRAY<RANGE<DATE>>>(ARRAY<RANGE<DATE>>[RANGE<DATE> "[1970-01-02, 1970-01-03)"], CAST(NULL AS ARRAY<RANGE<DATE>>)))raw");
}

TEST(MapValueTest, MapConstructionInitializerList) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* map_type,
                            MakeMapType(Int64Type(), Int64Type()));

  EXPECT_THAT(Value::MakeMap(map_type, {}),
              IsOkAndHolds(MapEntriesWhere(IsEmpty())));

  EXPECT_THAT(Value::MakeMap(
                  map_type, {std::make_pair(Value::Int64(1), Value::Int64(2))}),
              IsOkAndHolds(MapEntriesWhere(
                  ElementsAre(Pair(Value::Int64(1), Value::Int64(2))))));
}

TEST(MapValueTest, MapConstructionSpan) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* map_type,
                            MakeMapType(Int64Type(), Int64Type()));

  EXPECT_THAT(
      Value::MakeMap(map_type,
                     absl::Span<const std::pair<const Value, const Value>>{}),
      IsOkAndHolds(MapEntriesWhere(IsEmpty())));

  std::vector<std::pair<const Value, const Value>> kv_vec = {
      std::make_pair(Value::Int64(1), Value::Int64(2))};
  // kv_vec is converted to absl::Span.
  EXPECT_THAT(Value::MakeMap(map_type, kv_vec),
              IsOkAndHolds(MapEntriesWhere(
                  ElementsAre(Pair(Value::Int64(1), Value::Int64(2))))));
}

TEST(MapValueTest, MapConstructionRvalueVector) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* map_type,
                            MakeMapType(Int64Type(), Int64Type()));

  EXPECT_THAT(Value::MakeMap(map_type, std::vector<std::pair<Value, Value>>{}),
              IsOkAndHolds(MapEntriesWhere(IsEmpty())));

  std::vector<std::pair<Value, Value>> kv_vec = {
      std::make_pair(Value::Int64(1), Value::Int64(2))};
  EXPECT_THAT(Value::MakeMap(map_type, std::move(kv_vec)),
              IsOkAndHolds(MapEntriesWhere(
                  ElementsAre(Pair(Value::Int64(1), Value::Int64(2))))));
}

TEST(MapValueTest, MapConstructionMapView) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* map_type,
                            MakeMapType(Int64Type(), Int64Type()));

  EXPECT_THAT(Value::MakeMap(map_type, absl::flat_hash_map<Value, Value>()),
              IsOkAndHolds(MapEntriesWhere(IsEmpty())));

  absl::flat_hash_map<Value, Value> kv_map = {
      std::make_pair(Value::Int64(1), Value::Int64(2))};
  EXPECT_THAT(Value::MakeMap(map_type, kv_map),
              IsOkAndHolds(MapEntriesWhere(
                  ElementsAre(Pair(Value::Int64(1), Value::Int64(2))))));
}

struct MapConstructionInvalidTestParams {
  const Type* key_type;
  const Type* value_type;
  const std::vector<std::pair<const Value, const Value>> map_entries;
  const std::string err_expected_type;
  const std::pair<std::string, std::string> err_actual_type;
};

#ifdef DEBUG  // Map values are only type checked in debug mode.
class MapConstructionInvalidTest
    : public ::testing::TestWithParam<MapConstructionInvalidTestParams> {};

TEST_P(MapConstructionInvalidTest, MapConstructionInvalid) {
  auto params = GetParam();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* map_type,
                            MakeMapType(params.key_type, params.value_type));
  EXPECT_THAT(
      Value::MakeMap(map_type, params.map_entries),
      StatusIs(
          absl::StatusCode::kInternal,
          ::testing::AllOf(
              HasSubstr(absl::StrCat("Expected ", params.err_expected_type)),
              HasSubstr(absl::StrCat(
                  "entry with key of ", params.err_actual_type.first,
                  " and value of ", params.err_actual_type.second)))));
}

INSTANTIATE_TEST_SUITE_P(
    MapConstructionTest, MapConstructionInvalidTest,
    ::testing::ValuesIn<MapConstructionInvalidTestParams>(
        {{.key_type = Int64Type(),
          .value_type = DoubleType(),
          .map_entries = {std::make_pair(Value::String("a"),
                                         Value::Bool(true))},
          .err_expected_type = "MAP<INT64, DOUBLE>",
          .err_actual_type = std::make_pair("STRING", "BOOL")},
         {.key_type = DateType(),
          .value_type = Int32Type(),
          .map_entries = {std::make_pair(Value::String("a"), Value::Int32(1))},
          .err_expected_type = "MAP<DATE, INT32>",
          .err_actual_type = std::make_pair("STRING", "INT32")},
         {.key_type = StringType(),
          .value_type = Int64Type(),
          .map_entries = {std::make_pair(Value::String("a"), Value::Date(1))},
          .err_expected_type = "MAP<STRING, INT64>",
          .err_actual_type = std::make_pair("STRING", "DATE")},
         {.key_type = BoolType(),
          .value_type = Int64Type(),
          .map_entries = {std::make_pair(Value::Bool(true), Value::Int32(1))},
          .err_expected_type = "MAP<BOOL, INT64>",
          .err_actual_type = std::make_pair("BOOL", "INT32")}}));
#endif

struct MapConstructionValidTestParams {
  const Type* key_type;
  const Type* value_type;
  const std::vector<std::pair<const Value, const Value>> map_entries;
};

class MapConstructionValidTest
    : public ::testing::TestWithParam<MapConstructionValidTestParams> {};

TEST_P(MapConstructionValidTest, MapConstruction) {
  auto params = GetParam();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* map_type,
                            MakeMapType(params.key_type, params.value_type));

  EXPECT_THAT(
      Value::MakeMap(map_type, params.map_entries),
      IsOkAndHolds(MapEntriesWhere(ElementsAreArray(params.map_entries))));
}

INSTANTIATE_TEST_SUITE_P(
    MapConstructionTest, MapConstructionValidTest,
    ::testing::ValuesIn<MapConstructionValidTestParams>({
        {
            .key_type = StringType(),
            .value_type = JsonType(),
            .map_entries = {},
        },
        {
            .key_type = Int64Type(),
            .value_type = DoubleType(),
            .map_entries = {std::make_pair(Value::Int64(1), Value::Double(2))},
        },
        {
            .key_type = FloatType(),
            .value_type = BoolType(),
            .map_entries =
                {std::make_pair(Value::Float(1.0), Value::Bool(true)),
                 std::make_pair(Value::Float(2.0), Value::Bool(false))},
        },
    }));

struct MapDuplicatesTestParams {
  const absl::StatusOr<const Type*> map_type;
  const std::vector<std::pair<const Value, const Value>> map_entries;
  const bool expect_ok = true;
  const std::string failure_duplicate_key;
};

class MapDuplicatesTest
    : public ::testing::TestWithParam<MapDuplicatesTestParams> {};

TEST_P(MapDuplicatesTest, MapConstructionDuplicateKeysError) {
  const auto& params = GetParam();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* map_type, params.map_type);
  const auto& result = Value::MakeMap(map_type, params.map_entries);

  if (params.expect_ok) {
    GOOGLESQL_EXPECT_OK(result);
  } else {
    EXPECT_THAT(
        result,
        StatusIs(
            absl::StatusCode::kOutOfRange,
            AllOf(HasSubstr("Duplicate map key"),
                  EndsWith(absl::StrCat(": ", params.failure_duplicate_key)))));
  }
}
INSTANTIATE_TEST_SUITE_P(
    MapDuplicatesTest, MapDuplicatesTest,
    ::testing::ValuesIn<MapDuplicatesTestParams>({
        // Basic success tests.
        {
            .map_type = MakeMapType(Int64Type(), JsonType()),
            .map_entries = {},
        },
        {
            .map_type = MakeMapType(Int64Type(), DoubleType()),
            .map_entries = {std::make_pair(Value::Int64(1), Value::Double(2))},
        },
        {
            .map_type = MakeMapType(FloatType(), BoolType()),
            .map_entries =
                {std::make_pair(Value::Float(1.0), Value::Bool(true)),
                 std::make_pair(Value::Float(2.0), Value::Bool(false))},
        },
        // Basic duplicate key failure tests.
        {
            .map_type = MakeMapType(Int64Type(), Int64Type()),
            .map_entries = {{Value::Int64(1), Value::Int64(1)},
                            {Value::Int64(2), Value::Int64(1)},
                            {Value::Int64(2), Value::Int64(3)}},
            .expect_ok = false,
            .failure_duplicate_key = "2",
        },
        {
            .map_type = MakeMapType(Int64Type(), Int64Type()),
            .map_entries = {{Value::Int64(1), Value::Int64(1)},
                            {Value::Int64(2), Value::Int64(1)},
                            {Value::Int64(2), Value::Int64(3)},
                            {Value::Int64(1), Value::Int64(3)}},
            .expect_ok = false,
            .failure_duplicate_key = "2",
        },
        {
            .map_type = MakeMapType(Int64Type(), Int64Type()),
            .map_entries = {{Value::Int64(1), Value::Int64(1)},
                            {Value::NullInt64(), Value::Int64(1)},
                            {Value::NullInt64(), Value::Int64(3)}},
            .expect_ok = false,
            .failure_duplicate_key = "NULL",
        },
        // Float complex value success tests.
        {
            .map_type = MakeMapType(FloatType(), Int64Type()),
            .map_entries =
                {{Value::Float(1), Value::Int64(1)},
                 {Value::NullFloat(), Value::Int64(1)},
                 {Value::Float(std::numeric_limits<float>::quiet_NaN()),
                  Value::Int64(1)},
                 {Value::Float(std::numeric_limits<float>::infinity()),
                  Value::Int64(1)},
                 {Value::Float(-std::numeric_limits<float>::infinity()),
                  Value::Int64(1)}},
            .expect_ok = true,
        },
        // Float complex value duplicate key tests.
        {
            .map_type = MakeMapType(FloatType(), Int64Type()),
            .map_entries =
                {{Value::Float(1), Value::Int64(1)},
                 {Value::Float(std::numeric_limits<float>::infinity()),
                  Value::Int64(1)},
                 {Value::Float(std::numeric_limits<float>::infinity()),
                  Value::Int64(3)}},
            .expect_ok = false,
            .failure_duplicate_key = "inf",
        },
        {
            .map_type = MakeMapType(FloatType(), Int64Type()),
            .map_entries =
                {{Value::Float(1), Value::Int64(1)},
                 {Value::Float(std::numeric_limits<float>::quiet_NaN()),
                  Value::Int64(1)},
                 {Value::Float(std::numeric_limits<float>::quiet_NaN()),
                  Value::Int64(3)}},
            .expect_ok = false,
            .failure_duplicate_key = "nan",
        },
    }));

TEST_F(ValueTest, MapValueEquality) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* map_type_string,
                            MakeMapType(StringType(), StringType()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* map_type_int64,
                            MakeMapType(Int64Type(), Int64Type()));

  EXPECT_THAT(Null(map_type_string), ValueAndHashEq(Null(map_type_string)));
  EXPECT_THAT(Null(map_type_string), ValueAndHashNotEq(Null(map_type_int64)));
  EXPECT_THAT(Null(map_type_string), ValueAndHashNotEq(Map({{"a", "b"}})));
  EXPECT_THAT(Map({{"a", "b"}}), ValueAndHashEq(Map({{"a", "b"}})));

  Value map_of_string = Map({{"a", "b"}});
  EXPECT_THAT(map_of_string, ValueAndHashEq(map_of_string));

  EXPECT_THAT(Map({{"a", "b"}}),
              ValueAndHashNotEq(Map({{"a", "b"}, {"c", "d"}})));
  EXPECT_THAT(Map({{"a", "b"}, {"c", "d"}}),
              ValueAndHashEq(Map({{"a", "b"}, {"c", "d"}})));

  EXPECT_THAT(
      Map({{"a", "b"}, {"c", "d"}}),
      ValueAndHashNotEq(Map({{"a", "b"}, {NullString(), NullString()}})));

  EXPECT_THAT(Map({{"a", "b"}, {NullString(), NullString()}}),
              ValueAndHashEq(Map({{"a", "b"}, {NullString(), NullString()}})));

  EXPECT_THAT(Map({{NullString(), "a"}, {"b", NullString()}}),
              ValueAndHashEq(Map({{NullString(), "a"}, {"b", NullString()}})));

  Value map_of_array_0 = Map({{"a", Value::Null(types::Int64ArrayType())}});
  Value map_of_array_1 =
      Map({{"a", Array({Value::Int64(1), Value::Int64(2)})}});
  Value map_of_array_2 = Map({{"a", Array({Value::Int64(1)})}});

  EXPECT_THAT(map_of_array_0, ValueAndHashEq(map_of_array_0));
  EXPECT_THAT(map_of_array_0, ValueAndHashNotEq(map_of_array_1));
  EXPECT_THAT(map_of_array_1, ValueAndHashEq(map_of_array_1));
  EXPECT_THAT(map_of_array_1, ValueAndHashNotEq(map_of_array_2));
  EXPECT_THAT(map_of_array_2, ValueAndHashEq(map_of_array_2));
  EXPECT_THAT(map_of_array_2, ValueAndHashNotEq(map_of_array_0));

  Value array_of_map_of_array_1 = Array({map_of_array_1});
  Value array_of_map_of_array_2 = Array({map_of_array_2});

  EXPECT_THAT(array_of_map_of_array_1, ValueAndHashEq(array_of_map_of_array_1));
  EXPECT_THAT(array_of_map_of_array_1,
              ValueAndHashNotEq(array_of_map_of_array_2));
  EXPECT_THAT(array_of_map_of_array_2, ValueAndHashEq(array_of_map_of_array_2));
}

TEST_F(ValueTest, MapValueEqualitySimpleInternalValue) {
  auto map_1 = Map({{"a", "b"}});
  auto map_2 = Map({{"a", "b"}, {"c", "d"}});
  auto map_3 = Map({{"a", "c"}, {"c", "d"}});

  EXPECT_TRUE(InternalValue::Equals(map_1, map_1));

  EXPECT_FALSE(InternalValue::Equals(map_1, map_2));
  EXPECT_FALSE(InternalValue::Equals(map_2, map_1));

  EXPECT_TRUE(InternalValue::Equals(map_2, map_2));

  EXPECT_FALSE(InternalValue::Equals(map_2, map_3));
  EXPECT_FALSE(InternalValue::Equals(map_3, map_2));

  EXPECT_TRUE(InternalValue::Equals(map_3, map_3));

  EXPECT_FALSE(InternalValue::Equals(map_3, map_1));
  EXPECT_FALSE(InternalValue::Equals(map_1, map_3));

  std::string why = "";
  EXPECT_FALSE(InternalValue::Equals(
      map_1, map_2, ValueEqualityCheckOptions{.reason = &why}));
  EXPECT_EQ(why,
            "Number of map entries was not equal: {1} and {2} in respective "
            "maps {{\"a\": \"b\"}} and {{\"a\": \"b\", \"c\": \"d\"}}\n");

  EXPECT_FALSE(InternalValue::Equals(
      map_2, map_1, ValueEqualityCheckOptions{.reason = &why}));
  EXPECT_EQ(why,
            "Number of map entries was not equal: {2} and {1} in respective "
            "maps {{\"a\": \"b\", \"c\": \"d\"}} and {{\"a\": \"b\"}}\n");
}

TEST_F(ValueTest, MapValueEqualityWithFloatMarginOptions) {
  auto map_1 = Map({{"a", 0.5f}});
  auto map_2 = Map({{"a", 0.50000006f}});
  auto map_3 = Map({{0.5f, "a"}});
  auto map_4 = Map({{0.50000006f, "a"}});
  auto map_5 = Map({
      {0.5f, "a"},
      {0.50000006f, "b"},
  });
  auto map_6 = Map({
      {0.5f, "a"},
      {0.50000006f, "a"},
  });
  auto map_7 = Map({
      {NullString(), 0.5f},
  });
  auto map_8 = Map({
      {NullString(), 0.50000006f},
  });

  std::string why = "";
  EXPECT_FALSE(InternalValue::Equals(
      map_1, map_2,
      ValueEqualityCheckOptions{.float_margin = kExactFloatMargin,
                                .reason = &why}));
  EXPECT_EQ(why,
            "The value for key {\"a\"} did not match. Value was {0.5} and "
            "{0.50000006} in respective maps {{\"a\": 0.5}} and {{\"a\": "
            "0.50000006}}.\n");

  EXPECT_TRUE(InternalValue::Equals(
      map_1, map_2,
      ValueEqualityCheckOptions{.float_margin = kDefaultFloatMargin,
                                .reason = &why}))
      << why;
  EXPECT_FALSE(InternalValue::Equals(
      map_3, map_4,
      ValueEqualityCheckOptions{.float_margin = kExactFloatMargin,
                                .reason = &why}));
  EXPECT_EQ(why,
            "Key {0.5} did not exist in both maps. Present in {{0.5: \"a\"}} "
            "but not present in {{0.50000006: \"a\"}}.\n");

  EXPECT_TRUE(InternalValue::Equals(
      map_3, map_4,
      ValueEqualityCheckOptions{.float_margin = kDefaultFloatMargin,
                                .reason = &why}))
      << why;

  EXPECT_FALSE(InternalValue::Equals(
      map_5, map_6,
      ValueEqualityCheckOptions{.float_margin = kDefaultFloatMargin,
                                .reason = &why}));
  EXPECT_EQ(why,
            "The value for key {0.50000006} did not match. Value was {\"b\"} "
            "and {\"a\"} in respective maps {{0.5: \"a\", 0.50000006: \"b\"}} "
            "and {{0.5: \"a\", 0.50000006: \"a\"}}.\n");

  EXPECT_FALSE(InternalValue::Equals(
      map_6, map_5,
      ValueEqualityCheckOptions{.float_margin = kDefaultFloatMargin,
                                .reason = &why}));
  EXPECT_EQ(why,
            "The value for key {0.50000006} did not match. Value was {\"b\"} "
            "and {\"a\"} in respective maps {{0.5: \"a\", 0.50000006: \"b\"}} "
            "and {{0.5: \"a\", 0.50000006: \"a\"}}.\n");

  EXPECT_FALSE(InternalValue::Equals(
      map_7, map_8,
      ValueEqualityCheckOptions{.float_margin = kExactFloatMargin,
                                .reason = &why}));
  EXPECT_EQ(why,
            "The value for key {NULL} did not match. Value was {0.5} and "
            "{0.50000006} in respective maps {{NULL: 0.5}} and {{NULL: "
            "0.50000006}}.\n");

  EXPECT_TRUE(InternalValue::Equals(
      map_1, map_2,
      ValueEqualityCheckOptions{.float_margin = kDefaultFloatMargin,
                                .reason = &why}));
  EXPECT_THAT(why, Eq("")) << "Reason should not be set when values are equal.";
}

TEST_F(ValueTest, MapValueEqualityWithArrayBagValues) {
  auto map_1 = Map({
      {"k1", Array({"a", "b"})},
      {"k2", Array({"c", "d"})},
  });

  // map_2 is map_1 but with "k1"=>["b", "a"](bag)
  auto map_2 = Map({
      {"k1", Array({"b", "a"}, InternalValue::kIgnoresOrder)},
      {"k2", Array({"c", "d"})},
  });

  // map_3 is map_1 but with "k2"=>["d", "c"](ord)
  auto map_3 = Map({
      {"k1", Array({"a", "b"}, InternalValue::kIgnoresOrder)},
      {"k2", Array({"d", "c"})},
  });

  EXPECT_TRUE(InternalValue::Equals(map_1, map_1));
  EXPECT_TRUE(InternalValue::Equals(map_2, map_2));

  EXPECT_TRUE(InternalValue::Equals(map_1, map_2));
  EXPECT_TRUE(InternalValue::Equals(map_2, map_1));

  std::string why = "";
  // map_1 and map_3 are considered equal, since the map value arrays will all
  // be considered as bags. (See also:
  // ValueTest::AsymmetricNestedArrayBagEqualityConsidersOrderednessBySlot)
  EXPECT_TRUE(InternalValue::Equals(map_1, map_3,
                                    ValueEqualityCheckOptions{.reason = &why}))
      << why;
  EXPECT_TRUE(InternalValue::Equals(map_3, map_1,
                                    ValueEqualityCheckOptions{.reason = &why}))
      << why;
  EXPECT_THAT(why, Eq("")) << "Reason should not be set when values are equal.";
}

TEST_F(ValueTest, MapValueEqualityWithOrderedArrayValues) {
  auto map_1 = Map({
      {"k1", Array({"a", "b"})},
      {"k2", Array({"c", "d"})},
  });

  // map_2 is map_1 but with "k1"=>["b", "a"]
  auto map_2 = Map({
      {"k1", Array({"b", "a"})},
      {"k2", Array({"c", "d"})},
  });

  // map_3 is map_1 but with "k2"=>["d", "c"](ord)
  auto map_3 = Map({
      {"k1", Array({"a", "b"})},
      {"k2", Array({"d", "c"})},
  });

  std::string why = "";
  EXPECT_TRUE(InternalValue::Equals(map_1, map_1,
                                    ValueEqualityCheckOptions{.reason = &why}))
      << why;
  EXPECT_TRUE(InternalValue::Equals(map_2, map_2,
                                    ValueEqualityCheckOptions{.reason = &why}))
      << why;
  EXPECT_THAT(why, Eq("")) << "Reason should not be set when values are equal.";

  EXPECT_FALSE(InternalValue::Equals(
      map_1, map_2, ValueEqualityCheckOptions{.reason = &why}));
  EXPECT_THAT(why, HasSubstr("The value for key {\"k1\"} did not match."));
  EXPECT_FALSE(InternalValue::Equals(
      map_2, map_1, ValueEqualityCheckOptions{.reason = &why}));
  EXPECT_THAT(why, HasSubstr("The value for key {\"k1\"} did not match."));

  EXPECT_FALSE(InternalValue::Equals(
      map_1, map_3, ValueEqualityCheckOptions{.reason = &why}));
  EXPECT_THAT(why, HasSubstr("The value for key {\"k2\"} did not match."));
  EXPECT_FALSE(InternalValue::Equals(
      map_3, map_1, ValueEqualityCheckOptions{.reason = &why}));
  EXPECT_THAT(why, HasSubstr("The value for key {\"k2\"} did not match."));
}

class MapValueEqualityTest
    : public ::testing::TestWithParam<std::tuple<Value, Value>> {};

TEST_P(MapValueEqualityTest, MapValueEqualityWithArrayBagKeysAndValues2) {
  auto [map_a, map_b] = GetParam();

  std::string why = "";
  EXPECT_TRUE(InternalValue::Equals(map_a, map_b,
                                    ValueEqualityCheckOptions{.reason = &why}))
      << why;
  EXPECT_TRUE(InternalValue::Equals(map_b, map_a,
                                    ValueEqualityCheckOptions{.reason = &why}))
      << why;
  EXPECT_THAT(why, Eq("")) << "Reason should not be set when values are equal.";
}

// All MAP values in this dataset are equivalent.
std::vector<Value> MapValueEqualityWithArrayBagKeysAndValuesTestDataEqual() {
  return std::vector<Value>{
      Map({
          {Array({"k1", "k99"}, InternalValue::kIgnoresOrder),
           Array({"a", "b"}, InternalValue::kIgnoresOrder)},
          {Array({"k2", "k2b"}), Array({"c", "d"})},
      }),
      Map({
          {Array({"k99", "k1"}, InternalValue::kIgnoresOrder),
           Array({"a", "b"}, InternalValue::kIgnoresOrder)},
          {Array({"k2", "k2b"}), Array({"d", "c"})},
      }),
      Map({
          {Array({"k99", "k1"}, InternalValue::kIgnoresOrder),
           Array({"b", "a"}, InternalValue::kIgnoresOrder)},
          {Array({"k2", "k2b"}), Array({"d", "c"})},
      }),
  };
}

INSTANTIATE_TEST_SUITE_P(
    MapValueEqualityTest, MapValueEqualityTest,
    ::testing::Combine(
        ::testing::ValuesIn(
            MapValueEqualityWithArrayBagKeysAndValuesTestDataEqual()),
        ::testing::ValuesIn(
            MapValueEqualityWithArrayBagKeysAndValuesTestDataEqual())));

TEST_F(ValueTest, MapValueInequalityWithArrayBagKeysAndValues) {
  auto arrkey_map_1 = Map({
      {Array({"k1", "k99"}, InternalValue::kIgnoresOrder),
       Array({"a", "b"}, InternalValue::kIgnoresOrder)},
      {Array({"k2", "k2b"}), Array({"c", "d"})},
  });

  auto arrkey_map_2 = Map({
      {Array({"k99", "k1"}, InternalValue::kIgnoresOrder),
       Array({"a", "b"}, InternalValue::kIgnoresOrder)},
      {Array({"k2", "k2b"}), Array({"d", "c"})},
  });

  auto arrkey_map_3 = Map({
      {Array({"k99", "k1"}, InternalValue::kIgnoresOrder),
       Array({"b", "a"}, InternalValue::kIgnoresOrder)},
      {Array({"k2", "k2b"}), Array({"d", "c"})},
  });
  auto arrkey_map_4 = Map({
      {Array({"k99", "k1"}, InternalValue::kIgnoresOrder),
       Array({"b", "x"}, InternalValue::kIgnoresOrder)},
      {Array({"k2", "k2b"}), Array({"d", "c"})},
  });
  auto arrkey_map_5 = Map({
      {Array({"k99", "k1"}, InternalValue::kIgnoresOrder),
       Array({"b", "a"}, InternalValue::kIgnoresOrder)},
      {Array({"k2", "k2b"}), Array({"c", "x"})},
  });

  // Equality of maps 1-3 is checked in the parameterized test above.

  std::string why = "";
  EXPECT_FALSE(InternalValue::Equals(
      arrkey_map_1, arrkey_map_4, ValueEqualityCheckOptions{.reason = &why}));
  EXPECT_THAT(why,
              HasSubstr("The value for key {[\"k1\", \"k99\"]} did not match"));
  EXPECT_FALSE(InternalValue::Equals(
      arrkey_map_4, arrkey_map_1, ValueEqualityCheckOptions{.reason = &why}));
  EXPECT_THAT(why,
              HasSubstr("The value for key {[\"k99\", \"k1\"]} did not match"));

  EXPECT_FALSE(InternalValue::Equals(
      arrkey_map_4, arrkey_map_2, ValueEqualityCheckOptions{.reason = &why}));
  EXPECT_FALSE(InternalValue::Equals(
      arrkey_map_2, arrkey_map_4, ValueEqualityCheckOptions{.reason = &why}));

  EXPECT_FALSE(InternalValue::Equals(
      arrkey_map_4, arrkey_map_3, ValueEqualityCheckOptions{.reason = &why}));
  EXPECT_FALSE(InternalValue::Equals(
      arrkey_map_3, arrkey_map_4, ValueEqualityCheckOptions{.reason = &why}));

  EXPECT_TRUE(InternalValue::Equals(arrkey_map_4, arrkey_map_4,
                                    ValueEqualityCheckOptions{.reason = &why}))
      << why;

  EXPECT_FALSE(InternalValue::Equals(
      arrkey_map_1, arrkey_map_5, ValueEqualityCheckOptions{.reason = &why}));
  EXPECT_THAT(why, HasSubstr("Value was {[\"c\", \"d\"]} and {[\"c\", \"x\"]} "
                             "in respective maps"));
  EXPECT_FALSE(InternalValue::Equals(
      arrkey_map_5, arrkey_map_1, ValueEqualityCheckOptions{.reason = &why}));
  EXPECT_THAT(why, HasSubstr("Value was {[\"c\", \"x\"]} and {[\"c\", \"d\"]} "
                             "in respective maps"));
  EXPECT_TRUE(InternalValue::Equals(arrkey_map_5, arrkey_map_5,
                                    ValueEqualityCheckOptions{.reason = &why}))
      << why;
}

TEST_F(ValueTest, MapValueEqualityWithOrderedArrayKeysAndValues) {
  auto arrkey_map_1 = Map({
      {Array({"k1", "k99"}), Array({"a", "b"})},
      {Array({"k2", "k2b"}), Array({"c", "d"})},
  });

  auto arrkey_map_2 = Map({
      {Array({"k99", "k1"}), Array({"a", "b"})},
      {Array({"k2", "k2b"}), Array({"d", "c"})},
  });

  auto arrkey_map_3 = Map({
      {Array({"k99", "k1"}), Array({"b", "a"})},
      {Array({"k2", "k2b"}), Array({"d", "c"})},
  });
  auto arrkey_map_4 = Map({
      {Array({"k99", "k1"}), Array({"b", "x"})},
      {Array({"k2", "k2b"}), Array({"d", "c"})},
  });
  auto arrkey_map_5 = Map({
      {Array({"k99", "k1"}), Array({"b", "a"})},
      {Array({"k2", "k2b"}), Array({"c", "x"})},
  });

  std::string why = "";

  EXPECT_FALSE(InternalValue::Equals(
      arrkey_map_1, arrkey_map_2, ValueEqualityCheckOptions{.reason = &why}));
  EXPECT_THAT(why,
              HasSubstr("Key {[\"k1\", \"k99\"]} did not exist in both maps"));
  EXPECT_FALSE(InternalValue::Equals(
      arrkey_map_2, arrkey_map_1, ValueEqualityCheckOptions{.reason = &why}));
  EXPECT_THAT(
      why, HasSubstr(
               "The value for key {[\"k2\", \"k2b\"]} did not match. Value was "
               "{[\"d\", \"c\"]} and {[\"c\", \"d\"]} in respective maps"));

  EXPECT_FALSE(InternalValue::Equals(
      arrkey_map_1, arrkey_map_3, ValueEqualityCheckOptions{.reason = &why}));
  EXPECT_THAT(why,
              HasSubstr("Key {[\"k1\", \"k99\"]} did not exist in both maps"));
  EXPECT_FALSE(InternalValue::Equals(
      arrkey_map_3, arrkey_map_1, ValueEqualityCheckOptions{.reason = &why}));
  EXPECT_THAT(why,
              HasSubstr("The value for key {[\"k2\", \"k2b\"]} did not match"));

  EXPECT_FALSE(InternalValue::Equals(
      arrkey_map_2, arrkey_map_3, ValueEqualityCheckOptions{.reason = &why}));
  EXPECT_THAT(why,
              HasSubstr("The value for key {[\"k99\", \"k1\"]} did not match"));
  EXPECT_FALSE(InternalValue::Equals(
      arrkey_map_3, arrkey_map_2, ValueEqualityCheckOptions{.reason = &why}));
  EXPECT_THAT(why,
              HasSubstr("The value for key {[\"k99\", \"k1\"]} did not match"));

  EXPECT_FALSE(InternalValue::Equals(
      arrkey_map_1, arrkey_map_4, ValueEqualityCheckOptions{.reason = &why}));
  EXPECT_FALSE(InternalValue::Equals(
      arrkey_map_4, arrkey_map_1, ValueEqualityCheckOptions{.reason = &why}));

  EXPECT_FALSE(InternalValue::Equals(
      arrkey_map_4, arrkey_map_2, ValueEqualityCheckOptions{.reason = &why}));
  EXPECT_FALSE(InternalValue::Equals(
      arrkey_map_2, arrkey_map_4, ValueEqualityCheckOptions{.reason = &why}));

  EXPECT_FALSE(InternalValue::Equals(
      arrkey_map_4, arrkey_map_3, ValueEqualityCheckOptions{.reason = &why}));
  EXPECT_FALSE(InternalValue::Equals(
      arrkey_map_3, arrkey_map_4, ValueEqualityCheckOptions{.reason = &why}));

  EXPECT_TRUE(InternalValue::Equals(arrkey_map_4, arrkey_map_4,
                                    ValueEqualityCheckOptions{.reason = &why}))
      << why;

  EXPECT_FALSE(InternalValue::Equals(
      arrkey_map_1, arrkey_map_5, ValueEqualityCheckOptions{.reason = &why}));
  EXPECT_FALSE(InternalValue::Equals(
      arrkey_map_5, arrkey_map_1, ValueEqualityCheckOptions{.reason = &why}));
  EXPECT_TRUE(InternalValue::Equals(arrkey_map_5, arrkey_map_5,
                                    ValueEqualityCheckOptions{.reason = &why}))
      << why;
}

TEST_F(ValueTest, MapValueEqualityNestedInContainer) {
  auto nested_map_1 = Map({{"k1", Map({{"n1", "a"}})}});
  auto nested_map_2 = Map({{"k1", Map({{"n1", "b"}})}});
  auto map_in_array_1 = Array({nested_map_1, nested_map_2});
  auto map_in_struct_1 = Struct({""}, {nested_map_1});
  auto map_in_struct_2 = Struct({""}, {nested_map_2});

  std::string why = "";

  EXPECT_TRUE(InternalValue::Equals(nested_map_1, nested_map_1));

  EXPECT_FALSE(InternalValue::Equals(
      nested_map_1, nested_map_2, ValueEqualityCheckOptions{.reason = &why}));
  EXPECT_THAT(why, HasSubstr("The value for key {\"k1\"} did not match"));

  EXPECT_TRUE(InternalValue::Equals(map_in_array_1, map_in_array_1));
  EXPECT_TRUE(InternalValue::Equals(map_in_struct_1, map_in_struct_1));
  EXPECT_FALSE(
      InternalValue::Equals(map_in_struct_1, map_in_struct_2,
                            ValueEqualityCheckOptions{.reason = &why}));
  EXPECT_THAT(why, HasSubstr("The value for key {\"k1\"} did not match"));
}

struct MapPrintingTestParam {
  Value value;
  std::string debug_string;
  std::string verbose_debug_string;
  std::string sql_string;
  std::string sql_literal_string;
  std::vector<ProductMode> product_modes = {PRODUCT_INTERNAL, PRODUCT_EXTERNAL};
};

class MapPrintingTest : public ::testing::TestWithParam<MapPrintingTestParam> {
};

TEST_P(MapPrintingTest, MapPrinting) {
  MapPrintingTestParam test_case = GetParam();
  Value& map = test_case.value;

  EXPECT_EQ(map.DebugString(), test_case.debug_string);
  EXPECT_EQ(map.DebugString(/*verbose=*/true), test_case.verbose_debug_string);
  EXPECT_EQ(map.Format(/*print_top_level_type=*/false), test_case.debug_string);
  EXPECT_EQ(map.Format(/*print_top_level_type=*/true),
            test_case.verbose_debug_string);

  for (ProductMode product_mode : test_case.product_modes) {
    EXPECT_EQ(map.GetSQL(product_mode), test_case.sql_string);
    EXPECT_EQ(map.GetSQLLiteral(product_mode), test_case.sql_literal_string);
  }

  TestGetSQL(map);
}

INSTANTIATE_TEST_SUITE_P(
    MapPrinting, MapPrintingTest,
    ::testing::ValuesIn<MapPrintingTestParam>({
        {
            .value = test_values::Map({std::make_pair(Value::Int64(1),
                                                      Value::Int64(2))}),
            .debug_string = "{1: 2}",
            .verbose_debug_string = "Map{Int64(1): Int64(2)}",
            .sql_string = "MAP_FROM_ARRAY(ARRAY<STRUCT<INT64, INT64>>[(1, 2)])",
            .sql_literal_string = "MAP_FROM_ARRAY([(1, 2)])",
        },
        {
            .value = test_values::Map(
                {{Value::NullFloat(), Value::Int64(2)},
                 {Value::Float(std::numeric_limits<float>::quiet_NaN()),
                  Value::NullInt64()}}),
            .debug_string = "{NULL: 2, nan: NULL}",
            .verbose_debug_string =
                "Map{Float(NULL): Int64(2), Float(nan): Int64(NULL)}",
            .sql_string =
                R"(MAP_FROM_ARRAY(ARRAY<STRUCT<FLOAT, INT64>>[(CAST(NULL AS FLOAT), 2), (CAST("nan" AS FLOAT), CAST(NULL AS INT64))]))",
            .sql_literal_string =
                R"(MAP_FROM_ARRAY([(NULL, 2), (CAST("nan" AS FLOAT), NULL)]))",
            .product_modes = {PRODUCT_INTERNAL},
        },
        {
            .value = test_values::Map(
                {{Value::NullFloat(), Value::Int64(2)},
                 {Value::Float(std::numeric_limits<float>::quiet_NaN()),
                  Value::NullInt64()}}),
            .debug_string = "{NULL: 2, nan: NULL}",
            .verbose_debug_string =
                "Map{Float(NULL): Int64(2), Float(nan): Int64(NULL)}",
            .sql_string =
                R"(MAP_FROM_ARRAY(ARRAY<STRUCT<FLOAT32, INT64>>[(CAST(NULL AS FLOAT32), 2), (CAST("nan" AS FLOAT32), CAST(NULL AS INT64))]))",
            .sql_literal_string =
                R"(MAP_FROM_ARRAY([(NULL, 2), (CAST("nan" AS FLOAT32), NULL)]))",
            .product_modes = {PRODUCT_EXTERNAL},
        },
        {
            .value = test_values::Map({
                {Value::String("foo"),
                 test_values::Array({test_values::Range(Date(1), Date(3)),
                                     test_values::Range(Value::NullDate(),
                                                        Date(3))})},
            }),
            .debug_string =
                R"({"foo": [[1970-01-02, 1970-01-04), [NULL, 1970-01-04)]})",
            .verbose_debug_string =
                R"(Map{String("foo"): Array[Range[Date(1970-01-02), Date(1970-01-04)), Range[Date(NULL), Date(1970-01-04))]})",
            .sql_string =
                R"sql(MAP_FROM_ARRAY(ARRAY<STRUCT<STRING, ARRAY<RANGE<DATE>>>>[("foo", ARRAY<RANGE<DATE>>[RANGE<DATE> "[1970-01-02, 1970-01-04)", RANGE<DATE> "[UNBOUNDED, 1970-01-04)"])]))sql",
            .sql_literal_string =
                R"sql(MAP_FROM_ARRAY([("foo", [RANGE<DATE> "[1970-01-02, 1970-01-04)", RANGE<DATE> "[UNBOUNDED, 1970-01-04)"])]))sql",
        },
    }));

TEST(MapPrintingTest, NullAndEmptyMaps) {
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_MAP_TYPE);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* map_type,
                            type_factory()->MakeMapType(
                                type_factory()->get_string(),
                                type_factory()->get_int64(), language_options));
  Value null_map = Value::Null(map_type);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value empty_map, Value::MakeMap(map_type, {}));

  EXPECT_EQ(null_map.DebugString(), "NULL");
  EXPECT_EQ(null_map.DebugString(/*verbose=*/true), "Map<String, Int64>(NULL)");
  EXPECT_EQ(null_map.Format(/*print_top_level_type=*/false), "NULL");
  EXPECT_EQ(null_map.Format(/*print_top_level_type=*/true),
            "Map<String, Int64>(NULL)");
  EXPECT_EQ(null_map.GetSQL(PRODUCT_INTERNAL),
            "CAST(NULL AS MAP<STRING, INT64>)");
  EXPECT_EQ(null_map.GetSQL(PRODUCT_EXTERNAL),
            "CAST(NULL AS MAP<STRING, INT64>)");
  EXPECT_EQ(null_map.GetSQLLiteral(PRODUCT_INTERNAL), "NULL");
  EXPECT_EQ(null_map.GetSQLLiteral(PRODUCT_EXTERNAL), "NULL");

  EXPECT_EQ(empty_map.DebugString(), "{}");
  EXPECT_EQ(empty_map.DebugString(/*verbose=*/true), "Map{}");
  EXPECT_EQ(empty_map.Format(/*print_top_level_type=*/false), "{}");
  EXPECT_EQ(empty_map.Format(/*print_top_level_type=*/true), "Map{}");
  EXPECT_EQ(empty_map.GetSQL(PRODUCT_INTERNAL),
            "MAP_FROM_ARRAY(ARRAY<STRUCT<STRING, INT64>>[])");
  EXPECT_EQ(empty_map.GetSQL(PRODUCT_EXTERNAL),
            "MAP_FROM_ARRAY(ARRAY<STRUCT<STRING, INT64>>[])");
  EXPECT_EQ(empty_map.GetSQLLiteral(PRODUCT_INTERNAL),
            "MAP_FROM_ARRAY(ARRAY<STRUCT<STRING, INT64>>[])");
  EXPECT_EQ(empty_map.GetSQLLiteral(PRODUCT_EXTERNAL),
            "MAP_FROM_ARRAY(ARRAY<STRUCT<STRING, INT64>>[])");
}

TEST(MapPrintingTest, InternalExternalProductModeDiffers) {
  Value map = test_values::Map({
      {Value::Float(1.1), Value::NullDouble()},
      {Value::Float(2.2),
       Value::Double(std::numeric_limits<double>::infinity())},
  });
  EXPECT_EQ(
      map.GetSQL(PRODUCT_INTERNAL),
      R"(MAP_FROM_ARRAY(ARRAY<STRUCT<FLOAT, DOUBLE>>[(CAST(1.1 AS FLOAT), CAST(NULL AS DOUBLE)), (CAST(2.2 AS FLOAT), CAST("inf" AS DOUBLE))]))");
  EXPECT_EQ(
      map.GetSQL(PRODUCT_INTERNAL, /*use_external_float32=*/true),
      R"(MAP_FROM_ARRAY(ARRAY<STRUCT<FLOAT, DOUBLE>>[(CAST(1.1 AS FLOAT), CAST(NULL AS DOUBLE)), (CAST(2.2 AS FLOAT), CAST("inf" AS DOUBLE))]))");
  EXPECT_EQ(
      map.GetSQL(PRODUCT_EXTERNAL),
      R"(MAP_FROM_ARRAY(ARRAY<STRUCT<FLOAT32, FLOAT64>>[(CAST(1.1 AS FLOAT32), CAST(NULL AS FLOAT64)), (CAST(2.2 AS FLOAT32), CAST("inf" AS FLOAT64))]))");
  EXPECT_EQ(
      map.GetSQL(PRODUCT_EXTERNAL, /*use_external_float32=*/true),
      R"(MAP_FROM_ARRAY(ARRAY<STRUCT<FLOAT32, FLOAT64>>[(CAST(1.1 AS FLOAT32), CAST(NULL AS FLOAT64)), (CAST(2.2 AS FLOAT32), CAST("inf" AS FLOAT64))]))");
  EXPECT_EQ(map.GetSQLLiteral(PRODUCT_INTERNAL),
            R"(MAP_FROM_ARRAY([(1.1, NULL), (2.2, CAST("inf" AS DOUBLE))]))");
  EXPECT_EQ(map.GetSQLLiteral(PRODUCT_INTERNAL, /*use_external_float32=*/true),
            R"(MAP_FROM_ARRAY([(1.1, NULL), (2.2, CAST("inf" AS DOUBLE))]))");
  EXPECT_EQ(map.GetSQLLiteral(PRODUCT_EXTERNAL),
            R"(MAP_FROM_ARRAY([(1.1, NULL), (2.2, CAST("inf" AS FLOAT64))]))");
  EXPECT_EQ(map.GetSQLLiteral(PRODUCT_EXTERNAL, /*use_external_float32=*/true),
            R"(MAP_FROM_ARRAY([(1.1, NULL), (2.2, CAST("inf" AS FLOAT64))]))");
}

TEST_F(ValueTest, InternalEqualsOnDifferentSizedStructs) {
  auto struct_1 = Struct({""}, {Value::Int64(1)});
  auto struct_2 = Struct({"", ""}, {Value::Int64(1), Value::Int64(1)});

  std::string why = "";
  EXPECT_FALSE(InternalValue::Equals(
      struct_1, struct_2,
      ValueEqualityCheckOptions{.float_margin = FloatMargin::UlpMargin(0),
                                .reason = &why}));
}

TEST_F(ValueTest, Invalid) {
  EXPECT_EQ(false, Value().is_valid());
  EXPECT_DEATH(Value().is_null(), "");
  EXPECT_DEATH(Value().type(), "");
}

TEST_F(ValueTest, NaN) {
  const Value float_nan =
      TestGetSQL(Value::Float(std::numeric_limits<float>::quiet_NaN()));
  const Value double_nan =
      TestGetSQL(Value::Double(std::numeric_limits<double>::quiet_NaN()));

  EXPECT_TRUE(std::isnan(float_nan.float_value()));
  EXPECT_TRUE(std::isnan(double_nan.double_value()));
  EXPECT_EQ("nan", float_nan.DebugString());
  EXPECT_EQ("nan", double_nan.DebugString());
  EXPECT_EQ("Float(nan)", float_nan.FullDebugString());
  EXPECT_EQ("Double(nan)", double_nan.FullDebugString());
  EXPECT_EQ("CAST(\"nan\" AS FLOAT32)", float_nan.GetSQL());
  EXPECT_EQ("CAST(\"nan\" AS FLOAT32)", float_nan.GetSQL(PRODUCT_EXTERNAL));
  EXPECT_EQ("CAST(\"nan\" AS FLOAT32)",
            float_nan.GetSQL(PRODUCT_EXTERNAL, /*use_external_float32=*/true));
  EXPECT_EQ("CAST(\"nan\" AS FLOAT64)", double_nan.GetSQL(PRODUCT_EXTERNAL));
  EXPECT_EQ("CAST(\"nan\" AS DOUBLE)", double_nan.GetSQL(PRODUCT_INTERNAL));
}

TEST_F(ValueTest, Enum) {
  const EnumType* enum_type = GetTestEnumType();
  const EnumType* proto3_enum_type = GetTestProto3EnumType();
  EXPECT_TRUE(Value::Enum(enum_type, 0).type()->Equals(enum_type));
  EXPECT_TRUE(TestGetSQL(Value::Null(enum_type))
                  .type()
                  ->Equals(Value::Enum(enum_type, 0).type()));
  EXPECT_EQ(0, TestGetSQL(Value::Enum(enum_type, 0)).enum_value());
  EXPECT_EQ(1, TestGetSQL(Value::Enum(enum_type, 1)).enum_value());
  EXPECT_EQ(2, TestGetSQL(Value::Enum(enum_type, 2)).enum_value());
  EXPECT_EQ("TESTENUM2", Enum(enum_type, 2).EnumDisplayName());
  EXPECT_EQ("ENUM1", Enum(proto3_enum_type, 1).EnumDisplayName());
  EXPECT_FALSE(Value::Enum(enum_type, 12345).is_valid());
  EXPECT_DEATH(Value::Uint32(12345).enum_value(), "Not an enum value");
  EXPECT_DEATH(Value::Uint32(12345).EnumDisplayName(), "Not an enum value");
  EXPECT_DEATH(Value::Enum(enum_type, 12345).enum_value(), "Not an enum value");
  EXPECT_TRUE(Value::Enum(proto3_enum_type, 2544).is_valid());
  EXPECT_TRUE(Value::Enum(proto3_enum_type, 2544).is_valid());
  // proto2 enums cannot contain unnamed values, even if we ask for them.
  EXPECT_FALSE(Value::Enum(enum_type, 2550).is_valid());
  EXPECT_EQ(2147483647, Value::Enum(enum_type, 2147483647).enum_value());
  EXPECT_EQ(2147483647, Value::Enum(enum_type, 2147483647).ToInt64());
  EXPECT_EQ(2147483647.0, Value::Enum(enum_type, 2147483647).ToDouble());

  // Verifies that we correctly cast the int64 to int32 (when the value is in
  // int32 range without any truncation) before converting it into an enum
  // value.
  EXPECT_EQ(0x2, Value::Enum(enum_type, 0x000000002).enum_value());
  EXPECT_FALSE(Value::Enum(enum_type, 0x100000002).is_valid());

  EXPECT_TRUE(Value::Null(enum_type).is_null());
  EXPECT_FALSE(Value::Enum(enum_type, 0).is_null());
  EXPECT_EQ(Value::Enum(enum_type, 0), Value::Enum(enum_type, 0));
  EXPECT_TRUE(Value::Enum(enum_type, 0).LessThan(Value::Enum(enum_type, 1)));
  EXPECT_FALSE(Value::Enum(enum_type, 1).LessThan(Value::Enum(enum_type, 0)));
  EXPECT_EQ(Enum(enum_type, 1), Enum(enum_type, "TESTENUM1"));
  // Enum names are case sensitive.
  EXPECT_FALSE(Enum(enum_type, "TeStEnUm1").is_valid());
  Value v(Value::Enum(enum_type, 1));
  EXPECT_EQ(v, Value::Enum(enum_type, 1));
  EXPECT_EQ("TESTENUM1", Value::Enum(enum_type, 1).ShortDebugString());
  EXPECT_EQ("Enum<googlesql_test.TestEnum>(TESTENUM1:1)",
            Value::Enum(enum_type, 1).FullDebugString());
  EXPECT_EQ("NULL", Value::Null(enum_type).ShortDebugString());
  EXPECT_EQ("Enum<googlesql_test.TestEnum>(NULL)",
            Value::Null(enum_type).FullDebugString());
  // Null enums.
  const EnumType* other_enum_type = GetOtherTestEnumType();
  Value enum_null_1 = Value::Null(enum_type);
  Value enum_null_2 = Value::Null(other_enum_type);
  EXPECT_TRUE(enum_null_1.is_null());
  EXPECT_TRUE(enum_null_2.is_null());
  EXPECT_TRUE(enum_null_1.Equals(enum_null_1));
  EXPECT_TRUE(enum_null_2.Equals(enum_null_2));
  EXPECT_FALSE(enum_null_1.Equals(enum_null_2));
}

TEST_F(ValueTest, EnumName) {
  const EnumType* enum_type = GetTestProto3EnumType();
  EXPECT_THAT(Value::Null(enum_type).EnumName(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Null enum value")));
  EXPECT_THAT(Value::Uint32(1).EnumName(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Not an enum value")));
  EXPECT_THAT(Value::Enum(enum_type, 1).EnumName(),
              ::absl_testing::IsOkAndHolds("ENUM1"));
  Value unnamed_enum = Value::Enum(enum_type, 2593);
  EXPECT_THAT(unnamed_enum.EnumName(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Value 2593 not in")));
  EXPECT_THAT(unnamed_enum.EnumDisplayName(), "2593");
}

TEST_F(ValueTest, EnumFormatting) {
  const EnumType* enum_type = GetTestEnumType();
  const Value enum_value = Value::Enum(enum_type, 0);
  EXPECT_EQ(enum_value.DebugString(/*verbose=*/true),
            "Enum<googlesql_test.TestEnum>(TESTENUM0:0)");
  EXPECT_EQ(enum_value.DebugString(), "TESTENUM0");
  EXPECT_EQ(enum_value.Format(), "Enum<googlesql_test.TestEnum>(TESTENUM0:0)");
  EXPECT_EQ(enum_value.Format(/*print_top_level_type=*/false), "TESTENUM0");
  EXPECT_EQ(enum_value.GetSQLLiteral(), R"sql("TESTENUM0")sql");
  EXPECT_EQ(enum_value.GetSQL(),
            R"sql(CAST("TESTENUM0" AS `googlesql_test.TestEnum`))sql");
}

TEST_F(ValueTest, EnumFormattingUnknownEnum) {
  const EnumType* enum_type = GetTestProto3EnumType();
  const Value enum_value = Value::Enum(enum_type, 2586);
  EXPECT_EQ(enum_value.DebugString(/*verbose=*/true),
            "Enum<googlesql_test.TestProto3Enum>(2586)");
  EXPECT_EQ(enum_value.DebugString(), "2586");
  EXPECT_EQ(enum_value.Format(), "Enum<googlesql_test.TestProto3Enum>(2586)");
  EXPECT_EQ(enum_value.Format(/*print_top_level_type=*/false), "2586");
  EXPECT_EQ(enum_value.GetSQLLiteral(), R"sql(2586)sql");
  EXPECT_EQ(enum_value.GetSQL(),
            R"sql(CAST(2586 AS `googlesql_test.TestProto3Enum`))sql");
}

TEST_F(ValueTest, StructWithNanAndInf) {
  // Null < NaN < -Inf < 0 < +Inf
  const Value struct_a_null = Struct({"a"}, {Value::NullFloat()});
  const Value struct_a_nan =
      Struct({"a"}, {std::numeric_limits<float>::quiet_NaN()});
  const Value struct_a_neginf =
      Struct({"a"}, {-std::numeric_limits<float>::infinity()});
  const Value struct_a_zero = Struct({"a"}, {Value::Float(0)});
  const Value struct_a_posinf =
      Struct({"a"}, {std::numeric_limits<float>::infinity()});
  const Value struct_b_zero = Struct({"b"}, {Value::Float(0)});

  EXPECT_TRUE(struct_a_null.LessThan(struct_a_nan));
  EXPECT_TRUE(struct_a_null.LessThan(struct_a_neginf));
  EXPECT_TRUE(struct_a_null.LessThan(struct_a_zero));
  EXPECT_TRUE(struct_a_null.LessThan(struct_a_posinf));
  EXPECT_FALSE(struct_a_nan.LessThan(struct_a_null));
  EXPECT_FALSE(struct_a_neginf.LessThan(struct_a_null));
  EXPECT_FALSE(struct_a_zero.LessThan(struct_a_null));
  EXPECT_FALSE(struct_a_posinf.LessThan(struct_a_null));

  EXPECT_TRUE(struct_a_nan.LessThan(struct_a_neginf));
  EXPECT_TRUE(struct_a_nan.LessThan(struct_a_zero));
  EXPECT_TRUE(struct_a_nan.LessThan(struct_a_posinf));
  EXPECT_FALSE(struct_a_neginf.LessThan(struct_a_nan));
  EXPECT_FALSE(struct_a_zero.LessThan(struct_a_nan));
  EXPECT_FALSE(struct_a_posinf.LessThan(struct_a_nan));

  EXPECT_TRUE(struct_a_neginf.LessThan(struct_a_zero));
  EXPECT_TRUE(struct_a_neginf.LessThan(struct_a_posinf));
  EXPECT_FALSE(struct_a_zero.LessThan(struct_a_neginf));
  EXPECT_FALSE(struct_a_posinf.LessThan(struct_a_neginf));

  EXPECT_TRUE(struct_a_zero.LessThan(struct_a_posinf));
  EXPECT_FALSE(struct_a_posinf.LessThan(struct_a_zero));

  const Value struct_b_posinf =
      Struct({"b"}, {Value::Float(std::numeric_limits<float>::infinity())});

  EXPECT_TRUE(struct_b_zero.LessThan(struct_a_posinf));

  // Nested struct with Nan and Infinity
  const Value struct_anan_bnan = Struct(
      {"a", "b"}, {Value::Float(std::numeric_limits<float>::quiet_NaN()),
                   Value::Float(std::numeric_limits<float>::quiet_NaN())});
  const Value struct_anull_bnan = Struct(
      {"a", "b"}, {Value::NullFloat(),
                   Value::Float(std::numeric_limits<float>::quiet_NaN())});
  const Value struct_anan_bnull =
      Struct({"a", "b"}, {Value::Float(std::numeric_limits<float>::quiet_NaN()),
                          Value::NullFloat()});
  EXPECT_TRUE(struct_anull_bnan.LessThan(struct_anan_bnan));
  EXPECT_TRUE(struct_anull_bnan.LessThan(struct_anan_bnull));
  EXPECT_FALSE(struct_anan_bnan.LessThan(struct_anull_bnan));
  EXPECT_FALSE(struct_anan_bnull.LessThan(struct_anull_bnan));

  EXPECT_TRUE(struct_anan_bnull.LessThan(struct_anan_bnan));
  EXPECT_FALSE(struct_anan_bnan.LessThan(struct_anan_bnull));

  // Compare with self, expecting all false.
  EXPECT_FALSE(struct_a_null.LessThan(struct_a_null));
  EXPECT_FALSE(struct_a_nan.LessThan(struct_a_nan));
  EXPECT_FALSE(struct_a_neginf.LessThan(struct_a_neginf));
  EXPECT_FALSE(struct_a_zero.LessThan(struct_a_zero));
  EXPECT_FALSE(struct_a_posinf.LessThan(struct_a_posinf));
  EXPECT_FALSE(struct_anan_bnan.LessThan(struct_anan_bnan));
  EXPECT_FALSE(struct_anull_bnan.LessThan(struct_anull_bnan));
  EXPECT_FALSE(struct_anan_bnull.LessThan(struct_anan_bnull));
}

TEST_F(ValueTest, StructLessThanSimple) {
  const Value struct_a_1 = Struct({"a"}, {1});
  const Value struct_a_2 = Struct({"a"}, {2});
  const Value struct_b_1 = Struct({"b"}, {1});
  const Value struct_ab_11 = Struct({"a", "b"}, {1, 1});
  const Value struct_ab_12 = Struct({"a", "b"}, {1, 2});
  const Value struct_ab_14 = Struct({"a", "b"}, {1, 4});
  const Value struct_ab_23 = Struct({"a", "b"}, {2, 3});
  const Value struct_ab_34 = Struct({"a", "b"}, {3, 4});
  const Value struct_cd_34 = Struct({"c", "d"}, {3, 4});

  // Compare struct with only one field.
  EXPECT_TRUE(struct_a_1.LessThan(struct_a_2));
  EXPECT_FALSE(struct_a_2.LessThan(struct_a_1));
  EXPECT_TRUE(struct_b_1.LessThan(struct_a_2));
  EXPECT_FALSE(struct_a_2.LessThan(struct_b_1));
  EXPECT_FALSE(struct_a_1.LessThan(struct_b_1));
  EXPECT_FALSE(struct_b_1.LessThan(struct_a_1));

  // Compare struct with two fields
  EXPECT_TRUE(struct_ab_12.LessThan(struct_ab_34));
  EXPECT_FALSE(struct_ab_34.LessThan(struct_ab_12));
  EXPECT_TRUE(struct_ab_12.LessThan(struct_cd_34));
  EXPECT_FALSE(struct_cd_34.LessThan(struct_ab_12));
  EXPECT_FALSE(struct_ab_34.LessThan(struct_cd_34));
  EXPECT_FALSE(struct_cd_34.LessThan(struct_ab_34));
  EXPECT_TRUE(struct_ab_14.LessThan(struct_ab_23));
  EXPECT_FALSE(struct_ab_23.LessThan(struct_ab_14));
  EXPECT_TRUE(struct_ab_11.LessThan(struct_ab_12));
  EXPECT_FALSE(struct_ab_12.LessThan(struct_ab_11));

  // Compare two structs with different number of fields, expect false.
  EXPECT_FALSE(struct_a_1.LessThan(struct_ab_34));
  EXPECT_FALSE(struct_ab_34.LessThan(struct_a_1));
}

TEST_F(ValueTest, StructLessThanNullSimple) {
  const Value struct_a_null = Struct({"a"}, {Value::NullInt32()});
  const Value struct_a_1 = Struct({"a"}, {1});

  const Value struct_ab_1null = Struct({"a", "b"}, {1, Value::NullInt32()});
  const Value struct_ab_12 = Struct({"a", "b"}, {1, 2});
  const Value struct_ab_null1 = Struct({"a", "b"}, {Value::NullInt32(), 1});
  const Value struct_ab_null2 = Struct({"a", "b"}, {Value::NullInt32(), 2});

  const Value null_struct_1 =
      Value::Null(MakeStructType({{"a", Int32Type()}, {"b", Int64Type()}}));
  const Value null_struct_2 =
      Value::Null(MakeStructType({{"c", Int32Type()}, {"d", Int64Type()}}));

  // Compare struct with one field.
  EXPECT_TRUE(struct_a_null.LessThan(struct_a_1));
  EXPECT_FALSE(struct_a_1.LessThan(struct_a_null));
  EXPECT_TRUE(struct_ab_null1.LessThan(struct_ab_null2));
  EXPECT_FALSE(struct_ab_null2.LessThan(struct_ab_null1));

  // Compare two nulls.
  EXPECT_FALSE(null_struct_1.LessThan(null_struct_2));
  EXPECT_FALSE(null_struct_2.LessThan(null_struct_1));

  // Compare structs with two fields.
  EXPECT_TRUE(struct_ab_1null.LessThan(struct_ab_12));
  EXPECT_FALSE(struct_ab_12.LessThan(struct_ab_1null));
  EXPECT_TRUE(struct_ab_null1.LessThan(struct_ab_1null));
  EXPECT_FALSE(struct_ab_1null.LessThan(struct_ab_null1));
  EXPECT_TRUE(struct_ab_null1.LessThan(struct_ab_12));
  EXPECT_FALSE(struct_ab_12.LessThan(struct_ab_null1));

  // Compare two structs with different number of fields. Expect all false.
  EXPECT_FALSE(struct_a_null.LessThan(struct_ab_1null));
  EXPECT_FALSE(struct_ab_1null.LessThan(struct_a_null));
  EXPECT_FALSE(struct_a_null.LessThan(struct_ab_null1));
  EXPECT_FALSE(struct_ab_null1.LessThan(struct_a_null));
}

TEST_F(ValueTest, StructLessThanNested) {
  const Value nested_f4 = Struct(
      {"a", "b"}, {1, Struct({"c", "d"}, {2, Struct({"e", "f"}, {3, 4})})});
  const Value nested_f5 = Struct(
      {"a", "b"}, {1, Struct({"c", "d"}, {2, Struct({"e", "f"}, {3, 5})})});
  EXPECT_TRUE(nested_f4.LessThan(nested_f5));
  EXPECT_FALSE(nested_f5.LessThan(nested_f4));

  // Compare nested struct containing nulls.
  const Value nested_anull =
      Struct({"a", "b"}, {Value::NullInt32(),
                          Struct({"c", "d"}, {2, Struct({"e", "f"}, {3, 5})})});
  const Value nested_bnull = Struct(
      {"a", "b"}, {1, Value::Null(MakeStructType(
                          {{"c", Int32Type()},
                           {"d", MakeStructType({{"e", Int32Type()},
                                                 {"f", Int32Type()}})}}))});
  const Value nested_cnull = Struct(
      {"a", "b"},
      {1, Struct({"c", "d"}, {Value::NullInt32(),
                              Struct({"e", "f"}, {3, Value::NullInt32()})})});
  const Value nested_fnull = Struct(
      {"a", "b"},
      {1,
       Struct({"c", "d"}, {2, Struct({"e", "f"}, {3, Value::NullInt32()})})});

  EXPECT_TRUE(nested_anull.LessThan(nested_bnull));
  EXPECT_TRUE(nested_anull.LessThan(nested_cnull));
  EXPECT_TRUE(nested_anull.LessThan(nested_fnull));
  EXPECT_TRUE(nested_bnull.LessThan(nested_cnull));
  EXPECT_TRUE(nested_bnull.LessThan(nested_fnull));
  EXPECT_TRUE(nested_cnull.LessThan(nested_fnull));

  EXPECT_FALSE(nested_bnull.LessThan(nested_anull));
  EXPECT_FALSE(nested_cnull.LessThan(nested_anull));
  EXPECT_FALSE(nested_fnull.LessThan(nested_anull));
  EXPECT_FALSE(nested_cnull.LessThan(nested_bnull));
  EXPECT_FALSE(nested_fnull.LessThan(nested_bnull));
  EXPECT_FALSE(nested_fnull.LessThan(nested_cnull));
}

TEST_F(ValueTest, ArrayLessThan) {
  const std::vector<Value> values = {Value::Null(types::Int64ArrayType()),
                                     values::Int64Array({}),
                                     Array({Value::NullInt64()}),
                                     Array({Value::NullInt64(), Int64(5)}),
                                     Array({Int64(1)}),
                                     Array({Int64(1), Value::NullInt64()}),
                                     Array({Int64(1), Int64(2)}),
                                     Array({Int64(1), Int64(2),
                                            Value::NullInt64()}),
                                     Array({Int64(1), Int64(2), Int64(3)}),
                                     Array({Int64(1), Int64(3), Int64(2)})},
                           Array({Int64(1), Int64(4)});

  for (int i = 0; i < values.size(); ++i) {
    for (int j = 0; j < values.size(); ++j) {
      EXPECT_EQ(i < j, values[i].LessThan(values[j]))
          << "Value1: " << values[i].DebugString()
          << " Value2: " << values[j].DebugString();
    }
  }
}

TEST_F(ValueTest, Proto) {
  TypeFactory type_factory;

  const ProtoType* proto_type = GetTestProtoType();
  googlesql_test::KitchenSinkPB k;
  absl::Cord bytes_2968;
  ABSL_CHECK(k.SerializePartialToCord(&bytes_2968));
  absl::Cord bytes = bytes_2968;
  // Empty proto.
  EXPECT_EQ(0, bytes.size());
  EXPECT_TRUE(Value::Proto(proto_type, bytes).type()->Equals(proto_type));
  googlesql_test::KitchenSinkPB kvalid;
  kvalid.set_int64_key_1(1);
  kvalid.set_int64_key_2(2);
  EXPECT_EQ("{int64_key_1: 1 int64_key_2: 2}",
            Proto(proto_type, kvalid).ShortDebugString());
  EXPECT_TRUE(Value::Null(proto_type).type()->Equals(proto_type));
  EXPECT_TRUE(Value::Null(proto_type).is_null());
  EXPECT_EQ("Proto<googlesql_test.KitchenSinkPB>(NULL)",
            Value::Null(proto_type).FullDebugString());
  EXPECT_EQ("NULL", Value::Null(proto_type).ShortDebugString());
  TestGetSQL(Value::Null(proto_type));
  EXPECT_FALSE(Value::Proto(proto_type, bytes).is_null());
  Value proto = TestGetSQL(Proto(proto_type, bytes));
  EXPECT_EQ(bytes, proto.ToCord());
  EXPECT_EQ(bytes, proto.ToString());
  EXPECT_EQ(bytes, proto.proto_value());
  EXPECT_EQ("Proto<googlesql_test.KitchenSinkPB>{}", proto.FullDebugString());
  EXPECT_EQ("{}", proto.ShortDebugString());
  // Non-empty proto.
  k.set_int32_val(3);
  absl::Cord bytes_2996;
  ABSL_CHECK(k.SerializePartialToCord(&bytes_2996));
  bytes = bytes_2996;
  EXPECT_EQ(2, bytes.size());
  Value proto1 = TestGetSQL(Proto(proto_type, bytes));
  Value proto2 = proto1;
  TestHashEqual(proto1, proto2);
  EXPECT_EQ(proto1, proto2);
  EXPECT_EQ("Proto<googlesql_test.KitchenSinkPB>{int32_val: 3\n}",
            proto1.FullDebugString());
  EXPECT_EQ("{int32_val: 3}", proto1.ShortDebugString());
  // Duplicate int32_val tag.
  bytes.Append(bytes);
  Value proto3 = TestGetSQL(Proto(proto_type, bytes));
  // Cord representation is different, but protos compare as equal.
  EXPECT_EQ(2, proto1.ToCord().size());
  EXPECT_EQ(4, proto3.ToCord().size());
  EXPECT_EQ(2, proto1.ToString().size());
  EXPECT_EQ(4, proto3.ToString().size());
  EXPECT_NE(std::string(proto1.ToCord()), std::string(proto3.ToCord()));
  EXPECT_NE(absl::Cord(proto1.ToString()), absl::Cord(proto3.ToString()));
  EXPECT_TRUE(proto1.Equals(proto3));
  EXPECT_EQ(proto1, proto3);
  TestHashEqual(proto1, proto3);
  // Null protos.
  const ProtoType* other_proto_type = GetOtherTestProtoType();
  Value proto_null_1 = Value::Null(proto_type);
  Value proto_null_2 = Value::Null(other_proto_type);
  EXPECT_TRUE(proto_null_1.is_null());
  EXPECT_TRUE(proto_null_2.is_null());
  EXPECT_TRUE(proto_null_1.Equals(proto_null_1));
  EXPECT_TRUE(proto_null_2.Equals(proto_null_2));
  EXPECT_FALSE(proto_null_1.Equals(proto_null_2));
  // Same human readable value despite duplicated tag.
  EXPECT_EQ("{int32_val: 3}", proto3.ShortDebugString());
  // Proto with field in NaN value.
  kvalid.set_double_val(std::numeric_limits<double>::quiet_NaN());
  Value proto_with_nan_1 = Proto(proto_type, kvalid);
  kvalid.set_double_val(-std::numeric_limits<double>::quiet_NaN());
  Value proto_with_nan_2 = Proto(proto_type, kvalid);
  EXPECT_NE(std::string(proto_with_nan_1.ToCord()),
            std::string(proto_with_nan_2.ToCord()));
  EXPECT_TRUE(proto_with_nan_1.Equals(proto_with_nan_2));

  // Test with a proto with a duplicate optional field.  The last one takes
  // precedence.
  k.set_int32_val(7);
  absl::Cord bytes_3038;
  ABSL_CHECK(k.SerializePartialToCord(&bytes_3038));
  absl::Cord bytes4 = bytes_3038;
  // Now we have two duplicate 3 values followed by a 7.
  bytes.Append(bytes4);
  Value proto4 = TestGetSQL(Proto(proto_type, bytes));
  EXPECT_EQ(6, proto4.ToCord().size());
  EXPECT_EQ(6, proto4.ToString().size());
  EXPECT_FALSE(proto1.Equals(proto4));
  EXPECT_NE(proto1, proto4);
  EXPECT_EQ("{int32_val: 7}", proto4.ShortDebugString());
  Value proto5 = TestGetSQL(Proto(proto_type, bytes4));
  EXPECT_TRUE(proto4.Equals(proto5));
  EXPECT_EQ(proto4, proto5);
  TestHashEqual(proto4, proto5);
  EXPECT_GT(proto4.ToCord().size(), proto5.ToCord().size());
  EXPECT_GT(proto4.ToString().size(), proto5.ToString().size());

  // One example where we get a reason out from the proto MessageDifferencer.
  std::string reason;
  EXPECT_FALSE(InternalValue::Equals(
      proto1, proto4, ValueEqualityCheckOptions{.reason = &reason}));
  // We test that get a reason but don't compare the actual reason string.
  EXPECT_FALSE(reason.empty());
  ABSL_LOG(INFO) << "Reason: " << reason;

  // Proto with an unknown tag.
  bytes = "";  // clear bytes;
  {
    google::protobuf::io::CordOutputStream cord_stream;
    google::protobuf::io::CodedOutputStream out(&cord_stream);
    out.WriteVarint32(
        WireFormatLite::MakeTag(150775, WireFormatLite::WIRETYPE_VARINT));
    out.WriteVarint32(57);
    out.Trim();
    bytes = cord_stream.Consume();
  }
  EXPECT_EQ("{150775: 57}",
            TestGetSQL(Proto(proto_type, bytes)).ShortDebugString());

  // Invalid proto contents is accepted without validation, but renders as
  // <unparseable>.
  {
    google::protobuf::io::CordOutputStream cord_stream(std::move(bytes));
    google::protobuf::io::CodedOutputStream out(&cord_stream);
    out.WriteVarint32(
        WireFormatLite::MakeTag(150776, WireFormatLite::WIRETYPE_END_GROUP));
    out.Trim();
    bytes = cord_stream.Consume();
  }
  EXPECT_EQ("Proto<googlesql_test.KitchenSinkPB>{<unparseable>}",
            Proto(proto_type, bytes).FullDebugString());
  google::protobuf::DynamicMessageFactory message_factory;
  std::unique_ptr<google::protobuf::Message> message(
      Proto(proto_type, bytes).ToMessage(&message_factory));
  std::string text_format;
  google::protobuf::TextFormat::PrintToString(*message, &text_format);
  EXPECT_EQ("150775: 57\n", text_format);

  // Protos with the same tags in different order.
  k.Clear();
  k.set_int32_val(5);
  std::string tag1;
  ASSERT_TRUE(k.SerializePartialToString(&tag1));
  k.Clear();
  k.set_string_val("abc");
  std::string tag2;
  ASSERT_TRUE(k.SerializePartialToString(&tag2));
  absl::Cord tag_1_2_bytes = absl::Cord(absl::StrCat(tag1, tag2));
  absl::Cord tag_2_1_bytes = absl::Cord(absl::StrCat(tag2, tag1));

  Value proto_1_2 = TestGetSQL(Proto(proto_type, tag_1_2_bytes));
  Value proto_2_1 = TestGetSQL(Proto(proto_type, tag_2_1_bytes));
  EXPECT_EQ(proto_1_2, proto_2_1);
  TestHashEqual(proto_1_2, proto_2_1);
  EXPECT_EQ(
      "Proto<googlesql_test.KitchenSinkPB>{int32_val: 5\n"
      "string_val: \"abc\"\n}",
      proto_2_1.FullDebugString());
  EXPECT_EQ(proto_1_2.FullDebugString(), proto_2_1.FullDebugString());
  EXPECT_NE(proto_1_2.ToCord(), proto_2_1.ToCord());
  EXPECT_NE(proto_1_2.ToString(), proto_2_1.ToString());

  // Test equality and hash codes when one message explicitly sets a field to
  // the default value and the other message leaves the field unset.
  k.Clear();
  k.set_int64_key_1(1);
  k.set_int64_key_2(2);
  Value unset_value = Proto(proto_type, k);
  k.set_bool_val(false);
  Value set_value = Proto(proto_type, k);
  EXPECT_NE(set_value, unset_value);
  EXPECT_NE(set_value.FullDebugString(), unset_value.FullDebugString());
  EXPECT_EQ(
      "Proto<googlesql_test.KitchenSinkPB>{int64_key_1: 1\n"
      "int64_key_2: 2\nbool_val: false\n}",
      set_value.FullDebugString());
  EXPECT_EQ(
      "Proto<googlesql_test.KitchenSinkPB>{int64_key_1: 1\n"
      "int64_key_2: 2\n}",
      unset_value.FullDebugString());
  EXPECT_FALSE(unset_value.Equals(set_value));
  TestHashEqual(set_value, unset_value);

  const ProtoType* proto3_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeProtoType(
      google::protobuf::Int32Value::descriptor(), &proto3_type));

  std::string result;
  {
    ::google::protobuf::io::StringOutputStream string_stream(&result);
    ::google::protobuf::io::CodedOutputStream output_stream(&string_stream);
    WireFormatLite::WriteInt32(/*field_number=*/1, /*value=*/0, &output_stream);
  }
  google::protobuf::Int32Value unset_proto3;
  absl::Cord result_bytes = absl::Cord(result);
  set_value = Value::Proto(proto3_type, result_bytes);
  absl::Cord bytes_3150;
  ABSL_CHECK(unset_proto3.SerializeToCord(&bytes_3150));
  unset_value = Value::Proto(proto3_type, bytes_3150);
  EXPECT_TRUE(set_value.Equals(unset_value));
  TestHashEqual(set_value, unset_value);
}

TEST_F(ValueTest, ProtoFormatting) {
  const ProtoType* proto_type = GetTestProtoType();
  googlesql_test::KitchenSinkPB k;
  k.set_int64_key_1(1);
  k.set_int64_key_2(2);
  k.set_double_val(12.00);
  const Value proto_value = Proto(proto_type, k);
  EXPECT_EQ(proto_value.DebugString(/*verbose=*/true),
            R"(Proto<googlesql_test.KitchenSinkPB>{int64_key_1: 1
int64_key_2: 2
double_val: 12
})");
  EXPECT_EQ(proto_value.DebugString(),
            "{int64_key_1: 1 int64_key_2: 2 double_val: 12}");
  EXPECT_EQ(proto_value.Format(), R"(PROTO<googlesql_test.KitchenSinkPB>{
  int64_key_1: 1
  int64_key_2: 2
  double_val: 12
})");
  EXPECT_EQ(proto_value.Format(/*print_top_level_type=*/false), R"({
  int64_key_1: 1
  int64_key_2: 2
  double_val: 12
})");
  EXPECT_EQ(proto_value.GetSQLLiteral(),
            R"sql("int64_key_1: 1 int64_key_2: 2 double_val: 12")sql");
  EXPECT_EQ(
      proto_value.GetSQL(),
      R"sql(CAST(b"\x08\x01\x10\x02I\x00\x00\x00\x00\x00\x00(@" AS `googlesql_test.KitchenSinkPB`))sql");
}

TEST_F(ValueTest, ClassAndProtoSize) {
  EXPECT_EQ(16, sizeof(Value))
      << "The size of Value class has changed, please also update the proto "
      << "and serialization code if you added/removed fields in it.";
  // TODO: Add Java serialization test for TIMESTAMP_PICO type.
  EXPECT_EQ(29, ValueProto::descriptor()->field_count())
      << "The number of fields in ValueProto has changed, please also update "
      << "the serialization code accordingly.";
  EXPECT_EQ(1, ValueProto::Array::descriptor()->field_count())
      << "The number of fields in ValueProto::Array has changed, please also "
      << "update the serialization code accordingly.";
  EXPECT_EQ(1, ValueProto::Struct::descriptor()->field_count())
      << "The number of fields in ValueProto::Struct has changed, please also "
      << "update the serialization code accordingly.";
  EXPECT_EQ(2, ValueProto::Range::descriptor()->field_count())
      << "The number of fields in ValueProto::Range has changed, please also "
      << "update the serialization code accordingly.";
  EXPECT_EQ(1, ValueProto::Map::descriptor()->field_count())
      << "The number of fields in ValueProto::Map has changed, please also "
      << "update the serialization code accordingly.";
  EXPECT_EQ(2, ValueProto::MapEntry::descriptor()->field_count())
      << "The number of fields in ValueProto::MapEntry has changed, please "
      << "also update the serialization code accordingly.";
}

TEST_F(ValueTest, HashSet) {
  absl::flat_hash_set<Value> s;
  EXPECT_EQ(true, s.insert(Int64(1)).second);
  EXPECT_EQ(false, s.insert(Int64(1)).second);
  EXPECT_EQ(true, s.insert(NullInt64()).second);
  EXPECT_EQ(false, s.insert(NullInt64()).second);

  EXPECT_EQ(true, s.insert(Bool(true)).second);
  EXPECT_EQ(false, s.insert(Bool(true)).second);
  EXPECT_EQ(true, s.insert(NullBool()).second);
  EXPECT_EQ(false, s.insert(NullBool()).second);

  EXPECT_EQ(true, s.insert(Double(1.3)).second);
  EXPECT_EQ(false, s.insert(Double(1.3)).second);
  EXPECT_EQ(true, s.insert(NullDouble()).second);
  EXPECT_EQ(false, s.insert(NullDouble()).second);

  EXPECT_EQ(true, s.insert(String("foo")).second);
  EXPECT_EQ(false, s.insert(String("foo")).second);
  EXPECT_EQ(true, s.insert(NullString()).second);
  EXPECT_EQ(false, s.insert(NullString()).second);

  const EnumType* enum_type = GetTestEnumType();
  EXPECT_EQ(true, s.insert(Enum(enum_type, 1)).second);
  EXPECT_EQ(false, s.insert(Enum(enum_type, 1)).second);
  EXPECT_EQ(true, s.insert(Null(enum_type)).second);
  EXPECT_EQ(false, s.insert(Null(enum_type)).second);

  const ProtoType* proto_type = GetTestProtoType();
  absl::Cord bytes("xyz");
  EXPECT_EQ(true, s.insert(Proto(proto_type, bytes)).second);
  EXPECT_EQ(false, s.insert(Proto(proto_type, bytes)).second);
  EXPECT_EQ(true, s.insert(Null(proto_type)).second);
  EXPECT_EQ(false, s.insert(Null(proto_type)).second);

  std::vector<Value> values = {Int64(1), Int64(2), Int64(1), NullInt64(),
                               NullInt64()};
  Value bag1 = InternalValue::ArrayChecked(MakeArrayType(Int64Type()),
                                           InternalValue::kIgnoresOrder,
                                           std::move(values));

  values = {Int64(1), NullInt64(), Int64(1), NullInt64(), Int64(2)};
  Value bag2 = InternalValue::ArrayChecked(MakeArrayType(Int64Type()),
                                           InternalValue::kIgnoresOrder,
                                           std::move(values));

  values = {Int64(1), NullInt64(), Int64(1), NullInt64(), Int64(2)};
  Value array1 = InternalValue::ArrayChecked(MakeArrayType(Int64Type()),
                                             InternalValue::kPreservesOrder,
                                             std::move(values));

  values = {Int64(1), NullInt64(), Int64(1), NullInt64(), Int64(2)};
  Value array2 = InternalValue::ArrayChecked(MakeArrayType(Int64Type()),
                                             InternalValue::kPreservesOrder,
                                             std::move(values));

  EXPECT_EQ(true, s.insert(bag1).second);
  EXPECT_EQ(false, s.insert(bag1).second);
  EXPECT_EQ(true, s.insert(bag2).second);
  EXPECT_EQ(false, s.insert(bag2).second);

  EXPECT_EQ(false, s.insert(array1).second);
  EXPECT_EQ(false, s.insert(array2).second);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* map_type_int64,
      MakeMapType(types::Int64Type(), types::Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* map_type_string,
      MakeMapType(types::StringType(), types::StringType()));

  EXPECT_TRUE(s.insert(Null(map_type_int64)).second);
  EXPECT_TRUE(s.insert(Null(map_type_string)).second);
  EXPECT_FALSE(s.insert(Null(map_type_int64)).second);
  EXPECT_FALSE(s.insert(Null(map_type_string)).second);

  EXPECT_TRUE(s.insert(Map({{"a", 1}, {"b", 2}})).second);
  EXPECT_FALSE(s.insert(Map({{"a", 1}, {"b", 2}})).second);
  EXPECT_TRUE(s.insert(Map({{1, "a"}, {2, "b"}})).second);
  EXPECT_FALSE(s.insert(Map({{1, "a"}, {2, "b"}})).second);
}

TEST_F(ValueTest, TimestampBounds) {
  const time_t lower = static_cast<time_t>(types::kTimestampSecondsMin);
  const time_t upper = static_cast<time_t>(types::kTimestampSecondsMax);
  setenv("TZ", "UTC", 1);
  EXPECT_EQ(std::string("Sat Jan  1 00:00:00 1678\n"), ctime(&lower));
  EXPECT_EQ(std::string("Tue Dec 31 23:59:59 2261\n"), ctime(&upper));
  // TODO Add similar tests that other timestamp bounds and date bounds
  // match these, once we have ToString functions for those types.
}

TEST_F(ValueTest, RangeConstructionSucceeds) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value result, Value::MakeRange(Date(1), Date(2)));
  EXPECT_EQ(Date(1), result.start());
  EXPECT_EQ(Date(2), result.end());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      result, Value::MakeRange(Date(1), Value::UnboundedEndDate()));
  EXPECT_EQ(Date(1), result.start());
  EXPECT_EQ(Value::UnboundedEndDate(), result.end());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      result, Value::MakeRange(Value::UnboundedStartDate(), Date(2)));
  EXPECT_EQ(Value::UnboundedStartDate(), result.start());
  EXPECT_EQ(Date(2), result.end());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      result,
      Value::MakeRange(Value::UnboundedStartDate(), Value::UnboundedEndDate()));
  EXPECT_EQ(Value::UnboundedStartDate(), result.start());
  EXPECT_EQ(Value::UnboundedEndDate(), result.end());
}

TEST_F(ValueTest, MakeRangeFromValidatedInputsSucceeds) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value result, Value::MakeRangeFromValidatedInputs(types::DateRangeType(),
                                                        Date(1), Date(2)));
  EXPECT_EQ(Date(1), result.start());
  EXPECT_EQ(Date(2), result.end());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      result, Value::MakeRangeFromValidatedInputs(
                  types::DateRangeType(), Date(1), Value::UnboundedEndDate()));
  EXPECT_EQ(Date(1), result.start());
  EXPECT_EQ(Value::UnboundedEndDate(), result.end());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(result, Value::MakeRangeFromValidatedInputs(
                                        types::DateRangeType(),
                                        Value::UnboundedStartDate(), Date(2)));
  EXPECT_EQ(Value::UnboundedStartDate(), result.start());
  EXPECT_EQ(Date(2), result.end());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      result, Value::MakeRangeFromValidatedInputs(types::DateRangeType(),
                                                  Value::UnboundedStartDate(),
                                                  Value::UnboundedEndDate()));
  EXPECT_EQ(Value::UnboundedStartDate(), result.start());
  EXPECT_EQ(Value::UnboundedEndDate(), result.end());
}

TEST_F(ValueTest, RangeConstructionUnequalTypesFails) {
  EXPECT_THAT(Value::MakeRange(Datetime(DatetimeValue::FromYMDHMSAndNanos(
                                   1, 2, 3, 4, 5, 6, 7)),
                               Date(99999)),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("must have the same type")));
  EXPECT_THAT(
      Value::MakeRange(Date(1), Datetime(DatetimeValue::FromYMDHMSAndNanos(
                                    1, 2, 3, 4, 5, 6, 7))),
      StatusIs(absl::StatusCode::kInternal,
               HasSubstr("must have the same type")));
}

TEST_F(ValueTest, RangeConstructionUnequalTypesFail) {
  EXPECT_THAT(
      Value::MakeRange(Date(1), Date(1)),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr(
              "Range start element must be smaller than range end element")));
  EXPECT_THAT(
      Value::MakeRange(Date(2), Date(1)),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr(
              "Range start element must be smaller than range end element")));
}

TEST_F(ValueTest, MakeRangeFromInvalidInputs) {
// Different range element types
#ifndef NDEBUG
  EXPECT_THAT(
      Value::MakeRangeFromValidatedInputs(
          types::DateRangeType(),
          Datetime(DatetimeValue::FromYMDHMSAndNanos(1, 2, 3, 4, 5, 6, 7)),
          Date(99999)),
      StatusIs(absl::StatusCode::kInternal,
               HasSubstr("must have the same type")));
#else
  GOOGLESQL_EXPECT_OK(Value::MakeRangeFromValidatedInputs(
      types::DateRangeType(),
      Datetime(DatetimeValue::FromYMDHMSAndNanos(1, 2, 3, 4, 5, 6, 7)),
      Date(99999)));
#endif

// Start value is not smaller than end value
#ifndef NDEBUG
  EXPECT_THAT(
      Value::MakeRangeFromValidatedInputs(types::DateRangeType(), Date(1),
                                          Date(1)),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr(
              "Range start element must be smaller than range end element")));
#else
  GOOGLESQL_EXPECT_OK(Value::MakeRangeFromValidatedInputs(types::DateRangeType(), Date(1),
                                                Date(1)));
#endif
}

TEST_F(ValueTest, RangeCopyAssign) {
  // Verify there are no memory leaks.
  {
    Value v1(Range(Date(1), Date(2)));
    EXPECT_EQ(TYPE_RANGE, v1.type()->kind());
    EXPECT_FALSE(v1.is_null());
    Value v2(v1);
    EXPECT_EQ(TYPE_RANGE, v2.type()->kind());
    EXPECT_FALSE(v2.is_null());
  }

  // Test the assignment operator.
  {
    Value v1 = Range(Date(1), Date(2));
    Value v2 = googlesql::values::Range(Date(1), Date(2));
    Value v3 = Value::Null(MakeRangeType(DatetimeType()));
    v3 = v1;
    EXPECT_EQ(TYPE_RANGE, v1.type()->kind());
    EXPECT_EQ(TYPE_RANGE, v2.type()->kind());
    EXPECT_EQ(TYPE_RANGE, v3.type()->kind());
    EXPECT_FALSE(v3.is_null());
  }
}

TEST_F(ValueTest, RangeNull) {
  {
    Value value = Value::Null(MakeRangeType(DateType()));
    EXPECT_EQ("RANGE<DATE>", value.type()->DebugString());
    EXPECT_TRUE(value.is_null());
    EXPECT_DEATH(value.start(), "Null value");
    EXPECT_DEATH(value.end(), "Null value");
    Value value_copy = value;
    EXPECT_TRUE(value.Equals(value_copy));
    EXPECT_EQ("RANGE<DATE>", value_copy.type()->DebugString());
    EXPECT_TRUE(value_copy.is_null());
  }

  {
    Value value = Value::Null(MakeRangeType(DatetimeType()));
    EXPECT_EQ("RANGE<DATETIME>", value.type()->DebugString());
    EXPECT_TRUE(value.is_null());
    EXPECT_DEATH(value.start(), "Null value");
    EXPECT_DEATH(value.end(), "Null value");
    Value value_copy = value;
    EXPECT_TRUE(value.Equals(value_copy));
    EXPECT_EQ("RANGE<DATETIME>", value_copy.type()->DebugString());
    EXPECT_TRUE(value_copy.is_null());
  }

  {
    Value value = Value::Null(MakeRangeType(TimestampType()));
    EXPECT_EQ("RANGE<TIMESTAMP>", value.type()->DebugString());
    EXPECT_TRUE(value.is_null());
    EXPECT_DEATH(value.start(), "Null value");
    EXPECT_DEATH(value.end(), "Null value");
    Value value_copy = value;
    EXPECT_TRUE(value.Equals(value_copy));
    EXPECT_EQ("RANGE<TIMESTAMP>", value_copy.type()->DebugString());
    EXPECT_TRUE(value_copy.is_null());
  }
}

TEST_F(ValueTest, RangeNonNull) {
  Value value = Range(Date(1), Date(2));
  EXPECT_EQ("RANGE<DATE>", value.type()->DebugString());
  EXPECT_TRUE(!value.is_null());
  EXPECT_EQ(Date(1), value.start());
  EXPECT_EQ(Date(2), value.end());
  Value value_copy = value;
  EXPECT_TRUE(value.Equals(value_copy));
  EXPECT_EQ("RANGE<DATE>", value_copy.type()->DebugString());
  EXPECT_FALSE(value_copy.is_null());
  EXPECT_EQ(Date(1), value_copy.start());
  EXPECT_EQ(Date(2), value_copy.end());
}

TEST_F(ValueTest, RangeUnbounded) {
  // Unbounded-start ranges.
  {
    Value value = Range(Value::UnboundedStartDate(), Date(2));
    EXPECT_EQ("RANGE<DATE>", value.type()->DebugString());
    EXPECT_TRUE(!value.is_null());
    EXPECT_EQ(Value::UnboundedStartDate(), value.start());
    EXPECT_EQ(Date(2), value.end());
    Value value_copy = value;
    EXPECT_TRUE(value.Equals(value_copy));
    EXPECT_EQ("RANGE<DATE>", value_copy.type()->DebugString());
    EXPECT_FALSE(value_copy.is_null());
    EXPECT_EQ(Value::UnboundedStartDate(), value_copy.start());
    EXPECT_EQ(Date(2), value_copy.end());
  }

  // Unbounded-end ranges.
  {
    Value value =
        Range(Datetime(DatetimeValue::FromYMDHMSAndNanos(1, 2, 3, 4, 5, 6, 7)),
              Value::UnboundedEndDatetime());
    EXPECT_EQ("RANGE<DATETIME>", value.type()->DebugString());
    EXPECT_TRUE(!value.is_null());
    EXPECT_EQ(Datetime(DatetimeValue::FromYMDHMSAndNanos(1, 2, 3, 4, 5, 6, 7)),
              value.start());
    EXPECT_EQ(Value::UnboundedEndDatetime(), value.end());
    Value value_copy = value;
    EXPECT_TRUE(value.Equals(value_copy));
    EXPECT_EQ("RANGE<DATETIME>", value_copy.type()->DebugString());
    EXPECT_FALSE(value_copy.is_null());
    EXPECT_EQ(Datetime(DatetimeValue::FromYMDHMSAndNanos(1, 2, 3, 4, 5, 6, 7)),
              value_copy.start());
    EXPECT_EQ(Value::UnboundedEndDatetime(), value_copy.end());
  }

  // Ranges unbounded at both ends.
  {
    Value value =
        Range(Value::UnboundedStartTimestamp(), Value::UnboundedEndTimestamp());
    EXPECT_EQ("RANGE<TIMESTAMP>", value.type()->DebugString());
    EXPECT_FALSE(value.is_null());
    EXPECT_EQ(Value::UnboundedStartTimestamp(), value.start());
    EXPECT_EQ(Value::UnboundedEndTimestamp(), value.end());
    Value value_copy = value;
    EXPECT_TRUE(value.Equals(value_copy));
    EXPECT_EQ("RANGE<TIMESTAMP>", value_copy.type()->DebugString());
    EXPECT_FALSE(value_copy.is_null());
    EXPECT_EQ(Value::UnboundedStartTimestamp(), value.start());
    EXPECT_EQ(Value::UnboundedEndTimestamp(), value.end());
  }
}

TEST_F(ValueTest, RangeBounded_EqualityWorks) {
  Value date_1_to_3 = Range(Date(1), Date(3));
  EXPECT_TRUE(date_1_to_3.Equals(Range(Date(1), Date(3))));
  EXPECT_FALSE(date_1_to_3.Equals(Range(Date(1), Date(2))));
  EXPECT_FALSE(date_1_to_3.Equals(Range(Date(2), Date(3))));

  Value datetime_1_to_3 = Range(
      Value::Datetime(DatetimeValue::FromYMDHMSAndNanos(1, 1, 1, 1, 1, 1, 1)),
      Value::Datetime(DatetimeValue::FromYMDHMSAndNanos(3, 3, 3, 3, 3, 3, 3)));
  EXPECT_TRUE(datetime_1_to_3.Equals(Range(
      Value::Datetime(DatetimeValue::FromYMDHMSAndNanos(1, 1, 1, 1, 1, 1, 1)),
      Value::Datetime(
          DatetimeValue::FromYMDHMSAndNanos(3, 3, 3, 3, 3, 3, 3)))));
  EXPECT_FALSE(datetime_1_to_3.Equals(Range(
      Value::Datetime(DatetimeValue::FromYMDHMSAndNanos(1, 1, 1, 1, 1, 1, 1)),
      Value::Datetime(
          DatetimeValue::FromYMDHMSAndNanos(2, 2, 2, 2, 2, 2, 2)))));
  EXPECT_FALSE(datetime_1_to_3.Equals(Range(
      Value::Datetime(DatetimeValue::FromYMDHMSAndNanos(2, 2, 2, 2, 2, 2, 2)),
      Value::Datetime(
          DatetimeValue::FromYMDHMSAndNanos(3, 3, 3, 3, 3, 3, 3)))));

  const absl::TimeZone utc = absl::UTCTimeZone();
  Value timestamp_1_to_3 =
      Range(Value::Timestamp(absl::FromCivil(
                absl::CivilSecond(1111, 01, 01, 01, 01, 01), utc)),
            Value::Timestamp(absl::FromCivil(
                absl::CivilSecond(3333, 03, 03, 03, 03, 03), utc)));
  EXPECT_TRUE(timestamp_1_to_3.Equals(Range(
      Value::Timestamp(
          absl::FromCivil(absl::CivilSecond(1111, 01, 01, 01, 01, 01), utc)),
      Value::Timestamp(
          absl::FromCivil(absl::CivilSecond(3333, 03, 03, 03, 03, 03), utc)))));
  EXPECT_FALSE(timestamp_1_to_3.Equals(Range(
      Value::Timestamp(
          absl::FromCivil(absl::CivilSecond(1111, 01, 01, 01, 01, 01), utc)),
      Value::Timestamp(
          absl::FromCivil(absl::CivilSecond(2222, 02, 02, 02, 02, 02), utc)))));
  EXPECT_FALSE(timestamp_1_to_3.Equals(Range(
      Value::Timestamp(
          absl::FromCivil(absl::CivilSecond(2222, 02, 02, 02, 02, 02), utc)),
      Value::Timestamp(
          absl::FromCivil(absl::CivilSecond(3333, 03, 03, 03, 03, 03), utc)))));
}

TEST_F(ValueTest, RangeNull_EqualityWorks) {
  Value date_range_1 = Value::Null(MakeRangeType(DateType()));
  Value date_range_2 = Value::Null(MakeRangeType(DateType()));
  EXPECT_TRUE(date_range_1.Equals(date_range_2));

  Value datetime_value_1 = Value::Null(MakeRangeType(DatetimeType()));
  Value datetime_value_2 = Value::Null(MakeRangeType(DatetimeType()));
  EXPECT_TRUE(datetime_value_1.Equals(datetime_value_2));

  Value timestamp_range_1 = Value::Null(MakeRangeType(TimestampType()));
  Value timestamp_range_2 = Value::Null(MakeRangeType(TimestampType()));
  EXPECT_TRUE(timestamp_range_1.Equals(timestamp_range_2));

  EXPECT_FALSE(date_range_1.Equals(datetime_value_1));
  EXPECT_FALSE(datetime_value_1.Equals(timestamp_range_1));
  EXPECT_FALSE(timestamp_range_1.Equals(date_range_1));
}

TEST_F(ValueTest, RangeUnboundedStart_EqualityWorks) {
  Value date_range_1 = Range(Value::UnboundedStartDate(), Date(1));
  Value date_range_2 = Range(Value::UnboundedStartDate(), Date(1));
  EXPECT_TRUE(date_range_1.Equals(date_range_2));

  Value datetime_value_1 = Range(
      Value::UnboundedStartDatetime(),
      Value::Datetime(DatetimeValue::FromYMDHMSAndNanos(1, 2, 3, 4, 5, 6, 7)));
  Value datetime_value_2 = Range(
      Value::UnboundedStartDatetime(),
      Value::Datetime(DatetimeValue::FromYMDHMSAndNanos(1, 2, 3, 4, 5, 6, 7)));
  EXPECT_TRUE(datetime_value_1.Equals(datetime_value_2));

  const absl::TimeZone utc = absl::UTCTimeZone();
  Value timestamp_range_1 =
      Range(Value::UnboundedStartTimestamp(),
            Value::Timestamp(absl::FromCivil(
                absl::CivilSecond(2020, 01, 02, 10, 00, 00), utc)));
  Value timestamp_range_2 =
      Range(Value::UnboundedStartTimestamp(),
            Value::Timestamp(absl::FromCivil(
                absl::CivilSecond(2020, 01, 02, 10, 00, 00), utc)));
  EXPECT_TRUE(timestamp_range_1.Equals(timestamp_range_2));

  EXPECT_FALSE(date_range_1.Equals(datetime_value_1));
  EXPECT_FALSE(datetime_value_1.Equals(timestamp_range_1));
  EXPECT_FALSE(timestamp_range_1.Equals(date_range_1));
}

TEST_F(ValueTest, RangeUnboundedEnd_EqualityWorks) {
  Value date_range_1 = Range(Date(1), Value::UnboundedEndDate());
  Value date_range_2 = Range(Date(1), Value::UnboundedEndDate());
  EXPECT_TRUE(date_range_1.Equals(date_range_2));

  Value datetime_value_1 = Range(
      Value::Datetime(DatetimeValue::FromYMDHMSAndNanos(1, 2, 3, 4, 5, 6, 7)),
      Value::UnboundedEndDatetime());
  Value datetime_value_2 = Range(
      Value::Datetime(DatetimeValue::FromYMDHMSAndNanos(1, 2, 3, 4, 5, 6, 7)),
      Value::UnboundedEndDatetime());
  EXPECT_TRUE(datetime_value_1.Equals(datetime_value_2));

  const absl::TimeZone utc = absl::UTCTimeZone();
  Value timestamp_range_1 =
      Range(Value::Timestamp(absl::FromCivil(
                absl::CivilSecond(2020, 01, 02, 10, 00, 00), utc)),
            Value::UnboundedEndTimestamp());
  Value timestamp_range_2 =
      Range(Value::Timestamp(absl::FromCivil(
                absl::CivilSecond(2020, 01, 02, 10, 00, 00), utc)),
            Value::UnboundedEndTimestamp());
  EXPECT_TRUE(timestamp_range_1.Equals(timestamp_range_2));

  EXPECT_FALSE(date_range_1.Equals(datetime_value_1));
  EXPECT_FALSE(datetime_value_1.Equals(timestamp_range_1));
  EXPECT_FALSE(timestamp_range_1.Equals(date_range_1));
}

TEST_F(ValueTest, RangeUnboundedStartAndEnd_EqualityWorks) {
  Value date_range_1 =
      Range(Value::UnboundedStartDate(), Value::UnboundedEndDate());
  Value date_range_2 =
      Range(Value::UnboundedStartDate(), Value::UnboundedEndDate());
  EXPECT_TRUE(date_range_1.Equals(date_range_2));

  Value datetime_value_1 =
      Range(Value::UnboundedStartDatetime(), Value::UnboundedEndDatetime());
  Value datetime_value_2 =
      Range(Value::UnboundedStartDatetime(), Value::UnboundedEndDatetime());
  EXPECT_TRUE(datetime_value_1.Equals(datetime_value_2));

  Value timestamp_range_1 =
      Range(Value::UnboundedStartTimestamp(), Value::UnboundedEndTimestamp());
  Value timestamp_range_2 =
      Range(Value::UnboundedStartTimestamp(), Value::UnboundedEndTimestamp());
  EXPECT_TRUE(timestamp_range_1.Equals(timestamp_range_2));

  EXPECT_FALSE(date_range_1.Equals(datetime_value_1));
  EXPECT_FALSE(datetime_value_1.Equals(timestamp_range_1));
  EXPECT_FALSE(timestamp_range_1.Equals(date_range_1));
}

TEST_F(ValueTest, RangeOfDatesFormatting) {
  const Value range_d_regular = Range(Value::Date(300), Value::Date(301));
  // Regular range
  EXPECT_EQ(range_d_regular.DebugString(/*verbose=*/true),
            "Range[Date(1970-10-28), Date(1970-10-29))");
  EXPECT_EQ(range_d_regular.DebugString(), R"([1970-10-28, 1970-10-29))");
  EXPECT_EQ(range_d_regular.Format(), R"(RANGE<DATE>[1970-10-28, 1970-10-29))");
  EXPECT_EQ(range_d_regular.Format(/*print_top_level_type=*/false),
            R"([1970-10-28, 1970-10-29))");
  EXPECT_EQ(range_d_regular.GetSQLLiteral(),
            R"sql(RANGE<DATE> "[1970-10-28, 1970-10-29)")sql");
  EXPECT_EQ(range_d_regular.GetSQL(),
            R"sql(RANGE<DATE> "[1970-10-28, 1970-10-29)")sql");

  // Range with unbounded start
  const Value range_d_unbounded_start =
      Range(Value::UnboundedStartDate(), Value::Date(301));
  EXPECT_EQ(range_d_unbounded_start.DebugString(/*verbose=*/true),
            R"(Range[Date(NULL), Date(1970-10-29)))");
  EXPECT_EQ(range_d_unbounded_start.DebugString(), R"([NULL, 1970-10-29))");
  EXPECT_EQ(range_d_unbounded_start.Format(),
            R"(RANGE<DATE>[NULL, 1970-10-29))");
  EXPECT_EQ(range_d_unbounded_start.Format(/*print_top_level_type=*/false),
            R"([NULL, 1970-10-29))");
  EXPECT_EQ(range_d_unbounded_start.GetSQLLiteral(),
            R"sql(RANGE<DATE> "[UNBOUNDED, 1970-10-29)")sql");
  EXPECT_EQ(range_d_unbounded_start.GetSQL(),
            R"sql(RANGE<DATE> "[UNBOUNDED, 1970-10-29)")sql");

  // Range with unbounded end
  const Value range_d_unbounded_end =
      Range(Value::Date(300), Value::UnboundedEndDate());
  EXPECT_EQ(range_d_unbounded_end.DebugString(/*verbose=*/true),
            R"(Range[Date(1970-10-28), Date(NULL)))");
  EXPECT_EQ(range_d_unbounded_end.DebugString(), R"([1970-10-28, NULL))");
  EXPECT_EQ(range_d_unbounded_end.Format(), R"(RANGE<DATE>[1970-10-28, NULL))");
  EXPECT_EQ(range_d_unbounded_end.Format(/*print_top_level_type=*/false),
            R"([1970-10-28, NULL))");
  EXPECT_EQ(range_d_unbounded_end.GetSQLLiteral(),
            R"sql(RANGE<DATE> "[1970-10-28, UNBOUNDED)")sql");
  EXPECT_EQ(range_d_unbounded_end.GetSQL(),
            R"sql(RANGE<DATE> "[1970-10-28, UNBOUNDED)")sql");

  // Range with unbounded start and end
  const Value range_d_unbounded_all =
      Range(Value::UnboundedStartDate(), Value::UnboundedEndDate());
  EXPECT_EQ(range_d_unbounded_all.DebugString(/*verbose=*/true),
            R"(Range[Date(NULL), Date(NULL)))");
  EXPECT_EQ(range_d_unbounded_all.DebugString(), R"([NULL, NULL))");
  EXPECT_EQ(range_d_unbounded_all.Format(), R"(RANGE<DATE>[NULL, NULL))");
  EXPECT_EQ(range_d_unbounded_all.Format(/*print_top_level_type=*/false),
            R"([NULL, NULL))");
  EXPECT_EQ(range_d_unbounded_all.GetSQLLiteral(),
            R"sql(RANGE<DATE> "[UNBOUNDED, UNBOUNDED)")sql");
  EXPECT_EQ(range_d_unbounded_all.GetSQL(),
            R"sql(RANGE<DATE> "[UNBOUNDED, UNBOUNDED)")sql");
}

TEST_F(ValueTest, RangeOfDatetimesFormatting) {
  const Value range_dt_regular =
      Range(Value::Datetime(
                DatetimeValue::FromYMDHMSAndNanos(2022, 9, 13, 16, 36, 11, 1)),
            Value::Datetime(
                DatetimeValue::FromYMDHMSAndNanos(2022, 9, 13, 16, 37, 11, 1)));

  // Regular range
  EXPECT_EQ(range_dt_regular.DebugString(/*verbose=*/true),
            "Range[Datetime(2022-09-13 16:36:11.000000001), "
            "Datetime(2022-09-13 16:37:11.000000001))");
  EXPECT_EQ(
      range_dt_regular.DebugString(),
      R"([2022-09-13 16:36:11.000000001, 2022-09-13 16:37:11.000000001))");
  EXPECT_EQ(range_dt_regular.Format(),
            R"(RANGE<DATETIME>[
  2022-09-13 16:36:11.000000001,
  2022-09-13 16:37:11.000000001
))");
  EXPECT_EQ(range_dt_regular.Format(/*print_top_level_type=*/false),
            R"([
  2022-09-13 16:36:11.000000001,
  2022-09-13 16:37:11.000000001
))");
  EXPECT_EQ(
      range_dt_regular.GetSQLLiteral(),
      R"sql(RANGE<DATETIME> "[2022-09-13 16:36:11.000000001, 2022-09-13 16:37:11.000000001)")sql");
  EXPECT_EQ(
      range_dt_regular.GetSQL(),
      R"sql(RANGE<DATETIME> "[2022-09-13 16:36:11.000000001, 2022-09-13 16:37:11.000000001)")sql");

  // Range with unbounded start
  const Value range_dt_unbounded_start =
      Range(Value::UnboundedStartDatetime(),
            Value::Datetime(
                DatetimeValue::FromYMDHMSAndNanos(2022, 9, 13, 16, 37, 11, 1)));
  EXPECT_EQ(range_dt_unbounded_start.DebugString(/*verbose=*/true),
            "Range[Datetime(NULL), Datetime(2022-09-13 16:37:11.000000001))");
  EXPECT_EQ(range_dt_unbounded_start.DebugString(),
            R"([NULL, 2022-09-13 16:37:11.000000001))");
  EXPECT_EQ(range_dt_unbounded_start.Format(),
            R"(RANGE<DATETIME>[
  NULL,
  2022-09-13 16:37:11.000000001
))");
  EXPECT_EQ(range_dt_unbounded_start.Format(/*print_top_level_type=*/false),
            R"([
  NULL,
  2022-09-13 16:37:11.000000001
))");
  EXPECT_EQ(
      range_dt_unbounded_start.GetSQLLiteral(),
      R"sql(RANGE<DATETIME> "[UNBOUNDED, 2022-09-13 16:37:11.000000001)")sql");
  EXPECT_EQ(
      range_dt_unbounded_start.GetSQL(),
      R"sql(RANGE<DATETIME> "[UNBOUNDED, 2022-09-13 16:37:11.000000001)")sql");

  // Range with unbounded end
  const Value range_dt_unbounded_end =
      Range(Value::Datetime(
                DatetimeValue::FromYMDHMSAndNanos(2022, 9, 13, 16, 36, 11, 1)),
            Value::UnboundedEndDatetime());
  EXPECT_EQ(range_dt_unbounded_end.DebugString(/*verbose=*/true),
            "Range[Datetime(2022-09-13 16:36:11.000000001), Datetime(NULL))");
  EXPECT_EQ(range_dt_unbounded_end.DebugString(),
            R"([2022-09-13 16:36:11.000000001, NULL))");
  EXPECT_EQ(range_dt_unbounded_end.Format(),
            R"(RANGE<DATETIME>[
  2022-09-13 16:36:11.000000001,
  NULL
))");
  EXPECT_EQ(range_dt_unbounded_end.Format(/*print_top_level_type=*/false),
            R"([
  2022-09-13 16:36:11.000000001,
  NULL
))");
  EXPECT_EQ(
      range_dt_unbounded_end.GetSQLLiteral(),
      R"sql(RANGE<DATETIME> "[2022-09-13 16:36:11.000000001, UNBOUNDED)")sql");
  EXPECT_EQ(
      range_dt_unbounded_end.GetSQL(),
      R"sql(RANGE<DATETIME> "[2022-09-13 16:36:11.000000001, UNBOUNDED)")sql");

  // Range with unbounded start and end
  const Value range_dt_unbounded_all =
      Range(Value::UnboundedStartDatetime(), Value::UnboundedEndDatetime());
  EXPECT_EQ(range_dt_unbounded_all.DebugString(/*verbose=*/true),
            "Range[Datetime(NULL), Datetime(NULL))");
  EXPECT_EQ(range_dt_unbounded_all.DebugString(), R"([NULL, NULL))");
  EXPECT_EQ(range_dt_unbounded_all.Format(), R"(RANGE<DATETIME>[NULL, NULL))");
  EXPECT_EQ(range_dt_unbounded_all.Format(/*print_top_level_type=*/false),
            R"([NULL, NULL))");
  EXPECT_EQ(range_dt_unbounded_all.GetSQLLiteral(),
            R"sql(RANGE<DATETIME> "[UNBOUNDED, UNBOUNDED)")sql");
  EXPECT_EQ(range_dt_unbounded_all.GetSQL(),
            R"sql(RANGE<DATETIME> "[UNBOUNDED, UNBOUNDED)")sql");
}

TEST_F(ValueTest, FormatRangeOfTimestamps) {
  const absl::TimeZone utc = absl::UTCTimeZone();
  absl::Time tmin =
      absl::FromCivil(absl::CivilSecond(00001, 01, 01, 00, 00, 00), utc);
  absl::Time tmax =
      absl::FromCivil(absl::CivilSecond(10000, 01, 01, 00, 00, 00), utc) -
      absl::Nanoseconds(1);

  // Regular range
  const Value range_ts_regular =
      Range(Value::Timestamp(tmin), Value::Timestamp(tmax));
  EXPECT_EQ(range_ts_regular.DebugString(/*verbose=*/true),
            "Range[Timestamp(0001-01-01 00:00:00+00), Timestamp(9999-12-31 "
            "23:59:59.999999999+00))");
  EXPECT_EQ(range_ts_regular.DebugString(),
            R"([0001-01-01 00:00:00+00, 9999-12-31 23:59:59.999999999+00))");
  EXPECT_EQ(range_ts_regular.Format(),
            R"(RANGE<TIMESTAMP>[
  0001-01-01 00:00:00+00,
  9999-12-31 23:59:59.999999999+00
))");
  EXPECT_EQ(range_ts_regular.Format(/*print_top_level_type=*/false),
            R"([
  0001-01-01 00:00:00+00,
  9999-12-31 23:59:59.999999999+00
))");
  EXPECT_EQ(
      range_ts_regular.GetSQLLiteral(),
      R"sql(RANGE<TIMESTAMP> "[0001-01-01 00:00:00+00, 9999-12-31 23:59:59.999999999+00)")sql");
  EXPECT_EQ(
      range_ts_regular.GetSQL(),
      R"sql(RANGE<TIMESTAMP> "[0001-01-01 00:00:00+00, 9999-12-31 23:59:59.999999999+00)")sql");

  // Range with unbounded start
  const Value range_ts_unbounded_start =
      Range(Value::UnboundedStartTimestamp(), Value::Timestamp(tmax));
  EXPECT_EQ(
      range_ts_unbounded_start.DebugString(/*verbose=*/true),
      "Range[Timestamp(NULL), Timestamp(9999-12-31 23:59:59.999999999+00))");
  EXPECT_EQ(range_ts_unbounded_start.DebugString(),
            R"([NULL, 9999-12-31 23:59:59.999999999+00))");
  EXPECT_EQ(range_ts_unbounded_start.Format(),
            R"(RANGE<TIMESTAMP>[
  NULL,
  9999-12-31 23:59:59.999999999+00
))");
  EXPECT_EQ(range_ts_unbounded_start.Format(/*print_top_level_type=*/false),
            R"([
  NULL,
  9999-12-31 23:59:59.999999999+00
))");
  EXPECT_EQ(
      range_ts_unbounded_start.GetSQLLiteral(),
      R"sql(RANGE<TIMESTAMP> "[UNBOUNDED, 9999-12-31 23:59:59.999999999+00)")sql");
  EXPECT_EQ(
      range_ts_unbounded_start.GetSQL(),
      R"sql(RANGE<TIMESTAMP> "[UNBOUNDED, 9999-12-31 23:59:59.999999999+00)")sql");

  // Range with unbounded end
  const Value range_ts_unbounded_end =
      Range(Value::Timestamp(tmin), Value::UnboundedEndTimestamp());
  EXPECT_EQ(range_ts_unbounded_end.DebugString(/*verbose=*/true),
            "Range[Timestamp(0001-01-01 00:00:00+00), Timestamp(NULL))");
  EXPECT_EQ(range_ts_unbounded_end.DebugString(),
            R"([0001-01-01 00:00:00+00, NULL))");
  EXPECT_EQ(range_ts_unbounded_end.Format(),
            R"(RANGE<TIMESTAMP>[
  0001-01-01 00:00:00+00,
  NULL
))");
  EXPECT_EQ(range_ts_unbounded_end.Format(/*print_top_level_type=*/false),
            R"([
  0001-01-01 00:00:00+00,
  NULL
))");
  EXPECT_EQ(range_ts_unbounded_end.GetSQLLiteral(),
            R"sql(RANGE<TIMESTAMP> "[0001-01-01 00:00:00+00, UNBOUNDED)")sql");
  EXPECT_EQ(range_ts_unbounded_end.GetSQL(),
            R"sql(RANGE<TIMESTAMP> "[0001-01-01 00:00:00+00, UNBOUNDED)")sql");

  // Range with unbounded start and end
  const Value range_ts_unbounded_all =
      Range(Value::UnboundedStartTimestamp(), Value::UnboundedEndTimestamp());
  EXPECT_EQ(range_ts_unbounded_all.DebugString(/*verbose=*/true),
            "Range[Timestamp(NULL), Timestamp(NULL))");
  EXPECT_EQ(range_ts_unbounded_all.DebugString(), R"([NULL, NULL))");
  EXPECT_EQ(range_ts_unbounded_all.Format(), R"(RANGE<TIMESTAMP>[NULL, NULL))");
  EXPECT_EQ(range_ts_unbounded_all.Format(/*print_top_level_type=*/false),
            R"([NULL, NULL))");
  EXPECT_EQ(range_ts_unbounded_all.GetSQLLiteral(),
            R"sql(RANGE<TIMESTAMP> "[UNBOUNDED, UNBOUNDED)")sql");
  EXPECT_EQ(range_ts_unbounded_all.GetSQL(),
            R"sql(RANGE<TIMESTAMP> "[UNBOUNDED, UNBOUNDED)")sql");
}

TEST_F(ValueTest, FormatArrayOfRanges) {
  const Value regular_range = Range(Value::Date(300), Value::Date(301));
  const Value range_unbounded_start =
      Range(Value::UnboundedStartDate(), Value::Date(22));
  const Value range_null = Value::Null(types::DateRangeType());
  const auto* range_array_type = MakeArrayType(types::DateRangeType());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value array_of_ranges,
      Value::MakeArray(range_array_type,
                       {regular_range, range_unbounded_start, range_null}));
  EXPECT_EQ(array_of_ranges.DebugString(/*verbose=*/true),
            "Array[Range[Date(1970-10-28), Date(1970-10-29)), "
            "Range[Date(NULL), Date(1970-01-23)), Range<DATE>(NULL)]");
  EXPECT_EQ(array_of_ranges.DebugString(),
            R"([[1970-10-28, 1970-10-29), [NULL, 1970-01-23), NULL])");
  EXPECT_EQ(array_of_ranges.Format(), R"(ARRAY<RANGE<DATE>>[
  [1970-10-28, 1970-10-29),
  [NULL, 1970-01-23),
  NULL
])");
  EXPECT_EQ(array_of_ranges.Format(/*print_top_level_type=*/false),
            R"(ARRAY<RANGE<DATE>>[
  [1970-10-28, 1970-10-29),
  [NULL, 1970-01-23),
  NULL
])");
  EXPECT_EQ(
      array_of_ranges.GetSQLLiteral(),
      R"sql([RANGE<DATE> "[1970-10-28, 1970-10-29)", RANGE<DATE> "[UNBOUNDED, 1970-01-23)", NULL])sql");
  EXPECT_EQ(
      array_of_ranges.GetSQL(),
      R"sql(ARRAY<RANGE<DATE>>[RANGE<DATE> "[1970-10-28, 1970-10-29)", RANGE<DATE> "[UNBOUNDED, 1970-01-23)", CAST(NULL AS RANGE<DATE>)])sql");
}

TEST_F(ValueTest, FormatStructOfRanges) {
  const Value range_of_dates = Range(Value::Date(300), Value::Date(301));
  const Value range_of_datetimes =
      Range(Value::UnboundedStartDatetime(),
            Value::Datetime(
                DatetimeValue::FromYMDHMSAndNanos(2022, 9, 13, 16, 37, 11, 1)));
  const Value range_of_timestamps = Value::Null(types::TimestampRangeType());
  const auto* range_struct_type =
      MakeStructType({{"d", types::DateRangeType()},
                      {"dt", types::DatetimeRangeType()},
                      {"t", types::TimestampRangeType()}});
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Value struct_of_ranges,
      Value::MakeStruct(range_struct_type, {range_of_dates, range_of_datetimes,
                                            range_of_timestamps}));
  EXPECT_EQ(struct_of_ranges.DebugString(/*verbose=*/true),
            "Struct{d:Range[Date(1970-10-28), Date(1970-10-29)), "
            "dt:Range[Datetime(NULL), Datetime(2022-09-13 "
            "16:37:11.000000001)), t:Range<TIMESTAMP>(NULL)}");
  EXPECT_EQ(
      struct_of_ranges.DebugString(),
      R"({d:[1970-10-28, 1970-10-29), dt:[NULL, 2022-09-13 16:37:11.000000001), t:NULL})");
  EXPECT_EQ(struct_of_ranges.Format(),
            R"(STRUCT<d RANGE<DATE>, dt RANGE<DATETIME>, t RANGE<TIMESTAMP>>{
  [1970-10-28, 1970-10-29),
  [
    NULL,
    2022-09-13 16:37:11.000000001
  ),
  NULL
})");
  EXPECT_EQ(struct_of_ranges.Format(/*print_top_level_type=*/false),
            R"({[1970-10-28, 1970-10-29),
 [
   NULL,
   2022-09-13 16:37:11.000000001
 ),
 NULL})");
  EXPECT_EQ(
      struct_of_ranges.GetSQLLiteral(),
      R"sql((RANGE<DATE> "[1970-10-28, 1970-10-29)", RANGE<DATETIME> "[UNBOUNDED, 2022-09-13 16:37:11.000000001)", NULL))sql");
  EXPECT_EQ(
      struct_of_ranges.GetSQL(),
      R"sql(STRUCT<d RANGE<DATE>, dt RANGE<DATETIME>, t RANGE<TIMESTAMP>>(RANGE<DATE> "[1970-10-28, 1970-10-29)", RANGE<DATETIME> "[UNBOUNDED, 2022-09-13 16:37:11.000000001)", CAST(NULL AS RANGE<TIMESTAMP>)))sql");
}

TEST_F(ValueTest, ValueConstructor) {
  const char chars[] = "a";
  absl::string_view pc(chars);
  ValueConstructor int32_val = {1};
  ValueConstructor uint32_val = {1u};
  ValueConstructor int64_val = {int64_t{1}};
  ValueConstructor uint64_val = {1ull};
  ValueConstructor bool_val = {True()};
  ValueConstructor float_val = {4.0F};
  ValueConstructor double_val = {3.5};
  ValueConstructor string_val = {"a"};
  ValueConstructor string_val2 = {chars};
  ValueConstructor string_val3 = {pc};

  EXPECT_EQ(Int32(1), int32_val.get());
  EXPECT_EQ(Uint32(1), uint32_val.get());
  EXPECT_EQ(Int64(1), int64_val.get());
  EXPECT_EQ(Uint64(1), uint64_val.get());
  EXPECT_EQ(True(), bool_val.get());
  EXPECT_EQ(Float(4), float_val.get());
  EXPECT_EQ(Double(3.5), double_val.get());
  EXPECT_EQ(String("a"), string_val.get());
  EXPECT_EQ(String("a"), string_val2.get());
  EXPECT_EQ(String("a"), string_val3.get());

  std::vector<Value> l = ValueConstructor::ToValues(
      {1, 1u, int64_t{1}, 1ull, True(), 4.0F, 3.5, "a"});
  EXPECT_EQ(Int32(1), l[0]);
  EXPECT_EQ(Uint32(1), l[1]);
  EXPECT_EQ(Int64(1), l[2]);
  EXPECT_EQ(Uint64(1), l[3]);
  EXPECT_EQ(True(), l[4]);
  EXPECT_EQ(Float(4), l[5]);
  EXPECT_EQ(Double(3.5), l[6]);
  EXPECT_EQ(String("a"), l[7]);
}

TEST_F(ValueTest, FormatTestArrayWrapping) {
  // Array that should Format on one line.
  EXPECT_EQ(Array({1, 2, 3, 4}).Format(), "ARRAY<INT32>[1, 2, 3, 4]");
  // Array that triggers wrap due to long element
  EXPECT_EQ(Array({"abcdefghijklmnopqrstuvw", "x", "y", "z"}).Format(),
            R"(ARRAY<STRING>[
  "abcdefghijklmnopqrstuvw",
  "x",
  "y",
  "z"
])");
  // Array that triggers wrap because its long.
  EXPECT_EQ(Array({"h", "i", "j", "k", "l", "m", "n", "p", "p", "q", "r", "s",
                   "t", "u", "v", "w", "x", "y", "z"})
                .Format(),
            R"(ARRAY<STRING>["h",
              "i",
              "j",
              "k",
              "l",
              "m",
              "n",
              "p",
              "p",
              "q",
              "r",
              "s",
              "t",
              "u",
              "v",
              "w",
              "x",
              "y",
              "z"])");
}

TEST_F(ValueTest, FormatTestStructWrapping) {
  // Struct that should Format on one line.
  EXPECT_EQ(Struct({"a", "b"}, {1, 2}).Format(),
            R"(STRUCT<a INT32, b INT32>{1, 2})");
  // Struct that triggers wrap due to many fields
  EXPECT_EQ(
      Struct({"a", "b", "c", "d", "e", "f", "g"}, {1, 2, 3, 4, 5, 6, 7})
          .Format(),
      R"(STRUCT<a INT32, b INT32, c INT32, d INT32, e INT32, f INT32, g INT32>{
  1,
  2,
  3,
  4,
  5,
  6,
  7
})");
}

TEST_F(ValueTest, FormatTestSanitization) {
  // Struct type containing dollar signs
  EXPECT_EQ(Struct({"$col1", "col2$", "$", "$$", "$$$", "$0$1$2", "$$0", "$$$0",
                    "$$$$0"},
                   {"$val1", "val2$", "$", "$$", "$$$", "$0$1$2", "$$0", "$$$0",
                    "$$$$0"})
                .Format(),
            R"(STRUCT<$col1 STRING,
       col2$ STRING,
       $ STRING,
       $$ STRING,
       $$$ STRING,
       $0$1$2 STRING,
       $$0 STRING,
       $$$0 STRING,
       $$$$0 STRING>{
  "$val1",
  "val2$",
  "$",
  "$$",
  "$$$",
  "$0$1$2",
  "$$0",
  "$$$0",
  "$$$$0"
})");
  // Array type containing dollar signs
  EXPECT_EQ(StructArray({"$col1", "col2$", "$", "$$", "$$$", "$0$1$2", "$$0",
                         "$$$0", "$$$$0"},
                        {{"$val1", "val2$", "$", "$$", "$$$", "$0$1$2", "$$0",
                          "$$$0", "$$$$0"}})
                .Format(),
            R"(ARRAY<STRUCT<$col1 STRING,
             col2$ STRING,
             $ STRING,
             $$ STRING,
             $$$ STRING,
             $0$1$2 STRING,
             $$0 STRING,
             $$$0 STRING,
             $$$$0 STRING>>[
  {"$val1", "val2$", "$", "$$", "$$$", "$0$1$2", "$$0", "$$$0", "$$$$0"}
])");
  // Array type containing dollar signs and long enough to trigger wrapping
  // after the multi-line array type.
  EXPECT_EQ(StructArray({"$col1", "col2$", "$", "$$", "$$$", "$0$1$2", "$$0",
                         "$$$0", "$$$$0", "extralong$typename"},
                        {{"$val1", "val2$", "$", "$$", "$$$", "$0$1$2", "$$0",
                          "$$$0", "$$$$0", "extralong$value"}})
                .Format(),
            R"(ARRAY<STRUCT<
        $col1 STRING,
        col2$ STRING,
        $ STRING,
        $$ STRING,
        $$$ STRING,
        $0$1$2 STRING,
        $$0 STRING,
        $$$0 STRING,
        $$$$0 STRING,
        extralong$typename STRING
      >>
[{"$val1",
  "val2$",
  "$",
  "$$",
  "$$$",
  "$0$1$2",
  "$$0",
  "$$$0",
  "$$$$0",
  "extralong$value"}])");
}

TEST_F(ValueTest, FormatTestTypeTruncation) {
  Value v = Array({Struct({"", ""}, {Array({2, 1}), Array({"bar", "foo"})})});
  // The type of an array should be printed down to the next level of arrays.
  EXPECT_EQ(v.Format(), R"(ARRAY<STRUCT<ARRAY<>, ARRAY<>>>[
  {
    ARRAY<INT32>[2, 1],
    ARRAY<STRING>["bar", "foo"]
  }
])");

  // The full type of a empty array should be printed.
  EXPECT_EQ(Value::EmptyArray(v.type()->AsArray()).Format(),
            R"(ARRAY<STRUCT<ARRAY<INT32>, ARRAY<STRING>>>[])");

  // The full type of a null array should be printed.
  EXPECT_EQ(Value::Null(v.type()->AsArray()).Format(),
            R"(ARRAY<STRUCT<ARRAY<INT32>, ARRAY<STRING>>>(NULL))");
}

TEST_F(ValueTest, FormatWrapAfterMultiLineArrayType) {
  // If an array type is broken over several lines, the opening square bracket
  // moves to its own line and the
  Value v = Array({Struct({"struct_field1", "struct_field2", "struct_field3",
                           "struct_field4"},
                          {"11", "12", "13", "14"}),
                   Struct({"struct_field1", "struct_field2", "struct_field3",
                           "struct_field4"},
                          {"21", "22", "23", "24"})});

  // The type of an array should be printed down to the next level of arrays.
  EXPECT_EQ(v.Format(), R"(ARRAY<STRUCT<struct_field1 STRING,
             struct_field2 STRING,
             struct_field3 STRING,
             struct_field4 STRING>>
[
  {"11", "12", "13", "14"},
  {"21", "22", "23", "24"}
])");
}

TEST_F(ValueTest, ProtoFormatTest) {
  googlesql_test::KitchenSinkPB k;
  k.set_int64_key_1(1);
  k.set_int64_key_2(2);
  k.set_int32_val(3);
  // Empty proto.
  absl::Cord bytes_4153;
  ABSL_CHECK(k.SerializePartialToCord(&bytes_4153));
  absl::Cord bytes = bytes_4153;

  const ProtoType* proto_type = GetTestProtoType();
  EXPECT_EQ(Struct({"p", "i"}, {Proto(proto_type, bytes), 1}).Format(),
            R"(STRUCT<
  p PROTO<googlesql_test.KitchenSinkPB>,
  i INT32
>{{
    int64_key_1: 1
    int64_key_2: 2
    int32_val: 3
  },
  1})");
}

// Test that proto value equality still works as expected when the proto
// descriptors come from different pools.
TEST_F(ValueTest, EquivalentProtos) {
  // Build a second DescriptorPool with copies of all types in the default pool.
  google::protobuf::DescriptorPoolDatabase database(
      *google::protobuf::DescriptorPool::generated_pool());
  google::protobuf::DescriptorPool alt_pool(&database);

  TypeFactory type_factory;

  const ProtoType* proto_type;
  const ProtoType* alt_proto_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeProtoType(
      googlesql_test::TestExtraPB::descriptor(), &proto_type));
  GOOGLESQL_ASSERT_OK(type_factory.MakeProtoType(
      alt_pool.FindMessageTypeByName("googlesql_test.TestExtraPB"),
      &alt_proto_type));

  EXPECT_FALSE(proto_type->Equals(alt_proto_type));
  EXPECT_TRUE(proto_type->Equivalent(alt_proto_type));

  googlesql_test::TestExtraPB proto_value;
  proto_value.set_int32_val1(5);
  absl::Cord bytes = absl::Cord(proto_value.SerializeAsString());
  Value value1 = Value::Proto(proto_type, bytes);
  Value value2 = Value::Proto(alt_proto_type, bytes);
  EXPECT_TRUE(value1.Equals(value2));

  proto_value.set_int32_val2(6);
  bytes = proto_value.SerializeAsString();
  value2 = Value::Proto(alt_proto_type, bytes);

  EXPECT_FALSE(value1.Equals(value2));

  // Do the same test with a proto enum type.
  const EnumType* enum_type;
  const EnumType* alt_enum_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeEnumType(googlesql_test::TestEnum_descriptor(),
                                      &enum_type));
  GOOGLESQL_ASSERT_OK(type_factory.MakeEnumType(
      alt_pool.FindEnumTypeByName("googlesql_test.TestEnum"), &alt_enum_type));

  EXPECT_FALSE(enum_type->Equals(alt_enum_type));
  EXPECT_TRUE(enum_type->Equivalent(alt_enum_type));

  EXPECT_TRUE(Value::Enum(enum_type, 1).Equals(Value::Enum(alt_enum_type, 1)));
  EXPECT_FALSE(Value::Enum(enum_type, 1).Equals(Value::Enum(alt_enum_type, 2)));

  // Enums also support LessThan, which should also work with types from
  // different pools.
  EXPECT_FALSE(
      Value::Enum(enum_type, 1).LessThan(Value::Enum(alt_enum_type, 1)));
  EXPECT_TRUE(
      Value::Enum(enum_type, 1).LessThan(Value::Enum(alt_enum_type, 2)));
}

static std::string TestParseInteger(absl::string_view input) {
  Value value;
  if (!Value::ParseInteger(input, &value)) {
    return "ERROR";
  } else {
    return value.FullDebugString();
  }
}

TEST_F(ValueTest, ParseInteger) {
  EXPECT_EQ("ERROR", TestParseInteger(""));
  EXPECT_EQ("ERROR", TestParseInteger(" "));
  EXPECT_EQ("ERROR", TestParseInteger("    "));
  EXPECT_EQ("ERROR", TestParseInteger("abc"));
  EXPECT_EQ("ERROR", TestParseInteger("0x"));
  EXPECT_EQ("ERROR", TestParseInteger("0xaz"));
  EXPECT_EQ("ERROR", TestParseInteger("1."));
  EXPECT_EQ("ERROR", TestParseInteger("1.5"));
  EXPECT_EQ("ERROR", TestParseInteger("123x"));
  EXPECT_EQ("Int64(0)", TestParseInteger("0"));
  EXPECT_EQ("Int64(123)", TestParseInteger("123"));
  EXPECT_EQ("Int64(-123)", TestParseInteger("-123"));

  EXPECT_EQ("Int64(291)", TestParseInteger("0x123"));
  EXPECT_EQ("Int64(-291)", TestParseInteger("-0x123"));
  EXPECT_EQ("Int64(109506)", TestParseInteger("0x1aBc2"));
  EXPECT_EQ("Int64(-109506)", TestParseInteger("-0X1aBc2"));
  EXPECT_EQ("Int64(109506)", TestParseInteger("+0X1aBc2"));
  EXPECT_EQ("ERROR", TestParseInteger("- 0X1aBc2"));
  EXPECT_EQ("ERROR", TestParseInteger("-+0X1aBc2"));

  EXPECT_EQ("Int64(11)", TestParseInteger("011"));
  EXPECT_EQ("Int64(-11)", TestParseInteger("-011"));
  EXPECT_EQ("Int64(11)", TestParseInteger("+011"));
  EXPECT_EQ("ERROR", TestParseInteger("- 011"));

  EXPECT_EQ("Int64(10)", TestParseInteger(" 10"));
  EXPECT_EQ("Int64(10)", TestParseInteger("10 "));
  EXPECT_EQ("Int64(10)", TestParseInteger("    10   "));
  EXPECT_EQ("Int64(-10)", TestParseInteger("   -10   "));
  EXPECT_EQ("ERROR", TestParseInteger("   - 10   "));

  EXPECT_EQ("Int64(10)", TestParseInteger("0XA"));
  EXPECT_EQ("Int64(10)", TestParseInteger("0XA   "));
  EXPECT_EQ("Int64(10)", TestParseInteger(" 0XA"));
  EXPECT_EQ("Int64(-10)", TestParseInteger("-0XA"));
  EXPECT_EQ("Int64(-10)", TestParseInteger("   -0XA   "));
  EXPECT_EQ("ERROR", TestParseInteger("   - 0XA   "));

  EXPECT_EQ("Int64(4294967295)", TestParseInteger("4294967295"));
  EXPECT_EQ("Int64(4294967296)", TestParseInteger("4294967296"));
  EXPECT_EQ("Int64(-4294967296)", TestParseInteger("-4294967296"));

  // Near std::numeric_limits<int64>::max().
  EXPECT_EQ("Int64(9223372036854775807)",
            TestParseInteger("9223372036854775807"));
  EXPECT_EQ("Uint64(9223372036854775808)",
            TestParseInteger("9223372036854775808"));
  EXPECT_EQ("Int64(9223372036854775807)",
            TestParseInteger("0x7FFFFFFFFFFFFFFF"));
  EXPECT_EQ("Uint64(9223372036854775808)",
            TestParseInteger("0x8000000000000000"));
  EXPECT_EQ("Uint64(18446744073709551615)",
            TestParseInteger("0xFFFFFFFFFFFFFFFF"));

  // Near std::numeric_limits<int64>::lowest().
  EXPECT_EQ("Int64(-9223372036854775807)",
            TestParseInteger("-9223372036854775807"));
  EXPECT_EQ("Int64(-9223372036854775808)",
            TestParseInteger("-9223372036854775808"));
  EXPECT_EQ("Int64(-9223372036854775807)",
            TestParseInteger("-0x7FFFFFFFFFFFFFFF"));
  EXPECT_EQ("ERROR", TestParseInteger("-9223372036854775809"));
  // Too many hex digits.
  EXPECT_EQ("ERROR", TestParseInteger("0xFFFFFFFFFFFFFFFABCD"));

  // Near std::numeric_limits<uint64>::max().
  EXPECT_EQ("Uint64(18446744073709551615)",
            TestParseInteger("18446744073709551615"));
  EXPECT_EQ("ERROR", TestParseInteger("18446744073709551616"));
  EXPECT_EQ("Uint64(18446744073709551615)",
            TestParseInteger("0xFFFFFFFFFFFFFFFF"));
  EXPECT_EQ("ERROR", TestParseInteger("0x10000000000000000"));
}

void ValueTest::TestParameterizedValueAfterReleaseOfTypeFactory(
    bool keep_type_alive_while_referenced_from_value) {
  SCOPED_TRACE(absl::StrCat("keep_type_alive_while_referenced_from_value=",
                            keep_type_alive_while_referenced_from_value));

  const internal::TypeStore* store = nullptr;
  Value proto_value;
  {
    Value enum_value, struct_value, array_value;
    {
      TypeFactoryOptions options;
      options.keep_alive_while_referenced_from_value =
          keep_type_alive_while_referenced_from_value;
      TypeFactory type_factory(options);
      store = internal::TypeStoreHelper::GetTypeStore(&type_factory);

      // Proto value
      const ProtoType* proto_type;
      GOOGLESQL_ASSERT_OK(type_factory.MakeProtoType(
          googlesql_test::TestExtraPB::descriptor(), &proto_type));

      googlesql_test::TestExtraPB proto_msg;
      proto_msg.set_int32_val1(5);
      absl::Cord bytes = absl::Cord(proto_msg.SerializeAsString());
      proto_value = Value::Proto(proto_type, bytes);

      // Enum value
      const EnumType* enum_type;
      const google::protobuf::EnumDescriptor* enum_descriptor =
          googlesql_test::TestEnum_descriptor();
      GOOGLESQL_ASSERT_OK(type_factory.MakeEnumType(enum_descriptor, &enum_type));
      enum_value = values::Enum(enum_type, 1);

      // Struct value
      const StructType* struct_type;
      GOOGLESQL_ASSERT_OK(type_factory.MakeStructType(
          {{"f1", proto_type}, {"f2", enum_type}}, &struct_type));
      struct_value = values::Struct(struct_type, {proto_value, enum_value});

      // Array value
      const ArrayType* array_type;
      GOOGLESQL_ASSERT_OK(type_factory.MakeArrayType(struct_type, &array_type));
      array_value = values::Array(array_type, {struct_value});
#ifdef NDEBUG
      if (!keep_type_alive_while_referenced_from_value) {
        ASSERT_EQ(internal::TypeStoreHelper::Test_GetRefCount(store), 1);

        // We don't keep track of references under release mode (NDEBUG) when
        // keep_alive_while_referenced_from_value is disabled. Thus, just
        // emulate an error message.
        ABSL_LOG(FATAL) << "Type factory is released while there are still some "
                      "objects that reference it";
      }
#endif
      // TypeFactory has been destroyed at this point.
    }

    // We expect 7 values currently exists: proto_value, enum_value,
    // struct_value, array_value, 2 field values in struct_value and 1 element
    // in array_value. Since TypeFactory is gone, we should have 7 references.
    EXPECT_EQ(internal::TypeStoreHelper::Test_GetRefCount(store), 7);

    EXPECT_EQ(proto_value.DebugString(), "{int32_val1: 5}");
    EXPECT_EQ(enum_value.DebugString(), "TESTENUM1");
    EXPECT_EQ(struct_value.DebugString(), "{f1:{int32_val1: 5}, f2:TESTENUM1}");
    EXPECT_EQ(array_value.DebugString(),
              "[{f1:{int32_val1: 5}, f2:TESTENUM1}]");
  }

  // Values enum_value, struct_value, array_value have been gone at this point
  // and only proto_value still alive - thus we should have a single reference.
  EXPECT_EQ(internal::TypeStoreHelper::Test_GetRefCount(store), 1);
}

TEST_F(ValueTest, ValueIsLifeAfterTypeFactoryRelease) {
#ifdef NDEBUG
  EXPECT_DEATH(TestParameterizedValueAfterReleaseOfTypeFactory(
                   /*keep_type_alive_while_referenced_from_value=*/false),
               "Type factory is released while there are still some objects "
               "that reference it");
#endif

  TestParameterizedValueAfterReleaseOfTypeFactory(
      /*keep_type_alive_while_referenced_from_value=*/true);
}

TEST_F(ValueTest, PhysicalByteSize) {
  // Coverage messes with the object sizes, so skip it.
  if (std::getenv("BAZEL_CC_COVERAGE_TOOL") != nullptr) return;

  // Null values.
  EXPECT_EQ(sizeof(Value), Value::NullBool().physical_byte_size());
  EXPECT_EQ(sizeof(Value), Value::NullBytes().physical_byte_size());
  EXPECT_EQ(sizeof(Value), Value::NullDate().physical_byte_size());
  EXPECT_EQ(sizeof(Value), Value::NullDatetime().physical_byte_size());
  EXPECT_EQ(sizeof(Value), Value::NullDouble().physical_byte_size());
  EXPECT_EQ(sizeof(Value), Value::NullFloat().physical_byte_size());
  EXPECT_EQ(sizeof(Value), Value::NullInt32().physical_byte_size());
  EXPECT_EQ(sizeof(Value), Value::NullInt64().physical_byte_size());
  EXPECT_EQ(sizeof(Value), Value::NullNumeric().physical_byte_size());
  EXPECT_EQ(sizeof(Value), Value::NullString().physical_byte_size());
  EXPECT_EQ(sizeof(Value), Value::NullTime().physical_byte_size());
  EXPECT_EQ(sizeof(Value), Value::NullTimestamp().physical_byte_size());
  EXPECT_EQ(sizeof(Value), Value::NullUint32().physical_byte_size());
  EXPECT_EQ(sizeof(Value), Value::NullUint64().physical_byte_size());
  EXPECT_EQ(sizeof(Value), Value::NullTokenList().physical_byte_size());

  // Constant sized types.
  auto bool_value = Value::Bool(true);
  auto date_value = Value::Date(1);
  auto int_value = Value::Int32(1);
  EXPECT_EQ(sizeof(Value), bool_value.physical_byte_size());
  EXPECT_EQ(sizeof(Value), date_value.physical_byte_size());
  EXPECT_EQ(sizeof(Value), Value::DatetimeFromPacked64Micros(0X1F7EA704E5181CD)
                               .physical_byte_size());
  EXPECT_EQ(sizeof(Value), Value::Double(1.0).physical_byte_size());
  EXPECT_EQ(sizeof(Value),
            Value::Enum(GetTestEnumType(), 0).physical_byte_size());
  EXPECT_EQ(sizeof(Value), Value::Float(1.0).physical_byte_size());
  EXPECT_EQ(sizeof(Value), int_value.physical_byte_size());
  EXPECT_EQ(sizeof(Value), Value::Int64(1).physical_byte_size());
  EXPECT_EQ(40, Value::Numeric(NumericValue::FromDouble(1.0).value())
                    .physical_byte_size());
  EXPECT_EQ(sizeof(Value),
            Value::Time(TimeValue::FromPacked64Micros(int64_t{0xD38F1E240}))
                .physical_byte_size());
  EXPECT_EQ(40, Value::TimestampFromUnixMicros(1).physical_byte_size());
  EXPECT_EQ(sizeof(Value), Value::Uint32(1).physical_byte_size());
  EXPECT_EQ(sizeof(Value), Value::Uint64(1).physical_byte_size());

  // Variable sized types.
  uint64_t empty_array_size = values::Int64Array({}).physical_byte_size();
  EXPECT_EQ(sizeof(Value) + sizeof(internal::ValueContentOrderedListRef) +
                sizeof(Value::TypedList),
            empty_array_size);
  EXPECT_EQ(empty_array_size + Value::Int64(1).physical_byte_size(),
            values::Int64Array({1}).physical_byte_size());
  EXPECT_EQ(empty_array_size + 3 * Value::Int64(1).physical_byte_size(),
            values::Int64Array({1, 2, 3}).physical_byte_size());

  EXPECT_EQ(sizeof(Value) + sizeof(internal::StringRef),
            Value::Bytes("").physical_byte_size());
  EXPECT_EQ(sizeof(Value) + sizeof(internal::StringRef) + 3 * sizeof(char),
            Value::Bytes("abc").physical_byte_size());
  // Strings should be consistent with bytes.
  EXPECT_EQ(Value::Bytes("").physical_byte_size(),
            Value::String("").physical_byte_size());
  EXPECT_EQ(Value::Bytes("abc").physical_byte_size(),
            Value::String("abc").physical_byte_size());

  // Structs should be consistent with their contents.
  EXPECT_EQ(sizeof(Value) + sizeof(internal::ValueContentOrderedListRef) +
                sizeof(Value::TypedList),
            Struct({}, {}).physical_byte_size());
  EXPECT_EQ(sizeof(Value) + sizeof(internal::ValueContentOrderedListRef) +
                sizeof(Value::TypedList) + bool_value.physical_byte_size() +
                date_value.physical_byte_size(),
            Struct({"b", "d"}, {bool_value, date_value}).physical_byte_size());

  // Ranges should be consistent with their contents.
  uint64_t expected_range_size = sizeof(Value) +
                                 sizeof(internal::ValueContentOrderedListRef) +
                                 sizeof(Value::TypedList) + 2 * sizeof(Value);
  uint64_t expected_timestamp_range_size =
      expected_range_size + sizeof(internal::TimestampPicosRef) * 2;
  uint64_t expected_unbounded_timestamp_range_size =
      expected_range_size + sizeof(internal::TimestampPicosRef);
  const absl::TimeZone utc = absl::UTCTimeZone();

  // Bounded ranges.
  EXPECT_EQ(expected_range_size,
            Range(Value::Date(1), Value::Date(2)).physical_byte_size());
  EXPECT_EQ(expected_range_size,
            Range(Value::Datetime(
                      DatetimeValue::FromYMDHMSAndNanos(1, 2, 3, 4, 5, 6, 7)),
                  Value::Datetime(
                      DatetimeValue::FromYMDHMSAndNanos(2, 2, 3, 4, 5, 6, 7)))
                .physical_byte_size());
  EXPECT_EQ(expected_timestamp_range_size,
            Range(Value::Timestamp(absl::FromCivil(
                      absl::CivilSecond(2020, 01, 01, 10, 00, 00), utc)),
                  Value::Timestamp(absl::FromCivil(
                      absl::CivilSecond(2020, 01, 02, 10, 00, 00), utc)))
                .physical_byte_size());

  // Ranges unbounded on both ends.
  EXPECT_EQ(expected_range_size,
            Range(Value::UnboundedStartDate(), Value::UnboundedEndDate())
                .physical_byte_size());
  EXPECT_EQ(expected_range_size, Range(Value::UnboundedStartDatetime(),
                                       Value::UnboundedEndDatetime())
                                     .physical_byte_size());
  EXPECT_EQ(expected_range_size, Range(Value::UnboundedStartTimestamp(),
                                       Value::UnboundedEndTimestamp())
                                     .physical_byte_size());

  // Unbounded-start ranges.
  EXPECT_EQ(
      expected_range_size,
      Range(Value::UnboundedStartDate(), Value::Date(2)).physical_byte_size());
  EXPECT_EQ(expected_range_size,
            Range(Value::UnboundedStartDatetime(),
                  Value::Datetime(
                      DatetimeValue::FromYMDHMSAndNanos(2, 2, 3, 4, 5, 6, 7)))
                .physical_byte_size());
  EXPECT_EQ(expected_unbounded_timestamp_range_size,
            Range(Value::UnboundedStartTimestamp(),
                  Value::Timestamp(absl::FromCivil(
                      absl::CivilSecond(2020, 01, 02, 10, 00, 00), utc)))
                .physical_byte_size());

  // Unbounded-end ranges.
  EXPECT_EQ(
      expected_range_size,
      Range(Value::Date(1), Value::UnboundedEndDate()).physical_byte_size());
  EXPECT_EQ(expected_range_size,
            Range(Value::Datetime(
                      DatetimeValue::FromYMDHMSAndNanos(1, 2, 3, 4, 5, 6, 7)),
                  Value::UnboundedEndDatetime())
                .physical_byte_size());
  EXPECT_EQ(expected_unbounded_timestamp_range_size,
            Range(Value::Timestamp(absl::FromCivil(
                      absl::CivilSecond(2020, 01, 01, 10, 00, 00), utc)),
                  Value::UnboundedEndTimestamp())
                .physical_byte_size());

  const std::string kLabel = "label";
  const std::string kDefinitionName = "ElementTable";
  const Value p0_value = Value::String("v0");
  const Value p1_value = Value::Int32(1);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value node,
      GraphNode({"graph_name"}, "id", {{"p0", p0_value}, {"p1", p1_value}},
                {kLabel}, kDefinitionName));
  EXPECT_EQ(node.physical_byte_size(),
            absl::c_accumulate(
                node.property_values(),
                sizeof(Value) + sizeof(internal::ValueContentOrderedListRef) +
                    sizeof(Value::GraphElementValue) +
                    sizeof(Value::TypedList) + node.GetIdentifier().length() +
                    kLabel.length() + kDefinitionName.length() +
                    sizeof(std::vector<Value>)
                ,
                [](int size, const auto& property) {
                  return size + property.physical_byte_size();
                }));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value edge,
      GraphEdge({"graph_nanme"}, "id", {{"p0", p0_value}, {"p1", p1_value}},
                {kLabel}, kDefinitionName, "src_node_id", "dst_node_id"));
  EXPECT_EQ(edge.physical_byte_size(),
            absl::c_accumulate(
                edge.property_values(),
                sizeof(Value) + sizeof(internal::ValueContentOrderedListRef) +
                    sizeof(Value::GraphElementValue) +
                    sizeof(Value::TypedList) + edge.GetIdentifier().length() +
                    edge.GetSourceNodeIdentifier().length() +
                    edge.GetDestNodeIdentifier().length() + kLabel.length() +
                    kDefinitionName.length() +
                    2 * sizeof(std::vector<Value>)
                ,
                [](int size, const auto& property) {
                  return size + property.physical_byte_size();
                }));

  // Map type
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* map_type,
                            MakeMapType(Int64Type(), Int64Type()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value empty_map, Value::MakeMap(map_type, {}));
  EXPECT_EQ(sizeof(Value) + sizeof(internal::ValueContentMapRef) +
                sizeof(Value::TypedMap),
            empty_map.physical_byte_size());

  const Value map_string = Value::String("foo");
  const Value map_int64 = Value::Int64(1);
  EXPECT_EQ(sizeof(Value) + sizeof(internal::ValueContentMapRef) +
                sizeof(Value::TypedMap) + map_string.physical_byte_size() +
                map_int64.physical_byte_size(),
            Map({std::make_pair(map_string, map_int64)}).physical_byte_size());
}

// Roundtrips Value through ValueProto and back.
static void SerializeDeserialize(const Value& value) {
  ValueProto value_proto;
  GOOGLESQL_ASSERT_OK(value.Serialize(&value_proto)) << value.DebugString();
  auto status_or_value = Value::Deserialize(value_proto, value.type());
  GOOGLESQL_ASSERT_OK(status_or_value.status());
  EXPECT_EQ(value, status_or_value.value()) << "\nSerialized value:\n"
                                            << value_proto.DebugString();
}

// Roundtrips ValueProto through Value and back.
static void DeserializeSerialize(const std::string& value_proto_str,
                                 const Type* type) {
  ValueProto value_proto;
  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(value_proto_str, &value_proto))
      << value_proto_str;
  auto status_or_value = Value::Deserialize(value_proto, type);
  GOOGLESQL_ASSERT_OK(status_or_value.status()) << value_proto.DebugString();
  ValueProto roundtrip_value_proto;
  GOOGLESQL_ASSERT_OK(status_or_value.value().Serialize(&roundtrip_value_proto))
      << value_proto.DebugString();
  EXPECT_THAT(value_proto, testing::EqualsProto(roundtrip_value_proto));
}

TEST_F(ValueTest, Serialize) {
  // Scalar types.
  SerializeDeserialize(NullInt32());
  SerializeDeserialize(Int32(-100));

  SerializeDeserialize(Null(Int32ArrayType()));
  SerializeDeserialize(EmptyArray(Int32ArrayType()));
  SerializeDeserialize(
      Array({NullInt32(), Int32(std::numeric_limits<int32_t>::lowest()),
             Int32(std::numeric_limits<int32_t>::max()), Int32(0)}));

  SerializeDeserialize(NullInt64());
  SerializeDeserialize(Int64(-9876543210));

  SerializeDeserialize(Null(Int64ArrayType()));
  SerializeDeserialize(EmptyArray(Int64ArrayType()));
  SerializeDeserialize(
      Array({NullInt64(), Int64(std::numeric_limits<int64_t>::lowest()),
             Int64(std::numeric_limits<int64_t>::max()), Int64(0)}));

  SerializeDeserialize(NullUint32());
  SerializeDeserialize(Uint32(42));

  SerializeDeserialize(Null(Uint32ArrayType()));
  SerializeDeserialize(EmptyArray(Uint32ArrayType()));
  SerializeDeserialize(
      Array({NullUint32(), Uint32(std::numeric_limits<int32_t>::max()),
             Uint32(std::numeric_limits<uint32_t>::max()), Uint32(0)}));

  SerializeDeserialize(NullUint64());
  SerializeDeserialize(Uint64(9876543210));

  SerializeDeserialize(Null(Uint64ArrayType()));
  SerializeDeserialize(EmptyArray(Uint64ArrayType()));
  SerializeDeserialize(
      Array({NullUint64(), Uint64(std::numeric_limits<int64_t>::max()),
             Uint64(std::numeric_limits<uint64_t>::max()), Uint64(0)}));

  SerializeDeserialize(NullBool());
  SerializeDeserialize(Bool(true));
  SerializeDeserialize(Bool(false));

  SerializeDeserialize(Null(BoolArrayType()));
  SerializeDeserialize(EmptyArray(BoolArrayType()));
  SerializeDeserialize(Array({Bool(false), Bool(true), NullBool()}));

  SerializeDeserialize(NullFloat());
  SerializeDeserialize(Float(3.1415));

  SerializeDeserialize(Null(FloatArrayType()));
  SerializeDeserialize(EmptyArray(FloatArrayType()));
  SerializeDeserialize(
      Array({Float(std::numeric_limits<float>::min()),
             Float(std::numeric_limits<float>::max()),
             Float(std::numeric_limits<float>::lowest()),
             Float(std::numeric_limits<float>::round_error()),
             Float(std::numeric_limits<float>::epsilon()),
             Float(std::numeric_limits<float>::infinity()),
             Float(-std::numeric_limits<float>::infinity()),
             Float(std::numeric_limits<float>::denorm_min()), NullFloat()}));

  SerializeDeserialize(NullDouble());
  SerializeDeserialize(Double(2.71));

  SerializeDeserialize(Null(DoubleArrayType()));
  SerializeDeserialize(EmptyArray(DoubleArrayType()));
  SerializeDeserialize(
      Array({Double(std::numeric_limits<double>::min()),
             Double(std::numeric_limits<double>::max()),
             Double(std::numeric_limits<double>::lowest()),
             Double(std::numeric_limits<double>::round_error()),
             Double(std::numeric_limits<double>::epsilon()),
             Double(std::numeric_limits<double>::infinity()),
             Double(-std::numeric_limits<double>::infinity()),
             Double(std::numeric_limits<double>::denorm_min()), NullDouble()}));

  SerializeDeserialize(Value::NullNumeric());
  SerializeDeserialize(Value::Numeric(NumericValue(int64_t{1})));

  SerializeDeserialize(EmptyArray(types::NumericArrayType()));
  SerializeDeserialize(
      Array({Value::Numeric(NumericValue()),
             Value::Numeric(NumericValue(int64_t{-1})),
             Value::Numeric(NumericValue(int64_t{1})),
             Value::Numeric(NumericValue::MinValue()),
             Value::Numeric(NumericValue::MaxValue()), Value::NullNumeric()}));

  SerializeDeserialize(Value::NullBigNumeric());
  SerializeDeserialize(Value::BigNumeric(BigNumericValue()));
  SerializeDeserialize(Value::BigNumeric(BigNumericValue(int64_t{1})));
  SerializeDeserialize(Value::BigNumeric(BigNumericValue(int64_t{-1})));
  SerializeDeserialize(Value::BigNumeric(BigNumericValue::MinValue()));
  SerializeDeserialize(Value::BigNumeric(BigNumericValue::MaxValue()));

  SerializeDeserialize(EmptyArray(types::BigNumericArrayType()));
  SerializeDeserialize(Array({Value::BigNumeric(BigNumericValue()),
                              Value::BigNumeric(BigNumericValue(int64_t{-1})),
                              Value::BigNumeric(BigNumericValue(int64_t{1})),
                              Value::BigNumeric(BigNumericValue::MinValue()),
                              Value::BigNumeric(BigNumericValue::MaxValue()),
                              Value::NullBigNumeric()}));

  SerializeDeserialize(Value::NullJson());
  SerializeDeserialize(Value::Json(JSONValue()));
  SerializeDeserialize(Value::Json(JSONValue(int64_t{1})));
  SerializeDeserialize(Value::Json(JSONValue(int64_t{-1})));
  SerializeDeserialize(Value::UnvalidatedJsonString("value"));

  SerializeDeserialize(EmptyArray(types::JsonArrayType()));
  SerializeDeserialize(
      Array({Value::NullJson(), Value::Json(JSONValue(int64_t{-1})),
             Value::Json(JSONValue(int64_t{1})),
             Value::UnvalidatedJsonString("value")}));

  SerializeDeserialize(NullString());
  SerializeDeserialize(String("Hello, world!"));

  SerializeDeserialize(Null(StringArrayType()));
  SerializeDeserialize(EmptyArray(StringArrayType()));
  SerializeDeserialize(Array({String("привет!"), String("!שָׁלוֹם")}));

  SerializeDeserialize(NullBytes());
  SerializeDeserialize(Bytes("\000\001\002"));

  SerializeDeserialize(Null(BytesArrayType()));
  SerializeDeserialize(EmptyArray(BytesArrayType()));
  SerializeDeserialize(Array({NullBytes(), Bytes("!@#$%\n")}));

  SerializeDeserialize(NullDate());
  SerializeDeserialize(Date(10000));

  SerializeDeserialize(Null(types::DateArrayType()));
  SerializeDeserialize(EmptyArray(types::DateArrayType()));
  SerializeDeserialize(Array({NullDate(), Date(googlesql::types::kDateMax),
                              Date(googlesql::types::kDateMin)}));

  SerializeDeserialize(NullTimestamp());
  SerializeDeserialize(TimestampFromUnixMicros(-5364662400000000));

  SerializeDeserialize(Null(types::TimestampArrayType()));
  SerializeDeserialize(EmptyArray(types::TimestampArrayType()));
  SerializeDeserialize(
      Array({NullTimestamp(),
             TimestampFromUnixMicros(googlesql::types::kTimestampMin),
             TimestampFromUnixMicros(googlesql::types::kTimestampMax)}));

  SerializeDeserialize(NullDatetime());
  SerializeDeserialize(Datetime(
      DatetimeValue::FromYMDHMSAndMicros(2010, 8, 7, 15, 26, 31, 712)));
  SerializeDeserialize(Datetime(
      DatetimeValue::FromYMDHMSAndNanos(1871, 11, 21, 9, 5, 4, 192837)));

  SerializeDeserialize(Null(types::DatetimeArrayType()));
  SerializeDeserialize(EmptyArray(types::DatetimeArrayType()));
  SerializeDeserialize(
      Array({NullDatetime(), Value::Datetime(DatetimeValue::FromYMDHMSAndNanos(
                                 1, 2, 3, 4, 5, 6, 7))}));

  SerializeDeserialize(NullTime());
  SerializeDeserialize(Time(TimeValue::FromHMSAndMicros(9, 18, 23, 501)));
  SerializeDeserialize(Time(TimeValue::FromHMSAndNanos(10, 1, 59, 372810)));

  SerializeDeserialize(Null(types::DatetimeArrayType()));
  SerializeDeserialize(EmptyArray(types::DatetimeArrayType()));
  SerializeDeserialize(
      Array({NullDatetime(), Value::Datetime(DatetimeValue::FromYMDHMSAndNanos(
                                 1, 2, 3, 4, 5, 6, 7))}));

  // Interval
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      IntervalValue interval_min,
      IntervalValue::FromMonthsDaysNanos(IntervalValue::kMinMonths,
                                         IntervalValue::kMinDays,
                                         IntervalValue::kMinNanos));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      IntervalValue interval_max,
      IntervalValue::FromMonthsDaysNanos(IntervalValue::kMaxMonths,
                                         IntervalValue::kMaxDays,
                                         IntervalValue::kMaxNanos));

  SerializeDeserialize(Value::NullInterval());
  SerializeDeserialize(Value::Interval(Nanos(0)));
  SerializeDeserialize(Value::Interval(Nanos(1001)));
  SerializeDeserialize(Value::Interval(Micros(-12)));
  SerializeDeserialize(Value::Interval(Days(370)));
  SerializeDeserialize(Value::Interval(Months(-121)));
  SerializeDeserialize(Value::Interval(interval_min));
  SerializeDeserialize(Value::Interval(interval_max));

  SerializeDeserialize(EmptyArray(types::IntervalArrayType()));
  SerializeDeserialize(
      Array({Value::NullInterval(), Value::Interval(Months(0)),
             Value::Interval(Days(-5)), Value::Interval(Micros(123456789)),
             Value::Interval(interval_min), Value::Interval(interval_max)}));

  // TokenList
  SerializeDeserialize(Value::NullTokenList());
  SerializeDeserialize(TokenListFromToken("test"));
  SerializeDeserialize(EmptyArray(types::TokenListArrayType()));
  SerializeDeserialize(
      Array({Value::NullTokenList(), TokenListFromToken("test")}));

  // Enum.
  const EnumType* enum_type = GetTestEnumType();
  SerializeDeserialize(Null(enum_type));
  SerializeDeserialize(Enum(enum_type, 1));

  // Proto3 open enum with known value.
  const EnumType* open_enum_type = GetTestProto3EnumType();
  SerializeDeserialize(Enum(open_enum_type, 2));

  const ArrayType* array_enum_type = GetTestArrayEnumType();
  SerializeDeserialize(Null(array_enum_type));
  SerializeDeserialize(EmptyArray(array_enum_type));
  SerializeDeserialize(
      Value::Array(array_enum_type,
                   {Null(enum_type), Enum(enum_type, 0), Enum(enum_type, 1)}));

  // Proto
  const ProtoType* proto_type = GetTestProtoType();

  googlesql_test::KitchenSinkPB ks;
  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(R"(
      int64_key_1: 1
      int64_key_2: 2
      )",
                                             &ks));

  absl::Cord bytes;
  ABSL_CHECK(ks.SerializePartialToCord(&bytes));
  absl::Cord ks_serialized = bytes;

  SerializeDeserialize(Null(proto_type));
  SerializeDeserialize(Proto(proto_type, ks_serialized));
  SerializeDeserialize(Array({Proto(proto_type, ks_serialized)}));

  // Empty structs
  SerializeDeserialize(Null(EmptyStructType()));
  SerializeDeserialize(Struct({}));

  // Array of empty structs.
  SerializeDeserialize(Array({Null(EmptyStructType()), Struct({})}));

  // Structs
  const StructType* simple_struct_type =
      MakeStructType({{"a", Int64ArrayType()}, {"t", TimestampType()}});

  const StructType* struct_type = MakeStructType({{"a", Int32ArrayType()},
                                                  {"b", BytesType()},
                                                  {"d", DateType()},
                                                  {"e", EmptyStructType()},
                                                  {"p", GetTestProtoType()},
                                                  {"s", simple_struct_type}});

  SerializeDeserialize(Null(struct_type));
  SerializeDeserialize(Struct({{"a", Null(Int32ArrayType())},
                               {"b", NullBytes()},
                               {"d", NullDate()},
                               {"e", Null(EmptyStructType())},
                               {"p", Null(GetTestProtoType())},
                               {"s", Null(simple_struct_type)}}));
  SerializeDeserialize(Struct(
      {{"a", Int32Array({0, 1, 2, 3, 4, 5})},
       {"b", Bytes("\001\000\002\005")},
       {"d", Date(365)},
       {"e", Struct({})},
       {"p", Null(GetTestProtoType())},
       {"s", Struct({{"a", Array({NullInt64(), Int64(999)})},
                     {"t", TimestampFromUnixMicros(1430855635016138)}})}}));
  SerializeDeserialize(Array({Struct(
      {{"a", Int32Array({0, 1, 2, 3, 4, 5})},
       {"b", Bytes("\001\000\002\005")},
       {"d", Date(365)},
       {"e", Struct({})},
       {"p", Null(GetTestProtoType())},
       {"s", Struct({{"a", Array({NullInt64(), Int64(999)})},
                     {"t", TimestampFromUnixMicros(1430855635016138)}})}})}));
}

TEST_F(ValueTest, Serialize_NullRangesSucceed) {
  SerializeDeserialize(Null(types::DateRangeType()));
  SerializeDeserialize(Null(types::DatetimeRangeType()));
  SerializeDeserialize(Null(types::TimestampRangeType()));
}

TEST_F(ValueTest, Serialize_BoundedRangesSucceed) {
  SerializeDeserialize(Range(Date(1), Date(2)));
  SerializeDeserialize(
      Range(Datetime(DatetimeValue::FromYMDHMSAndNanos(1, 2, 3, 4, 5, 6, 7)),
            Datetime(DatetimeValue::FromYMDHMSAndNanos(7, 6, 5, 4, 3, 2, 1))));
  SerializeDeserialize(
      Range(TimestampFromUnixMicros(1), TimestampFromUnixMicros(2)));
}

TEST_F(ValueTest, Serialize_UnboundedStartRangesSucceed) {
  SerializeDeserialize(Range(Value::UnboundedStartDate(), Date(2)));
  SerializeDeserialize(
      Range(Value::UnboundedStartDatetime(),
            Datetime(DatetimeValue::FromYMDHMSAndNanos(7, 6, 5, 4, 3, 2, 1))));
  SerializeDeserialize(
      Range(Value::UnboundedStartTimestamp(), TimestampFromUnixMicros(2)));
}

TEST_F(ValueTest, Serialize_UnboundedEndRangesSucceed) {
  SerializeDeserialize(Range(Date(1), Value::UnboundedEndDate()));
  SerializeDeserialize(
      Range(Datetime(DatetimeValue::FromYMDHMSAndNanos(1, 2, 3, 4, 5, 6, 7)),
            Value::UnboundedEndDatetime()));
  SerializeDeserialize(
      Range(TimestampFromUnixMicros(1), Value::UnboundedEndTimestamp()));
}

TEST_F(ValueTest, Serialize_UnboundedStartAndEndRangesSucceed) {
  SerializeDeserialize(
      Range(Value::UnboundedStartDate(), Value::UnboundedEndDate()));
  SerializeDeserialize(
      Range(Value::UnboundedStartDatetime(), Value::UnboundedEndDatetime()));
  SerializeDeserialize(
      Range(Value::UnboundedStartTimestamp(), Value::UnboundedEndTimestamp()));
}

TEST_F(ValueTest, Serialize_ArraysOfRangesSucceed) {
  const ArrayType* array_date_range_type =
      MakeArrayType(types::DateRangeType());
  SerializeDeserialize(EmptyArray(array_date_range_type));
  SerializeDeserialize(Array({Range(Date(1), Date(2))}));
  SerializeDeserialize(
      Array({Null(types::DateRangeType()), Range(Date(1), Date(2)),
             Range(Date(3), Date(4))}));
}

TEST_F(ValueTest, Serialize_StructsOfRangesSucceed) {
  const StructType* struct_type =
      MakeStructType({{"a", types::DateRangeType()},
                      {"b", types::DatetimeRangeType()},
                      {"c", types::TimestampRangeType()}});
  SerializeDeserialize(Null(struct_type));
  SerializeDeserialize(Struct({{"a", Null(types::DateRangeType())},
                               {"b", Null(types::DatetimeRangeType())},
                               {"c", Null(types::TimestampRangeType())}}));
  SerializeDeserialize(Struct({
      {"a", Range(Date(1), Date(2))},
      {"b",
       Range(Value::UnboundedStartDatetime(),
             Datetime(DatetimeValue::FromYMDHMSAndNanos(1, 2, 3, 4, 5, 6, 7)))},
      {"c", Range(TimestampFromUnixMicros(1), Value::UnboundedEndTimestamp())},
  }));
}

TEST_F(ValueTest, Serialize_NullMapSucceeds) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* map_string_type,
      MakeMapType(types::StringType(), types::StringType()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* map_struct_type,
      MakeMapType(types::StringType(),
                  MakeStructType({{"a", types::StringType()},
                                  {"b", types::Int64Type()}})));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* map_nested_type,
                            MakeMapType(types::StringType(), map_struct_type));

  SerializeDeserialize(Null(map_string_type));
  SerializeDeserialize(Null(map_struct_type));
  SerializeDeserialize(Null(map_nested_type));
}

TEST_F(ValueTest, Serialize_EmptyMapSucceeds) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* map_string_type,
      MakeMapType(types::StringType(), types::StringType()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* map_struct_type,
      MakeMapType(types::StringType(),
                  MakeStructType({{"a", types::StringType()},
                                  {"b", types::Int64Type()}})));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* map_nested_type,
                            MakeMapType(types::StringType(), map_struct_type));

  SerializeDeserialize(Value::MakeMap(map_string_type, {}).value());
  SerializeDeserialize(Value::MakeMap(map_struct_type, {}).value());
  SerializeDeserialize(Value::MakeMap(map_nested_type, {}).value());
}

TEST_F(ValueTest, Serialize_MapSucceeds) {
  SerializeDeserialize(test_values::Map({{"a", "b"}}));
  SerializeDeserialize(
      test_values::Map({{Value::NullBool(), Value::NullString()}}));
  SerializeDeserialize(test_values::Map(
      {{Value::String("foo"),
        test_values::Array({Value::String("bar"), Value::String("baz")})}}));
  SerializeDeserialize(test_values::Map(
      {{Value::String("foo"),
        test_values::Map({{Value::Bool(true),
                           test_values::Array({Value::String("bar"),
                                               Value::String("baz")})}})}}));

  googlesql_test::KitchenSinkPB kitchen_sink_proto;
  kitchen_sink_proto.set_int64_key_1(1);
  kitchen_sink_proto.set_int64_key_2(2);

  SerializeDeserialize(test_values::Map(
      {{Value::String("foo"),
        values::Proto(GetTestProtoType(), kitchen_sink_proto)}}));
}

TEST_F(ValueTest, Deserialize) {
  // Scalars.
  DeserializeSerialize("", Int32Type());
  DeserializeSerialize("int32_value: 0", Int32Type());

  DeserializeSerialize("", Int32ArrayType());
  DeserializeSerialize("array_value: <>", Int32ArrayType());
  DeserializeSerialize("array_value: <element: <>>", Int32ArrayType());
  DeserializeSerialize(R"(
    array_value: <
      element: <int32_value: -1>
      element: <>
      element: <int32_value: 1>>
    )",
                       Int32ArrayType());

  // Empty struct.
  DeserializeSerialize("", EmptyStructType());
  DeserializeSerialize("struct_value: <>", EmptyStructType());

  // Array of empty structs.
  const Type* array_empty_struct_type = MakeArrayType(EmptyStructType());
  DeserializeSerialize("", array_empty_struct_type);
  DeserializeSerialize("array_value: <>", array_empty_struct_type);
  DeserializeSerialize("array_value: <element: <>>", array_empty_struct_type);
  DeserializeSerialize("array_value: <element: <struct_value: <>>>",
                       array_empty_struct_type);
  DeserializeSerialize(R"(
    array_value: <
      element: <struct_value: <>>
      element: <struct_value: <>>>
    )",
                       array_empty_struct_type);

  // Structs.
  const StructType* simple_struct_type =
      MakeStructType({{"a", Int64ArrayType()}, {"t", TimestampType()}});

  const StructType* struct_type = MakeStructType({{"a", Int32ArrayType()},
                                                  {"b", BytesType()},
                                                  {"d", DateType()},
                                                  {"e", EmptyStructType()},
                                                  {"p", GetTestProtoType()},
                                                  {"s", simple_struct_type}});

  DeserializeSerialize("", struct_type);
  DeserializeSerialize(R"(
    struct_value: <
      field: <>  # a
      field: <>  # b
      field: <>  # d
      field: <>  # e
      field: <>  # p
      field: <>  # s
    >
    )",
                       struct_type);

  DeserializeSerialize(R"(
    struct_value: <
      field: <array_value: <>>      # a
      field: <bytes_value: 'xxx'>   # b
      field: <date_value: 1000>     # d
      field: <struct_value: <>>     # e
      field: <>                     # p
      field:                        # s
        <struct_value <
          field: <                  # s.a
            array_value: <
              element: <int64_value: 1>
              element: <>
              element: <int64_value: 2>
            >
          >
          field:                    # s.t
            <timestamp_value: <seconds: 946684800>>
        >
      >
    >
    )",
                       struct_type);

  ValueProto value_proto;
  absl::StatusOr<Value> status_or_value;

  // Timestamp value.
  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString("timestamp_value: <seconds: 5>",
                                             &value_proto));
  status_or_value = Value::Deserialize(value_proto, TimestampType());
  GOOGLESQL_EXPECT_OK(status_or_value.status());
  EXPECT_EQ(5000000, status_or_value.value().ToUnixMicros());

  // Invalid values for DATE/TIMESTAMP
  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(
      absl::StrCat("date_value: ", googlesql::types::kDateMin - 1),
      &value_proto));
  status_or_value = Value::Deserialize(value_proto, DateType());
  EXPECT_THAT(status_or_value, StatusIs(absl::StatusCode::kOutOfRange));

  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(
      absl::StrCat("date_value: ", googlesql::types::kDateMax + 1),
      &value_proto));
  status_or_value = Value::Deserialize(value_proto, DateType());
  EXPECT_THAT(status_or_value, StatusIs(absl::StatusCode::kOutOfRange));

  const int64_t kTimestampSecondsMin =
      googlesql::types::kTimestampMin / 1000000;
  const int64_t kTimestampSecondsMax =
      googlesql::types::kTimestampMax / 1000000;
  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(
      absl::StrCat("timestamp_value: <seconds: ", kTimestampSecondsMin - 1,
                   " nanos: 999999999>"),
      &value_proto));
  status_or_value = Value::Deserialize(value_proto, TimestampType());
  EXPECT_THAT(status_or_value, StatusIs(absl::StatusCode::kOutOfRange));

  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(
      absl::StrCat("timestamp_value: <seconds: ", kTimestampSecondsMax + 1,
                   ">"),
      &value_proto));
  status_or_value = Value::Deserialize(value_proto, TimestampType());
  EXPECT_THAT(status_or_value, StatusIs(absl::StatusCode::kOutOfRange));

  // Invalid ENUM value.
  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString("enum_value: -10", &value_proto));
  status_or_value = Value::Deserialize(value_proto, GetTestEnumType());
  EXPECT_THAT(status_or_value, StatusIs(absl::StatusCode::kOutOfRange));

  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString("enum_value: 100", &value_proto));
  status_or_value = Value::Deserialize(value_proto, GetTestEnumType());
  EXPECT_THAT(status_or_value, StatusIs(absl::StatusCode::kOutOfRange));

  // Type mismatch errors.
  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString("int32_value: 1", &value_proto));
  status_or_value = Value::Deserialize(value_proto, Int64Type());
  EXPECT_THAT(status_or_value, StatusIs(absl::StatusCode::kInternal));
  status_or_value = Value::Deserialize(value_proto, DateType());
  EXPECT_THAT(status_or_value, StatusIs(absl::StatusCode::kInternal));
  status_or_value = Value::Deserialize(value_proto, GetTestEnumType());
  EXPECT_THAT(status_or_value, StatusIs(absl::StatusCode::kInternal));

  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(R"(
    array_value: <
      element: <uint32_value: 1>
      element: <int32_value: 1>>  # wrong type!
    )",
                                             &value_proto));
  status_or_value = Value::Deserialize(value_proto, Uint64ArrayType());
  EXPECT_THAT(status_or_value, StatusIs(absl::StatusCode::kInternal));

  // simple_struct_type: STRUCT<ARRAY<INT64>> a, TIMESTAMP t>
  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(R"(
    struct_value: <
      field: <array_value: <element: <int32_value: 1>>>  # wrong type!
      field: <timestamp_value: <seconds: 935573798>>
    >
    )",
                                             &value_proto));
  status_or_value = Value::Deserialize(value_proto, simple_struct_type);
  EXPECT_THAT(status_or_value, StatusIs(absl::StatusCode::kInternal));

  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(R"(
    struct_value: <
      field: <array_value: <element: <int64_value: 1>>>
      field: <int64_value: 935573798000000>  # wrong type!
    >
    )",
                                             &value_proto));
  status_or_value = Value::Deserialize(value_proto, simple_struct_type);
  EXPECT_THAT(status_or_value, StatusIs(absl::StatusCode::kInternal));

  // Mismatch in number of fields in struct.
  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(R"(
    struct_value: <
      field: <timestamp_value: <seconds: 935573798>>
    >
    )",
                                             &value_proto));
  status_or_value = Value::Deserialize(value_proto, simple_struct_type);
  EXPECT_THAT(status_or_value, StatusIs(absl::StatusCode::kInternal));
}

TEST_F(ValueTest, Deserialize_NullRangesSucceed) {
  DeserializeSerialize("", types::DateRangeType());
  DeserializeSerialize("", types::DatetimeRangeType());
  DeserializeSerialize("", types::TimestampRangeType());
}

TEST_F(ValueTest, Deserialize_BoundedRangesSucceed) {
  DeserializeSerialize(R"(
    range_value: <
      start: <date_value: 1>,
      end: <date_value: 2>
    >)",
                       types::DateRangeType());
  DeserializeSerialize(R"(
    range_value: <
      start: <datetime_value: <bit_field_datetime_seconds: 111111111111
                               nanos: 0>>,
      end: <datetime_value: <bit_field_datetime_seconds: 222222222222
                             nanos: 0>>
    >)",
                       types::DatetimeRangeType());
  DeserializeSerialize(R"(
    range_value: <
      start: <timestamp_value: <seconds: 1>>,
      end: <timestamp_value: <seconds: 2>>
    >)",
                       types::TimestampRangeType());
}

TEST_F(ValueTest, Deserialize_UnboundedStartRangesSucceed) {
  DeserializeSerialize(R"(
    range_value: <
      start: <>
      end: <date_value: 2>
    >)",
                       types::DateRangeType());
  DeserializeSerialize(R"(
    range_value: <
      start: <>,
      end: <datetime_value: <bit_field_datetime_seconds: 222222222222
                             nanos: 0>>
    >)",
                       types::DatetimeRangeType());
  DeserializeSerialize(R"(
    range_value: <
      start: <>,
      end: <timestamp_value: <seconds: 2>>
    >)",
                       types::TimestampRangeType());
}

TEST_F(ValueTest, Deserialize_UnboundedEndRangesSucceed) {
  DeserializeSerialize(R"(
    range_value: <
      start: <date_value: 1>
      end: <>
    >)",
                       types::DateRangeType());
  DeserializeSerialize(R"(
    range_value: <
      start: <datetime_value: <bit_field_datetime_seconds: 111111111111
                               nanos: 0>>,
      end: <>
    >)",
                       types::DatetimeRangeType());
  DeserializeSerialize(R"(
    range_value: <
      start: <timestamp_value: <seconds: 1>>,
      end: <>
    >)",
                       types::TimestampRangeType());
}

TEST_F(ValueTest, Deserialize_UnboundedStartAndEndRangesSucceed) {
  DeserializeSerialize(R"(
    range_value: <
      start: <>
      end: <>
    >)",
                       types::DateRangeType());
  DeserializeSerialize(R"(
    range_value: <
      start: <>,
      end: <>
    >)",
                       types::DatetimeRangeType());
  DeserializeSerialize(R"(
    range_value: <
      start: <>,
      end: <>
    >)",
                       types::TimestampRangeType());
}

TEST_F(ValueTest, Deserialize_ArraysOfRangesSucceed) {
  const ArrayType* array_date_range_type =
      MakeArrayType(types::DateRangeType());
  DeserializeSerialize(R"(
    array_value: <
      element: <>
      element: <range_value: <start: <>              end: <> >>
      element: <range_value: <start: <date_value: 1> end: <date_value: 2>>>
      element: <range_value: <start: <>              end: <date_value: 2>>>
      element: <range_value: <start: <date_value: 1> end: <>>>
    >)",
                       array_date_range_type);
  const ArrayType* array_datetime_range_type =
      MakeArrayType(types::DatetimeRangeType());
  DeserializeSerialize(R"(
    array_value: <
      element: <>
      element: <range_value: <start: <> end: <> >>
      element: <range_value: <
        start: <datetime_value: <bit_field_datetime_seconds: 111111111111
                                 nanos: 0>>
        end: <datetime_value: <bit_field_datetime_seconds: 222222222222
                               nanos: 0>>
      >>
      element: <range_value: <
        start: <>
        end: <datetime_value: <bit_field_datetime_seconds: 222222222222
                               nanos: 0>>
      >>
      element: <range_value: <
        start: <datetime_value: <bit_field_datetime_seconds: 111111111111
                                 nanos: 0>>
        end: <>
      >>
    >)",
                       array_datetime_range_type);
  const ArrayType* array_timestamp_range_type =
      MakeArrayType(types::TimestampRangeType());
  DeserializeSerialize(R"(
    array_value: <
      element: <>
      element: <range_value: <start: <> end: <> >>
      element: <range_value: <
        start: <timestamp_value: <seconds: 1>>
        end: <timestamp_value: <seconds: 2>>
      >>
      element: <range_value: <
        start: <>
        end: <timestamp_value: <seconds: 2>>
      >>
      element: <range_value: <
        start: <timestamp_value: <seconds: 1>>
        end: <>
      >>
    >)",
                       array_timestamp_range_type);
}

TEST_F(ValueTest, Deserialize_StructsOfRangesSucceed) {
  const StructType* struct_range_type =
      MakeStructType({{"a", types::DateRangeType()},
                      {"b", types::DatetimeRangeType()},
                      {"c", types::TimestampRangeType()}});
  DeserializeSerialize(R"(
    struct_value: <
      field: <>  # a
      field: <>  # b
      field: <>  # c
    >)",
                       struct_range_type);
  DeserializeSerialize(R"(
    struct_value: <
      field: <range_value: <start: <> end: <> >>  # a
      field: <range_value: <start: <> end: <> >>  # b
      field: <range_value: <start: <> end: <> >>  # c
    >)",
                       struct_range_type);
  DeserializeSerialize(R"(
    struct_value: <
      field: <range_value: <start: <date_value: 1> end: <> >>  # a
      field: <range_value: <  # b
        start: <>
        end: <datetime_value: <bit_field_datetime_seconds: 222222222222
                               nanos: 0>> >>
      field: <range_value: <  # c
        start: <timestamp_value: <seconds: 1>>
        end: <timestamp_value: <seconds: 2>>
      >>
    >)",
                       struct_range_type);
}

class DateRangesWithInvalidValues : public ::testing::TestWithParam<int64_t> {};

INSTANTIATE_TEST_SUITE_P(Deserialize, DateRangesWithInvalidValues,
                         ::testing::Values(googlesql::types::kDateMin - 1,
                                           googlesql::types::kDateMax + 1));

TEST_P(DateRangesWithInvalidValues, DateRangesWithOutOfRangeValuesFail) {
  ValueProto value_proto;
  absl::StatusOr<Value> status_or_value;

  // Invalid (out of range) DATE elements.
  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(absl::StrFormat(R"(
        range_value: <
          start: <date_value: %d>
          end: <>
        >)",
                                                             GetParam()),
                                             &value_proto));
  status_or_value = Value::Deserialize(value_proto, types::DateRangeType());
  EXPECT_THAT(status_or_value, StatusIs(absl::StatusCode::kOutOfRange));

  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(absl::StrFormat(R"(
        range_value: <
          start: <>
          end: <date_value: %d>
        >)",
                                                             GetParam()),
                                             &value_proto));
  status_or_value = Value::Deserialize(value_proto, types::DateRangeType());
  EXPECT_THAT(status_or_value, StatusIs(absl::StatusCode::kOutOfRange));
}

class DatetimeRangesWithInvalidValues
    : public ::testing::TestWithParam<int64_t> {};

INSTANTIATE_TEST_SUITE_P(
    Deserialize, DatetimeRangesWithInvalidValues,
    ::testing::Values(
        // 0001-01-00 00:00:00.000000
        DatetimeValue::FromYMDHMSAndNanos(0001, 01, 00, 00, 00, 00, 0)
            .Packed64DatetimeSeconds(),
        // 9999-12-31 23:59:60.000000
        DatetimeValue::FromYMDHMSAndNanos(9999, 12, 31, 23, 59, 60, 0)
            .Packed64DatetimeSeconds()));

TEST_P(DatetimeRangesWithInvalidValues,
       DatetimeRangesWithOutOfRangeValuesFail) {
  ValueProto value_proto;
  absl::StatusOr<Value> status_or_value;

  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(absl::StrFormat(R"(
        range_value: <
          start: <datetime_value: <bit_field_datetime_seconds: %d
                                   nanos: 0>>
          end: <>
        >)",
                                                             GetParam()),
                                             &value_proto));
  status_or_value = Value::Deserialize(value_proto, types::DatetimeRangeType());
  EXPECT_THAT(status_or_value, StatusIs(absl::StatusCode::kOutOfRange));

  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(absl::StrFormat(R"(
        range_value: <
          start: <>
          end: <datetime_value: <bit_field_datetime_seconds: %d
                                 nanos: 0>>
        >)",
                                                             GetParam()),
                                             &value_proto));
  status_or_value = Value::Deserialize(value_proto, types::DatetimeRangeType());
  EXPECT_THAT(status_or_value, StatusIs(absl::StatusCode::kOutOfRange));
}

class TimestampRangesWithInvalidValues
    : public ::testing::TestWithParam<int64_t> {};

INSTANTIATE_TEST_SUITE_P(
    Deserialize, TimestampRangesWithInvalidValues,
    ::testing::Values(googlesql::types::kTimestampMin / 1000000 - 1,
                      googlesql::types::kTimestampMax / 1000000 + 1));

TEST_P(TimestampRangesWithInvalidValues,
       TimestampRangesWithOutOfRangeValuesFail) {
  ValueProto value_proto;
  absl::StatusOr<Value> status_or_value;

  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(absl::StrFormat(R"(
        range_value: <
          start: <timestamp_value: <seconds: %d
                                    nanos: 999999999>>
          end: <>
        >)",
                                                             GetParam()),
                                             &value_proto));
  status_or_value =
      Value::Deserialize(value_proto, types::TimestampRangeType());
  EXPECT_THAT(status_or_value, StatusIs(absl::StatusCode::kOutOfRange));

  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(absl::StrFormat(R"(
        range_value: <
          start: <>
          end: <timestamp_value: <seconds: %d
                                  nanos: 999999999>>
        >)",
                                                             GetParam()),
                                             &value_proto));
  status_or_value =
      Value::Deserialize(value_proto, types::TimestampRangeType());
  EXPECT_THAT(status_or_value, StatusIs(absl::StatusCode::kOutOfRange));
}

// Test case for deserializing invalid ranges.
struct InvalidRangeTestCase {
  // Textproto of ValueProto to deserialize.
  absl::string_view value_text_proto;
  // Type of range to supply to Deserialize function.
  const Type* range_type;
  // Expected error message substring.
  absl::string_view expected_error_message;
  // Expected error status code
  absl::StatusCode expected_error_status_code;
};

class DeserializeInvalidRangesTest
    : public ::testing::TestWithParam<InvalidRangeTestCase> {};

std::vector<InvalidRangeTestCase> GetInvalidRangeTestCases() {
  return {
      {
          // Wrong top-level type.
          "range_value: <start: <> end: <date_value: 2>>",
          /*range_type=*/types::DateArrayType(),
          /*expected_error_message=*/"Type mismatch",
          /*expected_error_status_code=*/absl::StatusCode::kInternal,
      },
      {
          // Wrong range element type.
          "range_value: <start: <> end: <date_value: 2>>",
          /*range_type=*/types::TimestampRangeType(),
          /*expected_error_message=*/"Type mismatch",
          /*expected_error_status_code=*/absl::StatusCode::kInternal,
      },
      {
          // Omitted start values are invalid.
          "range_value: <start: <date_value: 1>>",
          /*range_type=*/types::DateRangeType(),
          /*expected_error_message=*/"Type mismatch",
          /*expected_error_status_code=*/absl::StatusCode::kInternal,
      },
      {
          // Omitted end values are invalid.
          "range_value: <end: <date_value: 2>>",
          /*range_type=*/types::DateRangeType(),
          /*expected_error_message=*/"Type mismatch",
          /*expected_error_status_code=*/absl::StatusCode::kInternal,
      },
      {
          // Omitted start and end values fails.
          "range_value: <>",
          /*range_type=*/types::DateRangeType(),
          /*expected_error_message=*/"Type mismatch",
          /*expected_error_status_code=*/absl::StatusCode::kInternal,
      },
      {
          // start == end fails.
          "range_value: <start: <date_value: 1> end: <date_value: 1>>",
          /*range_type=*/types::DateRangeType(),
          /*expected_error_message=*/"Range start element must be smaller",
          /*expected_error_status_code=*/absl::StatusCode::kInvalidArgument,
      },
      {
          // start > end fails.
          "range_value: <start: <date_value: 2> end: <date_value: 1>>",
          /*range_type=*/types::DateRangeType(),
          /*expected_error_message=*/"Range start element must be smaller",
          /*expected_error_status_code=*/absl::StatusCode::kInvalidArgument,
      },
  };
}

INSTANTIATE_TEST_SUITE_P(Deserialize, DeserializeInvalidRangesTest,
                         ::testing::ValuesIn(GetInvalidRangeTestCases()));

TEST_P(DeserializeInvalidRangesTest, InvalidRangeValuesFail) {
  ValueProto value_proto;
  absl::StatusOr<Value> status_or_value;
  const InvalidRangeTestCase& param = GetParam();
  // Convert to std::string. ZetaSQL doesn't like if we pass absl::string_view
  // into ParseFromString directly.
  const std::string value_text_proto(param.value_text_proto);
  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(value_text_proto, &value_proto));
  status_or_value = Value::Deserialize(value_proto, param.range_type);
  EXPECT_THAT(status_or_value,
              StatusIs(param.expected_error_status_code,
                       HasSubstr(param.expected_error_message)));
}

TEST_F(ValueTest, DeserializeValidMapSucceeds) {
  TypeFactory factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* map_type,
      factory.MakeMapType(types::StringType(), types::StringType()));
  DeserializeSerialize("", map_type);
  DeserializeSerialize("map_value {}", map_type);
  DeserializeSerialize("map_value { entry { key {} value {} } }", map_type);
  DeserializeSerialize(
      R"(map_value {
          entry { key { string_value: "foo" } value { string_value: "bar" } }
         })",
      map_type);
}

TEST_F(ValueTest, DeserializeInvalidMapFails) {
  TypeFactory factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* map_type,
      factory.MakeMapType(types::StringType(), types::StringType()));

  {
    ValueProto value_proto;
    ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString("map_value { entry {} }",
                                                    &value_proto));
    EXPECT_THAT(Value::Deserialize(value_proto, map_type),
                StatusIs(absl::StatusCode::kOutOfRange,
                         HasSubstr("must set both key and value")));
  }
  {
    ValueProto value_proto;
    ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
        "map_value { entry { key {} } }", &value_proto));
    EXPECT_THAT(Value::Deserialize(value_proto, map_type),
                StatusIs(absl::StatusCode::kOutOfRange,
                         HasSubstr("must set both key and value")));
  }
  {
    ValueProto value_proto;
    ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
        "map_value { entry { value {} } }", &value_proto));
    EXPECT_THAT(Value::Deserialize(value_proto, map_type),
                StatusIs(absl::StatusCode::kOutOfRange,
                         HasSubstr("must set both key and value")));
  }
}

TEST_F(ValueTest, MetadataChecks) {
  Value::Metadata metadata_int32(TypeKind::TYPE_INT32);
  EXPECT_EQ(metadata_int32.has_type_pointer(), false);
  EXPECT_EQ(metadata_int32.type_kind(), TypeKind::TYPE_INT32);
  EXPECT_EQ(metadata_int32.value_extended_content(), 0);
  EXPECT_EQ(metadata_int32.preserves_order(), Value::kPreservesOrder);
  EXPECT_EQ(metadata_int32.is_null(), false);

  Value::Metadata metadata_int32_null(TypeKind::TYPE_INT32, /*is_null=*/true,
                                      /*preserves_order=*/false,
                                      /*value_extended_content=*/123);
  EXPECT_EQ(metadata_int32_null.has_type_pointer(), false);
  EXPECT_EQ(metadata_int32_null.type_kind(), TypeKind::TYPE_INT32);
  EXPECT_EQ(metadata_int32_null.value_extended_content(), 123);
  EXPECT_EQ(metadata_int32_null.preserves_order(), false);
  EXPECT_EQ(metadata_int32_null.is_null(), true);

  Value::Metadata metadata_int64(TypeKind::TYPE_INT64, /*is_null=*/false,
                                 /*preserves_order=*/true,
                                 /*value_extended_content=*/456);
  EXPECT_EQ(metadata_int64.has_type_pointer(), false);
  EXPECT_EQ(metadata_int64.type_kind(), TypeKind::TYPE_INT64);
  EXPECT_EQ(metadata_int64.value_extended_content(), 456);
  EXPECT_EQ(metadata_int64.preserves_order(), true);
  EXPECT_EQ(metadata_int64.is_null(), false);
}

// Test that Value::DebugString stack overflow cutoffs work and we don't crash.
static void StackOverflowTest() {
  TypeFactory factory;
  Value v = values::Int64(5);
  for (int i = 0; i < 1000; ++i) {
    const StructType* new_type;
    absl::Status status =
        factory.MakeStructType({{"field", v.type()}}, &new_type);
    if (!status.ok()) {
      EXPECT_GE(i, 100) << status;
      EXPECT_THAT(status, StatusIs(absl::StatusCode::kResourceExhausted));
      break;
    }
    const ArrayType* new_array_type;
    status = factory.MakeArrayType(new_type, &new_array_type);
    if (!status.ok()) {
      EXPECT_GE(i, 100) << status;
      EXPECT_THAT(status, StatusIs(absl::StatusCode::kResourceExhausted));
      break;
    }

    (void)new_type->DebugString();
    (void)new_array_type->DebugString(/*details=*/true);

    v = values::Struct(new_type, {v});
    v = values::Array(new_array_type, {v});
    LOG_EVERY_N_SEC(INFO, 10) << "Depth " << i << " " << v;
  }
  std::string s = v.DebugString();
  ABSL_LOG(INFO) << s;
  if (GOOGLESQL_DEBUG_MODE) {
    EXPECT_THAT(s, AnyOf(HasSubstr("[{field:[{field:[{field:[{field:5}]}]}]}]"),
                         HasSubstr("... <out of stack>")));
  }

  s = v.DebugString(/*verbose=*/true);
  ABSL_LOG(INFO) << s;
  if (GOOGLESQL_DEBUG_MODE) {
    EXPECT_THAT(
        s,
        AnyOf(HasSubstr("Struct{field:Array[Struct{field:Int64(5)}]}]}]}]}]}"),
              HasSubstr("... <out of stack>")));
  }
}

TEST(ValueDebugString, StackOverflow) {
  StackOverflowTest();
}

namespace {

template <typename T>
bool IsNaN(const Value& value) {
  return value.type()->IsFloatingPoint() && !value.is_null() &&
         std::isnan(value.Get<T>());
}

template <>
bool IsNaN<NumericValue>(const Value& value) {
  return false;
}

template <>
bool IsNaN<BigNumericValue>(const Value& value) {
  return false;
}

template <>
bool IsNaN<UuidValue>(const Value& value) {
  return false;
}

template <>
bool IsNaN<TimestampPicosValue>(const Value& value) {
  return false;
}

// Date ranges.
template <>
bool IsNaN<std::pair<int32_t, int32_t>>(const Value& value) {
  return false;
}

// Datetime ranges.
template <>
bool IsNaN<std::pair<DatetimeValue, DatetimeValue>>(const Value& value) {
  return false;
}

// Timestamp ranges.
template <>
bool IsNaN<std::pair<absl::Time, absl::Time>>(const Value& value) {
  return false;
}

template <typename T>
void MakeSortedVector(std::vector<googlesql::Value>* values) {
  // In GoogleSQL the ordering of values is ((broken link)):
  // NULL, NaN(s), negative infinity, finite negative numbers, zero,
  // finite positive numbers, positive infinity.

  // NULL
  values->push_back(googlesql::Value::MakeNull<T>());

  // NaN(s) - quiet and signaling, negative and positive
  if (std::numeric_limits<T>::has_quiet_NaN) {
    values->push_back(
        googlesql::Value::Make(std::numeric_limits<T>::quiet_NaN()));
    values->push_back(
        googlesql::Value::Make(-std::numeric_limits<T>::quiet_NaN()));
  }
  if (std::numeric_limits<T>::has_signaling_NaN) {
    values->push_back(
        googlesql::Value::Make(std::numeric_limits<T>::signaling_NaN()));
    values->push_back(
        googlesql::Value::Make(-std::numeric_limits<T>::signaling_NaN()));
  }

  // For signed numbers
  if (std::numeric_limits<T>::is_signed) {
    // Negative infinity
    if (std::numeric_limits<T>::has_infinity) {
      values->push_back(
          googlesql::Value::Make(-std::numeric_limits<T>::infinity()));
    }
    // Lowest finite number (will be negative)
    values->push_back(googlesql::Value::Make(std::numeric_limits<T>::lowest()));

    // Some finite negative number
    values->push_back(googlesql::Value::Make(static_cast<T>(-5)));
  }
  // Zero
  values->push_back(googlesql::Value::Make(static_cast<T>(0)));
  // Some finite positive number
  values->push_back(googlesql::Value::Make(static_cast<T>(239)));
  // Highest finite positive number
  values->push_back(googlesql::Value::Make(std::numeric_limits<T>::max()));
  // Positive infinity
  if (std::numeric_limits<T>::has_infinity) {
    values->push_back(
        googlesql::Value::Make(std::numeric_limits<T>::infinity()));
  }
}

template <>
void MakeSortedVector<NumericValue>(std::vector<googlesql::Value>* values) {
  // NULL
  values->push_back(NullNumeric());
  // Lowest finite number (will be negative)
  values->push_back(Value::Numeric(NumericValue::MinValue()));

  // Some finite negative number
  values->push_back(Value::Numeric(NumericValue(-5)));
  // Zero
  values->push_back(Value::Numeric(NumericValue(0)));
  // Some finite positive number
  values->push_back(
      Value::Numeric(NumericValue::FromStringStrict("123.4").value()));
  // Highest finite positive number
  values->push_back(Value::Numeric(NumericValue::MaxValue()));
}

template <>
void MakeSortedVector<BigNumericValue>(std::vector<googlesql::Value>* values) {
  // NULL
  values->push_back(NullBigNumeric());
  // Lowest finite number (will be negative)
  values->push_back(Value::BigNumeric(BigNumericValue::MinValue()));

  // Some finite negative number
  values->push_back(Value::BigNumeric(BigNumericValue(-5)));
  // Zero
  values->push_back(Value::BigNumeric(BigNumericValue(0)));
  // Some finite positive number
  values->push_back(
      Value::BigNumeric(BigNumericValue::FromStringStrict("123.4").value()));
  // Highest finite positive number
  values->push_back(Value::BigNumeric(BigNumericValue::MaxValue()));
}

template <>
void MakeSortedVector<UuidValue>(std::vector<googlesql::Value>* values) {
  // NULL
  values->push_back(NullUuid());
  // Lowest uuid value
  values->push_back(Value::Uuid(
      UuidValue::FromString("00000000-0000-0000-0000-000000000000").value()));
  // Some uuid value
  values->push_back(Value::Uuid(
      UuidValue::FromString("9d5da323-4c20-360f-bd9b-ec54feec54f0").value()));
  // Highest uuid value
  values->push_back(Value::Uuid(
      UuidValue::FromString("ffffffff-ffff-ffff-ffff-ffffffffffff").value()));
}

// Date ranges.
template <>
void MakeSortedVector<std::pair<int32_t, int32_t>>(
    std::vector<googlesql::Value>* values) {
  // NULL
  values->push_back(Value::Null(MakeRangeType(DateType())));

  // Unbounded-start range with min date end.
  values->push_back(Range(Value::UnboundedStartDate(), Date(types::kDateMin)));

  // Unbounded-start range with arbitrary date end.
  values->push_back(Range(Value::UnboundedStartDate(), Date(100)));

  // Unbounded-start range with max date.
  values->push_back(Range(Value::UnboundedStartDate(), Date(types::kDateMax)));

  // Unbounded-start range with unbounded end.
  values->push_back(
      Range(Value::UnboundedStartDate(), Value::UnboundedEndDate()));

  // Min date start, arbitrary end date.
  values->push_back(Range(Date(types::kDateMin), Date(100)));

  // Arbitrarily larger start date, same end date.
  values->push_back(Range(Date(50), Date(100)));

  // Unbounded-end date.
  values->push_back(Range(Date(50), Value::UnboundedEndDate()));

  // Max date start, unbounded-end date.
  values->push_back(Range(Date(types::kDateMax), Value::UnboundedEndDate()));
}

// Datetime ranges.
template <>
void MakeSortedVector<std::pair<DatetimeValue, DatetimeValue>>(
    std::vector<googlesql::Value>* values) {
  // NULL
  values->push_back(Value::Null(MakeRangeType(DatetimeType())));

  Value datetime_min =
      Datetime(DatetimeValue::FromYMDHMSAndNanos(0001, 01, 01, 00, 00, 00, 0));
  Value datetime_max = Datetime(
      DatetimeValue::FromYMDHMSAndNanos(9999, 12, 31, 23, 59, 59, 999999999));

  // Unbounded-start range with min datetime end.
  values->push_back(Range(Value::UnboundedStartDatetime(), datetime_min));

  // Unbounded-start range with arbitrary datetime end.
  values->push_back(Range(Value::UnboundedStartDatetime(),
                          Datetime(DatetimeValue::FromYMDHMSAndNanos(
                              2020, 01, 01, 00, 00, 00, 0))));

  // Unbounded-start range with max datetime.
  values->push_back(Range(Value::UnboundedStartDatetime(), datetime_max));

  // Unbounded-start range with unbounded end.
  values->push_back(
      Range(Value::UnboundedStartDatetime(), Value::UnboundedEndDatetime()));

  // Min datetime start, arbitrary end datetime.
  values->push_back(
      Range(datetime_min, Datetime(DatetimeValue::FromYMDHMSAndNanos(
                              2020, 01, 01, 00, 00, 00, 0))));

  // Arbitrarily larger start datetime, same end datetime.
  values->push_back(Range(
      Datetime(DatetimeValue::FromYMDHMSAndNanos(1000, 01, 01, 00, 00, 00, 0)),
      Datetime(
          DatetimeValue::FromYMDHMSAndNanos(2020, 01, 01, 00, 00, 00, 0))));

  // Unbounded-end date.
  values->push_back(Range(
      Datetime(DatetimeValue::FromYMDHMSAndNanos(1000, 01, 01, 00, 00, 00, 0)),
      Value::UnboundedEndDatetime()));

  // Max datetime start, unbounded-end date.
  values->push_back(Range(datetime_max, Value::UnboundedEndDatetime()));
}

// Timestamp ranges.
template <>
void MakeSortedVector<std::pair<absl::Time, absl::Time>>(
    std::vector<googlesql::Value>* values) {
  // NULL
  values->push_back(Value::Null(MakeRangeType(TimestampType())));

  const absl::TimeZone utc = absl::UTCTimeZone();
  Value timestamp_min = Value::Timestamp(
      absl::FromCivil(absl::CivilSecond(00001, 01, 01, 00, 00, 00), utc));
  Value timestamp_max = Value::Timestamp(
      absl::FromCivil(absl::CivilSecond(10000, 01, 01, 00, 00, 00), utc) -
      absl::Nanoseconds(1));

  // Unbounded-start range with min timestamp end.
  values->push_back(Range(Value::UnboundedStartTimestamp(), timestamp_min));

  // Unbounded-start range with arbitrary timestamp end.
  values->push_back(Range(Value::UnboundedStartTimestamp(),
                          Value::Timestamp(absl::FromCivil(
                              absl::CivilSecond(2020, 01, 01, 0, 0, 0), utc))));

  // Unbounded-start range with max timestamp.
  values->push_back(Range(Value::UnboundedStartTimestamp(), timestamp_max));

  // Unbounded-start range with unbounded end.
  values->push_back(
      Range(Value::UnboundedStartTimestamp(), Value::UnboundedEndTimestamp()));

  // Min timestamp start, arbitrary end timestamp.
  values->push_back(Range(timestamp_min,
                          Value::Timestamp(absl::FromCivil(
                              absl::CivilSecond(2020, 01, 01, 0, 0, 0), utc))));

  // Arbitrarily larger start timestamp, same end timestamp.
  values->push_back(Range(Value::Timestamp(absl::FromCivil(
                              absl::CivilSecond(1000, 01, 01, 0, 0, 0), utc)),
                          Value::Timestamp(absl::FromCivil(
                              absl::CivilSecond(2020, 01, 01, 0, 0, 0), utc))));

  // Unbounded-end date.
  values->push_back(Range(Value::Timestamp(absl::FromCivil(
                              absl::CivilSecond(1000, 01, 01, 0, 0, 0), utc)),
                          Value::UnboundedEndTimestamp()));

  // Max timestamp start, unbounded-end date.
  values->push_back(Range(timestamp_max, Value::UnboundedEndTimestamp()));
}

}  // namespace

class ValueCompareTest : public ::testing::Test {
 public:
  ValueCompareTest() = default;
  ValueCompareTest(const ValueCompareTest&) = delete;
  ValueCompareTest& operator=(const ValueCompareTest&) = delete;
  ~ValueCompareTest() override = default;

  template <typename T>
  void TestSortOrder() {
    // In GoogleSQL the ordering of values is ((broken link)):
    // NULL, NaN(s), negative infinity, finite negative numbers, zero,
    // finite positive numbers, positive infinity.
    std::vector<googlesql::Value> values;

    MakeSortedVector<T>(&values);

    size_t num_values = values.size();
    for (int i = 0; i < num_values; i++) {
      // values[i] = values[i] for any i
      EXPECT_EQ(values[i], values[i]);
      EXPECT_TRUE(values[i].Equals(values[i]));
      EXPECT_FALSE(values[i].LessThan(values[i]));
      for (int j = i + 1; j < num_values; j++) {
        if (IsNaN<T>(values[i]) && IsNaN<T>(values[j])) {
          // values[i] == values[j] when both are NaNs
          EXPECT_EQ(values[i], values[j]);
          EXPECT_TRUE(values[i].Equals(values[j]));
          EXPECT_FALSE(values[i].LessThan(values[j]));
          EXPECT_FALSE(values[j].LessThan(values[i]));
        } else {
          // values[i] < values[j]
          EXPECT_NE(values[i], values[j]);
          EXPECT_FALSE(values[i].Equals(values[j]));
          EXPECT_TRUE(values[i].LessThan(values[j]));
          EXPECT_FALSE(values[j].LessThan(values[i]));
        }
      }
    }
  }
};

TEST_F(ValueCompareTest, SortOrder) {
  TestSortOrder<int32_t>();
  TestSortOrder<uint32_t>();
  TestSortOrder<int64_t>();
  TestSortOrder<uint64_t>();
  TestSortOrder<float>();
  TestSortOrder<double>();
  TestSortOrder<NumericValue>();
  TestSortOrder<BigNumericValue>();
  TestSortOrder<UuidValue>();
  // Date ranges.
  TestSortOrder<std::pair<int32_t, int32_t>>();
  // Datetime ranges.
  TestSortOrder<std::pair<DatetimeValue, DatetimeValue>>();
  // Timestamp ranges.
  TestSortOrder<std::pair<absl::Time, absl::Time>>();
}

}  // namespace googlesql
