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

#include "googlesql/public/analyzer.h"

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/common/errors.h"
#include "googlesql/common/status_payload_utils.h"
#include "googlesql/base/testing/status_matchers.h"  
#include "googlesql/common/testing/testing_proto_util.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parser.h"
#include "googlesql/proto/options.pb.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output.h"
#include "googlesql/public/analyzer_output_properties.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/error_helpers.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/literal_remover.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/parse_resume_location.h"
#include "googlesql/public/rewriter_interface.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/sql_formatter.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/array_type.h"
#include "googlesql/public/types/enum_type.h"
#include "googlesql/public/types/proto_type.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "googlesql/resolved_ast/sql_builder.h"
#include "googlesql/testdata/sample_catalog.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/node_hash_set.h"
#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status.h"
#include "googlesql/base/status_macros.h"

ABSL_DECLARE_FLAG(bool, googlesql_redact_error_messages_for_tests);
ABSL_DECLARE_FLAG(googlesql::ErrorMessageStability,
                  googlesql_default_error_message_stability);

namespace googlesql {

using testing::_;
using testing::AllOf;
using testing::ElementsAre;
using testing::Eq;
using testing::ExplainMatchResult;
using testing::HasSubstr;
using testing::IsEmpty;
using testing::IsNull;
using testing::MatchesRegex;
using testing::Not;
using absl_testing::StatusIs;

class AnalyzerOptionsTest : public ::testing::Test {
 public:
  AnalyzerOptionsTest() {
    options_.mutable_language()->SetSupportsAllStatementKinds();
    sample_catalog_ = std::make_unique<SampleCatalog>(options_.language());
  }
  AnalyzerOptionsTest(const AnalyzerOptionsTest&) = delete;
  AnalyzerOptionsTest& operator=(const AnalyzerOptionsTest&) = delete;
  ~AnalyzerOptionsTest() override = default;

  SimpleCatalog* catalog() {
    return sample_catalog_->catalog();
  }

  AnalyzerOptions options_;
  TypeFactory type_factory_;

  void ValidateQueryParam(absl::string_view name, const Type* type) {
    const std::string& key = absl::AsciiStrToLower(name);
    const QueryParametersMap& query_parameters = options_.query_parameters();
    EXPECT_TRUE(googlesql_base::ContainsKey(query_parameters, key));
    EXPECT_EQ(query_parameters.find(key)->second, type);
  }

  static size_t SizeOfAnalyzerOptionsData() {
    return sizeof(AnalyzerOptions::Data);
  }

 private:
  std::unique_ptr<SampleCatalog> sample_catalog_;
};

TEST_F(AnalyzerOptionsTest, AddSystemVariable) {
  // Simple cases
  EXPECT_EQ(options_.system_variables().size(), 0);
  GOOGLESQL_EXPECT_OK(options_.AddSystemVariable({"bytes"}, type_factory_.get_bytes()));
  GOOGLESQL_EXPECT_OK(options_.AddSystemVariable({"bool"}, type_factory_.get_bool()));
  GOOGLESQL_EXPECT_OK(
      options_.AddSystemVariable({"foo", "bar"}, type_factory_.get_string()));
  GOOGLESQL_EXPECT_OK(options_.AddSystemVariable({"foo.bar", "baz"},
                                       type_factory_.get_string()));

  // Null type
  EXPECT_THAT(
      options_.AddSystemVariable({"zzz"}, nullptr),
      StatusIs(
          _, HasSubstr("Type associated with system variable cannot be NULL")));

  // Unsupported type
  AnalyzerOptions external_mode(options_);
  external_mode.mutable_language()->set_product_mode(
      ProductMode::PRODUCT_EXTERNAL);
  EXPECT_THAT(
      external_mode.AddSystemVariable({"zzz"}, type_factory_.get_int32()),
      StatusIs(_,
               HasSubstr("System variable zzz has unsupported type: INT32")));

  // Duplicate variable
  EXPECT_THAT(
      options_.AddSystemVariable({"foo", "bar"}, type_factory_.get_int64()),
      StatusIs(_, HasSubstr("Duplicate system variable foo.bar")));

  // Duplicate variable (case insensitive)
  EXPECT_THAT(
      options_.AddSystemVariable({"FOO", "BaR"}, type_factory_.get_int64()),
      StatusIs(_, HasSubstr("Duplicate system variable FOO.BaR")));

  // Empty name path
  EXPECT_THAT(
      options_.AddSystemVariable({}, type_factory_.get_int64()),
      StatusIs(_, HasSubstr("System variable cannot have empty name path")));

  // name path with empty element
  EXPECT_THAT(
      options_.AddSystemVariable({""}, type_factory_.get_int64()),
      StatusIs(
          _,
          HasSubstr("System variable cannot have empty string as path part")));
}

TEST_F(AnalyzerOptionsTest, AddQueryParameter) {
  EXPECT_EQ(options_.query_parameters().size(), 0);
  GOOGLESQL_EXPECT_OK(
      options_.AddQueryParameter("bytes", type_factory_.get_bytes()));
  GOOGLESQL_EXPECT_OK(options_.AddQueryParameter("bool", type_factory_.get_bool()));
  GOOGLESQL_EXPECT_OK(options_.AddQueryParameter("int32", type_factory_.get_int32()));
  GOOGLESQL_EXPECT_OK(options_.AddQueryParameter("string", type_factory_.get_string()));
  GOOGLESQL_EXPECT_OK(
      options_.AddQueryParameter("MiXeDcAsE", type_factory_.get_string()));

  const QueryParametersMap& query_parameters = options_.query_parameters();
  EXPECT_EQ(query_parameters.size(), 5);
  ValidateQueryParam("bool", type_factory_.get_bool());
  ValidateQueryParam("bytes", type_factory_.get_bytes());
  ValidateQueryParam("int32", type_factory_.get_int32());
  ValidateQueryParam("string", type_factory_.get_string());
  // Verifies case insensitivity of parameter names.
  ValidateQueryParam("mixedcase", type_factory_.get_string());
  ValidateQueryParam("MIXEDCASE", type_factory_.get_string());
  ValidateQueryParam("mixedCASE", type_factory_.get_string());

  // Invalid cases.
  EXPECT_THAT(
      options_.AddQueryParameter("random", nullptr),
      StatusIs(
          _, HasSubstr("Type associated with query parameter cannot be NULL")));
  EXPECT_THAT(
      options_.AddPositionalQueryParameter(nullptr),
      StatusIs(
          _, HasSubstr("Type associated with query parameter cannot be NULL")));
  EXPECT_THAT(options_.AddQueryParameter("", type_factory_.get_bool()),
              StatusIs(_, HasSubstr("Query parameter cannot have empty name")));
  EXPECT_THAT(options_.AddQueryParameter("string", type_factory_.get_bool()),
              StatusIs(_, HasSubstr("Duplicate parameter name")));
}

TEST_F(AnalyzerOptionsTest, AddQueryParameterCivilTimeTypes) {
  EXPECT_EQ(options_.query_parameters().size(), 0);
  const StructType* time_struct;
  GOOGLESQL_CHECK_OK(type_factory_.MakeStructType(
      {{"time_value", type_factory_.get_time()}}, &time_struct));
  const ArrayType* time_array;
  GOOGLESQL_CHECK_OK(type_factory_.MakeArrayType(type_factory_.get_time(), &time_array));
  const ProtoType* proto_with_time;
  GOOGLESQL_CHECK_OK(type_factory_.MakeProtoType(
      googlesql_test::CivilTimeTypesSinkPB::descriptor(), &proto_with_time));

  options_.mutable_language()->DisableAllLanguageFeatures();
  EXPECT_FALSE(
      options_.AddQueryParameter("time_1", type_factory_.get_time()).ok());
  EXPECT_FALSE(
      options_.AddPositionalQueryParameter(type_factory_.get_time()).ok());
  EXPECT_FALSE(options_.AddQueryParameter("time_struct_1", time_struct).ok());
  EXPECT_FALSE(options_.AddQueryParameter("time_array_1", time_array).ok());
  // Adding a proto as parameter should be fine, even if there are some field in
  // it can be recognized as time or datetime.
  GOOGLESQL_EXPECT_OK(options_.AddQueryParameter("proto_with_time_1", proto_with_time));

  options_.mutable_language()->EnableLanguageFeature(FEATURE_CIVIL_TIME);
  GOOGLESQL_EXPECT_OK(options_.AddQueryParameter("time_2", type_factory_.get_time()));
  GOOGLESQL_EXPECT_OK(options_.AddQueryParameter("time_struct_2", time_struct));
  GOOGLESQL_EXPECT_OK(options_.AddQueryParameter("time_array_2", time_array));
  GOOGLESQL_EXPECT_OK(options_.AddQueryParameter("proto_with_time_2", proto_with_time));
}

TEST_F(AnalyzerOptionsTest, ParserASTOwnershipTests) {
  const std::string sql("SELECT 1;");

  std::unique_ptr<ParserOutput> parser_output;
  ParserOptions parser_options = options_.GetParserOptions();
  GOOGLESQL_ASSERT_OK(ParseStatement(sql, parser_options, &parser_output));

  // Analyze statement from ParserOutput without taking ownership of
  // <parser_output>.
  std::unique_ptr<const AnalyzerOutput> analyzer_output_unowned_parser_output;
  GOOGLESQL_EXPECT_OK(AnalyzeStatementFromParserOutputUnowned(
      &parser_output, options_, sql, catalog(), &type_factory_,
      &analyzer_output_unowned_parser_output));
  // <parser_output> retains ownership.
  ASSERT_NE(nullptr, parser_output);

  // Analyze statement from ParserOutput and take ownership of <parser_output>.
  std::unique_ptr<const AnalyzerOutput> analyzer_output_owned_parser_output;
  GOOGLESQL_EXPECT_OK(AnalyzeStatementFromParserOutputOwnedOnSuccess(
      &parser_output, options_, sql, catalog(), &type_factory_,
      &analyzer_output_owned_parser_output));

  GOOGLESQL_EXPECT_OK(analyzer_output_owned_parser_output->resolved_statement()
                ->CheckNoFieldsAccessed());
  // Both analyses result in the same resolved AST.
  EXPECT_EQ(analyzer_output_unowned_parser_output->resolved_statement()
              ->DebugString(),
            analyzer_output_owned_parser_output->resolved_statement()
              ->DebugString());

  // <parser_output> ownership has been released.
  EXPECT_EQ(nullptr, parser_output);

  // Test that for failed resolution, ownership of the <parser_output>
  // is retained.
  const std::string bad_sql("SELECT 1 + 'a';");

  // Re-use same ParserOutput.
  GOOGLESQL_ASSERT_OK(ParseStatement(bad_sql, parser_options, &parser_output));

  // Re-use same AnalyzerOutput.
  EXPECT_FALSE(AnalyzeStatementFromParserOutputOwnedOnSuccess(
      &parser_output, options_, bad_sql, catalog(), &type_factory_,
      &analyzer_output_owned_parser_output).ok());
  GOOGLESQL_EXPECT_OK(analyzer_output_owned_parser_output->resolved_statement()
                ->CheckNoFieldsAccessed());

  // <parser_output> retains ownership.
  EXPECT_NE(nullptr, parser_output);
}

TEST_F(AnalyzerOptionsTest, QueryParameterModes) {
  std::unique_ptr<const AnalyzerOutput> output;
  auto catalog = std::make_unique<googlesql::SimpleCatalog>("empty_catalog");
  {
    // Named mode with positional parameter.
    AnalyzerOptions options = options_;
    options.set_parameter_mode(PARAMETER_NAMED);
    GOOGLESQL_EXPECT_OK(options.AddPositionalQueryParameter(types::Int64Type()));

    EXPECT_THAT(AnalyzeStatement("SELECT 1;", options, catalog.get(),
                                 &type_factory_, &output),
                StatusIs(_, HasSubstr("Positional parameters cannot be "
                                      "provided in named parameter mode")));
  }

  {
    // Positional mode with named parameter.
    AnalyzerOptions options = options_;
    options.set_parameter_mode(PARAMETER_POSITIONAL);
    GOOGLESQL_EXPECT_OK(options.AddQueryParameter("test_param", types::Int64Type()));

    EXPECT_THAT(AnalyzeStatement("SELECT 1;", options, catalog.get(),
                                 &type_factory_, &output),
                StatusIs(_, HasSubstr("Named parameters cannot be provided "
                                      "in positional parameter mode")));
  }

  {
    // Adding a positional parameter after setting allow_undeclared_parameters.
    AnalyzerOptions options = options_;
    options.set_allow_undeclared_parameters(true);
    options.set_parameter_mode(PARAMETER_POSITIONAL);
    EXPECT_THAT(options.AddPositionalQueryParameter(types::Int64Type()),
                StatusIs(_, HasSubstr("Positional query parameters cannot "
                                      "be provided when undeclared "
                                      "parameters are allowed")));
  }

  {
    // Adding a positional parameter prior to setting
    // allow_undeclared_parameters.
    AnalyzerOptions options = options_;
    options.set_parameter_mode(PARAMETER_POSITIONAL);
    GOOGLESQL_EXPECT_OK(options.AddPositionalQueryParameter(types::Int64Type()));
    options.set_allow_undeclared_parameters(true);

    EXPECT_THAT(
        AnalyzeStatement("SELECT 1;", options, catalog.get(), &type_factory_,
                         &output),
        StatusIs(
            _,
            HasSubstr(
                "When undeclared parameters are allowed, no positional query "
                "parameters can be provided")));
  }
}

TEST_F(AnalyzerOptionsTest, AddExpressionColumn) {
  EXPECT_EQ(0, options_.expression_columns().size());
  GOOGLESQL_EXPECT_OK(
      options_.AddExpressionColumn("bytes", type_factory_.get_bytes()));
  GOOGLESQL_EXPECT_OK(options_.AddExpressionColumn("bool", type_factory_.get_bool()));
  GOOGLESQL_EXPECT_OK(
      options_.AddExpressionColumn("MiXeDcAsE", type_factory_.get_string()));

  const QueryParametersMap& expression_columns = options_.expression_columns();
  EXPECT_EQ(3, expression_columns.size());
  EXPECT_EQ("", options_.in_scope_expression_column_name());
  EXPECT_EQ(nullptr, options_.in_scope_expression_column_type());

  // Invalid cases.
  EXPECT_THAT(
      options_.AddExpressionColumn("random", nullptr),
      StatusIs(
          _,
          HasSubstr("Type associated with expression column cannot be NULL")));
  EXPECT_THAT(
      options_.AddExpressionColumn("", type_factory_.get_bool()),
      StatusIs(_, HasSubstr("Expression column cannot have empty name")));
  EXPECT_THAT(
      options_.AddExpressionColumn("MIXEDcase", type_factory_.get_bool()),
      StatusIs(_, HasSubstr("Duplicate expression column name")));

  EXPECT_THAT(options_.SetInScopeExpressionColumn("MIXEDcase",
                                                  type_factory_.get_bool()),
              StatusIs(_, HasSubstr("Duplicate expression column name")));
  EXPECT_EQ("", options_.in_scope_expression_column_name());
  EXPECT_EQ(nullptr, options_.in_scope_expression_column_type());

  GOOGLESQL_EXPECT_OK(options_.SetInScopeExpressionColumn("InScopeName",
                                                type_factory_.get_bool()));
  EXPECT_EQ(4, expression_columns.size());
  EXPECT_EQ("inscopename", options_.in_scope_expression_column_name());
  EXPECT_EQ("BOOL", options_.in_scope_expression_column_type()->DebugString());
}

TEST_F(AnalyzerOptionsTest, SetInScopeExpressionColumn_Unnamed) {
  EXPECT_TRUE(options_.expression_columns().empty());
  EXPECT_EQ("", options_.in_scope_expression_column_name());
  EXPECT_EQ(nullptr, options_.in_scope_expression_column_type());

  EXPECT_THAT(options_.SetInScopeExpressionColumn("col3", nullptr),
              StatusIs(_, HasSubstr("cannot be NULL")));

  GOOGLESQL_EXPECT_OK(options_.SetInScopeExpressionColumn("", types::Int32Type()));

  EXPECT_EQ(1, options_.expression_columns().size());
  EXPECT_EQ("", options_.in_scope_expression_column_name());
  EXPECT_EQ("INT32", options_.in_scope_expression_column_type()->DebugString());

  EXPECT_THAT(
      options_.SetInScopeExpressionColumn("xxx", types::BoolType()),
      StatusIs(_, HasSubstr("Cannot call SetInScopeExpressionColumn twice")));

  GOOGLESQL_EXPECT_OK(options_.AddExpressionColumn("col1", types::BoolType()));
  EXPECT_EQ(2, options_.expression_columns().size());
  EXPECT_EQ("", options_.in_scope_expression_column_name());
  EXPECT_EQ("INT32", options_.in_scope_expression_column_type()->DebugString());
}

TEST_F(AnalyzerOptionsTest, SetInScopeExpressionColumn_Named) {
  GOOGLESQL_EXPECT_OK(options_.SetInScopeExpressionColumn("value", types::Int32Type()));

  EXPECT_EQ(1, options_.expression_columns().size());
  EXPECT_EQ("value", options_.in_scope_expression_column_name());
  EXPECT_EQ("INT32", options_.in_scope_expression_column_type()->DebugString());

  // When we insert a named in-scope expression column, it also shows up in
  // expression_columns().
  EXPECT_EQ("value", options_.expression_columns().begin()->first);
  EXPECT_EQ("INT32",
            options_.expression_columns().begin()->second->DebugString());

  GOOGLESQL_EXPECT_OK(options_.AddExpressionColumn("col1", types::BoolType()));
  EXPECT_EQ(2, options_.expression_columns().size());
}

TEST_F(AnalyzerOptionsTest, SetDdlPseudoColumns) {
  EXPECT_EQ(nullptr, options_.ddl_pseudo_columns_callback());

  std::string table_name;
  std::vector<std::string> option_names;
  options_.SetDdlPseudoColumnsCallback(
      [this, &table_name, &option_names](
          const std::vector<std::string>& table_name_parts,
          const std::vector<const ResolvedOption*>& options,
          std::vector<std::pair<std::string, const Type*>>* pseudo_columns) {
        // Just record the table name and options.
        table_name = absl::StrJoin(table_name_parts, ".");
        option_names.clear();
        option_names.reserve(options.size());
        for (const ResolvedOption* option : options) {
          option_names.push_back(option->name());
          // Each option name can be referenced as a pseudo-column.
          pseudo_columns->push_back(
              std::make_pair(option->name(), type_factory_.get_int64()));
        }
        return absl::OkStatus();
      });
  EXPECT_NE(nullptr, options_.ddl_pseudo_columns_callback());

  std::unique_ptr<const AnalyzerOutput> output;
  LanguageOptions language_options;
  language_options.SetSupportsAllStatementKinds();
  language_options.EnableLanguageFeature(FEATURE_CREATE_TABLE_PARTITION_BY);
  options_.set_language(language_options);

  // Each test case contains the statement to analyze and the expected table and
  // options that the DDL pseudo-column callback will receive.
  struct TestCase {
    std::string statement;
    std::string expected_table;
    std::vector<std::string> expected_options;
  };
  const std::vector<TestCase> kTestCases = {
      // No PARTITION BY, hence the callback is not invoked.
      {"CREATE TABLE T (x INT64);", "", {}},
      // The PARTITION BY callback receives table name "T".
      {"CREATE TABLE T (x INT64) PARTITION BY x;", "T", {}},
      // Different table name.
      {"CREATE TABLE T.X.Y (x INT64) PARTITION BY x;", "T.X.Y", {}},
      // OPTIONS are foo and bar.
      {"CREATE TABLE T (x INT64) PARTITION BY x OPTIONS (foo=true, bar=1);",
       "T",
       {"foo", "bar"}},
      // The callback sets the pseudo-columns to the names of the options.
      {"CREATE TABLE T (x INT64) PARTITION BY foo, bar "
       "OPTIONS (foo=true, bar=1);",
       "T",
       {"foo", "bar"}},
  };
  for (const TestCase& test_case : kTestCases) {
    SCOPED_TRACE(test_case.statement);
    table_name.clear();
    option_names.clear();
    GOOGLESQL_ASSERT_OK(AnalyzeStatement(test_case.statement, options_, catalog(),
                               &type_factory_, &output));
    GOOGLESQL_EXPECT_OK(output->resolved_statement()->CheckNoFieldsAccessed());

    EXPECT_EQ(test_case.expected_table, table_name);
    EXPECT_THAT(option_names,
                ::testing::ElementsAreArray(test_case.expected_options));
  }

  options_.SetDdlPseudoColumns(
      {{"_partition_timestamp", type_factory_.get_timestamp()},
       {"_partition_date", type_factory_.get_date()}});
  std::vector<std::pair<std::string, const Type*>> pseudo_columns;
  GOOGLESQL_ASSERT_OK(options_.ddl_pseudo_columns_callback()({}, {}, &pseudo_columns));
  EXPECT_EQ(2, pseudo_columns.size());
  GOOGLESQL_ASSERT_OK(
      AnalyzeStatement("CREATE TABLE T (x INT64, y STRING) "
                       "PARTITION BY _partition_date, _partition_timestamp;",
                       options_, catalog(), &type_factory_, &output));
  GOOGLESQL_EXPECT_OK(output->resolved_statement()->CheckNoFieldsAccessed());
}

TEST_F(AnalyzerOptionsTest, AnalyzeStatementWithPreRewriteCallback) {
  AnalyzerOptions options;
  options.mutable_language()->EnableLanguageFeature(
      FEATURE_UNNEST_AND_FLATTEN_ARRAYS);
  SampleCatalog catalog(options.language());
  TypeFactory type_factory;
  std::unique_ptr<const AnalyzerOutput> output;

  options.SetPreRewriteCallback([](const AnalyzerOutput& output) {
    if (absl::Status status = output.resolved_node()->CheckNoFieldsAccessed();
        !status.ok()) {
      return status;
    }
    if (output.resolved_statement() != nullptr) {
      if (absl::StrContains(output.resolved_statement()->DebugString(),
                            "FlattenedArg")) {
        return absl::InternalError(
            "Pre-rewrite callback called before rewrite");
      }
      return absl::InternalError("Pre-rewrite callback called after rewrite");
    }
    return absl::OkStatus();
  });
  options.enable_rewrite(REWRITE_FLATTEN, true);

  EXPECT_THAT(
      AnalyzeStatement("SELECT FLATTEN([STRUCT([] AS X)].X)", options,
                       catalog.catalog(), &type_factory, &output),
      StatusIs(_, HasSubstr("Pre-rewrite callback called before rewrite")));
}

TEST_F(AnalyzerOptionsTest, AnalyzeExpressionWithPreRewriteCallback) {
  AnalyzerOptions options;
  options.mutable_language()->EnableLanguageFeature(FEATURE_ANONYMIZATION);
  SampleCatalog catalog(options.language());
  TypeFactory type_factory;
  std::unique_ptr<const AnalyzerOutput> output;

  options.SetPreRewriteCallback([](const AnalyzerOutput& output) {
    if (output.resolved_expr() != nullptr) {
      if (!absl::StrContains(output.resolved_expr()->DebugString(),
                             "+-AggregateScan")) {
        return absl::InternalError(
            "Pre-rewrite callback called before rewrite");
      }
      return absl::InternalError("Pre-rewrite callback called after rewrite");
    }
    return absl::OkStatus();
  });

  EXPECT_THAT(
      AnalyzeExpression("5 IN (SELECT ANON_COUNT(*) FROM KeyValue)", options,
                        catalog.catalog(), &type_factory, &output),
      StatusIs(_, HasSubstr("Pre-rewrite callback called before rewrite")));
}

class ErrorRewriter : public Rewriter {
 public:
  explicit ErrorRewriter(absl::string_view name) : name_(name) {}

