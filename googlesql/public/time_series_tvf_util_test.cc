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

#include "googlesql/public/time_series_tvf_util.h"

#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "google/protobuf/timestamp.pb.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/testing/error_matchers.h"
#include "googlesql/public/type.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/testdata/test_proto3.pb.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/descriptor.h"

namespace googlesql {
namespace {

using ::testing::HasSubstr;

struct ParseSimpleIdentifierPathTestCase {
  std::string name;
  std::string input;
  std::vector<std::string> expected_output;
  std::string expected_error;  // If empty, the test is expected to succeed
};

class ParseSimpleIdentifierPathTest
    : public ::testing::TestWithParam<ParseSimpleIdentifierPathTestCase> {};

TEST_P(ParseSimpleIdentifierPathTest, ParseSimpleIdentifierPath) {
  const auto& test = GetParam();

  // Prepare a non-null-terminated string view of the input to verify scanner
  // boundary safety.
  std::unique_ptr<char[]> input_buf(new char[test.input.size()]());
  std::memcpy(input_buf.get(), test.input.c_str(), test.input.size());
  absl::string_view input_no_null_term(input_buf.get(), test.input.size());

  if (test.expected_error.empty()) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<std::string> result,
                         ParseSimpleIdentifierPath(test.input));
    EXPECT_EQ(result, test.expected_output);

    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<std::string> result_no_term,
                         ParseSimpleIdentifierPath(input_no_null_term));
    EXPECT_EQ(result_no_term, test.expected_output);
  } else {
    EXPECT_THAT(ParseSimpleIdentifierPath(test.input),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr(test.expected_error)));

    EXPECT_THAT(ParseSimpleIdentifierPath(input_no_null_term),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr(test.expected_error)));
  }
}

INSTANTIATE_TEST_SUITE_P(
    ParseSimpleIdentifierPathTests, ParseSimpleIdentifierPathTest,
    ::testing::ValuesIn(std::vector<ParseSimpleIdentifierPathTestCase>{
        // ==========================================
        // Success Cases
        // ==========================================
        // {"name", "input", {"expected_output"}, "expected_error" (empty if
        // success)}
        {"SingleIdentifier", "abc", {"abc"}},
        {"SingleDot", "abc.def", {"abc", "def"}},
        {"MultiDottedPath", "a.b.c", {"a", "b", "c"}},
        {"NumericPathTicks", "abc.`123`", {"abc", "123"}},
        {"NumericPathNoTicks", "abc.123", {"abc", "123"}},
        {"NumericPathBothTicks", "abc.`123`.456", {"abc", "123", "456"}},

        // Keywords are parsed as normal identifiers (not treated as reserved)
        {"UnquotedKeywords", "select.from", {"select", "from"}},
        {"QuotedAndUnquotedKeywords",
         "abc.`select`.from.`table`",
         {"abc", "select", "from", "table"}},

        {"SpacesInBackquotes", "`a c`.def.`g h`", {"a c", "def", "g h"}},
        {"DotInBackquotes", "abc.def.`ghi.jkl`", {"abc", "def", "ghi.jkl"}},
        {"QuotedNonAscii", "`你好，世界`", {"你好，世界"}},
        {"QuotedNonAsciiInner", "abc.`你好，世界`", {"abc", "你好，世界"}},
        {"SlashInQuoted", "`abc/def`", {"abc/def"}},
        {"BackslashInQuotedUnprocessed", "`abc\\def`", {"abc\\def"}},
        {"HexEscapeInQuoted", "`abc\\x2e\\x60`", {"abc\\x2e\\x60"}},

        // ==========================================
        // Failure Cases
        // ==========================================
        // {"name", "input", {"expected_output"} (empty if error),
        // "expected_error"}
        {"NonAsciiUnquoted", "abc.äöü", {}, "invalid character"},
        {"EmptyPath", "", {}, "cannot be empty"},
        {"LeadingDot", ".abc", {}, "cannot begin with '.'"},
        {"TrailingDot", "abc.", {}, "cannot end with '.'"},
        {"EmptyPathComponent", "abc..def", {}, "empty component"},
        {"EmptyQuotedPathComponent", "abc.``", {}, "empty component"},

        // Digits not allowed at the start of the first path component.
        {"PathStartsWithNumber0", "123", {}, "start with a digit, found '1'"},
        {"PathStartsWithNumber1", "123a", {}, "start with a digit, found '1'"},
        {"PathStartsWithNumber2", "123.a", {}, "start with a digit, found '1'"},

        // Unclosed Backticks.
        {"UnmatchedBackquoteStart", "`abc", {}, "unmatched backtick '`'"},
        {"UnmatchedBackquoteMiddle", "abc.`def", {}, "unmatched backtick '`'"},

        // Unquoted slashes and backslashes are disallowed.
        {"SlashInUnquoted", "abc/def", {}, "invalid character '/'"},
        {"BackslashInUnquoted", "abc\\def", {}, "invalid character '\\'"},

        // Backquotes in invalid positions.
        {"BackquoteInMiddleOfSegment",
         "a`b`",
         {},
         "only allowed at the start of a path component"},

        {"BackquoteInMiddleOfSegment2",
         "abc.d`ef`",
         {},
         "only allowed at the start of a path component"},

        {"TextAfterClosingBackquote",
         "`abc`def",
         {},
         "Expected '.' or end of string after closing backtick"},

        {"BackquoteConsecutive",
         "`abc``def`",
         {},
         "Expected '.' or end of string after closing backtick"},

        // Invalid characters not quoted.
        {"AllWhitespace", "     ", {}, "invalid character"},
        {"StarWithinComponent", "abc.123*def", {}, "invalid character"},
        {"PercentStartComponent", "abc.%def", {}, "invalid character"},
        {"PoundEndComponent", "abc.def#", {}, "invalid character"},
    }),
    [](const ::testing::TestParamInfo<ParseSimpleIdentifierPathTest::ParamType>&
           info) { return info.param.name; });

