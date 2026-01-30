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

#include "googlesql/public/proto_value_conversion.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/base/path.h"
#include "google/protobuf/timestamp.pb.h"
#include "google/protobuf/descriptor.pb.h"
#include "googlesql/common/testing/proto_matchers.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/analyzer.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/convert_type_to_proto.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/proto/type_annotation.pb.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/timestamp_picos_value.h"
#include "googlesql/public/token_list_util.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/value.h"
#include "googlesql/public/value.pb.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/functional/bind_front.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "google/protobuf/compiler/parser.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"
#include "google/protobuf/text_format.h"
#include "google/protobuf/dynamic_message.h"
#include "google/protobuf/io/tokenizer.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

using googlesql::testing::EqualsProto;
using ::google::protobuf::DescriptorPool;
using ::google::protobuf::FieldDescriptorProto;
using ::google::protobuf::FileDescriptor;
using ::google::protobuf::Message;

using ::testing::HasSubstr;
using ::testing::NotNull;
using ::testing::Pointee;
using ::testing::Values;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;

namespace {

class ProtoValueConversionTest : public ::testing::Test {
 public:

 protected:
  ProtoValueConversionTest()
      : descriptor_pool_(google::protobuf::DescriptorPool::generated_pool()),
        catalog_("base", &type_factory_) {
    message_factory_.SetDelegateToGeneratedFactory(true);
    catalog_.SetDescriptorPool(&descriptor_pool_);
  }

  ProtoValueConversionTest(const ProtoValueConversionTest&) = delete;
  ProtoValueConversionTest& operator=(const ProtoValueConversionTest&) = delete;

  ~ProtoValueConversionTest() override = default;

  absl::Status ParseLiteralExpression(
      absl::string_view expression_sql, Value* value_out,
      const ConvertTypeToProtoOptions& options) {
    std::unique_ptr<const AnalyzerOutput> output;
    LanguageOptions language_options;
    language_options.EnableLanguageFeature(FEATURE_NUMERIC_TYPE);
    language_options.EnableLanguageFeature(FEATURE_GEOGRAPHY);
    language_options.EnableLanguageFeature(FEATURE_CIVIL_TIME);
    language_options.EnableLanguageFeature(FEATURE_BIGNUMERIC_TYPE);
    language_options.EnableLanguageFeature(FEATURE_JSON_TYPE);
    language_options.EnableLanguageFeature(FEATURE_INTERVAL_TYPE);
    language_options.EnableLanguageFeature(FEATURE_TOKENIZED_SEARCH);
    language_options.EnableLanguageFeature(FEATURE_RANGE_TYPE);
    language_options.EnableLanguageFeature(FEATURE_V_1_4_UUID_TYPE);
    if (options.timestamp_format_options.timestamp_format ==
        ConvertTypeToProtoOptions::TimestampFormat::kTimestampPicosProto) {
      language_options.EnableLanguageFeature(FEATURE_TIMESTAMP_NANOS);
      language_options.EnableLanguageFeature(FEATURE_TIMESTAMP_PICOS);
    }
    if (catalog_.functions().empty()) {
      catalog_.AddBuiltinFunctions(
          BuiltinFunctionOptions::AllReleasedFunctions());
    }
    GOOGLESQL_RETURN_IF_ERROR(AnalyzeExpression(expression_sql,
                                      AnalyzerOptions(language_options),
                                      &catalog_, &type_factory_, &output));
    GOOGLESQL_RET_CHECK(output != nullptr) << expression_sql;
    const ResolvedExpr* expr = output->resolved_expr();
    GOOGLESQL_RET_CHECK(expr != nullptr) << expression_sql;
    // When the timestamp format is kTimestampPicosProto, we need to handle
    // different cases for the resolved expression. Specifically, when the
    // timestamp format is in picos precision, the resolved expression can be a
    // a MakeStruct or a FunctionCall for array. For other cases, the resolved
    // expression should be a ResolvedLiteral.
    if (language_options.LanguageFeatureEnabled(FEATURE_TIMESTAMP_PICOS)) {
      if (expr->node_kind() == RESOLVED_LITERAL) {
        const ResolvedLiteral* literal = expr->GetAs<ResolvedLiteral>();
        *value_out = literal->value();
      } else if (expr->node_kind() == RESOLVED_MAKE_STRUCT) {
        // The resolver provides the node kind as MakeStruct, in this case so
        // we convert to a Struct value.
        const ResolvedMakeStruct* make_struct =
            expr->GetAs<ResolvedMakeStruct>();
        const Type* type = make_struct->type();
        std::vector<Value> field_values;
        for (const auto& field_expr : make_struct->field_list()) {
          GOOGLESQL_RET_CHECK_EQ(field_expr->node_kind(), RESOLVED_CAST);
          const ResolvedCast* cast = field_expr->GetAs<ResolvedCast>();
          GOOGLESQL_RET_CHECK(cast->expr()->node_kind() == RESOLVED_LITERAL);
          const ResolvedLiteral* literal =
              cast->expr()->GetAs<ResolvedLiteral>();
          GOOGLESQL_ASSIGN_OR_RETURN(
              TimestampPicosValue value,
              TimestampPicosValue::FromString(literal->value().string_value(),
                                              absl::UTCTimeZone(),
                                              /*allow_tz_in_str=*/true));
          field_values.push_back(Value::Timestamp(value));
        }
        GOOGLESQL_ASSIGN_OR_RETURN(*value_out,
                         Value::MakeStruct(type->AsStruct(), field_values));
      } else if (expr->node_kind() == RESOLVED_FUNCTION_CALL) {
        // The resolver provides the node kind as FunctionCall for array, in
        // this case so we convert to an Array value.
        const ResolvedFunctionCall* function_call =
            expr->GetAs<ResolvedFunctionCall>();
        GOOGLESQL_RET_CHECK_EQ(function_call->function()->Name(), "$make_array");
        const ArrayType* type = function_call->type()->AsArray();
        std::vector<Value> element_values;
        for (const auto& arg_expr : function_call->argument_list()) {
          GOOGLESQL_RET_CHECK_EQ(arg_expr->node_kind(), RESOLVED_CAST);
          const ResolvedCast* cast = arg_expr->GetAs<ResolvedCast>();
          GOOGLESQL_RET_CHECK(cast->expr()->node_kind() == RESOLVED_LITERAL);
          const ResolvedLiteral* literal =
              cast->expr()->GetAs<ResolvedLiteral>();
          GOOGLESQL_ASSIGN_OR_RETURN(
              TimestampPicosValue value,
              TimestampPicosValue::FromString(literal->value().string_value(),
                                              absl::UTCTimeZone(),
                                              /*allow_tz_in_str=*/true));
          element_values.push_back(Value::Timestamp(value));
        }
        GOOGLESQL_ASSIGN_OR_RETURN(*value_out, Value::MakeArray(type, element_values));
      } else {
        GOOGLESQL_RET_CHECK_FAIL() << "Expression '" << expression_sql
                         << "' evaluated to non-literal or MakeStruct: "
                         << expr->DebugString();
      }
    } else {
      GOOGLESQL_RET_CHECK_EQ(RESOLVED_LITERAL, expr->node_kind())
          << "Expression '" << expression_sql
          << "' evaluated to non-literal: " << expr->DebugString();
      const ResolvedLiteral* literal = expr->GetAs<ResolvedLiteral>();
      *value_out = literal->value();
    }
    return absl::OkStatus();
  }

  absl::Status GetProtoDescriptorForType(
      const Type* type, const ConvertTypeToProtoOptions& options_in,
      const google::protobuf::Descriptor** descriptor_out) {
    const int proto_id = next_dynamic_proto_id_++;
    const std::string message_name = absl::StrCat("GeneratedProto", proto_id);
    ConvertTypeToProtoOptions options = options_in;
    options.message_name = message_name;

    google::protobuf::FileDescriptorProto file;
    if (type->IsStruct()) {
      GOOGLESQL_RETURN_IF_ERROR(ConvertStructToProto(type->AsStruct(), &file, options));
    } else if (type->IsArray()) {
      GOOGLESQL_RETURN_IF_ERROR(ConvertArrayToProto(type->AsArray(), &file, options));
    } else {
      GOOGLESQL_RET_CHECK_FAIL() << type->DebugString();
    }

    file.set_name(message_name);

    const google::protobuf::FileDescriptor* file_descriptor =
        descriptor_pool_.BuildFile(file);
    GOOGLESQL_RET_CHECK(file_descriptor != nullptr);

    *descriptor_out = file_descriptor->FindMessageTypeByName(message_name);
    GOOGLESQL_RET_CHECK(*descriptor_out != nullptr);

    return absl::OkStatus();
  }

