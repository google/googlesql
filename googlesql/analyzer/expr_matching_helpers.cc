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

#include "googlesql/analyzer/expr_matching_helpers.h"

#include <stddef.h>

#include <algorithm>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "googlesql/analyzer/name_scope.h"
#include "googlesql/common/graph_element_utils.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/type.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_helper.h"
#include "googlesql/resolved_ast/resolved_collation.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "absl/container/flat_hash_set.h"
#include "absl/hash/hash.h"
#include "googlesql/base/check.h"
#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

namespace {

// Returns TestIsSameExpressionForGroupByResult::kNotEqual immediately if two
// primitive fields are not equal, otherwise continue. An example use case:
//
//   RETURN_IF_PRIMITIVE_NOT_EQUAL(expr1->field1(), expr1->field1());
//   RETURN_IF_PRIMITIVE_NOT_EQUAL(expr1->field2(), expr1->field2());
//   ...
//
#define RETURN_IF_PRIMITIVE_NOT_EQUAL(lhs, rhs)             \
  if (lhs != rhs) {                                         \
    return TestIsSameExpressionForGroupByResult::kNotEqual; \
  }

// Returns TestIsSameExpressionForGroupByResult::kNotEqual immediately if two
// object fields are not equal, otherwise continue. An example use case:
//
//   RETURN_IF_OBJECT_NOT_EQUAL(expr1->field1(), expr1->field1());
//   RETURN_IF_OBJECT_NOT_EQUAL(expr1->field2(), expr1->field2());
//   ...
//
#define RETURN_IF_OBJECT_NOT_EQUAL(lhs, rhs)                \
  if (!lhs.Equals(rhs)) {                                   \
    return TestIsSameExpressionForGroupByResult::kNotEqual; \
  }

// Returns the result immediately if the result of the
// TestIsSameExpressionForGroupBy function for two expressions is kNotEquala or
// kUnknown, otherwise continue. An example use case:
//
//   RETURN_IF_EXPR_NOT_EQUAL(expr1->sub_expr(), expr2->sub_expr());
//   RETURN_IF_NOT_EQUAL(expr1->field1(), expr1->field1());
//  ...
//
#define RETURN_IF_EXPR_NOT_EQUAL(expr1, expr2, language_options)       \
  GOOGLESQL_ASSIGN_OR_RETURN(                                                    \
      TestIsSameExpressionForGroupByResult result,                     \
      TestIsSameExpressionForGroupBy(expr1, expr2, language_options)); \
  if (result != TestIsSameExpressionForGroupByResult::kEqual) {        \
    return result;                                                     \
  }

}  // namespace

absl::StatusOr<bool> IsSameExpressionForGroupBy(
    const ResolvedExpr* expr1, const ResolvedExpr* expr2,
    const LanguageOptions& language_options) {
  GOOGLESQL_ASSIGN_OR_RETURN(
      TestIsSameExpressionForGroupByResult result,
      TestIsSameExpressionForGroupBy(expr1, expr2, language_options));
  return result == TestIsSameExpressionForGroupByResult::kEqual;
}

// TODO: b/406852830 - Deprecated. Remove this function once spanner dev branch
// is migrated to pass LanguageOptions into the IsSameExpressionForGroupBy
// function.
absl::StatusOr<bool> IsSameExpressionForGroupBy(const ResolvedExpr* expr1,
                                                const ResolvedExpr* expr2) {
  return IsSameExpressionForGroupBy(expr1, expr2, LanguageOptions());
}