  absl::StatusOr<std::unique_ptr<const ResolvedNode>> Rewrite(
      const AnalyzerOptions& options, std::unique_ptr<const ResolvedNode> input,
      Catalog& catalog, TypeFactory& type_factory,
      AnalyzerOutputProperties& output_properties) const override {
    return absl::InternalError(absl::StrCat(name_, " rewriter always fails"));
  }

  std::string Name() const override { return name_; }

 private:
  std::string name_;
};

TEST_F(AnalyzerOptionsTest, AnalyzeStatementWithTrailingRewriter) {
  AnalyzerOptions options;
  SampleCatalog catalog(options.language());
  TypeFactory type_factory;
  std::unique_ptr<const AnalyzerOutput> output;
  options.add_trailing_rewriter(std::make_shared<ErrorRewriter>("Trailing"));

  EXPECT_THAT(AnalyzeStatement("SELECT 1", options, catalog.catalog(),
                               &type_factory, &output),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Trailing rewriter always fails")));
}

TEST_F(AnalyzerOptionsTest, AnalyzeStatementWithLeadingRewriter) {
  AnalyzerOptions options;
  SampleCatalog catalog(options.language());
  TypeFactory type_factory;
  std::unique_ptr<const AnalyzerOutput> output;
  options.add_leading_rewriter(std::make_shared<ErrorRewriter>("Leading"));
  options.add_trailing_rewriter(std::make_shared<ErrorRewriter>("Trailing"));

  EXPECT_THAT(AnalyzeStatement("SELECT 1", options, catalog.catalog(),
                               &type_factory, &output),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Leading rewriter always fails")));
}

MATCHER_P(HasInvalidArgumentError, expected_message, "") {
  return ExplainMatchResult(Eq(absl::StatusCode::kInvalidArgument), arg.code(),
                            result_listener) &&
         ExplainMatchResult(Eq(expected_message), arg.message(),
                            result_listener);
}

TEST_F(AnalyzerOptionsTest, ErrorMessageFormat) {
  std::unique_ptr<const AnalyzerOutput> output;

  const std::string query = "select *\nfrom BadTable";
  const std::string expr = "1 +\n2 + BadCol +\n3";

  EXPECT_EQ(ErrorMessageMode::ERROR_MESSAGE_ONE_LINE,
            options_.error_message_mode());

  EXPECT_THAT(
      AnalyzeStatement(query, options_, catalog(), &type_factory_, &output),
      HasInvalidArgumentError(
          "Table not found: BadTable; Did you mean abTable? [at 2:6]"));
  EXPECT_THAT(
      AnalyzeExpression(expr, options_, catalog(), &type_factory_, &output),
      HasInvalidArgumentError("Unrecognized name: BadCol [at 2:5]"));

  options_.set_error_message_mode(ErrorMessageMode::ERROR_MESSAGE_WITH_PAYLOAD);
  EXPECT_THAT(
      AnalyzeStatement(query, options_, catalog(), &type_factory_, &output),
      HasInvalidArgumentError("Table not found: BadTable; Did you mean "
                              "abTable?"));

  EXPECT_THAT(
      AnalyzeExpression(expr, options_, catalog(), &type_factory_, &output),
      HasInvalidArgumentError("Unrecognized name: BadCol"));

  options_.set_error_message_mode(
      ErrorMessageMode::ERROR_MESSAGE_MULTI_LINE_WITH_CARET);
  EXPECT_THAT(
      AnalyzeStatement(query, options_, catalog(), &type_factory_, &output),
      HasInvalidArgumentError(
          "Table not found: BadTable; Did you mean abTable? [at 2:6]\n"
          "from BadTable\n"
          "     ^"));
  EXPECT_THAT(
      AnalyzeExpression(expr, options_, catalog(), &type_factory_, &output),
      HasInvalidArgumentError("Unrecognized name: BadCol [at 2:5]\n"
                              "2 + BadCol +\n"
                              "    ^"));
}

TEST_F(AnalyzerOptionsTest, ErrorMessageStability_ResolutionError) {
  std::unique_ptr<const AnalyzerOutput> output;

  const std::string query = "select *\nfrom BadTable";
  const std::string expr = "1 +\n2 + BadCol +\n3";

  EXPECT_EQ(ErrorMessageStability::ERROR_MESSAGE_STABILITY_PRODUCTION,
            options_.error_message_stability());
  EXPECT_EQ(ErrorMessageStability::ERROR_MESSAGE_STABILITY_PRODUCTION,
            options_.error_message_options().stability);

  EXPECT_THAT(
      AnalyzeStatement(query, options_, catalog(), &type_factory_, &output),
      HasInvalidArgumentError(
          "Table not found: BadTable; Did you mean abTable? [at 2:6]"));
  EXPECT_THAT(
      AnalyzeExpression(expr, options_, catalog(), &type_factory_, &output),
      HasInvalidArgumentError("Unrecognized name: BadCol [at 2:5]"));

  options_.set_error_message_stability(ERROR_MESSAGE_STABILITY_TEST_REDACTED);
  EXPECT_THAT(
      AnalyzeStatement(query, options_, catalog(), &type_factory_, &output),
      HasInvalidArgumentError("SQL ERROR"));

  EXPECT_THAT(
      AnalyzeExpression(expr, options_, catalog(), &type_factory_, &output),
      HasInvalidArgumentError("SQL ERROR"));
}

TEST_F(AnalyzerOptionsTest, ErrorMessageStability_SyntaxError) {
  std::unique_ptr<const AnalyzerOutput> output;

  const std::string query = "select 1 1 1";
  const std::string expr = "1 + + + ";

  EXPECT_EQ(ErrorMessageStability::ERROR_MESSAGE_STABILITY_PRODUCTION,
            options_.error_message_stability());
  EXPECT_EQ(ErrorMessageStability::ERROR_MESSAGE_STABILITY_PRODUCTION,
            options_.error_message_options().stability);

  EXPECT_THAT(
      AnalyzeStatement(query, options_, catalog(), &type_factory_, &output),
      HasInvalidArgumentError(
          R"(Syntax error: Expected end of input but got integer literal "1" [at 1:10])"));
  EXPECT_THAT(
      AnalyzeExpression(expr, options_, catalog(), &type_factory_, &output),
      HasInvalidArgumentError(
          "Syntax error: Unexpected end of expression [at 1:8]"));

  options_.set_error_message_stability(ERROR_MESSAGE_STABILITY_TEST_REDACTED);
  EXPECT_THAT(
      AnalyzeStatement(query, options_, catalog(), &type_factory_, &output),
      HasInvalidArgumentError("SQL ERROR"));

  EXPECT_THAT(
      AnalyzeExpression(expr, options_, catalog(), &type_factory_, &output),
      HasInvalidArgumentError("SQL ERROR"));
}

TEST_F(AnalyzerOptionsTest, ErrorMessageStability_RecationFlag) {
  std::unique_ptr<const AnalyzerOutput> output;

  constexpr absl::string_view query = "select 1 1 1";
  constexpr absl::string_view expr = "1 + + + ";

  EXPECT_EQ(ErrorMessageStability::ERROR_MESSAGE_STABILITY_PRODUCTION,
            options_.error_message_stability());
  EXPECT_EQ(ErrorMessageStability::ERROR_MESSAGE_STABILITY_PRODUCTION,
            options_.error_message_options().stability);

  absl::SetFlag(&FLAGS_googlesql_redact_error_messages_for_tests, true);
  options_ = AnalyzerOptions();
  EXPECT_EQ(ErrorMessageStability::ERROR_MESSAGE_STABILITY_TEST_REDACTED,
            options_.error_message_stability());
  EXPECT_EQ(ErrorMessageStability::ERROR_MESSAGE_STABILITY_TEST_REDACTED,
            options_.error_message_options().stability);

  EXPECT_THAT(
      AnalyzeStatement(query, options_, catalog(), &type_factory_, &output),
      HasInvalidArgumentError("SQL ERROR"));

  EXPECT_THAT(
      AnalyzeExpression(expr, options_, catalog(), &type_factory_, &output),
      HasInvalidArgumentError("SQL ERROR"));

  absl::SetFlag(&FLAGS_googlesql_redact_error_messages_for_tests, false);
}

class ErrorStabilityFlagTest : public AnalyzerOptionsTest {
 protected:
  void SetStabilityFlag(ErrorMessageStability stability) {
    absl::SetFlag(&FLAGS_googlesql_default_error_message_stability, stability);
    this->options_ = AnalyzerOptions();
    this->options_.set_error_message_mode(
        ErrorMessageMode::ERROR_MESSAGE_WITH_PAYLOAD);
  };
};

MATCHER(HasPayload, "") { return internal::HasPayload(arg); }

TEST_F(ErrorStabilityFlagTest, ErrorMessageStability_Flag) {
  std::unique_ptr<const AnalyzerOutput> output;

  constexpr absl::string_view query = "select 1 1 1";
  constexpr absl::string_view expr = "1 + + + ";

  EXPECT_EQ(ERROR_MESSAGE_STABILITY_PRODUCTION,
            options_.error_message_stability());
  EXPECT_EQ(ERROR_MESSAGE_STABILITY_PRODUCTION,
            options_.error_message_options().stability);

  // UNSPECIFIED should have the same behavior as not being set.
  SetStabilityFlag(ERROR_MESSAGE_STABILITY_UNSPECIFIED);
  EXPECT_EQ(ERROR_MESSAGE_STABILITY_PRODUCTION,
            options_.error_message_stability());
  EXPECT_EQ(ERROR_MESSAGE_STABILITY_PRODUCTION,
            options_.error_message_options().stability);

  // This mode should redact both message and payloads.
  SetStabilityFlag(ERROR_MESSAGE_STABILITY_TEST_REDACTED);
  EXPECT_EQ(ERROR_MESSAGE_STABILITY_TEST_REDACTED,
            options_.error_message_stability());
  EXPECT_EQ(ERROR_MESSAGE_STABILITY_TEST_REDACTED,
            options_.error_message_options().stability);

  EXPECT_THAT(
      AnalyzeStatement(query, options_, catalog(), &type_factory_, &output),
      AllOf(
          StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("SQL ERROR")),
          Not(HasPayload())));

  EXPECT_THAT(
      AnalyzeExpression(expr, options_, catalog(), &type_factory_, &output),
      AllOf(
          StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("SQL ERROR")),
          Not(HasPayload())));

  // This mode will redact only message preserving payloads.
  SetStabilityFlag(ERROR_MESSAGE_STABILITY_TEST_REDACTED_WITH_PAYLOADS);
  // This will be ignored for now.
  absl::SetFlag(&FLAGS_googlesql_redact_error_messages_for_tests, true);
  EXPECT_EQ(ERROR_MESSAGE_STABILITY_TEST_REDACTED_WITH_PAYLOADS,
            options_.error_message_stability());
  EXPECT_EQ(ERROR_MESSAGE_STABILITY_TEST_REDACTED_WITH_PAYLOADS,
            options_.error_message_options().stability);

  EXPECT_THAT(
      AnalyzeStatement(query, options_, catalog(), &type_factory_, &output),
      AllOf(
          StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("SQL ERROR")),
          HasPayload()));