  // Performs the round-trip test for the given SQL expression and options.
  // Returns true if no test error was encountered and false otherwise.
  bool RoundTripTest(absl::string_view expression_sql,
                     const ConvertTypeToProtoOptions& options) {
    DoRoundTripTest(expression_sql, options);
    return !::testing::Test::HasFailure();
  }

  absl::StatusOr<std::unique_ptr<google::protobuf::Message>> LiteralValueToProto(
      absl::string_view expression_sql,
      const ConvertTypeToProtoOptions& options) {
    Value value;
    GOOGLESQL_RETURN_IF_ERROR(ParseLiteralExpression(expression_sql, &value, options));

    std::unique_ptr<google::protobuf::Message> proto;
    GOOGLESQL_RETURN_IF_ERROR(ValueToProto(value, options, &proto));
    return proto;
  }

  absl::Status ValueToProto(const Value& value,
                            const ConvertTypeToProtoOptions& options,
                            std::unique_ptr<google::protobuf::Message>* proto_out) {
    const google::protobuf::Descriptor* descriptor = nullptr;
    GOOGLESQL_RETURN_IF_ERROR(
        GetProtoDescriptorForType(value.type(), options, &descriptor));

    proto_out->reset(message_factory_.GetPrototype(descriptor)->New());
    GOOGLESQL_RETURN_IF_ERROR(ConvertStructOrArrayValueToProtoMessage(
        value, &message_factory_, proto_out->get()));
    return absl::OkStatus();
  }

  std::unique_ptr<google::protobuf::Message> TestFileConversion(
      const Value& value, const ConvertTypeToProtoOptions& options) {
    std::unique_ptr<google::protobuf::Message> proto_out;
    GOOGLESQL_EXPECT_OK(ValueToProto(value, options, &proto_out));
    return proto_out;
  }

  TypeFactory type_factory_;

 private:
  void DoRoundTripTest(absl::string_view expression_sql,
                       const ConvertTypeToProtoOptions& options) {
    // Print out the current state in case of failure.
    SCOPED_TRACE(absl::Substitute(
        "Round trip test for expression: $0\n"
        "Array wrappers: $1; element wrappers: $2",
        expression_sql, options.generate_nullable_array_wrappers,
        options.generate_nullable_element_wrappers));

    Value value;
    GOOGLESQL_ASSERT_OK(ParseLiteralExpression(expression_sql, &value, options));

    std::unique_ptr<google::protobuf::Message> proto;
    GOOGLESQL_ASSERT_OK(ValueToProto(value, options, &proto));

    const Type* round_tripped_type = nullptr;
    GOOGLESQL_ASSERT_OK(type_factory_.MakeUnwrappedTypeFromProto(proto->GetDescriptor(),
                                                       &round_tripped_type));

    Value round_tripped_value;
    GOOGLESQL_ASSERT_OK(ConvertProtoMessageToStructOrArrayValue(
        *proto, round_tripped_type, &round_tripped_value));

    ASSERT_TRUE(value.type()->Equals(round_tripped_value.type()))
        << "Expression '" << expression_sql
        << "' has type: " << value.type()->DebugString()
        << " which round-trips to: "
        << round_tripped_value.type()->DebugString();
    ASSERT_TRUE(value.Equals(round_tripped_value))
        << "Expression '" << expression_sql
        << "' evaluates to: " << value.DebugString()
        << " which round-trips to: " << round_tripped_value.DebugString();
  }

  google::protobuf::DescriptorPool descriptor_pool_;
  SimpleCatalog catalog_;
  // We use a counter to generate new names for the dynamic proto descriptors
  // generated by GetPRotoDescriptorForType.
  int next_dynamic_proto_id_ = 0;
  google::protobuf::DynamicMessageFactory message_factory_;
};

TEST_F(ProtoValueConversionTest, RoundTrip) {
  const std::vector<std::string> base_test_expressions = {
      // STRUCTs containing all types (including NULLs).
      "STRUCT(-64)",
      "STRUCT(CAST(NULL AS INT64))",
      "STRUCT(CAST(-32 AS INT32))",
      "STRUCT(CAST(NULL AS INT32))",
      "STRUCT(CAST(64 AS UINT64))",
      "STRUCT(CAST(NULL AS UINT64))",
      "STRUCT(CAST(32 AS UINT32))",
      "STRUCT(CAST(NULL AS UINT32))",
      "STRUCT(TRUE)",
      "STRUCT(CAST(NULL AS BOOL))",
      "STRUCT(CAST(0.25 AS FLOAT))",
      "STRUCT(CAST(NULL AS FLOAT))",
      "STRUCT(0.5)",
      "STRUCT(CAST(NULL AS DOUBLE))",
      "STRUCT('str')",
      "STRUCT(CAST(NULL AS STRING))",
      "STRUCT(b'invalid utf-8 bytes\xff')",
      "STRUCT(CAST(NULL AS BYTES))",
      "STRUCT(CAST('1998-09-04' AS DATE))",
      "STRUCT(CAST(NULL AS DATE))",
      "STRUCT(CAST('1998-09-04 01:23:45.678901' AS DATETIME))",
      "STRUCT(CAST(NULL AS DATETIME))",
      "STRUCT(CAST('01:23:45.678901' AS TIME))",
      "STRUCT(CAST(NULL AS TIME))",
      "STRUCT(CAST('1998-09-04 01:23:45.678901 UTC' AS TIMESTAMP))",
      "STRUCT(TIMESTAMP '1998-09-04 01:23:45.678901 UTC')",
      "STRUCT(CAST(NULL AS TIMESTAMP))",
      "STRUCT(CAST('TYPE_ENUM' AS googlesql.TypeKind))",
      "STRUCT(CAST(NULL AS googlesql.TypeKind))",
      "STRUCT(STRUCT(64))",
      "STRUCT(CAST(NULL AS STRUCT<INT64>))",
      "STRUCT([1,2,3])",
      "STRUCT(CAST(NULL AS NUMERIC))",
      "STRUCT(CAST(-2.000000001 AS NUMERIC))",
      "STRUCT(CAST(NULL AS BIGNUMERIC))",
      "STRUCT(CAST(-12345678901234567890123456789.1234567890123456789012345678 "
      "AS BIGNUMERIC))",
      "STRUCT(CAST(NULL AS GEOGRAPHY))",
      "STRUCT(CAST(NULL AS JSON))",
      "STRUCT(CAST(NULL AS TOKENLIST))",
      "STRUCT(RANGE<DATE> '[2022-12-06, 2022-12-07)')",
      "STRUCT(CAST(NULL AS RANGE<DATE>))",
      "STRUCT(RANGE<DATETIME> '[2022-12-05 16:44:00.000007, 2022-12-05 "
      "16:45:00.000007)')",
      "STRUCT(CAST(NULL AS RANGE<DATETIME>))",
      "STRUCT(RANGE<TIMESTAMP> '[2022-12-05 16:44:00.000007+00, 2022-12-05 "
      "16:45:00.000007)')",
      "STRUCT(CAST(NULL AS RANGE<TIMESTAMP>))",
      "STRUCT(CAST(NULL AS UUID))",
      "STRUCT(CAST('00000000000040008000000000000000' AS UUID))",

      // STRUCT containing a proto.
      R"(
          CAST(STRUCT('''proto_name: 'foo'
                         proto_file_name: 'foo.proto'
                         file_descriptor_set_index: 32''')
               AS STRUCT<googlesql.ProtoTypeProto>)
      )",
      "STRUCT(CAST(NULL AS googlesql.ProtoTypeProto))",

      // STRUCT with names.
      "STRUCT(2 as x)",

      // STRUCT with duplicate names.
      "STRUCT(2 as x, 3 as x)",

      // STRUCT containing a PROTO that is a wrapper.
      "CAST(STRUCT('value: 3') AS STRUCT<googlesql_test.NullableInt>)",

      // STRUCT containing a PROTO that is annotated as a nullable array.
      R"(
          CAST(STRUCT('value: 3 value: 4')
               AS STRUCT<googlesql_test.NullableArrayOfInt>)
      )",

