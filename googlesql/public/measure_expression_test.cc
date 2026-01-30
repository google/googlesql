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

#include "googlesql/public/measure_expression.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/parse_resume_location.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/templated_sql_function.h"
#include "googlesql/public/templated_sql_tvf.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "googlesql/base/testing/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
using ::absl_testing::StatusIs;
using ::testing::HasSubstr;
using ExpressionKind =
    ::googlesql::Column::ExpressionAttributes::ExpressionKind;

namespace {

class TablePermittingDuplicateColumnNames : public Table {
 public:
  explicit TablePermittingDuplicateColumnNames(std::string name)
      : name_(name) {};

  std::string Name() const override { return name_; }

  std::string FullName() const override { return Name(); }

  int NumColumns() const override { return static_cast<int>(columns_.size()); }

  const Column* GetColumn(int i) const override {
    if (i < 0 || i >= columns_.size()) {
      return nullptr;
    }
    return columns_[i].get();
  }

  const Column* FindColumnByName(const std::string& name) const override {
    auto column_matcher = [&name](const std::unique_ptr<const Column>& column) {
      return column->Name() == name;
    };
    auto it = std::find_if(columns_.cbegin(), columns_.cend(), column_matcher);
    if (it == columns_.cend()) {
      return nullptr;
    }
    const Column* column = it->get();
    // Check for duplicate column names.
    it = std::find_if(it + 1, columns_.cend(), column_matcher);
    if (it != columns_.cend()) {
      return nullptr;
    }
    return column;
  }

  void AddColumn(std::unique_ptr<const Column> column) {
    columns_.push_back(std::move(column));
  }

 private:
  std::string name_;
  std::vector<std::unique_ptr<const Column>> columns_;
};

}  // namespace

TEST(DuplicateColumnNamesTest,
     ErrorWhenAnalyzingMeasureOverTableWithDuplicateColumnNames) {
  TypeFactory type_factory;
  SimpleCatalog catalog("placeholder_catalog", &type_factory);
  auto table = std::make_unique<TablePermittingDuplicateColumnNames>(
      "placeholder_table");
  table->AddColumn(std::make_unique<SimpleColumn>(
      "placeholder_table", "key", type_factory.get_int64(), /*is_owned=*/true));
  table->AddColumn(std::make_unique<SimpleColumn>(
      "placeholder_table", "key", type_factory.get_int64(), /*is_owned=*/true));
  AnalyzerOptions analyzer_options;
  std::unique_ptr<const AnalyzerOutput> analyzer_output;
  EXPECT_THAT(
      AnalyzeMeasureExpression("SUM(key)", *table, catalog, type_factory,
                               analyzer_options, analyzer_output),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Measures cannot be defined on tables with duplicate "
                         "column names")));
}

class MeasureExpressionTest : public ::testing::Test {
 public:
  MeasureExpressionTest()
      : type_factory_(), catalog_("measure_catalog", &type_factory_) {
    catalog_.AddBuiltinFunctions(
        BuiltinFunctionOptions::AllReleasedFunctions());
    LoadValueTables();
    LoadUserDefinedEntities();
  }

