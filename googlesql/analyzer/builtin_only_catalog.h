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

#ifndef GOOGLESQL_ANALYZER_BUILTIN_ONLY_CATALOG_H_
#define GOOGLESQL_ANALYZER_BUILTIN_ONLY_CATALOG_H_

#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/catalog.h"
#include "googlesql/public/property_graph.h"
#include "googlesql/public/types/type.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace googlesql {

// An implementation of googlesql::Catalog that wraps another Catalog and only
// allows lookups to return builtin objects. If the underlying Catalog returns a
// non-builtin object, this Catalog produces an error.
//
// This Catalog is used by the analyzer to ensure that only builtin objects can
// be referenced, and non-builtin objects are not referenced in the case of
// shadowing. For example, when analyzing SQL templates for builtin rewriters.
//
// By default this Catalog returns kNotFound for lookups of object types for
// which there are not GoogleSQL-builtin objects (see each Find* function
// below), however `set_allow_*` can be used to control this behavior for
// specific object types.
//
// By default this Catalog treats only GoogleSQL-builtin functions as allowed,
// however `set_allowed_function_groups` can be used to allow functions from
// specific function groups, for example to allow a function group containing
// engine-builtin functions.
class BuiltinOnlyCatalog : public Catalog {
 public:
  explicit BuiltinOnlyCatalog(const absl::string_view name,
                              Catalog& wrapped_catalog)
      : name_(name), wrapped_catalog_(wrapped_catalog) {}

  BuiltinOnlyCatalog(const BuiltinOnlyCatalog&) = delete;
  BuiltinOnlyCatalog& operator=(const BuiltinOnlyCatalog&) = delete;

  std::string FullName() const override { return name_; }

  // Find the given function in the catalog. By default, only GoogleSQL-builtin
  // functions are allowed, however `set_allowed_function_groups` can be used to
  // allow functions from specific function groups (e.g. those containing
  // engine-builtin functions).
  absl::Status FindFunction(
      const absl::Span<const std::string>& path, const Function** function,
      const FindOptions& options = FindOptions()) override;
  // Find the given type in the catalog. There are no builtin types in the
  // catalog that are relevant to rewriters, however some SQL templates for
  // BuiltinFunctionInliner rewriter may reference types. Whether or not to
  // allow type lookups can be controlled by `set_allow_types`.
  absl::Status FindType(const absl::Span<const std::string>& path,
                        const Type** type,
                        const FindOptions& options = FindOptions()) override;
  // Find the given TVF in the catalog. By default, only GoogleSQL-builtin
  // TVFs are allowed, however `set_allowed_function_groups` can be used to
  // allow TVFs from specific groups (e.g. those containing engine-builtin
  // TVFs).
  absl::Status FindTableValuedFunction(
      const absl::Span<const std::string>& path,
      const TableValuedFunction** function,
      const FindOptions& options = FindOptions()) override;
  // Find the given table in the catalog. There are no builtin tables, however
  // some engine-provided SQL templates for BuiltinFunctionInliner rewriter may
  // reference tables. Whether or not to allow table lookups can be controlled
  // by `set_allow_tables`.
  absl::Status FindTable(const absl::Span<const std::string>& path,
                         const Table** table,
                         const FindOptions& options = FindOptions()) override;
  // There are no builtin procedures, always returns kNotFound.
  absl::Status FindProcedure(
      const absl::Span<const std::string>& path, const Procedure** procedure,
      const FindOptions& options = FindOptions()) override;
  // There are no builtin models, always returns kNotFound.
  absl::Status FindModel(const absl::Span<const std::string>& path,
                         const Model** model,
                         const FindOptions& options = FindOptions()) override;
  // There are no builtin property graphs, always returns kNotFound.
  absl::Status FindPropertyGraph(absl::Span<const std::string> path,
                                 const PropertyGraph*& property_graph,
                                 const FindOptions& options) override;
  // There are no builtin constants, always returns kNotFound.
  absl::Status FindConstantWithPathPrefix(
      absl::Span<const std::string> path, int* num_names_consumed,
      const Constant** constant,
      const FindOptions& options = FindOptions()) override;

  void set_allow_tables(bool allow_tables) { allow_tables_ = allow_tables; }
  void set_allow_types(bool allow_types) { allow_types_ = allow_types; }
  void set_allowed_function_groups(
      std::vector<std::string> allowed_function_groups) {
    allowed_function_groups_ = std::move(allowed_function_groups);
  }
  void reset_allows() {
    allow_tables_ = false;
    allow_types_ = false;
    allowed_function_groups_.clear();
  }

 private:
  // The name of this catalog.
  const std::string name_;

  Catalog& wrapped_catalog_;

  // Whether or not to allow lookups for (potentially non-builtin) tables. This
  // can be enabled for rewriters which intend to reference catalog tables.
  bool allow_tables_ = false;
  // Whether or not to allow lookups for (potentially non-builtin) types. This
  // can be enabled for rewriters which intend to reference catalog types.
  bool allow_types_ = false;
  // A list of (case-sensitive) function groups which should be treated as
  // builtin and allowed to be returned from this Catalog. The expectation is
  // that this will normally be 0 or 1 elements, so a vector is used instead of
  // a set. This can be set for specific rewrites to allow engine-builtin
  // functions.
  std::vector<std::string> allowed_function_groups_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_ANALYZER_BUILTIN_ONLY_CATALOG_H_
