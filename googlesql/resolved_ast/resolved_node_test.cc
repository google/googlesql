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

#include "googlesql/resolved_ast/resolved_node.h"

#include <memory>
#include <utility>
#include <vector>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/type_parameters.pb.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/types/type_parameters.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/make_node_vector.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_builder.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/statusor.h"

namespace googlesql {

using ::testing::Not;
using ::absl_testing::IsOk;

static TypeParameters MakeStringTypeParameters(int max_length) {
  StringTypeParametersProto proto;
  proto.set_max_length(max_length);
  absl::StatusOr<TypeParameters> string_type_parameters_or_error =
      TypeParameters::MakeStringTypeParameters(proto);
  GOOGLESQL_EXPECT_OK(string_type_parameters_or_error.status());
  return *string_type_parameters_or_error;
}

static TypeParameters MakeNumericTypeParameters(int precision, int scale) {
  NumericTypeParametersProto proto;
  proto.set_precision(precision);
  proto.set_scale(scale);
  absl::StatusOr<TypeParameters> numeric_type_parameters_or_error =
      TypeParameters::MakeNumericTypeParameters(proto);
  GOOGLESQL_EXPECT_OK(numeric_type_parameters_or_error.status());
  return *numeric_type_parameters_or_error;
}

TEST(ResolvedColumnDefinitionTest, TestGetFullTypeParameters) {
  // Column type is STRUCT<INT64, STRING(10), ARRAY<NUMERIC(10,5)>, DATE>
  TypeFactory type_factory;
  const Type* numeric_array = nullptr;
  GOOGLESQL_EXPECT_OK(
      type_factory.MakeArrayType(type_factory.get_numeric(), &numeric_array));
  std::vector<StructType::StructField> struct_fields = {
      {"f1", type_factory.get_int64()},
      {"f2", type_factory.get_string()},
      {"f3", numeric_array},
      {"f4", type_factory.get_date()}};
  const Type* struct_type = nullptr;
  GOOGLESQL_EXPECT_OK(type_factory.MakeStructType(struct_fields, &struct_type));

  // Constructs annotation for
  // STRUCT<INT64, STRING(10), ARRAY<NUMERIC(10,5)>, DATE> type.
  // Only INT64 and STRING(10) and ARRAY<NUMERIC(10,5)> have annotations. DATE
  // has no annotation since it doesn't have type parameters and is the end of
  // the list.
  std::vector<std::unique_ptr<const ResolvedColumnAnnotations>>
      child_annotations;
  child_annotations.push_back(MakeResolvedColumnAnnotations(
      /*collation_name=*/nullptr, /*not_null=*/false, /*option_list=*/{},
      /*child_list=*/{}, TypeParameters()));
  child_annotations.push_back(MakeResolvedColumnAnnotations(
      /*collation_name=*/nullptr, /*not_null=*/false, /*option_list=*/{},
      /*child_list=*/{}, MakeStringTypeParameters(10)));
  std::vector<std::unique_ptr<const ResolvedColumnAnnotations>>
      grand_child_annotations;
  grand_child_annotations.push_back(MakeResolvedColumnAnnotations(
      /*collation_name=*/nullptr, /*not_null=*/false, /*option_list=*/{},
      /*child_list=*/{}, MakeNumericTypeParameters(10, 5)));
  child_annotations.push_back(MakeResolvedColumnAnnotations(
      /*collation_name=*/nullptr, /*not_null=*/false, /*option_list=*/{},
      std::move(grand_child_annotations), TypeParameters()));
  std::unique_ptr<const ResolvedColumnAnnotations> annotations =
      MakeResolvedColumnAnnotations(
          /*collation_name=*/nullptr, /*not_null=*/false, /*option_list=*/{},
          std::move(child_annotations), TypeParameters());

  // Constructs ResolvedColumnDefinition.
  googlesql::ResolvedColumn resolved_column1;
  std::unique_ptr<const ResolvedColumnDefinition> resolved_column_definition =
      MakeResolvedColumnDefinition("test_column", struct_type,
                                   std::move(annotations),
                                   /*is_hidden=*/false, resolved_column1,
                                   /*generated_column_info=*/{},
                                   /*default_value=*/{});

  absl::StatusOr<TypeParameters> type_parameters_or_error =
      resolved_column_definition->GetFullTypeParameters();
  GOOGLESQL_EXPECT_OK(type_parameters_or_error.status());
  EXPECT_EQ(type_parameters_or_error->DebugString(),
            "[null,(max_length=10),[(precision=10,scale=5)],null]");
}

TEST(ResolvedArrayScanTest, TestBackwardCompatibility) {
  TypeFactory type_factory;
  IdStringPool pool;

  const ArrayType* int64_array_type = types::Int64ArrayType();
  const ArrayType* string_array_type = types::StringArrayType();
  const Value array1 = values::Int64Array({1, 2, 3});
  const Value array2 = values::StringArray({"a", "b"});

  {
    // `array_expr_list` and `element_column_list` only have one element.
    std::unique_ptr<const ResolvedExpr> expr1 =
        MakeResolvedLiteral(int64_array_type, array1);
    std::vector<std::unique_ptr<const ResolvedExpr>> array_expr_list =
        MakeNodeVector(std::move(expr1));
    std::vector<ResolvedColumn> element_column_list{ResolvedColumn(
        1, pool.Make("$array"), pool.Make("$e1"), type_factory.get_int64())};

    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedArrayScan> array_scan,
                         ResolvedArrayScanBuilder()
                             .set_array_expr_list(std::move(array_expr_list))
                             .set_element_column_list(element_column_list)
                             .Build());
    // Only legacy accessor is called.
    array_scan->array_expr()->MarkFieldsAccessed();
    array_scan->element_column();
    GOOGLESQL_EXPECT_OK(array_scan->CheckFieldsAccessed());
  }

  {
    // `array_expr_list` and `element_column_list` have 2+ element.
    std::unique_ptr<const ResolvedExpr> expr1 =
        MakeResolvedLiteral(int64_array_type, array1);
    std::unique_ptr<const ResolvedExpr> expr2 =
        MakeResolvedLiteral(string_array_type, array2);

    std::vector<std::unique_ptr<const ResolvedExpr>> array_expr_list =
        MakeNodeVector(std::move(expr1), std::move(expr2));
    std::vector<ResolvedColumn> element_column_list = {
        ResolvedColumn(1, pool.Make("$array"), pool.Make("$e1"),
                       type_factory.get_int64()),
        ResolvedColumn(2, pool.Make("$array"), pool.Make("$e2"),
                       type_factory.get_string())};

    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const ResolvedArrayScan> array_scan,
                         ResolvedArrayScanBuilder()
                             .set_array_expr_list(std::move(array_expr_list))
                             .set_element_column_list(element_column_list)
                             .Build());
    // Only legacy accessors are called.
    array_scan->array_expr()->MarkFieldsAccessed();
    array_scan->element_column();
    EXPECT_THAT(array_scan->CheckFieldsAccessed(), Not(IsOk()));
    // Only one vector accessor is called.
    for (auto& array_expr : array_scan->array_expr_list()) {
      array_expr->MarkFieldsAccessed();
    }
    EXPECT_THAT(array_scan->CheckFieldsAccessed(), Not(IsOk()));
    // Both vector accessors are called.
    array_scan->element_column_list();
    GOOGLESQL_EXPECT_OK(array_scan->CheckFieldsAccessed());
  }
}

}  // namespace googlesql
