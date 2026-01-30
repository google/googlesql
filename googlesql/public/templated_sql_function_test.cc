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

#include "googlesql/public/templated_sql_function.h"

#include <cstdint>
#include <memory>

#include "googlesql/base/testing/status_matchers.h"  
#include "googlesql/public/function.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/parse_resume_location.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace googlesql {

TEST(TemplatedSqlFunctionTest, SerailizeDeserialize) {
  TypeFactory type_factory;
  TemplatedSQLFunction initial(
      {"test_function"},
      FunctionSignature(type_factory.get_int64(), /*arguments=*/{},
                        /*context_id=*/static_cast<int64_t>(0)),
      /*argument_names=*/{},
      ParseResumeLocation::FromStringView("filename", "input"),
      Function::AGGREGATE, FunctionOptions());
  EXPECT_TRUE(initial.IsAggregate());

  FunctionProto proto;
  FileDescriptorSetMap file_descriptor_set_map;
  GOOGLESQL_ASSERT_OK(initial.Serialize(&file_descriptor_set_map, &proto,
                              /*omit_signatures=*/false));

  std::unique_ptr<Function> deserialized;
  GOOGLESQL_ASSERT_OK(TemplatedSQLFunction::Deserialize(proto, /*pools=*/{},
                                              &type_factory, &deserialized));
  EXPECT_TRUE(deserialized->IsAggregate());
}

TEST(TemplatedSqlFunctionTest, RegiesteredDeserializer) {
  TypeFactory type_factory;
  TemplatedSQLFunction initial(
      {"test_function"},
      FunctionSignature(type_factory.get_int64(), /*arguments=*/{},
                        /*context_id=*/static_cast<int64_t>(0)),
      /*argument_names=*/{},
      ParseResumeLocation::FromStringView("filename", "input"),
      Function::AGGREGATE, FunctionOptions());
  EXPECT_TRUE(initial.IsAggregate());

  FunctionProto proto;
  FileDescriptorSetMap file_descriptor_set_map;
  GOOGLESQL_ASSERT_OK(initial.Serialize(&file_descriptor_set_map, &proto,
                              /*omit_signatures=*/false));

  std::unique_ptr<Function> deserialized;
  GOOGLESQL_ASSERT_OK(
      Function::Deserialize(proto, /*pools=*/{}, &type_factory, &deserialized));
  EXPECT_TRUE(deserialized->IsAggregate());
  EXPECT_TRUE(deserialized->Is<TemplatedSQLFunction>());
}

}  // namespace googlesql
