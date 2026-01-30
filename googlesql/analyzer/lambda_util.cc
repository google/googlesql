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

#include "googlesql/analyzer/lambda_util.h"

#include <algorithm>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/parser/ast_node_kind.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parse_tree_errors.h"
#include "googlesql/public/id_string.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

// Extracts lambda argument name from the `arg_expr` and appends it to `names`.
// Return error if the `arg_expr` is not a path expression with a single
// identifier.
static absl::StatusOr<IdString> ExtractArgumentNameFromExpr(
    const ASTExpression* arg_expr) {
  const ASTPathExpression* path_expr =
      arg_expr->GetAsOrNull<ASTPathExpression>();
  if (path_expr == nullptr) {
    return MakeSqlErrorAt(arg_expr)
           << "Lambda argument name must be a single identifier";
  }
  if (path_expr->num_names() != 1) {
    return MakeSqlErrorAt(arg_expr)
           << "Lambda argument name must be a single identifier";
  }
  return path_expr->name(0)->GetAsIdString();
}

// Extracts lambda argument names from `ast_lambda` and append them to `names`.
// Returns error if the argument list is not in the right shape. `names` could
// nullptr when only check is needed.
static absl::Status ExtractLambdaArgumentNames(const ASTLambda* ast_lambda,
                                               std::vector<IdString>* names) {
  ABSL_DCHECK(names != nullptr);
  const ASTExpression* args_expr = ast_lambda->argument_list();
  if (args_expr->node_kind() == AST_STRUCT_CONSTRUCTOR_WITH_PARENS) {
    const ASTStructConstructorWithParens* struct_cons =
        args_expr->GetAsOrDie<ASTStructConstructorWithParens>();
    const absl::Span<const ASTExpression* const>& fields =
        struct_cons->field_expressions();
    if (names != nullptr) {
      names->reserve(fields.size());
    }
    for (const ASTExpression* field : fields) {
      GOOGLESQL_ASSIGN_OR_RETURN(IdString name, ExtractArgumentNameFromExpr(field));
      const auto itr = std::find(names->begin(), names->end(), name);
      if (itr != names->end()) {
        return MakeSqlErrorAt(field)
               << "Lambda argument name `" << name.ToStringView()
               << "` is already defined";
      }
      names->push_back(name);
    }
    return absl::OkStatus();
  }
  if (args_expr->node_kind() == AST_PATH_EXPRESSION) {
    GOOGLESQL_ASSIGN_OR_RETURN(IdString name, ExtractArgumentNameFromExpr(args_expr));
    names->push_back(name);
    return absl::OkStatus();
  }

  return MakeSqlErrorAt(args_expr) << "Expecting lambda argument list";
}

absl::StatusOr<std::vector<IdString>> ExtractLambdaArgumentNames(
    const ASTLambda* ast_lambda) {
  std::vector<IdString> names;
  GOOGLESQL_RETURN_IF_ERROR(ExtractLambdaArgumentNames(ast_lambda, &names));
  return names;
}

absl::Status ValidateLambdaArgumentListIsIdentifierList(
    const ASTLambda* ast_lambda) {
  std::vector<IdString> names;
  return ExtractLambdaArgumentNames(ast_lambda, &names);
}

}  // namespace googlesql