  EXPECT_THAT(
      AnalyzeExpression(expr, options_, catalog(), &type_factory_, &output),
      AllOf(
          StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("SQL ERROR")),
          HasPayload()));

  // Now that --googlesql_default_error_message_stability is set, it will have
  // effect when this flag is set to unspecified.c
  SetStabilityFlag(ERROR_MESSAGE_STABILITY_UNSPECIFIED);
  EXPECT_EQ(ERROR_MESSAGE_STABILITY_TEST_REDACTED,
            options_.error_message_stability());
  EXPECT_EQ(ERROR_MESSAGE_STABILITY_TEST_REDACTED,
            options_.error_message_options().stability);

  absl::SetFlag(&FLAGS_googlesql_redact_error_messages_for_tests, false);
}

TEST_F(AnalyzerOptionsTest, NestedCatalogTypesErrorMessageFormat) {
  std::unique_ptr<const AnalyzerOutput> output;

  SimpleCatalog leaf_catalog_1("leaf_catalog_1");
  SimpleCatalog leaf_catalog_2("leaf_catalog_2");
  catalog()->AddCatalog(&leaf_catalog_1);
  catalog()->AddCatalog(&leaf_catalog_2);

  const ProtoType* proto_type_with_catalog = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory_.MakeProtoType(
      TypeProto::descriptor(), &proto_type_with_catalog, {"catalog_name"}));
  leaf_catalog_1.AddType("googlesql.TypeProto", proto_type_with_catalog);

  const ProtoType* proto_type_without_catalog = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory_.MakeProtoType(TypeProto::descriptor(),
                                        &proto_type_without_catalog));
  leaf_catalog_2.AddType("googlesql.TypeProto", proto_type_without_catalog);

  // The catalog name is shown.
  EXPECT_THAT(
      AnalyzeExpression("CAST(1 AS leaf_catalog_1.googlesql.TypeProto)",
                        options_, catalog(), &type_factory_, &output),
      HasInvalidArgumentError("Invalid cast from INT64 to "
                              "catalog_name.googlesql.TypeProto [at 1:6]"));

  // The catalog name is not shown.
  EXPECT_THAT(AnalyzeExpression("CAST(1 AS leaf_catalog_2.googlesql.TypeProto)",
                                options_, catalog(), &type_factory_, &output),
              HasInvalidArgumentError("Invalid cast from INT64 to "
                                      "googlesql.TypeProto [at 1:6]"));
}

// Some of these were previously dchecking because of bug 20010119.
TEST_F(AnalyzerOptionsTest, EofErrorMessageTrailingNewlinesAndWhitespace) {
  std::unique_ptr<const AnalyzerOutput> output;

  options_.set_error_message_mode(
      ErrorMessageMode::ERROR_MESSAGE_MULTI_LINE_WITH_CARET);

  // Trailing newlines are ignored for unexpected end of statement errors.
  for (absl::string_view newline : {"\n", "\r\n", "\n\r", "\r"}) {
    EXPECT_THAT(
        AnalyzeStatement(newline, options_, catalog(), &type_factory_, &output),
        HasInvalidArgumentError(
            "Syntax error: Unexpected end of statement [at 1:1]\n"
            "\n"
            "^"));
  }

  // All of these instances of trailing whitespace are ignored for unexpected
  // end of statement errors. We're not testing all possible whitespace, but
  // one multibyte whitespace character is included to verify that we're using
  // the generic whitespace rule for this, and not some ASCII-only hack.
  for (absl::string_view whitespace :
       {" ", "   ", "\342\200\200" /* EN QUAD */}) {
    for (absl::string_view newline : {"\n", "\r\n", "\n\r", "\r"}) {
      for (absl::string_view more_whitespace :
           {"", " ", "   ", "\342\200\200" /* EN QUAD */}) {
        EXPECT_THAT(
            AnalyzeStatement(
                absl::StrCat("SELECT", whitespace, newline, more_whitespace),
                options_, catalog(), &type_factory_, &output),
            HasInvalidArgumentError(absl::StrCat(
                "Syntax error: Unexpected end of statement [at 1:7]\n"
                "SELECT",
                whitespace,
                "\n"
                "      ^")));
      }
    }
  }
}

// Basic functionality of literal replacement.
TEST_F(AnalyzerOptionsTest, LiteralReplacement) {
  std::unique_ptr<const AnalyzerOutput> output;
  options_.set_record_parse_locations(true);

  std::string sql = "   \tSELECT 1, 'Yes', 'a'='a'AND'b'='b'";
  GOOGLESQL_EXPECT_OK(AnalyzeStatement(sql, options_, catalog(),
                             &type_factory_, &output));

  std::string new_sql;
  LiteralReplacementMap literal_map;
  GeneratedParameterMap generated_parameters;
  GOOGLESQL_ASSERT_OK(ReplaceLiteralsByParameters(
      sql, LiteralReplacementOptions{}, options_, output->resolved_statement(),
      &literal_map, &generated_parameters, &new_sql));
  EXPECT_EQ(
      "   \tSELECT @_p0_INT64, @_p1_STRING, @_p2_STRING=@_p3_STRING "
      "AND@_p4_STRING=@_p5_STRING",
      new_sql);
  ASSERT_EQ(6, literal_map.size());
  for (const auto& pair : literal_map) {
    if (pair.second == "_p0_INT64") {
      EXPECT_EQ(pair.first->value(), Value::Int64(1));
    } else if (pair.second == "_p1_STRING") {
      EXPECT_EQ(pair.first->value(), Value::String("Yes"));
    } else {
      EXPECT_THAT(pair.second,
                  testing::AnyOf("_p0_INT64", "_p1_STRING", "_p2_STRING",
                                 "_p3_STRING", "_p4_STRING", "_p5_STRING"));
    }
  }
  ASSERT_EQ(6, generated_parameters.size());
  EXPECT_EQ(generated_parameters["_p0_INT64"], Value::Int64(1));
  EXPECT_EQ(generated_parameters["_p1_STRING"], Value::String("Yes"));
}

// Test 'option_names_to_ignore' which is a set of option names to ignore during
// literal removal.
TEST_F(AnalyzerOptionsTest, LiteralReplacementIgnoreAllowlistedOptions) {
  std::unique_ptr<const AnalyzerOutput> output;
  options_.set_record_parse_locations(true);

  std::string sql = "   \t@{ hint1=1, hint2=true, hint3='foo' }SELECT 1, 'Yes'";
  GOOGLESQL_EXPECT_OK(
      AnalyzeStatement(sql, options_, catalog(), &type_factory_, &output));

  std::string new_sql;
  LiteralReplacementMap literal_map;
  GeneratedParameterMap generated_parameters;
  // Ignore hint1 and hint3 but remove hint2
  GOOGLESQL_ASSERT_OK(ReplaceLiteralsByParameters(
      sql,
      LiteralReplacementOptions{.ignored_option_names = {"hint1", "hint3"}},
      options_, output->resolved_statement(), &literal_map,
      &generated_parameters, &new_sql));
  EXPECT_EQ(
      "   \t@{ hint1=1, hint2=@_p0_BOOL, hint3='foo' }SELECT @_p1_INT64, "
      "@_p2_STRING",
      new_sql);
  ASSERT_EQ(3, literal_map.size());
  for (const auto& pair : literal_map) {
    if (pair.second == "_p0_BOOL") {
      EXPECT_EQ(pair.first->value(), Value::Bool(true));
    } else if (pair.second == "_p1_INT64") {
      EXPECT_EQ(pair.first->value(), Value::Int64(1));
    } else if (pair.second == "_p2_STRING") {
      EXPECT_EQ(pair.first->value(), Value::String("Yes"));
    }
  }
  ASSERT_EQ(3, generated_parameters.size());
  EXPECT_EQ(generated_parameters["_p0_BOOL"], Value::Bool(true));
  EXPECT_EQ(generated_parameters["_p1_INT64"], Value::Int64(1));
  EXPECT_EQ(generated_parameters["_p2_STRING"], Value::String("Yes"));
}

// Example of using a callback to override parameter name generation for a given
// literal.
TEST_F(AnalyzerOptionsTest, LiteralReplacementWithCallback) {
  std::unique_ptr<const AnalyzerOutput> output;
  options_.set_record_parse_locations(true);
  std::string sql = "SELECT 1, 2";
  GOOGLESQL_EXPECT_OK(
      AnalyzeStatement(sql, options_, catalog(), &type_factory_, &output));

  std::vector<const ResolvedNode*> literal_nodes;
  output->resolved_statement()->GetDescendantsWithKinds({RESOLVED_LITERAL},
                                                        &literal_nodes);
  ASSERT_EQ(2, literal_nodes.size());
  const ResolvedLiteral* literal1 = literal_nodes[0]->GetAs<ResolvedLiteral>();
  ASSERT_EQ(literal1->value(), Value::Int64(1));

  absl::flat_hash_map<const ResolvedLiteral*, std::string>
      predefined_literal_parameter_name_map = {{literal1, "my_param"}};
  googlesql::LiteralReplacementOptions options;
  options.parameter_name_override_callback =
      [&predefined_literal_parameter_name_map](
          const googlesql::ResolvedLiteral* literal)
      -> std::optional<std::string> {
    if (auto it = predefined_literal_parameter_name_map.find(literal);
        it != predefined_literal_parameter_name_map.end()) {
      return it->second;  // Return overridden name if literal is in map.
    }
    return std::nullopt;  // Fall back to default naming.
  };

  {
    std::string new_sql;
    LiteralReplacementMap literal_map;
    GeneratedParameterMap generated_parameters;
    GOOGLESQL_ASSERT_OK(ReplaceLiteralsByParameters(
        sql, options, options_, output->resolved_statement(), &literal_map,
        &generated_parameters, &new_sql));
    EXPECT_EQ("SELECT @my_param, @_p0_INT64", new_sql);
    EXPECT_EQ(2, literal_map.size());
    EXPECT_EQ(literal_map[literal1], "my_param");
    EXPECT_EQ(2, generated_parameters.size());
    EXPECT_EQ(generated_parameters["my_param"], Value::Int64(1));
    EXPECT_EQ(generated_parameters["_p0_INT64"], Value::Int64(2));
  }

  {
    std::string new_sql;
    LiteralReplacementMap literal_map;
    GeneratedParameterMap generated_parameters;
    GOOGLESQL_ASSERT_OK(options_.AddQueryParameter("my_param", types::Int64Type()));
    EXPECT_THAT(
        ReplaceLiteralsByParameters(sql, options, options_,
                                    output->resolved_statement(), &literal_map,
                                    &generated_parameters, &new_sql),
        StatusIs(
            absl::StatusCode::kInvalidArgument,
            HasSubstr("Generated parameter name from the override callback is "
                      "conflict with an existing parameter:")));
  }
}

MATCHER_P(HasDeprecationWarning, expected_message, "") {
  std::optional<absl::Cord> warning =
      arg.GetPayload(kErrorMessageModeUrl);
  if (!warning.has_value()) {
    *result_listener << "Expected a payload on the deprecation warning of type "
                        "googlesql.DeprecationWarning";
    return false;
  }
  googlesql::DeprecationWarning warning_proto;
  warning_proto.ParseFromString(std::string(warning.value()));
  return ExplainMatchResult(Eq(absl::StatusCode::kInvalidArgument), arg.code(),
                            result_listener) &&
         ExplainMatchResult(Eq(expected_message), arg.message(),
                            result_listener) &&
         ExplainMatchResult(Eq(googlesql::DeprecationWarning::DEPRECATED_FUNCTION),
                            warning_proto.kind(),
                            result_listener);
}
TEST_F(AnalyzerOptionsTest, DeprecationWarnings) {
  // Create some deprecated functions as a stable way to generate deprecation
  // errors.  (Real deprecated features in the language won't stick around.)
  FunctionOptions function_options;
  function_options.set_is_deprecated(true);

  SimpleCatalog catalog("catalog");
  catalog.AddBuiltinFunctions(BuiltinFunctionOptions::AllReleasedFunctions());
  catalog.AddOwnedFunction(new Function(
      "depr1", "test_group", Function::SCALAR,
      {{ARG_TYPE_ANY_1, {ARG_TYPE_ANY_1}, 1}}, function_options));
  catalog.AddOwnedFunction(new Function(
      "depr2", "test_group", Function::SCALAR,
      {{ARG_TYPE_ANY_1, {ARG_TYPE_ANY_1}, 2}}, function_options));

  // Use the same base expression in a query or in an expression, with a
  // leading newline so the error locations come out the same.
  const std::string base_expr = "DEPR1(5) + DEPR1(6) + DEPR2(7)";
  const std::string expr = absl::StrCat("\n", base_expr);
  const std::string sql = absl::StrCat("SELECT\n", base_expr);
  const std::string multi_sql = absl::StrCat("SELECT 1; SELECT\n", base_expr);

  std::unique_ptr<const AnalyzerOutput> output;

  EXPECT_FALSE(options_.language().error_on_deprecated_syntax());

  // Test with multiple input types (0=Statement, 1=NextStatement, 2=Expression)
  // and with error_on_deprecated_syntax on or off.
  for (int input_type = 0; input_type <= 2; ++input_type) {
    for (int with_errors = 0; with_errors <= 1; ++with_errors) {
      options_.mutable_language()->set_error_on_deprecated_syntax(
          with_errors == 1);

      absl::Status status;
      if (input_type == 0) {
        status = AnalyzeStatement(sql, options_, &catalog, &type_factory_,
                                  &output);
      } else if (input_type == 1) {
        // Parse two queries from one string.  The first query has no warnings.
        bool at_end;
        ParseResumeLocation location =
            ParseResumeLocation::FromStringView(multi_sql);
        GOOGLESQL_EXPECT_OK(AnalyzeNextStatement(
            &location, options_, &catalog, &type_factory_, &output, &at_end));
        ASSERT_FALSE(at_end);
        EXPECT_EQ(0, output->deprecation_warnings().size());

        status = AnalyzeNextStatement(
            &location, options_, &catalog, &type_factory_, &output, &at_end);
        EXPECT_TRUE(at_end);
      } else if (input_type == 2) {
        status = AnalyzeExpression(expr, options_, &catalog, &type_factory_,
                                   &output);
      }

      if (with_errors == 1) {
        EXPECT_THAT(status,
                    HasDeprecationWarning(
                        "Function TEST_GROUP:DEPR1 is deprecated [at 2:1]"));
      } else {
        GOOGLESQL_EXPECT_OK(status);
        ASSERT_EQ(2, output->deprecation_warnings().size());
        EXPECT_THAT(output->deprecation_warnings()[0],
                    HasDeprecationWarning(
                        "Function TEST_GROUP:DEPR1 is deprecated [at 2:1]"));
        EXPECT_THAT(
            output->deprecation_warnings()[1],
            HasDeprecationWarning("Function TEST_GROUP:DEPR2 is deprecated [at "
                                  "2:23]"));
      }
    }
  }
}