absl::StatusOr<TestIsSameExpressionForGroupByResult>
TestIsSameExpressionForGroupBy(const ResolvedExpr* expr1,
                               const ResolvedExpr* expr2,
                               const LanguageOptions& language_options) {
  RETURN_IF_PRIMITIVE_NOT_EQUAL(expr1->node_kind(), expr2->node_kind());
  // The type() method returns an abstract type pointer, no way to dereference
  // it and calls RETURN_IF_OBJECT_NOT_EQUAL.
  if (!expr1->type()->Equals(expr2->type())) {
    return TestIsSameExpressionForGroupByResult::kNotEqual;
  }
  // Need to make sure that all non-default fields of expressions were accessed
  // to make sure that if new fields are added, this function checks them or
  // expressions are not considered the same.
  expr1->ClearFieldsAccessed();
  expr2->ClearFieldsAccessed();

  switch (expr1->node_kind()) {
    case RESOLVED_LITERAL: {
      const ResolvedLiteral* lit1 = expr1->GetAs<ResolvedLiteral>();
      const ResolvedLiteral* lit2 = expr2->GetAs<ResolvedLiteral>();
      // Value::Equals does the right thing for GROUP BY - NULLs, NaNs, infs
      // etc are considered equal.
      RETURN_IF_OBJECT_NOT_EQUAL(lit1->value(), lit2->value());
      break;
    }
    case RESOLVED_PARAMETER: {
      const ResolvedParameter* param1 = expr1->GetAs<ResolvedParameter>();
      const ResolvedParameter* param2 = expr2->GetAs<ResolvedParameter>();
      // Parameter names are normalized, so case sensitive comparison is ok.
      RETURN_IF_PRIMITIVE_NOT_EQUAL(param1->name(), param2->name());
      RETURN_IF_PRIMITIVE_NOT_EQUAL(param1->position(), param2->position());
      break;
    }
    case RESOLVED_EXPRESSION_COLUMN: {
      const ResolvedExpressionColumn* col1 =
          expr1->GetAs<ResolvedExpressionColumn>();
      const ResolvedExpressionColumn* col2 =
          expr2->GetAs<ResolvedExpressionColumn>();
      // Engine could be case sensitive for column names, so stay conservative
      // and do case sensitive comparison.
      RETURN_IF_PRIMITIVE_NOT_EQUAL(col1->name(), col2->name());
      break;
    }
    case RESOLVED_COLUMN_REF: {
      const ResolvedColumnRef* ref1 = expr1->GetAs<ResolvedColumnRef>();
      const ResolvedColumnRef* ref2 = expr2->GetAs<ResolvedColumnRef>();
      RETURN_IF_PRIMITIVE_NOT_EQUAL(ref1->column().column_id(),
                                    ref2->column().column_id());
      break;
    }
    case RESOLVED_GET_STRUCT_FIELD: {
      const ResolvedGetStructField* str1 =
          expr1->GetAs<ResolvedGetStructField>();
      const ResolvedGetStructField* str2 =
          expr2->GetAs<ResolvedGetStructField>();
      RETURN_IF_EXPR_NOT_EQUAL(str1->expr(), str2->expr(), language_options);
      RETURN_IF_PRIMITIVE_NOT_EQUAL(str1->field_idx(), str2->field_idx());
      break;
    }
    case RESOLVED_GET_PROTO_FIELD: {
      const ResolvedGetProtoField* proto_field1 =
          expr1->GetAs<ResolvedGetProtoField>();
      const ResolvedGetProtoField* proto_field2 =
          expr2->GetAs<ResolvedGetProtoField>();
      RETURN_IF_EXPR_NOT_EQUAL(proto_field1->expr(), proto_field2->expr(),
                               language_options);
      RETURN_IF_PRIMITIVE_NOT_EQUAL(proto_field1->expr()->type()->kind(),
                                    proto_field2->expr()->type()->kind());
      RETURN_IF_PRIMITIVE_NOT_EQUAL(proto_field1->field_descriptor()->number(),
                                    proto_field2->field_descriptor()->number());
      RETURN_IF_PRIMITIVE_NOT_EQUAL(proto_field1->default_value(),
                                    proto_field2->default_value());
      RETURN_IF_PRIMITIVE_NOT_EQUAL(proto_field1->get_has_bit(),
                                    proto_field2->get_has_bit());
      RETURN_IF_PRIMITIVE_NOT_EQUAL(proto_field1->format(),
                                    proto_field2->format());
      RETURN_IF_PRIMITIVE_NOT_EQUAL(
          proto_field1->return_default_value_when_unset(),
          proto_field2->return_default_value_when_unset());
      break;
    }
    case RESOLVED_CAST: {
      const ResolvedCast* cast1 = expr1->GetAs<ResolvedCast>();
      const ResolvedCast* cast2 = expr2->GetAs<ResolvedCast>();
      RETURN_IF_EXPR_NOT_EQUAL(cast1->expr(), cast2->expr(), language_options);
      RETURN_IF_PRIMITIVE_NOT_EQUAL(cast1->return_null_on_error(),
                                    cast2->return_null_on_error());
      if ((cast1->format() == nullptr) != (cast2->format() == nullptr)) {
        return TestIsSameExpressionForGroupByResult::kNotEqual;
      }
      if (cast1->format() != nullptr) {
        RETURN_IF_EXPR_NOT_EQUAL(cast1->format(), cast2->format(),
                                 language_options);
      }

      if ((cast1->time_zone() == nullptr) != (cast2->time_zone() == nullptr)) {
        return TestIsSameExpressionForGroupByResult::kNotEqual;
      }
      if (cast1->time_zone() != nullptr) {
        RETURN_IF_EXPR_NOT_EQUAL(cast1->time_zone(), cast2->time_zone(),
                                 language_options);
      }

      if (!cast1->type_modifiers().Equals(cast2->type_modifiers())) {
        return TestIsSameExpressionForGroupByResult::kNotEqual;
      }

      break;
    }
    case RESOLVED_FUNCTION_CALL: {
      const ResolvedFunctionCall* func1 = expr1->GetAs<ResolvedFunctionCall>();
      const ResolvedFunctionCall* func2 = expr2->GetAs<ResolvedFunctionCall>();
      RETURN_IF_PRIMITIVE_NOT_EQUAL(func1->function(), func2->function());
      RETURN_IF_PRIMITIVE_NOT_EQUAL(func1->error_mode(), func2->error_mode());
      if (func1->function()->function_options().volatility ==
          FunctionEnums::VOLATILE) {
        return TestIsSameExpressionForGroupByResult::kNotEqual;
      }
      // We currently don't support checking expression equality for generic
      // arguments.
      if (!func1->generic_argument_list().empty() ||
          !func2->generic_argument_list().empty()) {
        return TestIsSameExpressionForGroupByResult::kUnknown;
      }
      const std::vector<std::unique_ptr<const ResolvedExpr>>& arg1_list =
          func1->argument_list();
      const std::vector<std::unique_ptr<const ResolvedExpr>>& arg2_list =
          func2->argument_list();
      RETURN_IF_PRIMITIVE_NOT_EQUAL(arg1_list.size(), arg2_list.size());
      for (int idx = 0; idx < arg1_list.size(); ++idx) {
        RETURN_IF_EXPR_NOT_EQUAL(arg1_list[idx].get(), arg2_list[idx].get(),
                                 language_options);
      }
      RETURN_IF_PRIMITIVE_NOT_EQUAL(func1->collation_list().size(),
                                    func2->collation_list().size());
      // If the function arguments are considered the same for group by, we
      // assume the <collation_list> in expressions should be the same. Returns
      // an internal error if that's not the case.
      for (int idx = 0; idx < func1->collation_list().size(); ++idx) {
        GOOGLESQL_RET_CHECK(func1->collation_list(idx).Equals(func2->collation_list(idx)))
            << "Different collation_list in expressions: "
            << ResolvedCollation::ToString(func1->collation_list()) << " vs "
            << ResolvedCollation::ToString(func2->collation_list());
        RETURN_IF_OBJECT_NOT_EQUAL(func1->collation_list(idx),
                                   func2->collation_list(idx));
      }
      RETURN_IF_PRIMITIVE_NOT_EQUAL(func1->generic_argument_list_size(),
                                    func2->generic_argument_list_size());
      for (int idx = 0; idx < func1->generic_argument_list_size(); ++idx) {
        const ResolvedFunctionArgument* arg1 =
            func1->generic_argument_list(idx);
        const ResolvedFunctionArgument* arg2 =
            func2->generic_argument_list(idx);
        if (arg1->expr() != nullptr && arg2->expr() != nullptr) {
          RETURN_IF_EXPR_NOT_EQUAL(arg1->expr(), arg2->expr(),
                                   language_options);
        } else if (arg1->inline_lambda() != nullptr &&
                   arg2->inline_lambda() != nullptr) {
          // Technically, `e1 -> f(e1)` is the same as `e2 -> f(e2)`, if f()
          // is not volatile, but we don't support that yet.
          RETURN_IF_EXPR_NOT_EQUAL(arg1->inline_lambda()->body(),
                                   arg2->inline_lambda()->body(),
                                   language_options);

          RETURN_IF_PRIMITIVE_NOT_EQUAL(
              arg1->inline_lambda()->parameter_list_size(),
              arg2->inline_lambda()->parameter_list_size());
          for (int idx = 0; idx < arg1->inline_lambda()->parameter_list_size();
               ++idx) {
            RETURN_IF_EXPR_NOT_EQUAL(arg1->inline_lambda()->parameter_list(idx),
                                     arg2->inline_lambda()->parameter_list(idx),
                                     language_options);
          }
        } else {
          // TODO: Support Scans, Models, Subqueries, Descriptors, etc.
          return TestIsSameExpressionForGroupByResult::kNotEqual;
        }
      }
      break;
    }
    case RESOLVED_GET_JSON_FIELD: {
      const auto* json_field1 = expr1->GetAs<ResolvedGetJsonField>();
      const auto* json_field2 = expr2->GetAs<ResolvedGetJsonField>();
      RETURN_IF_EXPR_NOT_EQUAL(json_field1->expr(), json_field2->expr(),
                               language_options);
      RETURN_IF_PRIMITIVE_NOT_EQUAL(json_field1->field_name(),
                                    json_field2->field_name());
      break;
    }
    case RESOLVED_MAKE_STRUCT: {
      if (!language_options.LanguageFeatureEnabled(
              FEATURE_MATCH_MAKE_STRUCT_IN_GROUP_BY)) {
        // If the language feature for MAKE_STRUCT support is not enabled, match
        // the behavior of the default case.
        return TestIsSameExpressionForGroupByResult::kUnknown;
      }
      const auto* make_struct1 = expr1->GetAs<ResolvedMakeStruct>();
      const auto* make_struct2 = expr2->GetAs<ResolvedMakeStruct>();
      RETURN_IF_PRIMITIVE_NOT_EQUAL(make_struct1->field_list().size(),
                                    make_struct2->field_list().size());
      for (int i = 0; i < make_struct1->field_list().size(); ++i) {
        RETURN_IF_EXPR_NOT_EQUAL(make_struct1->field_list(i),
                                 make_struct2->field_list(i), language_options);
      }
      break;
    }
    default:
      // Without explicit support for this type of expression, returns an
      // unknown comparison result.
      return TestIsSameExpressionForGroupByResult::kUnknown;
  }

  GOOGLESQL_RETURN_IF_ERROR(expr1->CheckFieldsAccessed());
  GOOGLESQL_RETURN_IF_ERROR(expr2->CheckFieldsAccessed());
  return TestIsSameExpressionForGroupByResult::kEqual;
}