      // STRUCT containing a PROTO that is annotated as a STRUCT.
      R"(
          CAST(STRUCT('''key: 'key' value: 1''')
               AS STRUCT<googlesql_test.KeyValueStruct>)
      )",

      // ARRAYs containing all types.  We don't include NULLs here.  See
      // nullable_element_test_expressions for test expressions with NULLs.
      "[]",
      "ARRAY<INT64>[]",
      "[-64]",
      "[CAST(-32 AS INT32)]",
      "[CAST(64 AS UINT64)]",
      "[CAST(32 AS UINT32)]",
      "[TRUE]",
      "[CAST(0.25 AS FLOAT)]",
      "[0.5]",
      "['str']",
      "[b'invalid utf-8 bytes\xff']",
      "[CAST('1998-09-04' AS DATE)]",
      "[CAST('1998-09-04 01:23:45.678901 UTC' AS TIMESTAMP)]",
      "[CAST('TYPE_ENUM' AS googlesql.TypeKind)]",
      "[STRUCT(64)]",
      "[CAST(-2.000000001 AS NUMERIC)]",
      "[CAST(-12345678901234567890123456789.1234567890123456789012345678 AS "
      "BIGNUMERIC)]",
      "[CAST('00000000000040008000000000000000' AS UUID)]",
      // ARRAYs containing ARRAYs would go here, but those aren't allowed
      // in GoogleSQL.  Instead, we do ARRAY<STRUCT<ARRAY>>.
      "[STRUCT([1,2,3])]",

      R"(
          ARRAY<googlesql.ProtoTypeProto>['''proto_name: 'foo'
                   proto_file_name: 'foo.proto'
                   file_descriptor_set_index: 32''']
      )",

      // ARRAY containing a PROTO that is a wrapper.
      "ARRAY<googlesql_test.NullableInt>['value: 3']",

      // ARRAY containing a PROTO that is annotated as a nullable array.
      "ARRAY<googlesql_test.NullableArrayOfInt>['value: 3 value: 4']",

      // ARRAY containing a PROTO that is annotated as a STRUCT.
      "ARRAY<googlesql_test.KeyValueStruct>['''key: 'key' value: 1''']",
  };

  const std::vector<std::string> nullable_array_test_expressions = {
      "STRUCT(CAST(NULL AS ARRAY<INT64>))",
  };

  const std::vector<std::string> nullable_element_test_expressions = {
      "[CAST(NULL AS INT64)]", "[CAST(NULL AS INT32)]",
      "[CAST(NULL AS UINT64)]", "[CAST(NULL AS UINT32)]",
      "[CAST(NULL AS BOOL)]", "[CAST(NULL AS FLOAT)]", "[CAST(NULL AS DOUBLE)]",
      "[CAST(NULL AS STRING)]", "[CAST(NULL AS BYTES)]", "[CAST(NULL AS DATE)]",
      "[CAST(NULL AS TIMESTAMP)]", "[CAST(NULL AS googlesql.TypeKind)]",
      "[CAST(NULL AS STRUCT<INT64>)]",
      // ARRAYs containing ARRAYs would go here, but those aren't allowed
      // in GoogleSQL.  Instead, we do ARRAY<STRUCT<ARRAY>>.
      "[CAST(NULL AS STRUCT<ARRAY<INT64>>)]",
      "[CAST(NULL AS googlesql.ProtoTypeProto)]", "[CAST(NULL AS NUMERIC)]",
      "[CAST(NULL AS BIGNUMERIC)]", "[CAST(NULL AS GEOGRAPHY)]",
      "[CAST(NULL AS TOKENLIST)]", "[CAST(NULL AS RANGE<DATE>)]",
      "[CAST(NULL AS RANGE<DATETIME>)]", "[CAST(NULL AS RANGE<TIMESTAMP>)]",
      "[CAST(NULL AS JSON)]", "[CAST(NULL AS UUID)]"};

  for (bool array_wrappers : {true, false}) {
    for (bool element_wrappers : {true, false}) {
      ConvertTypeToProtoOptions options;
      options.generate_nullable_array_wrappers = array_wrappers;
      options.generate_nullable_element_wrappers = element_wrappers;
      for (const std::string& test_expression : base_test_expressions) {
        ASSERT_TRUE(RoundTripTest(test_expression, options));
      }
      if (array_wrappers) {
        for (const std::string& test_expression :
             nullable_array_test_expressions) {
          ASSERT_TRUE(RoundTripTest(test_expression, options));
        }
      }
      if (element_wrappers) {
        for (const std::string& test_expression :
             nullable_element_test_expressions) {
          ASSERT_TRUE(RoundTripTest(test_expression, options));
        }
      }
    }
  }
}

TEST_F(ProtoValueConversionTest, ToProtoDefaultOptions) {
  const ConvertTypeToProtoOptions default_options;
  const std::vector<std::pair<std::string, std::string>> tests = {
      // Dates should be encoded as googlesql DATEs by default.
      // Timestamps should be encoded as TIMESTAMP_MICROS by default.
      {"STRUCT(CAST('1970-01-02' AS DATE) AS d)", "d: 1"},
      {"STRUCT(CAST(NULL AS DATE) AS d)", ""},
      {"STRUCT(CAST('1970-01-01 00:00:12.345678 UTC' AS TIMESTAMP) AS t)",
       "t: 12345678"},
      {"STRUCT(CAST(NULL AS TIMESTAMP) AS t)", ""},
  };

  for (const auto& sql_proto_pair : tests) {
    const std::string& test_expression = sql_proto_pair.first;
    const std::string& expected_proto = sql_proto_pair.second;
    ASSERT_THAT(LiteralValueToProto(test_expression, default_options),
                IsOkAndHolds(Pointee(EqualsProto(expected_proto))))
        << test_expression;
  }
}

TEST_F(ProtoValueConversionTest, DateDecimal) {
  // Make sure that DATE_DECIMAL format is obeyed in both directions.
  ConvertTypeToProtoOptions options;
  options.field_format_map[TYPE_DATE] = FieldFormat::DATE_DECIMAL;

  const std::vector<std::pair<std::string, std::string>> tests = {
      {"STRUCT(CAST('1970-01-02' AS DATE) AS d)", "d: 19700102"},
      {"STRUCT(CAST(NULL AS DATE) AS d)", ""},
  };

  for (const auto& sql_proto_pair : tests) {
    const std::string& test_expression = sql_proto_pair.first;
    const std::string& expected_proto = sql_proto_pair.second;
    ASSERT_THAT(LiteralValueToProto(test_expression, options),
                IsOkAndHolds(Pointee(EqualsProto(expected_proto))))
        << test_expression;
    ASSERT_TRUE(RoundTripTest(test_expression, options));
  }

  // Also test funny DATE_DECIMAL cases:
  //   1) DATE_DECIMAL 0 should get translated to a NULL value.
  //   2) Invalid DATE_DECIMALs should result in error.
  {
    // We start by generating a Value with a non-null date, and converting
    // it to proto.
    Value value;
    GOOGLESQL_ASSERT_OK(ParseLiteralExpression("STRUCT(CAST('1970-01-02' AS DATE) AS d)",
                                     &value, options));
    const StructType* struct_type = value.type()->AsStruct();
    ASSERT_TRUE(struct_type != nullptr);
    std::unique_ptr<google::protobuf::Message> proto;
    GOOGLESQL_ASSERT_OK(ValueToProto(value, options, &proto));
    ASSERT_THAT(*proto, EqualsProto("d: 19700102"));

    // Now, overwrite the proto with one that has 0 for the date field.
    ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString("d: 0", proto.get()));
    Value result_value;
    GOOGLESQL_ASSERT_OK(ConvertProtoMessageToStructOrArrayValue(*proto, value.type(),
                                                      &result_value));
    EXPECT_EQ("Struct{d:Date(NULL)}", result_value.FullDebugString());

    // Now try some invalid DATE_DECIMALs.
    for (int invalid_date : {-1, 1, 101, 100000101, 20001301, 20001232}) {
      ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
          absl::StrCat("d: ", invalid_date), proto.get()))
          << invalid_date;
      EXPECT_THAT(ConvertProtoMessageToStructOrArrayValue(*proto, value.type(),
                                                          &result_value),
                  StatusIs(absl::StatusCode::kOutOfRange,
                           HasSubstr("Invalid DATE_DECIMAL")))
          << invalid_date;
    }
  }
}