// Create a function named `if` that can shadow the built-in `if` function.
std::unique_ptr<Function> CreateIfFunction(std::string function_group) {
  FunctionOptions function_options;
  function_options.set_evaluator([](const absl::Span<const Value> args) {
    bool condition = args[0].bool_value();
    return Value::Bool(!condition);
  });
  return std::make_unique<Function>(
      "if", function_group, Function::SCALAR,
      std::vector<FunctionSignature>{
          {types::BoolType(),
           {types::BoolType(), types::BoolType(), types::BoolType()},
           1}},
      function_options);
}

// Create a function named `rewritten_function` with the given rewrite options,
// to be rewritten by the BuiltinFunctionInliner rewriter.
std::unique_ptr<Function> CreateRewrittenFunction(
    std::string sql, bool allow_table_references,
    std::vector<std::string> allowed_function_groups) {
  FunctionSignatureOptions function_signature_options =
      FunctionSignatureOptions().set_rewrite_options(
          FunctionSignatureRewriteOptions()
              .set_enabled(true)
              .set_rewriter(REWRITE_BUILTIN_FUNCTION_INLINER)
              .set_sql(sql)
              .set_allow_table_references(allow_table_references)
              .set_allowed_function_groups(allowed_function_groups));
  return std::make_unique<Function>(
      "rewritten_function", "GoogleSQL", Function::SCALAR,
      std::vector<FunctionSignature>{
          {types::BoolType(), {}, 1, function_signature_options}});
}

// When a built-in function that is required by a rewrite is hidden by a
// non-built-in function, an error is returned.
TEST_F(AnalyzerOptionsTest, BuiltInHiddenByNonBuiltIn) {
  SimpleCatalog catalog("catalog");
  GoogleSQLBuiltinFunctionOptions options;
  options.exclude_function_ids.insert(FN_IF);
  catalog.AddBuiltinFunctions(options);
  // Hides the built-in `IF` function by a user-defined `IF`.
  catalog.AddOwnedFunction(CreateIfFunction(/*function_group=*/""));

  // TYPEOF internally uses IF.
  std::string expr = R"sql(
    TYPEOF("hello")
  )sql";
  std::unique_ptr<const AnalyzerOutput> output;
  absl::Status status =
      AnalyzeExpression(expr, options_, &catalog, &type_factory_, &output);
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(),
            "Required built-in function \"`if`\" not available");
}

// By default, BuiltinFunctionInliner only allows SQL templates to reference
// GoogleSQL-builtin functions.
TEST_F(AnalyzerOptionsTest, BuiltinFunctionInlinerShadowed) {
  SimpleCatalog catalog("catalog");
  catalog.AddOwnedFunction(CreateRewrittenFunction(
      "IF(true, true, true)", /*allow_table_references=*/false,
      /*allowed_function_groups=*/{}));
  // Mimic a UDF `IF`, which will be referenced when analyzing the SQL template
  // for `rewritten_function`.
  catalog.AddOwnedFunction(CreateIfFunction(/*function_group=*/""));

  std::string expr = "rewritten_function()";
  std::unique_ptr<const AnalyzerOutput> output;
  absl::Status status =
      AnalyzeExpression(expr, options_, &catalog, &type_factory_, &output);
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(status.message(),
              HasSubstr("Required built-in function \"`IF`\" not available"));
}

// BuiltinFunctionInliner allows additional function groups, if specified in the
// FunctionSignatureRewriteOptions.
TEST_F(AnalyzerOptionsTest, BuiltinFunctionInlinerEngineBuiltin) {
  SimpleCatalog catalog("catalog");
  const std::string kFunctionGroup = "test_group";
  catalog.AddOwnedFunction(CreateRewrittenFunction(
      "IF(true, true, true)", /*allow_table_references=*/false,
      /*allowed_function_groups=*/{"x", kFunctionGroup}));
  // Mimic an engine-defined `IF`, which will be referenced when analyzing the
  // SQL template for `rewritten_function`. This function is allowed by the
  // `allowed_function_groups` option in the FunctionSignatureRewriteOptions.
  catalog.AddOwnedFunction(CreateIfFunction(kFunctionGroup));

  std::string expr = "rewritten_function()";
  std::unique_ptr<const AnalyzerOutput> output;
  GOOGLESQL_EXPECT_OK(
      AnalyzeExpression(expr, options_, &catalog, &type_factory_, &output));
}

// By default, BuiltinFunctionInliner will not allow table references.
TEST_F(AnalyzerOptionsTest, BuiltinFunctionInlinerDisallowTableReference) {
  SimpleCatalog catalog("catalog");
  catalog.AddOwnedFunction(
      CreateRewrittenFunction("(SELECT T.x FROM test_table AS T)",
                              /*allow_table_references=*/false,
                              /*allowed_function_groups=*/{}));
  auto table = std::make_unique<SimpleTable>("test_table");
  GOOGLESQL_ASSERT_OK(table->AddColumn(
      new SimpleColumn("test_table", "x", type_factory_.get_bool()),
      /*is_owned=*/true));
  catalog.AddOwnedTable(std::move(table));

  std::string expr = "rewritten_function()";
  std::unique_ptr<const AnalyzerOutput> output;
  absl::Status status =
      AnalyzeExpression(expr, options_, &catalog, &type_factory_, &output);
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(status.message(), HasSubstr("Table not found: test_table"));
}

// BuiltinFunctionInliner allows table references if indicated by the
// FunctionSignatureRewriteOptions.
TEST_F(AnalyzerOptionsTest, BuiltinFunctionInlinerAllowTableReference) {
  SimpleCatalog catalog("catalog");
  catalog.AddOwnedFunction(
      CreateRewrittenFunction("(SELECT T.x FROM test_table AS T)",
                              /*allow_table_references=*/true,
                              /*allowed_function_groups=*/{}));
  auto table = std::make_unique<SimpleTable>("test_table");
  GOOGLESQL_ASSERT_OK(table->AddColumn(
      new SimpleColumn("test_table", "x", type_factory_.get_bool()),
      /*is_owned=*/true));
  catalog.AddOwnedTable(std::move(table));

  std::string expr = "rewritten_function()";
  std::unique_ptr<const AnalyzerOutput> output;
  GOOGLESQL_EXPECT_OK(
      AnalyzeExpression(expr, options_, &catalog, &type_factory_, &output));
}

TEST_F(AnalyzerOptionsTest, ResolvedASTRewrites) {
  // Should be on by default.
  EXPECT_TRUE(options_.rewrite_enabled(REWRITE_FLATTEN));
  options_.enable_rewrite(REWRITE_FLATTEN, /*enable=*/false);
  EXPECT_FALSE(options_.rewrite_enabled(REWRITE_FLATTEN));
  options_.enable_rewrite(REWRITE_FLATTEN);
  EXPECT_TRUE(options_.rewrite_enabled(REWRITE_FLATTEN));

  absl::btree_set<ResolvedASTRewrite> rewrites;
  rewrites.insert(REWRITE_INVALID_DO_NOT_USE);
  options_.set_enabled_rewrites(rewrites);
  EXPECT_FALSE(options_.rewrite_enabled(REWRITE_FLATTEN));
  EXPECT_TRUE(options_.rewrite_enabled(REWRITE_INVALID_DO_NOT_USE));
}

// Need to implement this to catch importing errors.
class MultiFileErrorCollector
    : public google::protobuf::compiler::MultiFileErrorCollector {
 public:
  MultiFileErrorCollector() = default;
  MultiFileErrorCollector(const MultiFileErrorCollector&) = delete;
  MultiFileErrorCollector& operator=(const MultiFileErrorCollector&) = delete;
  void RecordError(absl::string_view filename, int line, int column,
                   absl::string_view message) override {
    absl::StrAppend(&error_, "Line ", line, " Column ", column, " :", message,
                    "\n");
  }
  const std::string& GetError() const { return error_; }

 private:
  std::string error_;
};

TEST_F(AnalyzerOptionsTest, Deserialize) {
  TypeFactory factory;

  const std::vector<std::string> test_files{
      // Order matters for these imports.
      "google/protobuf/descriptor.proto",
      "googlesql/public/proto/type_annotation.proto",
      "googlesql/testdata/test_schema.proto",
      "googlesql/testdata/external_extension.proto",
  };
  std::unique_ptr<google::protobuf::compiler::DiskSourceTree> source_tree =
      CreateProtoSourceTree();
  MultiFileErrorCollector error_collector;

  std::unique_ptr<google::protobuf::compiler::Importer> proto_importer =
      std::make_unique<google::protobuf::compiler::Importer>(source_tree.get(),
                                                   &error_collector);

  for (const std::string& test_file : test_files) {
    ABSL_CHECK(proto_importer->Import(test_file) != nullptr)
        << "Error importing " << test_file << ": "
        << error_collector.GetError();
  }
  std::unique_ptr<google::protobuf::DescriptorPool> external_pool =
      std::make_unique<google::protobuf::DescriptorPool>(proto_importer->pool());

  const EnumType* generated_enum_type;
  GOOGLESQL_CHECK_OK(factory.MakeEnumType(TypeKind_descriptor(), &generated_enum_type));

  const ProtoType* generated_proto_type;
  GOOGLESQL_CHECK_OK(factory.MakeProtoType(
      TypeProto::descriptor(), &generated_proto_type));

  const EnumType* external_enum_type;
  GOOGLESQL_CHECK_OK(factory.MakeEnumType(
      external_pool->FindEnumTypeByName("googlesql_test.TestEnum"),
      &external_enum_type));

  const ProtoType* external_proto_type;
  GOOGLESQL_CHECK_OK(factory.MakeProtoType(
      external_pool->FindMessageTypeByName("googlesql_test.KitchenSinkPB"),
      &external_proto_type));

  std::vector<const google::protobuf::DescriptorPool*> pools = {
    external_pool.get(), google::protobuf::DescriptorPool::generated_pool()
  };

  AnalyzerOptionsProto proto;
  proto.set_error_message_mode(ERROR_MESSAGE_WITH_PAYLOAD);
  proto.set_prune_unused_columns(true);
  proto.set_allow_undeclared_parameters(true);
  proto.set_parameter_mode(PARAMETER_POSITIONAL);
  proto.mutable_language_options()->set_product_mode(PRODUCT_INTERNAL);
  proto.set_default_timezone("Asia/Shanghai");
  proto.set_preserve_column_aliases(false);
  proto.set_parse_location_record_type(PARSE_LOCATION_RECORD_FULL_NODE_SCOPE);

  auto* param = proto.add_query_parameters();
  param->set_name("q1");
  GOOGLESQL_CHECK_OK(types::Int64Type()->SerializeToSelfContainedProto(
      param->mutable_type()));

  param = proto.add_query_parameters();
  param->set_name("q2");
  GOOGLESQL_CHECK_OK(external_enum_type->SerializeToProtoAndFileDescriptors(
      param->mutable_type()));
  param->mutable_type()->mutable_enum_type()->set_file_descriptor_set_index(0);

  param = proto.add_query_parameters();
  param->set_name("q3");
  GOOGLESQL_CHECK_OK(external_proto_type->SerializeToProtoAndFileDescriptors(
      param->mutable_type()));
  param->mutable_type()->mutable_proto_type()->set_file_descriptor_set_index(0);

  param = proto.add_query_parameters();
  param->set_name("q4");
  GOOGLESQL_CHECK_OK(generated_enum_type->SerializeToProtoAndFileDescriptors(
      param->mutable_type()));
  param->mutable_type()->mutable_enum_type()->set_file_descriptor_set_index(1);

  param = proto.add_query_parameters();
  param->set_name("q5");
  GOOGLESQL_CHECK_OK(generated_proto_type->SerializeToProtoAndFileDescriptors(
      param->mutable_type()));
  param->mutable_type()->mutable_proto_type()->set_file_descriptor_set_index(1);

  GOOGLESQL_ASSERT_OK(types::Int64Type()->SerializeToSelfContainedProto(
      proto.add_positional_query_parameters()));

  GOOGLESQL_ASSERT_OK(generated_proto_type->SerializeToSelfContainedProto(
      proto.add_positional_query_parameters()));
  proto.mutable_positional_query_parameters(1)
      ->mutable_proto_type()
      ->set_file_descriptor_set_index(1);

  auto* column = proto.add_expression_columns();
  column->set_name("c1");
  GOOGLESQL_CHECK_OK(external_enum_type->SerializeToProtoAndFileDescriptors(
      column->mutable_type()));
  column->mutable_type()->mutable_enum_type()->set_file_descriptor_set_index(0);

  column = proto.add_expression_columns();
  column->set_name("c2");
  GOOGLESQL_CHECK_OK(types::Int64Type()->SerializeToSelfContainedProto(
      column->mutable_type()));

  column = proto.add_expression_columns();
  column->set_name("c3");
  GOOGLESQL_CHECK_OK(external_proto_type->SerializeToProtoAndFileDescriptors(
      column->mutable_type()));
  column->mutable_type()->mutable_proto_type()
      ->set_file_descriptor_set_index(0);

  column = proto.add_expression_columns();
  column->set_name("c4");
  GOOGLESQL_CHECK_OK(generated_enum_type->SerializeToProtoAndFileDescriptors(
      column->mutable_type()));
  column->mutable_type()->mutable_enum_type()->set_file_descriptor_set_index(1);

  column = proto.add_expression_columns();
  column->set_name("c5");
  GOOGLESQL_CHECK_OK(generated_proto_type->SerializeToProtoAndFileDescriptors(
      column->mutable_type()));
  column->mutable_type()->mutable_proto_type()
      ->set_file_descriptor_set_index(1);

  auto* in_scope_column = proto.mutable_in_scope_expression_column();
  in_scope_column->set_name("isc");
  GOOGLESQL_CHECK_OK(generated_proto_type->SerializeToProtoAndFileDescriptors(
      in_scope_column->mutable_type()));
  in_scope_column->mutable_type()->mutable_proto_type()
      ->set_file_descriptor_set_index(1);

  auto* allowed = proto.mutable_allowed_hints_and_options();
  allowed->add_disallow_unknown_hints_with_qualifier("qual");
  allowed->add_disallow_unknown_hints_with_qualifier("");
  allowed->set_disallow_unknown_options(true);
  auto* hint1 = allowed->add_hint();
  hint1->set_qualifier("test_qual");
  hint1->set_name("hint1");
  GOOGLESQL_CHECK_OK(generated_proto_type->SerializeToProtoAndFileDescriptors(
      hint1->mutable_type()));
  hint1->mutable_type()->mutable_proto_type()->set_file_descriptor_set_index(1);
  hint1->set_allow_unqualified(true);
  auto* hint2 = allowed->add_hint();
  hint2->set_qualifier("test_qual");
  hint2->set_name("untyped_hint");
  hint2->set_allow_unqualified(true);
  auto* option1 = allowed->add_option();
  option1->set_name("untyped_option");
  auto* option2 = allowed->add_option();
  option2->set_name("option2");
  GOOGLESQL_CHECK_OK(generated_enum_type->SerializeToProtoAndFileDescriptors(
      option2->mutable_type()));
  option2->mutable_type()->mutable_enum_type()
      ->set_file_descriptor_set_index(1);

  AnalyzerOptions options;
  GOOGLESQL_CHECK_OK(AnalyzerOptions::Deserialize(proto, pools, &factory, &options));

  ASSERT_TRUE(options.prune_unused_columns());
  ASSERT_EQ(PARSE_LOCATION_RECORD_FULL_NODE_SCOPE,
            options.parse_location_record_type());
  ASSERT_TRUE(options.allow_undeclared_parameters());
  ASSERT_EQ(ERROR_MESSAGE_WITH_PAYLOAD, options.error_message_mode());
  ASSERT_EQ(PRODUCT_INTERNAL, options.language().product_mode());
  ASSERT_EQ(PARAMETER_POSITIONAL, options.parameter_mode());
  ASSERT_FALSE(options.preserve_column_aliases());

  ASSERT_EQ(5, options.query_parameters().size());
  ASSERT_TRUE(types::Int64Type()->Equals(options.query_parameters().at("q1")));
  ASSERT_TRUE(external_enum_type->Equals(options.query_parameters().at("q2")));
  ASSERT_TRUE(external_proto_type->Equals(options.query_parameters().at("q3")));
  ASSERT_TRUE(generated_enum_type->Equals(options.query_parameters().at("q4")));
  ASSERT_TRUE(generated_proto_type->Equals(
      options.query_parameters().at("q5")));

  ASSERT_EQ(2, options.positional_query_parameters().size());
  ASSERT_TRUE(
      types::Int64Type()->Equals(options.positional_query_parameters()[0]));
  ASSERT_TRUE(
      generated_proto_type->Equals(options.positional_query_parameters()[1]));

  ASSERT_EQ(6, options.expression_columns().size());
  ASSERT_TRUE(external_enum_type->Equals(
      options.expression_columns().at("c1")));
  ASSERT_TRUE(types::Int64Type()->Equals(
      options.expression_columns().at("c2")));
  ASSERT_TRUE(external_proto_type->Equals(
      options.expression_columns().at("c3")));
  ASSERT_TRUE(generated_enum_type->Equals(
      options.expression_columns().at("c4")));
  ASSERT_TRUE(generated_proto_type->Equals(
      options.expression_columns().at("c5")));
  ASSERT_TRUE(generated_proto_type->Equals(
      options.expression_columns().at("isc")));

  ASSERT_TRUE(options.has_in_scope_expression_column());
  ASSERT_EQ("isc", options.in_scope_expression_column_name());
  ASSERT_TRUE(generated_proto_type->Equals(
      options.in_scope_expression_column_type()));
  ASSERT_EQ("Asia/Shanghai", options.default_time_zone().name());

  ASSERT_TRUE(options.allowed_hints_and_options()
              .disallow_unknown_hints_with_qualifiers.find("qual") !=
                  options.allowed_hints_and_options()
                  .disallow_unknown_hints_with_qualifiers.end());
  ASSERT_TRUE(options.allowed_hints_and_options()
              .disallow_unknown_hints_with_qualifiers.find("") !=
                  options.allowed_hints_and_options()
                  .disallow_unknown_hints_with_qualifiers.end());
  ASSERT_TRUE(options.allowed_hints_and_options().disallow_unknown_options);
  ASSERT_TRUE(generated_proto_type->Equals(
      options.allowed_hints_and_options()
          .hints_lower.find(std::make_pair("test_qual", "hint1"))
          ->second));
  ASSERT_TRUE(generated_proto_type->Equals(
      options.allowed_hints_and_options()
          .hints_lower.find(std::make_pair("", "hint1"))
          ->second));
  ASSERT_THAT(options.allowed_hints_and_options()
                  .hints_lower.find(std::make_pair("test_qual", "untyped_hint"))
                  ->second,
              IsNull());
  ASSERT_TRUE(generated_enum_type->Equals(options.allowed_hints_and_options()
                                              .options_lower.find("option2")
                                              ->second.type));
  ASSERT_THAT(options.allowed_hints_and_options()
                  .options_lower.find("untyped_option")
                  ->second.type,
              IsNull());
}

