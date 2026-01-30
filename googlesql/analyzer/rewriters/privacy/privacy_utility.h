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

#ifndef GOOGLESQL_ANALYZER_REWRITERS_PRIVACY_PRIVACY_UTILITY_H_
#define GOOGLESQL_ANALYZER_REWRITERS_PRIVACY_PRIVACY_UTILITY_H_

#include <memory>
#include <vector>

#include "googlesql/analyzer/named_argument_info.h"
#include "googlesql/analyzer/query_resolver_helper.h"
#include "googlesql/analyzer/resolver.h"
#include "googlesql/proto/anon_output_with_report.pb.h"
#include "googlesql/proto/internal_error_location.pb.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/functions/differential_privacy.pb.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_enums.pb.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "googlesql/base/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/status_builder.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
// Consistently format a SQL error message that include the parse location in
// the node.
googlesql_base::StatusBuilder MakeSqlErrorAtNode(const ResolvedNode& node);

// Use the resolver to create a new function call using resolved arguments. The
// calling code must ensure that the arguments can always be coerced and
// resolved to a valid function. Any returned status is an internal error.
// TODO: jmorcos - Conditional functions should respect the side effects scope
//                nesting depth.
absl::StatusOr<std::unique_ptr<ResolvedExpr>> ResolveFunctionCall(
    absl::string_view function_name,
    std::vector<std::unique_ptr<const ResolvedExpr>> arguments,
    std::vector<NamedArgumentInfo> named_arguments, Resolver* resolver);
}  // namespace googlesql

#endif  // GOOGLESQL_ANALYZER_REWRITERS_PRIVACY_PRIVACY_UTILITY_H_
