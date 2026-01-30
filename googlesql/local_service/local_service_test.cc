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

#include "googlesql/local_service/local_service.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/base/path.h"
#include "google/protobuf/wrappers.pb.h"
#include "google/protobuf/compiler/importer.h"
#include "google/protobuf/descriptor.pb.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/text_format.h"
#include "googlesql/common/status_payload_utils.h"
#include "googlesql/common/testing/proto_matchers.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/common/testing/testing_proto_util.h"
#include "googlesql/proto/function.pb.h"
#include "googlesql/proto/simple_catalog.pb.h"
#include "googlesql/public/formatter_options.pb.h"
#include "googlesql/public/functions/date_time_util.h"
#include "googlesql/public/parse_resume_location.pb.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/simple_table.pb.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/value.h"
#include "googlesql/public/value.pb.h"
#include "googlesql/resolved_ast/resolved_ast.pb.h"
#include "googlesql/testdata/test_proto3.pb.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/status.h"

namespace googlesql {

using ::googlesql::testing::EqualsProto;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::UnorderedElementsAre;
using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
namespace local_service {

class GoogleSqlLocalServiceImplTest : public ::testing::Test {
 protected:
  void SetUp() override {
    source_tree_ = CreateProtoSourceTree();
    proto_importer_ = std::make_unique<google::protobuf::compiler::Importer>(
        source_tree_.get(), nullptr);
    ASSERT_NE(nullptr, proto_importer_->Import(
                           "googlesql/testdata/test_schema.proto"));
    pool_ = std::make_unique<google::protobuf::DescriptorPool>(proto_importer_->pool());
    // We expect 1, the builtin descriptor pool.
    EXPECT_EQ(1, service_.NumRegisteredDescriptorPools());
    EXPECT_EQ(0, service_.NumRegisteredCatalogs());
    EXPECT_EQ(0, service_.NumSavedPreparedExpression());
    EXPECT_EQ(0, service_.NumSavedPreparedQueries());
    EXPECT_EQ(0, service_.NumSavedPreparedModifies());
  }

  void TearDown() override {
    // We expect 1, the builtin descriptor pool.
    EXPECT_EQ(1, service_.NumRegisteredDescriptorPools());
    EXPECT_EQ(0, service_.NumRegisteredCatalogs());
    EXPECT_EQ(0, service_.NumSavedPreparedExpression());
    EXPECT_EQ(0, service_.NumSavedPreparedQueries());
    EXPECT_EQ(0, service_.NumSavedPreparedModifies());
  }

  absl::Status Prepare(const PrepareRequest& request,
                       PrepareResponse* response) {
    return service_.Prepare(request, response);
  }

  absl::Status PrepareQuery(const PrepareQueryRequest& request,
                            PrepareQueryResponse* response) {
    return service_.PrepareQuery(request, response);
  }

  absl::Status PrepareModify(const PrepareModifyRequest& request,
                             PrepareModifyResponse* response) {
    return service_.PrepareModify(request, response);
  }

  absl::Status Unprepare(int64_t id) { return service_.Unprepare(id); }

  absl::Status UnprepareQuery(int64_t id) {
    return service_.UnprepareQuery(id);
  }

  absl::Status UnprepareModify(int64_t id) {
    return service_.UnprepareModify(id);
  }

  absl::Status Evaluate(const EvaluateRequest& request,
                        EvaluateResponse* response) {
    return service_.Evaluate(request, response);
  }

  absl::Status EvaluateQuery(const EvaluateQueryRequest& request,
                             EvaluateQueryResponse* response) {
    return service_.EvaluateQuery(request, response);
  }

  absl::Status EvaluateModify(const EvaluateModifyRequest& request,
                              EvaluateModifyResponse* response) {
    return service_.EvaluateModify(request, response);
  }

  absl::Status Analyze(const AnalyzeRequest& request,
                       AnalyzeResponse* response) {
    return service_.Analyze(request, response);
  }

  absl::Status Parse(const ParseRequest& request, ParseResponse* response) {
    return service_.Parse(request, response);
  }

  absl::Status BuildSql(const BuildSqlRequest& request,
                        BuildSqlResponse* response) {
    return service_.BuildSql(request, response);
  }

  absl::Status ExtractTableNamesFromStatement(
      const ExtractTableNamesFromStatementRequest& request,
      ExtractTableNamesFromStatementResponse* response) {
    return service_.ExtractTableNamesFromStatement(request, response);
  }

  absl::Status ExtractTableNamesFromNextStatement(
      const ExtractTableNamesFromNextStatementRequest& request,
      ExtractTableNamesFromNextStatementResponse* response) {
    return service_.ExtractTableNamesFromNextStatement(request, response);
  }

  absl::Status FormatSql(const FormatSqlRequest& request,
                         FormatSqlResponse* response) {
    return service_.FormatSql(request, response);
  }

  absl::Status LenientFormatSql(const FormatSqlRequest& request,
                                FormatSqlResponse* response) {
    return service_.LenientFormatSql(request, response);
  }

  absl::Status RegisterCatalog(const RegisterCatalogRequest& request,
                               RegisterResponse* response) {
    return service_.RegisterCatalog(request, response);
  }

  absl::Status UnregisterCatalog(int64_t id) {
    return service_.UnregisterCatalog(id);
  }

  size_t NumSavedPreparedExpression() {
    return service_.NumSavedPreparedExpression();
  }

  size_t NumSavedPreparedQueries() {
    return service_.NumSavedPreparedQueries();
  }

  size_t NumSavedPreparedModifies() {
    return service_.NumSavedPreparedModifies();
  }

  absl::Status GetTableFromProto(const TableFromProtoRequest& request,
                                 SimpleTableProto* response) {
    return service_.GetTableFromProto(request, response);
  }

  absl::Status GetBuiltinFunctions(
      const GoogleSQLBuiltinFunctionOptionsProto& proto,
      GetBuiltinFunctionsResponse* response) {
    return service_.GetBuiltinFunctions(proto, response);
  }

  GoogleSqlLocalServiceImpl service_;
  std::unique_ptr<google::protobuf::compiler::DiskSourceTree> source_tree_;
  std::unique_ptr<google::protobuf::compiler::Importer> proto_importer_;
  std::unique_ptr<google::protobuf::DescriptorPool> pool_;
  TypeFactory factory_;
};

void AddEmptyFileDescriptorSet(DescriptorPoolListProto* list) {
  list->add_definitions()->mutable_file_descriptor_set();
}

void AddBuiltin(DescriptorPoolListProto* list) {
  list->add_definitions()->mutable_builtin();
}

void AddRegisteredDescriptorPool(DescriptorPoolListProto* list, int64_t id) {
  list->add_definitions()->set_registered_id(id);
}

void AddKitchenSinkDescriptorPool(DescriptorPoolListProto* list) {
  TypeFactory factory;
  const ProtoType* proto_type = nullptr;
  GOOGLESQL_CHECK_OK(factory.MakeProtoType(googlesql_test::KitchenSinkPB::descriptor(),
                                 &proto_type));
  TypeProto ignored;

  GOOGLESQL_CHECK_OK(proto_type->SerializeToProtoAndFileDescriptors(
      &ignored, list->add_definitions()->mutable_file_descriptor_set()));
}

void AddKitchenSink3DescriptorPool(DescriptorPoolListProto* list) {
  TypeFactory factory;
  const ProtoType* proto_type = nullptr;
  GOOGLESQL_CHECK_OK(factory.MakeProtoType(
      googlesql_test::Proto3KitchenSink::descriptor(), &proto_type));
  TypeProto ignored;

  GOOGLESQL_CHECK_OK(proto_type->SerializeToProtoAndFileDescriptors(
      &ignored, list->add_definitions()->mutable_file_descriptor_set()));
}

void AddTestTable(SimpleTableProto* table, absl::string_view table_name) {
  table->set_name(table_name);
  SimpleColumnProto* col1 = table->add_column();
  col1->set_name("column_str");
  col1->mutable_type()->set_type_kind(TYPE_STRING);
  SimpleColumnProto* col2 = table->add_column();
  col2->set_name("column_bool");
  col2->mutable_type()->set_type_kind(TYPE_BOOL);
  SimpleColumnProto* col3 = table->add_column();
  col3->set_name("column_int");
  col3->mutable_type()->set_type_kind(TYPE_INT32);
}

void InsertTestTableContent(
    google::protobuf::Map<std::string, ::googlesql::local_service::TableContent>*
        tables_contents,
    absl::string_view table_name) {
  TableContent table_content;
  TableData* table_data = table_content.mutable_table_data();

  TableData_Row* table_data_row_1 = table_data->add_row();
  table_data_row_1->add_cell()->set_string_value("string_1");
  table_data_row_1->add_cell()->set_bool_value(true);
  table_data_row_1->add_cell()->set_int32_value(123);

  TableData_Row* table_data_row_2 = table_data->add_row();
  table_data_row_2->add_cell()->set_string_value("string_2");
  table_data_row_2->add_cell()->set_bool_value(true);
  table_data_row_2->add_cell()->set_int32_value(321);

  (*tables_contents)[table_name] = table_content;
}

void InsertEmptyTestTableContent(
    google::protobuf::Map<std::string, ::googlesql::local_service::TableContent>*
        tables_contents,
    absl::string_view table_name) {
  TableContent table_content;
  table_content.mutable_table_data();

  (*tables_contents)[table_name] = table_content;
}

TEST_F(GoogleSqlLocalServiceImplTest, PrepareCleansUpPoolsAndCatalogsOnError) {
  PrepareRequest request;
  PrepareResponse response;

  request.set_sql("1");

  // Fails on deserializing 3rd pool
  AddBuiltin(request.mutable_descriptor_pool_list());
  AddKitchenSinkDescriptorPool(request.mutable_descriptor_pool_list());
  AddRegisteredDescriptorPool(request.mutable_descriptor_pool_list(), -5);

  ASSERT_FALSE(Prepare(request, &response).ok());
  // Fix descriptor pools
  request.mutable_descriptor_pool_list()->Clear();
  AddBuiltin(request.mutable_descriptor_pool_list());
  AddKitchenSinkDescriptorPool(request.mutable_descriptor_pool_list());

  // Bad catalog id
  request.set_registered_catalog_id(-1);
  ASSERT_FALSE(Prepare(request, &response).ok());

  // Fix bad catalog
  request.clear_registered_catalog_id();
  request.mutable_simple_catalog()->add_constant();
  ASSERT_FALSE(Prepare(request, &response).ok());

  // make the catalog valid again
  request.mutable_simple_catalog()->clear_constant();

  request.set_sql("foo");
  ASSERT_FALSE(Prepare(request, &response).ok());

  // No prepared state saved on failure.
  EXPECT_EQ(0, NumSavedPreparedExpression());

  request.set_sql("foo + @bar");
  auto* param = request.mutable_options()->add_query_parameters();
  param->set_name("bar");
  param->mutable_type()->set_type_kind(TYPE_INT64);

  ASSERT_FALSE(Prepare(request, &response).ok());
  EXPECT_EQ(0, NumSavedPreparedExpression());

  auto* column = request.mutable_options()->add_expression_columns();
  column->set_name("foo");
  column->mutable_type()->set_type_kind(TYPE_STRING);

  ASSERT_FALSE(Prepare(request, &response).ok());
  EXPECT_EQ(0, NumSavedPreparedExpression());
}

TEST_F(GoogleSqlLocalServiceImplTest, EvaluateCleansUpPoolsOnError) {
  EvaluateRequest request;
  EvaluateResponse response;

  request.set_sql("1");

  // Fails on deserializing 3rd pool
  AddBuiltin(request.mutable_descriptor_pool_list());
  AddKitchenSinkDescriptorPool(request.mutable_descriptor_pool_list());
  AddRegisteredDescriptorPool(request.mutable_descriptor_pool_list(), -5);

  ASSERT_FALSE(Evaluate(request, &response).ok());
  // Fix descriptor pools
  request.mutable_descriptor_pool_list()->Clear();
  AddBuiltin(request.mutable_descriptor_pool_list());
  AddKitchenSinkDescriptorPool(request.mutable_descriptor_pool_list());

  // No prepared state saved on failure.
  EXPECT_EQ(0, NumSavedPreparedExpression());

  request.set_sql("foo + @bar");
  auto* param = request.mutable_options()->add_query_parameters();
  param->set_name("bar");
  param->mutable_type()->set_type_kind(TYPE_INT64);

  ASSERT_FALSE(Evaluate(request, &response).ok());
  EXPECT_EQ(0, NumSavedPreparedExpression());

  auto* column = request.mutable_options()->add_expression_columns();
  column->set_name("foo");
  column->mutable_type()->set_type_kind(TYPE_STRING);

  ASSERT_FALSE(Evaluate(request, &response).ok());
  EXPECT_EQ(0, NumSavedPreparedExpression());
}

TEST_F(GoogleSqlLocalServiceImplTest, EvaluateWithWrongId) {
  EvaluateRequest evaluate_request;
  evaluate_request.set_prepared_expression_id(12345);

  EvaluateResponse evaluate_response;
  ASSERT_FALSE(Evaluate(evaluate_request, &evaluate_response).ok());
  // No prepared state saved on failure.
  EXPECT_EQ(0, NumSavedPreparedExpression());
}

TEST_F(GoogleSqlLocalServiceImplTest, EvaluateFailuresNoRegister) {
  EvaluateRequest request;
  EvaluateResponse response;

  request.set_sql("foo");
  ASSERT_FALSE(Evaluate(request, &response).ok());
  // No prepared state saved on failure.
  EXPECT_EQ(0, NumSavedPreparedExpression());

  request.set_sql("foo + @bar");
  auto* param = request.add_params();
  param->set_name("bar");
  param->mutable_value()->set_int64_value(1);

  ASSERT_FALSE(Evaluate(request, &response).ok());
  EXPECT_EQ(0, NumSavedPreparedExpression());

  auto* column = request.add_columns();
  column->set_name("foo");
  column->mutable_value()->set_string_value("");

  ASSERT_FALSE(Evaluate(request, &response).ok());
  EXPECT_EQ(0, NumSavedPreparedExpression());
}

TEST_F(GoogleSqlLocalServiceImplTest, UnprepareUnknownId) {
  ASSERT_FALSE(Unprepare(10086).ok());
}

TEST_F(GoogleSqlLocalServiceImplTest, TableFromProto) {
  TableFromProtoRequest request;
  SimpleTableProto response;
  const ProtoType* proto_type;
  TypeFactory factory;
  TypeProto proto;
  google::protobuf::FileDescriptorSet descriptor_set;

  GOOGLESQL_CHECK_OK(factory.MakeProtoType(googlesql_test::TestSQLTable::descriptor(),
                                 &proto_type));
  GOOGLESQL_CHECK_OK(
      proto_type->SerializeToProtoAndFileDescriptors(&proto, &descriptor_set));
  *request.mutable_file_descriptor_set() = descriptor_set;
  *request.mutable_proto() = proto.proto_type();
  ASSERT_TRUE(GetTableFromProto(request, &response).ok());
  EXPECT_EQ("TestSQLTable", response.name());
  EXPECT_FALSE(response.is_value_table());
  EXPECT_EQ(2, response.column_size());
  const SimpleColumnProto column1 = response.column(0);
  EXPECT_EQ("f1", column1.name());
  EXPECT_EQ(TYPE_INT32, column1.type().type_kind());
  const SimpleColumnProto column2 = response.column(1);
  EXPECT_EQ("f2", column2.name());
  EXPECT_EQ(TYPE_INT32, column2.type().type_kind());
}

TEST_F(GoogleSqlLocalServiceImplTest, NonGoogleSQLTableFromProto) {
  TableFromProtoRequest request;
  SimpleTableProto response;
  const ProtoType* proto_type;
  TypeFactory factory;
  GOOGLESQL_CHECK_OK(factory.MakeProtoType(googlesql_test::KitchenSinkPB::descriptor(),
                                 &proto_type));
  TypeProto proto;
  google::protobuf::FileDescriptorSet descriptor_set;
  GOOGLESQL_CHECK_OK(
      proto_type->SerializeToProtoAndFileDescriptors(&proto, &descriptor_set));
  *request.mutable_file_descriptor_set() = descriptor_set;
  *request.mutable_proto() = proto.proto_type();
  ASSERT_TRUE(GetTableFromProto(request, &response).ok());
  SimpleTableProto expected;
  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        name: "KitchenSinkPB"
        is_value_table: true
        column {
          name: "value"
          type {
            type_kind: TYPE_PROTO
            proto_type {
              proto_name: "googlesql_test.KitchenSinkPB"
              proto_file_name: "googlesql/testdata/test_schema.proto"
            }
          }
          is_pseudo_column: false
          has_default_value: false
        }
      )pb",
      &expected));
  EXPECT_EQ(expected.DebugString(), response.DebugString());
}

TEST_F(GoogleSqlLocalServiceImplTest, BadTableFromProto) {
  TableFromProtoRequest request;
  SimpleTableProto response;
  const ProtoType* proto_type;
  TypeFactory factory;
  GOOGLESQL_CHECK_OK(factory.MakeProtoType(googlesql_test::KitchenSinkPB::descriptor(),
                                 &proto_type));
  TypeProto proto;
  google::protobuf::FileDescriptorSet descriptor_set;
  GOOGLESQL_CHECK_OK(
      proto_type->SerializeToProtoAndFileDescriptors(&proto, &descriptor_set));
  *request.mutable_proto() = proto.proto_type();
  absl::Status status = GetTableFromProto(request, &response);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ("Proto type name not found: googlesql_test.KitchenSinkPB",
            status.message());
  EXPECT_EQ(response.ByteSizeLong(), 0);

  request.mutable_proto()->set_proto_file_name("unmatched_file_name");
  *request.mutable_file_descriptor_set() = descriptor_set;
  status = GetTableFromProto(request, &response);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(absl::StrCat("Proto googlesql_test.KitchenSinkPB found in ",
                         "googlesql/testdata/test_schema.proto",
                         ", not unmatched_file_name as specified."),
            status.message());
}

TEST_F(GoogleSqlLocalServiceImplTest, AnalyzeWrongCatalogId) {
  AnalyzeRequest request;
  request.set_registered_catalog_id(12345);

  AnalyzeResponse response;
  absl::Status status = Analyze(request, &response);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ("generic::invalid_argument: Registered catalog 12345 unknown.",
            internal::StatusToString(status));
}

TEST_F(GoogleSqlLocalServiceImplTest, ExtractTableNamesFromStatement) {
  ExtractTableNamesFromStatementRequest request;
  request.set_sql_statement("select count(1) from foo.bar;");

  ExtractTableNamesFromStatementResponse response;
  GOOGLESQL_ASSERT_OK(ExtractTableNamesFromStatement(request, &response));
  ExtractTableNamesFromStatementResponse expectedResponse;
  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(R"pb(table_name {
                                                    table_name_segment: "foo"
                                                    table_name_segment: "bar"
                                                  })pb", &expectedResponse));
  EXPECT_THAT(response, EqualsProto(expectedResponse));
}

TEST_F(GoogleSqlLocalServiceImplTest, ExtractTableNamesFromScript) {
  ExtractTableNamesFromStatementRequest request;
  request.set_sql_statement(
      "select count(1) from foo.bar; select count(1) from x.y.z");
  request.set_allow_script(true);
  ExtractTableNamesFromStatementResponse response;
  GOOGLESQL_ASSERT_OK(ExtractTableNamesFromStatement(request, &response));
  ExtractTableNamesFromStatementResponse expectedResponse;
  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(
      R"pb(table_name { table_name_segment: "foo" table_name_segment: "bar" }
           table_name {
             table_name_segment: "x"
             table_name_segment: "y"
             table_name_segment: "z"
           })pb",
      &expectedResponse));
  EXPECT_THAT(response, EqualsProto(expectedResponse));
}

TEST_F(GoogleSqlLocalServiceImplTest, ExtractTableNamesFromFirstStatement) {
  ExtractTableNamesFromNextStatementRequest request;
  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(
      R"pb(parse_resume_location {
             input: "select count(1) from foo.bar; select id from baz;"
           })pb",
      &request));

  ExtractTableNamesFromNextStatementResponse response;
  GOOGLESQL_ASSERT_OK(ExtractTableNamesFromNextStatement(request, &response));
  ExtractTableNamesFromNextStatementResponse expectedResponse;
  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(
      R"pb(table_name {
             table_name_segment: "foo"
             table_name_segment: "bar"
           }
           resume_byte_position: 29)pb",
      &expectedResponse));
  EXPECT_THAT(response, EqualsProto(expectedResponse));
}

TEST_F(GoogleSqlLocalServiceImplTest, ExtractWithUnsupportedStatement) {
  ExtractTableNamesFromNextStatementRequest request;
  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(
      R"pb(parse_resume_location {
             input: "CREATE TABLE test AS SELECT COUNT(1) FROM foo.bar;"
             byte_position: 0
           })pb",
      &request));

  ExtractTableNamesFromNextStatementResponse response;
  absl::Status status = ExtractTableNamesFromNextStatement(request, &response);
  EXPECT_THAT(status,
              ::absl_testing::StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(GoogleSqlLocalServiceImplTest, ExtractWithWrongStatementSupported) {
  ExtractTableNamesFromNextStatementRequest request;
  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(
      R"pb(parse_resume_location {
             input: "CREATE TABLE test AS SELECT COUNT(1) FROM foo.bar;"
             byte_position: 0
           }
           options {
             supported_statement_kinds: RESOLVED_CONSTANT
           }
           )pb",
      &request));

  ExtractTableNamesFromNextStatementResponse response;
  absl::Status status = ExtractTableNamesFromNextStatement(request, &response);
  EXPECT_THAT(status,
              ::absl_testing::StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(GoogleSqlLocalServiceImplTest, ExtractWithAllStatementsSupported) {
  ExtractTableNamesFromNextStatementRequest request;
  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(
      R"pb(parse_resume_location {
             input: "CREATE TABLE test AS SELECT COUNT(1) FROM foo.bar;"
             byte_position: 0
           }
           options {}
           )pb",
      &request));

  ExtractTableNamesFromNextStatementResponse response;
  GOOGLESQL_ASSERT_OK(ExtractTableNamesFromNextStatement(request, &response));
  ExtractTableNamesFromNextStatementResponse expectedResponse;
  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(
      R"pb(table_name {
             table_name_segment: "foo"
             table_name_segment: "bar"
           }
           resume_byte_position: 50)pb",
      &expectedResponse));
  EXPECT_THAT(response, EqualsProto(expectedResponse));
}

