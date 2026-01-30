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

#include "googlesql/resolved_ast/column_factory.h"

#include <memory>
#include <string>

#include "googlesql/base/atomic_sequence_num.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/collation.h"
#include "googlesql/public/types/simple_value.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace googlesql {
namespace {

TEST(ColumnFactory, NoSequence) {
  ColumnFactory factory(10);
  ResolvedColumn column =
      factory.MakeCol("table", "column", types::StringType());

  EXPECT_EQ(column.column_id(), 11);
  EXPECT_EQ(column.type(), types::StringType());
  EXPECT_EQ(column.table_name(), "table");
  EXPECT_EQ(column.name(), "column");

  EXPECT_EQ(factory.max_column_id(), 11);
}

TEST(ColumnFactory, NoSequenceAnnotated) {
  ColumnFactory factory(10);
  ResolvedColumn column =
      factory.MakeCol("table", "column", {types::StringType(), nullptr});

  EXPECT_EQ(column.column_id(), 11);
  EXPECT_EQ(column.type(), types::StringType());
  EXPECT_EQ(column.table_name(), "table");
  EXPECT_EQ(column.name(), "column");

  EXPECT_EQ(factory.max_column_id(), 11);
}

TEST(ColumnFactory, WithSequenceBehind) {
  googlesql_base::SequenceNumber sequence;
  ColumnFactory factory(5, &sequence);
  ResolvedColumn column =
      factory.MakeCol("table", "column", types::Int32Type());

  EXPECT_EQ(column.column_id(), 6);
  EXPECT_EQ(column.type(), types::Int32Type());
  EXPECT_EQ(column.table_name(), "table");
  EXPECT_EQ(column.name(), "column");

  // Sequence should have been used.
  EXPECT_EQ(7, sequence.GetNext());
  EXPECT_EQ(factory.max_column_id(), 6);
}

TEST(ColumnFactory, WithSequenceBehindAnnotated) {
  googlesql_base::SequenceNumber sequence;
  ColumnFactory factory(5, &sequence);
  ResolvedColumn column =
      factory.MakeCol("table", "column", {types::Int32Type(), nullptr});

  EXPECT_EQ(column.column_id(), 6);
  EXPECT_EQ(column.type(), types::Int32Type());
  EXPECT_EQ(column.table_name(), "table");
  EXPECT_EQ(column.name(), "column");

  // Sequence should have been used.
  EXPECT_EQ(7, sequence.GetNext());
  EXPECT_EQ(factory.max_column_id(), 6);
}

TEST(ColumnFactory, WithSequenceAhead) {
  googlesql_base::SequenceNumber sequence;
  for (int i = 0; i < 10; ++i) {
    sequence.GetNext();
  }

  ColumnFactory factory(0, &sequence);
  ResolvedColumn column =
      factory.MakeCol("table", "column", types::Int32Type());

  // Should be well past the max column seen passed in of 0.
  EXPECT_EQ(column.column_id(), 10);
  EXPECT_EQ(column.type(), types::Int32Type());
  EXPECT_EQ(column.table_name(), "table");
  EXPECT_EQ(column.name(), "column");

  // Should still get the right max_column_id.
  EXPECT_EQ(11, sequence.GetNext());
  EXPECT_EQ(factory.max_column_id(), 10);
}

TEST(ColumnFactory, WithSequenceAheadAnnotated) {
  googlesql_base::SequenceNumber sequence;
  for (int i = 0; i < 10; ++i) {
    sequence.GetNext();
  }

  ColumnFactory factory(0, &sequence);
  ResolvedColumn column =
      factory.MakeCol("table", "column", {types::Int32Type(), nullptr});

  // Should be well past the max column seen passed in of 0.
  EXPECT_EQ(column.column_id(), 10);
  EXPECT_EQ(column.type(), types::Int32Type());
  EXPECT_EQ(column.table_name(), "table");
  EXPECT_EQ(column.name(), "column");

  // Should still get the right max_column_id.
  EXPECT_EQ(11, sequence.GetNext());
  EXPECT_EQ(factory.max_column_id(), 10);
}

TEST(ColumnFactory, ColumnCollationTest) {
  googlesql_base::SequenceNumber sequence;
  ColumnFactory column_factory(0, &sequence);

  std::unique_ptr<AnnotationMap> annotation_map =
      AnnotationMap::Create(types::StringType());
  annotation_map->SetAnnotation(static_cast<int>(AnnotationKind::kCollation),
                                SimpleValue::String("und:ci"));

  ResolvedColumn collate_column = column_factory.MakeCol(
      "test", "collate", {types::StringType(), annotation_map.get()});
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Collation collation,
                       Collation::MakeCollation(*annotation_map));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Collation column_collation,
      Collation::MakeCollation(*collate_column.type_annotation_map()));

  ASSERT_TRUE(collation.Equals(column_collation));
}

}  // namespace
}  // namespace googlesql
