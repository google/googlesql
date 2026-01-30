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

#include "googlesql/public/types/map_type.h"

#include <initializer_list>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "googlesql/base/testing/status_matchers.h"  
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/type_parameters.pb.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/array_type.h"
#include "googlesql/public/types/collation.h"
#include "googlesql/public/types/enum_type.h"
#include "googlesql/public/types/proto_type.h"
#include "googlesql/public/types/range_type.h"
#include "googlesql/public/types/simple_value.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/types/type_modifiers.h"
#include "googlesql/public/value.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "googlesql/testing/test_value.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "google/protobuf/descriptor.h"

namespace googlesql {
namespace {

using google::protobuf::EnumDescriptor;
using testing::HasSubstr;
using testing::NotNull;
using absl_testing::IsOk;
using absl_testing::IsOkAndHolds;
using absl_testing::StatusIs;
using MapTestAllSimpleTypes = testing::TestWithParam<TypeKind>;

struct MapTestFormatValueContentDebugModeTestCase {
  Value value;
  std::string expected_debug_string;
  std::string expected_verbose_debug_string;
};

using MapTestFormatValueContentDebugMode =
    testing::TestWithParam<MapTestFormatValueContentDebugModeTestCase>;

}  // namespace

// All types which can be constructed via TypeFactory::TypeFromSimpleTypeKind.
const auto kSimpleTypes = {
    TYPE_INT32, TYPE_INT64, TYPE_UINT32, TYPE_UINT64, TYPE_BOOL, TYPE_FLOAT,
    TYPE_DOUBLE, TYPE_STRING, TYPE_BYTES, TYPE_TIMESTAMP,
    TYPE_DATE, TYPE_TIME, TYPE_DATETIME, TYPE_INTERVAL, TYPE_GEOGRAPHY,
    TYPE_NUMERIC, TYPE_BIGNUMERIC, TYPE_JSON, TYPE_TOKENLIST};

INSTANTIATE_TEST_SUITE_P(
    TypeTest, MapTestAllSimpleTypes, testing::ValuesIn(kSimpleTypes),
    [](const testing::TestParamInfo<MapTestAllSimpleTypes::ParamType>& info) {
      return TypeKind_Name(info.param);
    });

// Asserts map type conformance to Type's Is...() and As...() methods, and
// asserts that equality and partitioning are disabled.
void BasicMapAsserts(const Type* map_type) {
  EXPECT_FALSE(map_type->IsSimpleType());
  EXPECT_FALSE(map_type->IsEnum());
  EXPECT_FALSE(map_type->IsArray());
  EXPECT_FALSE(map_type->IsStruct());
  EXPECT_FALSE(map_type->IsProto());
  EXPECT_FALSE(map_type->IsStructOrProto());
  EXPECT_FALSE(map_type->IsRangeType());
  EXPECT_TRUE(map_type->IsMapType());
  EXPECT_EQ(map_type->AsStruct(), nullptr);
  EXPECT_EQ(map_type->AsArray(), nullptr);
  EXPECT_EQ(map_type->AsProto(), nullptr);
  EXPECT_EQ(map_type->AsEnum(), nullptr);
  EXPECT_EQ(map_type->AsRange(), nullptr);
  EXPECT_FALSE(map_type->IsGraphElement());
  EXPECT_EQ(nullptr, map_type->AsGraphElement());

  EXPECT_FALSE(map_type->SupportsEquality());

  LanguageOptions language_options;
  EXPECT_FALSE(map_type->IsSupportedType(language_options));

  language_options.EnableLanguageFeature(FEATURE_MAP_TYPE);
  std::string no_partitioning_type;
  EXPECT_FALSE(
      map_type->SupportsPartitioning(language_options, &no_partitioning_type));
  EXPECT_EQ(no_partitioning_type, "MAP");
}

TEST_P(MapTestAllSimpleTypes, MapCanBeConstructedWithSimpleType) {
  TypeFactory factory;
  TypeKind type_kind = GetParam();

  const Type* key_type = types::TypeFromSimpleTypeKind(type_kind);
  const Type* value_type = types::TypeFromSimpleTypeKind(type_kind);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* map_type,
                       factory.MakeMapType(key_type, value_type));

  EXPECT_TRUE(key_type == GetMapKeyType(map_type));
  EXPECT_TRUE(value_type == GetMapValueType(map_type));