  void LoadValueTables() {
    const StructType* struct_type = nullptr;
    GOOGLESQL_ASSERT_OK(type_factory_.MakeStructType(
        {{"field_1", type_factory_.get_int64()},
         {"field_2", type_factory_.get_int32()},
         {"duplicate_field", type_factory_.get_int64()},
         {"duplicate_field", type_factory_.get_int64()},
         {"ambiguous", type_factory_.get_int64()}},
        &struct_type));
    auto struct_value_table = std::make_unique<SimpleTable>(
        "StructValueTable",
        std::vector<const Column*>{
            new SimpleColumn("StructValueTable", "value", struct_type),
            new SimpleColumn("StructValueTable", "key",
                             type_factory_.get_int64(),
                             {.is_pseudo_column = true}),
            new SimpleColumn("StructValueTable", "ambiguous",
                             type_factory_.get_int64(),
                             {.is_pseudo_column = true})},
        /*take_ownership=*/true);
    struct_value_table->set_is_value_table(true);
    struct_value_table_ = struct_value_table.get();
    catalog_.AddOwnedTable(std::move(struct_value_table));

    catalog_.SetDescriptorPool(google::protobuf::DescriptorPool::generated_pool());
    const Type* proto_type = nullptr;
    GOOGLESQL_CHECK_OK(catalog_.FindType(
        {std::string(googlesql_test::KitchenSinkPB::descriptor()->full_name())},
        &proto_type));
    ABSL_CHECK(proto_type->IsProto());
    auto proto_value_table = std::make_unique<SimpleTable>(
        "ProtoValueTable",
        std::vector<const Column*>{
            new SimpleColumn("ProtoValueTable", "value", proto_type),
            new SimpleColumn("ProtoValueTable", "key",
                             type_factory_.get_int64(),
                             {.is_pseudo_column = true}),
            new SimpleColumn("ProtoValueTable", "bool_val",
                             type_factory_.get_bool(),
                             {.is_pseudo_column = true}),
        },
        /*take_ownership=*/true);
    proto_value_table->set_is_value_table(true);
    proto_value_table_ = proto_value_table.get();
    catalog_.AddOwnedTable(std::move(proto_value_table));

    auto int64_value_table = std::make_unique<SimpleTable>(
        "Int64ValueTable",
        std::vector<const Column*>{
            new SimpleColumn("Int64ValueTable", "value",
                             type_factory_.get_int64()),
            new SimpleColumn("Int64ValueTable", "key",
                             type_factory_.get_int64(),
                             {.is_pseudo_column = true})},
        /*take_ownership=*/true);
    int64_value_table->set_is_value_table(true);
    int64_value_table_ = int64_value_table.get();
    catalog_.AddOwnedTable(std::move(int64_value_table));
  }

  void LoadUserDefinedEntities() {
    // Non templated UDF
    catalog_.AddOwnedFunction(new Function(
        "udf", "MyGroup", Function::SCALAR,
        {{types::Int64Type(), {types::Int64Type()}, /*context_id=*/-1}},
        FunctionOptions()));
    // Templated UDF
    catalog_.AddOwnedFunction(new TemplatedSQLFunction(
        {"udf_templated"},
        FunctionSignature(FunctionArgumentType(ARG_TYPE_ANY_1),
                          {FunctionArgumentType(
                              ARG_TYPE_ANY_1, FunctionArgumentType::REQUIRED)},
                          /*context_id=*/-1),
        /*argument_names=*/{"x"}, ParseResumeLocation::FromString("x + 42")));

    // Non templated UDA
    catalog_.AddOwnedFunction(new Function(
        "uda", "MyGroup", Function::AGGREGATE,
        {{types::Int64Type(), {types::Int64Type()}, /*context_id=*/-1}},
        FunctionOptions()));
    // Templated UDA
    catalog_.AddOwnedFunction(new TemplatedSQLFunction(
        {"uda_templated"},
        FunctionSignature(FunctionArgumentType(ARG_TYPE_ANY_1),
                          {FunctionArgumentType(
                              ARG_TYPE_ANY_1, FunctionArgumentType::REQUIRED)},
                          /*context_id=*/-1),
        /*argument_names=*/{"x"}, ParseResumeLocation::FromString("sum(x)"),
        Function::AGGREGATE));

    // Non templated TVF
    TVFRelation::ColumnList columns;
    columns.emplace_back("key", types::Int64Type());
    columns.emplace_back("value", types::Int64Type());
    TVFRelation output_schema(columns);
    FunctionSignature tvf_sig(
        FunctionArgumentType::RelationWithSchema(
            output_schema,
            /*extra_relation_input_columns_allowed=*/false),
        {FunctionArgumentType::RelationWithSchema(
            output_schema, /*extra_relation_input_columns_allowed=*/false)},
        /*context_id=*/-1);
    catalog_.AddOwnedTableValuedFunction(
        new FixedOutputSchemaTVF({"tvf"}, {tvf_sig}, output_schema));
    // Templated TVF
    ParseResumeLocation sql_body =
        ParseResumeLocation::FromStringView("SELECT 1 as output");
    FunctionSignature function_signature(FunctionArgumentType::AnyRelation(),
                                         {FunctionArgumentType::AnyRelation()},
                                         /*context_id=*/1);
    catalog_.AddOwnedTableValuedFunction(
        new TemplatedSQLTVF({"tvf_templated"}, function_signature,
                            /*arg_name_list=*/{"x"}, sql_body));
  }

