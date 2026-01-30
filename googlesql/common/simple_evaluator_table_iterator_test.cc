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

#include "googlesql/common/simple_evaluator_table_iterator.h"

#include <algorithm>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/simple_catalog.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/memory/memory.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "googlesql/base/status_macros.h"
#include "googlesql/base/clock.h"

namespace googlesql {
namespace {

using testing::ElementsAre;
using testing::IsEmpty;
using absl_testing::IsOkAndHolds;

using types::Int64Type;

using values::Int64;

// Fixture for tests of SimpleEvaluatorTableIterator::SetColumnFilters().
class ColumnFilterTest : public ::testing::Test {
 protected:
  void SetUp() override {
    for (int i = 1; i <= 3; ++i) {
      columns_.push_back(std::make_unique<SimpleColumn>(
          "TestTable", absl::StrCat("column", i), Int64Type()));
    }

    ResetIter(/*filter_column_idxs=*/{0, 1, 2});
  }

  void ResetIter(const absl::flat_hash_set<int> filter_column_idxs) {
    std::vector<const Column*> columns;
    columns.reserve(columns_.size());
    for (const std::unique_ptr<Column>& column : columns_) {
      columns.push_back(column.get());
    }

    const std::vector<std::vector<Value>> column_major_values = {
        {Int64(1), Int64(2), Int64(3), Int64(4)},
        {Int64(10), Int64(20), Int64(30), Int64(40)},
        {Int64(100), Int64(200), Int64(300), Int64(400)}};

    std::vector<std::shared_ptr<const std::vector<Value>>>
        column_major_values_for_iter;
    column_major_values_for_iter.reserve(column_major_values.size());
    for (const std::vector<Value>& values : column_major_values) {
      column_major_values_for_iter.push_back(
          std::make_shared<const std::vector<Value>>(values));
    }

    iter_ = std::make_unique<SimpleEvaluatorTableIterator>(
        columns, column_major_values_for_iter, /*num_rows=*/4,
        /*end_status=*/absl::OkStatus(), filter_column_idxs,
        /*cancel_cb=*/[]() {}, /*set_deadline_cb=*/[](absl::Time) {},
        googlesql_base::Clock::RealClock());
  }

  absl::StatusOr<std::vector<std::vector<Value>>> Read(
      absl::flat_hash_map<int, std::unique_ptr<ColumnFilter>> filter_map) {
    GOOGLESQL_RETURN_IF_ERROR(iter_->SetColumnFilterMap(std::move(filter_map)));

    std::vector<std::vector<Value>> rows;
    while (true) {
      if (!iter_->NextRow()) {
        GOOGLESQL_RETURN_IF_ERROR(iter_->Status());
        return rows;
      }

      std::vector<Value> row;
      row.reserve(iter_->NumColumns());
      for (int i = 0; i < iter_->NumColumns(); ++i) {
        row.push_back(iter_->GetValue(i));
      }
      rows.push_back(std::move(row));
    }
  }