// TODO: b/406852830 - Deprecated. Remove this function once spanner dev branch
// is migrated to pass LanguageOptions into the TestIsSameExpressionForGroupBy
// function.
absl::StatusOr<TestIsSameExpressionForGroupByResult>
TestIsSameExpressionForGroupBy(const ResolvedExpr* expr1,
                               const ResolvedExpr* expr2) {
  return TestIsSameExpressionForGroupBy(expr1, expr2, LanguageOptions());
}

// If `resolved_expr` is a series of one or more field accesses, unwind the
// field accesses to return the underlying expression and populate the
// `name_path` with the field names.
static const ResolvedExpr* UnwindFieldAccesses(
    const ResolvedExpr* resolved_expr, ValidNamePath* name_path,
    IdStringPool* id_string_pool) {
  while (resolved_expr->node_kind() == RESOLVED_GET_PROTO_FIELD) {
    const ResolvedGetProtoField* get_proto_field =
        resolved_expr->GetAs<ResolvedGetProtoField>();
    // NOTE - The ResolvedGetProtoField has a get_has_bit() function
    // that identifies whether this expression fetches the field value, or
    // a boolean that indicates if the value was present.  If get_has_bit()
    // is true, by convention the name of that pseudocolumn is the field
    // name with the prefix 'has_'.
    if (get_proto_field->get_has_bit()) {
      name_path->mutable_name_path()->push_back(id_string_pool->Make(
          absl::StrCat("has_", get_proto_field->field_descriptor()->name())));
    } else {
      name_path->mutable_name_path()->push_back(
          id_string_pool->Make(get_proto_field->field_descriptor()->name()));
    }
    resolved_expr = get_proto_field->expr();
  }
  while (resolved_expr->node_kind() == RESOLVED_GET_STRUCT_FIELD) {
    const ResolvedGetStructField* get_struct_field =
        resolved_expr->GetAs<ResolvedGetStructField>();
    const StructType* struct_type =
        get_struct_field->expr()->type()->AsStruct();
    name_path->mutable_name_path()->push_back(id_string_pool->Make(
        struct_type->field(get_struct_field->field_idx()).name));
    resolved_expr = get_struct_field->expr();
  }
  while (resolved_expr->node_kind() == RESOLVED_GRAPH_GET_ELEMENT_PROPERTY) {
    const ResolvedGraphGetElementProperty* get_element_property =
        resolved_expr->GetAs<ResolvedGraphGetElementProperty>();
    if (absl::StatusOr<std::string> property_name =
            GetPropertyName(get_element_property);
        property_name.ok()) {
      name_path->mutable_name_path()->push_back(
          id_string_pool->Make(*property_name));
      resolved_expr = get_element_property->expr();
    } else {
      // If we fail to get the property name then we stop unwinding.
      ABSL_DCHECK(false) << "Failed to get the property name out of expression: "
                    << resolved_expr->DebugString();
      break;
    }
  }
  std::reverse(name_path->mutable_name_path()->begin(),
               name_path->mutable_name_path()->end());
  return resolved_expr;
}