  absl::StatusOr<const ResolvedExpr*> AnalyzeMeasureExpressionForTable(
      absl::string_view table_name,
      std::vector<std::pair<std::string, const Type*>> columns,
      absl::string_view measure_name, absl::string_view measure_expr,
      bool can_reference_udas = true) {
    auto table = std::make_unique<SimpleTable>(table_name, columns);
    AnalyzerOptions analyzer_options;
    analyzer_options.mutable_language()->EnableLanguageFeature(
        FEATURE_MULTILEVEL_AGGREGATION);
    analyzer_options.mutable_language()->EnableLanguageFeature(
        FEATURE_AGGREGATE_FILTERING);
    analyzer_options.mutable_language()->EnableLanguageFeature(
        FEATURE_ORDER_BY_IN_AGGREGATE);
    analyzer_options.mutable_language()->EnableLanguageFeature(
        FEATURE_HAVING_IN_AGGREGATE);
    analyzer_options.mutable_language()->EnableLanguageFeature(
        FEATURE_TABLE_VALUED_FUNCTIONS);
    analyzer_options.mutable_language()->EnableLanguageFeature(
        FEATURE_ANALYTIC_FUNCTIONS);
    analyzer_options.mutable_language()->EnableLanguageFeature(
        FEATURE_WITH_EXPRESSION);
    analyzer_options.mutable_language()->EnableLanguageFeature(
        FEATURE_FIRST_AND_LAST_N);
    if (can_reference_udas) {
      analyzer_options.mutable_language()->EnableLanguageFeature(
          FEATURE_MULTILEVEL_AGGREGATION_ON_UDAS);
    }
    std::unique_ptr<const AnalyzerOutput> analyzer_output;
    GOOGLESQL_ASSIGN_OR_RETURN(
        const ResolvedExpr* resolved_measure_expr,
        AnalyzeMeasureExpression(measure_expr, *table, catalog_, type_factory_,
                                 analyzer_options, analyzer_output));
    analyzer_outputs_.push_back(std::move(analyzer_output));
    return resolved_measure_expr;
  }

 protected:
  TypeFactory type_factory_;
  SimpleCatalog catalog_;
  const SimpleTable* struct_value_table_ = nullptr;
  const SimpleTable* proto_value_table_ = nullptr;
  const SimpleTable* int64_value_table_ = nullptr;
  std::vector<std::unique_ptr<const AnalyzerOutput>> analyzer_outputs_;
};

static void TableContainsMeasure(const SimpleTable& table,
                                 const std::string& measure_name,
                                 absl::string_view measure_expr,
                                 ResolvedNodeKind measure_node_kind) {
  const Column* measure_column = table.FindColumnByName(measure_name);
  ASSERT_NE(measure_column, nullptr);
  EXPECT_TRUE(measure_column->GetType()->IsMeasureType());
  EXPECT_TRUE(measure_column->HasMeasureExpression() &&
              measure_column->GetExpression()->HasResolvedExpression());
  EXPECT_EQ(measure_column->GetExpression()->GetExpressionKind(),
            ExpressionKind::MEASURE_EXPRESSION);
  EXPECT_EQ(measure_column->GetExpression()->GetExpressionString(),
            measure_expr);
  EXPECT_EQ(
      measure_column->GetExpression()->GetResolvedExpression()->node_kind(),
      measure_node_kind);
}

TEST_F(MeasureExpressionTest, InvalidMeasureReferencingMeasure) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* measure_type,
      type_factory_.MakeMeasureType(type_factory_.get_int64()));
  EXPECT_THAT(
      AnalyzeMeasureExpressionForTable("table",
                                       {{"key", type_factory_.get_int64()},
                                        {"value", type_factory_.get_int64()},
                                        {"measure_col", measure_type}},
                                       "measure_1", "ANY_VALUE(measure_col)"),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr(
              "Measure expression: ANY_VALUE(measure_col) cannot reference "
              "column: measure_col which contains a measure type")));
}