TEST_F(ProtoValueConversionTest, TimestampSeconds) {
  ConvertTypeToProtoOptions options;
  options.field_format_map[TYPE_TIMESTAMP] = FieldFormat::TIMESTAMP_SECONDS;

  const std::vector<std::pair<std::string, std::string>> tests = {
      {"STRUCT(CAST('1998-09-04 01:23:45 UTC' AS TIMESTAMP) AS t)",
       "t:904872225 "},
      {"STRUCT(CAST('1965-09-04 01:23:45 UTC' AS TIMESTAMP) AS t)",
       "t:-136506975"},
      {"STRUCT(CAST(NULL AS TIMESTAMP) AS t)", ""},
  };

  for (const auto& sql_proto_pair : tests) {
    const std::string& test_expression = sql_proto_pair.first;
    const std::string& expected_proto = sql_proto_pair.second;
    ASSERT_THAT(LiteralValueToProto(test_expression, options),
                IsOkAndHolds(Pointee(EqualsProto(expected_proto))))
        << test_expression;
    ASSERT_TRUE(RoundTripTest(test_expression, options));
  }
}

class ProtoValueConversionRangeRoundtripTest
    : public ProtoValueConversionTest,
      public ::testing::WithParamInterface<std::string> {};

std::vector<std::string> GetRangeTestCases() {
  return {
      // Dates
      {"STRUCT(RANGE<DATE> '[2022-12-06, 2022-12-07)')"},
      {"STRUCT(RANGE<DATE> '[2022-12-06, UNBOUNDED)')"},
      {"STRUCT(RANGE<DATE> '[UNBOUNDED, 2022-12-07)')"},
      {"STRUCT(RANGE<DATE> '[UNBOUNDED, UNBOUNDED)')"},
      // Datetimes
      {"STRUCT(RANGE<DATETIME> '[2022-12-05 16:44:00.000007, 2022-12-05 "
       "16:45:00.000007)')"},
      {"STRUCT(RANGE<DATETIME> '[2022-12-05 16:44:00.000007, UNBOUNDED)')"},
      {"STRUCT(RANGE<DATETIME> '[UNBOUNDED, 2022-12-05 16:45:00.000007)')"},
      {"STRUCT(RANGE<DATETIME> '[UNBOUNDED, UNBOUNDED)')"},
      // Timestamps
      {"STRUCT(RANGE<TIMESTAMP> '[2022-12-05 16:44:00.000007+00, 2022-12-05 "
       "16:45:00.000007+00)')"},
      {"STRUCT(RANGE<TIMESTAMP> '[2022-12-05 16:44:00.000007+00, UNBOUNDED)')"},
      {"STRUCT(RANGE<TIMESTAMP> '[UNBOUNDED, 2022-12-05 16:45:00.000007+00)')"},
      {"STRUCT(RANGE<TIMESTAMP> '[UNBOUNDED, UNBOUNDED)')"},
  };
}

INSTANTIATE_TEST_SUITE_P(Range, ProtoValueConversionRangeRoundtripTest,
                         ::testing::ValuesIn(GetRangeTestCases()));

TEST_P(ProtoValueConversionRangeRoundtripTest, Do) {
  ConvertTypeToProtoOptions options;
  ASSERT_TRUE(RoundTripTest(GetParam(), options));
}

class SimpleErrorCollector : public DescriptorPool::ErrorCollector {
 public:
  SimpleErrorCollector() = default;

  void RecordError(absl::string_view filename, absl::string_view element_name,
                   const Message* descriptor, ErrorLocation location,
                   absl::string_view message) override {
    absl::StrAppend(&errors_, message);
  }

  const std::string& errors() const { return errors_; }

 private:
  std::string errors_;
};

TEST_F(ProtoValueConversionTest, WideSchemaTest) {
  std::vector<StructField> fields;
  TypeFactory type_factory;

  for (int i = 0; i < 30000; i++) {
    fields.push_back({absl::StrCat("int64_col_", i), type_factory.get_int64()});
  }

  const StructType* schema = nullptr;
  GOOGLESQL_CHECK_OK(type_factory.MakeStructType(fields, &schema));

  google::protobuf::FileDescriptorProto file_descriptor_proto;
  ConvertTypeToProtoOptions options;
  GOOGLESQL_CHECK_OK(ConvertStructToProto(schema, &file_descriptor_proto, options));

  file_descriptor_proto.set_name("wide_schema");
  SimpleErrorCollector error_collector;

  auto pool = std::make_unique<google::protobuf::DescriptorPool>(
      /*underlay=*/google::protobuf::DescriptorPool::generated_pool());

  const FileDescriptor* result =
      pool->BuildFileCollectingErrors(file_descriptor_proto, &error_collector);

  ABSL_CHECK_NE(result, nullptr) << error_collector.errors();
  ASSERT_EQ(error_collector.errors(), "");

  ASSERT_EQ(result->name(), "wide_schema");
  ASSERT_EQ(result->message_type_count(), 1);
  const auto* descriptor = result->message_type(0);
  ASSERT_EQ(descriptor->field_count(), 30000);
  ASSERT_EQ(descriptor->field(0)->name(), "int64_col_0");
  ASSERT_EQ(descriptor->field(0)->number(), 1);

  ASSERT_EQ(descriptor->field(18998)->name(), "int64_col_18998");
  ASSERT_EQ(descriptor->field(18998)->number(), 18999);

  // Starting from tag_number 19000, we shift up 1000 to respect the reserved
  // range of Protobuf
  ASSERT_EQ(descriptor->field(18999)->name(), "int64_col_18999");
  ASSERT_EQ(descriptor->field(18999)->number(), 19000 + 1000);

  ASSERT_EQ(descriptor->field(29999)->name(), "int64_col_29999");
  ASSERT_EQ(descriptor->field(29999)->number(), 30000 + 1000);
}

TEST_F(ProtoValueConversionTest, TimestampMillis) {
  ConvertTypeToProtoOptions options;
  options.field_format_map[TYPE_TIMESTAMP] = FieldFormat::TIMESTAMP_MILLIS;

  const std::vector<std::pair<std::string, std::string>> tests = {
      {"STRUCT(CAST('1998-09-04 01:23:45.123 UTC' AS TIMESTAMP) AS t)",
       "t:904872225123 "},
      {"STRUCT(CAST('1965-09-04 01:23:45.765 UTC' AS TIMESTAMP) AS t)",
       "t:-136506974235"},
      {"STRUCT(CAST(NULL AS TIMESTAMP) AS t)", ""},
  };

  for (const auto& sql_proto_pair : tests) {
    const std::string& test_expression = sql_proto_pair.first;
    const std::string& expected_proto = sql_proto_pair.second;
    ASSERT_THAT(LiteralValueToProto(test_expression, options),
                IsOkAndHolds(Pointee(EqualsProto(expected_proto))))
        << test_expression;
    ASSERT_TRUE(RoundTripTest(test_expression, options));
  }
}

TEST_F(ProtoValueConversionTest, TimestampNanosLegacyFormat) {
  ConvertTypeToProtoOptions options;
  options.field_format_map[TYPE_TIMESTAMP] = FieldFormat::TIMESTAMP_NANOS;

  const std::vector<std::pair<std::string, std::string>> tests = {
      {"STRUCT(CAST('1998-09-04 01:23:45.123456 UTC' AS TIMESTAMP) AS t)",
       "t:904872225123456000 "},
      {"STRUCT(CAST('1965-09-04 01:23:45.987654 UTC' AS TIMESTAMP) AS t)",
       "t:-136506974012346000"},
      {"STRUCT(CAST(NULL AS TIMESTAMP) AS t)", ""},
  };

  for (const auto& sql_proto_pair : tests) {
    const std::string& test_expression = sql_proto_pair.first;
    const std::string& expected_proto = sql_proto_pair.second;
    ASSERT_THAT(LiteralValueToProto(test_expression, options),
                IsOkAndHolds(Pointee(EqualsProto(expected_proto))))
        << test_expression;
    ASSERT_TRUE(RoundTripTest(test_expression, options));
  }
}