TEST_F(AnalyzerOptionsTest, AllowedHintsAndOptionsSerializeAndDeserialize) {
  TypeFactory factory;

  const std::vector<std::string> test_files{
      // Order matters for these imports.
      "google/protobuf/descriptor.proto",
      "googlesql/public/proto/type_annotation.proto",
      "googlesql/testdata/test_schema.proto",
      "googlesql/testdata/external_extension.proto",
  };
  std::unique_ptr<google::protobuf::compiler::DiskSourceTree> source_tree =
      CreateProtoSourceTree();
  MultiFileErrorCollector error_collector;

  std::unique_ptr<google::protobuf::compiler::Importer> proto_importer =
      std::make_unique<google::protobuf::compiler::Importer>(source_tree.get(),
                                                   &error_collector);

  for (const std::string& test_file : test_files) {
    ABSL_CHECK(proto_importer->Import(test_file) != nullptr)
        << "Error importing " << test_file << ": "
        << error_collector.GetError();
  }
  std::unique_ptr<google::protobuf::DescriptorPool> external_pool =
      std::make_unique<google::protobuf::DescriptorPool>(proto_importer->pool());

  const EnumType* generated_enum_type;
  GOOGLESQL_CHECK_OK(factory.MakeEnumType(TypeKind_descriptor(), &generated_enum_type));

  const ProtoType* generated_proto_type;
  GOOGLESQL_CHECK_OK(factory.MakeProtoType(
      TypeProto::descriptor(), &generated_proto_type));

  std::vector<const google::protobuf::DescriptorPool*> pools = {
      google::protobuf::DescriptorPool::generated_pool(), external_pool.get(),
  };

  std::map<const google::protobuf::DescriptorPool*,
  std::unique_ptr<Type::FileDescriptorEntry> > file_descriptor_set_map;
  AllowedHintsAndOptionsProto proto;
  AllowedHintsAndOptions allowed("Qual");
  allowed.AddHint("test_qual", "hint1", generated_proto_type, true);
  allowed.AddHint("test_qual", "hint2", generated_proto_type, false);
  allowed.AddHint("", "hint2", generated_proto_type, true);
  allowed.AddHint("test_qual", "hint3", nullptr, false);
  allowed.AddHint("", "hint3", nullptr, true);
  allowed.AddHint("test_qual", "hint4", generated_enum_type, false);
  allowed.AddHint("", "hint4", nullptr, true);
  allowed.AddHint("test_qual", "hint5", nullptr, false);
  allowed.AddHint("", "hint5", generated_enum_type, true);
  allowed.AddHint("test_qual", "hint6", generated_proto_type, false);
  allowed.AddHint("", "untyped_qual", nullptr, true);
  allowed.AddOption("option1", generated_enum_type);
  allowed.AddOption("untyped_option", nullptr);
  allowed.disallow_duplicate_option_names = true;
  GOOGLESQL_CHECK_OK(allowed.Serialize(&file_descriptor_set_map, &proto));
  EXPECT_EQ(2, proto.disallow_unknown_hints_with_qualifier_size());
  EXPECT_TRUE(proto.disallow_unknown_options());
  EXPECT_TRUE(proto.disallow_duplicate_option_names());
  EXPECT_EQ(9, proto.hint_size());
  EXPECT_EQ(2, proto.option_size());
  AllowedHintsAndOptions generated_allowed;
  GOOGLESQL_CHECK_OK(AllowedHintsAndOptions::Deserialize(
      proto, pools, &factory, &generated_allowed));
  EXPECT_EQ(2, generated_allowed.disallow_unknown_hints_with_qualifiers.size());
  EXPECT_TRUE(generated_allowed.disallow_unknown_options);
  EXPECT_TRUE(generated_allowed.disallow_duplicate_option_names);
  EXPECT_EQ(12, generated_allowed.hints_lower.size());
  EXPECT_EQ(2, generated_allowed.options_lower.size());
}

TEST(AllowedHintsAndOptionsTest, ClassAndProtoSize) {
  EXPECT_EQ(8, sizeof(AllowedHintsAndOptions) - sizeof(std::set<std::string>) -
                   sizeof(absl::flat_hash_map<std::string, std::string>) -
                   3 * sizeof(absl::flat_hash_map<std::string, const Type*>))
      << "The size of AllowedHintsAndOptions class has changed, please also "
      << "update the proto and serialization code if you added/removed fields "
      << "in it.";
  EXPECT_EQ(7, AllowedHintsAndOptionsProto::descriptor()->field_count())
      << "The number of fields in AllowedHintsAndOptionsProto has changed, "
      << "please also update the serialization code accordingly.";
}

// Defines a triple for testing the SupportedStatement feature in GoogleSQL.
struct SupportedStatementTestInput {
  SupportedStatementTestInput(absl::string_view stmt, bool success,
                              const std::set<ResolvedNodeKind>& statement_kinds)
      : statement(stmt),
        expect_success(success),
        supported_statement_kinds(statement_kinds) {}
  // The statement to attempt to parse and analyze.
  const std::string statement;
  // The expected outcome of the parse and analyze.
  const bool expect_success;
  // The ResolvedNodeKinds to allow. The empty set is all accepting.
  const std::set<ResolvedNodeKind> supported_statement_kinds;
};

// Test GoogleSQL's ability to reject statements not in the allowlist
// contained in the AnalyzerOptions.
TEST(AnalyzerSupportedStatementsTest, SupportedStatementTest) {
  TypeFactory type_factory;
  std::unique_ptr<const AnalyzerOutput> output;
  const std::string query = "SELECT 1;";
  const std::string explain_query = "EXPLAIN SELECT 1;";
  // The statement is resolved first before looking at the hint.  If we have
  // an invalid hint on an unsupported statement, we'll get the unsupported
  // statement error.
  const std::string hinted_query = "@{key=value} SELECT 1;";
  const std::string bad_hinted_query = "@{key=@xxx} SELECT 1;";

  std::vector<SupportedStatementTestInput> test_input = {
      SupportedStatementTestInput(query, true, {}),
      SupportedStatementTestInput(query, true, {RESOLVED_QUERY_STMT}),
      SupportedStatementTestInput(query, false, {RESOLVED_EXPLAIN_STMT}),
      SupportedStatementTestInput(query, true,
                                  {RESOLVED_QUERY_STMT, RESOLVED_EXPLAIN_STMT}),
      SupportedStatementTestInput(explain_query, true, {}),
      SupportedStatementTestInput(explain_query, false, {RESOLVED_QUERY_STMT}),
      SupportedStatementTestInput(explain_query, false,
                                  {RESOLVED_EXPLAIN_STMT}),
      SupportedStatementTestInput(explain_query, true,
                                  {RESOLVED_QUERY_STMT,
                                   RESOLVED_EXPLAIN_STMT}),
      SupportedStatementTestInput(hinted_query, true, {RESOLVED_QUERY_STMT}),
      SupportedStatementTestInput(hinted_query, false, {RESOLVED_EXPLAIN_STMT}),
      SupportedStatementTestInput(bad_hinted_query, false,
                                  {RESOLVED_EXPLAIN_STMT}),
  };

  for (const SupportedStatementTestInput& input : test_input) {
    AnalyzerOptions options;
    options.mutable_language()->SetSupportedStatementKinds(
        input.supported_statement_kinds);
    SampleCatalog catalog(options.language());
    const absl::Status status = AnalyzeStatement(
        input.statement, options, catalog.catalog(), &type_factory, &output);
    if (input.expect_success) {
      GOOGLESQL_EXPECT_OK(status);
    } else {
      EXPECT_THAT(status, StatusIs(_, HasSubstr("Statement not supported:")));
    }
  }
}

// Verifies that an unsupported ALTER TABLE statement reports an informative
// error message rather than a syntax error (b/31389932).
TEST(AnalyzerSupportedStatementsTest, AlterTableNotSupported) {
  AnalyzerOptions options;
  options.mutable_language()->SetSupportedStatementKinds({RESOLVED_QUERY_STMT});
  SampleCatalog catalog(options.language());
  TypeFactory type_factory;
  std::unique_ptr<const AnalyzerOutput> output;
  const std::string sql = "ALTER TABLE t ADD COLUMN c INT64";
  auto status = AnalyzeStatement(
      sql, options, catalog.catalog(), &type_factory, &output);
  EXPECT_THAT(status, StatusIs(_, HasSubstr("Statement not supported")));
}

// Defines a test for the LanguageFeatures option in GoogleSQL.  The
// <expected_error_string> is only compared if it is non-empty and
// <expect_success> is false.
struct SupportedFeatureTestInput {
  SupportedFeatureTestInput(absl::string_view stmt,
                            const LanguageOptions::LanguageFeatureSet& features,
                            bool expect_success_in,
                            absl::string_view error_string = "")
      : statement(stmt),
        supported_features(features),
        expect_success(expect_success_in),
        expected_error_string(error_string) {}

  // The statement to parse and analyze.
  const std::string statement;
  // The LanguageFeatures to allowlist.
  const LanguageOptions::LanguageFeatureSet supported_features;
  // The expected outcome of the parse and analyze.
  const bool expect_success;
  // The expected error string, only relevant if <expect_success> is false.
  const std::string expected_error_string;

  std::string FeaturesToString() const {
    return LanguageOptions::ToString(supported_features);
  }
};

// Test GoogleSQL's ability to reject features not in the allowlist
// contained in the AnalyzerOptions.
TEST(AnalyzerSupportedFeaturesTest, SupportedFeaturesTest) {
  // Simple getter/setter tests.
  AnalyzerOptions options;
  EXPECT_FALSE(
      options.language().LanguageFeatureEnabled(FEATURE_ANALYTIC_FUNCTIONS));
  options.mutable_language()->EnableLanguageFeature(FEATURE_ANALYTIC_FUNCTIONS);
  EXPECT_TRUE(
      options.language().LanguageFeatureEnabled(FEATURE_ANALYTIC_FUNCTIONS));
  options.mutable_language()->DisableAllLanguageFeatures();
  EXPECT_FALSE(
      options.language().LanguageFeatureEnabled(FEATURE_ANALYTIC_FUNCTIONS));

  // Setting an already set feature is not an error.
  options.mutable_language()->EnableLanguageFeature(FEATURE_ANALYTIC_FUNCTIONS);
  EXPECT_TRUE(
      options.language().LanguageFeatureEnabled(FEATURE_ANALYTIC_FUNCTIONS));
  options.mutable_language()->EnableMaximumLanguageFeatures();
  EXPECT_TRUE(
      options.language().LanguageFeatureEnabled(FEATURE_ANALYTIC_FUNCTIONS));

  // Disable all and enable all.
  options.mutable_language()->DisableAllLanguageFeatures();
  EXPECT_FALSE(
      options.language().LanguageFeatureEnabled(FEATURE_ANALYTIC_FUNCTIONS));
  options.mutable_language()->EnableMaximumLanguageFeatures();
  EXPECT_TRUE(
      options.language().LanguageFeatureEnabled(FEATURE_ANALYTIC_FUNCTIONS));

  // Query tests.
  TypeFactory type_factory;
  std::unique_ptr<const AnalyzerOutput> output;
  const std::string aggregate_query = "SELECT sum(key) from KeyValue;";
  const std::string analytic_query = "SELECT sum(key) over () from KeyValue;";

  std::vector<SupportedFeatureTestInput> test_input = {
      SupportedFeatureTestInput(aggregate_query, {}, true /* expect_success */),
      SupportedFeatureTestInput(aggregate_query, {FEATURE_ANALYTIC_FUNCTIONS},
                                true),
      // This tests the allowlist feature capability prior to
      // analysis/resolution being implemented for analytic functions.
      SupportedFeatureTestInput(analytic_query, {}, false,
                                "Analytic functions not supported"),
      SupportedFeatureTestInput(analytic_query, {FEATURE_ANALYTIC_FUNCTIONS},
                                true /* expect_success */),
  };

  for (const SupportedFeatureTestInput& input : test_input) {
    AnalyzerOptions options;
    options.mutable_language()->SetEnabledLanguageFeatures(
        input.supported_features);
    ABSL_LOG(INFO) << "Supported features: " << input.FeaturesToString();
    SampleCatalog catalog(options.language());
    const absl::Status status = AnalyzeStatement(
        input.statement, options, catalog.catalog(), &type_factory, &output);
    if (input.expect_success) {
      GOOGLESQL_EXPECT_OK(status)
          << "Query: " << input.statement
          << "\nSupported features: " << input.FeaturesToString();
      EXPECT_GT(output->max_column_id(), 0);
    } else {
      EXPECT_THAT(status, StatusIs(_, HasSubstr(input.expected_error_string)))
          << "Query: " << input.statement
          << "\nSupported features: " << input.FeaturesToString();
    }
  }
}

