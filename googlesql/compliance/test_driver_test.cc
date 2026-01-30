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

#include "googlesql/compliance/test_driver.h"

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "googlesql/common/measure_analysis_utils.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/compliance/test_driver.pb.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/array_type.h"
#include "googlesql/public/types/proto_type.h"
#include "googlesql/public/types/simple_value.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "googlesql/base/check.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"

namespace googlesql {

using ::googlesql_test::KitchenSinkPB;
using ::testing::ElementsAre;

TEST(TestDriverTest, ClassAndProtoSize) {
  // We're forced to replicate the exact structure for this test because the
  // presence of bools makes computing an 'expected'
  // (sizeof(X) == sizeof(X.field1) + sizeof(X.field2) +...) weird due
  // to alignment (bool will take 64 or 32 bytes in this case).
  struct MockTestDatabase {
    std::set<std::string> proto_files;
    bool runs_as_test;
    std::set<std::string> proto_names;
    std::set<std::string> enum_names;
    std::map<std::string, TestTable> tables;
    std::map<std::string, std::string> graph_defs;
    std::map<std::string, std::string> tvfs;
    std::set<std::string> measure_function_defs;
  };
  struct MockTestTestTableOptions {
    int expected_table_size_min;
    int expected_table_size_max;
    bool is_value_table;
    double nullable_probability;
    std::set<LanguageFeature> required_features;
    std::string userid_column;
  };
  struct MockTestTable {
    Value table_as_value;
    MockTestTestTableOptions options;
  };
  static_assert(sizeof(TestDatabase) == sizeof(MockTestDatabase),
                "Please change SerializeTestDatabase (test_driver.cc) and "
                "TestDatabaseProto (test_driver.proto) tests if TestDatabase "
                "is modified.");
  struct MockMeasureColumnDef {
    std::string name;
    std::string expression;
    bool is_pseudo_column;
    std::optional<std::vector<int>> row_identity_column_indices;
  };
  static_assert(sizeof(MeasureColumnDef) == sizeof(MockMeasureColumnDef),
                "Please change SerializeTestDatabase (test_driver.cc) and "
                "TestDatabaseProto (test_driver.proto) tests if "
                "MeasureColumnDef is modified.");
  EXPECT_EQ(TestDatabaseProto::descriptor()->field_count(), 8);
  EXPECT_EQ(7, TestTableOptionsProto::descriptor()->field_count());
  EXPECT_EQ(5, TestTableProto::descriptor()->field_count());
  EXPECT_EQ(4, MeasureColumnDefProto::descriptor()->field_count());
}

Value KitchenSinkPBValue(const ProtoType* proto_type,
                         const KitchenSinkPB proto) {
  absl::Cord bytes;
  ABSL_CHECK(proto.SerializeToCord(&bytes));
  return Value::Proto(proto_type, bytes);
}

TEST(TestDriverTest, SerializeDeserializeTestDbWithProtos) {
  TypeFactory type_factory;

  const ProtoType* proto_type;
  GOOGLESQL_ASSERT_OK(
      type_factory.MakeProtoType(KitchenSinkPB::descriptor(), &proto_type));

  const StructType* struct_proto_type;
  GOOGLESQL_ASSERT_OK(
      type_factory.MakeStructType({{"a", proto_type}}, &struct_proto_type));

  const ArrayType* array_struct_proto_type;
  GOOGLESQL_ASSERT_OK(
      type_factory.MakeArrayType(struct_proto_type, &array_struct_proto_type));

  KitchenSinkPB pb;
  pb.set_int64_key_1(3);
  pb.set_int64_key_2(4);
  pb.set_string_val("abcde");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value row1,
                       Value::MakeStruct(struct_proto_type,
                                         {KitchenSinkPBValue(proto_type, pb)}));
  pb.set_string_val("defgh");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value row2,
                       Value::MakeStruct(struct_proto_type,
                                         {KitchenSinkPBValue(proto_type, pb)}));
  TestTable t;
  t.table_as_value = Value::Array(array_struct_proto_type, {row1, row2});
  t.measure_column_defs.push_back(
      {.name = "measure1",
       .expression = "COUNT(1)",
       .is_pseudo_column = false,
       .row_identity_column_indices = std::vector<int>{1, 2}});
  t.measure_column_defs.push_back(
      {.name = "measure2",
       .expression = "SUM(a)",
       .is_pseudo_column = true,
       .row_identity_column_indices = std::nullopt});
  TestDatabase test_db;
  test_db.tables["t"] = t;
  test_db.measure_function_defs.push_back("CREATE MEASURE m1 AS (1)");

  TestDatabaseProto test_db_proto;
  GOOGLESQL_ASSERT_OK(SerializeTestDatabase(test_db, &test_db_proto));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      TestDatabase deserialized_db,
      DeserializeTestDatabase(test_db_proto, &type_factory,
                              ::google::protobuf::DescriptorPool::generated_pool()));
  ASSERT_EQ(deserialized_db.tables["t"].table_as_value.DebugString(),
            test_db.tables["t"].table_as_value.DebugString())
      << "Table differs after round-trip serialization/deserialization";

  ASSERT_EQ(deserialized_db.tables["t"].measure_column_defs.size(), 2);
  const auto& m1 = deserialized_db.tables["t"].measure_column_defs[0];
  EXPECT_EQ(m1.name, "measure1");
  EXPECT_EQ(m1.expression, "COUNT(1)");
  EXPECT_FALSE(m1.is_pseudo_column);
  ASSERT_TRUE(m1.row_identity_column_indices.has_value());
  EXPECT_THAT(*m1.row_identity_column_indices, ElementsAre(1, 2));

  const auto& m2 = deserialized_db.tables["t"].measure_column_defs[1];
  EXPECT_EQ(m2.name, "measure2");
  EXPECT_EQ(m2.expression, "SUM(a)");
  EXPECT_TRUE(m2.is_pseudo_column);
  EXPECT_FALSE(m2.row_identity_column_indices.has_value());
}