TEST_F(GoogleSqlLocalServiceImplTest, ExtractTableNamesFromEmptyStatement) {
  ExtractTableNamesFromNextStatementRequest request;
  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(
      R"pb(parse_resume_location {
             input: "select count(1) from foo.bar;   "
             byte_position: 29
           })pb",
      &request));

  ExtractTableNamesFromNextStatementResponse response;
  absl::Status status = ExtractTableNamesFromNextStatement(request, &response);
  ASSERT_FALSE(status.ok());
  EXPECT_EQ("Syntax error: Unexpected end of statement [at 1:30]",
            status.message());
}

TEST_F(GoogleSqlLocalServiceImplTest, ExtractWithBigResumePosition) {
  ExtractTableNamesFromNextStatementRequest request;
  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(
      R"pb(parse_resume_location {
             input: "select count(1) from foo.bar;"
             byte_position: 9000
           })pb",
      &request));

  ExtractTableNamesFromNextStatementResponse response;
  absl::Status status = ExtractTableNamesFromNextStatement(request, &response);
  ASSERT_FALSE(status.ok());
  EXPECT_THAT(status, ::absl_testing::StatusIs(absl::StatusCode::kInternal));
}

TEST_F(GoogleSqlLocalServiceImplTest, ExtractWithNegativeResumePosition) {
  ExtractTableNamesFromNextStatementRequest request;
  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(
      R"pb(parse_resume_location {
             input: "select count(1) from foo.bar;"
             byte_position: -1
           })pb",
      &request));

  ExtractTableNamesFromNextStatementResponse response;
  absl::Status status = ExtractTableNamesFromNextStatement(request, &response);
  ASSERT_FALSE(status.ok());
  EXPECT_THAT(status, ::absl_testing::StatusIs(absl::StatusCode::kInternal));
}

TEST_F(GoogleSqlLocalServiceImplTest, ExtractTableNamesFromNextStatement) {
  ExtractTableNamesFromNextStatementRequest request;
  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(
      R"pb(parse_resume_location {
             input: "select count(1) from foo.bar; select id from baz;"
             byte_position: 29
           })pb",
      &request));

  ExtractTableNamesFromNextStatementResponse response;
  GOOGLESQL_ASSERT_OK(ExtractTableNamesFromNextStatement(request, &response));
  ExtractTableNamesFromNextStatementResponse expectedResponse;
  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(
      R"pb(table_name {
             table_name_segment: "baz"
           }
           resume_byte_position: 49)pb",
      &expectedResponse));
  EXPECT_THAT(response, EqualsProto(expectedResponse));
}

TEST_F(GoogleSqlLocalServiceImplTest, ExtractTableNamesWithNoSemicolon) {
  ExtractTableNamesFromNextStatementRequest request;
  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(
      R"pb(parse_resume_location {
             input: "select count(1) from foo.bar; select id from baz"
             byte_position: 29
           })pb",
      &request));

  ExtractTableNamesFromNextStatementResponse response;
  GOOGLESQL_ASSERT_OK(ExtractTableNamesFromNextStatement(request, &response));
  ExtractTableNamesFromNextStatementResponse expectedResponse;
  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(
      R"pb(table_name {
             table_name_segment: "baz"
           }
           resume_byte_position: 48)pb",
      &expectedResponse));
  EXPECT_THAT(response, EqualsProto(expectedResponse));
}

TEST_F(GoogleSqlLocalServiceImplTest, UnregisterWrongCatalogId) {
  absl::Status status = UnregisterCatalog(12345);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ("generic::invalid_argument: Unknown catalog ID: 12345",
            internal::StatusToString(status));
}

TEST_F(GoogleSqlLocalServiceImplTest, Parse) {
  ParseRequest request;
  request.set_sql_statement("select 9");
  request.set_allow_script(false);
  ParseResponse response;
  GOOGLESQL_ASSERT_OK(Parse(request, &response));

  ParseResponse expectedResponse;
  google::protobuf::TextFormat::ParseFromString(
      R"pb(parsed_statement {
             ast_query_statement_node {
               parent {
                 parent {
                   parse_location_range { filename: "" start: 0 end: 8 }
                 }
               }
               query {
                 parent {
                   parent {
                     parse_location_range { filename: "" start: 0 end: 8 }
                   }
                   parenthesized: false
                 }
                 query_expr {
                   ast_select_node {
                     parent {
                       parent {
                         parse_location_range { filename: "" start: 0 end: 8 }
                       }
                       parenthesized: false
                     }
                     distinct: false
                     select_list {
                       parent {
                         parse_location_range { filename: "" start: 7 end: 8 }
                       }
                       columns {
                         parent {
                           parse_location_range { filename: "" start: 7 end: 8 }
                         }
                         expression {
                           ast_leaf_node {
                             ast_printable_leaf_node {
                               ast_int_literal_node {
                                 parent {
                                   parent {
                                     parent {
                                       parent {
                                         parse_location_range {
                                           filename: ""
                                           start: 7
                                           end: 8
                                         }
                                       }
                                       parenthesized: false
                                     }
                                   }
                                   image: "9"
                                 }
                               }
                             }
                           }
                         }
                       }
                     }
                   }
                 }
                 is_nested: false
                 is_pivot_input: false
               }
             }
           })pb",
      &expectedResponse);
  EXPECT_THAT(response, EqualsProto(expectedResponse));

  ParseRequest request2;
  request2.set_allow_script(false);
  ParseResumeLocationProto* parse_resume_location2 =
      request2.mutable_parse_resume_location();
  parse_resume_location2->set_input("select 9;select 10;");
  parse_resume_location2->set_byte_position(0);
  ParseResponse response2;
  GOOGLESQL_EXPECT_OK(Parse(request2, &response2));
  EXPECT_EQ(9, response2.resume_byte_position());

  ParseRequest request3;
  request3.set_allow_script(false);
  ParseResumeLocationProto* parse_resume_location3 =
      request3.mutable_parse_resume_location();
  parse_resume_location3->set_input("select 9;select 10;");
  parse_resume_location3->set_byte_position(response2.resume_byte_position());
  ParseResponse response3;
  GOOGLESQL_EXPECT_OK(Parse(request3, &response3));
  EXPECT_EQ(19, response3.resume_byte_position());
}

TEST_F(GoogleSqlLocalServiceImplTest, ParseScript) {
  const std::string script_content = R"(
    DECLARE variable INT64;
    SET variable = 1;
  )";

  ParseRequest request;
  request.set_sql_statement(script_content);
  request.set_allow_script(true);
  ParseResponse response;
  GOOGLESQL_ASSERT_OK(Parse(request, &response));

  ParseResponse expectedResponse;
  google::protobuf::TextFormat::ParseFromString(
      R"pb(parsed_script {
             parent { parse_location_range { filename: "" start: 5 end: 50 } }
             statement_list_node {
               parent { parse_location_range { filename: "" start: 5 end: 50 } }
               statement_list {
                 ast_script_statement_node {
                   ast_variable_declaration_node {
                     parent {
                       parent {
                         parent {
                           parse_location_range {
                             filename: ""
                             start: 5
                             end: 27
                           }
                         }
                       }
                     }
                     variable_list {
                       parent {
                         parse_location_range { filename: "" start: 13 end: 21 }
                       }
                       identifier_list {
                         parent {
                           parent {
                             parse_location_range {
                               filename: ""
                               start: 13
                               end: 21
                             }
                           }
                           parenthesized: false
                         }
                         id_string: "variable"
                         is_quoted: false
                       }
                     }
                     type {
                       ast_simple_type_node {
                         parent {
                           parent {
                             parse_location_range {
                               filename: ""
                               start: 22
                               end: 27
                             }
                           }
                         }
                         type_name {
                           parent {
                             parent {
                               parent {
                                 parse_location_range {
                                   filename: ""
                                   start: 22
                                   end: 27
                                 }
                               }
                               parenthesized: false
                             }
                           }
                           names {
                             parent {
                               parent {
                                 parse_location_range {
                                   filename: ""
                                   start: 22
                                   end: 27
                                 }
                               }
                               parenthesized: false
                             }
                             id_string: "INT64"
                             is_quoted: false
                           }
                         }
                       }
                     }
                   }
                 }
               }
               statement_list {
                 ast_script_statement_node {
                   ast_single_assignment_node {
                     parent {
                       parent {
                         parent {
                           parse_location_range {
                             filename: ""
                             start: 33
                             end: 49
                           }
                         }
                       }
                     }
                     variable {
                       parent {
                         parent {
                           parse_location_range {
                             filename: ""
                             start: 37
                             end: 45
                           }
                         }
                         parenthesized: false
                       }
                       id_string: "variable"
                       is_quoted: false
                     }
                     expression {
                       ast_leaf_node {
                         ast_printable_leaf_node {
                           ast_int_literal_node {
                             parent {
                               parent {
                                 parent {
                                   parent {
                                     parse_location_range {
                                       filename: ""
                                       start: 48
                                       end: 49
                                     }
                                   }
                                   parenthesized: false
                                 }
                               }
                               image: "1"
                             }
                           }
                         }
                       }
                     }
                   }
                 }
               }
               variable_declarations_allowed: true
             }
           })pb",
      &expectedResponse);
  EXPECT_THAT(response, EqualsProto(expectedResponse));

  ParseRequest request2;
  ParseResumeLocationProto* parse_resume_location =
      request2.mutable_parse_resume_location();
  parse_resume_location->set_input(script_content);
  parse_resume_location->set_byte_position(0);
  request2.set_allow_script(true);
  ParseResponse response2;
  GOOGLESQL_ASSERT_OK(Parse(request2, &response2));
  EXPECT_EQ(28, response2.resume_byte_position());
}

TEST_F(GoogleSqlLocalServiceImplTest, ParseWithEmptyTarget) {
  const std::string script_content = R"(
    DECLARE variable INT64;
    SET variable = 1;
  )";

  ParseRequest request;
  request.set_allow_script(false);
  ParseResponse response;
  ASSERT_FALSE(Parse(request, &response).ok());

  ParseRequest request2;
  request.set_allow_script(true);
  ParseResponse response2;
  ASSERT_FALSE(Parse(request2, &response2).ok());
}

TEST_F(GoogleSqlLocalServiceImplTest, Analyze) {
  const std::string catalog_proto_text = R"pb(
    name: "foo"
    table {
      name: "bar"
      serialization_id: 1
      column {
        name: "baz"
        type { type_kind: TYPE_INT32 }
        is_pseudo_column: false
      }
    })pb";

  SimpleCatalogProto catalog;
  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(catalog_proto_text, &catalog));

  AnalyzeRequest request;
  *request.mutable_simple_catalog() = catalog;
  request.set_sql_statement("select baz from bar;");

  AnalyzeResponse response;
  GOOGLESQL_EXPECT_OK(Analyze(request, &response));

  AnalyzeRequest request2;
  *request2.mutable_simple_catalog() = catalog;
  ParseResumeLocationProto* parse_resume_location2 =
      request2.mutable_parse_resume_location();
  parse_resume_location2->set_input("select baz from bar;select baz from bar;");
  parse_resume_location2->set_byte_position(0);
  AnalyzeResponse response2;
  GOOGLESQL_EXPECT_OK(Analyze(request2, &response2));
  EXPECT_EQ(20, response2.resume_byte_position());

  AnalyzeRequest request3;
  *request3.mutable_simple_catalog() = catalog;
  ParseResumeLocationProto* parse_resume_location3 =
      request3.mutable_parse_resume_location();
  parse_resume_location3->set_input("select baz from bar;select baz from bar;");
  parse_resume_location3->set_byte_position(response2.resume_byte_position());
  AnalyzeResponse response3;
  GOOGLESQL_EXPECT_OK(Analyze(request3, &response3));
  EXPECT_EQ(40, response3.resume_byte_position());
}

void AddDateTruncToCatalog(SimpleCatalogProto* catalog) {
  catalog->mutable_builtin_function_options()->add_include_function_ids(
      FN_DATE_TRUNC_DATE);
}

// builtin-only use for datetimepart
TEST_F(GoogleSqlLocalServiceImplTest,
       AnalyzeWithDescriptorPoolListProtoBuiltinOnlyForFunction) {
  AnalyzeRequest request;
  request.set_sql_statement(R"(select DATE_TRUNC(DATE "2020-10-20", MONTH))");
  // We add a useless descriptor set, this ensures that 'zero' is not somehow
  // magic.
  AddEmptyFileDescriptorSet(request.mutable_descriptor_pool_list());
  AddBuiltin(request.mutable_descriptor_pool_list());
  AddDateTruncToCatalog(request.mutable_simple_catalog());

  AnalyzeResponse response;
  GOOGLESQL_EXPECT_OK(Analyze(request, &response));

  auto date_trunc_call = response.resolved_statement()
                             .resolved_query_stmt_node()
                             .query()
                             .resolved_project_scan_node()
                             .expr_list(0)
                             .expr()
                             .resolved_function_call_base_node()
                             .resolved_function_call_node()
                             .parent();
  auto signature = date_trunc_call.signature();
  EXPECT_EQ(date_trunc_call.function().name(), "GoogleSQL:date_trunc");

  TypeProto datetimepart_type;
  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(
      R"pb(type_kind: TYPE_ENUM
           enum_type {
             enum_name: "googlesql.functions.DateTimestampPart"
             enum_file_name: "googlesql/public/functions/datetime.proto"
             file_descriptor_set_index: 1
           })pb",
      &datetimepart_type));

  EXPECT_THAT(signature.argument(1).type(), EqualsProto(datetimepart_type));
}

// The presence of the built in DescrpitorPool doesn't magically make that
// available in the catalog
TEST_F(GoogleSqlLocalServiceImplTest,
       AnalyzeWithDescriptorPoolListProtoBuiltinNotMagicallyInCatalog) {
  AnalyzeRequest request;
  request.set_sql_statement(R"(select new google.protobuf.Int64Value())");
  AddBuiltin(request.mutable_descriptor_pool_list());

  AnalyzeResponse response;
  EXPECT_THAT(Analyze(request, &response),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(GoogleSqlLocalServiceImplTest,
       AnalyzeWithDescriptorPoolListProtoCatalogIndependence) {
  constexpr int32_t kKitchenSinkPool = 1;
  constexpr int32_t kKitchenSink3Pool = 2;
  AnalyzeRequest request;
  request.set_sql_statement(
      R"(select new googlesql_test.KitchenSinkPB(1 as int64_key_1, 2 as int64_key_2),
                new a.googlesql_test.Proto3KitchenSink(1 as int64_val),
                new b.googlesql_test.KitchenSinkPB(1 as int64_key_1, 2 as int64_key_2))");
  AddBuiltin(request.mutable_descriptor_pool_list());
  // kKitchenSinkPool ->
  AddKitchenSinkDescriptorPool(request.mutable_descriptor_pool_list());
  // kKitchenSink3Pool ->
  AddKitchenSink3DescriptorPool(request.mutable_descriptor_pool_list());

  SimpleCatalogProto* root_catalog = request.mutable_simple_catalog();
  root_catalog->set_file_descriptor_set_index(kKitchenSinkPool);

  SimpleCatalogProto* sub_catalog_a = root_catalog->add_catalog();
  sub_catalog_a->set_name("a");
  sub_catalog_a->set_file_descriptor_set_index(kKitchenSink3Pool);

  SimpleCatalogProto* sub_catalog_b = root_catalog->add_catalog();
  sub_catalog_b->set_name("b");
  sub_catalog_b->set_file_descriptor_set_index(kKitchenSinkPool);

  AnalyzeResponse response;
  GOOGLESQL_CHECK_OK(Analyze(request, &response));

  TypeProto expected_kitchen_sink_proto_type;
  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        type_kind: TYPE_PROTO
        proto_type {
          proto_name: "googlesql_test.KitchenSinkPB"
          proto_file_name: "googlesql/testdata/test_schema.proto"
          file_descriptor_set_index: 1
        })pb",
      &expected_kitchen_sink_proto_type));

  TypeProto expected_kitchen_sink_proto3_type;
  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        type_kind: TYPE_PROTO
        proto_type {
          proto_name: "googlesql_test.Proto3KitchenSink"
          proto_file_name: "googlesql/testdata/test_proto3.proto"
          file_descriptor_set_index: 2
        })pb",
      &expected_kitchen_sink_proto3_type));

  auto output_columns = response.resolved_statement()
                            .resolved_query_stmt_node()
                            .output_column_list();
  ASSERT_EQ(output_columns.size(), 3);
  EXPECT_THAT(output_columns[0].column().type(),
              EqualsProto(expected_kitchen_sink_proto_type));

  EXPECT_THAT(output_columns[1].column().type(),
              EqualsProto(expected_kitchen_sink_proto3_type));

  EXPECT_THAT(output_columns[2].column().type(),
              EqualsProto(expected_kitchen_sink_proto_type));
}

TypeProto MakeInt64TypeProto(int file_descriptor_pool_index) {
  TypeProto proto;
  proto.set_type_kind(TYPE_PROTO);
  proto.mutable_proto_type()->set_proto_name("google.protobuf.Int64Value");
  proto.mutable_proto_type()->set_proto_file_name(
      "google/protobuf/wrappers.proto");
  proto.mutable_proto_type()->set_file_descriptor_set_index(
      file_descriptor_pool_index);
  return proto;
}

TypeProto MakeTestEnumTypeProto(int file_descriptor_pool_index) {
  TypeProto proto;
  proto.set_type_kind(TYPE_ENUM);
  proto.mutable_enum_type()->set_enum_name("googlesql_test.TestEnum");
  proto.mutable_enum_type()->set_enum_file_name(
      "googlesql/testdata/test_schema.proto");
  proto.mutable_enum_type()->set_file_descriptor_set_index(
      file_descriptor_pool_index);
  return proto;
}

AnalyzerOptionsProto::QueryParameterProto MakeInt64QueryParameter(
    absl::string_view parameter_name, int descriptor_pool_index) {
  AnalyzerOptionsProto::QueryParameterProto proto;
  proto.set_name(parameter_name);
  *proto.mutable_type() = MakeInt64TypeProto(descriptor_pool_index);
  return proto;
}

AnalyzerOptionsProto::QueryParameterProto MakeTestEnumQueryParameter(
    absl::string_view parameter_name, int descriptor_pool_index) {
  AnalyzerOptionsProto::QueryParameterProto proto;
  proto.set_name(parameter_name);
  *proto.mutable_type() = MakeTestEnumTypeProto(descriptor_pool_index);
  return proto;
}

EvaluateRequest::Parameter MakeInt64ValueParameter(absl::string_view name,
                                                   int64_t v) {
  EvaluateRequest::Parameter parameter;
  parameter.set_name(name);
  google::protobuf::Int64Value value;
  value.set_value(v);

  TypeFactory factory;
  const ProtoType* type;
  GOOGLESQL_EXPECT_OK(
      factory.MakeProtoType(google::protobuf::Int64Value::descriptor(), &type));
  GOOGLESQL_EXPECT_OK(values::Proto(type, value).Serialize(parameter.mutable_value()));
  return parameter;
}

EvaluateRequest::Parameter MakeTestEnumParameter(absl::string_view name,
                                                 googlesql_test::TestEnum v) {
  EvaluateRequest::Parameter parameter;
  parameter.set_name(name);

  TypeFactory factory;
  const EnumType* type;
  GOOGLESQL_EXPECT_OK(factory.MakeEnumType(googlesql_test::TestEnum_descriptor(), &type));
  GOOGLESQL_EXPECT_OK(values::Enum(type, v).Serialize(parameter.mutable_value()));
  return parameter;
}

// We can use the builtin DescriptorPool for parameters, even if that
// pool is not used in the catalog (or there is no catalog).
TEST_F(GoogleSqlLocalServiceImplTest,
       AnalyzeWithDescriptorPoolListProtoBuiltinOnlyForParameter) {
  AnalyzeRequest request;
  request.set_sql_statement(R"(select @p1.value, @p1)");
  // We add a useless descriptor set, this ensures that 'zero' is not somehow
  // magic.
  AddEmptyFileDescriptorSet(request.mutable_descriptor_pool_list());
  AddBuiltin(request.mutable_descriptor_pool_list());

  TypeFactory factory;
  AnalyzerOptions options;
  const ProtoType* int64_proto_type = nullptr;
  GOOGLESQL_CHECK_OK(factory.MakeProtoType(google::protobuf::Int64Value::descriptor(),
                                 &int64_proto_type));
  GOOGLESQL_CHECK_OK(options.AddQueryParameter("p1", int64_proto_type));

  google::protobuf::DescriptorPool empty;
  FileDescriptorSetMap descriptor_map;
  // Add an empty entry to ensure 'zero' is not magic, this won't be used.
  descriptor_map.emplace(&empty, std::make_unique<Type::FileDescriptorEntry>());
  GOOGLESQL_CHECK_OK(options.Serialize(&descriptor_map, request.mutable_options()));
  // We expect the generated pool (i.e. what Int64Value is using) as the 2nd
  // entry.
  ABSL_CHECK_EQ(descriptor_map.size(), 2);
  EXPECT_THAT(descriptor_map[&empty]->file_descriptors, ::testing::IsEmpty());

  AnalyzeResponse response;
  GOOGLESQL_EXPECT_OK(Analyze(request, &response));
  TypeProto expected_proto_type =
      MakeInt64TypeProto(/*file_descriptor_pool_index=*/1);
  auto output_columns = response.resolved_statement()
                            .resolved_query_stmt_node()
                            .output_column_list();
  ASSERT_EQ(output_columns.size(), 2);
  // Note, this is googlesql::TypeKind
  EXPECT_EQ(output_columns[0].column().type().type_kind(), TYPE_INT64);
  // Note, this is a representation of google.protobuf.Int64Value.
  EXPECT_THAT(output_columns[1].column().type(),
              EqualsProto(expected_proto_type));
}

TEST_F(GoogleSqlLocalServiceImplTest, RegisterCatalogEmptyDescriptorPoolList) {
  RegisterCatalogRequest request;
  request.mutable_simple_catalog();
  // Add an empty list, to trigger the correct response codepath.
  request.mutable_descriptor_pool_list();
  RegisterResponse response;
  GOOGLESQL_ASSERT_OK(RegisterCatalog(request, &response));
  // We expect an empty (but present) descriptor_pool_id_list.
  EXPECT_FALSE(response.has_descriptor_pool_id_list());
  EXPECT_THAT(response.descriptor_pool_id_list().registered_ids(), IsEmpty());
  GOOGLESQL_ASSERT_OK(UnregisterCatalog(response.registered_id()));
}

TEST_F(GoogleSqlLocalServiceImplTest,
       RegisterCatalogReturnsIdsForDescriptorPoolInputs) {
  RegisterCatalogRequest request;
  request.mutable_simple_catalog();
  AddEmptyFileDescriptorSet(request.mutable_descriptor_pool_list());
  AddBuiltin(request.mutable_descriptor_pool_list());

  RegisterResponse response;
  GOOGLESQL_ASSERT_OK(RegisterCatalog(request, &response));
  EXPECT_THAT(response.descriptor_pool_id_list().registered_ids_size(), 2);

  GOOGLESQL_ASSERT_OK(UnregisterCatalog(response.registered_id()));
}