  BasicMapAsserts(map_type);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* map_with_map_key_type,
                       factory.MakeMapType(map_type, value_type));
  BasicMapAsserts(map_with_map_key_type);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* map_with_map_value_type,
                       factory.MakeMapType(key_type, map_type));
  BasicMapAsserts(map_with_map_value_type);
}

TEST(TypeTest, MapTypeRequiresKeyTypeToBeGroupable) {
  TypeFactory factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* map_type_with_groupable_simple_key,
      factory.MakeMapType(factory.get_string(), factory.get_string()));

  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_MAP_TYPE);
  EXPECT_TRUE(
      map_type_with_groupable_simple_key->IsSupportedType(language_options));
}

TEST(TypeTest, MapTypeRequiresKeyTypeToBeGroupableConditionallyGroupableKey) {
  TypeFactory factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* map_type_with_array_key,
      factory.MakeMapType(types::Int32ArrayType(), factory.get_string()));

  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_MAP_TYPE);
  EXPECT_FALSE(map_type_with_array_key->IsSupportedType(language_options));
  language_options.EnableLanguageFeature(FEATURE_GROUP_BY_ARRAY);
  EXPECT_TRUE(map_type_with_array_key->IsSupportedType(language_options));
}

TEST(TypeTest, MapTypeRequiresKeyAndValueTypesToBeSupported) {
  TypeFactory factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* map_type,
      factory.MakeMapType(types::DateRangeType(), factory.get_time()));
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_MAP_TYPE);
  EXPECT_FALSE(map_type->IsSupportedType(language_options));

  language_options.EnableLanguageFeature(FEATURE_CIVIL_TIME);
  EXPECT_FALSE(map_type->IsSupportedType(language_options));

  language_options.EnableLanguageFeature(FEATURE_RANGE_TYPE);
  EXPECT_TRUE(map_type->IsSupportedType(language_options));
}

TEST(TypeTest, TestNamesValid) {
  TypeFactory factory;
  googlesql_test::KitchenSinkPB kitchen_sink;
  const ProtoType* proto_type;
  GOOGLESQL_EXPECT_OK(factory.MakeProtoType(kitchen_sink.GetDescriptor(), &proto_type));
  EXPECT_THAT(proto_type, NotNull());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* map_type,
                       factory.MakeMapType(factory.get_string(), proto_type));

  EXPECT_EQ(map_type->DebugString(),
            "MAP<STRING, "
            "PROTO<googlesql_test.KitchenSinkPB>>");
  EXPECT_EQ(map_type->ShortTypeName(PRODUCT_INTERNAL),
            "MAP<STRING, "
            "googlesql_test.KitchenSinkPB>");
  EXPECT_EQ(map_type->TypeName(PRODUCT_INTERNAL),
            "MAP<STRING, "
            "`googlesql_test.KitchenSinkPB`>");
}

TEST(TypeTest, TestNamesValidWithModifiers) {
  TypeFactory factory;
  googlesql_test::KitchenSinkPB kitchen_sink;
  const ProtoType* proto_type;
  GOOGLESQL_EXPECT_OK(factory.MakeProtoType(kitchen_sink.GetDescriptor(), &proto_type));
  EXPECT_THAT(proto_type, NotNull());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* map_type,
                       factory.MakeMapType(factory.get_string(), proto_type));

  TypeModifiers empty_modifiers =
      TypeModifiers::MakeTypeModifiers(TypeParameters(), Collation());

  // Make a MAP type modifier with a string length parameter on the key.
  StringTypeParametersProto string_parameters_proto;
  string_parameters_proto.set_max_length(10);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      TypeParameters string_parameters,
      TypeParameters::MakeStringTypeParameters(string_parameters_proto));
  TypeParameters map_parameters_string_key_max_length =
      TypeParameters::MakeTypeParametersWithChildList(
          {string_parameters, TypeParameters()});
  TypeModifiers map_modifiers_string_key_max_length =
      TypeModifiers::MakeTypeModifiers(map_parameters_string_key_max_length,
                                       Collation());

  // Make a MAP type modifier with a type parameter directly on the map type.
  // Since MAP doesn't actually support direct type parameters, we reuse the
  // string parameter here.
  TypeModifiers map_modifiers_confused_type_param_on_map =
      TypeModifiers::MakeTypeModifiers(string_parameters, Collation());

  // Make a MAP type modifier with a collation annotation on the key.
  std::unique_ptr<AnnotationMap> annotation_map =
      AnnotationMap::Create(types::StringType());
  annotation_map->SetAnnotation(static_cast<int>(AnnotationKind::kCollation),
                                SimpleValue::String("und:ci"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Collation collation,
                       Collation::MakeCollation(*annotation_map));
  TypeModifiers map_modifiers_collation =
      TypeModifiers::MakeTypeModifiers(TypeParameters(), collation);

  EXPECT_THAT(
      map_type->TypeNameWithModifiers(empty_modifiers, PRODUCT_INTERNAL),
      IsOkAndHolds("MAP<STRING, `googlesql_test.KitchenSinkPB`>"));
  EXPECT_THAT(map_type->TypeNameWithModifiers(
                  map_modifiers_string_key_max_length, PRODUCT_INTERNAL),
              IsOkAndHolds("MAP<STRING(10), `googlesql_test.KitchenSinkPB`>"));
  EXPECT_THAT(
      map_type->TypeNameWithModifiers(map_modifiers_confused_type_param_on_map,
                                      PRODUCT_INTERNAL),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr(
                   "Type parameters are only supported on MAP key and value")));
  EXPECT_THAT(
      map_type->TypeNameWithModifiers(map_modifiers_collation,
                                      PRODUCT_INTERNAL),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("MAP does not support collation on key and value")));
}