bool GetSourceColumnAndNamePath(const ResolvedExpr* resolved_expr,
                                ResolvedColumn target_column,
                                ResolvedColumn* source_column,
                                bool* is_correlated, ValidNamePath* name_path,
                                IdStringPool* id_string_pool) {
  *source_column = ResolvedColumn();
  *is_correlated = false;
  resolved_expr = UnwindFieldAccesses(resolved_expr, name_path, id_string_pool);
  if (resolved_expr->node_kind() == RESOLVED_COLUMN_REF) {
    *source_column = resolved_expr->GetAs<ResolvedColumnRef>()->column();
    *is_correlated = resolved_expr->GetAs<ResolvedColumnRef>()->is_correlated();
    name_path->set_target_column(target_column);
    return true;
  }
  return false;
}

const ResolvedArgumentRef* GetSourceArgumentRefAndNamePath(
    const ResolvedExpr* resolved_expr, ResolvedColumn target_column,
    ValidNamePath* name_path, IdStringPool* id_string_pool) {
  resolved_expr = UnwindFieldAccesses(resolved_expr, name_path, id_string_pool);
  if (resolved_expr->Is<ResolvedArgumentRef>()) {
    name_path->set_target_column(target_column);
    return resolved_expr->GetAs<ResolvedArgumentRef>();
  }
  return nullptr;
}

