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

#include "googlesql/common/json_conversion_utils.h"

#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/types/array_type.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/types/vector_type_util.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "googlesql/base/testing/status_matchers.h"

namespace googlesql {
namespace {

using ::testing::IsNull;

TEST(GetUnsupportedTypeForJsonConversionTest, SupportedTypes) {
  TypeFactory type_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ArrayType* int64_array,
                       type_factory.MakeArrayType(types::Int64Type()));
  const StructType* struct_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeStructType(
      {{"a", types::StringType()}, {"b", int64_array}}, &struct_type));

  EXPECT_THAT(GetUnsupportedTypeForJsonConversion(types::Int64Type()),
              IsNull());
  EXPECT_THAT(GetUnsupportedTypeForJsonConversion(types::BytesType()),
              IsNull());
  EXPECT_THAT(GetUnsupportedTypeForJsonConversion(types::JsonType()), IsNull());
  EXPECT_THAT(GetUnsupportedTypeForJsonConversion(types::DateRangeType()),
              IsNull());
  EXPECT_THAT(GetUnsupportedTypeForJsonConversion(int64_array), IsNull());
  EXPECT_THAT(GetUnsupportedTypeForJsonConversion(struct_type), IsNull());
}

TEST(GetUnsupportedTypeForJsonConversionTest, UnsupportedTypes) {
  TypeFactory type_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* vector_type, MakeVectorType(&type_factory));
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_MAP_TYPE);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* map_type,
      type_factory.MakeMapType(types::StringType(), types::Int64Type(),
                               language_options));

  EXPECT_EQ(GetUnsupportedTypeForJsonConversion(vector_type), vector_type);
  EXPECT_EQ(GetUnsupportedTypeForJsonConversion(map_type), map_type);
}

TEST(GetUnsupportedTypeForJsonConversionTest, ReturnsNestedUnsupportedType) {
  TypeFactory type_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* vector_type, MakeVectorType(&type_factory));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ArrayType* vector_array,
                       type_factory.MakeArrayType(vector_type));
  const StructType* struct_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeStructType(
      {{"a", types::StringType()}, {"v", vector_array}}, &struct_type));

  EXPECT_EQ(GetUnsupportedTypeForJsonConversion(vector_array), vector_type);
  EXPECT_EQ(GetUnsupportedTypeForJsonConversion(struct_type), vector_type);

  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_MAP_TYPE);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* map_type,
      type_factory.MakeMapType(types::StringType(), types::Int64Type(),
                               language_options));
  const StructType* struct_with_map_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeStructType(
      {{"a", types::Int64Type()}, {"m", map_type}}, &struct_with_map_type));
  EXPECT_EQ(GetUnsupportedTypeForJsonConversion(struct_with_map_type),
            map_type);
}

}  // namespace
}  // namespace googlesql