TEST(TypeTest, TestNamesValidWithNesting) {
  TypeFactory factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* map_type,
      factory.MakeMapType(factory.get_string(), factory.get_string()));
  const StructType* struct_type;
  GOOGLESQL_ASSERT_OK(factory.MakeStructType({{"a", map_type}}, &struct_type));
  const ArrayType* array_type;
  GOOGLESQL_ASSERT_OK(factory.MakeArrayType(struct_type, &array_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* outer_map_type,
                       factory.MakeMapType(array_type, array_type));

  EXPECT_EQ(outer_map_type->DebugString(),
            "MAP<ARRAY<STRUCT<a MAP<STRING, STRING>>>, ARRAY<STRUCT<a "
            "MAP<STRING, STRING>>>>");

  EXPECT_EQ(outer_map_type->ShortTypeName(PRODUCT_INTERNAL),
            "MAP<ARRAY<STRUCT<a MAP<STRING, STRING>>>, ARRAY<STRUCT<a "
            "MAP<STRING, STRING>>>>");
  EXPECT_EQ(outer_map_type->ShortTypeName(PRODUCT_EXTERNAL),
            "MAP<ARRAY<STRUCT<a MAP<STRING, STRING>>>, ARRAY<STRUCT<a "
            "MAP<STRING, STRING>>>>");

  EXPECT_EQ(outer_map_type->TypeName(PRODUCT_INTERNAL),
            "MAP<ARRAY<STRUCT<a MAP<STRING, STRING>>>, ARRAY<STRUCT<a "
            "MAP<STRING, STRING>>>>");
  EXPECT_EQ(outer_map_type->TypeName(PRODUCT_EXTERNAL),
            "MAP<ARRAY<STRUCT<a MAP<STRING, STRING>>>, ARRAY<STRUCT<a "
            "MAP<STRING, STRING>>>>");
}
TEST(TypeTest, MapTypeWithStructValid) {
  TypeFactory factory;
  const StructType* struct_type;
  GOOGLESQL_ASSERT_OK(
      factory.MakeStructType({{"a", factory.get_string()}}, &struct_type));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* map_type,
                       factory.MakeMapType(struct_type, struct_type));
  BasicMapAsserts(map_type);

  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_MAP_TYPE);
  EXPECT_FALSE(map_type->IsSupportedType(language_options));
  language_options.EnableLanguageFeature(FEATURE_GROUP_BY_STRUCT);
  EXPECT_TRUE(map_type->IsSupportedType(language_options));
}

TEST(MapTest, MapTypeWithArrayValid) {
  TypeFactory factory;
  const ArrayType* array_type;
  GOOGLESQL_ASSERT_OK(factory.MakeArrayType(factory.get_string(), &array_type));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* map_type,
                       factory.MakeMapType(array_type, array_type));
  BasicMapAsserts(map_type);

  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_MAP_TYPE);
  EXPECT_FALSE(map_type->IsSupportedType(language_options));
  language_options.EnableLanguageFeature(FEATURE_GROUP_BY_ARRAY);
  EXPECT_TRUE(map_type->IsSupportedType(language_options));
}