// =====================================================================
// ResolveTimestampColumnPath Tests
// =====================================================================

struct ResolveTimestampColumnPathTestCase {
  std::string name;
  std::string path;
  int expected_column_index = -1;
  struct ExpectedStep {
    TypeFieldPathStep::Kind kind;
    int struct_field_index = -1;
    std::string proto_field_name;
    std::string expected_type_name;
  };
  std::vector<ExpectedStep> expected_steps;
  std::string expected_error;
  absl::StatusCode expected_status_code = absl::StatusCode::kInvalidArgument;

  enum RelationKind {
    SQL_TABLE,
    VALUE_TABLE,
    VALUE_TABLE_SCALAR,
    VALUE_TABLE_PROTO,
    VALUE_TABLE_NESTED_STRUCT,
  };
  RelationKind relation_kind = SQL_TABLE;
};

class ResolveTimestampColumnPathTest
    : public ::testing::TestWithParam<ResolveTimestampColumnPathTestCase> {
 protected:
  // Parameterized tests are initialized at startup before the TypeFactory is
  // constructed, preventing test cases from referencing `const Type*` directly.
  // To work around this, we map type names to Type objects that are
  // initialized during SetUp().
  const Type* GetExpectedType(absl::string_view type_name) const {
    if (type_name == "ts_type") {
      return ts_type_;
    }
    if (type_name == "struct_type") {
      return nested_struct_type_;
    }
    if (type_name == "proto_ts_type") {
      return proto_ts_type_;
    }
    ADD_FAILURE() << "Test '" << GetParam().name
                  << "' has unknown expected type name: " << type_name;
    return nullptr;
  }

  void SetUp() override {
    ts_type_ = type_factory_.get_timestamp();

    GOOGLESQL_ASSERT_OK(type_factory_.MakeProtoType(
        googlesql_test::Proto3KitchenSink::descriptor(),
        &proto3_kitchen_sink_type_));

    GOOGLESQL_ASSERT_OK(type_factory_.MakeProtoType(
        google::protobuf::Timestamp::descriptor(), &proto_ts_type_));

    GOOGLESQL_ASSERT_OK(type_factory_.MakeStructType(
        {{"timestamp_field", ts_type_},
         {"int64_field", type_factory_.get_int64()}},
        &nested_struct_type_));

    GOOGLESQL_ASSERT_OK(type_factory_.MakeStructType(
        {{"int64_field", type_factory_.get_int64()},
         {"timestamp_field", ts_type_},
         {"struct_field", nested_struct_type_},
         {"ambiguous_field", type_factory_.get_int64()},
         {"Ambiguous_Field", ts_type_}},
        &struct_col_type_));

    relations_[ResolveTimestampColumnPathTestCase::SQL_TABLE] =
        std::make_unique<TVFRelation>(TVFRelation::ColumnList{
            {"timestamp_col", ts_type_},
            {"proto_ts_type_col", proto_ts_type_},
            {"struct_col", struct_col_type_},
            {"proto_col", proto3_kitchen_sink_type_},
            {"ambiguous_col", type_factory_.get_int64()},
            {"Ambiguous_Col", ts_type_}});

    // Value Table Relation (Type is STRUCT<timestamp_field TIMESTAMP,
    // int64_field INT64>)
    const StructType* val_row_type = nullptr;
    GOOGLESQL_ASSERT_OK(type_factory_.MakeStructType(
        {{"timestamp_field", ts_type_},
         {"int64_field", type_factory_.get_int64()}},
        &val_row_type));

    relations_[ResolveTimestampColumnPathTestCase::VALUE_TABLE] =
        std::make_unique<TVFRelation>(TVFRelation::ValueTable(val_row_type));

    // Value Table Scalar Relation (Type is TIMESTAMP)
    relations_[ResolveTimestampColumnPathTestCase::VALUE_TABLE_SCALAR] =
        std::make_unique<TVFRelation>(TVFRelation::ValueTable(ts_type_));

    // Value Table Proto Relation (Type is Proto3KitchenSink)
    relations_[ResolveTimestampColumnPathTestCase::VALUE_TABLE_PROTO] =
        std::make_unique<TVFRelation>(
            TVFRelation::ValueTable(proto3_kitchen_sink_type_));

    // Value Table Nested Struct Relation (Type is struct_col_type)
    relations_[ResolveTimestampColumnPathTestCase::VALUE_TABLE_NESTED_STRUCT] =
        std::make_unique<TVFRelation>(
            TVFRelation::ValueTable(struct_col_type_));
  }

  const TVFRelation* GetRelation(
      ResolveTimestampColumnPathTestCase::RelationKind kind) const {
    auto it = relations_.find(kind);
    if (it == relations_.end()) {
      return nullptr;
    }
    return it->second.get();
  }

  TypeFactory type_factory_;
  std::map<ResolveTimestampColumnPathTestCase::RelationKind,
           std::unique_ptr<TVFRelation>>
      relations_;
  const Type* ts_type_ = nullptr;
  const ProtoType* proto3_kitchen_sink_type_ = nullptr;
  const ProtoType* proto_ts_type_ = nullptr;
  const StructType* nested_struct_type_ = nullptr;
  const StructType* struct_col_type_ = nullptr;
};