TEST_F(ProtoValueConversionTest, TimestampPicosFormatVariousPrecisions) {
  ConvertTypeToProtoOptions options;
  options.timestamp_format_options.timestamp_format =
      ConvertTypeToProtoOptions::TimestampFormat::kTimestampPicosProto;
  const std::vector<std::string> tests = {
      // Picos precision
      {"STRUCT(CAST('1998-09-04 01:23:45.123456789123 UTC' AS TIMESTAMP) AS "
       "t)"},
      {"STRUCT(TIMESTAMP '1965-09-04 01:23:45.987654321987 UTC')"},
      // Nanos precision
      {"STRUCT(CAST('1998-09-04 01:23:45.123456789 UTC' AS TIMESTAMP) AS t)"},
      {"STRUCT(TIMESTAMP '1965-09-04 01:23:45.987654321 UTC')"},
      // Micros precision
      {"STRUCT(CAST('1998-09-04 01:23:45.123456 UTC' AS TIMESTAMP) AS t)"},
      {"STRUCT(TIMESTAMP '1965-09-04 01:23:45.987654 UTC')"},
      // Array of timestamps in picos, nanos and millis precision
      {"[CAST('1998-09-04 01:23:45.678901123345 UTC' AS TIMESTAMP), "
       "CAST('1998-09-05 01:23:45.678901123345 UTC' AS TIMESTAMP)]"},
      {"[CAST('1998-09-04 01:23:45.678901123 UTC' AS TIMESTAMP), "
       "CAST('1998-09-05 01:23:45.678901123 UTC' AS TIMESTAMP)]"},
      {"[CAST('1998-09-04 01:23:45.678901 UTC' AS TIMESTAMP), CAST('1998-09-05 "
       "01:23:45.678901 UTC' AS TIMESTAMP)]"},
      {"STRUCT(CAST(NULL AS TIMESTAMP) AS t)"}};
  for (const auto& test_expression : tests) {
    ASSERT_TRUE(RoundTripTest(test_expression, options));
  }
}

TEST_F(ProtoValueConversionTest, TimestampOutOfRange) {
  const ConvertTypeToProtoOptions options;
  // We start by generating a Value with a non-null timestamp, and converting
  // it to proto.
  Value value;
  GOOGLESQL_ASSERT_OK(ParseLiteralExpression(
      "STRUCT(CAST('1998-09-04 01:23:45.678901 UTC' AS TIMESTAMP) AS t)",
      &value, options));
  const StructType* struct_type = value.type()->AsStruct();
  ASSERT_TRUE(struct_type != nullptr);
  std::unique_ptr<google::protobuf::Message> proto;
  GOOGLESQL_ASSERT_OK(ValueToProto(value, options, &proto));

  // Now, overwrite the proto with one that has an out of range value for the
  // timestamp field.
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      absl::StrCat("t: ", std::numeric_limits<int64_t>::max()), proto.get()));
  Value result_value;
  EXPECT_THAT(ConvertProtoMessageToStructOrArrayValue(*proto, value.type(),
                                                      &result_value),
              StatusIs(absl::StatusCode::kOutOfRange,
                       HasSubstr("Invalid encoded timestamp")));
}

TEST_F(ProtoValueConversionTest, MatchStructFieldsByName) {
  Value value;
  GOOGLESQL_ASSERT_OK(ParseLiteralExpression("STRUCT<nanos INT32, seconds INT64>(0, 0)",
                                   &value, ConvertTypeToProtoOptions()));
  const StructType* struct_type = value.type()->AsStruct();
  ASSERT_NE(struct_type, nullptr);
  // Confirming & demonstrating field order.
  ASSERT_EQ(struct_type->num_fields(), 2);
  ASSERT_EQ(struct_type->field(0).name, "nanos");
  ASSERT_EQ(struct_type->field(1).name, "seconds");

  google::protobuf::Timestamp proto;
  proto.set_seconds(12345);
  proto.set_nanos(67890);

  Value result_value;
  // Proto field order is (seconds INT64, nanos INT32), where struct field order
  // is swapped.  If we use field order to match, we will try to merge an INT64
  // proto field into an INT32 value type, which won't work.
  EXPECT_THAT(ConvertProtoMessageToStructOrArrayValue(proto, value.type(),
                                                      &result_value),
              StatusIs(absl::StatusCode::kInternal));
  // But if we match fields by name, things should work.
  GOOGLESQL_ASSERT_OK(ConvertProtoMessageToStructOrArrayValue(
      proto, value.type(), &result_value,
      /*match_struct_fields_by_name=*/true));
  EXPECT_EQ(result_value.FindFieldByName("nanos").int32_value(), 67890);
  EXPECT_EQ(result_value.FindFieldByName("seconds").int64_value(), 12345);
}

TEST_F(ProtoValueConversionTest, MatchStructFieldsByName_NamesDontMatch) {
  Value value;
  GOOGLESQL_ASSERT_OK(ParseLiteralExpression("STRUCT<millis INT32, seconds INT64>(0, 0)",
                                   &value, ConvertTypeToProtoOptions()));
  const StructType* struct_type = value.type()->AsStruct();
  ASSERT_NE(struct_type, nullptr);

  google::protobuf::Timestamp proto;
  proto.set_seconds(12345);
  proto.set_nanos(67890);

  Value result_value;
  EXPECT_THAT(ConvertProtoMessageToStructOrArrayValue(
                  proto, value.type(), &result_value,
                  /*match_struct_fields_by_name=*/true),
              StatusIs(absl::StatusCode::kNotFound,
                       HasSubstr("STRUCT field 'millis' not found in proto")));
}

TEST_F(ProtoValueConversionTest, TimestampMicrosInvalidFieldFormat) {
  // Create the proto descriptor.
  google::protobuf::FileDescriptorProto file_proto;
  file_proto.set_name("test.proto");
  google::protobuf::DescriptorProto* message_type = file_proto.add_message_type();
  message_type->set_name("M");
  auto* field_proto = message_type->add_field();
  field_proto->set_name("t");
  field_proto->set_number(1);
  field_proto->set_label(FieldDescriptorProto::LABEL_OPTIONAL);
  field_proto->set_type(FieldDescriptorProto::TYPE_STRING);
  field_proto->mutable_options()->SetExtension(googlesql::format,
                                               FieldFormat::TIMESTAMP_MICROS);

  // Build the proto descriptor and get the target field descriptor.
  google::protobuf::DescriptorPool descriptor_pool(
      google::protobuf::DescriptorPool::generated_pool());
  const google::protobuf::FileDescriptor* file_descriptor =
      descriptor_pool.BuildFile(file_proto);
  ASSERT_THAT(file_descriptor, NotNull()) << file_proto.DebugString();
  const google::protobuf::Descriptor* descriptor =
      file_descriptor->FindMessageTypeByName("M");
  ASSERT_THAT(descriptor, NotNull()) << descriptor->DebugString();
  const google::protobuf::FieldDescriptor* field = descriptor->FindFieldByName("t");
  ASSERT_THAT(field, NotNull()) << descriptor->DebugString();

  // Create a default value for the target type.
  Value value;
  GOOGLESQL_ASSERT_OK(ParseLiteralExpression(
      "CAST('1998-09-04 01:23:45.678901 UTC' AS TIMESTAMP)", &value, {}));

  google::protobuf::DynamicMessageFactory message_factory;
  std::unique_ptr<google::protobuf::Message> proto(
      message_factory.GetPrototype(descriptor)->New());

  // The field is annotated with TIMESTAMP_MICROS, but the field type is
  // string, so an error should be raised.
  EXPECT_THAT(
      MergeValueToProtoField(value, field,
                             /*use_wire_format_annotations=*/false,
                             &message_factory, proto.get()),
      StatusIs(absl::StatusCode::kInternal, HasSubstr("RET_CHECK failure")));
}

TEST_F(ProtoValueConversionTest, InvalidRange) {
  const ConvertTypeToProtoOptions options;
  // We start by generating a Value with a non-null range, and converting
  // it to proto.
  Value value;
  GOOGLESQL_ASSERT_OK(ParseLiteralExpression(
      "STRUCT(RANGE<DATE> '[2022-12-06, 2022-12-07)' AS r)", &value, options));
  const StructType* struct_type = value.type()->AsStruct();
  ASSERT_TRUE(struct_type != nullptr);
  std::unique_ptr<google::protobuf::Message> proto;
  GOOGLESQL_ASSERT_OK(ValueToProto(value, options, &proto));

  // Now, overwrite the proto with one that has an invalid encoded range value
  // for the range field.
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString("r: '1'", proto.get()));
  Value result_value;
  EXPECT_THAT(ConvertProtoMessageToStructOrArrayValue(*proto, value.type(),
                                                      &result_value),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Too few bytes to read RANGE")));
}

