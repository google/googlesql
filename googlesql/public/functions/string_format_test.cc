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

#include "googlesql/public/functions/string_format.h"

#include <cstdint>
#include <string>
#include <vector>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/types/array_type.h"
#include "googlesql/public/types/declarative_type.h"
#include "googlesql/public/types/graph_element_type.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/types/vector_type_util.h"
#include "googlesql/public/value.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace googlesql {
namespace functions {

using ::testing::HasSubstr;
using ::absl_testing::StatusIs;

void TestBadPattern(absl::string_view pattern, absl::Span<const Value> values,
                    bool canonicalize_zero) {
  std::string output;
  bool is_null;
  absl::Status status =
      StringFormatUtf8(pattern, values, ProductMode::PRODUCT_INTERNAL, &output,
                       &is_null, canonicalize_zero);
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kOutOfRange,
                               HasSubstr("Format string")));
  std::vector<const Type*> types;
  for (const auto& value : values) {
    types.push_back(value.type());
  }
  status = CheckStringFormatUtf8ArgumentTypes(pattern, types,
                                              ProductMode::PRODUCT_INTERNAL);
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kOutOfRange,
                               HasSubstr("Format string")));
  EXPECT_THAT(output, testing::IsEmpty());
}

void TestBadValue(absl::string_view pattern, Value value,
                  bool canonicalize_zero) {
  std::string output;
  bool is_null;
  const absl::Status status =
      StringFormatUtf8(pattern, {value}, ProductMode::PRODUCT_INTERNAL, &output,
                       &is_null, canonicalize_zero);
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kOutOfRange,
                               HasSubstr("Invalid value")));
  EXPECT_THAT(output, testing::IsEmpty());
}

using StringFormatTest = testing::TestWithParam<bool>;

TEST_P(StringFormatTest, TestBadUtf8Patterns) {
  bool canonicalize_zero = GetParam();
  // Single invalid byte - this should be the first of a multi-byte sequence.
  TestBadPattern("\xc1", {}, canonicalize_zero);
  TestBadPattern("a\xc1", {}, canonicalize_zero);
  TestBadPattern("a\xc1z", {}, canonicalize_zero);
  TestBadPattern("%\xc1z", {}, canonicalize_zero);
  TestBadPattern("\xc1%", {}, canonicalize_zero);
  TestBadPattern("%\xc1%", {}, canonicalize_zero);
  TestBadPattern("%\xc1s", {values::String("")}, canonicalize_zero);
  TestBadPattern("%\xc1s", {values::String("")}, canonicalize_zero);
  TestBadPattern("%\xc1.s", {values::String("")}, canonicalize_zero);
  TestBadPattern("%.\xc1s", {values::String("")}, canonicalize_zero);

  // Disallowed byte. This has the correct number of bytes (4), but this
  // particular pattern is disallowed (as it would encode to 'zero' which should
  // simply be '\0'.
  TestBadPattern("\xf0\x80\x80\x80", {}, canonicalize_zero);
  TestBadPattern("a\xf0\x80\x80\x80", {}, canonicalize_zero);
  TestBadPattern("a\xf0\x80\x80\x80z", {}, canonicalize_zero);
  TestBadPattern("%\xf0\x80\x80\x80z", {}, canonicalize_zero);
  TestBadPattern("\xf0\x80\x80\x80%", {}, canonicalize_zero);
  TestBadPattern("%\xf0\x80\x80\x80%", {}, canonicalize_zero);
  TestBadPattern("%\xf0\x80\x80\x80s", {values::String("")}, canonicalize_zero);
  TestBadPattern("%\xf0\x80\x80\x80s", {values::String("")}, canonicalize_zero);
}