TEST_F(GoogleSqlLocalServiceImplTest,
       RegisterCatalogCleanupsDescriptorPoolsOnError) {
  RegisterCatalogRequest request;
  request.mutable_simple_catalog();
  AddEmptyFileDescriptorSet(request.mutable_descriptor_pool_list());
  AddBuiltin(request.mutable_descriptor_pool_list());
  // Force an missing name error.
  request.mutable_simple_catalog()->add_constant();

  RegisterResponse response;
  ASSERT_FALSE(RegisterCatalog(request, &response).ok());
}

TEST_F(GoogleSqlLocalServiceImplTest, RegisterCatalogWithTableData) {
  RegisterCatalogRequest request;
  AddTestTable(request.mutable_simple_catalog()->add_table(), "TestTable");
  // Add an empty list, to trigger the correct response codepath.
  request.mutable_descriptor_pool_list();
  InsertTestTableContent(request.mutable_table_content(), "TestTable");

  RegisterResponse response;
  GOOGLESQL_ASSERT_OK(RegisterCatalog(request, &response));
  // We expect an empty (but present) descriptor_pool_id_list.
  EXPECT_FALSE(response.has_descriptor_pool_id_list());
  EXPECT_THAT(response.descriptor_pool_id_list().registered_ids(), IsEmpty());
  GOOGLESQL_ASSERT_OK(UnregisterCatalog(response.registered_id()));
}

std::vector<TypeProto> GetOutputTypes(
    const AnyResolvedStatementProto& resolved_statement) {
  EXPECT_TRUE(resolved_statement.has_resolved_query_stmt_node());
  const ResolvedQueryStmtProto& query_proto =
      resolved_statement.resolved_query_stmt_node();
  std::vector<TypeProto> types;
  for (const auto& output_column : query_proto.output_column_list()) {
    types.push_back(output_column.column().type());
  }
  return types;
}

TypeProto GetOutputType(const AnyResolvedStatementProto& resolved_statement) {
  std::vector<TypeProto> types = GetOutputTypes(resolved_statement);
  ABSL_CHECK_EQ(types.size(), 1);
  return types[0];
}

TEST_F(GoogleSqlLocalServiceImplTest, AnalyzeWithRegisteredCatalog) {
  int64_t catalog_id;
  int64_t kitchen_sink_pool_id;
  {
    RegisterCatalogRequest catalog_request;
    catalog_request.mutable_simple_catalog();
    AddKitchenSinkDescriptorPool(
        catalog_request.mutable_descriptor_pool_list());
    catalog_request.mutable_simple_catalog()->set_file_descriptor_set_index(0);
    RegisterResponse catalog_response;
    GOOGLESQL_ASSERT_OK(RegisterCatalog(catalog_request, &catalog_response));
    catalog_id = catalog_response.registered_id();
    EXPECT_THAT(
        catalog_response.descriptor_pool_id_list().registered_ids_size(), 1);
    kitchen_sink_pool_id =
        catalog_response.descriptor_pool_id_list().registered_ids(0);
  }

  AnalyzeRequest analyze_request;
  AddBuiltin(analyze_request.mutable_descriptor_pool_list());
  AddRegisteredDescriptorPool(analyze_request.mutable_descriptor_pool_list(),
                              kitchen_sink_pool_id);
  constexpr int32_t kKitchenSinkPoolIndex = 1;

  analyze_request.set_registered_catalog_id(catalog_id);
  analyze_request.set_sql_statement(
      R"(select new googlesql_test.KitchenSinkPB(1 as int64_key_1, 2 as int64_key_2))");

  AnalyzeResponse analyze_response;

  GOOGLESQL_ASSERT_OK(Analyze(analyze_request, &analyze_response));
  TypeProto output_column_type =
      GetOutputType(analyze_response.resolved_statement());
  EXPECT_EQ(output_column_type.type_kind(), TYPE_PROTO);
  EXPECT_EQ(output_column_type.proto_type().proto_name(),
            "googlesql_test.KitchenSinkPB");
  EXPECT_EQ(output_column_type.proto_type().file_descriptor_set_index(),
            kKitchenSinkPoolIndex);
  GOOGLESQL_ASSERT_OK(UnregisterCatalog(catalog_id));
}

TEST_F(GoogleSqlLocalServiceImplTest,
       AnalyzeWithRegisteredCatalogShouldFailWithMissingPool) {
  int64_t catalog_id;
  {
    RegisterCatalogRequest catalog_request;
    catalog_request.mutable_simple_catalog();
    AddKitchenSinkDescriptorPool(
        catalog_request.mutable_descriptor_pool_list());
    catalog_request.mutable_simple_catalog()->set_file_descriptor_set_index(0);
    RegisterResponse catalog_response;
    GOOGLESQL_ASSERT_OK(RegisterCatalog(catalog_request, &catalog_response));
    catalog_id = catalog_response.registered_id();
    EXPECT_THAT(
        catalog_response.descriptor_pool_id_list().registered_ids_size(), 1);
  }

  AnalyzeRequest analyze_request;
  AddBuiltin(analyze_request.mutable_descriptor_pool_list());
  // Notice, we don't add KitchenPool to descriptor_pool_list
  // While we can _analyze_ the query without problem, we cannot serialize
  // the result, we rely on the client side to pass in all possibly-needed
  // descriptor pools as part of the request.
  analyze_request.set_registered_catalog_id(catalog_id);
  analyze_request.set_sql_statement(
      R"(select new googlesql_test.KitchenSinkPB(1 as int64_key_1, 2 as int64_key_2))");

  AnalyzeResponse analyze_response;
  EXPECT_THAT(Analyze(analyze_request, &analyze_response),
              StatusIs(absl::StatusCode::kInternal));
  GOOGLESQL_ASSERT_OK(UnregisterCatalog(catalog_id));
}

TEST_F(GoogleSqlLocalServiceImplTest,
       UnregisterCatalogAlsoUnregistersOwnedDescriptorPools) {
  int64_t catalog1_id;
  int64_t kitchen_sink_pool_id;  // kitchen
  int64_t pool_b_id;
  int64_t builtin_pool_id;
  // REGISTER CATALOG 1
  {
    RegisterCatalogRequest request1;
    request1.mutable_simple_catalog();
    AddKitchenSinkDescriptorPool(request1.mutable_descriptor_pool_list());
    AddEmptyFileDescriptorSet(request1.mutable_descriptor_pool_list());
    // Add builtin twice to validate they have the same id.
    AddBuiltin(request1.mutable_descriptor_pool_list());
    AddBuiltin(request1.mutable_descriptor_pool_list());

    RegisterResponse response1;
    GOOGLESQL_ASSERT_OK(RegisterCatalog(request1, &response1));
    catalog1_id = response1.registered_id();
    EXPECT_THAT(response1.descriptor_pool_id_list().registered_ids_size(), 4);
    kitchen_sink_pool_id =
        response1.descriptor_pool_id_list().registered_ids(0);
    pool_b_id = response1.descriptor_pool_id_list().registered_ids(1);
    builtin_pool_id = response1.descriptor_pool_id_list().registered_ids(2);
    EXPECT_EQ(response1.descriptor_pool_id_list().registered_ids(3),
              builtin_pool_id);
  }

  // Use an AnalyzeRequest to 'probe' the descriptor pool.
  AnalyzeRequest analyze_request;
  AddRegisteredDescriptorPool(analyze_request.mutable_descriptor_pool_list(),
                              kitchen_sink_pool_id);
  AddRegisteredDescriptorPool(analyze_request.mutable_descriptor_pool_list(),
                              builtin_pool_id);
  analyze_request.mutable_simple_catalog()->set_file_descriptor_set_index(0);
  analyze_request.set_sql_statement(
      R"(select new googlesql_test.KitchenSinkPB(1 as int64_key_1, 2 as int64_key_2))");

  AnalyzeResponse analyze_response;

  GOOGLESQL_ASSERT_OK(Analyze(analyze_request, &analyze_response));

  // UNREGISTER CATALOG 1 - This should drop pool_b
  GOOGLESQL_ASSERT_OK(UnregisterCatalog(catalog1_id));

  // Same request again, not it is an error because the descriptor pool is gone.
  EXPECT_THAT(Analyze(analyze_request, &analyze_response),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

void ExpectTypeIsDate(const TypeProto& type) {
  EXPECT_EQ(type.type_kind(), TYPE_DATE);
}

void ExpectTypeIsString(const TypeProto& type) {
  EXPECT_EQ(type.type_kind(), TYPE_STRING);
}

void ExpectTypeIsBool(const TypeProto& type) {
  EXPECT_EQ(type.type_kind(), TYPE_BOOL);
}

void ExpectTypeIsInt32(const TypeProto& type) {
  EXPECT_EQ(type.type_kind(), TYPE_INT32);
}

void ExpectOutputTypeIsDate(const PrepareResponse& response) {
  ExpectTypeIsDate(response.prepared().output_type());
}

void ExpectOutputTypeIsDate(const EvaluateResponse& response) {
  ExpectTypeIsDate(response.prepared().output_type());
}

void ExpectOutputIsDate(const EvaluateResponse& response, int year, int month,
                        int day) {
  int32_t expected_date;
  GOOGLESQL_EXPECT_OK(functions::ConstructDate(year, month, day, &expected_date));
  EXPECT_EQ(response.value().date_value(), expected_date);
}

void ExpectTypeIsTestEnum(const TypeProto& type,
                          int file_descriptor_set_index) {
  EXPECT_EQ(type.type_kind(), TYPE_ENUM);
  EXPECT_EQ(type.enum_type().enum_name(), "googlesql_test.TestEnum");
  EXPECT_EQ(type.enum_type().enum_file_name(),
            "googlesql/testdata/test_schema.proto");
  EXPECT_EQ(type.enum_type().file_descriptor_set_index(),
            file_descriptor_set_index);
}

void ExpectValueIsTestEnum(const ValueProto& value,
                           googlesql_test::TestEnum expected) {
  EXPECT_EQ(value.enum_value(), expected);
}

void ExpectValueIsString(const ValueProto& value, std::string expected) {
  EXPECT_EQ(value.string_value(), expected);
}

void ExpectValueIsBool(const ValueProto& value, bool expected) {
  EXPECT_EQ(value.bool_value(), expected);
}

void ExpectValueIsInt32(const ValueProto& value, int32_t expected) {
  EXPECT_EQ(value.int32_value(), expected);
}

void ExpectTypeIsKitchenSink3(const TypeProto& type,
                              int64_t file_descriptor_set_index) {
  EXPECT_EQ(type.type_kind(), TYPE_PROTO);
  EXPECT_EQ(type.proto_type().proto_name(), "googlesql_test.Proto3KitchenSink");
  EXPECT_EQ(type.proto_type().proto_file_name(),
            "googlesql/testdata/test_proto3.proto");
  EXPECT_EQ(type.proto_type().file_descriptor_set_index(),
            file_descriptor_set_index);
}

void ExpectValueIsKitchenSink3(
    const ValueProto& value,
    const googlesql_test::Proto3KitchenSink& expected) {
  googlesql_test::Proto3KitchenSink actual;
  ASSERT_TRUE(actual.ParseFromCord(absl::Cord(value.proto_value())));
  EXPECT_THAT(actual, EqualsProto(expected));
}

TEST_F(GoogleSqlLocalServiceImplTest,
       PrepareFailuresNoRegisterDescriptorPoolList) {
  PrepareRequest request;
  PrepareResponse response;

  request.set_sql("foo");
  AddKitchenSink3DescriptorPool(request.mutable_descriptor_pool_list());
  ASSERT_FALSE(Prepare(request, &response).ok());
  // No prepared state saved on failure.
  EXPECT_EQ(0, NumSavedPreparedExpression());

  request.set_sql("foo + @bar");
  auto* param = request.mutable_options()->add_query_parameters();
  param->set_name("bar");
  param->mutable_type()->set_type_kind(TYPE_INT64);

  ASSERT_FALSE(Prepare(request, &response).ok());
  EXPECT_EQ(0, NumSavedPreparedExpression());

  auto* column = request.mutable_options()->add_expression_columns();
  column->set_name("foo");
  column->mutable_type()->set_type_kind(TYPE_STRING);

  ASSERT_FALSE(Prepare(request, &response).ok());
  EXPECT_EQ(0, NumSavedPreparedExpression());
}

TEST_F(GoogleSqlLocalServiceImplTest, EvaluateWithDescriptorPoolList) {
  EvaluateRequest evaluate_request;
  evaluate_request.set_sql(R"(DATE "2020-10-20")");

  // Note, it is always recommended to include at least the
  // builtin descriptor pool, but it shouldn't be necessary for this particular
  // query.
  evaluate_request.mutable_descriptor_pool_list();

  EvaluateResponse evaluate_response;
  GOOGLESQL_EXPECT_OK(Evaluate(evaluate_request, &evaluate_response));
  EXPECT_EQ(evaluate_response.prepared()
                .descriptor_pool_id_list()
                .registered_ids_size(),
            0);
  ExpectOutputTypeIsDate(evaluate_response);
  ExpectOutputIsDate(evaluate_response, 2020, 10, 20);

  // Evaluate always creates a prepared expression, make sure we delete it.
  GOOGLESQL_EXPECT_OK(Unprepare(evaluate_response.prepared().prepared_expression_id()));
}

// As above, but using prepared request.
TEST_F(GoogleSqlLocalServiceImplTest,
       PrepareAndEvaluateWithDescriptorPoolList) {
  // PREPARE
  PrepareRequest prepare_request;
  prepare_request.set_sql(R"(DATE "2020-10-20")");

  // Note, it is always recommended to include at least the
  // builtin descriptor pool, but it shouldn't be necessary for this particular
  // query.
  prepare_request.mutable_descriptor_pool_list();

  PrepareResponse prepare_response;
  GOOGLESQL_EXPECT_OK(Prepare(prepare_request, &prepare_response));
  ExpectOutputTypeIsDate(prepare_response);
  EXPECT_EQ(prepare_response.prepared()
                .descriptor_pool_id_list()
                .registered_ids_size(),
            0);

  // EVALUATE
  EvaluateRequest evaluate_request;
  evaluate_request.mutable_descriptor_pool_list();
  evaluate_request.set_prepared_expression_id(
      prepare_response.prepared().prepared_expression_id());
  EvaluateResponse evaluate_response;
  GOOGLESQL_EXPECT_OK(Evaluate(evaluate_request, &evaluate_response));

  ExpectOutputIsDate(evaluate_response, 2020, 10, 20);
  GOOGLESQL_EXPECT_OK(Unprepare(prepare_response.prepared().prepared_expression_id()));
}

// We use query parameters as a proxy for descriptor pool resolution across
// all the of various types potentially encoded inside AnalyzerOptions
// The assumption is that if it works for params, it will work for everything
// else as well (of course, there could be bugs in AnalyzerOptions
// deserialization, but this isn't the place to test for that).
TEST_F(GoogleSqlLocalServiceImplTest,
       EvaluateWithDescriptorPoolListProtoWithParam) {
  EvaluateRequest evaluate_request;
  // Note: for now, evaluate without prepare always creates a default simple
  // catalog with all builtin function included.
  evaluate_request.set_sql(R"(IF(@p1 is null, null, @p2))");

  // fake empty pool to ensure '0' isn't magic
  AddEmptyFileDescriptorSet(evaluate_request.mutable_descriptor_pool_list());
  AddBuiltin(evaluate_request.mutable_descriptor_pool_list());
  AddKitchenSinkDescriptorPool(evaluate_request.mutable_descriptor_pool_list());
  // Use the builtin pool
  *evaluate_request.mutable_options()->add_query_parameters() =
      MakeInt64QueryParameter("p1", 1);
  // Use kitchen sink
  *evaluate_request.mutable_options()->add_query_parameters() =
      MakeTestEnumQueryParameter("p2", 2);

  *evaluate_request.add_params() = MakeInt64ValueParameter("p1", 5);
  *evaluate_request.add_params() =
      MakeTestEnumParameter("p2", googlesql_test::TestEnum::TESTENUM1);

  EvaluateResponse evaluate_response;
  GOOGLESQL_EXPECT_OK(Evaluate(evaluate_request, &evaluate_response));
  EXPECT_EQ(evaluate_response.prepared()
                .descriptor_pool_id_list()
                .registered_ids_size(),
            3);
  ExpectTypeIsTestEnum(evaluate_response.prepared().output_type(),
                       /*file_descriptor_set_index=*/2);
  ExpectValueIsTestEnum(evaluate_response.value(),
                        googlesql_test::TestEnum::TESTENUM1);
  // Evaluate always creates a prepared expression, make sure we delete it.
  GOOGLESQL_EXPECT_OK(Unprepare(evaluate_response.prepared().prepared_expression_id()));
}

// As above, but with prepare
TEST_F(GoogleSqlLocalServiceImplTest,
       PreparedAndEvaluateWithDescriptorPoolListProtoWithParam) {
  // PREPARE
  PrepareRequest prepare_request;
  prepare_request.set_sql(R"(IF(@p1 is null, null, @p2))");

  // Ensure we have the builtin functions
  prepare_request.mutable_simple_catalog()->mutable_builtin_function_options();

  // fake empty pool to ensure '0' isn't magic
  AddEmptyFileDescriptorSet(prepare_request.mutable_descriptor_pool_list());
  AddBuiltin(prepare_request.mutable_descriptor_pool_list());
  AddKitchenSinkDescriptorPool(prepare_request.mutable_descriptor_pool_list());
  // Use the builtin pool
  *prepare_request.mutable_options()->add_query_parameters() =
      MakeInt64QueryParameter("p1", 1);
  // Use kitchen sink
  *prepare_request.mutable_options()->add_query_parameters() =
      MakeTestEnumQueryParameter("p2", 2);
  PrepareResponse prepare_response;
  GOOGLESQL_EXPECT_OK(Prepare(prepare_request, &prepare_response));
  ExpectTypeIsTestEnum(prepare_response.prepared().output_type(), 2);
  EXPECT_EQ(prepare_response.prepared()
                .descriptor_pool_id_list()
                .registered_ids_size(),
            3);

  // EVALUATE
  EvaluateRequest evaluate_request;
  evaluate_request.set_prepared_expression_id(
      prepare_response.prepared().prepared_expression_id());

  *evaluate_request.add_params() = MakeInt64ValueParameter("p1", 5);
  *evaluate_request.add_params() =
      MakeTestEnumParameter("p2", googlesql_test::TestEnum::TESTENUM1);

  EvaluateResponse evaluate_response;
  GOOGLESQL_EXPECT_OK(Evaluate(evaluate_request, &evaluate_response));
  // On an already prepared expression, we should not return the registered
  // ids.
  EXPECT_EQ(evaluate_response.prepared()
                .descriptor_pool_id_list()
                .registered_ids_size(),
            0);
  ExpectValueIsTestEnum(evaluate_response.value(),
                        googlesql_test::TestEnum::TESTENUM1);

  // Evaluate always creates a prepared expression, make sure we delete it.
  GOOGLESQL_EXPECT_OK(Unprepare(evaluate_response.prepared().prepared_expression_id()));
}

// As above, but with prepare and a registered catalog
TEST_F(GoogleSqlLocalServiceImplTest,
       RegisterPreparedAndEvaluateWithDescriptorPoolListProtoWithParam) {
  // REGISTER
  RegisterCatalogRequest catalog_request;
  // Ensure we have the builtin functions
  catalog_request.mutable_simple_catalog()->mutable_builtin_function_options();

  // We register the pools with the catalog, even though it doesn't use them
  // it should 'owned' these pools, and let us reference them in the future.
  // fake empty pool to ensure '0' isn't magic
  AddEmptyFileDescriptorSet(catalog_request.mutable_descriptor_pool_list());
  AddBuiltin(catalog_request.mutable_descriptor_pool_list());
  AddKitchenSinkDescriptorPool(catalog_request.mutable_descriptor_pool_list());

  RegisterResponse catalog_response;
  GOOGLESQL_ASSERT_OK(RegisterCatalog(catalog_request, &catalog_response));
  const int64_t catalog_id = catalog_response.registered_id();

  const int64_t empty_descriptor_pool_id =
      catalog_response.descriptor_pool_id_list().registered_ids(0);
  const int64_t kitchen_sink_pool_id =
      catalog_response.descriptor_pool_id_list().registered_ids(2);

  // PREPARE
  PrepareRequest prepare_request;
  prepare_request.set_registered_catalog_id(catalog_id);
  prepare_request.set_sql(R"(IF(@p1 is null, null, @p2))");

  AddRegisteredDescriptorPool(prepare_request.mutable_descriptor_pool_list(),
                              empty_descriptor_pool_id);
  AddBuiltin(prepare_request.mutable_descriptor_pool_list());
  AddRegisteredDescriptorPool(prepare_request.mutable_descriptor_pool_list(),
                              kitchen_sink_pool_id);

  // Use the builtin pool
  *prepare_request.mutable_options()->add_query_parameters() =
      MakeInt64QueryParameter("p1", 1);
  // Use kitchen sink
  *prepare_request.mutable_options()->add_query_parameters() =
      MakeTestEnumQueryParameter("p2", 2);
  PrepareResponse prepare_response;
  GOOGLESQL_EXPECT_OK(Prepare(prepare_request, &prepare_response));
  ExpectTypeIsTestEnum(prepare_response.prepared().output_type(), 2);
  EXPECT_THAT(
      catalog_response.descriptor_pool_id_list(),
      EqualsProto(prepare_response.prepared().descriptor_pool_id_list()));

  // EVALUATE
  EvaluateRequest evaluate_request;
  evaluate_request.set_prepared_expression_id(
      prepare_response.prepared().prepared_expression_id());

  *evaluate_request.add_params() = MakeInt64ValueParameter("p1", 5);
  *evaluate_request.add_params() =
      MakeTestEnumParameter("p2", googlesql_test::TestEnum::TESTENUM1);

  EvaluateResponse evaluate_response;
  GOOGLESQL_EXPECT_OK(Evaluate(evaluate_request, &evaluate_response));
  EXPECT_EQ(evaluate_response.prepared()
                .descriptor_pool_id_list()
                .registered_ids_size(),
            0);
  ExpectValueIsTestEnum(evaluate_response.value(),
                        googlesql_test::TestEnum::TESTENUM1);

  GOOGLESQL_EXPECT_OK(Unprepare(evaluate_response.prepared().prepared_expression_id()));
  GOOGLESQL_EXPECT_OK(UnregisterCatalog(catalog_response.registered_id()));
}

TEST_F(GoogleSqlLocalServiceImplTest,
       RegisterPreparedAndEvaluateWithDescriptorPoolListProtoWithParamComplex) {
  int64_t catalog_id;
  int64_t kitchen_sink3_pool_id;
  {
    RegisterCatalogRequest catalog_request;
    // Ensure we have the builtin functions
    catalog_request.mutable_simple_catalog()
        ->mutable_builtin_function_options();
    AddKitchenSink3DescriptorPool(
        catalog_request.mutable_descriptor_pool_list());
    catalog_request.mutable_simple_catalog()->set_file_descriptor_set_index(0);
    RegisterResponse catalog_response;
    GOOGLESQL_ASSERT_OK(RegisterCatalog(catalog_request, &catalog_response));
    catalog_id = catalog_response.registered_id();
    EXPECT_THAT(
        catalog_response.descriptor_pool_id_list().registered_ids_size(), 1);
    kitchen_sink3_pool_id =
        catalog_response.descriptor_pool_id_list().registered_ids(0);
  }

  PrepareRequest prepare_request;
  prepare_request.set_registered_catalog_id(catalog_id);
  prepare_request.set_sql(R"(new googlesql_test.Proto3KitchenSink(
      @p1.value as int64_val,
      if (cast(@p2 as INT32) = 1,
          cast('ENUM1' as googlesql_test.TestProto3Enum),
          cast('ENUM2' as googlesql_test.TestProto3Enum)) as test_enum))");

  // Used for p1
  AddBuiltin(prepare_request.mutable_descriptor_pool_list());
  // Used for p2
  AddKitchenSinkDescriptorPool(prepare_request.mutable_descriptor_pool_list());
  // Used in the output value.
  AddRegisteredDescriptorPool(prepare_request.mutable_descriptor_pool_list(),
                              kitchen_sink3_pool_id);

  // Use the builtin pool
  *prepare_request.mutable_options()->add_query_parameters() =
      MakeInt64QueryParameter("p1", 0);
  // Use kitchen sink
  *prepare_request.mutable_options()->add_query_parameters() =
      MakeTestEnumQueryParameter("p2", 1);
  PrepareResponse prepare_response;
  GOOGLESQL_EXPECT_OK(Prepare(prepare_request, &prepare_response));
  ExpectTypeIsKitchenSink3(prepare_response.prepared().output_type(), 2);
  EXPECT_EQ(prepare_response.prepared()
                .descriptor_pool_id_list()
                .registered_ids_size(),
            3);
  EXPECT_EQ(
      prepare_response.prepared().descriptor_pool_id_list().registered_ids(2),
      kitchen_sink3_pool_id);

  // EVALUATE
  EvaluateRequest base_evaluate_request;
  base_evaluate_request.set_prepared_expression_id(
      prepare_response.prepared().prepared_expression_id());

  {
    EvaluateRequest evaluate_request = base_evaluate_request;
    *evaluate_request.add_params() = MakeInt64ValueParameter("p1", 5);
    *evaluate_request.add_params() =
        MakeTestEnumParameter("p2", googlesql_test::TestEnum::TESTENUM1);
    EvaluateResponse evaluate_response;
    GOOGLESQL_EXPECT_OK(Evaluate(evaluate_request, &evaluate_response));
    EXPECT_EQ(evaluate_response.prepared()
                  .descriptor_pool_id_list()
                  .registered_ids_size(),
              0);
    googlesql_test::Proto3KitchenSink expected_output_1;
    expected_output_1.set_int64_val(5);
    expected_output_1.set_test_enum(googlesql_test::TestProto3Enum::ENUM1);
    ExpectValueIsKitchenSink3(evaluate_response.value(), expected_output_1);
  }
  {
    EvaluateRequest evaluate_request = base_evaluate_request;
    *evaluate_request.add_params() = MakeInt64ValueParameter("p1", 7);
    *evaluate_request.add_params() = MakeTestEnumParameter(
        "p2", googlesql_test::TestEnum::TESTENUM2147483647);
    EvaluateResponse evaluate_response;
    GOOGLESQL_EXPECT_OK(Evaluate(evaluate_request, &evaluate_response));
    EXPECT_EQ(evaluate_response.prepared()
                  .descriptor_pool_id_list()
                  .registered_ids_size(),
              0);
    googlesql_test::Proto3KitchenSink expected_output_1;
    expected_output_1.set_int64_val(7);
    expected_output_1.set_test_enum(googlesql_test::TestProto3Enum::ENUM2);
    ExpectValueIsKitchenSink3(evaluate_response.value(), expected_output_1);
  }

  GOOGLESQL_EXPECT_OK(Unprepare(prepare_response.prepared().prepared_expression_id()));
  GOOGLESQL_EXPECT_OK(UnregisterCatalog(catalog_id));
}

TEST_F(GoogleSqlLocalServiceImplTest, PrepareRegistersCatalog) {
  PrepareRequest prepare_request;
  prepare_request.set_sql("1");
  prepare_request.mutable_simple_catalog();
  AddBuiltin(prepare_request.mutable_descriptor_pool_list());

  PrepareResponse prepare_response;
  GOOGLESQL_ASSERT_OK(Prepare(prepare_request, &prepare_response));
  /*
  ASSERT_EQ(NumRegisteredDescriptorPools(), 1);
  ASSERT_EQ(NumRegisteredCatalogs(), 1);
  ASSERT_EQ(NumSavedPreparedExpression(), 1);
  */
  GOOGLESQL_ASSERT_OK(Unprepare(prepare_response.prepared().prepared_expression_id()));
}

TEST_F(GoogleSqlLocalServiceImplTest,
       UnprepareAlsoUnregistersOwnedCatalogsAndDescriptorPools) {
  int64_t catalog1_id;
  int64_t kitchen_sink_pool_id;
  int64_t builtin_pool_id;

  // REGISTER CATALOG 1
  {
    RegisterCatalogRequest request1;
    request1.mutable_simple_catalog();
    AddKitchenSinkDescriptorPool(request1.mutable_descriptor_pool_list());
    AddBuiltin(request1.mutable_descriptor_pool_list());

    RegisterResponse response1;
    GOOGLESQL_ASSERT_OK(RegisterCatalog(request1, &response1));
    catalog1_id = response1.registered_id();
    EXPECT_THAT(response1.descriptor_pool_id_list().registered_ids_size(), 2);
    kitchen_sink_pool_id =
        response1.descriptor_pool_id_list().registered_ids(0);
    builtin_pool_id = response1.descriptor_pool_id_list().registered_ids(1);
  }
  int64_t kitchen_sink3_pool_id;
  int64_t prepared_expression_id;
  {
    PrepareRequest prepare_request;
    prepare_request.set_sql("1");
    prepare_request.set_registered_catalog_id(catalog1_id);
    // We don't actually reference the kitchen_sink_pool_id;
    AddRegisteredDescriptorPool(prepare_request.mutable_descriptor_pool_list(),
                                builtin_pool_id);
    AddKitchenSink3DescriptorPool(
        prepare_request.mutable_descriptor_pool_list());

    PrepareResponse prepare_response;
    GOOGLESQL_ASSERT_OK(Prepare(prepare_request, &prepare_response));
    prepared_expression_id =
        prepare_response.prepared().prepared_expression_id();
    kitchen_sink3_pool_id =
        prepare_response.prepared().descriptor_pool_id_list().registered_ids(1);
  }
  // Use an AnalyzeRequest to 'probe' kitchen_sink_pool_id.
  AnalyzeRequest analyze_request;
  AddRegisteredDescriptorPool(analyze_request.mutable_descriptor_pool_list(),
                              kitchen_sink_pool_id);
  AddRegisteredDescriptorPool(analyze_request.mutable_descriptor_pool_list(),
                              builtin_pool_id);
  analyze_request.mutable_simple_catalog()->set_file_descriptor_set_index(0);
  analyze_request.set_sql_statement(
      R"(select new googlesql_test.KitchenSinkPB(1 as int64_key_1, 2 as int64_key_2))");

  {
    AnalyzeResponse analyze_response;
    GOOGLESQL_ASSERT_OK(Analyze(analyze_request, &analyze_response));
  }

  AnalyzeRequest analyze_kitchen_sink3_request;
  AddRegisteredDescriptorPool(
      analyze_kitchen_sink3_request.mutable_descriptor_pool_list(),
      kitchen_sink3_pool_id);
  AddRegisteredDescriptorPool(
      analyze_kitchen_sink3_request.mutable_descriptor_pool_list(),
      builtin_pool_id);
  analyze_kitchen_sink3_request.mutable_simple_catalog()
      ->set_file_descriptor_set_index(0);
  analyze_kitchen_sink3_request.set_sql_statement(
      R"(select new googlesql_test.Proto3KitchenSink(1 as int32_val))");

  {
    AnalyzeResponse analyze_response;
    GOOGLESQL_ASSERT_OK(Analyze(analyze_kitchen_sink3_request, &analyze_response));
  }
  // Unprepare, this should drop kitchen_sink3_pool
  // but kitchen_sink_pool should be okay.
  GOOGLESQL_ASSERT_OK(Unprepare(prepared_expression_id));

  {
    // Test that the kitchen_sink_pool is okay.
    AnalyzeResponse analyze_response;
    GOOGLESQL_ASSERT_OK(Analyze(analyze_request, &analyze_response));
  }

  {
    // Test that the kitchen_sink3_pool now fails because the pool is gone..
    AnalyzeResponse analyze_response;
    // Same request again, not it is an error because the descriptor pool is
    // gone.
    EXPECT_THAT(Analyze(analyze_kitchen_sink3_request, &analyze_response),
                StatusIs(absl::StatusCode::kInvalidArgument));
  }
  GOOGLESQL_ASSERT_OK(UnregisterCatalog(catalog1_id));
}

TEST_F(GoogleSqlLocalServiceImplTest, AnalyzeExpression) {
  SimpleCatalogProto catalog;

  googlesql::GoogleSQLBuiltinFunctionOptionsProto options;
  googlesql::GoogleSQLBuiltinFunctionOptionsProto* builtin_function_options =
      catalog.mutable_builtin_function_options();
  *builtin_function_options = options;

  AnalyzeRequest request;
  *request.mutable_simple_catalog() = catalog;
  request.set_sql_expression("123");

  AnalyzeResponse response;
  GOOGLESQL_EXPECT_OK(Analyze(request, &response));

  AnalyzeResponse expectedResponse;
  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(
      R"pb(resolved_expression {
             resolved_literal_node {
               parent {
                 type { type_kind: TYPE_INT64 }
                 type_annotation_map {}
               }
               value {
                 type { type_kind: TYPE_INT64 }
                 value { int64_value: 123 }
               }
               has_explicit_type: false
               float_literal_id: 0
               preserve_in_literal_remover: false
             }
           })pb",
      &expectedResponse));
  EXPECT_THAT(response, EqualsProto(expectedResponse));

  AnalyzeRequest request2;
  *request2.mutable_simple_catalog() = catalog;

  request2.set_sql_expression("foo < 123");
  auto* column = request2.mutable_options()->add_expression_columns();
  column->set_name("foo");
  column->mutable_type()->set_type_kind(TYPE_INT32);
  AnalyzeResponse response2;
  GOOGLESQL_EXPECT_OK(Analyze(request2, &response2));

  AnalyzeResponse expectedResponse2;
  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(
      R"pb(resolved_expression {
             resolved_function_call_base_node {
               resolved_function_call_node {
                 parent {
                   parent {
                     type { type_kind: TYPE_BOOL }
                     type_annotation_map {}
                   }
                   function { name: "GoogleSQL:$less" }
                   signature {
                     argument {
                       kind: ARG_TYPE_FIXED
                       type { type_kind: TYPE_INT32 }
                       options {
                         cardinality: REQUIRED
                         extra_relation_input_columns_allowed: true
                       }
                       num_occurrences: 1
                     }
                     argument {
                       kind: ARG_TYPE_FIXED
                       type { type_kind: TYPE_INT32 }
                       options {
                         cardinality: REQUIRED
                         extra_relation_input_columns_allowed: true
                       }
                       num_occurrences: 1
                     }
                     return_type {
                       kind: ARG_TYPE_FIXED
                       type { type_kind: TYPE_BOOL }
                       options {
                         cardinality: REQUIRED
                         extra_relation_input_columns_allowed: true
                       }
                       num_occurrences: 1
                     }
                     context_id: 105
                     options {
                       is_deprecated: false
                       uses_operation_collation: true
                     }
                   }
                   argument_list {
                     resolved_expression_column_node {
                       parent {
                         type { type_kind: TYPE_INT32 }
                         type_annotation_map {}
                       }
                       name: "foo"
                     }
                   }
                   argument_list {
                     resolved_literal_node {
                       parent {
                         type { type_kind: TYPE_INT32 }
                         type_annotation_map {}
                       }
                       value {
                         type { type_kind: TYPE_INT32 }
                         value { int32_value: 123 }
                       }
                       has_explicit_type: false
                       float_literal_id: 0
                       preserve_in_literal_remover: false
                     }
                   }
                   error_mode: DEFAULT_ERROR_MODE
                 }
                 function_call_info {}
               }
             }
           })pb",
      &expectedResponse2));
  EXPECT_THAT(response2, EqualsProto(expectedResponse2));
}