TEST(MapTest, MapTypeWithRangeValid) {
  TypeFactory factory;
  const RangeType* range_type;
  GOOGLESQL_ASSERT_OK(factory.MakeRangeType(factory.get_timestamp(), &range_type));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* map_type,
                       factory.MakeMapType(range_type, range_type));
  BasicMapAsserts(map_type);

  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_MAP_TYPE);
  EXPECT_FALSE(map_type->IsSupportedType(language_options));
  language_options.EnableLanguageFeature(FEATURE_RANGE_TYPE);
  EXPECT_TRUE(map_type->IsSupportedType(language_options));
}

TEST(MapTest, MapTypeWithProtoValid) {
  TypeFactory factory;

  googlesql_test::KitchenSinkPB kitchen_sink;
  const ProtoType* proto_type;

  GOOGLESQL_EXPECT_OK(factory.MakeProtoType(kitchen_sink.GetDescriptor(), &proto_type));
  EXPECT_THAT(proto_type, NotNull());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* map_type,
                       factory.MakeMapType(factory.get_string(), proto_type));
  BasicMapAsserts(map_type);

  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_MAP_TYPE);
  EXPECT_TRUE(map_type->IsSupportedType(language_options));
}
TEST(MapTest, MapTypeWithEnumValid) {
  TypeFactory factory;

  const EnumType* enum_type;
  const EnumDescriptor* enum_descriptor = googlesql_test::TestEnum_descriptor();
  GOOGLESQL_EXPECT_OK(factory.MakeEnumType(enum_descriptor, &enum_type));
  EXPECT_THAT(enum_type, NotNull());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* map_type,
                       factory.MakeMapType(enum_type, enum_type));
  BasicMapAsserts(map_type);

  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_MAP_TYPE);
  EXPECT_TRUE(map_type->IsSupportedType(language_options));
}

// Note: More rigorous testing is done through GetSQLLiteral() value_test.cc
TEST(MapTest, FormatValueContentSQLLiteralMode) {
  Type::FormatValueContentOptions options;
  options.mode = Type::FormatValueContentOptions::Mode::kSQLLiteral;
  options.verbose = true;

  Value map_value =
      test_values::Map({{Value::String("foo"), Value::Int64(100)}});

  EXPECT_EQ(
      map_value.type()->FormatValueContent(map_value.GetContent(), options),
      R"(MAP_FROM_ARRAY([("foo", 100)]))");
}

// Note: More rigorous testing is done through GetSQL() value_test.cc
TEST(MapTest, FormatValueContentSQLExpressionMode) {
  {
    Type::FormatValueContentOptions options = {
        .mode = Type::FormatValueContentOptions::Mode::kSQLExpression,
    };
    Value map_value =
        test_values::Map({{Value::String("foo"), Value::Int64(100)}});
    EXPECT_EQ(
        map_value.type()->FormatValueContent(map_value.GetContent(), options),
        R"(MAP_FROM_ARRAY(ARRAY<STRUCT<STRING, INT64>>[("foo", 100)]))");
  }
  // MAP with FLOATs and use_external_float32 = false
  {
    Type::FormatValueContentOptions options = {
        .mode = Type::FormatValueContentOptions::Mode::kSQLExpression,
    };
    Value map_value =
        test_values::Map({{Value::Float(1.5), Value::Double(2.5)}});
    EXPECT_EQ(
        map_value.type()->FormatValueContent(map_value.GetContent(), options),
        R"(MAP_FROM_ARRAY(ARRAY<STRUCT<FLOAT32, FLOAT64>>[(CAST(1.5 AS FLOAT32), 2.5)]))");
  }

  // MAP with FLOATs and use_external_float32 = true
  {
    Type::FormatValueContentOptions options = {
        .use_external_float32 = true,
        .mode = Type::FormatValueContentOptions::Mode::kSQLExpression,
    };
    Value map_value =
        test_values::Map({{Value::Float(1.5), Value::Double(2.5)}});
    EXPECT_EQ(
        map_value.type()->FormatValueContent(map_value.GetContent(), options),
        R"(MAP_FROM_ARRAY(ARRAY<STRUCT<FLOAT32, FLOAT64>>[(CAST(1.5 AS FLOAT32), 2.5)]))");
  }

  // MAP with FLOAT in nested container and use_external_float32 = false
  {
    Type::FormatValueContentOptions options = {
        .mode = Type::FormatValueContentOptions::Mode::kSQLExpression,
    };
    Value map_value = test_values::Map(
        {{Value::Float(1.5), test_values::Array({Value::Float(2.5)})}});
    EXPECT_EQ(
        map_value.type()->FormatValueContent(map_value.GetContent(), options),
        R"(MAP_FROM_ARRAY(ARRAY<STRUCT<FLOAT32, ARRAY<FLOAT32>>>[(CAST(1.5 AS FLOAT32), ARRAY<FLOAT32>[CAST(2.5 AS FLOAT32)])]))");
  }

  // MAP with FLOAT in nested container and use_external_float32 = true
  {
    Type::FormatValueContentOptions options = {
        .use_external_float32 = true,
        .mode = Type::FormatValueContentOptions::Mode::kSQLExpression,
    };
    Value map_value = test_values::Map(
        {{Value::Float(1.5), test_values::Array({Value::Float(2.5)})}});
    EXPECT_EQ(
        map_value.type()->FormatValueContent(map_value.GetContent(), options),
        R"(MAP_FROM_ARRAY(ARRAY<STRUCT<FLOAT32, ARRAY<FLOAT32>>>[(CAST(1.5 AS FLOAT32), ARRAY<FLOAT32>[CAST(2.5 AS FLOAT32)])]))");
  }
}