TEST_F(MeasureExpressionTest, InvalidMeasureReferencingArrayOfMeasure) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* measure_type,
      type_factory_.MakeMeasureType(type_factory_.get_int64()));
  const Type* array_of_measure_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory_.MakeArrayType(measure_type, &array_of_measure_type));
  EXPECT_THAT(
      AnalyzeMeasureExpressionForTable(
          "table",
          {{"key", type_factory_.get_int64()},
           {"value", type_factory_.get_int64()},
           {"array_of_measure", array_of_measure_type}},
          "measure_1", "ANY_VALUE(array_of_measure)"),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Measure expression: ANY_VALUE(array_of_measure) "
                         "cannot reference column: array_of_measure which "
                         "contains a measure type")));
}

TEST_F(MeasureExpressionTest, MeasureReferencingUdfs) {
  std::vector<std::pair<std::string, const Type*>> columns = {
      {"key", type_factory_.get_int64()}, {"value", type_factory_.get_int64()}};
  catalog_.AddOwnedTable(new SimpleTable("TestTable", columns));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const ResolvedExpr* measure_expr,
      AnalyzeMeasureExpressionForTable("table", columns, "measure_col",
                                       "SUM(value) + udf(1)"));
  ASSERT_NE(measure_expr, nullptr);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(measure_expr, AnalyzeMeasureExpressionForTable(
                                         "table", columns, "measure_col",
                                         "SUM(value) + udf_templated(1)"));
  ASSERT_NE(measure_expr, nullptr);
}

TEST_F(MeasureExpressionTest, MeasureReferencingUdas) {
  std::vector<std::pair<std::string, const Type*>> columns = {
      {"key", type_factory_.get_int64()}, {"value", type_factory_.get_int64()}};

  // Without FEATURE_MULTILEVEL_AGGREGATION_ON_UDAS, we cannot reference UDAs in
  // measure expressions.
  EXPECT_THAT(
      AnalyzeMeasureExpressionForTable("table", columns, "measure_col",
                                       "SUM(value) + uda(value)",
                                       /*can_reference_udas=*/false),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Measure expression must not reference UDA; found: "
                         "uda")));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const ResolvedExpr* measure_expr,
      AnalyzeMeasureExpressionForTable("table", columns, "measure_col",
                                       "SUM(value) + uda(value)"));
  ASSERT_NE(measure_expr, nullptr);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(measure_expr, AnalyzeMeasureExpressionForTable(
                                         "table", columns, "measure_col",
                                         "SUM(value) + uda_templated(value)"));
  ASSERT_NE(measure_expr, nullptr);
}

TEST_F(MeasureExpressionTest, InvalidMeasureReferencingUserDefinedEntities) {
  std::vector<std::pair<std::string, const Type*>> columns = {
      {"key", type_factory_.get_int64()}, {"value", type_factory_.get_int64()}};
  catalog_.AddOwnedTable(new SimpleTable("TestTable", columns));

  EXPECT_THAT(
      AnalyzeMeasureExpressionForTable(
          "table", columns, "measure_col",
          "SUM(value) + countif(exists(select 1 from tvf(TABLE TestTable)))"),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr("Measure expression must not reference TVF; found: tvf")));

  EXPECT_THAT(
      AnalyzeMeasureExpressionForTable("table", columns, "measure_col",
                                       "SUM(value) + countif(exists(select 1 "
                                       "from tvf_templated(TABLE TestTable)))"),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Measure expression must not reference TVF; found: "
                         "tvf_templated")));
}

TEST_F(MeasureExpressionTest, MeasureExpressionOnStructValueTable) {
  std::unique_ptr<const AnalyzerOutput> analyzer_output;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedExpr* resolved_measure_expr,
                       AnalyzeMeasureExpression(
                           "SUM(field_1)", *struct_value_table_, catalog_,
                           type_factory_, AnalyzerOptions(), analyzer_output));
  EXPECT_EQ(resolved_measure_expr->node_kind(),
            ResolvedAggregateFunctionCall::TYPE);
}

