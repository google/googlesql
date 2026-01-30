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

#include "googlesql/testdata/error_catalog.h"

#include <memory>
#include <string>

#include "googlesql/public/catalog.h"
#include "absl/status/status.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_builder.h"

namespace googlesql {

class Constant;
class Function;
class Procedure;
class TableValuedFunction;
class Type;

absl::Status ErrorCatalog::Create(
    absl::StatusCode code, std::unique_ptr<ErrorCatalog>* error_catalog) {
  GOOGLESQL_RET_CHECK_NE(absl::StatusCode::kOk, code);
  error_catalog->reset(new ErrorCatalog(code));
  return absl::OkStatus();
}

absl::Status ErrorCatalog::FindTable(const absl::Span<const std::string>& path,
                                     const Table** table,
                                     const FindOptions& options) {
  return ::googlesql_base::StatusBuilder(error_code_) << "FindTable error";
}

absl::Status ErrorCatalog::FindModel(const absl::Span<const std::string>& path,
                                     const Model** table,
                                     const FindOptions& options) {
  return ::googlesql_base::StatusBuilder(error_code_) << "FindModel error";
}

absl::Status ErrorCatalog::FindFunction(
    const absl::Span<const std::string>& path, const Function** function,
    const FindOptions& options) {
  return ::googlesql_base::StatusBuilder(error_code_) << "FindFunction error";
}

absl::Status ErrorCatalog::FindTableValuedFunction(
    const absl::Span<const std::string>& path,
    const TableValuedFunction** function, const FindOptions& options) {
  return ::googlesql_base::StatusBuilder(error_code_) << "FindTableValuedFunction error";
}

absl::Status ErrorCatalog::FindProcedure(
    const absl::Span<const std::string>& path, const Procedure** procedure,
    const FindOptions& options) {
  return ::googlesql_base::StatusBuilder(error_code_) << "FindProcedure error";
}

absl::Status ErrorCatalog::FindType(const absl::Span<const std::string>& path,
                                    const Type** type,
                                    const FindOptions& options) {
  return ::googlesql_base::StatusBuilder(error_code_) << "FindType error";
}

absl::Status ErrorCatalog::FindConstantWithPathPrefix(
    const absl::Span<const std::string> path, int* num_names_consumed,
    const Constant** constant, const FindOptions& options) {
  return ::googlesql_base::StatusBuilder(error_code_) << "FindConstant error";
}

}  // namespace googlesql
