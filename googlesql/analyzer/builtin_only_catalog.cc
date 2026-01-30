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

#include "googlesql/analyzer/builtin_only_catalog.h"

#include <algorithm>
#include <memory>
#include <string>

#include "googlesql/public/catalog.h"
#include "googlesql/public/function.h"
#include "googlesql/public/property_graph.h"
#include "googlesql/public/strings.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/types/type.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

absl::Status BuiltinOnlyCatalog::FindFunction(
    const absl::Span<const std::string>& path, const Function** function,
    const FindOptions& options) {
  GOOGLESQL_RET_CHECK(function != nullptr);
  GOOGLESQL_RETURN_IF_ERROR(wrapped_catalog_.FindFunction(path, function, options));
  if ((*function)->IsGoogleSQLBuiltin()) {
    // GoogleSQL-builtin functions are always allowed.
    return absl::OkStatus();
  }
  if (std::find(allowed_function_groups_.begin(),
                allowed_function_groups_.end(),
                (*function)->GetGroup()) != allowed_function_groups_.end()) {
    // Functions from an allowed function group are allowed.
    return absl::OkStatus();
  }
  *function = nullptr;
  return absl::InvalidArgumentError(
      absl::Substitute("Required built-in function \"$0\" not available",
                       IdentifierPathToString(path)));
}

absl::Status BuiltinOnlyCatalog::FindTableValuedFunction(
    const absl::Span<const std::string>& path,
    const TableValuedFunction** function, const FindOptions& options) {
  GOOGLESQL_RET_CHECK(function != nullptr);
  GOOGLESQL_RETURN_IF_ERROR(
      wrapped_catalog_.FindTableValuedFunction(path, function, options));
  if ((*function)->IsGoogleSQLBuiltin()) {
    // GoogleSQL-builtin TVFs are always allowed.
    return absl::OkStatus();
  }
  if (std::find(allowed_function_groups_.begin(),
                allowed_function_groups_.end(),
                (*function)->GetGroup()) != allowed_function_groups_.end()) {
    // TVFs from an allowed group are allowed.
    return absl::OkStatus();
  }
  *function = nullptr;
  return absl::InvalidArgumentError(
      absl::Substitute("Required built-in TVF \"$0\" not available",
                       IdentifierPathToString(path)));
}

absl::Status BuiltinOnlyCatalog::FindTable(
    const absl::Span<const std::string>& path, const Table** table,
    const FindOptions& options) {
  GOOGLESQL_RET_CHECK(table != nullptr);
  if (allow_tables_) {
    return wrapped_catalog_.FindTable(path, table, options);
  }
  *table = nullptr;
  return TableNotFoundError(path);
}

absl::Status BuiltinOnlyCatalog::FindProcedure(
    const absl::Span<const std::string>& path, const Procedure** procedure,
    const FindOptions& options) {
  GOOGLESQL_RET_CHECK(procedure != nullptr);
  *procedure = nullptr;
  return ProcedureNotFoundError(path);
}

absl::Status BuiltinOnlyCatalog::FindModel(
    const absl::Span<const std::string>& path, const Model** model,
    const FindOptions& options) {
  GOOGLESQL_RET_CHECK(model != nullptr);
  *model = nullptr;
  return ModelNotFoundError(path);
}

absl::Status BuiltinOnlyCatalog::FindType(
    const absl::Span<const std::string>& path, const Type** type,
    const FindOptions& options) {
  GOOGLESQL_RET_CHECK(type != nullptr);
  if (allow_types_) {
    return wrapped_catalog_.FindType(path, type, options);
  }
  *type = nullptr;
  return TypeNotFoundError(path);
}

absl::Status BuiltinOnlyCatalog::FindPropertyGraph(
    absl::Span<const std::string> path, const PropertyGraph*& property_graph,
    const FindOptions& options) {
  property_graph = nullptr;
  return PropertyGraphNotFoundError(path);
}

absl::Status BuiltinOnlyCatalog::FindConstantWithPathPrefix(
    const absl::Span<const std::string> path, int* num_names_consumed,
    const Constant** constant, const FindOptions& options) {
  GOOGLESQL_RET_CHECK(num_names_consumed != nullptr);
  GOOGLESQL_RET_CHECK(constant != nullptr);
  *constant = nullptr;
  *num_names_consumed = 0;
  return ConstantNotFoundError(path);
}

}  // namespace googlesql