TEST(AnalyzerSupportedFeaturesTest, EnableFeaturesForVersion) {
  // Tests below show that SetLanguageVersion sets the features to exactly
  // the features for that version, and clears any others (like
  // FEATURE_ANALYTIC_FUNCTIONS).
  AnalyzerOptions options;
  options.mutable_language()->EnableLanguageFeature(FEATURE_ANALYTIC_FUNCTIONS);
  options.mutable_language()->EnableLanguageFeature(FEATURE_ORDER_BY_COLLATE);

  options.mutable_language()->SetLanguageVersion(VERSION_1_0);
  EXPECT_FALSE(
      options.language().LanguageFeatureEnabled(FEATURE_ANALYTIC_FUNCTIONS));
  EXPECT_FALSE(
      options.language().LanguageFeatureEnabled(FEATURE_ORDER_BY_COLLATE));

  options.mutable_language()->EnableLanguageFeature(FEATURE_ANALYTIC_FUNCTIONS);
  options.mutable_language()->SetLanguageVersion(VERSION_1_1);
  EXPECT_FALSE(
      options.language().LanguageFeatureEnabled(FEATURE_ANALYTIC_FUNCTIONS));
  EXPECT_TRUE(
      options.language().LanguageFeatureEnabled(FEATURE_ORDER_BY_COLLATE));

  options.mutable_language()->EnableLanguageFeature(FEATURE_ANALYTIC_FUNCTIONS);
  EXPECT_TRUE(
      options.language().LanguageFeatureEnabled(FEATURE_ANALYTIC_FUNCTIONS));
  EXPECT_TRUE(
      options.language().LanguageFeatureEnabled(FEATURE_ORDER_BY_COLLATE));

  options.mutable_language()->DisableAllLanguageFeatures();
  EXPECT_FALSE(
      options.language().LanguageFeatureEnabled(FEATURE_ANALYTIC_FUNCTIONS));
  EXPECT_FALSE(
      options.language().LanguageFeatureEnabled(FEATURE_ORDER_BY_COLLATE));

  // VERSION_CURRENT clears the features and adds back in the maximal set
  // of versioned features, but not the cross-version features.
  options.mutable_language()->EnableLanguageFeature(FEATURE_ANALYTIC_FUNCTIONS);
  options.mutable_language()->SetLanguageVersion(VERSION_CURRENT);
  EXPECT_FALSE(
      options.language().LanguageFeatureEnabled(FEATURE_ANALYTIC_FUNCTIONS));
  EXPECT_TRUE(
      options.language().LanguageFeatureEnabled(FEATURE_ORDER_BY_COLLATE));
}

// Test resolving a proto extension where the extension comes from
// a different DescriptorPool.  We have to load up a second DescriptorPool
// manually to test this.
TEST(AnalyzerTest, ExternalExtension) {
  const std::vector<std::string> test_files{
      // Order matters for these imports.
      "google/protobuf/descriptor.proto",
      "googlesql/public/proto/type_annotation.proto",
      "googlesql/testdata/test_schema.proto",
      "googlesql/testdata/external_extension.proto",
  };

  std::unique_ptr<google::protobuf::compiler::DiskSourceTree> source_tree =
      CreateProtoSourceTree();

  MultiFileErrorCollector error_collector;
  std::unique_ptr<google::protobuf::compiler::Importer> proto_importer =
      std::make_unique<google::protobuf::compiler::Importer>(source_tree.get(),
                                                   &error_collector);

  for (const std::string& test_file : test_files) {
    ABSL_CHECK(proto_importer->Import(test_file) != nullptr)
        << "Error importing " << test_file << ": "
        << error_collector.GetError();
  }
  std::unique_ptr<google::protobuf::DescriptorPool> external_pool =
      std::make_unique<google::protobuf::DescriptorPool>(proto_importer->pool());
  const std::string external_extension_name =
      "googlesql_test.ExternalExtension";
  const google::protobuf::Descriptor* external_extension =
      external_pool->FindMessageTypeByName(external_extension_name);
  ASSERT_NE(external_extension, nullptr);

  const std::string query1 =
      "SELECT "
      "t.KitchenSink.test_extra_pb.(googlesql_test.ExternalExtension.field) "
      "as ext_field FROM TestTable t";

  TypeFactory type_factory;
  AnalyzerOptions options;
  SampleCatalog sample_catalog(options.language());
  SimpleCatalog* catalog = sample_catalog.catalog();

  std::unique_ptr<const AnalyzerOutput> output1;

  // Check that the extension type name does not exist in the default catalog.
  const Type* found_type;
  GOOGLESQL_EXPECT_OK(catalog->FindType({"googlesql_test.KitchenSinkPB"}, &found_type));
  EXPECT_FALSE(catalog->FindType({external_extension_name}, &found_type).ok());

  // Analyze the queries and we'll get an error that the extension is not found.
  EXPECT_THAT(
      AnalyzeStatement(query1, options, catalog, &type_factory, &output1),
      StatusIs(
          _,
          HasSubstr(
              "Extension googlesql_test.ExternalExtension.field not found")));

  // Now add the extension type into the Catalog.
  // (Modifying the SampleCatalog is a hack to save code.)
  const ProtoType* extension_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeProtoType(external_extension, &extension_type));
  static_cast<SimpleCatalog*>(catalog)->AddType(
      external_extension_name, extension_type);

  GOOGLESQL_EXPECT_OK(catalog->FindType({external_extension_name}, &found_type));

  // Re-analyze the queries and now it will succeed.
  // This checks that we can create and validate a ResolvedGetProtoField that
  // points at a FieldDescriptor from a different DescriptorPool.
  GOOGLESQL_ASSERT_OK(AnalyzeStatement(query1, options, catalog, &type_factory,
                             &output1));
}

static void ExpectStatementHasAnonymization(
    absl::string_view sql, bool expect_anonymization = true,
    bool expect_analyzer_success = true) {
  AnalyzerOptions options;
  options.mutable_language()->SetSupportedStatementKinds(
      {RESOLVED_QUERY_STMT, RESOLVED_CREATE_TABLE_AS_SELECT_STMT});
  options.mutable_language()->EnableLanguageFeature(FEATURE_ANONYMIZATION);
  options.enable_rewrite(REWRITE_ANONYMIZATION, /*enable=*/false);
  SampleCatalog catalog(options.language());
  TypeFactory type_factory;
  std::unique_ptr<const AnalyzerOutput> output;
  absl::Status status = AnalyzeStatement(
      sql, options, catalog.catalog(), &type_factory, &output);
  if (expect_analyzer_success) {
    GOOGLESQL_EXPECT_OK(status);
    EXPECT_EQ(output->analyzer_output_properties().has_anonymization,
              expect_anonymization);
  } else {
    // Note that if the analyzer failed, then there is no AnalyzerOutput.
    EXPECT_FALSE(status.ok());
  }
}

TEST(AnalyzerTest, TestStatementHasAnonymization) {
  ExpectStatementHasAnonymization(
      "SELECT * FROM KeyValue", false);
  ExpectStatementHasAnonymization(
      "SELECT WITH ANONYMIZATION key FROM KeyValue GROUP BY key");
  ExpectStatementHasAnonymization("SELECT ANON_COUNT(*) FROM KeyValue");
  ExpectStatementHasAnonymization("SELECT ANON_SUM(key) FROM KeyValue");
  ExpectStatementHasAnonymization("SELECT ANON_AVG(key) FROM KeyValue");
  ExpectStatementHasAnonymization("SELECT ANON_COUNT(value) FROM KeyValue");
  ExpectStatementHasAnonymization(
      "SELECT * FROM (SELECT ANON_COUNT(*) FROM KeyValue)");
  ExpectStatementHasAnonymization(
      "SELECT (SELECT ANON_COUNT(*) FROM KeyValue) FROM KeyValue");
  ExpectStatementHasAnonymization(
      absl::StrCat("SELECT * FROM KeyValue ",
                   "WHERE key IN (SELECT ANON_COUNT(*) FROM KeyValue)"));
  // DDL has anonymization
  ExpectStatementHasAnonymization(
      "CREATE TABLE foo AS SELECT ANON_COUNT(*) AS a FROM KeyValue");

  // Resolution fails, has_anonymization is false even though we found it
  // in the first part of the query (before finding the error).
  ExpectStatementHasAnonymization(
      absl::StrCat("SELECT ANON_COUNT(*) FROM KeyValue ",
                   "UNION ALL ",
                   "SELECT 'string_literal'"), false, false);
}

static void ExpectExpressionHasAnonymization(absl::string_view sql,
                                             bool expect_anonymization = true) {
  AnalyzerOptions options;
  options.mutable_language()->EnableLanguageFeature(FEATURE_ANONYMIZATION);
  SampleCatalog catalog(options.language());
  TypeFactory type_factory;
  std::unique_ptr<const AnalyzerOutput> output;
  absl::Status status = AnalyzeExpression(
      sql, options, catalog.catalog(), &type_factory, &output);
  GOOGLESQL_ASSERT_OK(status);
  EXPECT_EQ(output->analyzer_output_properties().has_anonymization,
            expect_anonymization);
}

TEST(AnalyzerTest, TestExpressionHasAnonymization) {
  // Note that AnalyzeExpression works only for scalar expressions, not
  // aggregate expressions, unless analyzer_options has
  // set_allow_aggregate_standalone_expression set to true.
  ExpectExpressionHasAnonymization("concat('a', 'b')", false);
  ExpectExpressionHasAnonymization("5 IN (SELECT ANON_COUNT(*) FROM KeyValue)");
}

TEST(AnalyzerTest, StandaloneAggregateExpression) {
  AnalyzerOptions options;
  SimpleCatalog catalog("catalog");
  catalog.AddBuiltinFunctions(
      googlesql::BuiltinFunctionOptions::AllReleasedFunctions());
  TypeFactory type_factory;
  std::unique_ptr<const AnalyzerOutput> output;

  GOOGLESQL_EXPECT_OK(options.AddExpressionColumn("number", type_factory.get_int32()));

  // Verify an error is given for calling an aggregate function in a standalone
  // expression if `allow_aggregate_standalone_expression` is not set to true.
  EXPECT_THAT(AnalyzeExpression("SUM(number)", options, &catalog, &type_factory,
                                &output),
              HasInvalidArgumentError("Aggregate function SUM not allowed in "
                                      "standalone expression [at 1:1]"));

  // Verify the call is ok when `allow_aggregate_standalone_expression` is true.
  options.set_allow_aggregate_standalone_expression(true);
  GOOGLESQL_ASSERT_OK(AnalyzeExpression("SUM(number)", options, &catalog, &type_factory,
                              &output));
  EXPECT_EQ(RESOLVED_AGGREGATE_FUNCTION_CALL,
            output->resolved_expr()->node_kind());

  // Verify an error is correctly given for an invalid nested aggregation.
  EXPECT_THAT(AnalyzeExpression("SUM(SUM(number))", options, &catalog,
                                &type_factory, &output),
              HasInvalidArgumentError(
                  "Aggregations of aggregations are not allowed [at 1:1]"));

  // Verify that analytic functions remain disallowed in standalone expressions.
  options.mutable_language()->EnableLanguageFeature(FEATURE_ANALYTIC_FUNCTIONS);
  EXPECT_THAT(
      AnalyzeExpression("ROW_NUMBER() OVER ()", options, &catalog,
                        &type_factory, &output),
      HasInvalidArgumentError(
          "Analytic function not allowed in standalone expression [at 1:1]"));

  // Test a complex aggregate expression that calls two aggregate functions.
  GOOGLESQL_ASSERT_OK(AnalyzeExpression("SUM(number) + COUNT(*)", options, &catalog,
                              &type_factory, &output));
  EXPECT_EQ(RESOLVED_FUNCTION_CALL, output->resolved_expr()->node_kind());

  // Test that nested aggregations are valid within a standalone aggregation.
  GOOGLESQL_ASSERT_OK(AnalyzeExpression("COUNT((SELECT COUNT(*) FROM UNNEST([number])))",
                              options, &catalog, &type_factory, &output));
  EXPECT_EQ(RESOLVED_AGGREGATE_FUNCTION_CALL,
            output->resolved_expr()->node_kind());
}

TEST(AnalyzerTest, AstRewriting) {
  AnalyzerOptions options;
  options.mutable_language()->EnableLanguageFeature(
      FEATURE_UNNEST_AND_FLATTEN_ARRAYS);
  SampleCatalog catalog(options.language());
  TypeFactory type_factory;
  std::unique_ptr<const AnalyzerOutput> output;

  for (const bool should_rewrite : { true, false }) {
    options.enable_rewrite(REWRITE_FLATTEN, should_rewrite);
    GOOGLESQL_ASSERT_OK(AnalyzeStatement("SELECT FLATTEN([STRUCT([] AS X)].X)", options,
                               catalog.catalog(), &type_factory, &output));
    if (should_rewrite) {
      // If rewrites are enabled the Flatten should be rewritten away.
      EXPECT_THAT(output->resolved_statement()->DebugString(),
                  Not(HasSubstr("FlattenedArg")));
    } else {
      // Otherwise it should remain.
      EXPECT_THAT(output->resolved_statement()->DebugString(),
                  HasSubstr("FlattenedArg"));
    }
  }
}

TEST(AnalyzerTest, AstRewritingLegacyAccessModeRespected) {
  AnalyzerOptions options;
  options.set_fields_accessed_mode(
      AnalyzerOptions::FieldsAccessedMode::LEGACY_FIELDS_ACCESSED_MODE);
  options.mutable_language()->EnableLanguageFeature(
      FEATURE_UNNEST_AND_FLATTEN_ARRAYS);
  SampleCatalog catalog(options.language());
  TypeFactory type_factory;
  std::unique_ptr<const AnalyzerOutput> output;

  for (const bool should_rewrite : {true, false}) {
    options.enable_rewrite(REWRITE_FLATTEN, should_rewrite);
    GOOGLESQL_ASSERT_OK(AnalyzeStatement("SELECT FLATTEN([STRUCT([] AS X)].X)", options,
                               catalog.catalog(), &type_factory, &output));
    if (should_rewrite) {
      GOOGLESQL_EXPECT_OK(output->resolved_statement()->CheckFieldsAccessed());
    } else {
      GOOGLESQL_EXPECT_OK(output->resolved_statement()->CheckNoFieldsAccessed());
    }
  }
}

TEST(AnalyzerTest, AstRewritingClearAccessModeRespected) {
  // Note: Analyzer file based tests also test this mode.
  AnalyzerOptions options;
  options.set_fields_accessed_mode(
      AnalyzerOptions::FieldsAccessedMode::CLEAR_FIELDS);
  options.mutable_language()->EnableLanguageFeature(
      FEATURE_UNNEST_AND_FLATTEN_ARRAYS);
  SampleCatalog catalog(options.language());
  TypeFactory type_factory;
  std::unique_ptr<const AnalyzerOutput> output;

  for (const bool should_rewrite : {true, false}) {
    options.enable_rewrite(REWRITE_FLATTEN, should_rewrite);
    GOOGLESQL_ASSERT_OK(AnalyzeStatement("SELECT FLATTEN([STRUCT([] AS X)].X)", options,
                               catalog.catalog(), &type_factory, &output));
    GOOGLESQL_EXPECT_OK(output->resolved_statement()->CheckNoFieldsAccessed());
  }
}

// Test that the language_options setters and getters on AnalyzerOptions work
// correctly and don't overwrite the options outside LanguageOptions.
TEST(AnalyzerTest, LanguageOptions) {
  AnalyzerOptions analyzer_options;
  EXPECT_FALSE(
      analyzer_options.language().SupportsStatementKind(RESOLVED_DELETE_STMT));
  EXPECT_EQ(ErrorMessageMode::ERROR_MESSAGE_ONE_LINE,
            analyzer_options.error_message_mode());
  analyzer_options.set_error_message_mode(
      ErrorMessageMode::ERROR_MESSAGE_WITH_PAYLOAD);
  EXPECT_EQ(ErrorMessageMode::ERROR_MESSAGE_WITH_PAYLOAD,
            analyzer_options.error_message_mode());

  LanguageOptions language_options;
  EXPECT_FALSE(language_options.SupportsStatementKind(RESOLVED_DELETE_STMT));
  language_options.SetSupportsAllStatementKinds();
  EXPECT_TRUE(language_options.SupportsStatementKind(RESOLVED_DELETE_STMT));

  analyzer_options.set_language(language_options);
  EXPECT_TRUE(
      analyzer_options.language().SupportsStatementKind(RESOLVED_DELETE_STMT));
  EXPECT_TRUE(
      analyzer_options.language().SupportsStatementKind(RESOLVED_DELETE_STMT));
  EXPECT_EQ(ErrorMessageMode::ERROR_MESSAGE_WITH_PAYLOAD,
            analyzer_options.error_message_mode());

  EXPECT_FALSE(analyzer_options.language().LanguageFeatureEnabled(
      FEATURE_ORDER_BY_COLLATE));
  analyzer_options.mutable_language()->EnableLanguageFeature(
      FEATURE_ORDER_BY_COLLATE);
  EXPECT_TRUE(analyzer_options.language().LanguageFeatureEnabled(
      FEATURE_ORDER_BY_COLLATE));

  const AnalyzerOptions analyzer_options2(language_options);
  EXPECT_TRUE(
      analyzer_options2.language().SupportsStatementKind(RESOLVED_DELETE_STMT));
  EXPECT_EQ(ErrorMessageMode::ERROR_MESSAGE_ONE_LINE,
            analyzer_options2.error_message_mode());
}