TEST_F(GoogleSqlLocalServiceImplTest, BuildSqlStatement) {
  const std::string catalog_proto_text = R"pb(
    name: "foo"
    table {
      name: "bar"
      serialization_id: 1
      column {
        name: "baz"
        type { type_kind: TYPE_INT32 }
        is_pseudo_column: false
      }
    })pb";

  SimpleCatalogProto catalog;
  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(catalog_proto_text, &catalog));

  BuildSqlRequest request;
  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(
      R"pb(resolved_statement {
             resolved_query_stmt_node {
               output_column_list {
                 name: "baz"
                 column {
                   column_id: 1
                   table_name: "bar"
                   name: "baz"
                   type { type_kind: TYPE_INT32 }
                 }
               }
               is_value_table: false
               query {
                 resolved_project_scan_node {
                   parent {
                     column_list {
                       column_id: 1
                       table_name: "bar"
                       name: "baz"
                       type { type_kind: TYPE_INT32 }
                     }
                     is_ordered: false
                   }
                   input_scan {
                     resolved_table_scan_node {
                       parent {
                         column_list {
                           column_id: 1
                           table_name: "bar"
                           name: "baz"
                           type { type_kind: TYPE_INT32 }
                         }
                         is_ordered: false
                       }
                       table {
                         name: "bar"
                         serialization_id: 1
                         full_name: "bar"
                       }
                       column_index_list: 0
                       alias: ""
                     }
                   }
                 }
               }
             }
           })pb",
      &request));

  *request.mutable_simple_catalog() = catalog;

  BuildSqlResponse response;
  GOOGLESQL_EXPECT_OK(BuildSql(request, &response));

  BuildSqlResponse expectedResponse;
  expectedResponse.set_sql("SELECT bar.baz AS baz FROM bar");

  EXPECT_THAT(response, EqualsProto(expectedResponse));
}

TEST_F(GoogleSqlLocalServiceImplTest, BuildSqlExpression) {
  BuildSqlRequest request;
  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(
      R"pb(resolved_expression {
             resolved_function_call_base_node {
               resolved_function_call_node {
                 parent {
                   parent { type { type_kind: TYPE_BOOL } }
                   function { name: "GoogleSQL:$less" }
                   signature {
                     argument {
                       kind: ARG_TYPE_FIXED
                       type { type_kind: TYPE_INT32 }
                       options {
                         cardinality: REQUIRED
                         extra_relation_input_columns_allowed: true
                       }
                       num_occurrences: 1
                     }
                     argument {
                       kind: ARG_TYPE_FIXED
                       type { type_kind: TYPE_INT32 }
                       options {
                         cardinality: REQUIRED
                         extra_relation_input_columns_allowed: true
                       }
                       num_occurrences: 1
                     }
                     return_type {
                       kind: ARG_TYPE_FIXED
                       type { type_kind: TYPE_BOOL }
                       options {
                         cardinality: REQUIRED
                         extra_relation_input_columns_allowed: true
                       }
                       num_occurrences: 1
                     }
                     context_id: 105
                     options { is_deprecated: false }
                   }
                   argument_list {
                     resolved_expression_column_node {
                       parent { type { type_kind: TYPE_INT32 } }
                       name: "foo"
                     }
                   }
                   argument_list {
                     resolved_literal_node {
                       parent { type { type_kind: TYPE_INT32 } }
                       value {
                         type { type_kind: TYPE_INT32 }
                         value { int32_value: 123 }
                       }
                       has_explicit_type: false
                       float_literal_id: 0
                     }
                   }
                   error_mode: DEFAULT_ERROR_MODE
                 }
                 function_call_info {}
               }
             }
           })pb",
      &request));
  SimpleCatalogProto catalog;

  googlesql::GoogleSQLBuiltinFunctionOptionsProto options;
  googlesql::GoogleSQLBuiltinFunctionOptionsProto* builtin_function_options =
      catalog.mutable_builtin_function_options();
  *builtin_function_options = options;

  *request.mutable_simple_catalog() = catalog;

  BuildSqlResponse response;
  GOOGLESQL_EXPECT_OK(BuildSql(request, &response));

  BuildSqlResponse expectedResponse;
  expectedResponse.set_sql("foo < (CAST(123 AS INT32))");

  EXPECT_THAT(response, EqualsProto(expectedResponse));
}

TEST_F(GoogleSqlLocalServiceImplTest, FormatSql) {
  FormatSqlRequest request;
  request.set_sql("seLect foo, bar from some_table where something limit 10");

  FormatSqlResponse response;
  GOOGLESQL_EXPECT_OK(FormatSql(request, &response));

  EXPECT_EQ(
      "SELECT\n"
      "  foo,\n"
      "  bar\n"
      "FROM\n"
      "  some_table\n"
      "WHERE\n"
      "  something\n"
      "LIMIT 10;",
      response.sql());
}

TEST_F(GoogleSqlLocalServiceImplTest, LenientFormatSql) {
  // We don't want to test the formatting logic here, only that the service
  // integration with the formatter works. This is why the query is super
  // simple.
  FormatSqlRequest request;
  request.set_sql("SELECT 1");

  FormatSqlResponse response;
  GOOGLESQL_EXPECT_OK(LenientFormatSql(request, &response));

  EXPECT_EQ("SELECT 1\n", response.sql());

  // Test that the format ranges are respected.
  request.set_sql("select 1;select 1;select 1;");
  FormatterRangeProto range_one;
  range_one.set_start(0);
  range_one.set_end(9);
  FormatterRangeProto range_two;
  range_two.set_start(18);
  range_two.set_end(27);
  *request.add_byte_ranges() = range_one;
  *request.add_byte_ranges() = range_two;
  GOOGLESQL_EXPECT_OK(LenientFormatSql(request, &response));

  EXPECT_EQ("SELECT 1;\nselect 1;SELECT 1;\n", response.sql());
}

TEST_F(GoogleSqlLocalServiceImplTest, LenientFormatSqlWithCustomOptions) {
  FormatSqlRequest request;
  request.set_sql(R"(# Test query
    SELECT "a long" + " expression" + " that" + " fits" + " in"
    + " default line_length" FROM Table
    WHERE condition_that_makes_the_entire_query > default_line_length;
  )");
  FormatterOptionsProto* options = request.mutable_options();
  options->set_new_line_type("\n|");
  options->set_line_length_limit(40);
  options->set_indentation_spaces(4);

  FormatSqlResponse response;
  GOOGLESQL_EXPECT_OK(LenientFormatSql(request, &response));

  EXPECT_EQ(R"(# Test query
|SELECT
|    "a long" + " expression" + " that"
|    + " fits" + " in"
|    + " default line_length"
|FROM Table
|WHERE
|    condition_that_makes_the_entire_query
|    > default_line_length;
|)",
            response.sql());

  options->set_allow_invalid_tokens(true);
  request.set_sql("'invalid-token");
  GOOGLESQL_EXPECT_OK(LenientFormatSql(request, &response));
  options->set_allow_invalid_tokens(false);
  EXPECT_THAT(LenientFormatSql(request, &response), Not(IsOk()));
}

TEST_F(GoogleSqlLocalServiceImplTest, GetBuiltinFunctions) {
  GoogleSQLBuiltinFunctionOptionsProto proto;
  GetBuiltinFunctionsResponse response;
  FunctionProto function1;
  FunctionProto function2;
  google::protobuf::TextFormat::ParseFromString(R"(
      language_options {
        name_resolution_mode: NAME_RESOLUTION_DEFAULT
        product_mode: PRODUCT_INTERNAL
        error_on_deprecated_syntax: false
        supported_statement_kinds: RESOLVED_QUERY_STMT
      }
      include_function_ids: FN_CEIL_DOUBLE
      include_function_ids: FN_EQUAL
      include_function_ids: FN_ANY_VALUE
      exclude_function_ids: FN_ABS_DOUBLE
      exclude_function_ids: FN_ANY_VALUE)",
                                      &proto);
  google::protobuf::TextFormat::ParseFromString(R"(
      name_path: "$equal"
      group: "GoogleSQL"
      mode: SCALAR
      signature {
        argument {
          kind: ARG_TYPE_ANY_1
          options {
            cardinality: REQUIRED
            extra_relation_input_columns_allowed: true
          }
          num_occurrences: -1
        }
        argument {
          kind: ARG_TYPE_ANY_1
          options {
            cardinality: REQUIRED
            extra_relation_input_columns_allowed: true
          }
          num_occurrences: -1
        }
        return_type {
          kind: ARG_TYPE_FIXED
          type {
            type_kind: TYPE_BOOL
          }
          options {
            cardinality: REQUIRED
            extra_relation_input_columns_allowed: true
          }
          num_occurrences: -1
        }
        context_id: 42
        options {
          is_deprecated: false
          uses_operation_collation: true
        }
      }
      options {
        supports_over_clause: false
        window_ordering_support: ORDER_UNSUPPORTED
        supports_window_framing: false
        arguments_are_coercible: true
        is_deprecated: false
        alias_name: ""
        sql_name: "="
        allow_external_usage: true
        volatility: IMMUTABLE
        supports_order_by: false
        supports_limit: false
        supports_null_handling_modifier: false
        supports_safe_error_mode: false
        supports_having_modifier: true
        uses_upper_case_sql_name: true
        may_suppress_side_effects: false
      })",
                                      &function1);
  google::protobuf::TextFormat::ParseFromString(R"(
      name_path: "ceil"
      group: "GoogleSQL"
      mode: SCALAR
      signature {
        argument {
          kind: ARG_TYPE_FIXED
          type {
            type_kind: TYPE_DOUBLE
          }
          options {
            cardinality: REQUIRED
            extra_relation_input_columns_allowed: true
          }
          num_occurrences: -1
        }
        return_type {
          kind: ARG_TYPE_FIXED
          type {
            type_kind: TYPE_DOUBLE
          }
          options {
            cardinality: REQUIRED
            extra_relation_input_columns_allowed: true
          }
          num_occurrences: -1
        }
        context_id: 1313
        options {
          is_deprecated: false
        }
      }
      options {
        supports_over_clause: false
        window_ordering_support: ORDER_UNSUPPORTED
        supports_window_framing: false
        arguments_are_coercible: true
        is_deprecated: false
        alias_name: "ceiling"
        sql_name: ""
        allow_external_usage: true
        volatility: IMMUTABLE
        supports_order_by: false
        supports_limit: false
        supports_null_handling_modifier: false
        supports_safe_error_mode: true
        supports_having_modifier: true
        uses_upper_case_sql_name: true
        may_suppress_side_effects: false
      })",
                                      &function2);
  function1.mutable_options()->set_supports_clamped_between_modifier(false);
  function2.mutable_options()->set_supports_clamped_between_modifier(false);

  GOOGLESQL_ASSERT_OK(GetBuiltinFunctions(proto, &response));
  EXPECT_EQ(response.function_size(), 2);
  std::vector<std::string> responses = {absl::StrCat(response.function(0)),
                                        absl::StrCat(response.function(1))};
  EXPECT_THAT(responses, UnorderedElementsAre(Eq(absl::StrCat(function1)),
                                              Eq(absl::StrCat(function2))));
}

