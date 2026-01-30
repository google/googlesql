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

#ifndef GOOGLESQL_PUBLIC_TEMPLATED_SQL_FUNCTION_H_
#define GOOGLESQL_PUBLIC_TEMPLATED_SQL_FUNCTION_H_

#include <memory>
#include <string>
#include <vector>

#include "googlesql/public/function.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/parse_resume_location.h"
#include "googlesql/public/type.h"
#include "absl/base/attributes.h"
#include "absl/status/status.h"
#include "google/protobuf/descriptor.h"
#include "googlesql/base/status.h"

// This file includes interfaces and classes related to templated SQL
// Functions.  It includes a generic interface (SQLFunctionInterface), and a
// concrete implementation of that interface (SQLFunction).
//
// Note: NON-templated SQL Function objects can be found in sql_function.h.

namespace googlesql {

class FunctionProto;
class ResolvedComputedColumn;
class ResolvedExpr;

// This represents a templated function with a SQL body.
//
// The purpose of this class is to help support statements of the form
// "CREATE [AGGREGATE] FUNCTION <name>(<arguments>) AS <expr>", where the
// <arguments> may have templated types like "ANY TYPE". In this case,
// GoogleSQL cannot resolve the function expression right away and must
// defer this analysis work until later when the function is called with
// concrete argument types.
//
// Note that this class also supports CREATE AGGREGATE FUNCTION for UDAs.
// UDAs are indicated by setting 'mode' to Function::AGGREGATE in
// the constructor.  Resolving a TemplatedSQLFunction UDA produces a
// TemplatedSQLFunctionCall instance that includes not only the UDA's
// function expression, but also a list of referenced (child) aggregates
// (if any).
class TemplatedSQLFunction : public Function {
 public:
  // The name of the GoogleSQL function group for TemplatedSQLFunction. All
  // TemplatedSQLFunctions have this group name, and no other Function object
  // can have this group name.
  static const char kTemplatedSQLFunctionGroup[];

  // Creates a new templated SQL function with the name in <function_name_path>.
  // The <argument_names> should match the arguments in <signature> 1:1.
  // For now, each templated SQL function only supports exactly one signature,
  // and overloading is not supported.  The <parse_resume_location> indicates
  // the location of the function's SQL expression body.
  // TODO: Extend this to support overloading, with rules for matching
  // arguments against templated types in each signature. One reasonable
  // approach might be to prefer non-templated types first in all cases.
  TemplatedSQLFunction(const std::vector<std::string>& function_name_path,
                       const FunctionSignature& signature,
                       const std::vector<std::string>& argument_names,
                       const ParseResumeLocation& parse_resume_location,
                       Mode mode = Function::SCALAR,
                       const FunctionOptions& options = FunctionOptions());

  const std::vector<std::string>& GetArgumentNames() const {
    return argument_names_;
  }

  // If set, <resolution_catalog_> is used during resolution in order
  // to resolve the function expression. See
  // FunctionResolver::ResolveTemplatedSQLFunctionCall for more.
  // Used for templated functions inside modules (which resolve
  // against the module catalog in which the function is defined).
  void set_resolution_catalog(Catalog* resolution_catalog) {
    resolution_catalog_ = resolution_catalog;
  }

  Catalog* resolution_catalog() const { return resolution_catalog_; }

  static absl::Status Deserialize(
      const FunctionProto& proto,
      const std::vector<const google::protobuf::DescriptorPool*>& pools,
      TypeFactory* factory, std::unique_ptr<Function>* result);

  absl::Status Serialize(FileDescriptorSetMap* file_descriptor_set_map,
                         FunctionProto* proto,
                         bool omit_signatures) const override;

  ParseResumeLocation GetParseResumeLocation() const {
    return parse_resume_location_;
  }

 private:
  // If non-NULL, this Catalog is used to override the catalog when the
  // resolver runs.
  Catalog* resolution_catalog_ = nullptr;

  // The list of names of all the function arguments, in the same order that
  // they appear in the function signature.
  const std::vector<std::string> argument_names_;

  // Indicates the original SQL function expression body inside the CREATE
  // FUNCTION statement that declared this function, starting after the AS
  // keyword.  The ParseResumeLocation contains a string_view along with
  // filename and byte_offset into that string_view, where the CREATE
  // statement begins.  This allows the TemplatedSQLFunction to get access
  // to the SQL function body from the <parse_resume_location_> when needed.
  ParseResumeLocation parse_resume_location_;
};

// This is the context for a specific call to a templated SQL function. It
// contains the fully-resolved function body in context of the actual concrete
// types of the arguments provided to the function call.
class TemplatedSQLFunctionCall : public ResolvedFunctionCallInfo {
 public:
  TemplatedSQLFunctionCall(
      std::unique_ptr<const ResolvedExpr> expr,
      std::vector<std::unique_ptr<const ResolvedComputedColumn>>
          aggregate_expr_list);

  TemplatedSQLFunctionCall(const TemplatedSQLFunctionCall&) = delete;
  TemplatedSQLFunctionCall& operator=(const TemplatedSQLFunctionCall&) = delete;

  const ResolvedExpr* expr() const { return expr_.get(); }
  const std::vector<std::unique_ptr<const ResolvedComputedColumn>>&
      aggregate_expression_list() const;

  std::string DebugString() const override;

 private:
  std::unique_ptr<const ResolvedExpr> expr_;
  std::vector<std::unique_ptr<const ResolvedComputedColumn>>
      aggregate_expression_list_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_TEMPLATED_SQL_FUNCTION_H_
