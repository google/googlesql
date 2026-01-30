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

#include "googlesql/common/resolution_scope.h"

#include <string>

#include "googlesql/parser/ast_node_kind.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parse_tree_errors.h"
#include "googlesql/base/case.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/ret_check.h"

namespace googlesql {

absl::StatusOr<ResolutionScope> GetResolutionScopeOption(
    const ASTStatement* stmt, ResolutionScope default_resolution_scope) {
  constexpr absl::string_view kBuiltinOption = "builtin";
  constexpr absl::string_view kGlobalOption = "global";
  const ASTOptionsList* options_list;

  switch (stmt->node_kind()) {
    case AST_CREATE_FUNCTION_STATEMENT:
    case AST_CREATE_TABLE_FUNCTION_STATEMENT: {
      const ASTCreateFunctionStmtBase* create_func_stmt =
          stmt->GetAs<const ASTCreateFunctionStmtBase>();
      options_list = create_func_stmt->options_list();
      break;
    }
    case AST_CREATE_VIEW_STATEMENT: {
      const ASTCreateViewStatementBase* create_view_stmt =
          stmt->GetAs<const ASTCreateViewStatementBase>();
      options_list = create_view_stmt->options_list();
      break;
    }
    case AST_CREATE_PROCEDURE_STATEMENT: {
      const ASTCreateProcedureStatement* create_procedure_stmt =
          stmt->GetAs<const ASTCreateProcedureStatement>();
      options_list = create_procedure_stmt->options_list();
      break;
    }
    default:
      GOOGLESQL_RET_CHECK_FAIL() << "Unsupported node kind: "
                       << stmt->GetNodeKindString();
  }

  if (options_list == nullptr) {
    return default_resolution_scope;
  }

  ResolutionScope resolution_scope = default_resolution_scope;
  bool found_allowed_references = false;

  for (const ASTOptionsEntry* option : options_list->options_entries()) {
    if (googlesql_base::CaseCompare(option->name()->GetAsStringView(),
                                             "allowed_references") != 0) {
      continue;
    }
    if (found_allowed_references) {
      return MakeSqlErrorAt(option)
             << "Option allowed_references can only occur once";
    }
    found_allowed_references = true;

    const ASTStringLiteral* ast_value =
        option->value()->GetAsOrNull<ASTStringLiteral>();
    if (ast_value == nullptr) {
      return MakeSqlErrorAt(option)
             << "Option allowed_references must be a string literal";
    }

    std::string value = absl::AsciiStrToLower(ast_value->string_value());
    if (value == kBuiltinOption) {
      resolution_scope = ResolutionScope::kBuiltin;
    } else if (value == kGlobalOption) {
      resolution_scope = ResolutionScope::kGlobal;
    } else {
      return MakeSqlErrorAt(option)
             << "Option allowed_references must be one of 'builtin' or "
                "'global'";
    }
  }
  return resolution_scope;
}

}  // namespace googlesql