TEST_F(GoogleSqlLocalServiceImplTest, GetBuiltinFunctionsReturnsTypes) {
  GoogleSQLBuiltinFunctionOptionsProto proto;
  GetBuiltinFunctionsResponse response;
  google::protobuf::TextFormat::ParseFromString(R"(
      language_options {
        product_mode: PRODUCT_INTERNAL
        error_on_deprecated_syntax: false
        supported_statement_kinds: RESOLVED_QUERY_STMT
        enabled_language_features: FEATURE_ROUND_WITH_ROUNDING_MODE
        enabled_language_features: FEATURE_NUMERIC_TYPE
      }
      include_function_ids: FN_ROUND_WITH_ROUNDING_MODE_NUMERIC)",
                                      &proto);
  FunctionProto expected_round_function;
  google::protobuf::TextFormat::ParseFromString(R"(
      name_path: "round"
      group: "GoogleSQL"
      mode: SCALAR
      signature {
        argument {
          kind: ARG_TYPE_FIXED
          type {
            type_kind: TYPE_NUMERIC
          }
          options {
            cardinality: REQUIRED
            extra_relation_input_columns_allowed: true
          }
          num_occurrences: -1
        }
        argument {
          kind: ARG_TYPE_FIXED
          type {
            type_kind: TYPE_INT64
          }
          options {
            cardinality: REQUIRED
            extra_relation_input_columns_allowed: true
          }
          num_occurrences: -1
        }
        argument {
          kind: ARG_TYPE_FIXED
          type {
            type_kind: TYPE_ENUM
            enum_type {
              enum_name: "googlesql.functions.RoundingMode"
              enum_file_name: "googlesql/public/functions/rounding_mode.proto"
              is_opaque: true
            }
          }
          options {
            cardinality: REQUIRED
            extra_relation_input_columns_allowed: true
          }
          num_occurrences: -1
        }
        return_type {
          kind: ARG_TYPE_FIXED
          type {
            type_kind: TYPE_NUMERIC
          }
          options {
            cardinality: REQUIRED
            extra_relation_input_columns_allowed: true
          }
          num_occurrences: -1
        }
        context_id: 1945
        options {
          is_deprecated: false
        }
      }
      options {
        supports_over_clause: false
        window_ordering_support: ORDER_UNSUPPORTED
        supports_window_framing: false
        arguments_are_coercible: true
        is_deprecated: false
        alias_name: ""
        sql_name: ""
        allow_external_usage: true
        volatility: IMMUTABLE
        supports_order_by: false
        supports_limit: false
        supports_null_handling_modifier: false
        supports_safe_error_mode: true
        supports_having_modifier: true
        uses_upper_case_sql_name: true
        supports_clamped_between_modifier: false
        may_suppress_side_effects: false
      })",
                                      &expected_round_function);

  TypeProto expected_rounding_mode_type;
  google::protobuf::TextFormat::ParseFromString(R"(
        type_kind: TYPE_ENUM
        enum_type {
          enum_name: "googlesql.functions.RoundingMode"
          enum_file_name: "googlesql/public/functions/rounding_mode.proto"
          is_opaque: true
        }
    )",
                                      &expected_rounding_mode_type);

  GOOGLESQL_ASSERT_OK(GetBuiltinFunctions(proto, &response));
  EXPECT_EQ(response.function_size(), 1);
  EXPECT_THAT(response.function(0), EqualsProto(expected_round_function));
  EXPECT_EQ(response.types_size(), 1);
  EXPECT_THAT(response.types().at("ROUNDING_MODE"),
              EqualsProto(expected_rounding_mode_type));
}

TEST_F(GoogleSqlLocalServiceImplTest,
       PrepareQueryNoCatalogWithDescriptorPoolListProto) {
  // Prepare Query
  PrepareQueryRequest prepare_request;
  prepare_request.set_sql(R"(SELECT "apple" AS fruit)");
  prepare_request.mutable_descriptor_pool_list();

  PrepareQueryResponse prepare_response;
  GOOGLESQL_EXPECT_OK(PrepareQuery(prepare_request, &prepare_response));

  EXPECT_EQ(prepare_response.prepared().columns_size(), 1);
  EXPECT_EQ(prepare_response.prepared().columns(0).name(), "fruit");
  ExpectTypeIsString(prepare_response.prepared().columns(0).type());

  EXPECT_EQ(prepare_response.prepared()
                .descriptor_pool_id_list()
                .registered_ids_size(),
            0);

  // Unprepare Query
  GOOGLESQL_EXPECT_OK(UnprepareQuery(prepare_response.prepared().prepared_query_id()));
}

TEST_F(GoogleSqlLocalServiceImplTest,
       PrepareQueryRegisteredCatalogWithDescriptorPoolListProto) {
  // Register Catalog
  RegisterCatalogRequest catalog_request;
  catalog_request.mutable_simple_catalog()->mutable_builtin_function_options();

  // We register the pools with the catalog, even though it doesn't use them
  // it should 'owned' these pools, and let us reference them in the future.
  // fake empty pool to ensure '0' isn't magic
  AddEmptyFileDescriptorSet(catalog_request.mutable_descriptor_pool_list());
  AddBuiltin(catalog_request.mutable_descriptor_pool_list());
  AddKitchenSinkDescriptorPool(catalog_request.mutable_descriptor_pool_list());

  RegisterResponse catalog_response;
  GOOGLESQL_ASSERT_OK(RegisterCatalog(catalog_request, &catalog_response));
  const int64_t catalog_id = catalog_response.registered_id();

  const int64_t empty_descriptor_pool_id =
      catalog_response.descriptor_pool_id_list().registered_ids(0);
  const int64_t kitchen_sink_pool_id =
      catalog_response.descriptor_pool_id_list().registered_ids(2);

  // Prepare Query
  PrepareQueryRequest prepare_request;
  prepare_request.set_registered_catalog_id(catalog_id);
  prepare_request.set_sql(R"(SELECT "apple" AS fruit)");

  AddRegisteredDescriptorPool(prepare_request.mutable_descriptor_pool_list(),
                              empty_descriptor_pool_id);
  AddBuiltin(prepare_request.mutable_descriptor_pool_list());
  AddRegisteredDescriptorPool(prepare_request.mutable_descriptor_pool_list(),
                              kitchen_sink_pool_id);

  PrepareQueryResponse prepare_response;
  GOOGLESQL_EXPECT_OK(PrepareQuery(prepare_request, &prepare_response));

  EXPECT_EQ(prepare_response.prepared().columns_size(), 1);
  EXPECT_EQ(prepare_response.prepared().columns(0).name(), "fruit");
  ExpectTypeIsString(prepare_response.prepared().columns(0).type());

  EXPECT_THAT(
      catalog_response.descriptor_pool_id_list(),
      EqualsProto(prepare_response.prepared().descriptor_pool_id_list()));

  // Unprepare Query
  GOOGLESQL_EXPECT_OK(UnprepareQuery(prepare_response.prepared().prepared_query_id()));

  // Unregister Catalog
  GOOGLESQL_EXPECT_OK(UnregisterCatalog(catalog_response.registered_id()));
}

TEST_F(GoogleSqlLocalServiceImplTest,
       PrepareQueryFullCatalogNoTableDataWithDescriptorPoolListProto) {
  // Prepare Query
  PrepareQueryRequest prepare_request;
  prepare_request.set_sql(R"(SELECT "apple" AS fruit)");
  prepare_request.mutable_simple_catalog()->mutable_builtin_function_options();
  AddTestTable(prepare_request.mutable_simple_catalog()->add_table(),
               "TestTable");

  // fake empty pool to ensure '0' isn't magic
  AddEmptyFileDescriptorSet(prepare_request.mutable_descriptor_pool_list());
  AddBuiltin(prepare_request.mutable_descriptor_pool_list());
  AddKitchenSinkDescriptorPool(prepare_request.mutable_descriptor_pool_list());

  PrepareQueryResponse prepare_response;
  GOOGLESQL_EXPECT_OK(PrepareQuery(prepare_request, &prepare_response));

  EXPECT_EQ(prepare_response.prepared().columns_size(), 1);
  EXPECT_EQ(prepare_response.prepared().columns(0).name(), "fruit");
  ExpectTypeIsString(prepare_response.prepared().columns(0).type());

  EXPECT_EQ(prepare_response.prepared()
                .descriptor_pool_id_list()
                .registered_ids_size(),
            3);

  // Unprepare query
  GOOGLESQL_EXPECT_OK(UnprepareQuery(prepare_response.prepared().prepared_query_id()));
}

TEST_F(GoogleSqlLocalServiceImplTest,
       PrepareQueryFullCatalogTableDataWithDescriptorPoolListProto) {
  // Prepare Query
  PrepareQueryRequest prepare_request;
  prepare_request.set_sql(R"(SELECT "apple" AS fruit)");
  prepare_request.mutable_simple_catalog()->mutable_builtin_function_options();
  AddTestTable(prepare_request.mutable_simple_catalog()->add_table(),
               "TestTable");
  InsertTestTableContent(prepare_request.mutable_table_content(), "TestTable");

  // fake empty pool to ensure '0' isn't magic
  AddEmptyFileDescriptorSet(prepare_request.mutable_descriptor_pool_list());
  AddBuiltin(prepare_request.mutable_descriptor_pool_list());
  AddKitchenSinkDescriptorPool(prepare_request.mutable_descriptor_pool_list());

  PrepareQueryResponse prepare_response;
  GOOGLESQL_EXPECT_OK(PrepareQuery(prepare_request, &prepare_response));

  EXPECT_EQ(prepare_response.prepared().columns_size(), 1);
  EXPECT_EQ(prepare_response.prepared().columns(0).name(), "fruit");
  ExpectTypeIsString(prepare_response.prepared().columns(0).type());

  EXPECT_EQ(prepare_response.prepared()
                .descriptor_pool_id_list()
                .registered_ids_size(),
            3);

  // Unprepare query
  GOOGLESQL_EXPECT_OK(UnprepareQuery(prepare_response.prepared().prepared_query_id()));
}

TEST_F(GoogleSqlLocalServiceImplTest,
       PrepareQueryCleansUpPoolsAndCatalogsOnError) {
  PrepareQueryRequest request;
  PrepareQueryResponse response;

  request.set_sql(R"(SELECT "apple" AS fruit)");

  // Fails on deserializing 3rd pool
  AddBuiltin(request.mutable_descriptor_pool_list());
  AddKitchenSinkDescriptorPool(request.mutable_descriptor_pool_list());
  AddRegisteredDescriptorPool(request.mutable_descriptor_pool_list(), -5);

  ASSERT_FALSE(PrepareQuery(request, &response).ok());
  // No prepared state saved on failure.
  EXPECT_EQ(0, NumSavedPreparedQueries());

  // Fix descriptor pools
  request.mutable_descriptor_pool_list()->Clear();
  AddBuiltin(request.mutable_descriptor_pool_list());
  AddKitchenSinkDescriptorPool(request.mutable_descriptor_pool_list());

  // Bad catalog id
  request.set_registered_catalog_id(-1);
  ASSERT_FALSE(PrepareQuery(request, &response).ok());
  // No prepared state saved on failure.
  EXPECT_EQ(0, NumSavedPreparedQueries());

  // Fix bad catalog id
  request.clear_registered_catalog_id();
  request.mutable_simple_catalog()->add_constant();
  ASSERT_FALSE(PrepareQuery(request, &response).ok());
  // No prepared state saved on failure.
  EXPECT_EQ(0, NumSavedPreparedQueries());

  // make the catalog valid again
  request.mutable_simple_catalog()->clear_constant();

  request.set_sql(R"(SELECT @foo AS fruit)");
  ASSERT_FALSE(PrepareQuery(request, &response).ok());
  // No prepared state saved on failure.
  EXPECT_EQ(0, NumSavedPreparedQueries());

  request.set_sql(R"(SELECT MOD(@foo, @bar) AS modulo)");
  auto* foo_param = request.mutable_options()->add_query_parameters();
  foo_param->set_name("foo");
  foo_param->mutable_type()->set_type_kind(TYPE_INT64);

  ASSERT_FALSE(PrepareQuery(request, &response).ok());
  // No prepared state saved on failure.
  EXPECT_EQ(0, NumSavedPreparedQueries());

  auto* bar_param = request.mutable_options()->add_query_parameters();
  bar_param->set_name("bar");
  bar_param->mutable_type()->set_type_kind(TYPE_STRING);

  ASSERT_FALSE(PrepareQuery(request, &response).ok());
  // No prepared state saved on failure.
  EXPECT_EQ(0, NumSavedPreparedQueries());
}

TEST_F(GoogleSqlLocalServiceImplTest, UnprepareQueryUnknownId) {
  ASSERT_FALSE(UnprepareQuery(10086).ok());
}

TEST_F(GoogleSqlLocalServiceImplTest,
       PrepareModifyRegisteredCatalogWithDescriptorPoolListProto) {
  // Register Catalog
  RegisterCatalogRequest catalog_request;
  catalog_request.mutable_simple_catalog()->mutable_builtin_function_options();
  AddTestTable(catalog_request.mutable_simple_catalog()->add_table(), "table");

  // We register the pools with the catalog, even though it doesn't use them
  // it should 'owned' these pools, and let us reference them in the future.
  // fake empty pool to ensure '0' isn't magic
  AddEmptyFileDescriptorSet(catalog_request.mutable_descriptor_pool_list());
  AddBuiltin(catalog_request.mutable_descriptor_pool_list());
  AddKitchenSinkDescriptorPool(catalog_request.mutable_descriptor_pool_list());

  RegisterResponse catalog_response;
  GOOGLESQL_ASSERT_OK(RegisterCatalog(catalog_request, &catalog_response));
  const int64_t catalog_id = catalog_response.registered_id();

  const int64_t empty_descriptor_pool_id =
      catalog_response.descriptor_pool_id_list().registered_ids(0);
  const int64_t kitchen_sink_pool_id =
      catalog_response.descriptor_pool_id_list().registered_ids(2);

  // Prepare Modify
  PrepareModifyRequest prepare_request;
  prepare_request.set_registered_catalog_id(catalog_id);
  prepare_request.set_sql(R"(DELETE FROM table WHERE true)");

  AddRegisteredDescriptorPool(prepare_request.mutable_descriptor_pool_list(),
                              empty_descriptor_pool_id);
  AddBuiltin(prepare_request.mutable_descriptor_pool_list());
  AddRegisteredDescriptorPool(prepare_request.mutable_descriptor_pool_list(),
                              kitchen_sink_pool_id);

  PrepareModifyResponse prepare_response;
  GOOGLESQL_EXPECT_OK(PrepareModify(prepare_request, &prepare_response));

  EXPECT_THAT(
      catalog_response.descriptor_pool_id_list(),
      EqualsProto(prepare_response.prepared().descriptor_pool_id_list()));

  // Unprepare Modify
  GOOGLESQL_EXPECT_OK(UnprepareModify(prepare_response.prepared().prepared_modify_id()));

  // Unregister Catalog
  GOOGLESQL_EXPECT_OK(UnregisterCatalog(catalog_response.registered_id()));
}

TEST_F(GoogleSqlLocalServiceImplTest,
       PrepareModifyFullCatalogNoTableDataWithDescriptorPoolListProto) {
  // Prepare Modify
  PrepareModifyRequest prepare_request;
  prepare_request.set_sql(R"(DELETE FROM table WHERE true)");
  prepare_request.mutable_simple_catalog()->mutable_builtin_function_options();
  AddTestTable(prepare_request.mutable_simple_catalog()->add_table(), "table");

  // fake empty pool to ensure '0' isn't magic
  AddEmptyFileDescriptorSet(prepare_request.mutable_descriptor_pool_list());
  AddBuiltin(prepare_request.mutable_descriptor_pool_list());
  AddKitchenSinkDescriptorPool(prepare_request.mutable_descriptor_pool_list());

  PrepareModifyResponse prepare_response;
  GOOGLESQL_EXPECT_OK(PrepareModify(prepare_request, &prepare_response));

  EXPECT_EQ(prepare_response.prepared()
                .descriptor_pool_id_list()
                .registered_ids_size(),
            3);

  // Unprepare Modify
  GOOGLESQL_EXPECT_OK(UnprepareModify(prepare_response.prepared().prepared_modify_id()));
}

TEST_F(GoogleSqlLocalServiceImplTest,
       PrepareModifyFullCatalogTableDataWithDescriptorPoolListProto) {
  // Prepare Modify
  PrepareModifyRequest prepare_request;
  prepare_request.set_sql(R"(DELETE FROM table WHERE true)");
  prepare_request.mutable_simple_catalog()->mutable_builtin_function_options();
  AddTestTable(prepare_request.mutable_simple_catalog()->add_table(), "table");
  InsertTestTableContent(prepare_request.mutable_table_content(), "table");

  // fake empty pool to ensure '0' isn't magic
  AddEmptyFileDescriptorSet(prepare_request.mutable_descriptor_pool_list());
  AddBuiltin(prepare_request.mutable_descriptor_pool_list());
  AddKitchenSinkDescriptorPool(prepare_request.mutable_descriptor_pool_list());

  PrepareModifyResponse prepare_response;
  GOOGLESQL_EXPECT_OK(PrepareModify(prepare_request, &prepare_response));

  EXPECT_EQ(prepare_response.prepared()
                .descriptor_pool_id_list()
                .registered_ids_size(),
            3);

  // Unprepare Modify
  GOOGLESQL_EXPECT_OK(UnprepareModify(prepare_response.prepared().prepared_modify_id()));
}

TEST_F(GoogleSqlLocalServiceImplTest,
       PrepareModifyCleansUpPoolsAndCatalogsOnError) {
  PrepareModifyRequest request;
  PrepareModifyResponse response;

  request.set_sql(R"(DELETE FROM table WHERE true)");

  // Fails on deserializing 3rd pool
  AddBuiltin(request.mutable_descriptor_pool_list());
  AddKitchenSinkDescriptorPool(request.mutable_descriptor_pool_list());
  AddRegisteredDescriptorPool(request.mutable_descriptor_pool_list(), -5);

  ASSERT_FALSE(PrepareModify(request, &response).ok());
  // No prepared state saved on failure.
  EXPECT_EQ(0, NumSavedPreparedModifies());

  // Fix descriptor pools
  request.mutable_descriptor_pool_list()->Clear();
  AddBuiltin(request.mutable_descriptor_pool_list());
  AddKitchenSinkDescriptorPool(request.mutable_descriptor_pool_list());

  // Bad catalog id
  request.set_registered_catalog_id(-1);
  ASSERT_FALSE(PrepareModify(request, &response).ok());
  // No prepared state saved on failure.
  EXPECT_EQ(0, NumSavedPreparedModifies());

  // Fix bad catalog id
  request.clear_registered_catalog_id();
  request.mutable_simple_catalog()->add_constant();
  ASSERT_FALSE(PrepareModify(request, &response).ok());
  // No prepared state saved on failure.
  EXPECT_EQ(0, NumSavedPreparedModifies());

  // make the catalog valid again
  request.mutable_simple_catalog()->clear_constant();
  // request.mutable_simple_catalog()->mutable_builtin_function_options();
  AddTestTable(request.mutable_simple_catalog()->add_table(), "table");

  request.set_sql(R"(DELETE FROM table WHERE column_str = @foo)");
  ASSERT_FALSE(PrepareModify(request, &response).ok());
  // No prepared state saved on failure.
  EXPECT_EQ(0, NumSavedPreparedModifies());

  // set the foo parameter with a wrong type
  auto* foo_param = request.mutable_options()->add_query_parameters();
  foo_param->set_name("foo");
  foo_param->mutable_type()->set_type_kind(TYPE_INT64);

  ASSERT_FALSE(PrepareModify(request, &response).ok());
  // No prepared state saved on failure.
  EXPECT_EQ(0, NumSavedPreparedModifies());
}

TEST_F(GoogleSqlLocalServiceImplTest, UnprepareModifyUnknownId) {
  ASSERT_FALSE(UnprepareModify(10086).ok());
}

TEST_F(GoogleSqlLocalServiceImplTest, EvaluateQueryWithSql) {
  // Evaluate Query
  EvaluateQueryRequest evaluate_request;
  evaluate_request.set_sql(R"(SELECT "apple" AS fruit)");

  EvaluateQueryResponse evaluate_response;
  GOOGLESQL_EXPECT_OK(EvaluateQuery(evaluate_request, &evaluate_response));

  EXPECT_EQ(evaluate_response.prepared().columns_size(), 1);
  EXPECT_EQ(evaluate_response.prepared().columns(0).name(), "fruit");
  ExpectTypeIsString(evaluate_response.prepared().columns(0).type());
  EXPECT_EQ(evaluate_response.prepared()
                .descriptor_pool_id_list()
                .registered_ids_size(),
            0);

  EXPECT_EQ(evaluate_response.content().table_data().row_size(), 1);
  const TableData::Row row_0 = evaluate_response.content().table_data().row(0);
  EXPECT_EQ(row_0.cell_size(), 1);
  ExpectValueIsString(row_0.cell(0), "apple");
}