TEST(SQLBuilderTest, Int32ParameterForLimit) {
  auto cast_limit = MakeResolvedCast(types::Int64Type(),
                                     MakeResolvedLiteral(values::Int32(2)),
                                     /*return_null_on_error=*/false);
  auto cast_offset = MakeResolvedCast(types::Int64Type(),
                                      MakeResolvedLiteral(values::Int32(1)),
                                      /*return_null_on_error=*/false);

  auto limit_offset_scan = MakeResolvedLimitOffsetScan(
      /*column_list=*/{}, MakeResolvedSingleRowScan(), std::move(cast_limit),
      std::move(cast_offset));
  SQLBuilder sql_builder;
  GOOGLESQL_ASSERT_OK(sql_builder.Process(*limit_offset_scan));
  std::string formatted_sql;
  GOOGLESQL_ASSERT_OK(FormatSql(sql_builder.sql(), &formatted_sql));
  EXPECT_EQ(
      "SELECT\n  1\nLIMIT CAST(CAST(2 AS INT32) AS INT64) OFFSET CAST(CAST(1 "
      "AS INT32) AS INT64);",
      formatted_sql);
}

// Adding specific unit test to input provided by Random Query Generator tree.
// From a SQL String (like in golden file sql_builder.test), we get a different
// tree (FilterScan is under other ResolvedScans and this scenario isn't tested.
TEST(SQLBuilderTest, WithScanWithFilterScan) {
  const std::string with_query_name = "WithTable";
  const std::string table_name = "T1";
  const std::string col_name = "C";

  TypeFactory type_factory;
  auto table = std::make_unique<SimpleTable>(table_name);

  // With entry list.
  const ResolvedColumn scan_column(
      10, googlesql::IdString::MakeGlobal(table_name),
      googlesql::IdString::MakeGlobal(col_name), type_factory.get_int32());
  auto table_scan = MakeResolvedTableScan({scan_column}, table.get(),
                                          /*for_system_time_expr=*/nullptr);
  auto filter_scan = MakeResolvedFilterScan(
      {scan_column}, std::move(table_scan),
      MakeResolvedLiteral(type_factory.get_bool(), Value::Bool(true)));
  std::vector<std::unique_ptr<const ResolvedWithEntry>> with_entry_list;
  with_entry_list.emplace_back(
      MakeResolvedWithEntry(with_query_name, std::move(filter_scan)));

  // With scan query.
  const ResolvedColumn query_column(
      20, googlesql::IdString::MakeGlobal(with_query_name),
      googlesql::IdString::MakeGlobal(col_name), type_factory.get_int32());
  auto with_ref_scan = MakeResolvedWithRefScan({query_column}, with_query_name);
  auto query = MakeResolvedProjectScan({query_column}, /*expr_list=*/{},
                                       std::move(with_ref_scan));
  auto with_scan =
      MakeResolvedWithScan({scan_column}, std::move(with_entry_list),
                           std::move(query), /*recursive=*/false);

  // Test that this scan is correctly supported in WITH clause.
  SQLBuilder sql_builder;
  GOOGLESQL_ASSERT_OK(sql_builder.Process(*with_scan));
  std::string formatted_sql;
  GOOGLESQL_ASSERT_OK(FormatSql(sql_builder.sql(), &formatted_sql));
  EXPECT_EQ(
      "WITH\n  WithTable AS (\n"
      "    SELECT\n      T1.C AS a_1\n    FROM\n      T1\n"
      "    WHERE\n      true\n  )\n"
      "SELECT\n  withrefscan_2.a_1 AS a_1\nFROM\n  WithTable AS withrefscan_2;",
      formatted_sql);
}

// Test that SqlBuilder prefer ResolvedTableScan.column_index_list over column
// and table names in ResolvedTableScan, which should have no semantic meaning.
// See the class comment on `ResolvedTableScan`.
TEST(SQLBuilderTest, TableScanPrefersColumnIndexList) {
  const std::string table_name = "T1";
  const std::string col_name = "C";
  const std::string unused_name = "UNUSED_NAME";
  const int column_id = 9;

  TypeFactory type_factory;
  auto table = std::make_unique<SimpleTable>(table_name);
  GOOGLESQL_ASSERT_OK(table->AddColumn(
      new SimpleColumn(table_name, col_name, type_factory.get_int32()),
      /*is_owned=*/true));
  const ResolvedColumn scan_column(column_id, IdString::MakeGlobal(unused_name),
                                   IdString::MakeGlobal(unused_name),
                                   type_factory.get_int32());
  auto table_scan = MakeResolvedTableScan({scan_column}, table.get(),
                                          /*for_system_time_expr=*/nullptr);
  table_scan->set_column_index_list({0});
  const ResolvedColumn query_column(
      column_id, IdString::MakeGlobal(unused_name),
      IdString::MakeGlobal(unused_name), type_factory.get_int32());
  auto query = MakeResolvedProjectScan({query_column}, /*expr_list=*/{},
                                       std::move(table_scan));

  SQLBuilder sql_builder;
  GOOGLESQL_ASSERT_OK(sql_builder.Process(*query));
  std::string formatted_sql;
  GOOGLESQL_ASSERT_OK(FormatSql(sql_builder.sql(), &formatted_sql));
  EXPECT_EQ(R"(SELECT
  T1.C AS a_1
FROM
  T1;)",
            formatted_sql);
}

// Adding specific unit test to input provided by Random Query Generator tree.
// From a SQL String (like in golden file sql_builder.test), we get a different
// tree (JoinScan is under other ResolvedScans and this scenario isn't tested.
TEST(SQLBuilderTest, WithScanWithJoinScan) {
  const std::string with_query_name = "WithTable";
  const std::string table_name1 = "T1";
  const std::string table_name2 = "T2";
  const std::string col_name = "C";

  TypeFactory type_factory;
  auto table1 = std::make_unique<SimpleTable>(table_name1);
  auto table2 = std::make_unique<SimpleTable>(table_name2);

  // With entry list.
  const ResolvedColumn scan_column(
      10, googlesql::IdString::MakeGlobal(table_name1),
      googlesql::IdString::MakeGlobal(col_name), type_factory.get_int32());
  auto table_scan1 = MakeResolvedTableScan({scan_column}, table1.get(),
                                           /*for_system_time_expr=*/nullptr);
  auto table_scan2 = MakeResolvedTableScan(/*column_list=*/{}, table2.get(),
                                           /*for_system_time_expr=*/nullptr);
  auto join_scan =
      MakeResolvedJoinScan({scan_column}, ResolvedJoinScan::INNER,
                           std::move(table_scan1), std::move(table_scan2),
                           /*join_expr=*/nullptr);
  std::vector<std::unique_ptr<const ResolvedWithEntry>> with_entry_list;
  with_entry_list.emplace_back(
      MakeResolvedWithEntry(with_query_name, std::move(join_scan)));

  // With scan query.
  const ResolvedColumn query_column(
      20, googlesql::IdString::MakeGlobal(with_query_name),
      googlesql::IdString::MakeGlobal(col_name), type_factory.get_int32());
  std::unique_ptr<ResolvedWithRefScan> with_ref_scan =
      MakeResolvedWithRefScan({query_column}, with_query_name);

  auto query = MakeResolvedProjectScan({query_column}, /*expr_list=*/{},
                                       std::move(with_ref_scan));
  auto with_scan =
      MakeResolvedWithScan({scan_column}, std::move(with_entry_list),
                           std::move(query), /*recursive=*/false);

  // Test that this scan is correctly supported in WITH clause.
  SQLBuilder sql_builder;
  GOOGLESQL_ASSERT_OK(sql_builder.Process(*with_scan));
  std::string formatted_sql;
  GOOGLESQL_ASSERT_OK(FormatSql(sql_builder.sql(), &formatted_sql));
  EXPECT_EQ(
      "WITH\n  WithTable AS (\n"
      "    SELECT\n      t1_2.a_1 AS a_1\n    FROM\n"
      "      (\n        SELECT\n          T1.C AS a_1\n        FROM\n"
      "          T1\n      ) AS t1_2\n"
      "      CROSS JOIN\n"
      "      (\n        SELECT\n          NULL\n        FROM\n"
      "          T2\n      ) AS t2_3\n"
      "  )\n"
      "SELECT\n  withrefscan_4.a_1 AS a_1\nFROM\n  WithTable AS withrefscan_4;",
      formatted_sql);
}

// Adding specific unit test to input provided by Random Query Generator tree.
// From a SQL String (like in golden file sql_builder.test), we get a different
// tree (ArrayScan is under other ResolvedScans and this scenario isn't tested.
TEST(SQLBuilderTest, WithScanWithArrayScan) {
  const std::string with_query_name = "WithTable";
  const std::string table_name = "T1";
  const std::string col_name = "C";

  TypeFactory type_factory;
  auto table = std::make_unique<SimpleTable>(table_name);

  // With entry list.
  const ResolvedColumn scan_column(
      10, googlesql::IdString::MakeGlobal(table_name),
      googlesql::IdString::MakeGlobal(col_name), type_factory.get_int32());
  auto table_scan = MakeResolvedTableScan({scan_column}, table.get(),
                                          /*for_system_time_expr=*/nullptr);
  const ResolvedColumn array_column(
      15, googlesql::IdString::MakeGlobal("ArrayName"),
      googlesql::IdString::MakeGlobal("ArrayCol"), type_factory.get_int32());
  auto array_expr = MakeResolvedSubqueryExpr(
      type_factory.get_bool(), ResolvedSubqueryExpr::ARRAY,
      /*parameter_list=*/{}, /*in_expr=*/nullptr,
      /*subquery=*/MakeResolvedSingleRowScan());
  std::vector<std::unique_ptr<const ResolvedExpr>> array_expr_list;
  array_expr_list.push_back(std::move(array_expr));
  std::vector<ResolvedColumn> element_column_list;
  element_column_list.push_back(array_column);
  auto array_scan = MakeResolvedArrayScan({scan_column}, std::move(table_scan),
                                          std::move(array_expr_list),
                                          std::move(element_column_list),
                                          /*array_offset_column=*/nullptr,
                                          /*join_expr=*/nullptr,
                                          /*is_outer=*/true,
                                          /*array_zip_mode=*/nullptr);
  std::vector<std::unique_ptr<const ResolvedWithEntry>> with_entry_list;
  with_entry_list.emplace_back(
      MakeResolvedWithEntry(with_query_name, std::move(array_scan)));

  // With scan query.
  const ResolvedColumn query_column(
      20, googlesql::IdString::MakeGlobal(with_query_name),
      googlesql::IdString::MakeGlobal(col_name), type_factory.get_int32());
  std::unique_ptr<ResolvedWithRefScan> with_ref_scan =
      MakeResolvedWithRefScan({query_column}, with_query_name);
  auto query = MakeResolvedProjectScan({query_column}, /*expr_list=*/{},
                                       std::move(with_ref_scan));
  auto with_scan =
      MakeResolvedWithScan({scan_column}, std::move(with_entry_list),
                           std::move(query), /*recursive=*/false);

  // Test that this scan is correctly supported in WITH clause.
  SQLBuilder sql_builder;
  GOOGLESQL_ASSERT_OK(sql_builder.Process(*with_scan));
  std::string formatted_sql;
  GOOGLESQL_ASSERT_OK(FormatSql(sql_builder.sql(), &formatted_sql));
  EXPECT_EQ(
      "WITH\n  WithTable AS (\n"
      "    SELECT\n      t1_2.a_1 AS a_1\n    FROM\n"
      "      (\n        SELECT\n          T1.C AS a_1\n        FROM\n"
      "          T1\n      ) AS t1_2\n"
      "      LEFT JOIN\n"
      "      UNNEST(ARRAY(\n"
      "        SELECT\n          1\n      )) AS a_3\n"
      "  )\n"
      "SELECT\n  withrefscan_4.a_1 AS a_1\nFROM\n  WithTable AS withrefscan_4;",
      formatted_sql);
}

TEST(AnalyzerTest, AnalyzeStatementsOfScript) {
  SampleCatalog catalog;
  std::string script = "SELECT * FROM KeyValue;\nSELECT garbage;";
  std::unique_ptr<ParserOutput> parser_output;
  GOOGLESQL_ASSERT_OK(ParseScript(script, ParserOptions(),
                        {.mode = ERROR_MESSAGE_ONE_LINE,
                         .attach_error_location_payload = false,
                         .stability = GetDefaultErrorMessageStability()},
                        &parser_output));
  ASSERT_EQ(parser_output->script()->statement_list().size(), 2);

  AnalyzerOptions analyzer_options;
  std::unique_ptr<const AnalyzerOutput> analyzer_output;

  // Analyze first statement in script - this should succeed.
  GOOGLESQL_ASSERT_OK(AnalyzeStatementFromParserAST(
      *parser_output->script()->statement_list()[0], analyzer_options, script,
      catalog.catalog(), catalog.type_factory(), &analyzer_output));
  ASSERT_EQ(analyzer_output->resolved_statement()->node_kind(),
            RESOLVED_QUERY_STMT);

  // Analyze second statement in script - this should fail, and the error
  // message should be relative to the entire script, not just the particular
  // statement being analyzed.
  absl::Status status = AnalyzeStatementFromParserAST(
      *parser_output->script()->statement_list()[1], analyzer_options, script,
      catalog.catalog(), catalog.type_factory(), &analyzer_output);
  ASSERT_THAT(status, StatusIs(absl::StatusCode::kInvalidArgument,
                               "Unrecognized name: garbage [at 2:8]"));
}