TEST_P(ResolveTimestampColumnPathTest, ResolveTimestampColumnPath) {
  const auto& test = GetParam();

  // Get the TVFRelation indicated by the test case.
  const TVFRelation* relation = GetRelation(test.relation_kind);
  ASSERT_NE(relation, nullptr)
      << "Relation not found for kind: " << test.relation_kind;

  absl::StatusOr<ResolvedTimestampColumnPath> result =
      ResolveTimestampColumnPath(*relation, test.path);

  if (test.expected_error.empty()) {
    GOOGLESQL_ASSERT_OK(result);
    EXPECT_EQ(result->column_index, test.expected_column_index);

    // Verify the field path steps.
    ASSERT_EQ(result->steps.size(), test.expected_steps.size());
    for (size_t i = 0; i < result->steps.size(); ++i) {
      const auto& actual_step = result->steps[i];
      const auto& expected_step = test.expected_steps[i];
      EXPECT_EQ(actual_step.kind, expected_step.kind);

      if (expected_step.kind == TypeFieldPathStep::STRUCT_FIELD) {
        EXPECT_EQ(actual_step.struct_field_index,
                  expected_step.struct_field_index);
        ASSERT_NE(actual_step.type, nullptr);
        const Type* expected_type =
            GetExpectedType(expected_step.expected_type_name);
        ASSERT_NE(expected_type, nullptr);
        EXPECT_TRUE(actual_step.type->Equals(expected_type));
      } else {
        ASSERT_NE(actual_step.proto_field_descriptor, nullptr);
        EXPECT_EQ(actual_step.proto_field_descriptor->name(),
                  expected_step.proto_field_name);
        EXPECT_EQ(actual_step.type, nullptr);
      }
    }
  } else {
    EXPECT_THAT(result, StatusIs(test.expected_status_code,
                                 HasSubstr(test.expected_error)));
  }
}

