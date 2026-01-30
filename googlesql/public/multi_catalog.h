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

#ifndef GOOGLESQL_PUBLIC_MULTI_CATALOG_H_
#define GOOGLESQL_PUBLIC_MULTI_CATALOG_H_

#include <memory>
#include <string>
#include <vector>

#include "googlesql/public/catalog.h"
#include "googlesql/public/constant.h"
#include "googlesql/public/function.h"
#include "googlesql/public/type.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace googlesql {

// Class that wraps an ordered list of Catalogs into a single Catalog.
// Optionally assumes ownership of the wrapped Catalogs.
//
// For Find*() functions, object names are looked up from the Catalogs in
// order, with the result of the first found lookup returned.
//
// For the MultiCatalog, the Find*() functions return status as:
//
// 1) NOT_FOUND - the name was not found in any catalog.
// 2) OK - the name was found and the object is returned from the first catalog
//    where it was found.
// 3) other error status - such status is returned from the first catalog
//    where it was encountered.  It indicates that the object is either invalid
//    to access (for instance, the name was found but the object is invalid),
//    or there was an internal error in the lookup mechanism.  In either case,
//    search through the catalogs stops, the error status is returned, and the
//    user query should be aborted.
//
// TODO: Consider supporting a variant mode that treats duplicate
// names in the catalogs as ambiguous, rather than returning the first.
class Procedure;
class TableValuedFunction;

class MultiCatalog : public Catalog {
 public:
  MultiCatalog(const MultiCatalog&) = delete;
  MultiCatalog& operator=(const MultiCatalog&) = delete;

  // Create a MultiCatalog from an ordered list of Catalogs.  Catalogs in the
  // list must be non-NULL or an error is returned.  Does not own the catalogs
  // in the list.  To pass ownership of the Catalogs see `AppendOwnedCatalog`
  // instead.
  static absl::Status Create(absl::string_view name,
                             const std::vector<Catalog*>& catalog_list,
                             std::unique_ptr<MultiCatalog>* multi_catalog);

  // Prepends a Catalog to the front of <catalog_list_>.  Returns a non-OK
  // status iff `catalog` is nullptr.
  absl::Status PrependCatalog(Catalog* catalog);

  // Appends a Catalog to the back of <catalog_list_>.   Returns a non-OK status
  // iff `catalog` is nullptr.
  absl::Status AppendCatalog(Catalog* catalog);

  // Similar to `PrependCatalog`, but assumes ownership.  Returns a non-OK
  // status iff `catalog` is nullptr.
  absl::Status PrependOwnedCatalog(std::unique_ptr<Catalog> catalog);

  // Similar to `AppendCatalog`, but assumes ownership.  Returns a non-OK status
  // iff `catalog` is nullptr.
  absl::Status AppendOwnedCatalog(std::unique_ptr<Catalog> catalog);

  // Removes the given catalog from this MultiCatalog.
  // Returns true if the catalog was found and removed, false otherwise.
  bool RemoveCatalog(Catalog* catalog);

  // Removes the given catalog from this MultiCatalog.
  // Returns ownership of the unique pointer to the removed catalog, or an
  // absl::NotFoundError if the catalog was not found.
  absl::StatusOr<std::unique_ptr<Catalog>> RemoveOwnedCatalog(Catalog* catalog);

  std::string FullName() const override { return name_; }

  // The Find*() functions invoke the corresponding Find*() function
  // on each catalog in 'catalog_list_', in order.  See class comments
  // for more detail.
  absl::Status FindTable(const absl::Span<const std::string>& path,
                         const Table** table,
                         const FindOptions& options = FindOptions()) override;
  absl::Status FindFunction(
      const absl::Span<const std::string>& path, const Function** function,
      const FindOptions& options = FindOptions()) override;
  absl::Status FindTableValuedFunction(
      const absl::Span<const std::string>& path,
      const TableValuedFunction** function,
      const FindOptions& options = FindOptions()) override;
  absl::Status FindProcedure(
      const absl::Span<const std::string>& path, const Procedure** procedure,
      const FindOptions& options = FindOptions()) override;
  absl::Status FindModel(const absl::Span<const std::string>& path,
                         const Model** model,
                         const FindOptions& options = FindOptions()) override;
  absl::Status FindType(const absl::Span<const std::string>& path,
                        const Type** type,
                        const FindOptions& options = FindOptions()) override;

  // Finds the longest prefix of <path> that references a Constant in any of the
  // contained catalogs, in the order in which they were given at construction
  // time. Invokes FindConstantWithPathPrefix() on each catalog. If a catalog
  // returns a constant or an error other than NOT_FOUND, any subsequent
  // catalogs are ignored. This enables name scoping among the catalogs.
  absl::Status FindConstantWithPathPrefix(
      absl::Span<const std::string> path, int* num_names_consumed,
      const Constant** constant,
      const FindOptions& options = FindOptions()) override;

  // Finds the longest prefix of <path> that represents a table among all
  // catalogs in this multi-catalog. Only when all catalog lookups fail will
  // this function return a NOT_FOUND status and a nullptr <table>.
  // TODO: Update MultiCatalog::FindConstantWithPathPrefix to find the
  // longest prefix among all catalogs and fix its debug string.
  absl::Status FindTableWithPathPrefix(absl::Span<const std::string> path,
                                       const FindOptions& options,
                                       int* num_names_consumed,
                                       const Table** table) override;

  absl::Status FindPropertyGraph(absl::Span<const std::string> path,
                                 const PropertyGraph*& property_graph,
                                 const FindOptions& options) override;

  // The Suggest*() functions look for suggestions in order based on the
  // catalog_list_, and return the first non-empty suggestion found.
  std::string SuggestTable(
      const absl::Span<const std::string>& mistyped_path) override;
  std::string SuggestFunction(
      const absl::Span<const std::string>& mistyped_path) override;
  std::string SuggestTableValuedFunction(
      const absl::Span<const std::string>& mistyped_path) override;
  std::string SuggestConstant(
      const absl::Span<const std::string>& mistyped_path) override;

  // Getter for returning the list of Catalogs in this MultiCatalog.
  const std::vector<Catalog*>& catalogs() const {
    return catalog_list_;
  }

 protected:
  // This constructor isn't public to restrict users to use the static Create()
  // method to generate new instances (which validates the input).
  MultiCatalog(absl::string_view name,
               const std::vector<Catalog*>& catalog_list)
      : name_(name), catalog_list_(catalog_list) {}

 private:
  // The name of this catalog.
  const std::string name_;

  // The ordered list of catalogs where names are looked up.
  std::vector<Catalog*> catalog_list_;

  // Storage for owned catalogs.
  std::vector<std::unique_ptr<Catalog>> owned_catalogs_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_MULTI_CATALOG_H_