size_t FieldPathHash(const ResolvedExpr* expr) {
  ABSL_DCHECK(expr != nullptr);
  switch (expr->node_kind()) {
    case RESOLVED_GET_PROTO_FIELD: {
      // Note that this only hashes the top-level type (e.g. ARRAY).
      const ResolvedGetProtoField* proto_field =
          expr->GetAs<ResolvedGetProtoField>();
      return absl::Hash<std::tuple<int, int, int, size_t>>()(
          std::make_tuple(expr->node_kind(), expr->type()->kind(),
                          proto_field->field_descriptor()->number(),
                          FieldPathHash(proto_field->expr())));
    }
    case RESOLVED_GET_STRUCT_FIELD: {
      const ResolvedGetStructField* struct_field =
          expr->GetAs<ResolvedGetStructField>();
      // Note that this only hashes the top-level type (e.g. ARRAY).
      return absl::Hash<std::tuple<int, int, int, size_t>>()(std::make_tuple(
          expr->node_kind(), expr->type()->kind(), struct_field->field_idx(),
          FieldPathHash(struct_field->expr())));
    }
    case RESOLVED_COLUMN_REF:
      return absl::Hash<std::pair<int, int>>()(std::make_pair(
          expr->node_kind(),
          expr->GetAs<ResolvedColumnRef>()->column().column_id()));
    default:
      return absl::Hash<int>()(expr->node_kind());
  }
}

absl::StatusOr<bool> ExprReferencesNonCorrelatedColumn(
    const ResolvedExpr& expr) {
  GOOGLESQL_ASSIGN_OR_RETURN(absl::flat_hash_set<const ResolvedColumnRef*> column_refs,
                   CollectFreeColumnRefs(expr));

  for (const ResolvedColumnRef* column_ref : column_refs) {
    if (!column_ref->is_correlated()) {
      return true;
    }
  }
  return false;
}

static bool IsProtoOrStructFieldAccess(const ResolvedNode* node) {
  return node->node_kind() == RESOLVED_GET_PROTO_FIELD ||
         node->node_kind() == RESOLVED_GET_STRUCT_FIELD;
}