TEST_F(GoogleSqlLocalServiceImplTest, EvaluateQueryWithSqlWithParam) {
  // Evaluate Query
  EvaluateQueryRequest evaluate_request;
  // Note: for now, evaluate without prepare always creates a default simple
  // catalog with all builtin function included.
  evaluate_request.set_sql(R"(SELECT @foo AS fruit)");

  auto* foo_query_param =
      evaluate_request.mutable_options()->add_query_parameters();
  foo_query_param->set_name("foo");
  foo_query_param->mutable_type()->set_type_kind(TYPE_STRING);

  auto* foo_param = evaluate_request.mutable_params()->Add();
  foo_param->set_name("foo");
  foo_param->mutable_value()->set_string_value("cherry");

  EvaluateQueryResponse evaluate_response;
  GOOGLESQL_EXPECT_OK(EvaluateQuery(evaluate_request, &evaluate_response));

  EXPECT_EQ(evaluate_response.prepared().columns_size(), 1);
  EXPECT_EQ(evaluate_response.prepared().columns(0).name(), "fruit");
  ExpectTypeIsString(evaluate_response.prepared().columns(0).type());
  EXPECT_EQ(evaluate_response.prepared()
                .descriptor_pool_id_list()
                .registered_ids_size(),
            0);

  EXPECT_EQ(evaluate_response.content().table_data().row_size(), 1);
  const TableData::Row row_0 = evaluate_response.content().table_data().row(0);
  EXPECT_EQ(row_0.cell_size(), 1);
  ExpectValueIsString(row_0.cell(0), "cherry");
}

TEST_F(GoogleSqlLocalServiceImplTest,
       EvaluateQueryWithDescriptorPoolListProto) {
  // Evaluate Query
  EvaluateQueryRequest evaluate_request;
  evaluate_request.set_sql(R"(SELECT "apple" AS fruit)");

  // fake empty pool to ensure '0' isn't magic
  AddEmptyFileDescriptorSet(evaluate_request.mutable_descriptor_pool_list());
  AddBuiltin(evaluate_request.mutable_descriptor_pool_list());
  AddKitchenSinkDescriptorPool(evaluate_request.mutable_descriptor_pool_list());

  EvaluateQueryResponse evaluate_response;
  GOOGLESQL_EXPECT_OK(EvaluateQuery(evaluate_request, &evaluate_response));

  EXPECT_EQ(evaluate_response.prepared().columns_size(), 1);
  EXPECT_EQ(evaluate_response.prepared().columns(0).name(), "fruit");
  ExpectTypeIsString(evaluate_response.prepared().columns(0).type());
  EXPECT_EQ(evaluate_response.prepared()
                .descriptor_pool_id_list()
                .registered_ids_size(),
            0);

  EXPECT_EQ(evaluate_response.content().table_data().row_size(), 1);
  const TableData::Row row_0 = evaluate_response.content().table_data().row(0);
  EXPECT_EQ(row_0.cell_size(), 1);
  ExpectValueIsString(row_0.cell(0), "apple");
}

TEST_F(GoogleSqlLocalServiceImplTest,
       EvaluateQueryWithDescriptorPoolListProtoWithParam) {
  // Evaluate Query
  EvaluateQueryRequest evaluate_request;
  // Note: for now, evaluate without prepare always creates a default simple
  // catalog with all builtin function included.
  evaluate_request.set_sql(R"(SELECT @foo AS fruit)");

  // fake empty pool to ensure '0' isn't magic
  AddEmptyFileDescriptorSet(evaluate_request.mutable_descriptor_pool_list());
  AddBuiltin(evaluate_request.mutable_descriptor_pool_list());
  AddKitchenSinkDescriptorPool(evaluate_request.mutable_descriptor_pool_list());

  auto* foo_query_param =
      evaluate_request.mutable_options()->add_query_parameters();
  foo_query_param->set_name("foo");
  foo_query_param->mutable_type()->set_type_kind(TYPE_STRING);

  auto* foo_param = evaluate_request.mutable_params()->Add();
  foo_param->set_name("foo");
  foo_param->mutable_value()->set_string_value("apple");

  EvaluateQueryResponse evaluate_response;
  GOOGLESQL_EXPECT_OK(EvaluateQuery(evaluate_request, &evaluate_response));

  EXPECT_EQ(evaluate_response.prepared().columns_size(), 1);
  EXPECT_EQ(evaluate_response.prepared().columns(0).name(), "fruit");
  ExpectTypeIsString(evaluate_response.prepared().columns(0).type());
  EXPECT_EQ(evaluate_response.prepared()
                .descriptor_pool_id_list()
                .registered_ids_size(),
            0);

  EXPECT_EQ(evaluate_response.content().table_data().row_size(), 1);
  const TableData::Row row_0 = evaluate_response.content().table_data().row(0);
  EXPECT_EQ(row_0.cell_size(), 1);
  ExpectValueIsString(row_0.cell(0), "apple");
}

TEST_F(GoogleSqlLocalServiceImplTest, EvaluateQueryErrors) {
  EvaluateQueryRequest request;
  EvaluateQueryResponse response;

  // Invalid SQL query
  request.set_sql(R"(Invalid SQL)");
  ASSERT_FALSE(EvaluateQuery(request, &response).ok());
  // No prepared state saved on failure.
  EXPECT_EQ(0, NumSavedPreparedQueries());

  // Non-existent table
  request.set_sql(R"(SELECT * FROM non_existent_table)");
  ASSERT_FALSE(EvaluateQuery(request, &response).ok());
  // No prepared state saved on failure.
  EXPECT_EQ(0, NumSavedPreparedQueries());

  request.set_sql(R"(SELECT "apple" AS fruit)");

  // Fails on deserializing 3rd pool
  AddBuiltin(request.mutable_descriptor_pool_list());
  AddKitchenSinkDescriptorPool(request.mutable_descriptor_pool_list());
  AddRegisteredDescriptorPool(request.mutable_descriptor_pool_list(), -5);

  ASSERT_FALSE(EvaluateQuery(request, &response).ok());
  // No prepared state saved on failure.
  EXPECT_EQ(0, NumSavedPreparedQueries());

  // Fix descriptor pools
  request.mutable_descriptor_pool_list()->Clear();
  AddBuiltin(request.mutable_descriptor_pool_list());
  AddKitchenSinkDescriptorPool(request.mutable_descriptor_pool_list());

  request.set_sql(R"(SELECT @foo AS fruit)");
  ASSERT_FALSE(EvaluateQuery(request, &response).ok());
  // No prepared state saved on failure.
  EXPECT_EQ(0, NumSavedPreparedQueries());

  request.set_sql(R"(SELECT MOD(@foo, @bar) AS modulo)");
  auto* foo_param = request.mutable_options()->add_query_parameters();
  foo_param->set_name("foo");
  foo_param->mutable_type()->set_type_kind(TYPE_INT64);

  ASSERT_FALSE(EvaluateQuery(request, &response).ok());
  // No prepared state saved on failure.
  EXPECT_EQ(0, NumSavedPreparedQueries());

  auto* bar_param = request.mutable_options()->add_query_parameters();
  bar_param->set_name("bar");
  bar_param->mutable_type()->set_type_kind(TYPE_STRING);

  ASSERT_FALSE(EvaluateQuery(request, &response).ok());
  // No prepared state saved on failure.
  EXPECT_EQ(0, NumSavedPreparedQueries());
}

TEST_F(GoogleSqlLocalServiceImplTest, EvaluateQueryWithFullCatalogNoTableData) {
  // Evaluate Query
  EvaluateQueryRequest evaluate_request;
  evaluate_request.set_sql(R"(SELECT * FROM TestTable)");
  evaluate_request.mutable_simple_catalog()->mutable_builtin_function_options();
  AddTestTable(evaluate_request.mutable_simple_catalog()->add_table(),
               "TestTable");

  EvaluateQueryResponse evaluate_response;
  EXPECT_EQ(EvaluateQuery(evaluate_request, &evaluate_response).code(),
            absl::StatusCode::kInvalidArgument);
}

TEST_F(GoogleSqlLocalServiceImplTest,
       EvaluateQueryWithDescriptorPoolListProtoWithFullCatalogNoTableData) {
  // Evaluate Query
  EvaluateQueryRequest evaluate_request;
  evaluate_request.set_sql(R"(SELECT * FROM TestTable)");
  evaluate_request.mutable_simple_catalog()->mutable_builtin_function_options();
  AddTestTable(evaluate_request.mutable_simple_catalog()->add_table(),
               "TestTable");

  // fake empty pool to ensure '0' isn't magic
  AddEmptyFileDescriptorSet(evaluate_request.mutable_descriptor_pool_list());
  AddBuiltin(evaluate_request.mutable_descriptor_pool_list());
  AddKitchenSinkDescriptorPool(evaluate_request.mutable_descriptor_pool_list());

  EvaluateQueryResponse evaluate_response;
  EXPECT_EQ(EvaluateQuery(evaluate_request, &evaluate_response).code(),
            absl::StatusCode::kInvalidArgument);
}

TEST_F(GoogleSqlLocalServiceImplTest, EvaluateQueryWithFullCatalogTableData) {
  // Evaluate Query
  EvaluateQueryRequest evaluate_request;
  evaluate_request.set_sql(
      R"(SELECT column_int FROM TestTable WHERE column_str = "string_1")");
  evaluate_request.mutable_simple_catalog()->mutable_builtin_function_options();
  AddTestTable(evaluate_request.mutable_simple_catalog()->add_table(),
               "TestTable");
  InsertTestTableContent(evaluate_request.mutable_table_content(), "TestTable");

  EvaluateQueryResponse evaluate_response;
  GOOGLESQL_EXPECT_OK(EvaluateQuery(evaluate_request, &evaluate_response));

  EXPECT_EQ(evaluate_response.prepared().columns_size(), 1);
  EXPECT_EQ(evaluate_response.prepared().columns(0).name(), "column_int");
  ExpectTypeIsInt32(evaluate_response.prepared().columns(0).type());
  // No registered prepared query
  EXPECT_FALSE(evaluate_response.prepared().has_prepared_query_id());

  EXPECT_EQ(evaluate_response.prepared()
                .descriptor_pool_id_list()
                .registered_ids_size(),
            0);

  EXPECT_EQ(evaluate_response.content().table_data().row_size(), 1);
  const TableData::Row row_0 = evaluate_response.content().table_data().row(0);
  EXPECT_EQ(row_0.cell_size(), 1);
  ExpectValueIsInt32(row_0.cell(0), 123);
}

TEST_F(GoogleSqlLocalServiceImplTest,
       EvaluateQueryWithDescriptorPoolListProtoWithFullCatalogTableData) {
  // Evaluate Query
  EvaluateQueryRequest evaluate_request;
  evaluate_request.set_sql(
      R"(SELECT column_int FROM TestTable WHERE column_str = "string_1")");
  evaluate_request.mutable_simple_catalog()->mutable_builtin_function_options();
  AddTestTable(evaluate_request.mutable_simple_catalog()->add_table(),
               "TestTable");
  InsertTestTableContent(evaluate_request.mutable_table_content(), "TestTable");

  // fake empty pool to ensure '0' isn't magic
  AddEmptyFileDescriptorSet(evaluate_request.mutable_descriptor_pool_list());
  AddBuiltin(evaluate_request.mutable_descriptor_pool_list());
  AddKitchenSinkDescriptorPool(evaluate_request.mutable_descriptor_pool_list());

  EvaluateQueryResponse evaluate_response;
  GOOGLESQL_EXPECT_OK(EvaluateQuery(evaluate_request, &evaluate_response));

  EXPECT_EQ(evaluate_response.prepared().columns_size(), 1);
  EXPECT_EQ(evaluate_response.prepared().columns(0).name(), "column_int");
  ExpectTypeIsInt32(evaluate_response.prepared().columns(0).type());
  // No registered prepared query
  EXPECT_FALSE(evaluate_response.prepared().has_prepared_query_id());

  EXPECT_EQ(evaluate_response.prepared()
                .descriptor_pool_id_list()
                .registered_ids_size(),
            0);

  EXPECT_EQ(evaluate_response.content().table_data().row_size(), 1);
  const TableData::Row row_0 = evaluate_response.content().table_data().row(0);
  EXPECT_EQ(row_0.cell_size(), 1);
  ExpectValueIsInt32(row_0.cell(0), 123);
}

TEST_F(GoogleSqlLocalServiceImplTest,
       EvaluateQueryWithRegisteredCatalogNoTableData) {
  // Register Catalog
  RegisterCatalogRequest catalog_request;
  catalog_request.mutable_simple_catalog()->mutable_builtin_function_options();
  AddTestTable(catalog_request.mutable_simple_catalog()->add_table(),
               "TestTable");

  RegisterResponse catalog_response;
  GOOGLESQL_ASSERT_OK(RegisterCatalog(catalog_request, &catalog_response));
  const int64_t catalog_id = catalog_response.registered_id();

  // Evaluate Query
  EvaluateQueryRequest evaluate_request;
  evaluate_request.set_sql(R"(SELECT * FROM TestTable)");
  evaluate_request.set_registered_catalog_id(catalog_id);

  EvaluateQueryResponse evaluate_response;
  EXPECT_EQ(EvaluateQuery(evaluate_request, &evaluate_response).code(),
            absl::StatusCode::kInvalidArgument);

  // Unregister Catalog
  GOOGLESQL_EXPECT_OK(UnregisterCatalog(catalog_id));
}

TEST_F(
    GoogleSqlLocalServiceImplTest,
    EvaluateQueryWithDescriptorPoolListProtoWithRegisteredCatalogNoTableData) {
  // Register Catalog
  RegisterCatalogRequest catalog_request;
  catalog_request.mutable_simple_catalog()->mutable_builtin_function_options();
  AddTestTable(catalog_request.mutable_simple_catalog()->add_table(),
               "TestTable");

  RegisterResponse catalog_response;
  GOOGLESQL_ASSERT_OK(RegisterCatalog(catalog_request, &catalog_response));
  const int64_t catalog_id = catalog_response.registered_id();

  // Evaluate Query
  EvaluateQueryRequest evaluate_request;
  evaluate_request.set_sql(R"(SELECT * FROM TestTable)");
  evaluate_request.set_registered_catalog_id(catalog_id);

  // fake empty pool to ensure '0' isn't magic
  AddEmptyFileDescriptorSet(evaluate_request.mutable_descriptor_pool_list());
  AddBuiltin(evaluate_request.mutable_descriptor_pool_list());
  AddKitchenSinkDescriptorPool(evaluate_request.mutable_descriptor_pool_list());

  EvaluateQueryResponse evaluate_response;
  EXPECT_EQ(EvaluateQuery(evaluate_request, &evaluate_response).code(),
            absl::StatusCode::kInvalidArgument);

  // Unregister Catalog
  GOOGLESQL_EXPECT_OK(UnregisterCatalog(catalog_id));
}

TEST_F(GoogleSqlLocalServiceImplTest,
       EvaluateQueryWithRegisteredCatalogEmptyTableData) {
  // Register Catalog
  RegisterCatalogRequest catalog_request;
  catalog_request.mutable_simple_catalog()->mutable_builtin_function_options();
  AddTestTable(catalog_request.mutable_simple_catalog()->add_table(),
               "TestTable");
  InsertEmptyTestTableContent(catalog_request.mutable_table_content(),
                              "TestTable");

  RegisterResponse catalog_response;
  GOOGLESQL_ASSERT_OK(RegisterCatalog(catalog_request, &catalog_response));
  const int64_t catalog_id = catalog_response.registered_id();

  // Evaluate Query
  EvaluateQueryRequest evaluate_request;
  evaluate_request.set_sql(R"(SELECT * FROM TestTable)");
  evaluate_request.set_registered_catalog_id(catalog_id);

  EvaluateQueryResponse evaluate_response;
  GOOGLESQL_EXPECT_OK(EvaluateQuery(evaluate_request, &evaluate_response));

  EXPECT_EQ(evaluate_response.prepared().columns_size(), 3);
  EXPECT_EQ(evaluate_response.prepared().columns(0).name(), "column_str");
  ExpectTypeIsString(evaluate_response.prepared().columns(0).type());
  EXPECT_EQ(evaluate_response.prepared().columns(1).name(), "column_bool");
  ExpectTypeIsBool(evaluate_response.prepared().columns(1).type());
  EXPECT_EQ(evaluate_response.prepared().columns(2).name(), "column_int");
  ExpectTypeIsInt32(evaluate_response.prepared().columns(2).type());
  // No registered prepared query
  EXPECT_FALSE(evaluate_response.prepared().has_prepared_query_id());

  EXPECT_EQ(evaluate_response.prepared()
                .descriptor_pool_id_list()
                .registered_ids_size(),
            0);

  EXPECT_EQ(evaluate_response.content().table_data().row_size(), 0);

  // Unregister Catalog
  GOOGLESQL_EXPECT_OK(UnregisterCatalog(catalog_id));
}

TEST_F(GoogleSqlLocalServiceImplTest,
       EvaluateQueryWithRegisteredCatalogWithTableData) {
  // Register Catalog
  RegisterCatalogRequest catalog_request;
  catalog_request.mutable_simple_catalog()->mutable_builtin_function_options();
  AddTestTable(catalog_request.mutable_simple_catalog()->add_table(),
               "TestTable");
  InsertTestTableContent(catalog_request.mutable_table_content(), "TestTable");

  RegisterResponse catalog_response;
  GOOGLESQL_ASSERT_OK(RegisterCatalog(catalog_request, &catalog_response));
  const int64_t catalog_id = catalog_response.registered_id();

  // Evaluate Query
  EvaluateQueryRequest evaluate_request;
  evaluate_request.set_sql(
      R"(SELECT * FROM TestTable WHERE column_str = "string_1")");
  evaluate_request.set_registered_catalog_id(catalog_id);

  EvaluateQueryResponse evaluate_response;
  GOOGLESQL_EXPECT_OK(EvaluateQuery(evaluate_request, &evaluate_response));

  EXPECT_EQ(evaluate_response.prepared().columns_size(), 3);
  EXPECT_EQ(evaluate_response.prepared().columns(0).name(), "column_str");
  ExpectTypeIsString(evaluate_response.prepared().columns(0).type());
  EXPECT_EQ(evaluate_response.prepared().columns(1).name(), "column_bool");
  ExpectTypeIsBool(evaluate_response.prepared().columns(1).type());
  EXPECT_EQ(evaluate_response.prepared().columns(2).name(), "column_int");
  ExpectTypeIsInt32(evaluate_response.prepared().columns(2).type());
  // No registered prepared query
  EXPECT_FALSE(evaluate_response.prepared().has_prepared_query_id());

  EXPECT_EQ(evaluate_response.prepared()
                .descriptor_pool_id_list()
                .registered_ids_size(),
            0);

  EXPECT_EQ(evaluate_response.content().table_data().row_size(), 1);
  const TableData::Row row_0 = evaluate_response.content().table_data().row(0);
  EXPECT_EQ(row_0.cell_size(), 3);
  ExpectValueIsString(row_0.cell(0), "string_1");
  ExpectValueIsBool(row_0.cell(1), true);
  ExpectValueIsInt32(row_0.cell(2), 123);

  // Unregister Catalog
  GOOGLESQL_EXPECT_OK(UnregisterCatalog(catalog_id));
}

TEST_F(GoogleSqlLocalServiceImplTest,
       EvaluateQueryWithRegisteredCatalogWithParamEmptyTableData) {
  // Register Catalog
  RegisterCatalogRequest catalog_request;
  catalog_request.mutable_simple_catalog()->mutable_builtin_function_options();
  AddTestTable(catalog_request.mutable_simple_catalog()->add_table(),
               "TestTable");
  InsertEmptyTestTableContent(catalog_request.mutable_table_content(),
                              "TestTable");

  RegisterResponse catalog_response;
  GOOGLESQL_ASSERT_OK(RegisterCatalog(catalog_request, &catalog_response));
  const int64_t catalog_id = catalog_response.registered_id();

  // Evaluate Query
  EvaluateQueryRequest evaluate_request;
  evaluate_request.set_sql(
      R"(SELECT * FROM TestTable WHERE column_str = "@str")");
  evaluate_request.set_registered_catalog_id(catalog_id);

  auto* str_query_param =
      evaluate_request.mutable_options()->add_query_parameters();
  str_query_param->set_name("str");
  str_query_param->mutable_type()->set_type_kind(TYPE_STRING);

  auto* str_param = evaluate_request.mutable_params()->Add();
  str_param->set_name("str");
  str_param->mutable_value()->set_string_value("string_1");

  EvaluateQueryResponse evaluate_response;
  GOOGLESQL_EXPECT_OK(EvaluateQuery(evaluate_request, &evaluate_response));

  EXPECT_EQ(evaluate_response.prepared().columns_size(), 3);
  EXPECT_EQ(evaluate_response.prepared().columns(0).name(), "column_str");
  ExpectTypeIsString(evaluate_response.prepared().columns(0).type());
  EXPECT_EQ(evaluate_response.prepared().columns(1).name(), "column_bool");
  ExpectTypeIsBool(evaluate_response.prepared().columns(1).type());
  EXPECT_EQ(evaluate_response.prepared().columns(2).name(), "column_int");
  ExpectTypeIsInt32(evaluate_response.prepared().columns(2).type());
  // No registered prepared query
  EXPECT_FALSE(evaluate_response.prepared().has_prepared_query_id());

  EXPECT_EQ(evaluate_response.prepared()
                .descriptor_pool_id_list()
                .registered_ids_size(),
            0);

  EXPECT_EQ(evaluate_response.content().table_data().row_size(), 0);

  // Unregister Catalog
  GOOGLESQL_EXPECT_OK(UnregisterCatalog(catalog_id));
}

