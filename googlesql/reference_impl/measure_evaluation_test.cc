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


#include "googlesql/reference_impl/measure_evaluation.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/common/measure_analysis_utils.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_builder.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "googlesql/base/testing/status_matchers.h"
#include "absl/strings/string_view.h"

namespace googlesql {
namespace {

using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::testing::HasSubstr;

class MeasureColumnToExprMappingTest : public ::testing::Test {
 public:
  explicit MeasureColumnToExprMappingTest()
      : type_factory_(), catalog_("measure_test_catalog", &type_factory_) {
    catalog_.AddBuiltinFunctions(
        BuiltinFunctionOptions::AllReleasedFunctions());
    table_with_measures_ = std::make_unique<SimpleTable>(
        "table_with_measures", std::vector<std::pair<std::string, const Type*>>{
                                   {"key", type_factory_.get_int64()},
                                   {"value", type_factory_.get_int64()}});
    GOOGLESQL_CHECK_OK(table_with_measures_->SetRowIdentityColumns({0}));
    AnalyzerOptions analyzer_options;
    measure_analyzer_outputs_ =
        AddMeasureColumnsToTable(*table_with_measures_,
                                 {{"sum_value", "SUM(value)"}}, type_factory_,
                                 catalog_, analyzer_options)
            .value();
    ABSL_CHECK_EQ(measure_analyzer_outputs_.size(), 1);
    ABSL_CHECK(measure_analyzer_outputs_[0]->resolved_expr() != nullptr);
  }

  const Column& GetMeasureCatalogColumn() const {
    return *table_with_measures_->FindColumnByName("sum_value");
  }

  int GetMeasureCatalogColumnIndex() const {
    return table_with_measures_->NumColumns() - 1;
  }

  const SimpleTable& GetTableWithMeasures() const {
    return *table_with_measures_;
  }

  TypeFactory* GetTypeFactory() { return &type_factory_; }

 private:
  TypeFactory type_factory_;
  SimpleCatalog catalog_;
  std::unique_ptr<SimpleTable> table_with_measures_;
  std::vector<std::unique_ptr<const AnalyzerOutput>> measure_analyzer_outputs_;
};

TEST_F(MeasureColumnToExprMappingTest, TrackMeasureColumns) {
  const SimpleTable& table = GetTableWithMeasures();
  const Column& measure_catalog_column = GetMeasureCatalogColumn();
  const Type* measure_type = measure_catalog_column.GetType();
  ASSERT_TRUE(measure_catalog_column.HasMeasureExpression());
  const ResolvedExpr* measure_expr =
      measure_catalog_column.GetExpression().value().GetResolvedExpression();
  MeasureColumnToExprMapping measure_column_to_expr_mapping;

  // Create a ResolvedColumn for the measure.
  IdStringPool id_string_pool;
  ResolvedColumn measure_column(
      /*column_id=*/1, id_string_pool.Make(GetTableWithMeasures().Name()),
      id_string_pool.Make("measure_resolved_column"), measure_type);
  // Create a ResolvedTableScan emitting the measure column.
  auto table_scan = ResolvedTableScanBuilder()
                        .set_table(&table)
                        .set_column_list({measure_column})
                        .set_column_index_list({GetMeasureCatalogColumnIndex()})
                        .Build();
  GOOGLESQL_CHECK_OK(table_scan);

  // Ensure that the measure expression can be looked up.
  GOOGLESQL_EXPECT_OK(
      measure_column_to_expr_mapping.TrackMeasureColumnsEmittedByTableScan(
          *table_scan.value()));
  EXPECT_THAT(measure_column_to_expr_mapping.GetMeasureExpr(measure_column),
              IsOkAndHolds(measure_expr));

  // Ensure that measure expression lookup fails for columns not emitted by the
  // table scan.
  ResolvedColumn renamed_measure_column(
      /*column_id=*/2, id_string_pool.Make(GetTableWithMeasures().Name()),
      id_string_pool.Make("renamed_measure_column"), measure_type);
  EXPECT_THAT(
      measure_column_to_expr_mapping.GetMeasureExpr(renamed_measure_column),
      StatusIs(absl::StatusCode::kNotFound, HasSubstr("Column not found")));

  // Create a WithRefScan emitting the renamed measure column, and track the
  // renamed measure column.
  auto with_entry = ResolvedWithEntryBuilder()
                        .set_with_query_name("placeholder_with_query_name")
                        .set_with_subquery(std::move(table_scan.value()))
                        .Build();
  GOOGLESQL_CHECK_OK(with_entry);
  measure_column_to_expr_mapping.TrackWithQueryScan(*with_entry.value());
  auto with_ref_scan = ResolvedWithRefScanBuilder()
                           .set_column_list({renamed_measure_column})
                           .set_with_query_name("placeholder_with_query_name")
                           .Build();
  GOOGLESQL_CHECK_OK(with_ref_scan);
  GOOGLESQL_EXPECT_OK(
      measure_column_to_expr_mapping.TrackMeasureColumnsRenamedByWithRefScan(
          *with_ref_scan.value()));

  // Ensure that the measure expression can be looked up for the renamed column.
  EXPECT_THAT(
      measure_column_to_expr_mapping.GetMeasureExpr(renamed_measure_column),
      IsOkAndHolds(measure_expr));
}

}  // namespace
}  // namespace googlesql
