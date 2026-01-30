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

#include "googlesql/testing/test_catalog.h"

#include <string>
#include <vector>

#include "googlesql/public/catalog.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/types/type.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

TestCatalog::~TestCatalog() = default;

absl::Status TestCatalog::GetErrorForName(absl::string_view name) const {
  const absl::Status* error =
      googlesql_base::FindOrNull(errors_, absl::AsciiStrToLower(name));
  if (error != nullptr) {
    return *error;
  } else {
    return absl::OkStatus();
  }
}

absl::Status TestCatalog::GetTable(const std::string& name, const Table** table,
                                   const FindOptions& options) {
  GOOGLESQL_RETURN_IF_ERROR(GetErrorForName(name));
  return SimpleCatalog::GetTable(name, table, options);
}

absl::Status TestCatalog::GetFunction(const std::string& name,
                                      const Function** function,
                                      const FindOptions& options) {
  GOOGLESQL_RETURN_IF_ERROR(GetErrorForName(name));
  return SimpleCatalog::GetFunction(name, function, options);
}

absl::Status TestCatalog::GetType(const std::string& name, const Type** type,
                                  const FindOptions& options) {
  GOOGLESQL_RETURN_IF_ERROR(GetErrorForName(name));
  return SimpleCatalog::GetType(name, type, options);
}

absl::Status TestCatalog::GetCatalog(const std::string& name, Catalog** catalog,
                                     const FindOptions& options) {
  GOOGLESQL_RETURN_IF_ERROR(GetErrorForName(name));
  return SimpleCatalog::GetCatalog(name, catalog, options);
}

void TestCatalog::AddError(absl::string_view name, const absl::Status& error) {
  googlesql_base::InsertOrDie(&errors_, absl::AsciiStrToLower(name), error);
}

TestFunction::TestFunction(
    absl::string_view function_name, Function::Mode mode,
    const std::vector<FunctionSignature>& function_signatures)
    : Function(function_name, "TestFunction", mode, function_signatures) {}

TestFunction::~TestFunction() = default;

}  // namespace googlesql
