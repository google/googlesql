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

#include "googlesql/compliance/measure_test_tables.h"

#include <optional>
#include <utility>
#include <vector>

#include "googlesql/common/internal_value.h"
#include "googlesql/common/measure_analysis_utils.h"
#include "googlesql/compliance/test_driver.h"
#include "googlesql/public/value.h"
#include "googlesql/testing/test_value.h"

namespace googlesql {

TestDatabase GetMeasureTablesTestDatabase(bool add_measures_with_udas) {
  TestDatabase test_db;
  Value measure_table_single_key_as_value = test_values::StructArray(
      {"key", "country", "quantity", "price", "date_str", "nullable_str"},
      {{1ll, "USA", 5ll, 10ll, "Jan 2024", Value::NullString()},
       {2ll, "USA", 15ll, 20ll, "Jan 2024", "not_null"},
       {3ll, "USA", 30ll, 30ll, "Feb 2024", "not_null"},
       {4ll, "Canada", 20ll, 40ll, "Jan 2024", "not_null"},
       {5ll, "Canada", 20ll, 50ll, "Jan 2024", Value::NullString()},
       {6ll, "Canada", 35ll, 60ll, "Feb 2024", Value::NullString()},
       {7ll, "Canada", 25ll, 70ll, "Feb 2024", "not_null"},
       {8ll, "Mexico", 25ll, 80ll, "Jan 2024", "not_null"},
       {9ll, "Mexico", 25ll, 90ll, "Jan 2024", Value::NullString()},
       {10ll, "Mexico", 50ll, 100ll, "Feb 2024", "not_null"}},
      InternalValue::kIgnoresOrder);
  std::vector<MeasureColumnDef> measure_column_defs = {
      {"measure_sum_price", "SUM(price)"},
      {"measure_ratio_price_to_quantity", "SUM(price) / SUM(quantity)"},
      {"measure_country_count", "COUNT(* GROUP BY country)"},
      {"measure_avg_monthly_price", "AVG(SUM(price) GROUP BY date_str)"},
      {"measure_array_agg_distinct_country", "ARRAY_AGG(DISTINCT country)"},
      {"measure_array_agg_nullable_str_ignore_nulls",
       "ARRAY_AGG(nullable_str IGNORE NULLS)"},
      {"measure_array_agg_nullable_str_respect_nulls",
       "ARRAY_AGG(nullable_str RESPECT NULLS)"},
      {"measure_array_agg_country_limit_one", "ARRAY_AGG(country LIMIT 1)"},
      {"measure_array_agg_price_respect_nulls",
       "ARRAY_AGG(price RESPECT NULLS)"},
      {"measure_pseudo_column_sum_price", "SUM(price)",
       /*is_pseudo_column=*/true},
      {"measure_sum_price_via_subquery", "SUM((SELECT price))",
       /*is_pseudo_column=*/true},
      {"measure_sum_price_via_aggregate_subquery",
       "SUM((SELECT SUM(price) FROM UNNEST([1])))",
       /*is_pseudo_column=*/true},
      {"measure_sum_price_plus_one_via_subquery",
       "SUM(price) + (SELECT SUM(1) FROM UNNEST([1]))",
       /*is_pseudo_column=*/true},
      {"measure_with_deeply_nested_subquery",
       "SUM((SELECT x FROM (SELECT (SELECT SUM(price) FROM UNNEST([1])) AS "
       "x)))"},
      {"measure_literal_one", "1", /*is_pseudo_column=*/true},
      {"measure_one_plus_one", "1 + 1", /*is_pseudo_column=*/true},
      {"measure_scalar_subquery", "(SELECT SUM(x) FROM UNNEST([1, 2, 3]) AS x)",
       /*is_pseudo_column=*/true},
      {"measure_with_enum_in_function_signature", "BIT_XOR(quantity)",
       /*is_pseudo_column=*/true},
      {"measure_aggregation_in_in_expr", "SUM(price) IN ((SELECT 1))"},
  };
  if (add_measures_with_udas) {
    test_db.measure_function_defs = {
        "CREATE TEMP FUNCTION AddUdf(a INT64, b INT64) AS (a + b);",
        "CREATE TEMP AGGREGATE FUNCTION SumUda(a INT64) AS (SUM(a));",
        "CREATE TEMP AGGREGATE FUNCTION CountUda(a INT64) AS (COUNT(a));",
        "CREATE TEMP FUNCTION AddUdfPlusTen(a INT64, b INT64) AS "
        "(AddUdf(a, b) + 10);"};
    // Add measure columns that use the UDFs and UDAs.
    measure_column_defs.push_back({"measure_sum_price_plus_sum_quantity_udf",
                                   "AddUdf(SUM(price), SUM(quantity))"});
    measure_column_defs.push_back({"measure_sum_price_uda", "SumUda(price)"});
    measure_column_defs.push_back(
        {"measure_count_price_uda", "CountUda(price)"});
    measure_column_defs.push_back({"measure_sum_price_per_key_uda",
                                   "SumUda(ANY_VALUE(price) GROUP BY key)"});
    measure_column_defs.push_back({"measure_sum_count_price_per_country_uda",
                                   "SumUda(CountUda(price) GROUP BY country)"});
    measure_column_defs.push_back(
        {"measure_sum_price_plus_sum_quantity_plus_ten_udf",
         "AddUdfPlusTen(SUM(price), SUM(quantity))"});
    measure_column_defs.push_back({"measure_sum_max_price_per_country_uda",
                                   "SumUda(MAX(price) GROUP BY country)"});
    measure_column_defs.push_back({"measure_sum_price_pseudocolumn_uda",
                                   "SumUda(price)",
                                   /*is_pseudo_column=*/true});
    measure_column_defs.push_back(
        {"measure_sum_price_plus_one_pseudocolumn_udf", "AddUdf(SUM(price), 1)",
         /*is_pseudo_column=*/true});
  }
  std::vector<int> row_identity_columns = {0};
  TestTable measure_table_single_key = {
      .table_as_value = std::move(measure_table_single_key_as_value),
      .measure_column_defs = std::move(measure_column_defs),
      .row_identity_columns = std::move(row_identity_columns)};
  test_db.tables.insert({"MeasureTable_SingleKey", measure_table_single_key});

  // Add a table that represents a joint structure of multiple tables, where
  // measure columns may have column-level row identity columns.
  constexpr int kStoreIdIndex = 1;
  constexpr int kProductIdIndex = 2;
  constexpr int kSaleDateIndex = 3;
  Value sales_denormalized_as_value = test_values::StructArray(
      {
          // The compliance test framework requires the first column to be
          // a primary key, so we include a unique `row_id` column here.
          "row_id",
          // (store_id, product_id, sale_date) forms the primary key of the
          // table, although it is not enforced by the compliance test
          // framework.
          "store_id",
          "product_id",
          "sale_date",
          // The revenue for a (store_id, product_id, sale_date).
          "revenue",
          // The number of units sold for a (store_id, product_id,
          // sale_date).
          "units_sold",
          // Determined only by product_id.
          "product_category",
          // Determined only by store_id.
          "store_region",
          // Promo code for a (store_id, product_id, sale_date).
          "promo_code",
      },
      {
          // Row 1.
          {
              1ll,                  // row_id
              1ll,                  // store_id
              101ll,                // product_id
              "2025-01-01",         // sale_date
              1000ll,               // revenue
              10ll,                 // units_sold
              "Electronics",        // product_category
              "North",              // store_region
              Value::NullString(),  // promo_code
          },
          // Row 2: Shares the same `store_id` as Row 1. `measure_regions`
          // will treat Row 1 and Row 2 as the same row.
          {
              2ll,           // row_id
              1ll,           // store_id
              102ll,         // product_id
              "2025-01-01",  // sale_date
              500ll,         // revenue
              20ll,          // units_sold
              "Groceries",   // product_category
              "North",       // store_region
              "SAVE10",      // promo_code
          },
          // Row 3: Shares the same `product_id` as Row 1.
          // `measure_categories` will treat Row 1 and Row 3 as the same row.
          {
              3ll,                  // row_id
              2ll,                  // store_id
              101ll,                // product_id
              "2025-01-01",         // sale_date
              1500ll,               // revenue
              15ll,                 // units_sold
              "Electronics",        // product_category
              "South",              // store_region
              Value::NullString(),  // promo_code
          },
          // Row 4: Shares the same `store_id` and `product_id` as Row 1 but
          // with a different `sale_date`.
          // `measure_category_and_region_combination` will treat Row 1 and
          // Row 4 as the same row.
          {
              4ll,            // row_id
              1ll,            // store_id
              101ll,          // product_id
              "2025-01-02",   // sale_date
              1200ll,         // revenue
              12ll,           // units_sold
              "Electronics",  // product_category
              "North",        // store_region
              "SAVE15",       // promo_code
          },
      },
      InternalValue::kIgnoresOrder);

  std::vector<MeasureColumnDef> sales_denormalized_measure_defs = {
      // Default to the table-level grain locking keys.
      {.name = "measure_total_revenue", .expression = "SUM(revenue)"},

      // Measures demonstrating grain locking on column-level row identity
      // columns. These measures aggregate attributes that depend on a subset
      // of table row identity columns (e.g. product_category depends on
      // product_id).
      {
          .name = "measure_categories",
          .expression = "ARRAY_AGG(product_category)",
          .row_identity_column_indices = std::vector<int>{kProductIdIndex},
      },
      {
          .name = "measure_regions",
          .expression = "ARRAY_AGG(store_region)",
          .row_identity_column_indices = std::vector<int>{kStoreIdIndex},
      },
      {
          .name = "measure_category_and_region_combination",
          .expression =
              "ARRAY_AGG(CONCAT(product_category, ', ', store_region))",
          .row_identity_column_indices =
              std::vector<int>{kStoreIdIndex, kProductIdIndex},
      },

      // Measure column with its own grain locking key but happens to be the
      // same as that of the table.
      {
          .name = "measure_total_units_sold",
          .expression = "SUM(units_sold)",
          .row_identity_column_indices =
              std::vector<int>{kStoreIdIndex, kProductIdIndex, kSaleDateIndex},
      },

      // Measures for testing nullability
      {
          .name = "measure_promo_codes_ignore_nulls",
          .expression = "ARRAY_AGG(promo_code IGNORE NULLS)",
          .row_identity_column_indices =
              std::vector<int>{kStoreIdIndex, kProductIdIndex, kSaleDateIndex},
      },
      {
          .name = "measure_promo_codes_respect_nulls",
          .expression = "ARRAY_AGG(promo_code RESPECT NULLS)",
          // Tests aggregation on a nullable column with table-level grain.
          .row_identity_column_indices = std::nullopt,
      },

      // Pseudo-column definitions + multi-level aggregates.
      {
          .name = "measure_product_categories_per_region",
          .expression = R"sql(
            ARRAY_AGG(
              STRUCT(
                ARRAY_AGG(product_category) AS product_categories,
                ANY_VALUE(store_region) AS store_region
              )
              GROUP BY store_region
            )
          )sql",
          .is_pseudo_column = true,
          .row_identity_column_indices =
              std::vector<int>{kStoreIdIndex, kProductIdIndex},
      },
  };

