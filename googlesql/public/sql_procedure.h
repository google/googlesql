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

#ifndef GOOGLESQL_PUBLIC_SQL_PROCEDURE_H_
#define GOOGLESQL_PUBLIC_SQL_PROCEDURE_H_

#include <memory>
#include <utility>

#include "googlesql/public/catalog.h"
#include "googlesql/public/module_details.h"
#include "googlesql/public/procedure.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "absl/memory/memory.h"
#include "absl/status/statusor.h"

namespace googlesql {

// A SQLProcedure represents a procedure that is defined using a CREATE
// PROCEDURE statement.
//
// Serialization and deserialization of procedures are handled by the base
// `Procedure` class.
class SQLProcedure : public Procedure {
 public:
  // Creates a SQLProcedure from the resolved <create_procedure_statement>.
  // Returns an error if the SQLProcedure could not be successfully created.
  // Does not take ownership of <create_procedure_statement>, which must
  // outlive this class.
  // Creates a SQLProcedure with empty module details.
  static absl::StatusOr<std::unique_ptr<SQLProcedure>> Create(
      const ResolvedCreateProcedureStmt* create_procedure_statement) {
    return absl::WrapUnique(new SQLProcedure(create_procedure_statement,
                                             ModuleDetails::CreateEmpty()));
  }

  // Same as above, but Creates a SQLProcedure with the specified
  // <module_details>.
  static absl::StatusOr<std::unique_ptr<SQLProcedure>> Create(
      const ResolvedCreateProcedureStmt* create_procedure_statement,
      ModuleDetails module_details) {
    return absl::WrapUnique(new SQLProcedure(create_procedure_statement,
                                             std::move(module_details)));
  }

  ~SQLProcedure() override = default;

  // This class is neither copyable nor assignable.
  SQLProcedure(const SQLProcedure&) = delete;
  SQLProcedure& operator=(const SQLProcedure&) = delete;

  const ResolvedCreateProcedureStmt* resolved_statement() const {
    return create_procedure_statement_;
  }

  const ModuleDetails& module_details() const { return module_details_; }

 private:
  explicit SQLProcedure(
      const ResolvedCreateProcedureStmt* create_procedure_statement,
      ModuleDetails module_details)
      : Procedure(create_procedure_statement->name_path(),
                  create_procedure_statement->signature()),
        create_procedure_statement_(create_procedure_statement),
        module_details_(std::move(module_details)) {}

  // Not owned.
  const ResolvedCreateProcedureStmt* create_procedure_statement_;

  // Details about the containing GoogleSQL module.
  const ModuleDetails module_details_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_SQL_PROCEDURE_H_
