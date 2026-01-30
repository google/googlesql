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

#include "googlesql/public/sql_constant.h"

#include <memory>
#include <string>

#include "googlesql/public/error_helpers.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "googlesql/base/ret_check.h"

namespace googlesql {

absl::Status SQLConstant::Create(
    const ResolvedCreateConstantStmt* create_constant_statement,
    std::unique_ptr<SQLConstant>* sql_constant) {
  GOOGLESQL_RET_CHECK(create_constant_statement != nullptr);
  sql_constant->reset(new SQLConstant(create_constant_statement));
  return absl::OkStatus();
}

absl::Status SQLConstant::SetEvaluationResult(
    const absl::StatusOr<Value>& evaluation_result) {
  GOOGLESQL_RET_CHECK(needs_evaluation_) << VerboseDebugString();
  if (evaluation_result.ok()) {
    GOOGLESQL_RET_CHECK(evaluation_result.value().type()->Equals(type()))
        << "\nSQLConstant type: " << type()->DebugString()
        << "\nEvaluation type:  "
        << evaluation_result.value().type()->DebugString();
  }
  evaluation_result_ = evaluation_result;
  needs_evaluation_ = false;
  return absl::OkStatus();
}

std::string SQLConstant::DebugString() const {
  std::string debug_string =
      absl::StrCat(FullName(), "=", ConstantValueDebugString());
  if (!needs_evaluation_ && !evaluation_result_.ok()) {
    absl::StrAppend(&debug_string, "\n",
                    FormatError(evaluation_result_.status()));
  }
  return debug_string;
}

std::string SQLConstant::VerboseDebugString() const {
  ABSL_CHECK_NE(create_constant_statement_, nullptr);
  return absl::StrCat(DebugString(), "\n",
                      create_constant_statement_->DebugString());
}

std::string SQLConstant::ConstantValueDebugString() const {
  if (needs_evaluation_ || !evaluation_result_.ok()) {
    return "Uninitialized value";
  }
  return evaluation_result_.value().DebugString();
}

}  // namespace googlesql
