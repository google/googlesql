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

#include "googlesql/reference_impl/variable_generator.h"

#include <map>
#include <memory>
#include <utility>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/type.h"
#include "googlesql/reference_impl/parameters.h"
#include "googlesql/reference_impl/variable_id.h"
#include "googlesql/testing/using_test_value.cc"  // NOLINT
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;

namespace googlesql {
namespace {

TEST(VariableGenerator, GetNewVariableName_EmptySuggestedName) {
  VariableGenerator gen;
  // Feeding in an empty suggested name twice yields different variables.
  EXPECT_EQ(gen.GetNewVariableName(""), VariableId("_"));
  EXPECT_EQ(gen.GetNewVariableName(""), VariableId("_.2"));

  // Attempting to reuse the generated variable names results in different
  // variables.
  EXPECT_EQ(gen.GetNewVariableName("_"), VariableId("_.3"));
  // The output is a little strange if the suggested names are subscripted. We
  // still get a new variable name, though.
  EXPECT_EQ(gen.GetNewVariableName("_.3"), VariableId("__3"));
  EXPECT_EQ(gen.GetNewVariableName("__3"), VariableId("__3.2"));
}

TEST(VariableGenerator, GetNewVariableName_NonEmptySuggestedName) {
  VariableGenerator gen;
  EXPECT_EQ(gen.GetNewVariableName("x"), VariableId("x"));
  EXPECT_EQ(gen.GetNewVariableName("x"), VariableId("x.2"));
  EXPECT_EQ(gen.GetNewVariableName("x"), VariableId("x.3"));

  // The output is a little strange if the suggested names are subscripted. We
  // still get a new variable name, though.
  EXPECT_EQ(gen.GetNewVariableName("x.3"), VariableId("x_3"));
  EXPECT_EQ(gen.GetNewVariableName("x_3"), VariableId("x_3.2"));
  EXPECT_EQ(gen.GetNewVariableName("x_3"), VariableId("x_3.3"));
}

TEST(VariableGenerator, GetNewVariableName_SuggestedNameWithDollarSign) {
  VariableGenerator gen;
  EXPECT_EQ(gen.GetNewVariableName("$"), VariableId("_"));
  EXPECT_EQ(gen.GetNewVariableName("$"), VariableId("_.2"));

  EXPECT_EQ(gen.GetNewVariableName("$x$"), VariableId("x"));
  EXPECT_EQ(gen.GetNewVariableName("$x$"), VariableId("x.2"));
  EXPECT_EQ(gen.GetNewVariableName("$x$"), VariableId("x.3"));

  EXPECT_EQ(gen.GetNewVariableName("$x$.$3$"), VariableId("x_3"));
}

TEST(VariableGenerator, GetNewVariableName_SuggestedNameWithAtSign) {
  VariableGenerator gen;
  EXPECT_EQ(gen.GetNewVariableName("@"), VariableId("_"));
  EXPECT_EQ(gen.GetNewVariableName("@"), VariableId("_.2"));

  EXPECT_EQ(gen.GetNewVariableName("@x@"), VariableId("x"));
  EXPECT_EQ(gen.GetNewVariableName("@x@"), VariableId("x.2"));
  EXPECT_EQ(gen.GetNewVariableName("@x@"), VariableId("x.3"));

  EXPECT_EQ(gen.GetNewVariableName("@x@.@3@"), VariableId("x_3"));
}

TEST(VariableGenerator, GetVariableNameFromParameterMap_MissingEntry) {
  VariableGenerator gen;
  ParameterMap map;
  ParameterMap expected_map;

  // "foo" is not already a variable name.
  EXPECT_EQ(gen.GetVariableNameFromParameter("foo", &map), VariableId("foo"));
  expected_map["foo"] = VariableId("foo");
  EXPECT_EQ(expected_map, map);

  // Create a variable named "bar".
  EXPECT_EQ(gen.GetNewVariableName("bar"), VariableId("bar"));

  // "bar" is already a variable name.
  EXPECT_EQ(gen.GetVariableNameFromParameter("bar", &map), VariableId("bar.2"));
  expected_map["bar"] = VariableId("bar.2");
  EXPECT_EQ(expected_map, map);
}

TEST(VariableGenerator, GetVariableNameFromParameterMap_FoundEntry) {
  VariableGenerator gen;
  ParameterMap map;
  ParameterMap expected_map;

  EXPECT_EQ(gen.GetVariableNameFromParameter("x", &map), VariableId("x"));
  expected_map["x"] = VariableId("x");
  EXPECT_EQ(expected_map, map);

  EXPECT_EQ(gen.GetVariableNameFromParameter("x", &map), VariableId("x"));
  EXPECT_EQ(expected_map, map);
}

TEST(VariableGenerator, GetVariableNameFromParameterList_Valid) {
  VariableGenerator gen;
  const ParameterList expected_list = {VariableId("foo"), VariableId("bar"),
                                       VariableId("baz")};
  ParameterList list = expected_list;

  // Parameter positions are 1-based.
  EXPECT_EQ(gen.GetVariableNameFromParameter(/*parameter_position=*/2, &list),
            VariableId("bar"));
  EXPECT_EQ(expected_list, list);
}

TEST(VariableGenerator, GetVariableNameFromParameterList_Gap) {
  VariableGenerator gen;
  ParameterList list = {VariableId("foo"), VariableId(), VariableId("bar")};

  // Parameter positions are 1-based.
  EXPECT_EQ(gen.GetVariableNameFromParameter(/*parameter_position=*/2, &list),
            VariableId("positional_param_2"));
  EXPECT_THAT(list,
              ElementsAre(VariableId("foo"), VariableId("positional_param_2"),
                          VariableId("bar")));
}

TEST(VariableGenerator, GetVariableNameFromParameterList_JustAfterEnd) {
  VariableGenerator gen;
  ParameterList list;

  EXPECT_EQ(gen.GetVariableNameFromParameter(/*parameter_position=*/1, &list),
            VariableId("positional_param_1"));
  EXPECT_THAT(list, ElementsAre(VariableId("positional_param_1")));

  EXPECT_EQ(gen.GetVariableNameFromParameter(/*parameter_position=*/2, &list),
            VariableId("positional_param_2"));
  EXPECT_THAT(list, ElementsAre(VariableId("positional_param_1"),
                                VariableId("positional_param_2")));
}

TEST(VariableGenerator, GetVariableNameFromParameterList_WayAfterEnd) {
  VariableGenerator gen;
  ParameterList list;

  EXPECT_EQ(gen.GetVariableNameFromParameter(/*parameter_position=*/2, &list),
            VariableId("positional_param_2"));
  EXPECT_THAT(list,
              ElementsAre(VariableId(), VariableId("positional_param_2")));
}

const char kTableName1[] = "Table1";
const char kTableName2[] = "Table2";

const int kColumnId1 = 1;
const int kColumnId2 = 2;
const int kColumnId3 = 3;

const char kColumnName1[] = "column_1";
const char kColumnName2[] = "column_2";

class ColumnToVariableMappingTest : public ::testing::Test {
 protected:
  const ResolvedColumn column_1_ = ResolvedColumn(
      kColumnId1, googlesql::IdString::MakeGlobal(kTableName1),
      googlesql::IdString::MakeGlobal(kColumnName1), Int64Type());
  const ResolvedColumn column_2_ = ResolvedColumn(
      kColumnId2, googlesql::IdString::MakeGlobal(kTableName1),
      googlesql::IdString::MakeGlobal(kColumnName2), Int64Type());
  // Column 3 has the same name as column 1, but is in a different table.
  const ResolvedColumn column_3_ = ResolvedColumn(
      kColumnId3, googlesql::IdString::MakeGlobal(kTableName2),
      googlesql::IdString::MakeGlobal(kColumnName1), Int64Type());

