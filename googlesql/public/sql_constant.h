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

#ifndef GOOGLESQL_PUBLIC_SQL_CONSTANT_H_
#define GOOGLESQL_PUBLIC_SQL_CONSTANT_H_

#include <memory>
#include <string>
#include <vector>

#include "googlesql/public/constant.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "googlesql/base/ret_check.h"

namespace googlesql {

// A SQLConstant represents a constant that is derived from a resolved SQL
// expression (e.g., with CREATE CONSTANT), where the constant's Value is
// determined by evaluating that SQL expression.
//
// A SQLConstant can be in one of three states (as enforced by this class):
// 1) A SQL Constant that has not been evaluated yet.  This Constant has
//    <needs_evaluation_> = true.
// 2) A SQL Constant that failed evaluation.  This Constant has
//    <needs_evaluation_> = false and <evaluation_result_> is an error Status.
// 3) A SQL Constant that was successfully evaluated.  This Constant has
//    <needs_evaluation_> = false and <evaluation_result_> is a valid Value.
//
class SQLConstant : public Constant {
 public:
  // Creates a SQLConstant from the resolved <create_constant_statement>.
  // Returns an error if the SQLConstant could not be successfully created.
  // Does not take ownership of <create_constant_statement>, which must
  // outlive this class.
  static absl::Status Create(
      const ::googlesql::ResolvedCreateConstantStmt* create_constant_statement,
      std::unique_ptr<SQLConstant>* sql_constant);

  ~SQLConstant() override = default;

  // This class is neither copyable nor assignable.
  SQLConstant(const SQLConstant& other_constant_with_value) = delete;
  SQLConstant& operator=(const SQLConstant& other_constant_with_value) = delete;

  // Returns the Type of the resolved Constant based on its resolved
  // expression type.
  const Type* type() const override {
    return constant_expression()->type();
  }

  // Returns whether or not the SQLConstant needs evaluation.
  bool needs_evaluation() const {
    return needs_evaluation_;
  }

  // Returns the SQLConstant's evaluation Status.
  // Must only be called if the SQLConstant has already been evaluated or
  // an internal error is returned.  An OK status indicates that the
  // SQLConstant was evaluated successfully.  Otherwise, a (non-internal)
  // error status indicates that the SQLConstant's SQL expression evaluation
  // failed (and therefore referencing the SQLConstant in a query must cause
  // the query to fail).
  absl::StatusOr<Value> evaluation_result() const {
    GOOGLESQL_RET_CHECK(!needs_evaluation_) << VerboseDebugString();
    return evaluation_result_;
  }

  // Sets the SQLConstant's evaluation result.  This can either be a valid
  // Value (if the SQLConstant expression evaluated successfully) or an
  // error Status.  Can only be called once (i.e., when <needs_evaluation_>),
  // or an internal error is returned.
  absl::Status SetEvaluationResult(
      const absl::StatusOr<Value>& evaluation_result);

  // Returns the Constant's resolved function expression body.  Guaranteed
  // to be non-NULL.
  const ResolvedExpr* constant_expression() const {
    return create_constant_statement_->expr();
  }

  bool HasValue() const override {
    return !needs_evaluation_ && evaluation_result_.ok();
  }
  absl::StatusOr<Value> GetValue() const override {
    GOOGLESQL_RET_CHECK(!needs_evaluation_);
    // If the constant evaluated to an error, HasValue() should be false
    // and GetValue() should not be called.
    GOOGLESQL_RET_CHECK_OK(evaluation_result_.status());
    return evaluation_result_;
  }

  std::string DebugString() const override;

  // Returns a debug string that includes the CREATE CONSTANT statement's
  // ResolvedAST.
  std::string VerboseDebugString() const;

  std::string ConstantValueDebugString() const override;

  const ParseLocationRange* GetParseLocationRange() const {
    return create_constant_statement_->GetParseLocationRangeOrNULL();
  }

 private:
  explicit SQLConstant(
      const ResolvedCreateConstantStmt* create_constant_statement)
      : Constant(create_constant_statement->name_path()),
        create_constant_statement_(create_constant_statement),
        needs_evaluation_(true) {}

  const ResolvedCreateConstantStmt* create_constant_statement_;  // Not owned.
  bool needs_evaluation_;
  absl::StatusOr<Value> evaluation_result_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_SQL_CONSTANT_H_