// Go through the motions on a non-NULL tokenlist.
TEST_F(ProtoValueConversionTest, TokenListRoundTrip) {
  const Value t = TokenListFromStringArray({"tokenlist"});

  const StructType* struct_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory_.MakeStructType({{"t", type_factory_.get_tokenlist()}},
                                         &struct_type));

  const Value value = Value::Struct(struct_type, {&t, 1});
  const ConvertTypeToProtoOptions options;
  std::unique_ptr<google::protobuf::Message> proto;
  GOOGLESQL_ASSERT_OK(ValueToProto(value, options, &proto));

  Value result_value;
  GOOGLESQL_ASSERT_OK(ConvertProtoMessageToStructOrArrayValue(*proto, value.type(),
                                                    &result_value));
  EXPECT_EQ(result_value.field(0), t);
}

// Verify MergeValueToProtoField using various combinations of destination proto
// field type, label and GoogleSQL format annotation.
class MergeValueToProtoFieldTypeCombinationsTest
    : public ::testing::TestWithParam<::testing::tuple<
          FieldDescriptorProto::Label, FieldDescriptorProto::Type,
          FieldFormat::Format, std::vector<Value>,
          std::string /* expected_proto */>> {
 protected:
  FieldDescriptorProto::Label label() const {
    return ::testing::get<0>(GetParam());
  }
  FieldDescriptorProto::Type type() const {
    return ::testing::get<1>(GetParam());
  }
  FieldFormat::Format format() const { return ::testing::get<2>(GetParam()); }
  const std::vector<Value>& values() const {
    return ::testing::get<3>(GetParam());
  }
  const std::string& expected_proto() const {
    return ::testing::get<4>(GetParam());
  }
};

TEST_P(MergeValueToProtoFieldTypeCombinationsTest, Do) {
  // Create the proto descriptor.
  google::protobuf::FileDescriptorProto file_proto;
  file_proto.set_name("test.proto");
  google::protobuf::DescriptorProto* message_type = file_proto.add_message_type();
  message_type->set_name("M");
  auto* field_proto = message_type->add_field();
  field_proto->set_name("t");
  field_proto->set_number(1);
  field_proto->set_label(label());
  field_proto->set_type(type());
  field_proto->mutable_options()->SetExtension(googlesql::format, format());

  // Build the proto descriptor and get the target field descriptor.
  google::protobuf::DescriptorPool descriptor_pool(
      google::protobuf::DescriptorPool::generated_pool());
  const google::protobuf::FileDescriptor* file_descriptor =
      descriptor_pool.BuildFile(file_proto);
  ASSERT_THAT(file_descriptor, NotNull()) << file_proto.DebugString();
  const google::protobuf::Descriptor* descriptor =
      file_descriptor->FindMessageTypeByName("M");
  ASSERT_THAT(descriptor, NotNull()) << file_descriptor->DebugString();
  const google::protobuf::FieldDescriptor* field = descriptor->FindFieldByName("t");
  ASSERT_THAT(field, NotNull()) << descriptor->DebugString();

  // Make the proto message. We need just one.
  google::protobuf::DynamicMessageFactory message_factory;
  std::unique_ptr<google::protobuf::Message> message(
      message_factory.GetPrototype(descriptor)->New());

  // Convert the input values to proto (merge each value into the message).
  for (const Value& value : values()) {
    GOOGLESQL_ASSERT_OK(
        MergeValueToProtoField(value, field, &message_factory, message.get()));
  }
  ASSERT_THAT(*message, EqualsProto(expected_proto()));

  // Do the reverse translation.
  int index = -1;
  for (const Value& expected_value : values()) {
    index += (label() == FieldDescriptorProto::LABEL_REPEATED) ? 1 : 0;
    Value value;
    GOOGLESQL_ASSERT_OK(ProtoFieldToValue(*message, field, index, expected_value.type(),
                                false, &value));
    ASSERT_EQ(value, expected_value);
  }
}

INSTANTIATE_TEST_SUITE_P(
    Micros, MergeValueToProtoFieldTypeCombinationsTest,
    ::testing::Combine(Values(FieldDescriptorProto::LABEL_OPTIONAL),
                       Values(FieldDescriptorProto::TYPE_UINT64,
                              FieldDescriptorProto::TYPE_INT64),
                       Values(FieldFormat::TIMESTAMP_MICROS),
                       Values(std::vector<Value>{
                           values::TimestampFromUnixMicros(904872225123456)}),
                       Values("t:904872225123456")));

INSTANTIATE_TEST_SUITE_P(
    RepeatedMicros, MergeValueToProtoFieldTypeCombinationsTest,
    ::testing::Combine(Values(FieldDescriptorProto::LABEL_REPEATED),
                       Values(FieldDescriptorProto::TYPE_UINT64,
                              FieldDescriptorProto::TYPE_INT64),
                       Values(FieldFormat::TIMESTAMP_MICROS),
                       Values(std::vector<Value>{
                           values::TimestampFromUnixMicros(904872225123456),
                           values::TimestampFromUnixMicros(904872225123123),
                       }),
                       Values("t:904872225123456 t:904872225123123")));

INSTANTIATE_TEST_SUITE_P(
    Date, MergeValueToProtoFieldTypeCombinationsTest,
    ::testing::Combine(Values(FieldDescriptorProto::LABEL_OPTIONAL),
                       Values(FieldDescriptorProto::TYPE_INT32,
                              FieldDescriptorProto::TYPE_INT64),
                       Values(FieldFormat::DATE),
                       Values(std::vector<Value>{values::Date(2352355)}),
                       Values("t:2352355")));

INSTANTIATE_TEST_SUITE_P(
    RepeatedDate, MergeValueToProtoFieldTypeCombinationsTest,
    ::testing::Combine(Values(FieldDescriptorProto::LABEL_REPEATED),
                       Values(FieldDescriptorProto::TYPE_INT32,
                              FieldDescriptorProto::TYPE_INT64),
                       Values(FieldFormat::DATE),
                       Values(std::vector<Value>{
                           values::Date(2352355),
                           values::Date(2352351),
                       }),
                       Values("t:2352355 t:2352351")));

INSTANTIATE_TEST_SUITE_P(
    DateDecimal, MergeValueToProtoFieldTypeCombinationsTest,
    ::testing::Combine(Values(FieldDescriptorProto::LABEL_OPTIONAL),
                       Values(FieldDescriptorProto::TYPE_INT32,
                              FieldDescriptorProto::TYPE_INT64),
                       Values(FieldFormat::DATE_DECIMAL),
                       Values(std::vector<Value>{values::Date(17898)}),
                       Values("t:20190102")));

INSTANTIATE_TEST_SUITE_P(
    RepeatedDateDecimal, MergeValueToProtoFieldTypeCombinationsTest,
    ::testing::Combine(Values(FieldDescriptorProto::LABEL_REPEATED),
                       Values(FieldDescriptorProto::TYPE_INT32,
                              FieldDescriptorProto::TYPE_INT64),
                       Values(FieldFormat::DATE_DECIMAL),
                       Values(std::vector<Value>{
                           values::Date(17898),
                           values::Date(17900),
                       }),
                       Values("t:20190102 t:20190104")));

INSTANTIATE_TEST_SUITE_P(
    Int32Encodings, MergeValueToProtoFieldTypeCombinationsTest,
    ::testing::Combine(Values(FieldDescriptorProto::LABEL_OPTIONAL),
                       Values(FieldDescriptorProto::TYPE_INT32,
                              FieldDescriptorProto::TYPE_SFIXED32,
                              FieldDescriptorProto::TYPE_SINT32),
                       Values(FieldFormat::DEFAULT_FORMAT),
                       Values(std::vector<Value>{values::Int32(1234567890)}),
                       Values("t:1234567890")));

INSTANTIATE_TEST_SUITE_P(
    Uint32Encodings, MergeValueToProtoFieldTypeCombinationsTest,
    ::testing::Combine(Values(FieldDescriptorProto::LABEL_OPTIONAL),
                       Values(FieldDescriptorProto::TYPE_UINT32,
                              FieldDescriptorProto::TYPE_FIXED32),
                       Values(FieldFormat::DEFAULT_FORMAT),
                       Values(std::vector<Value>{values::Uint32(1234567890)}),
                       Values("t:1234567890")));