// Returns true if the `column_ref_list` contains a equal pointer to
// `column_ref`.
static bool ContainsColumnReference(
    absl::Span<const std::unique_ptr<const ResolvedColumnRef>> column_ref_list,
    const ResolvedColumnRef* column_ref) {
  for (const auto& param : column_ref_list) {
    if (param.get() == column_ref) {
      return true;
    }
  }
  return false;
}

// Returns true if column reference `child` is defined in the `parameter_list`
// of its parent node.
// REQUIRES: `child` is a key in `parent_pointer_map`.
static bool IsInParameterListOfParent(
    const ResolvedColumnRef* child,
    const ParentPointerMap& parent_pointer_map) {
  const ResolvedNode* parent = parent_pointer_map.find(child)->second;
  if (parent->node_kind() == RESOLVED_SUBQUERY_EXPR) {
    const std::vector<std::unique_ptr<const ResolvedColumnRef>>&
        parameter_list =
            parent->GetAs<ResolvedSubqueryExpr>()->parameter_list();
    if (ContainsColumnReference(parameter_list, child)) {
      return true;
    }
  }
  if (parent->node_kind() == RESOLVED_INLINE_LAMBDA) {
    const std::vector<std::unique_ptr<const ResolvedColumnRef>>&
        parameter_list =
            parent->GetAs<ResolvedInlineLambda>()->parameter_list();
    if (ContainsColumnReference(parameter_list, child)) {
      return true;
    }
  }
  return false;
}

// Returns true and populate `name_path` if we are able to find a path
// expression that starts from `column_ref` using the child node to parent node
// relationships provided by `parent_pointer_map`. Otherwise:
// - If `parent_pointer_map` is illegal, or the given `column_ref` does exist in
// the `parent_pointer_map`, the function returns an error.
// - If the given node shows up in the parameter_list of its parent node, it's
// not possible to obtain a field path, it returns false and does not set
// `name_path`.
//
// `column_ref`: The starting node to reverse traverse the resolved ast.
// `parent_pointer_map`: Contains all the reverse pointers from the child node
//     to its parent node collected from the same resolved ast.
// `name_path`: The longest name path that can be restored.
// `id_string_pool`: It is used to allocate IdStrings that is used internally.
static absl::StatusOr<bool> GetLongestProtoOrStructNamePathForPrefixMatching(
    const ResolvedColumnRef* column_ref,
    const ParentPointerMap& parent_pointer_map, ValidNamePath* name_path,
    IdStringPool* id_string_pool) {
  // Try to obtain parent node of input column ref.
  auto it = parent_pointer_map.find(column_ref);
  GOOGLESQL_RET_CHECK(it != parent_pointer_map.end())
      << "column_ref does not exist in the parent_pointer_map";
  const ResolvedNode* parent = it->second;

  // We do not find a valid parent, so the path is the column ref itself.
  if (parent == nullptr) {
    name_path->set_name_path({});
    name_path->set_target_column(column_ref->column());
    return true;
  }

  // When the parent node is ResolvedSubqueryExpr or ResolvedInlineLambda, it
  // does not belong to a reverse pointer path of a path expression. Ignore it.
  if (IsInParameterListOfParent(column_ref, parent_pointer_map)) {
    return false;
  }

  // If the parent node of the column ref does not belong to a groupable path
  // expression, the path is the column ref itself.
  if (!IsProtoOrStructFieldAccess(parent)) {
    name_path->set_name_path({});
    name_path->set_target_column(column_ref->column());
    return true;
  }

  // `parent` pointer should always point to a node in a reverse pointer path
  // of a path expression.
  const ResolvedExpr* path_start = parent->GetAs<ResolvedExpr>();
  while (true) {
    auto it = parent_pointer_map.find(path_start);
    if (it != parent_pointer_map.end() && it->second != nullptr &&
        IsProtoOrStructFieldAccess(it->second)) {
      path_start = it->second->GetAs<ResolvedExpr>();
      continue;
    }
    break;
  }
  ResolvedColumn unused_source_column;
  bool unused_is_correlated;
  // This should always returns true, otherwise there is bug in the original
  // tree of which we built parent pointer map on top or the logic of building
  // the parent pointer map.
  //
  // If `valid_name_path` comes out of any anonymous struct, it would contain
  // empty name in the name path. This case will make this function returns
  // true temporarily, but the `valid_name_path` is not expected to be useful.
  bool found = GetSourceColumnAndNamePath(
      path_start, column_ref->column(), &unused_source_column,
      &unused_is_correlated, name_path, id_string_pool);
  GOOGLESQL_RET_CHECK(found) << "could not restore a valid name path out of the parent "
                      "node found in the parent_pointer_map";
  return found;
}

