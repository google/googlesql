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

#ifndef GOOGLESQL_RESOLVED_AST_TEST_UTILS_H_
#define GOOGLESQL_RESOLVED_AST_TEST_UTILS_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/catalog.h"
#include "googlesql/public/function.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace googlesql {
namespace testing {

// Returns a hand-constructed resolved tree representing "SELECT 1 AS x".
std::unique_ptr<ResolvedQueryStmt> MakeSelect1Stmt(IdStringPool& pool);

// Wraps up to 3 expressions as arguments in a call to a test function.
std::unique_ptr<ResolvedFunctionCall> WrapInFunctionCall(
    TypeFactory* type_factory, std::unique_ptr<ResolvedExpr> arg1 = nullptr,
    std::unique_ptr<ResolvedExpr> arg2 = nullptr,
    std::unique_ptr<ResolvedExpr> arg3 = nullptr);

// Convenience function to wrap resolved expression within collate function
// for collation type provided in collation_str.
absl::StatusOr<std::unique_ptr<const ResolvedExpr>> MakeCollateCallForTest(
    std::unique_ptr<const ResolvedExpr> expr, absl::string_view collation_str,
    AnalyzerOptions& analyzer_options, Catalog& catalog,
    TypeFactory& type_factory);

// Convenience function to build a list of literals wrapped in collate function.
// `literals` is a vector of pairs <data string, collation type string>
absl::StatusOr<std::vector<std::unique_ptr<const ResolvedExpr>>>
BuildResolvedLiteralsWithCollationForTest(
    std::vector<std::pair<std::string, std::string>> literals,
    AnalyzerOptions& analyzer_options, Catalog& catalog,
    TypeFactory& type_factory);

// Convenience function to create concat function for string literals.
absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>> ConcatStringForTest(
    const Type* argument_type,
    std::vector<std::unique_ptr<const ResolvedExpr>>& elements,
    AnalyzerOptions& analyzer_options, Catalog& catalog,
    TypeFactory& type_factory);

// Convenience function to retrieve a function from catalog.
absl::StatusOr<const Function*> GetBuiltinFunctionFromCatalogForTest(
    absl::string_view function_name, AnalyzerOptions& analyzer_options,
    Catalog& catalog);

}  // namespace testing
}  // namespace googlesql

#endif  // GOOGLESQL_RESOLVED_AST_TEST_UTILS_H_