 protected:
  std::vector<std::unique_ptr<Column>> columns_;
  std::unique_ptr<SimpleEvaluatorTableIterator> iter_;
};

TEST_F(ColumnFilterTest, NoFilters) {
  EXPECT_THAT(
      Read(/*filter_map=*/{}),
      IsOkAndHolds(ElementsAre(ElementsAre(Int64(1), Int64(10), Int64(100)),
                               ElementsAre(Int64(2), Int64(20), Int64(200)),
                               ElementsAre(Int64(3), Int64(30), Int64(300)),
                               ElementsAre(Int64(4), Int64(40), Int64(400)))));
}

TEST_F(ColumnFilterTest, OneAllFilter) {
  absl::flat_hash_map<int, std::unique_ptr<ColumnFilter>> filter_map;
  filter_map.emplace(1, std::make_unique<ColumnFilter>(Value(), Value()));

  EXPECT_THAT(
      Read(std::move(filter_map)),
      IsOkAndHolds(ElementsAre(ElementsAre(Int64(1), Int64(10), Int64(100)),
                               ElementsAre(Int64(2), Int64(20), Int64(200)),
                               ElementsAre(Int64(3), Int64(30), Int64(300)),
                               ElementsAre(Int64(4), Int64(40), Int64(400)))));
}

TEST_F(ColumnFilterTest, OneLeFilter) {
  absl::flat_hash_map<int, std::unique_ptr<ColumnFilter>> filter_map;
  filter_map.emplace(1, std::make_unique<ColumnFilter>(Value(), Int64(20)));

  EXPECT_THAT(
      Read(std::move(filter_map)),
      IsOkAndHolds(ElementsAre(ElementsAre(Int64(1), Int64(10), Int64(100)),
                               ElementsAre(Int64(2), Int64(20), Int64(200)))));
}

TEST_F(ColumnFilterTest, OneGeFilter) {
  absl::flat_hash_map<int, std::unique_ptr<ColumnFilter>> filter_map;
  filter_map.emplace(1, std::make_unique<ColumnFilter>(Int64(30), Value()));

  EXPECT_THAT(
      Read(std::move(filter_map)),
      IsOkAndHolds(ElementsAre(ElementsAre(Int64(3), Int64(30), Int64(300)),
                               ElementsAre(Int64(4), Int64(40), Int64(400)))));
}

TEST_F(ColumnFilterTest, OneLeAndGeFilter) {
  absl::flat_hash_map<int, std::unique_ptr<ColumnFilter>> filter_map;
  filter_map.emplace(1, std::make_unique<ColumnFilter>(Int64(20), Int64(30)));

  EXPECT_THAT(
      Read(std::move(filter_map)),
      IsOkAndHolds(ElementsAre(ElementsAre(Int64(2), Int64(20), Int64(200)),
                               ElementsAre(Int64(3), Int64(30), Int64(300)))));
}

TEST_F(ColumnFilterTest, OneEmptyInFilter) {
  absl::flat_hash_map<int, std::unique_ptr<ColumnFilter>> filter_map;
  filter_map.emplace(1, std::make_unique<ColumnFilter>(std::vector<Value>{}));

  EXPECT_THAT(Read(std::move(filter_map)), IsOkAndHolds(IsEmpty()));
}

TEST_F(ColumnFilterTest, OneInFilter) {
  absl::flat_hash_map<int, std::unique_ptr<ColumnFilter>> filter_map;
  filter_map.emplace(1, std::make_unique<ColumnFilter>(
                            std::vector<Value>{Int64(20), Int64(40)}));

  EXPECT_THAT(
      Read(std::move(filter_map)),
      IsOkAndHolds(ElementsAre(ElementsAre(Int64(2), Int64(20), Int64(200)),
                               ElementsAre(Int64(4), Int64(40), Int64(400)))));
}

TEST_F(ColumnFilterTest, OverlappingDeletionsInThreeColumns) {
  absl::flat_hash_map<int, std::unique_ptr<ColumnFilter>> filter_map;
  filter_map.emplace(0, std::make_unique<ColumnFilter>(Int64(3), Value()));
  filter_map.emplace(1, std::make_unique<ColumnFilter>(
                            std::vector<Value>{Int64(10), Int64(30)}));
  filter_map.emplace(2, std::make_unique<ColumnFilter>(Value(), Int64(300)));

  EXPECT_THAT(
      Read(std::move(filter_map)),
      IsOkAndHolds(ElementsAre(ElementsAre(Int64(3), Int64(30), Int64(300)))));
}

TEST_F(ColumnFilterTest, DeletionsInThreeColumnsOnlyRespectTwo) {
  absl::flat_hash_map<int, std::unique_ptr<ColumnFilter>> filter_map;
  filter_map.emplace(0, std::make_unique<ColumnFilter>(Int64(3), Value()));
  filter_map.emplace(2, std::make_unique<ColumnFilter>(Int64(0), Value()));

  // Both filters drop everything, but if we only filter on the first column, we
  // only drop some of the rows.
  ResetIter(/*filter_column_idxs=*/{0});
  EXPECT_THAT(
      Read(std::move(filter_map)),
      IsOkAndHolds(ElementsAre(ElementsAre(Int64(3), Int64(30), Int64(300)),
                               ElementsAre(Int64(4), Int64(40), Int64(400)))));
}

}  // namespace
}  // namespace googlesql
