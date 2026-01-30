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

#ifndef GOOGLESQL_ANALYZER_INPUT_ARGUMENT_TYPE_RESOLVER_HELPER_H_
#define GOOGLESQL_ANALYZER_INPUT_ARGUMENT_TYPE_RESOLVER_HELPER_H_

#include <memory>
#include <vector>

#include "googlesql/parser/parse_tree.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/input_argument_type.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"

namespace googlesql {

// Get an InputArgumentType for a ResolvedExpr, identifying whether or not it
// is a parameter, a literal, or an analysis time constant.
//
// The `pick_default_type_for_untyped_expr` argument controls how to deal
// with an untyped input argument like a NULL or empty array literal.
// - When it is false, this function will return an untyped InputArgumentType
//   when `expr` is NULL or empty array without an explicit type.
// - When it is true, an InputArgumentType with the default type for NULL or
//   empty array will be returned.
//
// `analyzer_options` is used to evaluate ResolvedExpr that is an analysis time
// constant without an initialized value.
absl::StatusOr<InputArgumentType> GetInputArgumentTypeForExpr(
    const ResolvedExpr* expr, bool pick_default_type_for_untyped_expr,
    const AnalyzerOptions& analyzer_options);

// Returns a list of `InputArgumentType` from a list of `ASTNode` and
// `ResolvedExpr`, invoking GetInputArgumentTypeForExpr() on each of the
// `argument_ast_nodes` and `arguments`.
// This method is called before signature matching. Lambdas are not resolved
// yet. Only `argument_ast_nodes` are used to determine `InputArgumentType` for
// lambda arguments.
//
// The `pick_default_type_for_untyped_expr` argument controls how to deal
// with an untyped input argument like a NULL or empty array literal.
// - When it is false, this function will return an untyped InputArgumentType
//   when `expr` is NULL or empty array without an explicit type.
// - When it is true, an InputArgumentType with the default type for NULL or
//   empty array will be returned.
//
// `analyzer_options` is used to evaluate ResolvedExpr that is an analysis time
// constant without an initialized value.
absl::StatusOr<std::vector<InputArgumentType>>
GetInputArgumentTypesForGenericArgumentList(
    const std::vector<const ASTNode*>& argument_ast_nodes,
    absl::Span<const std::unique_ptr<const ResolvedExpr>> arguments,
    bool pick_default_type_for_untyped_expr,
    const AnalyzerOptions& analyzer_options);

}  // namespace googlesql

#endif  // GOOGLESQL_ANALYZER_INPUT_ARGUMENT_TYPE_RESOLVER_HELPER_H_