TEST_P(StringFormatTest, TestBadUtf8Values) {
  bool canonicalize_zero = GetParam();
  Value bad_value1 = Value::String("\xc1");
  Value long_bad_value1 = Value::String("1234566789\xc1xyz");
  Value bad_value2 = Value::String("\xf0\x80\x80\x80");

  TestBadValue("%s", bad_value1, canonicalize_zero);
  TestBadValue("%.2s", bad_value1, canonicalize_zero);
  TestBadValue("%t", bad_value1, canonicalize_zero);
  TestBadValue("%T", bad_value1, canonicalize_zero);

  TestBadValue("%s", bad_value2, canonicalize_zero);
  TestBadValue("%.2s", bad_value2, canonicalize_zero);
  TestBadValue("%t", bad_value2, canonicalize_zero);
  TestBadValue("%T", bad_value2, canonicalize_zero);

  TestBadValue("%s", long_bad_value1, canonicalize_zero);
  // This particular test could be deleted in the future if we get a little
  // more clever about processing strings.  We don't actually need to examine
  // the end of this string to return a correct result.
  TestBadValue("%.2s", long_bad_value1, canonicalize_zero);
  TestBadValue("%t", long_bad_value1, canonicalize_zero);
  TestBadValue("%T", long_bad_value1, canonicalize_zero);

  TypeFactory type_factory;

  const ArrayType* array_type = types::StringArrayType();
  const Value bad_array_value = values::StringArray({"\xc1"});
  TestBadValue("%t", bad_array_value, canonicalize_zero);
  TestBadValue("%T", bad_array_value, canonicalize_zero);

  const StructType* struct_type;
  GOOGLESQL_ASSERT_OK(
      type_factory.MakeStructType({{"f1", types::StringType()}}, &struct_type));
  const Value bad_struct_value = Value::Struct(struct_type, {bad_value1});
  TestBadValue("%t", bad_struct_value, canonicalize_zero);
  TestBadValue("%T", bad_struct_value, canonicalize_zero);

  const StructType* struct_of_array_type;
  GOOGLESQL_ASSERT_OK(
      type_factory.MakeStructType({{"a1", array_type}}, &struct_of_array_type));
  const Value bad_struct_of_array_value =
      Value::Struct(struct_of_array_type, {bad_array_value});
  TestBadValue("%t", bad_struct_of_array_value, canonicalize_zero);
  TestBadValue("%T", bad_struct_of_array_value, canonicalize_zero);

  const ArrayType* array_of_struct_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeArrayType(struct_type, &array_of_struct_type));
  const Value bad_array_of_struct_value =
      Value::Array(array_of_struct_type, {bad_struct_value});
  TestBadValue("%t", bad_array_of_struct_value, canonicalize_zero);
  TestBadValue("%T", bad_array_of_struct_value, canonicalize_zero);
}

TEST_P(StringFormatTest, TestBadJsonValue) {
  bool canonicalize_zero = GetParam();
  Value bad_json = Value::UnvalidatedJsonString(R"({"a": 12)");

  TestBadValue("%t", bad_json, canonicalize_zero);
  TestBadValue("%T", bad_json, canonicalize_zero);
  TestBadValue("%p", bad_json, canonicalize_zero);
  TestBadValue("%P", bad_json, canonicalize_zero);
}

INSTANTIATE_TEST_SUITE_P(CanonicalizedZero, StringFormatTest, testing::Bool());

TEST(CheckStringFormat, TestUnsupportedType) {
  TypeFactory type_factory;
  const googlesql::GraphElementType* graph_element_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory.MakeGraphElementType(
      {"aml"}, GraphElementType::ElementKind::kNode, {},
      /*element_tables=*/{}, &graph_element_type));
  EXPECT_THAT(CheckStringFormatUtf8ArgumentTypes("%t", {graph_element_type},
                                                 ProductMode::PRODUCT_EXTERNAL),
              StatusIs(absl::StatusCode::kUnimplemented));
  EXPECT_THAT(CheckStringFormatUtf8ArgumentTypes(
                  "%t", {type_factory.get_column_list_spec()},
                  ProductMode::PRODUCT_EXTERNAL),
              StatusIs(absl::StatusCode::kUnimplemented));

  // VECTOR is rejected, including when nested inside an ARRAY.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* vector_type, MakeVectorType(&type_factory));
  const ArrayType* vector_array_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory.MakeArrayType(vector_type, &vector_array_type));
  for (absl::string_view pattern : {"%t", "%T"}) {
    EXPECT_THAT(
        CheckStringFormatUtf8ArgumentTypes(pattern, {vector_type},
                                           ProductMode::PRODUCT_EXTERNAL),
        StatusIs(absl::StatusCode::kUnimplemented, HasSubstr("VECTOR")));
    EXPECT_THAT(
        CheckStringFormatUtf8ArgumentTypes(pattern, {vector_array_type},
                                           ProductMode::PRODUCT_EXTERNAL),
        StatusIs(absl::StatusCode::kUnimplemented, HasSubstr("VECTOR")));
  }

  std::string output;
  bool is_null;
  EXPECT_THAT(
      StringFormatUtf8("%t", {Value::Null(type_factory.get_column_list_spec())},
                       ProductMode::PRODUCT_EXTERNAL, &output, &is_null),
      StatusIs(absl::StatusCode::kUnimplemented));

  // FORMAT also rejects VECTOR at evaluation time. This matters when the
  // pattern is not a literal (e.g. a query parameter or a column), since the
  // argument types are then not checked during analysis.
  for (absl::string_view pattern : {"%t", "%T"}) {
    for (const Type* type :
         std::vector<const Type*>{vector_type, vector_array_type}) {
      EXPECT_THAT(
          StringFormatUtf8(pattern, {Value::Null(type)},
                           ProductMode::PRODUCT_EXTERNAL, &output, &is_null),
          StatusIs(absl::StatusCode::kUnimplemented, HasSubstr("VECTOR")));
    }
  }
}

