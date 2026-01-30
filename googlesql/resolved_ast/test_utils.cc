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

#include "googlesql/resolved_ast/test_utils.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/annotation/collation.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/simple_type.h"
#include "googlesql/public/types/simple_value.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/make_node_vector.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/base/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace testing {
// Returns a hand-constructed resolved tree representing "SELECT 1 AS x".
std::unique_ptr<ResolvedQueryStmt> MakeSelect1Stmt(IdStringPool& pool) {
  std::vector<ResolvedColumn> empty_column_list;
  std::unique_ptr<ResolvedSingleRowScan> input_scan =
      MakeResolvedSingleRowScan(empty_column_list);

  ResolvedColumn column_x =
      ResolvedColumn(1, pool.Make("tbl"), pool.Make("x"), types::Int64Type());

  std::vector<ResolvedColumn> column_list;
  column_list.push_back(column_x);

  std::unique_ptr<ResolvedProjectScan> project_scan = MakeResolvedProjectScan(
      column_list,
      MakeNodeVector(MakeResolvedComputedColumn(
          column_list.back(), MakeResolvedLiteral(Value::Int64(1)))),
      std::move(input_scan));

  std::vector<std::unique_ptr<ResolvedOutputColumn>> output_column_list =
      MakeNodeVector(MakeResolvedOutputColumn("x", column_x));

  std::unique_ptr<ResolvedQueryStmt> query_stmt =
      MakeResolvedQueryStmt(std::move(output_column_list),
                            /*is_value_table=*/false, std::move(project_scan));
  ABSL_CHECK_EQ(R"(
QueryStmt
+-output_column_list=
| +-tbl.x#1 AS x [INT64]
+-query=
  +-ProjectScan
    +-column_list=[tbl.x#1]
    +-expr_list=
    | +-x#1 := Literal(type=INT64, value=1)
    +-input_scan=
      +-SingleRowScan
)",
            absl::StrCat("\n", query_stmt->DebugString()));
  return query_stmt;
}

std::unique_ptr<ResolvedFunctionCall> WrapInFunctionCall(
    TypeFactory* type_factory, std::unique_ptr<ResolvedExpr> arg1,
    std::unique_ptr<ResolvedExpr> arg2, std::unique_ptr<ResolvedExpr> arg3) {
  const Type* int64_type = type_factory->get_int64();
  static Function* function =
      new Function("test", "test_group", Function::SCALAR);
  FunctionArgumentTypeList function_arg_types;

  std::vector<std::unique_ptr<ResolvedExpr>> args;
  if (arg1 != nullptr) {
    function_arg_types.push_back(FunctionArgumentType(arg1->type()));
    args.push_back(std::move(arg1));
  }
  if (arg2 != nullptr) {
    function_arg_types.push_back(FunctionArgumentType(arg2->type()));
    args.push_back(std::move(arg2));
  }
  if (arg3 != nullptr) {
    function_arg_types.push_back(FunctionArgumentType(arg3->type()));
    args.push_back(std::move(arg3));
  }

  FunctionSignature sig(FunctionArgumentType(int64_type), function_arg_types,
                        static_cast<int64_t>(1234));
  return MakeResolvedFunctionCall(type_factory->get_int64(), function, sig,
                                  std::move(args),
                                  ResolvedFunctionCall::DEFAULT_ERROR_MODE);
}

absl::StatusOr<std::vector<std::unique_ptr<const ResolvedExpr>>>
BuildResolvedLiteralsWithCollationForTest(
    std::vector<std::pair<std::string, std::string>> literals,
    AnalyzerOptions& analyzer_options, Catalog& catalog,
    TypeFactory& type_factory) {
  std::vector<std::unique_ptr<const ResolvedExpr>> result;

  for (const auto& [str, collation_str] : literals) {
    std::unique_ptr<ResolvedLiteral> arg =
        MakeResolvedLiteral(types::StringType(), Value::String(str),
                            /*has_explicit_type=*/true);
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedExpr> expr,
        MakeCollateCallForTest(std::move(arg), collation_str, analyzer_options,
                               catalog, type_factory));
    result.push_back(std::move(expr));
  }
  return result;
}