TEST(TestDriverTest, SerializeDeserializeTableOptions) {
  TestDatabase test_db;
  TestTable& t = test_db.tables["t"];
  t.options.set_expected_table_size_range(3, 8);
  t.options.set_is_value_table(true);
  t.options.mutable_required_features()->insert(
      FEATURE_ALTER_TABLE_RENAME_COLUMN);
  t.options.set_userid_column("abc");
  t.options.set_nullable_probability(0.12345);

  TypeFactory type_factory;
  std::unique_ptr<AnnotationMap> annotation_map1 =
      AnnotationMap::Create(type_factory.get_int64());
  annotation_map1->SetAnnotation(1, SimpleValue::Int64(1234));
  annotation_map1->SetAnnotation(2, SimpleValue::Int64(5678));
  t.options.set_column_annotations({annotation_map1.get()});

  const StructType* row_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeStructType(
      {{"a", type_factory.get_int64()}, {"b", type_factory.get_string()}},
      &row_type));
  const ArrayType* table_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeArrayType(row_type, &table_type));
  t.table_as_value = Value::Array(table_type, {});

  TestDatabaseProto test_db_proto;
  GOOGLESQL_ASSERT_OK(SerializeTestDatabase(test_db, &test_db_proto));

  SCOPED_TRACE(absl::StrCat(test_db_proto));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      TestDatabase deserialized_db,
      DeserializeTestDatabase(test_db_proto, &type_factory,
                              ::google::protobuf::DescriptorPool::generated_pool()));
  const TestTable& deserialized_t = deserialized_db.tables["t"];
  ASSERT_EQ(deserialized_t.options.expected_table_size_min(), 3);
  ASSERT_EQ(deserialized_t.options.expected_table_size_max(), 8);
  ASSERT_EQ(deserialized_t.options.is_value_table(), true);
  ASSERT_EQ(deserialized_t.options.required_features().size(), 1);
  ASSERT_TRUE(deserialized_t.options.required_features().find(
                  FEATURE_ALTER_TABLE_RENAME_COLUMN) !=
              deserialized_t.options.required_features().end());
  ASSERT_EQ(deserialized_t.options.userid_column(), "abc");
  ASSERT_EQ(deserialized_t.options.nullable_probability(), 0.12345);
  ASSERT_EQ(deserialized_t.options.column_annotations().size(), 1);
  ASSERT_TRUE(deserialized_t.options.column_annotations().at(0) != nullptr);
  ASSERT_TRUE(deserialized_t.options.column_annotations().at(0)->GetAnnotation(
                  1) != nullptr);
  ASSERT_TRUE(deserialized_t.options.column_annotations().at(0)->GetAnnotation(
                  2) != nullptr);
  ASSERT_EQ(deserialized_t.options.column_annotations()
                .at(0)
                ->GetAnnotation(1)
                ->DebugString(),
            SimpleValue::Int64(1234).DebugString());
  ASSERT_EQ(deserialized_t.options.column_annotations()
                .at(0)
                ->GetAnnotation(2)
                ->DebugString(),
            SimpleValue::Int64(5678).DebugString());
}

}  // namespace googlesql