INSTANTIATE_TEST_SUITE_P(
    Int64Encodings, MergeValueToProtoFieldTypeCombinationsTest,
    ::testing::Combine(Values(FieldDescriptorProto::LABEL_OPTIONAL),
                       Values(FieldDescriptorProto::TYPE_INT64,
                              FieldDescriptorProto::TYPE_SFIXED64,
                              FieldDescriptorProto::TYPE_SINT64),
                       Values(FieldFormat::DEFAULT_FORMAT),
                       Values(std::vector<Value>{values::Int64(1234567890123)}),
                       Values("t:1234567890123")));

INSTANTIATE_TEST_SUITE_P(
    Uint64Encodings, MergeValueToProtoFieldTypeCombinationsTest,
    ::testing::Combine(Values(FieldDescriptorProto::LABEL_OPTIONAL),
                       Values(FieldDescriptorProto::TYPE_UINT64,
                              FieldDescriptorProto::TYPE_FIXED64),
                       Values(FieldFormat::DEFAULT_FORMAT),
                       Values(std::vector<Value>{
                           values::Uint64(1234567890123)}),
                       Values("t:1234567890123")));

class MergeValueToProtoFieldTest : public ProtoValueConversionTest,
                                   public ::testing::WithParamInterface<bool> {
 protected:
  void SetUp() override {
    ProtoValueConversionTest::SetUp();
    use_wire_format_annotations_ = GetParam();
  }

  bool use_wire_format_annotations_;
  google::protobuf::DescriptorPool descriptor_pool_;
};

INSTANTIATE_TEST_SUITE_P(MergeValueToProtoFieldTest, MergeValueToProtoFieldTest,
                         ::testing::Bool());

TEST_P(MergeValueToProtoFieldTest, SubMessageBehavior) {
  ConvertTypeToProtoOptions options;
  options.generate_nullable_array_wrappers = false;
  options.generate_nullable_element_wrappers = false;
  google::protobuf::DynamicMessageFactory message_factory;

  const ProtoType* proto_type = nullptr;
  GOOGLESQL_ASSERT_OK(
      type_factory_.MakeProtoType(ProtoTypeProto::descriptor(), &proto_type));

  const StructType* struct_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory_.MakeStructType(
      {StructField("proto_field", proto_type)}, &struct_type));

  std::unique_ptr<google::protobuf::Message> proto;

  const google::protobuf::Descriptor* descriptor = nullptr;
  GOOGLESQL_ASSERT_OK(GetProtoDescriptorForType(struct_type, options, &descriptor));

  proto.reset(message_factory.GetPrototype(descriptor)->New());
  const google::protobuf::Reflection* reflection = proto->GetReflection();

  const google::protobuf::FieldDescriptor* proto_field =
      proto->GetDescriptor()->FindFieldByName("proto_field");
  ASSERT_TRUE(proto_field != nullptr);

  // Merge a filled in message.
  ProtoTypeProto proto_value;
  proto_value.set_proto_name("content");
  Value value = values::Proto(proto_type, proto_value);

  GOOGLESQL_ASSERT_OK(MergeValueToProtoField(value, proto_field,
                                   use_wire_format_annotations_,
                                   &message_factory, proto.get()));
  const google::protobuf::Message& result1 = reflection->GetMessage(*proto, proto_field);
  EXPECT_THAT(result1, EqualsProto(proto_value));

  // Merge an empty message.
  proto_value.Clear();
  value = values::Proto(proto_type, proto_value);

  GOOGLESQL_ASSERT_OK(MergeValueToProtoField(value, proto_field,
                                   use_wire_format_annotations_,
                                   &message_factory, proto.get()));
  const google::protobuf::Message& result2 = reflection->GetMessage(*proto, proto_field);
  EXPECT_THAT(result2, EqualsProto(proto_value));

  // Merge a bad (unparseable) message
  Value bad_value = values::Proto(proto_type, absl::Cord("bad_proto_value"));

  EXPECT_THAT(
      MergeValueToProtoField(bad_value, proto_field,
                             use_wire_format_annotations_, &message_factory,
                             proto.get()),
      StatusIs(
          absl::StatusCode::kOutOfRange,
          HasSubstr(
              "Cannot parse value for field: GeneratedProto0.proto_field")));
}

TEST_P(MergeValueToProtoFieldTest, ArrayBehavior) {
  ConvertTypeToProtoOptions options;
  options.generate_nullable_array_wrappers = false;
  options.generate_nullable_element_wrappers = false;
  google::protobuf::DynamicMessageFactory message_factory;

  const StructType* struct_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory_.MakeStructType(
      {StructField("int64_array_field", types::Int64ArrayType())},
      &struct_type));

  std::unique_ptr<google::protobuf::Message> proto;

  const google::protobuf::Descriptor* descriptor = nullptr;
  GOOGLESQL_ASSERT_OK(GetProtoDescriptorForType(struct_type, options, &descriptor));

  proto.reset(message_factory.GetPrototype(descriptor)->New());

  const google::protobuf::FieldDescriptor* int64_array_field =
      proto->GetDescriptor()->FindFieldByName("int64_array_field");
  ASSERT_TRUE(int64_array_field != nullptr);

  // Merge an empty array.
  Value value;
  GOOGLESQL_ASSERT_OK(
      ParseLiteralExpression("CAST([] AS ARRAY<INT64>)", &value, options));
  GOOGLESQL_ASSERT_OK(MergeValueToProtoField(value, int64_array_field,
                                   use_wire_format_annotations_,
                                   &message_factory, proto.get()));
  EXPECT_THAT(*proto, EqualsProto(""));

  // Merge a singular element.
  GOOGLESQL_ASSERT_OK(ParseLiteralExpression("40", &value, options));
  GOOGLESQL_ASSERT_OK(MergeValueToProtoField(value, int64_array_field,
                                   use_wire_format_annotations_,
                                   &message_factory, proto.get()));
  EXPECT_THAT(*proto, EqualsProto("int64_array_field: 40"));

  // Merge an array.
  GOOGLESQL_ASSERT_OK(ParseLiteralExpression("[41, 42]", &value, options));
  GOOGLESQL_ASSERT_OK(MergeValueToProtoField(value, int64_array_field,
                                   use_wire_format_annotations_,
                                   &message_factory, proto.get()));
  EXPECT_THAT(*proto, EqualsProto("int64_array_field: [40, 41, 42]"));

  // Merge a singular element.
  GOOGLESQL_ASSERT_OK(ParseLiteralExpression("43", &value, options));
  GOOGLESQL_ASSERT_OK(MergeValueToProtoField(value, int64_array_field,
                                   use_wire_format_annotations_,
                                   &message_factory, proto.get()));
  EXPECT_THAT(*proto, EqualsProto("int64_array_field: [40, 41, 42, 43]"));
}

TEST_P(MergeValueToProtoFieldTest, GeographyBehavior) {
  ConvertTypeToProtoOptions options;
  options.generate_nullable_array_wrappers = false;
  options.generate_nullable_element_wrappers = false;
  google::protobuf::DynamicMessageFactory message_factory;

  const StructType* struct_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory_.MakeStructType(
      {StructField("geo_field", types::GeographyType())}, &struct_type));

  std::unique_ptr<google::protobuf::Message> proto;

  const google::protobuf::Descriptor* descriptor = nullptr;
  GOOGLESQL_ASSERT_OK(GetProtoDescriptorForType(struct_type, options, &descriptor));

  proto.reset(message_factory.GetPrototype(descriptor)->New());

  const google::protobuf::FieldDescriptor* geo_field =
      proto->GetDescriptor()->FindFieldByName("geo_field");
  ASSERT_TRUE(geo_field != nullptr);

  // A NULL GEOGRAPHY value.
  Value value;
  GOOGLESQL_ASSERT_OK(ParseLiteralExpression("CAST(NULL AS GEOGRAPHY)", &value, options));
  GOOGLESQL_ASSERT_OK(MergeValueToProtoField(value, geo_field,
                                   use_wire_format_annotations_,
                                   &message_factory, proto.get()));
  EXPECT_THAT(*proto, EqualsProto(""));
}

