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

#ifndef GOOGLESQL_TESTDATA_ERROR_CATALOG_H_
#define GOOGLESQL_TESTDATA_ERROR_CATALOG_H_

#include <memory>
#include <string>

#include "googlesql/public/catalog.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace googlesql {

// Catalog class that returns errors from Find*() lookups.
// Only used for testing.
class Constant;
class Function;
class Procedure;
class TableValuedFunction;
class Type;

class ErrorCatalog : public Catalog {
 public:
  // Static Create() function, which returns an error if 'code' is OK
  // (i.e., not an error code).
  static absl::Status Create(absl::StatusCode code,
                             std::unique_ptr<ErrorCatalog>* error_catalog);

  std::string FullName() const override { return "catalog_with_errors"; }

  // Catalog interface implementations that always return an error.
  using Catalog::FindTable;  // Support non-virtual signatures.
  absl::Status FindTable(const absl::Span<const std::string>& path,
                         const Table** table,
                         const FindOptions& options) override;
  using Catalog::FindTableFromString;  // Support non-virtual signatures.
  absl::Status FindTableFromString(absl::string_view table_literal,
                                   const Table** table,
                                   const FindOptions& options) override;
  using Catalog::FindModel;  // Support non-virtual signatures.
  absl::Status FindModel(const absl::Span<const std::string>& path,
                         const Model** table,
                         const FindOptions& options) override;
  using Catalog::FindFunction;  // Support non-virtual signatures.
  absl::Status FindFunction(const absl::Span<const std::string>& path,
                            const Function** function,
                            const FindOptions& options) override;
  using Catalog::FindTableValuedFunction;  // Support non-virtual signatures.
  absl::Status FindTableValuedFunction(
      const absl::Span<const std::string>& path,
      const TableValuedFunction** function,
      const FindOptions& options) override;
  using Catalog::FindProcedure;  // Support non-virtual signatures.
  absl::Status FindProcedure(const absl::Span<const std::string>& path,
                             const Procedure** procedure,
                             const FindOptions& options) override;
  using Catalog::FindType;  // Support non-virtual signatures.
  absl::Status FindType(const absl::Span<const std::string>& path,
                        const Type** type, const FindOptions& options) override;
  using Catalog::FindConstantWithPathPrefix;  // Support non-virtual signatures.
  absl::Status FindConstantWithPathPrefix(absl::Span<const std::string> path,
                                          int* num_names_consumed,
                                          const Constant** constant,
                                          const FindOptions& options) override;

 private:
  // Private constructor, forcing users to invoke the Create() method
  // which validates the error code.
  explicit ErrorCatalog(absl::StatusCode code) : error_code_(code) {}

  // The error code to use when building the return status.
  absl::StatusCode error_code_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_TESTDATA_ERROR_CATALOG_H_