absl::StatusOr<const Function*> GetBuiltinFunctionFromCatalogForTest(
    absl::string_view function_name, AnalyzerOptions& analyzer_options,
    Catalog& catalog) {
  const Function* fn_out = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(catalog.FindFunction({std::string(function_name)}, &fn_out,
                                       analyzer_options.find_options()));
  GOOGLESQL_RET_CHECK(fn_out != nullptr);
  return fn_out;
}

absl::StatusOr<AnnotationMap*> GetOrCreateMutableTypeAnnotationMap(
    ResolvedFunctionCall* resolved_func, TypeFactory& type_factory) {
  GOOGLESQL_RET_CHECK(resolved_func != nullptr);
  const AnnotationMap* annotation_map = resolved_func->type_annotation_map();
  if (annotation_map == nullptr) {
    std::unique_ptr<AnnotationMap> new_annotation_map =
        AnnotationMap::Create(resolved_func->type());
    annotation_map = new_annotation_map.get();
    GOOGLESQL_ASSIGN_OR_RETURN(annotation_map,
                     // Set `normalize = false` to avoid destroying internal
                     // annotation map while transferring the ownership.
                     type_factory.TakeOwnership(std::move(new_annotation_map),
                                                /*normalize=*/false));
    resolved_func->set_type_annotation_map(annotation_map);
  }
  return const_cast<AnnotationMap*>(annotation_map);
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>> ConcatStringForTest(
    const Type* argument_type,
    std::vector<std::unique_ptr<const ResolvedExpr>>& elements,
    AnalyzerOptions& analyzer_options, Catalog& catalog,
    TypeFactory& type_factory) {
  GOOGLESQL_RET_CHECK(!elements.empty());
  GOOGLESQL_ASSIGN_OR_RETURN(const Function* concat_fn,
                   GetBuiltinFunctionFromCatalogForTest(
                       "concat", analyzer_options, catalog));
  GOOGLESQL_RET_CHECK(concat_fn != nullptr);

  GOOGLESQL_RET_CHECK(!concat_fn->signatures().empty());
  const FunctionSignature* concat_string_fn_signature =
      concat_fn->GetSignature(0);
  GOOGLESQL_RET_CHECK(concat_string_fn_signature != nullptr);

  return MakeResolvedFunctionCall(
      type_factory.MakeSimpleType(TypeKind::TYPE_STRING), concat_fn,
      *concat_string_fn_signature, std::move(elements),
      ResolvedFunctionCall::DEFAULT_ERROR_MODE);
}

absl::StatusOr<std::unique_ptr<const ResolvedExpr>> MakeCollateCallForTest(
    std::unique_ptr<const ResolvedExpr> expr, absl::string_view collation_str,
    AnalyzerOptions& analyzer_options, Catalog& catalog,
    TypeFactory& type_factory) {
  GOOGLESQL_RET_CHECK(expr != nullptr);
  GOOGLESQL_ASSIGN_OR_RETURN(const Function* collate_function,
                   GetBuiltinFunctionFromCatalogForTest(
                       "collate", analyzer_options, catalog));
  std::unique_ptr<ResolvedLiteral> collation_type =
      MakeResolvedLiteral(Value::String(collation_str));
  collation_type->set_preserve_in_literal_remover(true);

  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  const Type* type = expr->type();
  args.push_back(std::move(expr));
  args.push_back(std::move(collation_type));
  std::unique_ptr<ResolvedFunctionCall> collate_fn = MakeResolvedFunctionCall(
      type, collate_function, *collate_function->GetSignature(0),
      std::move(args), ResolvedFunctionCall::DEFAULT_ERROR_MODE);

  // Explicitly adjust annotation map for collate function.
  if (!collation_str.empty()) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        AnnotationMap * annotation_map,
        GetOrCreateMutableTypeAnnotationMap(collate_fn.get(), type_factory));
    annotation_map->SetAnnotation<CollationAnnotation>(
        SimpleValue::String(std::string(collation_str)));
  }
  return collate_fn;
}

}  // namespace testing
}  // namespace googlesql