TEST(CheckStringFormat, UnsupportedForDeclarativeTypes) {
  TypeFactory type_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* declarative_type,
                       type_factory.MakeDeclarativeType(
                           DeclarativeTypeDescriptor()
                               .set_type_id({"NS", "T1"})
                               .set_display_name("t1")
                               .set_backing_type(type_factory.get_int64())));
  EXPECT_THAT(CheckStringFormatUtf8ArgumentTypes("%t", {declarative_type},
                                                 ProductMode::PRODUCT_EXTERNAL),
              StatusIs(absl::StatusCode::kUnimplemented));
  std::string output;
  bool is_null;
  EXPECT_THAT(
      StringFormatUtf8("%t", {Value::Null(declarative_type)},
                       ProductMode::PRODUCT_EXTERNAL, &output, &is_null),
      StatusIs(absl::StatusCode::kUnimplemented));
}

using FormatOptions =
    DeclarativeTypeDescriptor::FormattingCustom::FormatOptions;

TEST(CheckStringFormat, SupportedForDeclarativeTypesWithCustomCallback) {
  TypeFactory type_factory;
  auto format_cb =
      +[](const ValueContent& value, const FormatOptions& opts) -> std::string {
    int64_t v = value.GetAs<int64_t>();
    if (opts.mode == FormatOptions::Mode::kSQLLiteral) {
      return absl::StrCat("LITERAL(", v, ")");
    }
    return absl::StrCat("TERSE(", v, ")");
  };

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* declarative_type,
      type_factory.MakeDeclarativeType(
          DeclarativeTypeDescriptor()
              .set_type_id({"NS", "T_FMT"})
              .set_display_name("t_fmt")
              .set_backing_type(type_factory.get_int64())
              .set_formatting_strategy(
                  DeclarativeTypeDescriptor::FormattingCustom(format_cb))));

  GOOGLESQL_EXPECT_OK(CheckStringFormatUtf8ArgumentTypes("%t", {declarative_type},
                                               ProductMode::PRODUCT_EXTERNAL));
  GOOGLESQL_EXPECT_OK(CheckStringFormatUtf8ArgumentTypes("%T", {declarative_type},
                                               ProductMode::PRODUCT_EXTERNAL));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value val,
                       Value::Declarative(declarative_type->AsDeclarativeType(),
                                          Value::Int64(42)));

  std::string output;
  bool is_null = false;
  GOOGLESQL_EXPECT_OK(StringFormatUtf8("%t", {val}, ProductMode::PRODUCT_EXTERNAL,
                             &output, &is_null));
  EXPECT_EQ(output, "TERSE(42)");

  GOOGLESQL_EXPECT_OK(StringFormatUtf8("%T", {val}, ProductMode::PRODUCT_EXTERNAL,
                             &output, &is_null));
  EXPECT_EQ(output, "LITERAL(42)");
}

TEST(StringFormat, GroupingWithPrecision) {
  // Test cases where precision_digits < leading_digits (case B in ConvertInt)
  // to ensure no uninitialized stack memory is read or leaked.
  std::string output;
  bool is_null = false;

  GOOGLESQL_EXPECT_OK(StringFormatUtf8("%'.2d", {values::Int64(5)},
                             ProductMode::PRODUCT_INTERNAL, &output, &is_null));
  EXPECT_EQ(output, "05");

  GOOGLESQL_EXPECT_OK(StringFormatUtf8("%'.5d", {values::Int64(1234)},
                             ProductMode::PRODUCT_INTERNAL, &output, &is_null));
  EXPECT_EQ(output, "01,234");

  GOOGLESQL_EXPECT_OK(StringFormatUtf8("%'.23o", {values::Uint64(uint64_t{1} << 63)},
                             ProductMode::PRODUCT_INTERNAL, &output, &is_null));
  EXPECT_EQ(output, "010,0000,0000,0000,0000,0000");
}

}  // namespace functions
}  // namespace googlesql