TEST_P(MergeValueToProtoFieldTest, RepeatedGeography) {
  // Prepare a repeated BYTES field with ST_GEOGRAPHY_ENCODED googlesql format.
  google::protobuf::DynamicMessageFactory message_factory;
  std::unique_ptr<google::protobuf::Message> proto;
  google::protobuf::FileDescriptorProto file_proto;
  file_proto.set_name("test.geo.proto");
  google::protobuf::DescriptorProto* message_type = file_proto.add_message_type();
  message_type->set_name("TestGeo");
  FieldDescriptorProto* field = message_type->add_field();
  field->set_name("geo_field");
  field->set_number(1);
  field->set_label(FieldDescriptorProto::LABEL_REPEATED);
  field->set_type(FieldDescriptorProto::TYPE_BYTES);
  field->mutable_options()->SetExtension(googlesql::format,
                                         FieldFormat::ST_GEOGRAPHY_ENCODED);
  const google::protobuf::FileDescriptor* file_descriptor =
      descriptor_pool_.BuildFile(file_proto);
  ASSERT_TRUE(file_descriptor != nullptr);
  const google::protobuf::Descriptor* descriptor = file_descriptor->message_type(0);
  ASSERT_TRUE(descriptor != nullptr);
  proto.reset(message_factory.GetPrototype(descriptor)->New());

  const google::protobuf::FieldDescriptor* geo_field =
      proto->GetDescriptor()->FindFieldByName("geo_field");
  ASSERT_TRUE(geo_field != nullptr);

  // Create a non-NULL GEOGRAPHY value and merge it twice.
}

TEST_P(MergeValueToProtoFieldTest, StructBehavior) {
  ConvertTypeToProtoOptions options;
  options.generate_nullable_array_wrappers = false;
  options.generate_nullable_element_wrappers = false;
  google::protobuf::DynamicMessageFactory message_factory;

  const StructType* empty_struct_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory_.MakeStructType({}, &empty_struct_type));
  const StructType* inner_struct_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory_.MakeStructType(
      {StructField("string_field", types::StringType()),
       StructField("int64_array_field", types::Int64ArrayType()),
       StructField("empty_struct_field", empty_struct_type)},
      &inner_struct_type));
  const ArrayType* struct_array_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory_.MakeArrayType(inner_struct_type, &struct_array_type));

  const StructType* nested_struct_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory_.MakeStructType(
      {StructField("string_field", types::StringType()),
       StructField("int64_array_field", types::Int64ArrayType()),
       StructField("empty_struct_field", empty_struct_type),
       StructField("inner_struct_field", inner_struct_type),
       StructField("struct_array_field", struct_array_type)},
      &nested_struct_type));

  const StructType* container_struct_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory_.MakeStructType(
      {StructField("nested_struct_field", nested_struct_type)},
      &container_struct_type));

  std::unique_ptr<google::protobuf::Message> proto;

  const google::protobuf::Descriptor* descriptor = nullptr;
  GOOGLESQL_ASSERT_OK(
      GetProtoDescriptorForType(container_struct_type, options, &descriptor));

  proto.reset(message_factory.GetPrototype(descriptor)->New());

  const google::protobuf::FieldDescriptor* nested_struct_field =
      proto->GetDescriptor()->FindFieldByName("nested_struct_field");
  ASSERT_TRUE(nested_struct_field != nullptr);

  const std::string nested_struct_type_str =
      nested_struct_type->TypeName(PRODUCT_EXTERNAL);
  Value value;
  GOOGLESQL_ASSERT_OK(ParseLiteralExpression(
      absl::StrCat("CAST(NULL AS ", nested_struct_type_str, ")"), &value,
      options));
  GOOGLESQL_ASSERT_OK(MergeValueToProtoField(value, nested_struct_field,
                                   use_wire_format_annotations_,
                                   &message_factory, proto.get()));
  EXPECT_THAT(*proto, EqualsProto(""));

  GOOGLESQL_ASSERT_OK(ParseLiteralExpression(
      // Need the struct type before the values, or otherwise the type of each
      // field will be treated as INT64.
      absl::StrCat(nested_struct_type_str, "(NULL, NULL, NULL, NULL, NULL)"),
      &value, options));
  GOOGLESQL_ASSERT_OK(MergeValueToProtoField(value, nested_struct_field,
                                   use_wire_format_annotations_,
                                   &message_factory, proto.get()));
  EXPECT_THAT(*proto, EqualsProto("nested_struct_field {}"));
  GOOGLESQL_ASSERT_OK(ParseLiteralExpression(
      "('foo', [1, 2], STRUCT(), ('bar', [], STRUCT()), [])", &value, options));
  GOOGLESQL_ASSERT_OK(MergeValueToProtoField(value, nested_struct_field,
                                   use_wire_format_annotations_,
                                   &message_factory, proto.get()));
  EXPECT_THAT(*proto, EqualsProto(
                          R"pb(
                            nested_struct_field {
                              string_field: "foo"
                              int64_array_field: [ 1, 2 ]
                              empty_struct_field {}
                              inner_struct_field {
                                string_field: "bar"
                                empty_struct_field {}
                              }
                            }
                          )pb"));

  const std::string inner_struct_type_str =
      inner_struct_type->TypeName(PRODUCT_EXTERNAL);
  GOOGLESQL_ASSERT_OK(ParseLiteralExpression(
      // Need the struct type before the values, or otherwise the expression
      // will not be treated as a literal.
      absl::StrCat(nested_struct_type_str,
                   "(NULL, [3], NULL, "
                   "('', [4, 5], STRUCT()), "
                   "[",
                   inner_struct_type_str, "('baz', [6], STRUCT()), ",
                   inner_struct_type_str,
                   "(' ', [7], NULL)"
                   "])"),
      &value, options));
  GOOGLESQL_ASSERT_OK(MergeValueToProtoField(value, nested_struct_field,
                                   use_wire_format_annotations_,
                                   &message_factory, proto.get()));
  EXPECT_THAT(
      *proto,
      EqualsProto(
          R"pb(
            nested_struct_field {
              string_field: "foo"
              int64_array_field: [ 1, 2, 3 ]
              empty_struct_field {}
              inner_struct_field {
                string_field: ""
                int64_array_field: [ 4, 5 ]
                empty_struct_field {}
              }
              struct_array_field {
                string_field: "baz"
                int64_array_field: 6
                empty_struct_field {}
              }
              struct_array_field { string_field: " " int64_array_field: 7 }
            }
          )pb"));
}

TEST_P(MergeValueToProtoFieldTest, EdgeCases) {
  const ConvertTypeToProtoOptions options;
  google::protobuf::DynamicMessageFactory message_factory;

  Value value;
  GOOGLESQL_ASSERT_OK(ParseLiteralExpression(
      "STRUCT('lala' AS string_field, STRUCT(1 AS int_field) AS nested_struct)",
      &value, options));
  const StructType* struct_type = value.type()->AsStruct();
  ASSERT_TRUE(struct_type != nullptr);

  // Construct values for the struct fields.
  Value string_value;
  GOOGLESQL_ASSERT_OK(ParseLiteralExpression("'lala'", &string_value, options));

  Value struct_value;
  GOOGLESQL_ASSERT_OK(
      ParseLiteralExpression("STRUCT(1 AS int_field)", &struct_value, options));

  std::unique_ptr<google::protobuf::Message> proto;

  const google::protobuf::Descriptor* descriptor = nullptr;
  GOOGLESQL_ASSERT_OK(GetProtoDescriptorForType(value.type(), options, &descriptor));

  proto.reset(message_factory.GetPrototype(descriptor)->New());
  const google::protobuf::Reflection* reflection = proto->GetReflection();

  // Merge string_field that should be supported.
  const google::protobuf::FieldDescriptor* string_field =
      proto->GetDescriptor()->FindFieldByName("string_field");
  ASSERT_TRUE(string_field != nullptr);
  GOOGLESQL_ASSERT_OK(MergeValueToProtoField(string_value, string_field,
                                   use_wire_format_annotations_,
                                   &message_factory, proto.get()));
  EXPECT_EQ(reflection->GetString(*proto, string_field), "lala");

  // Merge should fail if the field and message descriptors do not match.
  const google::protobuf::Descriptor* incorrect_descriptor = nullptr;
  GOOGLESQL_ASSERT_OK(GetProtoDescriptorForType(struct_value.type(), options,
                                      &incorrect_descriptor));

  std::unique_ptr<google::protobuf::Message> incorrect_proto;
  incorrect_proto.reset(
      message_factory.GetPrototype(incorrect_descriptor)->New());

  EXPECT_THAT(
      MergeValueToProtoField(struct_value, string_field,
                             use_wire_format_annotations_, &message_factory,
                             incorrect_proto.get()),
      StatusIs(absl::StatusCode::kInternal,
               HasSubstr("Field and output proto descriptors do not match")));
}

}  // namespace
}  // namespace googlesql