absl::StatusOr<bool> AllPathsInExprHaveExpectedPrefixes(
    const ResolvedExpr& expr, const ValidFieldInfoMap& expected_prefixes,
    IdStringPool* id_string_pool) {
  GOOGLESQL_ASSIGN_OR_RETURN(
      absl::flat_hash_set<const ResolvedColumnRef*> free_column_refs,
      CollectFreeColumnRefs(expr));

  // Record the column ids of all the unbounded ColumnRefs that might reference
  // the current FROM clause scope.
  absl::flat_hash_set<ResolvedColumn> free_column_ids;
  for (const ResolvedColumnRef* column_ref : free_column_refs) {
    if (!column_ref->is_correlated()) {
      free_column_ids.insert(column_ref->column());
    }
  }

  // Build a parent pointer map out of the input expression and obtain all the
  // starting nodes in the tree so that we can reverse traverse the tree to
  // obtain field paths out of those references to the free columns.
  absl::flat_hash_set<const ResolvedColumnRef*> matched_column_refs;

  GOOGLESQL_ASSIGN_OR_RETURN(const ParentPointerMap parent_pointer_map,
                   CollectParentPointersOfUnboundedFieldAccessPaths(
                       expr, free_column_ids, matched_column_refs));

  // For each starting node, try to get its field path.
  // If we can't get a field path, this just means the column ref does not
  // belong to a valid path expression, so just ignore this column ref.
  //
  // Otherwise, check if the field path has any prefix in `valid_field_map`. If
  // there is no prefix in the map, it probably references a column in the FROM
  // scope and its field path is not already considered a post-GROUP BY column,
  // so the function returns false. Only if all column refs that are not skipped
  // successfully find their prefix in `valid_field_map` will the function
  // returns true.
  //
  // Note that, a column ref with no field access parent node contains an
  // empty field path and is a valid path.
  for (const ResolvedColumnRef* column_ref : matched_column_refs) {
    ValidNamePath name_path;
    GOOGLESQL_ASSIGN_OR_RETURN(
        bool found_name_path,
        GetLongestProtoOrStructNamePathForPrefixMatching(
            column_ref, parent_pointer_map, &name_path, id_string_pool));
    if (!found_name_path) {
      continue;
    }

    if (!expected_prefixes.ContainsColumnAndNamePathPrefix(column_ref->column(),
                                                           name_path)) {
      return false;
    }
  }
  return true;
}

bool ContainsTableArrayNamePathWithFreeColumnRef(const ResolvedExpr* node,
                                                 int* column_id) {
  if (node->node_kind() == RESOLVED_COLUMN_REF) {
    if (node->GetAs<ResolvedColumnRef>()->is_correlated()) {
      return false;
    }
    *column_id = node->GetAs<ResolvedColumnRef>()->column().column_id();
    return true;
  }
  if (node->node_kind() == RESOLVED_FLATTEN) {
    return ContainsTableArrayNamePathWithFreeColumnRef(
        node->GetAs<ResolvedFlatten>()->expr(), column_id);
  }
  if (node->node_kind() == RESOLVED_GET_PROTO_FIELD) {
    return ContainsTableArrayNamePathWithFreeColumnRef(
        node->GetAs<ResolvedGetProtoField>()->expr(), column_id);
  }
  if (node->node_kind() == RESOLVED_GET_STRUCT_FIELD) {
    return ContainsTableArrayNamePathWithFreeColumnRef(
        node->GetAs<ResolvedGetStructField>()->expr(), column_id);
  }
  if (node->node_kind() == RESOLVED_GET_JSON_FIELD) {
    return ContainsTableArrayNamePathWithFreeColumnRef(
        node->GetAs<ResolvedGetJsonField>()->expr(), column_id);
  }
  if (node->node_kind() == RESOLVED_GET_ROW_FIELD) {
    return ContainsTableArrayNamePathWithFreeColumnRef(
        node->GetAs<ResolvedGetRowField>()->expr(), column_id);
  }
  return false;
}