  const VariableId variable_1_ = VariableId(kColumnName1);
  const VariableId variable_2_ = VariableId(kColumnName2);
  const VariableId variable_3_ = VariableId(absl::StrCat(kColumnName1, ".2"));
};

TEST_F(ColumnToVariableMappingTest, Basic) {
  auto gen_owner = std::make_unique<VariableGenerator>();
  VariableGenerator* gen = gen_owner.get();
  ColumnToVariableMapping mapping(std::move(gen_owner));
  EXPECT_EQ(gen, mapping.variable_generator());

  // The mapping is initially empty.
  ColumnToVariableMapping::Map expected_map;
  EXPECT_EQ(expected_map, mapping.map());

  // 'column_1_' is not in the mapping.
  EXPECT_THAT(mapping.LookupVariableNameForColumn(column_1_),
              StatusIs(absl::StatusCode::kNotFound, HasSubstr(kColumnName1)));

  // Insert 'column_1_' into the mapping and verify that it's there.
  EXPECT_EQ(mapping.AssignNewVariableToColumn(column_1_), variable_1_);
  expected_map[column_1_] = variable_1_;
  EXPECT_EQ(expected_map, mapping.map());
  EXPECT_THAT(mapping.LookupVariableNameForColumn(column_1_),
              IsOkAndHolds(variable_1_));

  // 'column_2_' is not in the mapping.
  EXPECT_THAT(mapping.LookupVariableNameForColumn(column_2_),
              StatusIs(absl::StatusCode::kNotFound, HasSubstr(kColumnName2)));

  // Insert 'column_2_' into the mapping and verify that both columns are there.
  EXPECT_EQ(mapping.AssignNewVariableToColumn(column_2_), variable_2_);
  expected_map[column_2_] = variable_2_;
  EXPECT_EQ(expected_map, mapping.map());
  EXPECT_THAT(mapping.LookupVariableNameForColumn(column_1_),
              IsOkAndHolds(variable_1_));
  EXPECT_THAT(mapping.LookupVariableNameForColumn(column_2_),
              IsOkAndHolds(variable_2_));
}

// Tests ColumnToVariableMapping::GetVariableNameFromColumn(), which is the same
// as LookupVariableNameForColumn() except that it assigns a new variable if the
// column is missing from the mapping.
TEST_F(ColumnToVariableMappingTest, GetVariableNameFromColumn) {
  ColumnToVariableMapping mapping(std::make_unique<VariableGenerator>());
  ColumnToVariableMapping::Map expected_map;
  EXPECT_EQ(expected_map, mapping.map());

  EXPECT_EQ(mapping.GetVariableNameFromColumn(column_1_), variable_1_);
  expected_map[column_1_] = variable_1_;
  EXPECT_EQ(expected_map, mapping.map());

  EXPECT_EQ(mapping.GetVariableNameFromColumn(column_2_), variable_2_);
  expected_map[column_2_] = variable_2_;
  EXPECT_EQ(expected_map, mapping.map());

  // 'column_3' has the same name as 'column_1', but it is in a different table.
  EXPECT_EQ(mapping.GetVariableNameFromColumn(column_3_), variable_3_);
  expected_map[column_3_] = variable_3_;
  EXPECT_EQ(expected_map, mapping.map());
}

TEST_F(ColumnToVariableMappingTest, SetMap) {
  ColumnToVariableMapping mapping(std::make_unique<VariableGenerator>());
  ColumnToVariableMapping::Map expected_map;

  EXPECT_EQ(expected_map, mapping.map());
  EXPECT_THAT(mapping.LookupVariableNameForColumn(column_1_),
              StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(mapping.LookupVariableNameForColumn(column_2_),
              StatusIs(absl::StatusCode::kNotFound));

  expected_map[column_1_] = variable_1_;
  expected_map[column_2_] = variable_2_;

  mapping.set_map(expected_map);
  EXPECT_EQ(expected_map, mapping.map());
  EXPECT_THAT(mapping.LookupVariableNameForColumn(column_1_),
              IsOkAndHolds(variable_1_));
  EXPECT_THAT(mapping.LookupVariableNameForColumn(column_2_),
              IsOkAndHolds(variable_2_));
}

}  // namespace
}  // namespace googlesql
