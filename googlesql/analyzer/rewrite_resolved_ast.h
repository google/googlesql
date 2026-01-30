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

#ifndef GOOGLESQL_ANALYZER_REWRITE_RESOLVED_AST_H_
#define GOOGLESQL_ANALYZER_REWRITE_RESOLVED_AST_H_

#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/types/type_factory.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace googlesql {
// Similar to RewriteResolvedAst() in analyzer.h, except that it does
// not register rewriters before executing. Instead, it assumes that all the
// rewriters have already been registered. This is to prevent a dependency
// cycle of the following form:
//
// (some rewriter class) -> internal analyzer -> InternalRewriteResolvedAst ->
// RegisterAllRewriters -> (some rewriter class)
absl::Status InternalRewriteResolvedAst(const AnalyzerOptions& analyzer_options,
                                        absl::string_view sql, Catalog* catalog,
                                        TypeFactory* type_factory,
                                        AnalyzerOutput& analyzer_output);
}  // namespace googlesql

#endif  // GOOGLESQL_ANALYZER_REWRITE_RESOLVED_AST_H_
