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

#include "googlesql/public/evaluator.h"

#include <string>

#include "googlesql/common/evaluator_registration_utils.h"
#include "absl/strings/string_view.h"

namespace googlesql {

PreparedExpression::PreparedExpression(absl::string_view sql,
                                       TypeFactory* type_factory)
    : PreparedExpressionBase(sql, type_factory) {
  internal::EnableFullEvaluatorFeatures();
}

PreparedExpression::PreparedExpression(absl::string_view sql,
                                       const EvaluatorOptions& options)
    : PreparedExpressionBase(sql, options) {
  internal::EnableFullEvaluatorFeatures();
}

PreparedExpression::PreparedExpression(const ResolvedExpr* expression,
                                       const EvaluatorOptions& options)
    : PreparedExpressionBase(expression, options) {
  internal::EnableFullEvaluatorFeatures();
}

PreparedQuery::PreparedQuery(absl::string_view sql,
                             const EvaluatorOptions& options)
    : PreparedQueryBase(sql, options) {
  internal::EnableFullEvaluatorFeatures();
}

PreparedQuery::PreparedQuery(const ResolvedQueryStmt* stmt,
                             const EvaluatorOptions& options)
    : PreparedQueryBase(stmt, options) {
  internal::EnableFullEvaluatorFeatures();
}

PreparedModify::PreparedModify(absl::string_view sql,
                               const EvaluatorOptions& options)
    : PreparedModifyBase(sql, options) {
  internal::EnableFullEvaluatorFeatures();
}

PreparedModify::PreparedModify(const ResolvedStatement* stmt,
                               const EvaluatorOptions& options)
    : PreparedModifyBase(stmt, options) {
  internal::EnableFullEvaluatorFeatures();
}

PreparedStatement::PreparedStatement(absl::string_view sql,
                                     const EvaluatorOptions& options)
    : PreparedStatementBase(sql, options) {
  internal::EnableFullEvaluatorFeatures();
}

PreparedStatement::PreparedStatement(const ResolvedStatement* stmt,
                                     const EvaluatorOptions& options)
    : PreparedStatementBase(stmt, options) {
  internal::EnableFullEvaluatorFeatures();
}

}  // namespace googlesql