TEST_F(GoogleSqlLocalServiceImplTest,
       EvaluateQueryWithRegisteredCatalogWithParamWithTableData) {
  // Register Catalog
  RegisterCatalogRequest catalog_request;
  catalog_request.mutable_simple_catalog()->mutable_builtin_function_options();
  AddTestTable(catalog_request.mutable_simple_catalog()->add_table(),
               "TestTable");
  InsertTestTableContent(catalog_request.mutable_table_content(), "TestTable");

  RegisterResponse catalog_response;
  GOOGLESQL_ASSERT_OK(RegisterCatalog(catalog_request, &catalog_response));
  const int64_t catalog_id = catalog_response.registered_id();

  // Evaluate Query
  EvaluateQueryRequest evaluate_request;
  evaluate_request.set_sql(
      R"(SELECT * FROM TestTable WHERE column_str = @str)");
  evaluate_request.set_registered_catalog_id(catalog_id);

  auto* str_query_param =
      evaluate_request.mutable_options()->add_query_parameters();
  str_query_param->set_name("str");
  str_query_param->mutable_type()->set_type_kind(TYPE_STRING);

  auto* str_param = evaluate_request.mutable_params()->Add();
  str_param->set_name("str");
  str_param->mutable_value()->set_string_value("non_existent_string");

  EvaluateQueryResponse evaluate_response;
  GOOGLESQL_EXPECT_OK(EvaluateQuery(evaluate_request, &evaluate_response));

  EXPECT_EQ(evaluate_response.prepared().columns_size(), 3);
  EXPECT_EQ(evaluate_response.prepared().columns(0).name(), "column_str");
  ExpectTypeIsString(evaluate_response.prepared().columns(0).type());
  EXPECT_EQ(evaluate_response.prepared().columns(1).name(), "column_bool");
  ExpectTypeIsBool(evaluate_response.prepared().columns(1).type());
  EXPECT_EQ(evaluate_response.prepared().columns(2).name(), "column_int");
  ExpectTypeIsInt32(evaluate_response.prepared().columns(2).type());
  // No registered prepared query
  EXPECT_FALSE(evaluate_response.prepared().has_prepared_query_id());

  EXPECT_EQ(evaluate_response.prepared()
                .descriptor_pool_id_list()
                .registered_ids_size(),
            0);

  EXPECT_EQ(evaluate_response.content().table_data().row_size(), 0);

  // Evaluate Query returning data
  str_param->mutable_value()->set_string_value("string_1");

  EvaluateQueryResponse evaluate_response_2;
  GOOGLESQL_EXPECT_OK(EvaluateQuery(evaluate_request, &evaluate_response_2));

  EXPECT_EQ(evaluate_response_2.prepared().columns_size(), 3);
  EXPECT_EQ(evaluate_response_2.prepared().columns(0).name(), "column_str");
  ExpectTypeIsString(evaluate_response_2.prepared().columns(0).type());
  EXPECT_EQ(evaluate_response_2.prepared().columns(1).name(), "column_bool");
  ExpectTypeIsBool(evaluate_response_2.prepared().columns(1).type());
  EXPECT_EQ(evaluate_response_2.prepared().columns(2).name(), "column_int");
  ExpectTypeIsInt32(evaluate_response_2.prepared().columns(2).type());
  // No registered prepared query
  EXPECT_FALSE(evaluate_response_2.prepared().has_prepared_query_id());

  EXPECT_EQ(evaluate_response_2.prepared()
                .descriptor_pool_id_list()
                .registered_ids_size(),
            0);

  EXPECT_EQ(evaluate_response_2.content().table_data().row_size(), 1);
  const TableData::Row row_0 =
      evaluate_response_2.content().table_data().row(0);
  EXPECT_EQ(row_0.cell_size(), 3);
  ExpectValueIsString(row_0.cell(0), "string_1");
  ExpectValueIsBool(row_0.cell(1), true);
  ExpectValueIsInt32(row_0.cell(2), 123);

  // Unregister Catalog
  GOOGLESQL_EXPECT_OK(UnregisterCatalog(catalog_id));
}

TEST_F(GoogleSqlLocalServiceImplTest,
       EvaluateQueryWithRegisteredCatalogWithTableContentField) {
  // Register Catalog
  RegisterCatalogRequest catalog_request;
  catalog_request.mutable_simple_catalog()->mutable_builtin_function_options();
  AddTestTable(catalog_request.mutable_simple_catalog()->add_table(),
               "TestTable");

  RegisterResponse catalog_response;
  GOOGLESQL_ASSERT_OK(RegisterCatalog(catalog_request, &catalog_response));
  const int64_t catalog_id = catalog_response.registered_id();

  // Evaluate Query
  EvaluateQueryRequest evaluate_request;
  evaluate_request.set_sql(
      R"(SELECT * FROM TestTable WHERE column_str = "string_1")");
  evaluate_request.set_registered_catalog_id(catalog_id);
  InsertTestTableContent(evaluate_request.mutable_table_content(), "TestTable");

  EvaluateQueryResponse evaluate_response;
  EXPECT_EQ(EvaluateQuery(evaluate_request, &evaluate_response).code(),
            absl::StatusCode::kInvalidArgument);

  // Unregister Catalog
  GOOGLESQL_EXPECT_OK(UnregisterCatalog(catalog_id));
}

TEST_F(GoogleSqlLocalServiceImplTest,
       EvaluateQueryWithRegisteredQueryNoTableData) {
  // Register Catalog
  RegisterCatalogRequest catalog_request;
  catalog_request.mutable_simple_catalog()->mutable_builtin_function_options();
  AddTestTable(catalog_request.mutable_simple_catalog()->add_table(),
               "TestTable");

  // We register the pools with the catalog, even though it doesn't use them
  // it should 'owned' these pools, and let us reference them in the future.
  // fake empty pool to ensure '0' isn't magic
  AddEmptyFileDescriptorSet(catalog_request.mutable_descriptor_pool_list());
  AddBuiltin(catalog_request.mutable_descriptor_pool_list());
  AddKitchenSinkDescriptorPool(catalog_request.mutable_descriptor_pool_list());

  RegisterResponse catalog_response;
  GOOGLESQL_ASSERT_OK(RegisterCatalog(catalog_request, &catalog_response));
  const int64_t catalog_id = catalog_response.registered_id();

  // Prepare Query
  PrepareQueryRequest prepare_request;
  prepare_request.set_registered_catalog_id(catalog_id);
  prepare_request.set_sql(R"(SELECT * FROM TestTable)");

  PrepareQueryResponse prepare_response;
  GOOGLESQL_EXPECT_OK(PrepareQuery(prepare_request, &prepare_response));
  const int64_t prepared_query_id =
      prepare_response.prepared().prepared_query_id();

  // Evaluate Query
  EvaluateQueryRequest evaluate_request;
  evaluate_request.set_prepared_query_id(prepared_query_id);

  EvaluateQueryResponse evaluate_response;
  EXPECT_EQ(EvaluateQuery(evaluate_request, &evaluate_response).code(),
            absl::StatusCode::kInvalidArgument);

  // Unprepare Query
  GOOGLESQL_EXPECT_OK(UnprepareQuery(prepared_query_id));

  // Unregister Catalog
  GOOGLESQL_EXPECT_OK(UnregisterCatalog(catalog_id));
}

TEST_F(GoogleSqlLocalServiceImplTest,
       EvaluateQueryWithRegisteredQueryEmptyTableData) {
  // Register Catalog
  RegisterCatalogRequest catalog_request;
  catalog_request.mutable_simple_catalog()->mutable_builtin_function_options();
  AddTestTable(catalog_request.mutable_simple_catalog()->add_table(),
               "TestTable");
  InsertEmptyTestTableContent(catalog_request.mutable_table_content(),
                              "TestTable");

  // We register the pools with the catalog, even though it doesn't use them
  // it should 'owned' these pools, and let us reference them in the future.
  // fake empty pool to ensure '0' isn't magic
  AddEmptyFileDescriptorSet(catalog_request.mutable_descriptor_pool_list());
  AddBuiltin(catalog_request.mutable_descriptor_pool_list());
  AddKitchenSinkDescriptorPool(catalog_request.mutable_descriptor_pool_list());

  RegisterResponse catalog_response;
  GOOGLESQL_ASSERT_OK(RegisterCatalog(catalog_request, &catalog_response));
  const int64_t catalog_id = catalog_response.registered_id();

  // Prepare Query
  PrepareQueryRequest prepare_request;
  prepare_request.set_registered_catalog_id(catalog_id);
  prepare_request.set_sql(R"(SELECT * FROM TestTable)");

  PrepareQueryResponse prepare_response;
  GOOGLESQL_EXPECT_OK(PrepareQuery(prepare_request, &prepare_response));
  const int64_t prepared_query_id =
      prepare_response.prepared().prepared_query_id();

  // Evaluate Query
  EvaluateQueryRequest evaluate_request;
  evaluate_request.set_prepared_query_id(prepared_query_id);

  EvaluateQueryResponse evaluate_response;
  GOOGLESQL_EXPECT_OK(EvaluateQuery(evaluate_request, &evaluate_response));

  // Prepared Query State is not being returned when using a prepared query id
  ASSERT_FALSE(evaluate_response.has_prepared());

  EXPECT_EQ(evaluate_response.content().table_data().row_size(), 0);

  // Unprepare Query
  GOOGLESQL_EXPECT_OK(UnprepareQuery(prepared_query_id));

  // Unregister Catalog
  GOOGLESQL_EXPECT_OK(UnregisterCatalog(catalog_id));
}

TEST_F(GoogleSqlLocalServiceImplTest,
       EvaluateQueryWithRegisteredQueryWithTableData) {
  // Register Catalog
  RegisterCatalogRequest catalog_request;
  catalog_request.mutable_simple_catalog()->mutable_builtin_function_options();
  AddTestTable(catalog_request.mutable_simple_catalog()->add_table(),
               "TestTable");
  InsertTestTableContent(catalog_request.mutable_table_content(), "TestTable");

  // We register the pools with the catalog, even though it doesn't use them
  // it should 'owned' these pools, and let us reference them in the future.
  // fake empty pool to ensure '0' isn't magic
  AddEmptyFileDescriptorSet(catalog_request.mutable_descriptor_pool_list());
  AddBuiltin(catalog_request.mutable_descriptor_pool_list());
  AddKitchenSinkDescriptorPool(catalog_request.mutable_descriptor_pool_list());

  RegisterResponse catalog_response;
  GOOGLESQL_ASSERT_OK(RegisterCatalog(catalog_request, &catalog_response));
  const int64_t catalog_id = catalog_response.registered_id();

  // Prepare Query
  PrepareQueryRequest prepare_request;
  prepare_request.set_registered_catalog_id(catalog_id);
  prepare_request.set_sql(
      R"(SELECT * FROM TestTable WHERE column_str = "string_1")");

  PrepareQueryResponse prepare_response;
  GOOGLESQL_EXPECT_OK(PrepareQuery(prepare_request, &prepare_response));
  const int64_t prepared_query_id =
      prepare_response.prepared().prepared_query_id();

  // Evaluate Query
  EvaluateQueryRequest evaluate_request;
  evaluate_request.set_prepared_query_id(prepared_query_id);

  EvaluateQueryResponse evaluate_response;
  GOOGLESQL_EXPECT_OK(EvaluateQuery(evaluate_request, &evaluate_response));

  // Prepared Query State is not being returned when using a prepared query id
  ASSERT_FALSE(evaluate_response.has_prepared());

  EXPECT_EQ(evaluate_response.content().table_data().row_size(), 1);
  const TableData::Row row_0 = evaluate_response.content().table_data().row(0);
  EXPECT_EQ(row_0.cell_size(), 3);
  ExpectValueIsString(row_0.cell(0), "string_1");
  ExpectValueIsBool(row_0.cell(1), true);
  ExpectValueIsInt32(row_0.cell(2), 123);

  // Unprepare Query
  GOOGLESQL_EXPECT_OK(UnprepareQuery(prepared_query_id));

  // Unregister Catalog
  GOOGLESQL_EXPECT_OK(UnregisterCatalog(catalog_id));
}

TEST_F(GoogleSqlLocalServiceImplTest,
       EvaluateQueryWithRegisteredQueryWithParamEmptyTableData) {
  // Register Catalog
  RegisterCatalogRequest catalog_request;
  catalog_request.mutable_simple_catalog()->mutable_builtin_function_options();
  AddTestTable(catalog_request.mutable_simple_catalog()->add_table(),
               "TestTable");
  InsertEmptyTestTableContent(catalog_request.mutable_table_content(),
                              "TestTable");

  // We register the pools with the catalog, even though it doesn't use them
  // it should 'owned' these pools, and let us reference them in the future.
  // fake empty pool to ensure '0' isn't magic
  AddEmptyFileDescriptorSet(catalog_request.mutable_descriptor_pool_list());
  AddBuiltin(catalog_request.mutable_descriptor_pool_list());
  AddKitchenSinkDescriptorPool(catalog_request.mutable_descriptor_pool_list());

  RegisterResponse catalog_response;
  GOOGLESQL_ASSERT_OK(RegisterCatalog(catalog_request, &catalog_response));
  const int64_t catalog_id = catalog_response.registered_id();

  // Prepare Query
  PrepareQueryRequest prepare_request;
  prepare_request.set_registered_catalog_id(catalog_id);
  prepare_request.set_sql(
      R"(SELECT * FROM TestTable WHERE column_str = "@str")");
  auto* str_query_param =
      prepare_request.mutable_options()->add_query_parameters();
  str_query_param->set_name("str");
  str_query_param->mutable_type()->set_type_kind(TYPE_STRING);

  PrepareQueryResponse prepare_response;
  GOOGLESQL_EXPECT_OK(PrepareQuery(prepare_request, &prepare_response));
  const int64_t prepared_query_id =
      prepare_response.prepared().prepared_query_id();

  // Evaluate Query
  EvaluateQueryRequest evaluate_request;
  evaluate_request.set_prepared_query_id(prepared_query_id);
  auto* str_param = evaluate_request.mutable_params()->Add();
  str_param->set_name("str");
  str_param->mutable_value()->set_string_value("string_1");

  EvaluateQueryResponse evaluate_response;
  GOOGLESQL_EXPECT_OK(EvaluateQuery(evaluate_request, &evaluate_response));

  // Prepared Query State is not being returned when using a prepared query id
  ASSERT_FALSE(evaluate_response.has_prepared());

  EXPECT_EQ(evaluate_response.content().table_data().row_size(), 0);

  // Unprepare Query
  GOOGLESQL_EXPECT_OK(UnprepareQuery(prepared_query_id));

  // Unregister Catalog
  GOOGLESQL_EXPECT_OK(UnregisterCatalog(catalog_id));
}

TEST_F(GoogleSqlLocalServiceImplTest,
       EvaluateQueryWithRegisteredQueryWithParamWithTableData) {
  // Register Catalog
  RegisterCatalogRequest catalog_request;
  catalog_request.mutable_simple_catalog()->mutable_builtin_function_options();
  AddTestTable(catalog_request.mutable_simple_catalog()->add_table(),
               "TestTable");
  InsertTestTableContent(catalog_request.mutable_table_content(), "TestTable");

  // We register the pools with the catalog, even though it doesn't use them
  // it should 'owned' these pools, and let us reference them in the future.
  // fake empty pool to ensure '0' isn't magic
  AddEmptyFileDescriptorSet(catalog_request.mutable_descriptor_pool_list());
  AddBuiltin(catalog_request.mutable_descriptor_pool_list());
  AddKitchenSinkDescriptorPool(catalog_request.mutable_descriptor_pool_list());

  RegisterResponse catalog_response;
  GOOGLESQL_ASSERT_OK(RegisterCatalog(catalog_request, &catalog_response));
  const int64_t catalog_id = catalog_response.registered_id();

  // Prepare Query
  PrepareQueryRequest prepare_request;
  prepare_request.set_registered_catalog_id(catalog_id);
  prepare_request.set_sql(R"(SELECT * FROM TestTable WHERE column_str = @str)");
  auto* str_query_param =
      prepare_request.mutable_options()->add_query_parameters();
  str_query_param->set_name("str");
  str_query_param->mutable_type()->set_type_kind(TYPE_STRING);

  PrepareQueryResponse prepare_response;
  GOOGLESQL_EXPECT_OK(PrepareQuery(prepare_request, &prepare_response));
  const int64_t prepared_query_id =
      prepare_response.prepared().prepared_query_id();

  // Evaluate Query returning no data
  EvaluateQueryRequest evaluate_request;
  evaluate_request.set_prepared_query_id(prepared_query_id);
  auto* str_param = evaluate_request.mutable_params()->Add();
  str_param->set_name("str");
  str_param->mutable_value()->set_string_value("non_existent_string");

  EvaluateQueryResponse evaluate_response;
  GOOGLESQL_EXPECT_OK(EvaluateQuery(evaluate_request, &evaluate_response));

  // Prepared Query State is not being returned when using a prepared query id
  ASSERT_FALSE(evaluate_response.has_prepared());

  EXPECT_EQ(evaluate_response.content().table_data().row_size(), 0);

  // Evaluate Query returning data
  str_param->mutable_value()->set_string_value("string_1");

  EvaluateQueryResponse evaluate_response_2;
  GOOGLESQL_EXPECT_OK(EvaluateQuery(evaluate_request, &evaluate_response_2));

  // Prepared Query State is not being returned when using a prepared query id
  ASSERT_FALSE(evaluate_response_2.has_prepared());

  EXPECT_EQ(evaluate_response_2.content().table_data().row_size(), 1);
  const TableData::Row row_0 =
      evaluate_response_2.content().table_data().row(0);
  EXPECT_EQ(row_0.cell_size(), 3);
  ExpectValueIsString(row_0.cell(0), "string_1");
  ExpectValueIsBool(row_0.cell(1), true);
  ExpectValueIsInt32(row_0.cell(2), 123);

  // Unprepare Query
  GOOGLESQL_EXPECT_OK(UnprepareQuery(prepared_query_id));

  // Unregister Catalog
  GOOGLESQL_EXPECT_OK(UnregisterCatalog(catalog_id));
}

TEST_F(GoogleSqlLocalServiceImplTest,
       EvaluateQueryWithRegisteredQueryWithTableContentField) {
  // Register Catalog
  RegisterCatalogRequest catalog_request;
  catalog_request.mutable_simple_catalog()->mutable_builtin_function_options();
  AddTestTable(catalog_request.mutable_simple_catalog()->add_table(),
               "TestTable");

  RegisterResponse catalog_response;
  GOOGLESQL_ASSERT_OK(RegisterCatalog(catalog_request, &catalog_response));
  const int64_t catalog_id = catalog_response.registered_id();

  // Prepare Query
  PrepareQueryRequest prepare_request;
  prepare_request.set_registered_catalog_id(catalog_id);
  prepare_request.set_sql(
      R"(SELECT * FROM TestTable WHERE column_str = "string_1")");

  PrepareQueryResponse prepare_response;
  GOOGLESQL_EXPECT_OK(PrepareQuery(prepare_request, &prepare_response));
  const int64_t prepared_query_id =
      prepare_response.prepared().prepared_query_id();

  // Evaluate Query
  EvaluateQueryRequest evaluate_request;
  evaluate_request.set_prepared_query_id(prepared_query_id);
  InsertTestTableContent(evaluate_request.mutable_table_content(), "TestTable");

  EvaluateQueryResponse evaluate_response;
  EXPECT_EQ(EvaluateQuery(evaluate_request, &evaluate_response).code(),
            absl::StatusCode::kInvalidArgument);

  // Unprepare Query
  GOOGLESQL_EXPECT_OK(UnprepareQuery(prepared_query_id));

  // Unregister Catalog
  GOOGLESQL_EXPECT_OK(UnregisterCatalog(catalog_id));
}

TEST_F(GoogleSqlLocalServiceImplTest,
       EvaluateQueryWithInvalidRegisteredQueryId) {
  // Evaluate Query returning no data
  EvaluateQueryRequest evaluate_request;
  evaluate_request.set_prepared_query_id(12345);

  EvaluateQueryResponse evaluate_response;
  ASSERT_FALSE(EvaluateQuery(evaluate_request, &evaluate_response).ok());
}

TEST_F(GoogleSqlLocalServiceImplTest,
       EvaluateModifyWithFullCatalogNoTableData) {
  // Evaluate Modify
  EvaluateModifyRequest evaluate_request;
  evaluate_request.set_sql(R"(DELETE FROM TestTable WHERE true)");
  evaluate_request.mutable_simple_catalog()->mutable_builtin_function_options();
  AddTestTable(evaluate_request.mutable_simple_catalog()->add_table(),
               "TestTable");

  EvaluateModifyResponse evaluate_response;
  EXPECT_EQ(EvaluateModify(evaluate_request, &evaluate_response).code(),
            absl::StatusCode::kInvalidArgument);
}

TEST_F(GoogleSqlLocalServiceImplTest,
       EvaluateModifyWithDescriptorPoolListProtoWithFullCatalogNoTableData) {
  // Evaluate Modify
  EvaluateModifyRequest evaluate_request;
  evaluate_request.set_sql(R"(DELETE FROM TestTable WHERE true)");
  evaluate_request.mutable_simple_catalog()->mutable_builtin_function_options();
  AddTestTable(evaluate_request.mutable_simple_catalog()->add_table(),
               "TestTable");

  // fake empty pool to ensure '0' isn't magic
  AddEmptyFileDescriptorSet(evaluate_request.mutable_descriptor_pool_list());
  AddBuiltin(evaluate_request.mutable_descriptor_pool_list());
  AddKitchenSinkDescriptorPool(evaluate_request.mutable_descriptor_pool_list());

  EvaluateModifyResponse evaluate_response;
  EXPECT_EQ(EvaluateModify(evaluate_request, &evaluate_response).code(),
            absl::StatusCode::kInvalidArgument);
}

TEST_F(GoogleSqlLocalServiceImplTest,
       EvaluateModifyWithFullCatalogTableDataDeleteOperation) {
  // Evaluate Modify
  EvaluateModifyRequest evaluate_request;
  evaluate_request.set_sql(
      R"(DELETE FROM TestTable WHERE column_str = "string_1")");
  evaluate_request.mutable_simple_catalog()->mutable_builtin_function_options();
  AddTestTable(evaluate_request.mutable_simple_catalog()->add_table(),
               "TestTable");
  InsertTestTableContent(evaluate_request.mutable_table_content(), "TestTable");

  EvaluateModifyResponse evaluate_response;
  GOOGLESQL_EXPECT_OK(EvaluateModify(evaluate_request, &evaluate_response));

  // No registered prepared modify
  EXPECT_FALSE(evaluate_response.prepared().has_prepared_modify_id());

  EXPECT_EQ(evaluate_response.prepared()
                .descriptor_pool_id_list()
                .registered_ids_size(),
            0);

  EXPECT_EQ(evaluate_response.table_name(), "TestTable");

  EXPECT_EQ(evaluate_response.content().size(), 1);

  const EvaluateModifyResponse::Row row_0 = evaluate_response.content(0);
  EXPECT_EQ(row_0.operation(), EvaluateModifyResponse::Row::DELETE);
  // In case of DELETE, no values are being returned
  EXPECT_EQ(row_0.cell_size(), 0);
}

