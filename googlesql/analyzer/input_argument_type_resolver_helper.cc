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

#include "googlesql/analyzer/input_argument_type_resolver_helper.h"

#include <memory>
#include <utility>
#include <vector>

#include "googlesql/analyzer/constant_resolver_helper.h"
#include "googlesql/common/errors.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/function.h"
#include "googlesql/public/input_argument_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

absl::StatusOr<InputArgumentType> GetInputArgumentTypeForExpr(
    const ResolvedExpr* expr, bool pick_default_type_for_untyped_expr,
    const AnalyzerOptions& analyzer_options) {
  ABSL_DCHECK(expr != nullptr);

  if (expr->type()->IsStruct() && expr->Is<ResolvedMakeStruct>()) {
    const ResolvedMakeStruct* struct_expr = expr->GetAs<ResolvedMakeStruct>();
    std::vector<InputArgumentType> field_types;
    field_types.reserve(struct_expr->field_list_size());
    for (const std::unique_ptr<const ResolvedExpr>& argument :
         struct_expr->field_list()) {
      GOOGLESQL_ASSIGN_OR_RETURN(InputArgumentType field_arg,
                       GetInputArgumentTypeForExpr(
                           argument.get(), pick_default_type_for_untyped_expr,
                           analyzer_options));
      field_types.push_back(std::move(field_arg));
    }
    // We construct a custom InputArgumentType for structs that may have
    // some literal and some non-literal fields.
    return InputArgumentType(expr->type()->AsStruct(), field_types);
  }

  // Literals that were explicitly casted (i.e., the original expression was
  // 'CAST(<literal> AS <type>)') are treated like non-literals with
  // respect to subsequent coercion.
  if (expr->Is<ResolvedLiteral>() &&
      !expr->GetAs<ResolvedLiteral>()->has_explicit_type()) {
    if (!pick_default_type_for_untyped_expr) {
      if (expr->GetAs<ResolvedLiteral>()->value().is_null()) {
        // This is a literal NULL that does not have an explicit type, so
        // it can coerce to anything.
        return InputArgumentType::UntypedNull();
      }
      // This is a literal empty array that does not have an explicit type,
      // so it can coerce to any array type.
      if (expr->GetAs<ResolvedLiteral>()->value().is_empty_array()) {
        return InputArgumentType::UntypedEmptyArray();
      }
    }
    return InputArgumentType(expr->GetAs<ResolvedLiteral>()->value());
  }

  if (expr->Is<ResolvedParameter>() &&
      expr->GetAs<ResolvedParameter>()->is_untyped()) {
    // Undeclared parameters can be coerced to any type.
    return InputArgumentType::UntypedQueryParameter();
  }

  if (expr->Is<ResolvedFunctionCall>() &&
      expr->GetAs<ResolvedFunctionCall>()->function()->FullName(
          true /* include_group */) == "GoogleSQL:error") {
    // This is an ERROR(message) function call.  We special case this to
    // make the output argument coercible to anything so expressions like
    //   IF(<condition>, <value>, ERROR("message"))
    // work for any value type.
    //
    // Note that this case does not apply if ERROR() is wrapped in a CAST, since
    // that expression has an explicit type. For example,
    // COALESCE('abc', CAST(ERROR('def') AS BYTES)) fails because BYTES does not
    // implicitly coerce to STRING.
    return InputArgumentType::UntypedNull();
  }

  bool is_query_parameter = expr->Is<ResolvedParameter>();
  bool is_literal_for_constness = expr->Is<ResolvedLiteral>();
  // TODO: b/277365877 - Upgrade this check to use `IsAnalysisConst` instead.
  if (expr->Is<ResolvedConstant>()) {
    if (!analyzer_options.language().LanguageFeatureEnabled(
            FEATURE_ANALYSIS_CONSTANT_FUNCTION_ARGUMENT)) {
      // When language feature is off, we treat the constant as a normal
      // expression for backward compatibility.
      return InputArgumentType(
          /*type=*/expr->type(), is_query_parameter, is_literal_for_constness);
    }

    // If the argument is a named constant with initialized value, we can create
    // an InputArgumentType directly from the value.
    if (expr->GetAs<ResolvedConstant>()->constant()->HasValue()) {
      return InputArgumentType(
          expr->GetAs<ResolvedConstant>()->constant()->GetValue());
    }

    // Obtains the constant value with the help of ConstantEvaluator. It
    // attempts to perform lazy evaluation against the constant body when
    // constant does not have an uninitialized value.
    absl::StatusOr<Value> constant_value = GetResolvedConstantValue(
        *expr->GetAs<ResolvedConstant>(), analyzer_options);

    if (ShouldPropagateError(constant_value.status())) {
      return constant_value.status();
    }

    // Handle runtime error. It indicates something is malfunctioning in
    // constant evaluator.
    GOOGLESQL_RET_CHECK(!absl::IsOutOfRange(constant_value.status()))
        << "Constant evaluator returns runtime error";

    return InputArgumentType(constant_value);
  }

  return InputArgumentType(expr->type(), is_query_parameter,
                           is_literal_for_constness);
}

static absl::StatusOr<InputArgumentType> GetInputArgumentTypeForGenericArgument(
    const ASTNode* argument_ast_node, const ResolvedExpr* expr,
    bool pick_default_type_for_untyped_expr,
    const AnalyzerOptions& analyzer_options) {
  ABSL_DCHECK(argument_ast_node != nullptr);

  bool expects_null_expr = argument_ast_node->Is<ASTLambda>() ||
                           argument_ast_node->Is<ASTSequenceArg>();
  if (expr == nullptr) {
    ABSL_DCHECK(expects_null_expr);
    if (argument_ast_node->Is<ASTLambda>()) {
      return InputArgumentType::LambdaInputArgumentType();
    } else if (argument_ast_node->Is<ASTSequenceArg>()) {
      return InputArgumentType::SequenceInputArgumentType();
    }
    ABSL_DCHECK(false) << "A nullptr placeholder can only be used for a lambda or "
                     "sequence argument";
  }
  ABSL_DCHECK(!expects_null_expr);
  return GetInputArgumentTypeForExpr(expr, pick_default_type_for_untyped_expr,
                                     analyzer_options);
}

absl::StatusOr<std::vector<InputArgumentType>>
GetInputArgumentTypesForGenericArgumentList(
    const std::vector<const ASTNode*>& argument_ast_nodes,
    absl::Span<const std::unique_ptr<const ResolvedExpr>> arguments,
    bool pick_default_type_for_untyped_expr,
    const AnalyzerOptions& analyzer_options) {
  ABSL_DCHECK_EQ(argument_ast_nodes.size(), arguments.size());
  std::vector<InputArgumentType> input_arguments;
  input_arguments.reserve(arguments.size());
  for (int i = 0; i < argument_ast_nodes.size(); i++) {
    GOOGLESQL_ASSIGN_OR_RETURN(InputArgumentType arg,
                     GetInputArgumentTypeForGenericArgument(
                         argument_ast_nodes[i], arguments[i].get(),
                         pick_default_type_for_untyped_expr, analyzer_options));
    input_arguments.push_back(std::move(arg));
  }
  return input_arguments;
}

}  // namespace googlesql
