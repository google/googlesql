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

#include "googlesql/common/graph_element_utils.h"

#include <vector>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/functions/json.h"
#include "googlesql/public/json_value.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/types/span.h"

namespace googlesql {
namespace {

using ::testing::NotNull;

TEST(GraphElementUtilsTest, IsOrContainsGraphElementTest) {
  TypeFactory factory;
  const Type* string_type = factory.get_string();
  const Type* int_type = factory.get_int64();
  const Type* bytes_type = factory.get_bytes();

  // Basic types
  ASSERT_FALSE(TypeIsOrContainsGraphElement(string_type));
  ASSERT_FALSE(TypeIsOrContainsGraphElement(int_type));
  ASSERT_FALSE(TypeIsOrContainsGraphElement(bytes_type));

  // Node type
  const GraphElementType* node_type;
  GOOGLESQL_ASSERT_OK(factory.MakeGraphElementType(
      {"aml"}, GraphElementType::ElementKind::kNode,
      {{"id", int_type}, {"name", string_type}, {"data", bytes_type}},
      &node_type));
  ASSERT_THAT(node_type, NotNull());
  ASSERT_TRUE(TypeIsOrContainsGraphElement(node_type));

  // Edge type
  const GraphElementType* edge_type;
  GOOGLESQL_ASSERT_OK(factory.MakeGraphElementType(
      {"aml"}, GraphElementType::kEdge,
      {{"transfer_id", int_type}, {"amount", int_type}}, &edge_type));
  ASSERT_THAT(edge_type, NotNull());
  ASSERT_TRUE(TypeIsOrContainsGraphElement(edge_type));

  // Path type
  const GraphPathType* path_type;
  GOOGLESQL_ASSERT_OK(factory.MakeGraphPathType(node_type, edge_type, &path_type));
  ASSERT_THAT(path_type, NotNull());
  ASSERT_TRUE(TypeIsOrContainsGraphElement(path_type));

  // Array of GraphNode
  const ArrayType* array_type;
  GOOGLESQL_ASSERT_OK(factory.MakeArrayType(node_type, &array_type));
  ASSERT_THAT(array_type, NotNull());
  ASSERT_TRUE(TypeIsOrContainsGraphElement(array_type));

  // Struct of GraphEdge and GraphPath
  const StructType* struct_type;
  GOOGLESQL_ASSERT_OK(factory.MakeStructType({{"edge", edge_type}, {"path", path_type}},
                                   &struct_type));
  ASSERT_THAT(struct_type, NotNull());
  ASSERT_TRUE(TypeIsOrContainsGraphElement(struct_type));
}

TEST(GraphElementUtilsTest, MakePropertiesJsonValueTest) {
  const LanguageOptions language_options = LanguageOptions::MaximumFeatures();
  const Value p1_value = Value::Bool(true);
  const Value p2_value = Value::Double(3.14);
  std::vector<Value::Property> properties = {{"p1", p1_value},
                                             {"p2", p2_value}};
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      JSONValue json_value,
      MakePropertiesJsonValue(absl::MakeSpan(properties), language_options));
  EXPECT_EQ(json_value.GetConstRef().GetMembers().size(), 2);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const bool p1,
      functions::ConvertJsonToBool(json_value.GetConstRef().GetMember("p1")));
  EXPECT_EQ(p1, p1_value.bool_value());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const double p2,
      functions::ConvertJsonToDouble(json_value.GetConstRef().GetMember("p2"),
                                     functions::WideNumberMode::kExact,
                                     ProductMode::PRODUCT_INTERNAL));
  EXPECT_EQ(p2, p2_value.double_value());
}

}  // namespace
}  // namespace googlesql