  std::vector<int> sales_denormalized_row_identity_columns = {
      kStoreIdIndex, kProductIdIndex, kSaleDateIndex};
  TestTable measure_table_sales_denormalized = {
      .table_as_value = std::move(sales_denormalized_as_value),
      .measure_column_defs = std::move(sales_denormalized_measure_defs),
      .row_identity_columns =
          std::move(sales_denormalized_row_identity_columns)};
  test_db.tables.insert(
      {"MeasureTable_Sales_Denormalized", measure_table_sales_denormalized});

  // We add another TestTable where all measure columns have row identity
  // columns but the table does not.
  Value measure_table_no_table_row_identity_columns_as_value =
      test_values::StructArray(
          {"row_id", "product_id", "product_name", "revenue"},
          {{1ll, 101ll, "apple", 100ll},
           {2ll, 102ll, "banana", 200ll},
           {3ll, 101ll, "apple", 150ll}},
          InternalValue::kIgnoresOrder);
  std::vector<MeasureColumnDef>
      measure_table_no_table_row_identity_columns_measure_defs = {
          {.name = "measure_all_product_names",
           .expression = "ARRAY_AGG(product_name)",
           .row_identity_column_indices = std::vector<int>{1}}};
  TestTable measure_table_no_table_row_identity_columns = {
      .table_as_value =
          std::move(measure_table_no_table_row_identity_columns_as_value),
      .measure_column_defs =
          std::move(measure_table_no_table_row_identity_columns_measure_defs),
  };
  test_db.tables.insert({"MeasureTable_NoTableRowIdentityColumns",
                         measure_table_no_table_row_identity_columns});
  return test_db;
}
}  // namespace googlesql