bool IsSameFieldPath(const ResolvedExpr* field_path1,
                     const ResolvedExpr* field_path2,
                     FieldPathMatchingOption match_option) {
  // Checking types is a useful optimization to allow returning early. However,
  // we can't rely on Type::Equals() because it considers two otherwise
  // identical proto descriptors from different descriptor pools to be
  // different. So we compare Type::Kinds instead.
  if (field_path1->node_kind() != field_path2->node_kind() ||
      field_path1->type()->kind() != field_path2->type()->kind()) {
    return false;
  }

  switch (field_path1->node_kind()) {
    case RESOLVED_GET_PROTO_FIELD: {
      const ResolvedGetProtoField* proto_field1 =
          field_path1->GetAs<ResolvedGetProtoField>();
      const ResolvedGetProtoField* proto_field2 =
          field_path2->GetAs<ResolvedGetProtoField>();

      const bool field_paths_match =
          (proto_field1->expr()->type()->kind() ==
           proto_field2->expr()->type()->kind()) &&
          proto_field1->field_descriptor()->number() ==
              proto_field2->field_descriptor()->number() &&
          proto_field1->default_value() == proto_field2->default_value() &&
          proto_field1->get_has_bit() == proto_field2->get_has_bit() &&
          proto_field1->format() == proto_field2->format() &&
          proto_field1->return_default_value_when_unset() ==
              proto_field2->return_default_value_when_unset() &&
          IsSameFieldPath(proto_field1->expr(), proto_field2->expr(),
                          match_option);
      if (match_option == FieldPathMatchingOption::kFieldPath) {
        return field_paths_match;
      }
      return field_paths_match &&
             proto_field1->type()->Equals(proto_field2->type()) &&
             proto_field1->expr()->type()->Equals(proto_field2->expr()->type());
    }
    case RESOLVED_GET_STRUCT_FIELD: {
      // Do not need to check field_expr_is_positional as it is just for
      // printing nicely.
      const ResolvedGetStructField* struct_field1 =
          field_path1->GetAs<ResolvedGetStructField>();
      const ResolvedGetStructField* struct_field2 =
          field_path2->GetAs<ResolvedGetStructField>();

      const bool field_paths_match =
          (struct_field1->expr()->type()->kind() ==
           struct_field2->expr()->type()->kind()) &&
          (struct_field1->field_idx() == struct_field2->field_idx()) &&
          IsSameFieldPath(struct_field1->expr(), struct_field2->expr(),
                          match_option);
      if (match_option == FieldPathMatchingOption::kFieldPath) {
        return field_paths_match;
      }
      return field_paths_match &&
             struct_field1->type()->Equals(struct_field2->type());
    }
    case RESOLVED_COLUMN_REF: {
      // A special case for comparing subscripted paths in UPDATE ... SET
      // statements when subscript literal values are equal.
      if (match_option == FieldPathMatchingOption::kUpdateItemElement) {
        return true;
      }
      // Ignore ResolvedColumnRef::is_correlated because it is IGNORABLE and
      // therefore semantically meaningless. If the ResolvedColumnRefs indicate
      // the same ResolvedColumn, then the expressions must be equivalent.
      return field_path1->GetAs<ResolvedColumnRef>()->column() ==
             field_path2->GetAs<ResolvedColumnRef>()->column();
    }
    case RESOLVED_ARGUMENT_REF: {
      const ResolvedArgumentRef* argument_ref1 =
          field_path1->GetAs<ResolvedArgumentRef>();
      const ResolvedArgumentRef* argument_ref2 =
          field_path2->GetAs<ResolvedArgumentRef>();
      return argument_ref1->name() == argument_ref2->name() &&
             argument_ref1->argument_kind() == argument_ref2->argument_kind() &&
             argument_ref1->type()->Equals(argument_ref2->type());
    }
    case RESOLVED_GET_JSON_FIELD: {
      const ResolvedGetJsonField* get_json_field1 =
          field_path1->GetAs<ResolvedGetJsonField>();
      const ResolvedGetJsonField* get_json_field2 =
          field_path2->GetAs<ResolvedGetJsonField>();
      const bool field_paths_match =
          get_json_field1->field_name() == get_json_field2->field_name() &&
          get_json_field1->type()->Equals(get_json_field2->type()) &&
          IsSameFieldPath(get_json_field1->expr(), get_json_field2->expr(),
                          match_option);
      return field_paths_match;
    }
    default:
      return false;
  }
}

}  // namespace googlesql
