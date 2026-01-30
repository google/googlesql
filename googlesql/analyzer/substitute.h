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

#ifndef GOOGLESQL_ANALYZER_SUBSTITUTE_H_
#define GOOGLESQL_ANALYZER_SUBSTITUTE_H_

#include <memory>
#include <string>

#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/status_builder.h"

namespace googlesql {

// Parses 'expression', which may reference names in 'variables' and 'lambdas'.
// Returns a ResolvedExpr that incorporates 'expressions', 'variables' and
// 'lambdas' into a single tree. The SQL is analyzed with 'options', 'catalog',
// and 'type_factory'. In the resulting resolved AST, we project the variable
// substitutions so that they only appear once in the resulting AST, rather than
// appearing everywhere where they are referenced.
//
// For this node to reasonably be incorporated into a valid parent AST,
// 'options' must contain a googlesql_base::SequenceNumber that is higher than the highest
// column id currently present in the parent AST or any of the 'variable'
// expressions. 'options' must also use the same arenas as the parent AST did
// during its analysis. 'options' may not have any expression columns whose
// names conflict with entries in 'variables'.
//
// 'expression' must not directly reference any positional query parameters (?)
// and it must only reference named parameters that are keys in 'lambdas'.
//
// 'variables' is a list of name->ResolvedExpr pairs. Each name is available
// when we analyze 'expression' as a column, with the same value as the
// ResolvedExpr. Note that each ResolvedExpr is only computed once. There must
// be no collision between the names in 'variables' and 'lambdas'.
//
// 'lambdas' is a map of lambda names to ResolvedInlineLambdas. Lambdas can be
// invoked by name in the SQL (see example below). The arguments to the lambda
// are a list of columns defined in the SQL, which are to replace lambda
// argument references in the lambda body. For example a named lambda such as
//     mylambda(element, offset)
// with mylambda as '(e, i)->e+i', will be equivalent to: 'element + offset'.
//
// `target_type` may be specified to coerce the expression when it is analyzed
// to the specified type.
//
// Note about naming of lambdas: there is built in protection against naming
// conflicts between the lambdas supplied to AnalyzeSubstitute and existing
// UDFS or builtin functions. The names of the lambdas in AnalyzeSubstitute
// do not appear in the final rewritten resolved AST.
//
// This is primarily designed for use in resolved AST rewrite rules. For
// example, suppose you wanted to make a rewrite rule for
//
// <map_arg>[KEY(<key_arg>)]
//
// You could use the following code to make such a substitution:
//
// AnalyzeSubstitute(options, catalog, type_factory, "R(
//     CASE
//       -- If the map is null the result is null.
//       WHEN m IS NULL THEN NULL
//       -- If the key isn't found, then it's an error.
//       ELSE IFNULL( ( SELECT value FROM UNNEST(m) WHERE key = k ),
//                    ERROR(FORMAT("key %t is not in map", k)) )
//     END)",
//     {{"m", <resolved_expr_for_map_arg>},
//      {"k", <resolved_expr_for_key_arg>}});
//
// The return value of this call will be the AST equivalent to:
//
// ( SELECT CASE WHEN m ... END
//   FROM ( SELECT map_arg AS m, key_arg as k ) )
//
// Note that actually producing this AST by hand would take possibly more than
// a hundred lines of code.
absl::StatusOr<std::unique_ptr<ResolvedExpr>> AnalyzeSubstitute(
    AnalyzerOptions options, Catalog& catalog, TypeFactory& type_factory,
    absl::string_view expression,
    const absl::flat_hash_map<std::string, const ResolvedExpr*>& variables,
    const absl::flat_hash_map<std::string, const ResolvedInlineLambda*>&
        lambdas = {},
    AnnotatedType target_type = AnnotatedType(/*type=*/nullptr,
                                              /*annotation_map=*/nullptr));

// Helper function which translates errors returned by AnalyzeSubstitute() into
// internal errors. This function is intended for rewriters who expect their
// calls to AnalyzeSubstitute() to always succeed.
//
// Typical usage:
//   GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedExpr> expr,
//      AnalyzeSubstitute(...), _.With(ExpectAnalyzeSubstituteSuccess));
absl::Status ExpectAnalyzeSubstituteSuccess(googlesql_base::StatusBuilder status_builder);

}  // namespace googlesql

#endif  // GOOGLESQL_ANALYZER_SUBSTITUTE_H_