TEST_F(MeasureExpressionTest,
       MeasureExpressionOnStructValueTable_ReferencingValueTableColumnName) {
  std::unique_ptr<const AnalyzerOutput> analyzer_output;
  EXPECT_THAT(AnalyzeMeasureExpression("SUM(value)", *struct_value_table_,
                                       catalog_, type_factory_,
                                       AnalyzerOptions(), analyzer_output),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Unrecognized name: value")));
}

TEST_F(MeasureExpressionTest,
       MeasureExpressionOnStructValueTable_ReferencingNonExistingField) {
  std::unique_ptr<const AnalyzerOutput> analyzer_output;
  EXPECT_THAT(AnalyzeMeasureExpression(
                  "SUM(non_existent_field)", *struct_value_table_, catalog_,
                  type_factory_, AnalyzerOptions(), analyzer_output),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Unrecognized name: non_existent_field")));
}

TEST_F(MeasureExpressionTest,
       MeasureExpressionOnStructValueTable_ReferencingDuplicateField) {
  std::unique_ptr<const AnalyzerOutput> analyzer_output;
  EXPECT_THAT(
      AnalyzeMeasureExpression("SUM(duplicate_field)", *struct_value_table_,
                               catalog_, type_factory_, AnalyzerOptions(),
                               analyzer_output),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Field duplicate_field is ambiguous in value table: "
                         "StructValueTable of type: STRUCT<field_1 INT64, "
                         "field_2 INT32, duplicate_field INT64, "
                         "duplicate_field INT64, ambiguous INT64>")));
}

TEST_F(MeasureExpressionTest,
       MeasureExpressionOnStructValueTable_ReferencingAmbiguousField) {
  std::unique_ptr<const AnalyzerOutput> analyzer_output;
  EXPECT_THAT(
      AnalyzeMeasureExpression("SUM(ambiguous)", *struct_value_table_, catalog_,
                               type_factory_, AnalyzerOptions(),
                               analyzer_output),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr(
              "Column `ambiguous` is ambiguous in value table "
              "`StructValueTable` for measure expression: SUM(ambiguous)")));
}

TEST_F(MeasureExpressionTest, MeasureExpressionOnProtoValueTable) {
  std::unique_ptr<const AnalyzerOutput> analyzer_output;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ResolvedExpr* resolved_measure_expr,
                       AnalyzeMeasureExpression(
                           "SUM(int64_val)", *proto_value_table_, catalog_,
                           type_factory_, AnalyzerOptions(), analyzer_output));
  EXPECT_EQ(resolved_measure_expr->node_kind(),
            ResolvedAggregateFunctionCall::TYPE);
}

TEST_F(MeasureExpressionTest,
       MeasureExpressionOnProtoValueTable_ReferencingValueTableColumnName) {
  std::unique_ptr<const AnalyzerOutput> analyzer_output;
  EXPECT_THAT(AnalyzeMeasureExpression("SUM(value)", *proto_value_table_,
                                       catalog_, type_factory_,
                                       AnalyzerOptions(), analyzer_output),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Unrecognized name: value")));
}

TEST_F(MeasureExpressionTest,
       MeasureExpressionOnProtoValueTable_ReferencingNonExistingField) {
  std::unique_ptr<const AnalyzerOutput> analyzer_output;
  EXPECT_THAT(AnalyzeMeasureExpression(
                  "SUM(non_existent_field)", *proto_value_table_, catalog_,
                  type_factory_, AnalyzerOptions(), analyzer_output),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Unrecognized name: non_existent_field")));
}

TEST_F(MeasureExpressionTest,
       MeasureExpressionOnProtoValueTable_ReferencingAmbiguousField) {
  std::unique_ptr<const AnalyzerOutput> analyzer_output;
  EXPECT_THAT(
      AnalyzeMeasureExpression("ANY_VALUE(bool_val)", *proto_value_table_,
                               catalog_, type_factory_, AnalyzerOptions(),
                               analyzer_output),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr(
              "Column `bool_val` is ambiguous in value table `ProtoValueTable` "
              "for measure expression: ANY_VALUE(bool_val)")));
}

TEST_F(MeasureExpressionTest,
       MeasureExpressionOnInt64ValueTable_ReferencingValueTableColumnName) {
  std::unique_ptr<const AnalyzerOutput> analyzer_output;
  EXPECT_THAT(AnalyzeMeasureExpression("SUM(value)", *int64_value_table_,
                                       catalog_, type_factory_,
                                       AnalyzerOptions(), analyzer_output),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Unrecognized name: value")));
}

}  // namespace googlesql