// The test cases in ResolveTimestampColumnPathTests run against 5 distinct
// test relations set up in ResolveTimestampColumnPathTest::SetUp():
//
// 1. SQL_TABLE:
//    Columns:
//      - {"timestamp_col", ts_type}
//      - {"proto_ts_type_col", proto_ts_type}
//      - {"struct_col", struct_col_type}  // STRUCT<int64_field INT64,
//                                         //        timestamp_field TIMESTAMP,
//                                         //        struct_field STRUCT<
//                                         //         timestamp_field TIMESTAMP,
//                                         //         int64_field INT64>,
//                                         //        ambiguous_field INT64,
//                                         //        Ambiguous_Field TIMESTAMP>
//      - {"proto_col", proto3_kitchen_sink_type}
//      - {"ambiguous_col", type_factory_.get_int64()}
//      - {"Ambiguous_Col", ts_type}
//
// 2. VALUE_TABLE (STRUCT<timestamp_field TIMESTAMP, int64_field INT64>):
//
// 3. VALUE_TABLE_SCALAR (scalar TIMESTAMP):
//    Tested for failure when attempting identifier path traversals.
//
// 4. VALUE_TABLE_PROTO (PROTO<Proto3KitchenSink>):
//
// 5. VALUE_TABLE_NESTED_STRUCT (struct_col's type):
INSTANTIATE_TEST_SUITE_P(
    ResolveTimestampColumnPathTests, ResolveTimestampColumnPathTest,
    ::testing::ValuesIn(std::vector<ResolveTimestampColumnPathTestCase>{
        // =====================================================================
        // SQL Table: Success Cases
        // =====================================================================

        // Standard googlesql timestamp column.
        {.name = "SqlSimpleTs",
         .path = "timestamp_col",
         .expected_column_index = 0,
         .expected_steps = {},
         .expected_error = "",
         .relation_kind = ResolveTimestampColumnPathTestCase::SQL_TABLE},

        // Standard googlesql timestamp column, case-insensitive.
        {.name = "SqlSimpleTsCaseInsensitive",
         .path = "Timestamp_Col",
         .expected_column_index = 0,
         .expected_steps = {},
         .expected_error = "",
         .relation_kind = ResolveTimestampColumnPathTestCase::SQL_TABLE},

        // Standard proto timestamp column.
        {.name = "SqlSimpleProtoTs",
         .path = "proto_ts_type_col",
         .expected_column_index = 1,
         .expected_steps = {},
         .expected_error = "",
         .relation_kind = ResolveTimestampColumnPathTestCase::SQL_TABLE},

        // Nested struct timestamp field.
        {.name = "SqlNestedStruct",
         .path = "struct_col.timestamp_field",
         .expected_column_index = 2,
         .expected_steps = {{.kind = TypeFieldPathStep::STRUCT_FIELD,
                             .struct_field_index = 1,
                             .expected_type_name = "ts_type"}},
         .expected_error = "",
         .relation_kind = ResolveTimestampColumnPathTestCase::SQL_TABLE},

        // Multiple level nested struct timestamp field.
        {.name = "SqlDoubleNestedStruct",
         .path = "struct_col.struct_field.timestamp_field",
         .expected_column_index = 2,
         .expected_steps = {{.kind = TypeFieldPathStep::STRUCT_FIELD,
                             .struct_field_index = 2,
                             .expected_type_name = "struct_type"},
                            {.kind = TypeFieldPathStep::STRUCT_FIELD,
                             .struct_field_index = 0,
                             .expected_type_name = "ts_type"}},
         .expected_error = "",
         .relation_kind = ResolveTimestampColumnPathTestCase::SQL_TABLE},

        // Nested proto timestamp field.
        {.name = "SqlNestedProto",
         .path = "proto_col.timestamp_wkt",
         .expected_column_index = 3,
         .expected_steps = {{.kind = TypeFieldPathStep::PROTO_FIELD,
                             .proto_field_name = "timestamp_wkt"}},
         .expected_error = "",
         .relation_kind = ResolveTimestampColumnPathTestCase::SQL_TABLE},

        // Proto int64 field with googlesql annotation.
        {.name = "SqlAnnotatedInt64ProtoTs",
         .path = "proto_col.timestamp_millis_format",
         .expected_column_index = 3,
         .expected_steps = {{.kind = TypeFieldPathStep::PROTO_FIELD,
                             .proto_field_name = "timestamp_millis_format"}},
         .expected_error = "",
         .relation_kind = ResolveTimestampColumnPathTestCase::SQL_TABLE},

        // Path containing backtick-quoted segments.
        {.name = "SqlQuotedSegments",
         .path = "`struct_col`.`struct_field`.`timestamp_field`",
         .expected_column_index = 2,
         .expected_steps = {{.kind = TypeFieldPathStep::STRUCT_FIELD,
                             .struct_field_index = 2,
                             .expected_type_name = "struct_type"},
                            {.kind = TypeFieldPathStep::STRUCT_FIELD,
                             .struct_field_index = 0,
                             .expected_type_name = "ts_type"}},
         .expected_error = "",
         .relation_kind = ResolveTimestampColumnPathTestCase::SQL_TABLE},

        // =====================================================================
        // SQL Table: Failure Cases
        // =====================================================================

        // Column does not exist.
        {.name = "SqlColNotFound",
         .path = "non_existent",
         .expected_column_index = -1,
         .expected_steps = {},
         .expected_error = "Unable to resolve 'non_existent' (path "
                           "component #1): Column not found",
         .relation_kind = ResolveTimestampColumnPathTestCase::SQL_TABLE},

        // Column is ambiguous.
        {.name = "SqlAmbiguousCol",
         .path = "ambiguous_col",
         .expected_column_index = -1,
         .expected_steps = {},
         .expected_error = "Unable to resolve 'ambiguous_col' (path "
                           "component #1): Ambiguous column reference",
         .relation_kind = ResolveTimestampColumnPathTestCase::SQL_TABLE},

        // Struct field name not found.
        {.name = "SqlFieldNotFound",
         .path = "struct_col.xyz",
         .expected_column_index = -1,
         .expected_steps = {},
         .expected_error = "Unable to resolve 'xyz' (path component #2): "
                           "Field not found in struct",
         .relation_kind = ResolveTimestampColumnPathTestCase::SQL_TABLE},

        // Leaf type not timestamp.
        {.name = "SqlLeafNotTimestamp",
         .path = "struct_col.int64_field",
         .expected_column_index = -1,
         .expected_steps = {},
         .expected_error =
             "Unable to resolve 'int64_field' (path component #2): Leaf "
             "field type must be TIMESTAMP or google.protobuf.Timestamp",
         .relation_kind = ResolveTimestampColumnPathTestCase::SQL_TABLE},

        // Proto field name not found.
        {.name = "SqlProtoFieldNotFound",
         .path = "proto_col.xyz",
         .expected_column_index = -1,
         .expected_steps = {},
         .expected_error = "Unable to resolve 'xyz' (path component #2): "
                           "Field not found in proto "
                           "googlesql_test.Proto3KitchenSink",
         .relation_kind = ResolveTimestampColumnPathTestCase::SQL_TABLE},

        // Repeated proto field not supported.
        {.name = "SqlRepeatedFieldTraversal",
         .path = "proto_col.repeated_int32_val",
         .expected_column_index = -1,
         .expected_steps = {},
         .expected_error =
             "Unable to resolve 'repeated_int32_val' (path "
             "component #2): Proto repeated field is not supported",
         .relation_kind = ResolveTimestampColumnPathTestCase::SQL_TABLE},

        // Map proto field not supported.
        {.name = "SqlMapFieldTraversal",
         .path = "proto_col.test_map",
         .expected_column_index = -1,
         .expected_steps = {},
         .expected_error = "Unable to resolve 'test_map' (path component #2): "
                           "Proto map field is not supported",
         .relation_kind = ResolveTimestampColumnPathTestCase::SQL_TABLE},

        // Leaf type non-timestamp proto field.
        {.name = "SqlLeafNotTimestampProto",
         .path = "proto_col.nested_value.nested_int64",
         .expected_column_index = -1,
         .expected_steps = {},
         .expected_error =
             "Unable to resolve 'nested_int64' (path component #3): Leaf "
             "field type must be TIMESTAMP or google.protobuf.Timestamp",
         .relation_kind = ResolveTimestampColumnPathTestCase::SQL_TABLE},

        // Struct field name reference ambiguous.
        {.name = "SqlStructFieldAmbiguous",
         .path = "struct_col.ambiguous_field",
         .expected_column_index = -1,
         .expected_steps = {},
         .expected_error = "Unable to resolve 'ambiguous_field' (path "
                           "component #2): Struct field name is ambiguous",
         .relation_kind = ResolveTimestampColumnPathTestCase::SQL_TABLE},

        // Traversing leaf type component.
        {.name = "SqlTraverseLeafType",
         .path = "struct_col.int64_field.timestamp_field",
         .expected_column_index = -1,
         .expected_steps = {},
         .expected_error = "Unable to resolve 'timestamp_field' (path "
                           "component #3): Cannot traverse non-struct/proto "
                           "field",
         .relation_kind = ResolveTimestampColumnPathTestCase::SQL_TABLE},

        // Proto extension syntax is not supported by parser.
        {.name = "SqlProtoExtensionNotSupported",
         .path = "proto_col.(ext)",
         .expected_column_index = -1,
         .expected_steps = {},
         .expected_error = "invalid character '('",
         .relation_kind = ResolveTimestampColumnPathTestCase::SQL_TABLE},

        // =====================================================================
        // Value Table: Success Cases
        // =====================================================================
        // Top-level struct field of a value table.
        {.name = "ValueTableTs",
         .path = "timestamp_field",
         .expected_column_index = 0,
         .expected_steps = {{.kind = TypeFieldPathStep::STRUCT_FIELD,
                             .struct_field_index = 0,
                             .expected_type_name = "ts_type"}},
         .expected_error = "",
         .relation_kind = ResolveTimestampColumnPathTestCase::VALUE_TABLE},

        // Top-level struct field of a value table, case-insensitive.
        {.name = "ValueTableTsCaseInsensitive",
         .path = "Timestamp_Field",
         .expected_column_index = 0,
         .expected_steps = {{.kind = TypeFieldPathStep::STRUCT_FIELD,
                             .struct_field_index = 0,
                             .expected_type_name = "ts_type"}},
         .expected_error = "",
         .relation_kind = ResolveTimestampColumnPathTestCase::VALUE_TABLE},

        // Nested struct field of a value table.
        {.name = "ValueTableNestedStruct",
         .path = "timestamp_field",
         .expected_column_index = 0,
         .expected_steps = {{.kind = TypeFieldPathStep::STRUCT_FIELD,
                             .struct_field_index = 1,
                             .expected_type_name = "ts_type"}},
         .expected_error = "",
         .relation_kind =
             ResolveTimestampColumnPathTestCase::VALUE_TABLE_NESTED_STRUCT},

        // Multiple level nested struct field of a value table.
        {.name = "ValueTableDoubleNestedStruct",
         .path = "struct_field.timestamp_field",
         .expected_column_index = 0,
         .expected_steps = {{.kind = TypeFieldPathStep::STRUCT_FIELD,
                             .struct_field_index = 2,
                             .expected_type_name = "struct_type"},
                            {.kind = TypeFieldPathStep::STRUCT_FIELD,
                             .struct_field_index = 0,
                             .expected_type_name = "ts_type"}},
         .expected_error = "",
         .relation_kind =
             ResolveTimestampColumnPathTestCase::VALUE_TABLE_NESTED_STRUCT},

        // Nested proto field of a value table.
        {.name = "ValueTableNestedProto",
         .path = "timestamp_wkt",
         .expected_column_index = 0,
         .expected_steps = {{.kind = TypeFieldPathStep::PROTO_FIELD,
                             .proto_field_name = "timestamp_wkt"}},
         .expected_error = "",
         .relation_kind =
             ResolveTimestampColumnPathTestCase::VALUE_TABLE_PROTO},

        // Value table proto int64 field with googlesql annotation.
        {.name = "ValueTableAnnotatedInt64ProtoTs",
         .path = "timestamp_millis_format",
         .expected_column_index = 0,
         .expected_steps = {{.kind = TypeFieldPathStep::PROTO_FIELD,
                             .proto_field_name = "timestamp_millis_format"}},
         .expected_error = "",
         .relation_kind =
             ResolveTimestampColumnPathTestCase::VALUE_TABLE_PROTO},

        // =====================================================================
        // Value Table: Failure Cases
        // =====================================================================

        // Value table struct field not found.
        {.name = "ValueTableFieldNotFound",
         .path = "xyz",
         .expected_column_index = -1,
         .expected_steps = {},
         .expected_error = "Unable to resolve 'xyz' (path component #1): "
                           "Field not found in struct",
         .relation_kind = ResolveTimestampColumnPathTestCase::VALUE_TABLE},

        // Value table struct field not found, table name is not supported to be
        // part of the path.
        {.name = "ValueTableNameInPath",
         .path = "my_val_table.xyz",
         .expected_column_index = -1,
         .expected_steps = {},
         .expected_error = "Unable to resolve 'my_val_table' (path component "
                           "#1): Field not found in struct",
         .relation_kind = ResolveTimestampColumnPathTestCase::VALUE_TABLE},

        // Value table has a scalar type.
        {.name = "ValueTableScalarTableNotSupported",
         .path = "my_scalar_table",
         .expected_column_index = -1,
         .expected_steps = {},
         .expected_error = "Value table type must be a struct or proto",
         .expected_status_code = absl::StatusCode::kInternal,
         .relation_kind =
             ResolveTimestampColumnPathTestCase::VALUE_TABLE_SCALAR},

        // Value table struct field name reference ambiguous.
        {.name = "ValueTableStructFieldAmbiguous",
         .path = "ambiguous_field",
         .expected_column_index = -1,
         .expected_steps = {},
         .expected_error = "Unable to resolve 'ambiguous_field' (path "
                           "component #1): Struct field name is ambiguous",
         .relation_kind =
             ResolveTimestampColumnPathTestCase::VALUE_TABLE_NESTED_STRUCT},

        // Traversing value table leaf type component.
        {.name = "ValueTableTraverseLeafType",
         .path = "int64_field.timestamp_field",
         .expected_column_index = -1,
         .expected_steps = {},
         .expected_error = "Unable to resolve 'timestamp_field' (path "
                           "component #2): Cannot traverse non-struct/proto "
                           "field",
         .relation_kind =
             ResolveTimestampColumnPathTestCase::VALUE_TABLE_NESTED_STRUCT},

        // Value table proto field not found.
        {.name = "ValueTableProtoFieldNotFound",
         .path = "xyz",
         .expected_column_index = -1,
         .expected_steps = {},
         .expected_error = "Unable to resolve 'xyz' (path component #1): "
                           "Field not found in proto "
                           "googlesql_test.Proto3KitchenSink",
         .relation_kind =
             ResolveTimestampColumnPathTestCase::VALUE_TABLE_PROTO},

        // Value table repeated proto field not supported.
        {.name = "ValueTableRepeatedFieldNotSupported",
         .path = "repeated_int32_val",
         .expected_column_index = -1,
         .expected_steps = {},
         .expected_error =
             "Unable to resolve 'repeated_int32_val' (path "
             "component #1): Proto repeated field is not supported",
         .relation_kind =
             ResolveTimestampColumnPathTestCase::VALUE_TABLE_PROTO},

        // Traversing map proto field of a value table.
        {.name = "ValueTableMapFieldNotSupported",
         .path = "test_map",
         .expected_column_index = -1,
         .expected_steps = {},
         .expected_error = "Unable to resolve 'test_map' (path component #1): "
                           "Proto map field is not supported",
         .relation_kind =
             ResolveTimestampColumnPathTestCase::VALUE_TABLE_PROTO},

        // Leaf type non-timestamp proto field of a value table.
        {.name = "ValLeafNotTimestampProto",
         .path = "nested_value.nested_int64",
         .expected_column_index = -1,
         .expected_steps = {},
         .expected_error = "Unable to resolve 'nested_int64' (path component "
                           "#2): Leaf field type must be TIMESTAMP or "
                           "google.protobuf.Timestamp",
         .relation_kind =
             ResolveTimestampColumnPathTestCase::VALUE_TABLE_PROTO}}),

    [](const ::testing::TestParamInfo<
        ResolveTimestampColumnPathTest::ParamType>& info) {
      return info.param.name;
    });

}  // namespace
}  // namespace googlesql