INSTANTIATE_TEST_SUITE_P(
    MapTest, MapTestFormatValueContentDebugMode,
    testing::ValuesIn(std::initializer_list<
                      MapTestFormatValueContentDebugModeTestCase>{
        {
            test_values::Map({{"a", true}}),
            R"({"a": true})",
            R"(Map{String("a"): Bool(true)})",
        },
        {
            test_values::Map(
                {{"a", true}, {"b", false}, {"c", Value::NullBool()}}),
            R"({"a": true, "b": false, "c": NULL})",
            R"(Map{String("a"): Bool(true), String("b"): Bool(false), String("c"): Bool(NULL)})",
        },
        {
            test_values::Map({{"foobar", Value::Int32(1)},
                              {"zoobar", Value::Int32(2)}}),
            R"({"foobar": 1, "zoobar": 2})",
            R"(Map{String("foobar"): Int32(1), String("zoobar"): Int32(2)})",
        },
        {
            test_values::Map({{"a", test_values::Array({Value::Int32(1),
                                                        Value::Int32(2)})}}),
            R"({"a": [1, 2]})",
            R"(Map{String("a"): Array[Int32(1), Int32(2)]})",
        },
        {
            test_values::Map(
                {{"nested",
                  test_values::Map(
                      {{"a", test_values::Map(
                                 {{"b", test_values::Map(
                                            {{"c", Value::Int32(1)}})}})}})}}),
            R"({"nested": {"a": {"b": {"c": 1}}}})",
            R"(Map{String("nested"): Map{String("a"): Map{String("b"): Map{String("c"): Int32(1)}}}})",
        },
        {
            test_values::Map(
                {{"nested",
                  test_values::Struct(
                      {{"field",
                        test_values::Map(
                            {{"a", test_values::Map(
                                       {{"b", Value::Int32(1)}})}})}})}}),
            R"({"nested": {field:{"a": {"b": 1}}}})",
            R"(Map{String("nested"): Struct{field:Map{String("a"): Map{String("b"): Int32(1)}}}})",
        },
    }));

TEST_P(MapTestFormatValueContentDebugMode, FormatValueContentDebugMode) {
  auto& [map_value, expected_format_str, expected_verbose_format_str] =
      GetParam();

  Type::FormatValueContentOptions options;
  options.mode = Type::FormatValueContentOptions::Mode::kDebug;
  options.verbose = false;

  EXPECT_EQ(
      map_value.type()->FormatValueContent(map_value.GetContent(), options),
      expected_format_str);

  options.verbose = true;
  EXPECT_EQ(
      map_value.type()->FormatValueContent(map_value.GetContent(), options),
      expected_verbose_format_str);
}

TEST(MapTest, FormatValueContentDebugModeEmptyMap) {
  TypeFactory factory;

  Type::FormatValueContentOptions options;
  options.mode = Type::FormatValueContentOptions::Mode::kDebug;

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* map_type,
      factory.MakeMapType(factory.get_string(), factory.get_int64()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value map_value, Value::MakeMap(map_type, {}));

  options.verbose = false;
  EXPECT_EQ(map_type->FormatValueContent(map_value.GetContent(), options),
            "{}");

  options.verbose = true;
  EXPECT_EQ(map_type->FormatValueContent(map_value.GetContent(), options),
            "Map{}");
}

