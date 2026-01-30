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

#include "googlesql/public/procedure.h"

#include <memory>
#include <utility>
#include <vector>

#include "googlesql/common/testing/proto_matchers.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_deserializer.h"
#include "googlesql/public/types/type_factory.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/str_cat.h"
#include "google/protobuf/descriptor.h"

namespace googlesql {

namespace {
constexpr char kCatalog[] = "catalog";
constexpr char kProcedureName[] = "procedure_name";
}  // namespace

TEST(ProcedureTest, SimpleTest) {
  TypeFactory factory;
  Procedure procedure({kCatalog, kProcedureName},
                      {factory.get_int64(), {}, -1});
  EXPECT_EQ(absl::StrCat(kCatalog, ".", kProcedureName), procedure.FullName());
  EXPECT_EQ(kProcedureName, procedure.Name());
  EXPECT_THAT(procedure.name_path(),
              ::testing::ElementsAre(kCatalog, kProcedureName));

  EXPECT_EQ("() -> INT64", procedure.signature().DebugString());
}

TEST(ProcedureTest, SerializeDeserializeTest) {
  TypeFactory factory;
  Procedure procedure({kCatalog, kProcedureName},
                      {factory.get_int64(), {}, -1});
  FileDescriptorSetMap file_descriptor_set_map;
  ProcedureProto proto;
  GOOGLESQL_EXPECT_OK(procedure.Serialize(&file_descriptor_set_map, &proto));
  EXPECT_THAT(proto, testing::EqualsProto(
                         "name_path: 'catalog' "
                         "name_path: 'procedure_name' "
                         "signature {"
                         "  return_type {"
                         "    kind: ARG_TYPE_FIXED"
                         "    type {"
                         "      type_kind: TYPE_INT64"
                         "    }"
                         "    options {"
                         "      cardinality: REQUIRED"
                         "      extra_relation_input_columns_allowed: true"
                         "    }"
                         "    num_occurrences: -1"
                         "  }"
                         "  context_id: -1"
                         "  options {"
                         "    is_deprecated: false"
                         "  }"
                         "}"));

  std::vector<const google::protobuf::DescriptorPool*> pools(
      file_descriptor_set_map.size());
  for (const auto& pair : file_descriptor_set_map) {
    pools[pair.second->descriptor_set_index] = pair.first;
  }

  std::unique_ptr<Procedure> procedure2 =
      Procedure::Deserialize(proto, TypeDeserializer(&factory, pools)).value();
  EXPECT_EQ(procedure.FullName(), procedure2->FullName());
  EXPECT_EQ(procedure.signature().DebugString(),
            procedure2->signature().DebugString());
}

TEST(ProcedureTest, GetFunctionSignatureMismatchErrorMessage) {
  TypeFactory factory;
  Procedure procedure({kCatalog, kProcedureName},
                      {factory.get_int64(), {factory.get_string()}, -1});
  EXPECT_EQ("catalog.procedure_name(STRING)",
            procedure.GetSupportedSignatureUserFacingText(PRODUCT_INTERNAL));
}

TEST(ProcedureTest, InvalidSignatureTest) {
  TypeFactory factory;
  EXPECT_DEBUG_DEATH(
      Procedure procedure(
          {"invalid_signature"},
          {factory.get_int64(),
           {{factory.get_int64(), FunctionArgumentType::REPEATED},
            {factory.get_int64(), FunctionArgumentType::OPTIONAL}},
           -1}),
      ".*The number of repeated arguments \\(1\\) must be greater than "
      "the number of optional arguments \\(1\\).*");
}

}  // namespace googlesql