TEST_F(GoogleSqlLocalServiceImplTest,
       EvaluateModifyWithFullCatalogTableDataUpdateOperation) {
  // Evaluate Modify
  EvaluateModifyRequest evaluate_request;
  evaluate_request.set_sql(
      R"(UPDATE TestTable SET column_int = 333 WHERE column_str = "string_1")");
  evaluate_request.mutable_simple_catalog()->mutable_builtin_function_options();
  AddTestTable(evaluate_request.mutable_simple_catalog()->add_table(),
               "TestTable");
  InsertTestTableContent(evaluate_request.mutable_table_content(), "TestTable");

  EvaluateModifyResponse evaluate_response;
  GOOGLESQL_EXPECT_OK(EvaluateModify(evaluate_request, &evaluate_response));

  // No registered prepared modify
  EXPECT_FALSE(evaluate_response.prepared().has_prepared_modify_id());

  EXPECT_EQ(evaluate_response.prepared()
                .descriptor_pool_id_list()
                .registered_ids_size(),
            0);

  EXPECT_EQ(evaluate_response.table_name(), "TestTable");

  EXPECT_EQ(evaluate_response.content().size(), 1);

  const EvaluateModifyResponse::Row row_0 = evaluate_response.content(0);
  EXPECT_EQ(row_0.operation(), EvaluateModifyResponse::Row::UPDATE);
  // In case of UPDATE, the updated values are being returned
  EXPECT_EQ(row_0.cell_size(), 3);
  ExpectValueIsString(row_0.cell(0), "string_1");
  ExpectValueIsBool(row_0.cell(1), true);
  ExpectValueIsInt32(row_0.cell(2), 333);
}

TEST_F(GoogleSqlLocalServiceImplTest,
       EvaluateModifyWithFullCatalogTableDataInsertOperation) {
  // Evaluate Modify
  EvaluateModifyRequest evaluate_request;
  evaluate_request.set_sql(
      R"(INSERT INTO TestTable (column_str, column_bool, column_int)
      VALUES ("string_4", false, 444))");
  evaluate_request.mutable_simple_catalog()->mutable_builtin_function_options();
  AddTestTable(evaluate_request.mutable_simple_catalog()->add_table(),
               "TestTable");
  InsertTestTableContent(evaluate_request.mutable_table_content(), "TestTable");

  EvaluateModifyResponse evaluate_response;
  GOOGLESQL_EXPECT_OK(EvaluateModify(evaluate_request, &evaluate_response));

  // No registered prepared modify
  EXPECT_FALSE(evaluate_response.prepared().has_prepared_modify_id());

  EXPECT_EQ(evaluate_response.prepared()
                .descriptor_pool_id_list()
                .registered_ids_size(),
            0);

  EXPECT_EQ(evaluate_response.table_name(), "TestTable");

  EXPECT_EQ(evaluate_response.content().size(), 1);

  const EvaluateModifyResponse::Row row_0 = evaluate_response.content(0);
  EXPECT_EQ(row_0.operation(), EvaluateModifyResponse::Row::INSERT);
  // In case of INSERT, the newly added values are being returned
  EXPECT_EQ(row_0.cell_size(), 3);
  ExpectValueIsString(row_0.cell(0), "string_4");
  ExpectValueIsBool(row_0.cell(1), false);
  ExpectValueIsInt32(row_0.cell(2), 444);
}

TEST_F(GoogleSqlLocalServiceImplTest,
       EvaluateModifyWithDescriptorPoolListProtoWithFullCatalogTableData) {
  // Evaluate Modify
  EvaluateModifyRequest evaluate_request;
  evaluate_request.set_sql(
      R"(DELETE FROM TestTable WHERE column_str = "string_1")");
  evaluate_request.mutable_simple_catalog()->mutable_builtin_function_options();
  AddTestTable(evaluate_request.mutable_simple_catalog()->add_table(),
               "TestTable");
  InsertTestTableContent(evaluate_request.mutable_table_content(), "TestTable");

  // fake empty pool to ensure '0' isn't magic
  AddEmptyFileDescriptorSet(evaluate_request.mutable_descriptor_pool_list());
  AddBuiltin(evaluate_request.mutable_descriptor_pool_list());
  AddKitchenSinkDescriptorPool(evaluate_request.mutable_descriptor_pool_list());

  EvaluateModifyResponse evaluate_response;
  GOOGLESQL_EXPECT_OK(EvaluateModify(evaluate_request, &evaluate_response));

  // No registered prepared modify
  EXPECT_FALSE(evaluate_response.prepared().has_prepared_modify_id());

  EXPECT_EQ(evaluate_response.prepared()
                .descriptor_pool_id_list()
                .registered_ids_size(),
            0);

  EXPECT_EQ(evaluate_response.table_name(), "TestTable");

  EXPECT_EQ(evaluate_response.content().size(), 1);

  const EvaluateModifyResponse::Row row_0 = evaluate_response.content(0);
  EXPECT_EQ(row_0.operation(), EvaluateModifyResponse::Row::DELETE);
  // In case of DELETE, no values are being returned
  EXPECT_EQ(row_0.cell_size(), 0);
}

TEST_F(GoogleSqlLocalServiceImplTest,
       EvaluateModifyWithRegisteredCatalogNoTableData) {
  // Register Catalog
  RegisterCatalogRequest catalog_request;
  catalog_request.mutable_simple_catalog()->mutable_builtin_function_options();
  AddTestTable(catalog_request.mutable_simple_catalog()->add_table(),
               "TestTable");

  RegisterResponse catalog_response;
  GOOGLESQL_ASSERT_OK(RegisterCatalog(catalog_request, &catalog_response));
  const int64_t catalog_id = catalog_response.registered_id();

  // Evaluate Modify
  EvaluateModifyRequest evaluate_request;
  evaluate_request.set_sql(R"(DELETE FROM TestTable WHERE true)");
  evaluate_request.set_registered_catalog_id(catalog_id);

  EvaluateModifyResponse evaluate_response;
  EXPECT_EQ(EvaluateModify(evaluate_request, &evaluate_response).code(),
            absl::StatusCode::kInvalidArgument);

  // Unregister Catalog
  GOOGLESQL_EXPECT_OK(UnregisterCatalog(catalog_id));
}

TEST_F(
    GoogleSqlLocalServiceImplTest,
    EvaluateModifyWithDescriptorPoolListProtoWithRegisteredCatalogNoTableData) {
  // Register Catalog
  RegisterCatalogRequest catalog_request;
  catalog_request.mutable_simple_catalog()->mutable_builtin_function_options();
  AddTestTable(catalog_request.mutable_simple_catalog()->add_table(),
               "TestTable");

  RegisterResponse catalog_response;
  GOOGLESQL_ASSERT_OK(RegisterCatalog(catalog_request, &catalog_response));
  const int64_t catalog_id = catalog_response.registered_id();

  // Evaluate Modify
  EvaluateModifyRequest evaluate_request;
  evaluate_request.set_sql(R"(DELETE FROM TestTable WHERE true)");
  evaluate_request.set_registered_catalog_id(catalog_id);

  // fake empty pool to ensure '0' isn't magic
  AddEmptyFileDescriptorSet(evaluate_request.mutable_descriptor_pool_list());
  AddBuiltin(evaluate_request.mutable_descriptor_pool_list());
  AddKitchenSinkDescriptorPool(evaluate_request.mutable_descriptor_pool_list());

  EvaluateModifyResponse evaluate_response;
  EXPECT_EQ(EvaluateModify(evaluate_request, &evaluate_response).code(),
            absl::StatusCode::kInvalidArgument);

  // Unregister Catalog
  GOOGLESQL_EXPECT_OK(UnregisterCatalog(catalog_id));
}

TEST_F(GoogleSqlLocalServiceImplTest,
       EvaluateModifyWithRegisteredCatalogWithTableData) {
  // Register Catalog
  RegisterCatalogRequest catalog_request;
  catalog_request.mutable_simple_catalog()->mutable_builtin_function_options();
  AddTestTable(catalog_request.mutable_simple_catalog()->add_table(),
               "TestTable");
  InsertTestTableContent(catalog_request.mutable_table_content(), "TestTable");

  RegisterResponse catalog_response;
  GOOGLESQL_ASSERT_OK(RegisterCatalog(catalog_request, &catalog_response));
  const int64_t catalog_id = catalog_response.registered_id();

  // Evaluate Modify
  EvaluateModifyRequest evaluate_request;
  evaluate_request.set_sql(R"(DELETE FROM TestTable WHERE true)");
  evaluate_request.set_registered_catalog_id(catalog_id);

  EvaluateModifyResponse evaluate_response;
  GOOGLESQL_EXPECT_OK(EvaluateModify(evaluate_request, &evaluate_response));

  // No registered prepared modify
  EXPECT_FALSE(evaluate_response.prepared().has_prepared_modify_id());

  EXPECT_EQ(evaluate_response.prepared()
                .descriptor_pool_id_list()
                .registered_ids_size(),
            0);

  EXPECT_EQ(evaluate_response.table_name(), "TestTable");

  EXPECT_EQ(evaluate_response.content().size(), 2);

  const EvaluateModifyResponse::Row row_0 = evaluate_response.content(0);
  EXPECT_EQ(row_0.operation(), EvaluateModifyResponse::Row::DELETE);
  // In case of DELETE, no values are being returned
  EXPECT_EQ(row_0.cell_size(), 0);

  const EvaluateModifyResponse::Row row_1 = evaluate_response.content(1);
  EXPECT_EQ(row_1.operation(), EvaluateModifyResponse::Row::DELETE);
  // In case of DELETE, no values are being returned
  EXPECT_EQ(row_1.cell_size(), 0);

  // Unregister Catalog
  GOOGLESQL_EXPECT_OK(UnregisterCatalog(catalog_id));
}

TEST_F(GoogleSqlLocalServiceImplTest,
       EvaluateModifyWithRegisteredCatalogWithParamWithTableData) {
  // Register Catalog
  RegisterCatalogRequest catalog_request;
  catalog_request.mutable_simple_catalog()->mutable_builtin_function_options();
  AddTestTable(catalog_request.mutable_simple_catalog()->add_table(),
               "TestTable");
  InsertTestTableContent(catalog_request.mutable_table_content(), "TestTable");

  RegisterResponse catalog_response;
  GOOGLESQL_ASSERT_OK(RegisterCatalog(catalog_request, &catalog_response));
  const int64_t catalog_id = catalog_response.registered_id();

  // Evaluate Modify
  EvaluateModifyRequest evaluate_request;
  evaluate_request.set_sql(R"(DELETE FROM TestTable WHERE column_int = @int)");
  evaluate_request.set_registered_catalog_id(catalog_id);

  auto* str_query_param =
      evaluate_request.mutable_options()->add_query_parameters();
  str_query_param->set_name("int");
  str_query_param->mutable_type()->set_type_kind(TYPE_INT32);

  auto* str_param = evaluate_request.mutable_params()->Add();
  str_param->set_name("int");
  str_param->mutable_value()->set_int32_value(123);

  EvaluateModifyResponse evaluate_response;
  GOOGLESQL_EXPECT_OK(EvaluateModify(evaluate_request, &evaluate_response));

  // No registered prepared modify
  EXPECT_FALSE(evaluate_response.prepared().has_prepared_modify_id());

  EXPECT_EQ(evaluate_response.prepared()
                .descriptor_pool_id_list()
                .registered_ids_size(),
            0);

  EXPECT_EQ(evaluate_response.table_name(), "TestTable");

  EXPECT_EQ(evaluate_response.content().size(), 1);

  const EvaluateModifyResponse::Row row_0 = evaluate_response.content(0);
  EXPECT_EQ(row_0.operation(), EvaluateModifyResponse::Row::DELETE);
  // In case of DELETE, no values are being returned
  EXPECT_EQ(row_0.cell_size(), 0);

  // Unregister Catalog
  GOOGLESQL_EXPECT_OK(UnregisterCatalog(catalog_id));
}

TEST_F(GoogleSqlLocalServiceImplTest,
       EvaluateModifyWithRegisteredCatalogWithTableContentField) {
  // Register Catalog
  RegisterCatalogRequest catalog_request;
  catalog_request.mutable_simple_catalog()->mutable_builtin_function_options();
  AddTestTable(catalog_request.mutable_simple_catalog()->add_table(),
               "TestTable");

  RegisterResponse catalog_response;
  GOOGLESQL_ASSERT_OK(RegisterCatalog(catalog_request, &catalog_response));
  const int64_t catalog_id = catalog_response.registered_id();

  // Evaluate Modify
  EvaluateModifyRequest evaluate_request;
  evaluate_request.set_sql(R"(DELETE FROM TestTable WHERE true)");
  evaluate_request.set_registered_catalog_id(catalog_id);
  InsertTestTableContent(evaluate_request.mutable_table_content(), "TestTable");

  EvaluateModifyResponse evaluate_response;
  EXPECT_EQ(EvaluateModify(evaluate_request, &evaluate_response).code(),
            absl::StatusCode::kInvalidArgument);

  // Unregister Catalog
  GOOGLESQL_EXPECT_OK(UnregisterCatalog(catalog_id));
}

TEST_F(GoogleSqlLocalServiceImplTest,
       EvaluateModifyWithRegisteredQueryNoTableData) {
  // Register Catalog
  RegisterCatalogRequest catalog_request;
  catalog_request.mutable_simple_catalog()->mutable_builtin_function_options();
  AddTestTable(catalog_request.mutable_simple_catalog()->add_table(),
               "TestTable");

  // We register the pools with the catalog, even though it doesn't use them
  // it should 'owned' these pools, and let us reference them in the future.
  // fake empty pool to ensure '0' isn't magic
  AddEmptyFileDescriptorSet(catalog_request.mutable_descriptor_pool_list());
  AddBuiltin(catalog_request.mutable_descriptor_pool_list());
  AddKitchenSinkDescriptorPool(catalog_request.mutable_descriptor_pool_list());

  RegisterResponse catalog_response;
  GOOGLESQL_ASSERT_OK(RegisterCatalog(catalog_request, &catalog_response));
  const int64_t catalog_id = catalog_response.registered_id();

  // Prepare Modify
  PrepareModifyRequest prepare_request;
  prepare_request.set_sql(R"(DELETE FROM TestTable WHERE true)");
  prepare_request.set_registered_catalog_id(catalog_id);

  PrepareModifyResponse prepare_response;
  GOOGLESQL_EXPECT_OK(PrepareModify(prepare_request, &prepare_response));
  const int64_t prepared_modify_id =
      prepare_response.prepared().prepared_modify_id();

  // Evaluate Modify
  EvaluateModifyRequest evaluate_request;
  evaluate_request.set_prepared_modify_id(prepared_modify_id);

  EvaluateModifyResponse evaluate_response;
  EXPECT_EQ(EvaluateModify(evaluate_request, &evaluate_response).code(),
            absl::StatusCode::kInvalidArgument);

  // Unprepare Query
  GOOGLESQL_EXPECT_OK(UnprepareModify(prepared_modify_id));

  // Unregister Catalog
  GOOGLESQL_EXPECT_OK(UnregisterCatalog(catalog_id));
}

TEST_F(GoogleSqlLocalServiceImplTest,
       EvaluateModifyWithRegisteredQueryWithTableData) {
  // Register Catalog
  RegisterCatalogRequest catalog_request;
  catalog_request.mutable_simple_catalog()->mutable_builtin_function_options();
  AddTestTable(catalog_request.mutable_simple_catalog()->add_table(),
               "TestTable");
  InsertTestTableContent(catalog_request.mutable_table_content(), "TestTable");

  // We register the pools with the catalog, even though it doesn't use them
  // it should 'owned' these pools, and let us reference them in the future.
  // fake empty pool to ensure '0' isn't magic
  AddEmptyFileDescriptorSet(catalog_request.mutable_descriptor_pool_list());
  AddBuiltin(catalog_request.mutable_descriptor_pool_list());
  AddKitchenSinkDescriptorPool(catalog_request.mutable_descriptor_pool_list());

  RegisterResponse catalog_response;
  GOOGLESQL_ASSERT_OK(RegisterCatalog(catalog_request, &catalog_response));
  const int64_t catalog_id = catalog_response.registered_id();

  // Prepare Modify
  PrepareModifyRequest prepare_request;
  prepare_request.set_sql(R"(DELETE FROM TestTable WHERE true)");
  prepare_request.set_registered_catalog_id(catalog_id);

  PrepareModifyResponse prepare_response;
  GOOGLESQL_EXPECT_OK(PrepareModify(prepare_request, &prepare_response));
  const int64_t prepared_modify_id =
      prepare_response.prepared().prepared_modify_id();

  // Evaluate Modify
  EvaluateModifyRequest evaluate_request;
  evaluate_request.set_prepared_modify_id(prepared_modify_id);

  EvaluateModifyResponse evaluate_response;
  GOOGLESQL_EXPECT_OK(EvaluateModify(evaluate_request, &evaluate_response));

  // No registered prepared modify
  EXPECT_FALSE(evaluate_response.prepared().has_prepared_modify_id());

  EXPECT_EQ(evaluate_response.prepared()
                .descriptor_pool_id_list()
                .registered_ids_size(),
            0);

  EXPECT_EQ(evaluate_response.table_name(), "TestTable");

  EXPECT_EQ(evaluate_response.content().size(), 2);

  const EvaluateModifyResponse::Row row_0 = evaluate_response.content(0);
  EXPECT_EQ(row_0.operation(), EvaluateModifyResponse::Row::DELETE);
  // In case of DELETE, no values are being returned
  EXPECT_EQ(row_0.cell_size(), 0);

  const EvaluateModifyResponse::Row row_1 = evaluate_response.content(1);
  EXPECT_EQ(row_1.operation(), EvaluateModifyResponse::Row::DELETE);
  // In case of DELETE, no values are being returned
  EXPECT_EQ(row_1.cell_size(), 0);

  // Unprepare Query
  GOOGLESQL_EXPECT_OK(UnprepareModify(prepared_modify_id));

  // Unregister Catalog
  GOOGLESQL_EXPECT_OK(UnregisterCatalog(catalog_id));
}

TEST_F(GoogleSqlLocalServiceImplTest,
       EvaluateModifyWithRegisteredQueryWithParamWithTableData) {
  // Register Catalog
  RegisterCatalogRequest catalog_request;
  catalog_request.mutable_simple_catalog()->mutable_builtin_function_options();
  AddTestTable(catalog_request.mutable_simple_catalog()->add_table(),
               "TestTable");
  InsertTestTableContent(catalog_request.mutable_table_content(), "TestTable");

  // We register the pools with the catalog, even though it doesn't use them
  // it should 'owned' these pools, and let us reference them in the future.
  // fake empty pool to ensure '0' isn't magic
  AddEmptyFileDescriptorSet(catalog_request.mutable_descriptor_pool_list());
  AddBuiltin(catalog_request.mutable_descriptor_pool_list());
  AddKitchenSinkDescriptorPool(catalog_request.mutable_descriptor_pool_list());

  RegisterResponse catalog_response;
  GOOGLESQL_ASSERT_OK(RegisterCatalog(catalog_request, &catalog_response));
  const int64_t catalog_id = catalog_response.registered_id();

  // Prepare Modify
  PrepareModifyRequest prepare_request;
  prepare_request.set_sql(R"(DELETE FROM TestTable WHERE column_int = @int)");
  prepare_request.set_registered_catalog_id(catalog_id);

  auto* str_query_param =
      prepare_request.mutable_options()->add_query_parameters();
  str_query_param->set_name("int");
  str_query_param->mutable_type()->set_type_kind(TYPE_INT32);

  PrepareModifyResponse prepare_response;
  GOOGLESQL_EXPECT_OK(PrepareModify(prepare_request, &prepare_response));
  const int64_t prepared_modify_id =
      prepare_response.prepared().prepared_modify_id();

  // Evaluate Modify
  EvaluateModifyRequest evaluate_request;
  evaluate_request.set_prepared_modify_id(prepared_modify_id);

  auto* str_param = evaluate_request.mutable_params()->Add();
  str_param->set_name("int");
  str_param->mutable_value()->set_int32_value(123);

  EvaluateModifyResponse evaluate_response;
  GOOGLESQL_EXPECT_OK(EvaluateModify(evaluate_request, &evaluate_response));

  // No registered prepared modify
  EXPECT_FALSE(evaluate_response.prepared().has_prepared_modify_id());

  EXPECT_EQ(evaluate_response.prepared()
                .descriptor_pool_id_list()
                .registered_ids_size(),
            0);

  EXPECT_EQ(evaluate_response.table_name(), "TestTable");

  EXPECT_EQ(evaluate_response.content().size(), 1);

  const EvaluateModifyResponse::Row row_0 = evaluate_response.content(0);
  EXPECT_EQ(row_0.operation(), EvaluateModifyResponse::Row::DELETE);
  // In case of DELETE, no values are being returned
  EXPECT_EQ(row_0.cell_size(), 0);

  // Unprepare Query
  GOOGLESQL_EXPECT_OK(UnprepareModify(prepared_modify_id));

  // Unregister Catalog
  GOOGLESQL_EXPECT_OK(UnregisterCatalog(catalog_id));
}

TEST_F(GoogleSqlLocalServiceImplTest,
       EvaluateModifyWithRegisteredQueryWithTableContentField) {
  // Register Catalog
  RegisterCatalogRequest catalog_request;
  catalog_request.mutable_simple_catalog()->mutable_builtin_function_options();
  AddTestTable(catalog_request.mutable_simple_catalog()->add_table(),
               "TestTable");

  RegisterResponse catalog_response;
  GOOGLESQL_ASSERT_OK(RegisterCatalog(catalog_request, &catalog_response));
  const int64_t catalog_id = catalog_response.registered_id();

  // Prepare Modify
  PrepareModifyRequest prepare_request;
  prepare_request.set_sql(R"(DELETE FROM TestTable WHERE true)");
  prepare_request.set_registered_catalog_id(catalog_id);

  PrepareModifyResponse prepare_response;
  GOOGLESQL_EXPECT_OK(PrepareModify(prepare_request, &prepare_response));
  const int64_t prepared_modify_id =
      prepare_response.prepared().prepared_modify_id();

  // Evaluate Modify
  EvaluateModifyRequest evaluate_request;
  evaluate_request.set_prepared_modify_id(prepared_modify_id);
  InsertTestTableContent(evaluate_request.mutable_table_content(), "TestTable");

  EvaluateModifyResponse evaluate_response;
  EXPECT_EQ(EvaluateModify(evaluate_request, &evaluate_response).code(),
            absl::StatusCode::kInvalidArgument);

  // Unprepare Query
  GOOGLESQL_EXPECT_OK(UnprepareModify(prepared_modify_id));

  // Unregister Catalog
  GOOGLESQL_EXPECT_OK(UnregisterCatalog(catalog_id));
}

TEST_F(GoogleSqlLocalServiceImplTest,
       EvaluateModifyWithInvalidRegisteredModifyId) {
  // Evaluate Modify returning no data
  EvaluateModifyRequest evaluate_request;
  evaluate_request.set_prepared_modify_id(12345);

  EvaluateModifyResponse evaluate_response;
  ASSERT_FALSE(EvaluateModify(evaluate_request, &evaluate_response).ok());
}

}  // namespace local_service
}  // namespace googlesql
