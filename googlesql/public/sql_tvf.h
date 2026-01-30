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

#ifndef GOOGLESQL_PUBLIC_SQL_TVF_H_
#define GOOGLESQL_PUBLIC_SQL_TVF_H_

#include <memory>
#include <string>
#include <vector>

#include "googlesql/public/catalog.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "absl/status/status.h"

// This file is includes interfaces and classes related to NON-templated SQL
// TVFs.  Currently, the only class implemented is SQLTableValuedFunction.
// We may want to add a generic interface at some point (for example,
// SQLTableValuedFunctionInterface) like we have for SQL Functions, but we
// don't have a need for that interface yet since there is only a single
// implementation.
//
// Note: Templated SQL TVF objects can be found in templated_sql_tvf.h.

namespace googlesql {

// The SQLTableValuedFunction is a sub-class of TableValuedFunction for
// *non-templated* table functions (functions whose arguments are strongly
// typed, i.e., does not include arguments having type ANY or ARBITRARY) whose
// implementation is defined with statement like:
//
// 'CREATE TABLE FUNCTION ... AS <sql query>'
//
// The current implementation only supports a single table function signature.
// TODO - Extend this implementation to support multiple different
// signatures (differing number and/or names of arguments).
class SQLTableValuedFunction : public TableValuedFunction {
 public:
  // Creates a SQLTableValuedFunction from the resolved
  // <create_tvf_statement>.  Returns an error if the
  // SQLTableValuedFunction could not be successfully created (for
  // example if the <create_tvf_statement> is not for a non-templated SQL TVF).
  //
  // Does not take ownership of <create_tvf_statement>, which must outlive
  // this class.
  static absl::Status Create(
      const ::googlesql::ResolvedCreateTableFunctionStmt* create_tvf_statement,
      std::unique_ptr<SQLTableValuedFunction>* simple_sql_tvf);

  // Creates a SQLTableValuedFunction from the resolved
  // <create_tvf_statement> and <tvf_options>.  Returns an error if the
  // SQLTableValuedFunction could not be successfully created (for
  // example if the <create_tvf_statement> is not for a non-templated SQL TVF).
  //
  // Does not take ownership of <create_tvf_statement>, which must outlive
  // this class.
  static absl::Status Create(
      const ::googlesql::ResolvedCreateTableFunctionStmt* create_tvf_statement,
      TableValuedFunctionOptions tvf_options,
      std::unique_ptr<SQLTableValuedFunction>* simple_sql_tvf);

  // Returns a signature with the <actual_arguments>, and a result
  // schema from <tvf_schema_>.
  absl::Status Resolve(
      const AnalyzerOptions* analyzer_options,
      const std::vector<TVFInputArgumentType>& actual_arguments,
      const FunctionSignature& concrete_signature, Catalog* catalog,
      TypeFactory* type_factory,
      std::shared_ptr<TVFSignature>* tvf_signature) const final;

  // Returns the associated CREATE TABLE FUNCTION statement.
  const ResolvedCreateTableFunctionStmt* ResolvedStatement() const {
    return create_tvf_statement_;
  }

  const std::vector<std::string>& GetArgumentNames() const {
    return create_tvf_statement_->argument_name_list();
  }

  const ResolvedScan* query() const { return create_tvf_statement_->query(); }

 private:
  // Constructor for valid table functions.
  explicit SQLTableValuedFunction(
      const ResolvedCreateTableFunctionStmt* create_tvf_statement,
      TableValuedFunctionOptions tvf_options)
      : TableValuedFunction(create_tvf_statement->name_path(),
                            create_tvf_statement->signature(),
                            tvf_options),
        tvf_schema_(GetQueryOutputSchema(*create_tvf_statement)),
        create_tvf_statement_(create_tvf_statement) {}

  // Returns a TVFRelation with the names and Types of columns returned by the
  // query related to <create_tvf_statement>.
  static TVFRelation GetQueryOutputSchema(
      const ResolvedCreateTableFunctionStmt& create_tvf_statement);

  // Instantiates an empty function signature.
  TVFRelation tvf_schema_;
  const ResolvedCreateTableFunctionStmt* create_tvf_statement_ = nullptr;
};

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_SQL_TVF_H_