class AnalyzeGeneratedColumnTest : public testing::Test {
 public:
  AnalyzeGeneratedColumnTest() : catalog_("test_catalog") {}
  void SetUp() override {
    catalog_.AddBuiltinFunctions(
        BuiltinFunctionOptions::AllReleasedFunctions());
    analyzer_options_.mutable_language()->SetSupportsAllStatementKinds();
  }
  void AddGeneratedColumnToTable(std::string column_name,
                                 std::vector<std::string> expression_columns,
                                 std::string generated_expr,
                                 SimpleTable* table) {
    AnalyzerOptions options = analyzer_options_;
    std::unique_ptr<const AnalyzerOutput> output;
    for (auto expression_column : expression_columns) {
      GOOGLESQL_ASSERT_OK(
          options.AddExpressionColumn(expression_column, types::Int64Type()));
    }
    GOOGLESQL_ASSERT_OK(AnalyzeExpression(generated_expr, options, &catalog_,
                                catalog_.type_factory(), &output));
    SimpleColumn::ExpressionAttributes expr_attributes(
        SimpleColumn::ExpressionAttributes::ExpressionKind::GENERATED,
        generated_expr, output->resolved_expr());
    GOOGLESQL_ASSERT_OK(table->AddColumn(
        new SimpleColumn(table->Name(), column_name, types::Int64Type(),
                         {.column_expression = expr_attributes}),
        /*is_owned=*/true));
    outputs_.push_back(std::move(output));
  }
  void AddGeneratedColumnsTestTable() {
    // Table representation
    // CREATE TABLE test_table (
    // A as (B+C),
    // B as (C+1),
    // C int64,
    // D as (A+B+C),
    // ID int64,
    // ) PRIMARY KEY(ID)
    auto test_table = std::make_unique<SimpleTable>(
        "test_table", std::vector<SimpleTable::NameAndType>{});

    // Adding column A AS (B+C) to test_table.
    AddGeneratedColumnToTable("A", {"B", "C"}, "B+C", test_table.get());
    // Adding column B AS (C+1) to test_table.
    AddGeneratedColumnToTable("B", {"C"}, "C+1", test_table.get());
    // Adding column C int64 to test_table.
    GOOGLESQL_ASSERT_OK(test_table->AddColumn(
        new SimpleColumn(test_table->Name(), "C", types::Int64Type()),
        /*is_owned=*/true));
    // Adding column D AS (A+B+C) to test_table.
    AddGeneratedColumnToTable("D", {"A", "B", "C"}, "A+B+C", test_table.get());
    // Adding column ID int64 to test_table.
    GOOGLESQL_ASSERT_OK(test_table->AddColumn(
        new SimpleColumn(test_table->Name(), "ID", types::Int64Type()),
        /*is_owned=*/true));

    test_table->SetContents(
        {{values::Int64(3), values::Int64(2), values::Int64(1),
          values::Int64(6), values::Int64(1)},
         {values::Int64(5), values::Int64(3), values::Int64(2),
          values::Int64(10), values::Int64(2)}});
    GOOGLESQL_ASSERT_OK(test_table->SetPrimaryKey({0}));
    catalog_.AddOwnedTable(std::move(test_table));
  }
  void AddGeneratedColumnsAsPrimaryKeyTestTable() {
    // Table representation
    // CREATE TABLE test_table (
    // A as (B+C),
    // B as (C+1),
    // C int64,
    // D as (A+B+C),
    // ) PRIMARY KEY(A,C)
    auto test_table = std::make_unique<SimpleTable>(
        "test_table", std::vector<SimpleTable::NameAndType>{});

    // Adding column A AS (B+C) to test_table.
    AddGeneratedColumnToTable("A", {"B", "C"}, "B+C", test_table.get());
    // Adding column B AS (C+1) to test_table.
    AddGeneratedColumnToTable("B", {"C"}, "C+1", test_table.get());
    // Adding column C int64 to test_table.
    GOOGLESQL_ASSERT_OK(test_table->AddColumn(
        new SimpleColumn(test_table->Name(), "C", types::Int64Type()),
        /*is_owned=*/true));
    // Adding column D AS (A+B+C) to test_table.
    AddGeneratedColumnToTable("D", {"A", "B", "C"}, "A+B+C", test_table.get());

    test_table->SetContents({{values::Int64(3), values::Int64(2),
                              values::Int64(1), values::Int64(6)},
                             {values::Int64(5), values::Int64(3),
                              values::Int64(2), values::Int64(10)}});
    GOOGLESQL_ASSERT_OK(test_table->SetPrimaryKey({0, 2}));
    catalog_.AddOwnedTable(std::move(test_table));
  }
  void AddCyclicGeneratedColumnsTable() {
    // Setup catalog and table.
    // Table ddl representation
    // CREATE TABLE test_table (
    // id INT64,
    // data INT64,
    // gen2 AS gen1*2,
    // gen1 AS gen2*2,
    // ) PRIMARY KEY(id,gen2);
    auto test_table = std::make_unique<SimpleTable>(
        "test_table",
        std::vector<SimpleTable::NameAndType>{{"id", types::Int64Type()},
                                              {"data", types::Int64Type()}});
    // Adding column gen2 AS (gen1*2) to test_table.
    AddGeneratedColumnToTable("gen2", {"gen1"}, "gen1*2", test_table.get());
    // Adding column gen1 AS (gen2*2) to test_table.
    AddGeneratedColumnToTable("gen1", {"gen2"}, "gen2*2", test_table.get());

    test_table->SetContents({{values::Int64(1), values::Int64(2),
                              values::Int64(8), values::Int64(4)},
                             {values::Int64(2), values::Int64(3),
                              values::Int64(12), values::Int64(6)}});
    GOOGLESQL_ASSERT_OK(test_table->SetPrimaryKey({0, 2}));
    catalog_.AddOwnedTable(std::move(test_table));
  }
  void AddNonGeneratedColumnsTable() {
    auto test_table = std::make_unique<SimpleTable>(
        "test_table",
        std::vector<SimpleTable::NameAndType>{{"id", types::Int64Type()},
                                              {"data", types::Int64Type()}});
    test_table->SetContents({{values::Int64(1), values::Int64(2),
                              values::Int64(8), values::Int64(4)},
                             {values::Int64(2), values::Int64(3),
                              values::Int64(12), values::Int64(6)}});
    GOOGLESQL_ASSERT_OK(test_table->SetPrimaryKey({0}));
    catalog_.AddOwnedTable(std::move(test_table));
  }
  absl::Status GetColumnNamesFromResolvedIds(
      std::vector<int> resolved_ids, const ResolvedTableScan* table_scan,
      std::vector<std::string>& column_names) {
    for (int resolved_id : resolved_ids) {
      bool id_found = false;
      for (const ResolvedColumn& column : table_scan->column_list()) {
        if (column.column_id() == resolved_id) {
          id_found = true;
          column_names.push_back(column.name());
        }
      }
      GOOGLESQL_RET_CHECK(id_found);
    }
    return absl::OkStatus();
  }

 protected:
  SimpleCatalog catalog_;
  std::vector<std::unique_ptr<const AnalyzerOutput>> outputs_;
  AnalyzerOptions analyzer_options_;
};

TEST_F(AnalyzeGeneratedColumnTest, TopologicalOrderOfGeneratedColumns) {
  AddGeneratedColumnsAsPrimaryKeyTestTable();
  std::string sql = "INSERT into test_table(C) values(3);";
  std::unique_ptr<const AnalyzerOutput> analyzer_output;
  std::vector<std::string> topologically_sorted_columns;

  GOOGLESQL_ASSERT_OK(AnalyzeStatement(sql, analyzer_options_, &catalog_,
                             catalog_.type_factory(), &analyzer_output));
  const ResolvedInsertStmt* insert_statement =
      analyzer_output->resolved_statement()->GetAs<ResolvedInsertStmt>();
  ASSERT_NE(insert_statement, nullptr);
  GOOGLESQL_ASSERT_OK(GetColumnNamesFromResolvedIds(
      insert_statement->topologically_sorted_generated_column_id_list(),
      insert_statement->table_scan(), topologically_sorted_columns));
  EXPECT_THAT(topologically_sorted_columns, ElementsAre("B", "A", "D"));
  EXPECT_EQ(
      insert_statement->topologically_sorted_generated_column_id_list().size(),
      3);
  // resolved expression for generated column `B = C+1`
  EXPECT_EQ(insert_statement->generated_column_expr_list()[0]->DebugString(),
            R"(FunctionCall(GoogleSQL:$add(INT64, INT64) -> INT64)
+-ColumnRef(type=INT64, column=test_table.C#3)
+-Literal(type=INT64, value=1)
)");
  // resolved expression for generated column `A = B+C`
  EXPECT_EQ(insert_statement->generated_column_expr_list()[1]->DebugString(),
            R"(FunctionCall(GoogleSQL:$add(INT64, INT64) -> INT64)
+-ColumnRef(type=INT64, column=test_table.B#2)
+-ColumnRef(type=INT64, column=test_table.C#3)
)");
  // resolved expression for generated column `D = A+B+C`
  EXPECT_EQ(insert_statement->generated_column_expr_list()[2]->DebugString(),
            R"(FunctionCall(GoogleSQL:$add(INT64, INT64) -> INT64)
+-FunctionCall(GoogleSQL:$add(INT64, INT64) -> INT64)
| +-ColumnRef(type=INT64, column=test_table.A#1)
| +-ColumnRef(type=INT64, column=test_table.B#2)
+-ColumnRef(type=INT64, column=test_table.C#3)
)");
}

TEST_F(AnalyzeGeneratedColumnTest,
       AnalyzeInsertStatementFailsWithCyclicGenColInTable) {
  AddCyclicGeneratedColumnsTable();
  std::unique_ptr<const AnalyzerOutput> analyzer_output;
  std::string sql = "INSERT into test_table(id,data) values(3,7);";

  // Analyze the insert statement - this should fail as generated columns in
  // table have cycles.
  EXPECT_THAT(
      AnalyzeStatement(sql, analyzer_options_, &catalog_,
                       catalog_.type_factory(), &analyzer_output),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          MatchesRegex(
              "Recursive dependencies detected while evaluating generated "
              "column expression values for test_table.gen.. The expression "
              "indicates the column depends on itself. Columns forming the "
              "cycle : test_table.gen., test_table.gen.")));
}

TEST_F(AnalyzeGeneratedColumnTest,
       AnalyzeInsertStatementHavingNoGenColInTable) {
  AddNonGeneratedColumnsTable();
  std::unique_ptr<const AnalyzerOutput> analyzer_output;
  std::string sql = "INSERT into test_table(id,data) values(3,7);";

  // Analyze the insert statement.
  GOOGLESQL_ASSERT_OK(AnalyzeStatement(sql, analyzer_options_, &catalog_,
                             catalog_.type_factory(), &analyzer_output));
  EXPECT_THAT(analyzer_output->resolved_statement()
                  ->GetAs<ResolvedInsertStmt>()
                  ->topologically_sorted_generated_column_id_list(),
              IsEmpty());
}

TEST_F(AnalyzeGeneratedColumnTest,
       TopologicalOrderOfGeneratedColumnsForUpdate) {
  AddGeneratedColumnsTestTable();
  std::string sql = "UPDATE test_table SET C = 3 WHERE ID = 1;";
  std::unique_ptr<const AnalyzerOutput> analyzer_output;
  std::vector<std::string> topologically_sorted_columns;

  GOOGLESQL_ASSERT_OK(AnalyzeStatement(sql, analyzer_options_, &catalog_,
                             catalog_.type_factory(), &analyzer_output));
  const ResolvedUpdateStmt* update_statement =
      analyzer_output->resolved_statement()->GetAs<ResolvedUpdateStmt>();
  ASSERT_NE(update_statement, nullptr);
  GOOGLESQL_ASSERT_OK(GetColumnNamesFromResolvedIds(
      update_statement->topologically_sorted_generated_column_id_list(),
      update_statement->table_scan(), topologically_sorted_columns));
  EXPECT_THAT(topologically_sorted_columns, ElementsAre("B", "A", "D"));
  EXPECT_EQ(
      update_statement->topologically_sorted_generated_column_id_list().size(),
      3);
  // resolved expression for generated column `B = C+1`
  EXPECT_EQ(update_statement->generated_column_expr_list()[0]->DebugString(),
            R"(FunctionCall(GoogleSQL:$add(INT64, INT64) -> INT64)
+-ColumnRef(type=INT64, column=test_table.C#3)
+-Literal(type=INT64, value=1)
)");
  // resolved expression for generated column `A = B+C`
  EXPECT_EQ(update_statement->generated_column_expr_list()[1]->DebugString(),
            R"(FunctionCall(GoogleSQL:$add(INT64, INT64) -> INT64)
+-ColumnRef(type=INT64, column=test_table.B#2)
+-ColumnRef(type=INT64, column=test_table.C#3)
)");
  // resolved expression for generated column `D = A+B+C`
  EXPECT_EQ(update_statement->generated_column_expr_list()[2]->DebugString(),
            R"(FunctionCall(GoogleSQL:$add(INT64, INT64) -> INT64)
+-FunctionCall(GoogleSQL:$add(INT64, INT64) -> INT64)
| +-ColumnRef(type=INT64, column=test_table.A#1)
| +-ColumnRef(type=INT64, column=test_table.B#2)
+-ColumnRef(type=INT64, column=test_table.C#3)
)");
}

TEST_F(AnalyzeGeneratedColumnTest,
       AnalyzeUpdateStatementFailsWithCyclicGenColInTable) {
  AddCyclicGeneratedColumnsTable();
  std::unique_ptr<const AnalyzerOutput> analyzer_output;
  std::string sql = "UPDATE test_table SET data = 7 WHERE id = 3;";

  // Analyze the update statement - this should fail as generated columns in
  // table have cycles.
  EXPECT_THAT(
      AnalyzeStatement(sql, analyzer_options_, &catalog_,
                       catalog_.type_factory(), &analyzer_output),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          MatchesRegex(
              "Recursive dependencies detected while evaluating generated "
              "column expression values for test_table.gen.. The expression "
              "indicates the column depends on itself. Columns forming the "
              "cycle : test_table.gen., test_table.gen.")));
}

TEST_F(AnalyzeGeneratedColumnTest, UpdateCount) {
  auto t = std::make_unique<SimpleTable>(
      "T", std::vector<SimpleTable::NameAndType>{});
  GOOGLESQL_ASSERT_OK(t->AddColumn(new SimpleColumn(t->Name(), "Id", types::StringType()),
                         /*is_owned=*/true));
  AddGeneratedColumnToTable("Value", {}, "1+1", t.get());
  t->SetContents({{values::String("1"), values::Int64(2)}});
  GOOGLESQL_ASSERT_OK(t->SetPrimaryKey({0}));
  catalog_.AddOwnedTable(std::move(t));

  auto c = std::make_unique<SimpleTable>(
      "C", std::vector<SimpleTable::NameAndType>{});
  GOOGLESQL_ASSERT_OK(c->AddColumn(new SimpleColumn(c->Name(), "Id", types::StringType()),
                         /*is_owned=*/true));
  GOOGLESQL_ASSERT_OK(
      c->AddColumn(new SimpleColumn(c->Name(), "Count", types::Int64Type()),
                   /*is_owned=*/true));
  c->SetContents({{values::String("1"), values::Int64(0)}});
  GOOGLESQL_ASSERT_OK(c->SetPrimaryKey({0}));
  catalog_.AddOwnedTable(std::move(c));

  std::string sql = "UPDATE C set Count=(SELECT count(*) from T) WHERE id='1';";
  std::unique_ptr<const AnalyzerOutput> analyzer_output;

  GOOGLESQL_EXPECT_OK(AnalyzeStatement(sql, analyzer_options_, &catalog_,
                             catalog_.type_factory(), &analyzer_output));
}

// Verify the catalog name path of the outer proto type will be carried to its
// inner field types.
// Have to put it here rather than in a text-based test since the Java library
// does not support deserializing the catalog name paths in the ProtoTypeProto
// from the SampleCatalog yet.
// See
// https://github.com/google/googlesql/blob/master/java/com/google/googlesql/TypeFactory.java;rcl=468220127;l=378
TEST(AnalyzerTest, ProtoTypesWithCatalogNamePath) {
  SampleCatalog sample_catalog;
  SimpleCatalog* catalog = sample_catalog.catalog();
  TypeFactory* factory = sample_catalog.type_factory();

  // Create a proto type with a catalog name path.
  const ProtoType* kitchen_sink_type = nullptr;
  GOOGLESQL_ASSERT_OK(factory->MakeProtoType(googlesql_test::KitchenSinkPB::descriptor(),
                                   &kitchen_sink_type,
                                   /*catalog_name_path=*/{"abc", "def"}));
  auto sub1 = std::make_unique<SimpleCatalog>("abc", factory);
  auto sub2 = std::make_unique<SimpleCatalog>("def", factory);
  sub2->AddType("googlesql_test.KitchenSinkPB", kitchen_sink_type);
  sub1->AddOwnedCatalog("def", std::move(sub2));
  catalog->AddOwnedCatalog("abc", std::move(sub1));

  // Get the AST
  AnalyzerOptions analyzer_options;
  std::unique_ptr<const AnalyzerOutput> analyzer_output;

  constexpr absl::string_view sql = R"sql(
WITH source AS (
  SELECT NEW abc.def.googlesql_test.KitchenSinkPB(
    a as int64_key_1,
    a + 1 as int64_key_2,
    MOD(a, 3) as test_enum
  )  t
FROM UNNEST(GENERATE_ARRAY(1, 10)) a
)
SELECT
  t.test_enum,
  t.nested_value
FROM source
  )sql";
  GOOGLESQL_ASSERT_OK(AnalyzeStatement(sql, analyzer_options, catalog, factory,
                             &analyzer_output));

  SQLBuilder sql_builder;
  GOOGLESQL_ASSERT_OK(sql_builder.Process(*analyzer_output->resolved_statement()));
  std::string formatted_sql;
  GOOGLESQL_ASSERT_OK(FormatSql(sql_builder.sql(), &formatted_sql));
  EXPECT_EQ(R"sql(WITH
  source AS (
    SELECT
      NEW abc.def.`googlesql_test.KitchenSinkPB`(a_1 AS int64_key_1, a_1 + 1 AS int64_key_2, CAST(MOD(a_1,
          3) AS abc.def.`googlesql_test.TestEnum`) AS test_enum) AS a_2
    FROM
      UNNEST(GENERATE_ARRAY(1, 10)) AS a_1
  )
SELECT
  withrefscan_3.a_2.test_enum AS test_enum,
  withrefscan_3.a_2.nested_value AS nested_value
FROM
  source AS withrefscan_3;)sql",
            formatted_sql);
}

}  // namespace googlesql