TEST(MapTest, MakeMapWithLanguageOptions) {
  TypeFactory factory;

  LanguageOptions language_map_enabled;
  language_map_enabled.EnableLanguageFeature(FEATURE_MAP_TYPE);

  LanguageOptions language_map_enabled_geography_enabled = language_map_enabled;
  language_map_enabled_geography_enabled.EnableLanguageFeature(
      FEATURE_GEOGRAPHY);

  LanguageOptions language_map_enabled_array_grouping_enabled =
      language_map_enabled;
  language_map_enabled_array_grouping_enabled.EnableLanguageFeature(
      FEATURE_GROUP_BY_ARRAY);

  LanguageOptions language_map_array_struct_grouping_enabled =
      language_map_enabled_array_grouping_enabled;
  language_map_array_struct_grouping_enabled.EnableLanguageFeature(
      FEATURE_GROUP_BY_STRUCT);

  const StructType* struct_type;
  GOOGLESQL_ASSERT_OK(factory.MakeStructType({{"a", types::Int32Type()}}, &struct_type));
  const ArrayType* array_of_struct_type;
  GOOGLESQL_ASSERT_OK(factory.MakeArrayType(struct_type, &array_of_struct_type));

  EXPECT_THAT(factory.MakeMapType(types::StringType(), types::StringType(),
                                  LanguageOptions()),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       "MAP datatype is not supported"));
  EXPECT_THAT(factory.MakeMapType(types::StringType(), types::StringType(),
                                  language_map_enabled),
              IsOk());
  EXPECT_THAT(factory.MakeMapType(types::GeographyType(), types::StringType(),
                                  language_map_enabled_geography_enabled),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       "MAP key type GEOGRAPHY is not groupable"));
  EXPECT_THAT(factory.MakeMapType(types::StringType(), types::GeographyType(),
                                  language_map_enabled_geography_enabled),
              IsOk())
      << "MAP should still be valid when value type is not groupable";
  EXPECT_THAT(factory.MakeMapType(array_of_struct_type, types::StringType(),
                                  language_map_enabled),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       "MAP key type ARRAY is not groupable"));
  EXPECT_THAT(
      factory.MakeMapType(array_of_struct_type, types::StringType(),
                          language_map_enabled_array_grouping_enabled),
      StatusIs(absl::StatusCode::kInvalidArgument,
               "MAP key type ARRAY containing STRUCT is not groupable"));
  EXPECT_THAT(factory.MakeMapType(types::Int64ArrayType(), types::StringType(),
                                  language_map_enabled_array_grouping_enabled),
              IsOk());
  EXPECT_THAT(factory.MakeMapType(array_of_struct_type, types::StringType(),
                                  language_map_array_struct_grouping_enabled),
              IsOk());
}

TEST(TypeFactoryTest, MapTypesAreCached) {
  TypeFactory factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* type1,
      factory.MakeMapType(factory.get_int64(), factory.get_double()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* type2,
      factory.MakeMapType(factory.get_int64(), factory.get_double()));
  EXPECT_TRUE(type1 == type2)
      << "Expected the two type pointers to be identical";
}

TEST_P(MapTestAllSimpleTypes, MapWithSimpleTypesUsesStaticFactory) {
  TypeFactory factory;
  TypeKind type_kind = GetParam();
  const Type* simple_type = types::TypeFromSimpleTypeKind(type_kind);

  const auto initial_factory_size = factory.GetEstimatedOwnedMemoryBytesSize();
  GOOGLESQL_ASSERT_OK(factory.MakeMapType(simple_type, simple_type));

  // Our factory should not change size, because the static factory was used.
  ASSERT_EQ(initial_factory_size, factory.GetEstimatedOwnedMemoryBytesSize());
}

TEST(TypeFactoryTest, MapWithComplexTypesUsesInstanceFactory) {
  TypeFactory factory;
  const Type* struct_type;
  GOOGLESQL_ASSERT_OK(factory.MakeStructType({{"a", types::Int32Type()}}, &struct_type));

  const auto initial_factory_size = factory.GetEstimatedOwnedMemoryBytesSize();
  GOOGLESQL_ASSERT_OK(factory.MakeMapType(struct_type, types::Int32Type()));

  ASSERT_NE(initial_factory_size, factory.GetEstimatedOwnedMemoryBytesSize());
}

}  // namespace googlesql
