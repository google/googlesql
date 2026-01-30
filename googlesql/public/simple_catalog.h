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

#ifndef GOOGLESQL_PUBLIC_SIMPLE_CATALOG_H_
#define GOOGLESQL_PUBLIC_SIMPLE_CATALOG_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/public/builtin_function.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/constant.h"
#include "googlesql/public/evaluator_table_iterator.h"
#include "googlesql/public/function.h"
#include "googlesql/public/procedure.h"
#include "googlesql/public/property_graph.h"
#include "googlesql/public/sql_view.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/type.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/type_deserializer.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "absl/base/nullability.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "googlesql/base/case.h"
#include "google/protobuf/descriptor.h"
#include "googlesql/base/ret_check.h"

namespace googlesql {

class TestDatabaseCatalog;
class ResolvedScan;
class SimpleCatalogProto;
class SimpleColumn;
class SimpleColumnProto;
class SimpleConnectionProto;
class SimpleConstantProto;
class SimpleModelProto;
class SimpleSequenceProto;
class SimpleTableProto;

// SimpleCatalog is a concrete implementation of the Catalog interface.
// It acts as a simple container for objects in the Catalog.
//
// This class is thread-safe.
class SimpleCatalog : public EnumerableCatalog {
 public:
  // Construct a Catalog with catalog name <name>.
  //
  // If <type_factory> is non-NULL, it will be stored (unowned) with this
  // SimpleCatalog and used to allocate any Types needed for the Catalog.
  // If <type_factory> is NULL, an owned TypeFactory will be constructed
  // internally when needed.
  explicit SimpleCatalog(absl::string_view name,
                         TypeFactory* type_factory = nullptr);
  SimpleCatalog(const SimpleCatalog&) = delete;
  SimpleCatalog& operator=(const SimpleCatalog&) = delete;

  std::string FullName() const override { return name_; }

  absl::Status GetTable(const std::string& name, const Table** table,
                        const FindOptions& options = FindOptions()) override
      ABSL_LOCKS_EXCLUDED(mutex_);

  absl::Status GetModel(const std::string& name, const Model** model,
                        const FindOptions& options = FindOptions()) override
      ABSL_LOCKS_EXCLUDED(mutex_);

  absl::Status GetConnection(const std::string& name,
                             const Connection** connection,
                             const FindOptions& options) override
      ABSL_LOCKS_EXCLUDED(mutex_);

  absl::Status GetSequence(const std::string& name, const Sequence** sequence,
                           const FindOptions& options) override
      ABSL_LOCKS_EXCLUDED(mutex_);

  absl::Status GetFunction(const std::string& name, const Function** function,
                           const FindOptions& options = FindOptions()) override
      ABSL_LOCKS_EXCLUDED(mutex_);

  absl::Status GetTableValuedFunction(
      const std::string& name, const TableValuedFunction** function,
      const FindOptions& options = FindOptions()) override
      ABSL_LOCKS_EXCLUDED(mutex_);

  absl::Status GetProcedure(
      const std::string& name, const Procedure** procedure,
      const FindOptions& options = FindOptions()) override;

  absl::Status GetType(const std::string& name, const Type** type,
                       const FindOptions& options = FindOptions()) override
      ABSL_LOCKS_EXCLUDED(mutex_);

  absl::Status GetCatalog(const std::string& name, Catalog** catalog,
                          const FindOptions& options = FindOptions()) override
      ABSL_LOCKS_EXCLUDED(mutex_);

  absl::Status GetConstant(const std::string& name, const Constant** constant,
                           const FindOptions& options = FindOptions()) override
      ABSL_LOCKS_EXCLUDED(mutex_);

  absl::Status GetPropertyGraph(absl::string_view name,
                                const PropertyGraph*& property_graph)
      ABSL_LOCKS_EXCLUDED(mutex_) {
    return GetPropertyGraph(name, property_graph, FindOptions());
  }

  absl::Status GetPropertyGraph(absl::string_view name,
                                const PropertyGraph*& property_graph,
                                const FindOptions& options) override
      ABSL_LOCKS_EXCLUDED(mutex_);

  // For suggestions we look from the last level of <mistyped_path>:
  //  - Whether the object exists directly in sub-catalogs.
  //  - If not above, whether there is a single name that's misspelled in the
  //    current catalog.
  std::string SuggestTable(
      const absl::Span<const std::string>& mistyped_path) override;
  std::string SuggestFunction(
      const absl::Span<const std::string>& mistyped_path) override;
  std::string SuggestTableValuedFunction(
      const absl::Span<const std::string>& mistyped_path) override;
  std::string SuggestConstant(
      const absl::Span<const std::string>& mistyped_path) override;
  std::string SuggestPropertyGraph(
      absl::Span<const std::string> mistyped_path) override;

  // TODO: Implement SuggestModel function.
  // TODO: Implement SuggestConnection function.

  // Add objects to the SimpleCatalog.
  //  - Names must be unique (case-insensitively) or the call will die.
  //  - For Add* methods, caller maintains ownership of all added objects, which
  // must outlive this catalog.
  //  - AddOwned* methods transfer ownership of the added object to the catalog.
  //  - Methods that do not take name of the object as a parameter use object's
  // own name.
  //  - *IfNotPresent methods attempt to add the object to the catalog, and
  // return false if the object is already present.
  //  - AddOwned*IfNotPresent methods will take ownership only if they
  // successfully added the object to the catalog.
  //  - AddOwned*IfNotPresent methods that take unique_ptr as a parameter
  //  will take ownership if they successfully added the object to the catalog,
  //  or deallocate the object otherwise.
  //
  // NOTE: Consider using unique_ptr version of AddOwned* methods where
  // available.

  // Tables
  void AddTable(absl::string_view name, const Table* table)
      ABSL_LOCKS_EXCLUDED(mutex_);
  void AddTable(const Table* table) ABSL_LOCKS_EXCLUDED(mutex_);
  void AddOwnedTable(absl::string_view name, std::unique_ptr<const Table> table)
      ABSL_LOCKS_EXCLUDED(mutex_);
  bool AddOwnedTableIfNotPresent(absl::string_view name,
                                 std::unique_ptr<const Table> table)
      ABSL_LOCKS_EXCLUDED(mutex_);
  void AddOwnedTable(std::unique_ptr<const Table> table)
      ABSL_LOCKS_EXCLUDED(mutex_);
  void AddOwnedTable(absl::string_view name, const Table* table);
  void AddOwnedTable(const Table* table) ABSL_LOCKS_EXCLUDED(mutex_);

  // Models
  void AddModel(absl::string_view name, const Model* model)
      ABSL_LOCKS_EXCLUDED(mutex_);
  void AddModel(const Model* model) ABSL_LOCKS_EXCLUDED(mutex_);
  void AddOwnedModel(absl::string_view name, std::unique_ptr<const Model> model)
      ABSL_LOCKS_EXCLUDED(mutex_);
  void AddOwnedModel(std::unique_ptr<const Model> model)
      ABSL_LOCKS_EXCLUDED(mutex_);
  void AddOwnedModel(absl::string_view name, const Model* model);
  void AddOwnedModel(const Model* model) ABSL_LOCKS_EXCLUDED(mutex_);
  bool AddOwnedModelIfNotPresent(std::unique_ptr<const Model> model)
      ABSL_LOCKS_EXCLUDED(mutex_);

  // Connections
  void AddConnection(absl::string_view name, const Connection* connection)
      ABSL_LOCKS_EXCLUDED(mutex_);
  void AddConnection(const Connection* connection) ABSL_LOCKS_EXCLUDED(mutex_);
  void AddOwnedConnection(absl::string_view name,
                          std::unique_ptr<const Connection> connection)
      ABSL_LOCKS_EXCLUDED(mutex_);
  void AddOwnedConnection(std::unique_ptr<const Connection> connection)
      ABSL_LOCKS_EXCLUDED(mutex_);
  bool AddOwnedConnectionIfNotPresent(
      std::unique_ptr<const Connection> connection) ABSL_LOCKS_EXCLUDED(mutex_);

  // Sequences
  void AddSequence(absl::string_view name, const Sequence* sequence)
      ABSL_LOCKS_EXCLUDED(mutex_);
  void AddSequence(const Sequence* sequence) ABSL_LOCKS_EXCLUDED(mutex_);
  bool AddOwnedSequenceIfNotPresent(std::unique_ptr<const Sequence> sequence)
      ABSL_LOCKS_EXCLUDED(mutex_);

  // Types
  void AddType(absl::string_view name, const Type* type)
      ABSL_LOCKS_EXCLUDED(mutex_);
  bool AddTypeIfNotPresent(absl::string_view name, const Type* type)
      ABSL_LOCKS_EXCLUDED(mutex_);

  // Catalogs
  void AddCatalog(absl::string_view name, Catalog* catalog)
      ABSL_LOCKS_EXCLUDED(mutex_);
  void AddCatalog(Catalog* catalog) ABSL_LOCKS_EXCLUDED(mutex_);
  void AddOwnedCatalog(absl::string_view name,
                       std::unique_ptr<Catalog> catalog);
  // TODO: Cleanup callers and delete
  void AddOwnedCatalog(std::unique_ptr<Catalog> catalog)
      ABSL_LOCKS_EXCLUDED(mutex_);
  void AddOwnedCatalog(absl::string_view name, Catalog* catalog);
  void AddOwnedCatalog(Catalog* catalog) ABSL_LOCKS_EXCLUDED(mutex_);
  bool AddOwnedCatalogIfNotPresent(absl::string_view name,
                                   std::unique_ptr<Catalog> catalog)
      ABSL_LOCKS_EXCLUDED(mutex_);

  // Add a new (owned) SimpleCatalog named <name>, and return it.
  SimpleCatalog* MakeOwnedSimpleCatalog(absl::string_view name)
      ABSL_LOCKS_EXCLUDED(mutex_);

  // Functions
  void AddFunction(absl::string_view name, const Function* function)
      ABSL_LOCKS_EXCLUDED(mutex_);
  void AddFunction(const Function* function) ABSL_LOCKS_EXCLUDED(mutex_);
  void AddOwnedFunction(absl::string_view name,
                        std::unique_ptr<const Function> function);
  void AddOwnedFunction(std::unique_ptr<const Function> function)
      ABSL_LOCKS_EXCLUDED(mutex_);
  // Return true if and takes ownership if actually inserted.
  bool AddOwnedFunctionIfNotPresent(absl::string_view name,
                                    std::unique_ptr<Function>* function);
  // Return true if and takes ownership if actually inserted.
  bool AddOwnedFunctionIfNotPresent(std::unique_ptr<Function>* function);
  void AddOwnedFunction(absl::string_view name, const Function* function);
  void AddOwnedFunction(const Function* function) ABSL_LOCKS_EXCLUDED(mutex_);

  // Table Valued Functions
  void AddTableValuedFunction(absl::string_view name,
                              const TableValuedFunction* function)
      ABSL_LOCKS_EXCLUDED(mutex_);
  void AddTableValuedFunction(const TableValuedFunction* function)
      ABSL_LOCKS_EXCLUDED(mutex_);
  void AddOwnedTableValuedFunction(
      absl::string_view name,
      std::unique_ptr<const TableValuedFunction> function);
  void AddOwnedTableValuedFunction(
      std::unique_ptr<const TableValuedFunction> function);
  bool AddOwnedTableValuedFunctionIfNotPresent(
      absl::string_view name,
      std::unique_ptr<TableValuedFunction>* table_function);
  bool AddOwnedTableValuedFunctionIfNotPresent(
      std::unique_ptr<TableValuedFunction>* table_function);
  void AddOwnedTableValuedFunction(absl::string_view name,
                                   const TableValuedFunction* function);
  void AddOwnedTableValuedFunction(const TableValuedFunction* function)
      ABSL_LOCKS_EXCLUDED(mutex_);

  // Procedures
  void AddProcedure(absl::string_view name, const Procedure* procedure);
  void AddProcedure(const Procedure* procedure) ABSL_LOCKS_EXCLUDED(mutex_);
  void AddOwnedProcedure(absl::string_view name,
                         std::unique_ptr<const Procedure> procedure);
  void AddOwnedProcedure(std::unique_ptr<const Procedure> procedure)
      ABSL_LOCKS_EXCLUDED(mutex_);
  bool AddOwnedProcedureIfNotPresent(std::unique_ptr<Procedure> procedure)
      ABSL_LOCKS_EXCLUDED(mutex_);
  void AddOwnedProcedure(absl::string_view name, const Procedure* procedure);
  void AddOwnedProcedure(const Procedure* procedure)
      ABSL_LOCKS_EXCLUDED(mutex_);

  // Constants
  void AddConstant(absl::string_view name, const Constant* constant);
  void AddConstant(const Constant* constant) ABSL_LOCKS_EXCLUDED(mutex_);
  void AddOwnedConstant(absl::string_view name,
                        std::unique_ptr<const Constant> constant);
  void AddOwnedConstant(std::unique_ptr<const Constant> constant)
      ABSL_LOCKS_EXCLUDED(mutex_);
  void AddOwnedConstant(absl::string_view name, const Constant* constant);
  void AddOwnedConstant(const Constant* constant) ABSL_LOCKS_EXCLUDED(mutex_);
  bool AddOwnedConstantIfNotPresent(std::unique_ptr<const Constant> constant)
      ABSL_LOCKS_EXCLUDED(mutex_);

  // Property Graphs
  void AddPropertyGraph(absl::string_view name,
                        const PropertyGraph* property_graph)
      ABSL_LOCKS_EXCLUDED(mutex_);
  void AddPropertyGraph(const PropertyGraph* property_graph)
      ABSL_LOCKS_EXCLUDED(mutex_);
  void AddOwnedPropertyGraph(
      absl::string_view, std::unique_ptr<const PropertyGraph> property_graph)
      ABSL_LOCKS_EXCLUDED(mutex_);
  void AddOwnedPropertyGraph(
      std::unique_ptr<const PropertyGraph> property_graph)
      ABSL_LOCKS_EXCLUDED(mutex_);
  bool AddOwnedPropertyGraphIfNotPresent(
      absl::string_view name,
      std::unique_ptr<const PropertyGraph> property_graph)
      ABSL_LOCKS_EXCLUDED(mutex_);

  // Add GoogleSQL built-in function definitions into this catalog. `options`
  // can be used to select which functions get loaded. See
  // builtin_function_options.h. Provided such functions are specified by
  // `options`, this can add functions in both the global namespace and more
  // specific namespaces. If any of the selected functions are in namespaces,
  // sub-Catalogs will be created and the appropriate functions will be added in
  // those sub-Catalogs.
  // Also: Functions and Catalogs with the same names must not already exist.
  void AddBuiltinFunctions(const BuiltinFunctionOptions& options)
      ABSL_LOCKS_EXCLUDED(mutex_);

  // DEPRECATED - As above but using the old name.
  ABSL_DEPRECATED("Inline me!")
  void AddGoogleSQLFunctions(const BuiltinFunctionOptions& options)
      ABSL_LOCKS_EXCLUDED(mutex_) {
    AddBuiltinFunctions(options);
  }

  // DEPRECATED - As above but using the old name and no argument.
  ABSL_DEPRECATED("Inline me!")
  void AddGoogleSQLFunctions() ABSL_LOCKS_EXCLUDED(mutex_) {
    AddBuiltinFunctions(BuiltinFunctionOptions::AllReleasedFunctions());
  }

  // DEPRECATED - As above but taking `LanguageOptions` directly.
  ABSL_DEPRECATED("Inline me!")
  void AddGoogleSQLFunctions(const LanguageOptions& options)
      ABSL_LOCKS_EXCLUDED(mutex_) {
    AddBuiltinFunctions(BuiltinFunctionOptions(options));
  }

  // Adds GoogleSQL built-in function signatures into this catalog along with
  // named types that those signature require. `options` can be used to select
  // which functions get loaded. See builtin_function_options.h. Provided such
  // functions are specified by `options`, this can add functions in both the
  // global namespace and more specific namespaces. If any of the selected
  // functions are in namespaces, sub-Catalogs will be created and the
  // appropriate functions will be added in those sub-Catalogs.
  // Also: Functions and Catalogs with the same names must not already exist.
  absl::Status AddBuiltinFunctionsAndTypes(
      const BuiltinFunctionOptions& options) ABSL_LOCKS_EXCLUDED(mutex_);

  // DEPRECATED - As above but using the old name.
  ABSL_DEPRECATED("Inline me!")
  absl::Status AddGoogleSQLFunctionsAndTypes(
      const BuiltinFunctionOptions& options) ABSL_LOCKS_EXCLUDED(mutex_) {
    return AddBuiltinFunctionsAndTypes(options);
  }

  // DEPRECATED - As above but using the old name an no argument.
  ABSL_DEPRECATED("Inline me!")
  absl::Status AddGoogleSQLFunctionsAndTypes() ABSL_LOCKS_EXCLUDED(mutex_) {
    return AddBuiltinFunctionsAndTypes(
        BuiltinFunctionOptions::AllReleasedFunctions());
  }

  // DEPRECATED - As above but taking `LanguageOptions` directly.
  ABSL_DEPRECATED("Inline me!")
  absl::Status AddGoogleSQLFunctionsAndTypes(const LanguageOptions& options)
      ABSL_LOCKS_EXCLUDED(mutex_) {
    return AddBuiltinFunctionsAndTypes(BuiltinFunctionOptions(options));
  }

  // DEPRECATED: Use AddFunction or AddBuiltinFunctionsAndTypes
  //
  // Add GoogleSQL built-in function definitions into this catalog.
  // This can add functions in both the global namespace and more specific
  // namespaces. If any of the selected functions are in namespaces,
  // sub-Catalogs will be created and the appropriate functions will be added in
  // those sub-Catalogs.
  // Also: Functions and Catalogs with the same names must not already exist.
  // Functions are unowned, and must outlive this catalog.
  //
  // Deprecated because the function is misnamed, misused, and explicitly
  // delegating function object ownership to the catalog is the preferred
  // memory ownership pattern now.
  ABSL_DEPRECATED("Use AddFunction or AddBuiltinFunctionsAndTypes")
  void AddGoogleSQLFunctions(const std::vector<const Function*>& functions)
      ABSL_LOCKS_EXCLUDED(mutex_);

  // Set the google::protobuf::DescriptorPool to use when resolving Types.
  // All message and enum types declared in <pool> will be resolvable with
  // FindType or GetType, treating the full name as one identifier.
  // These type name lookups will be case sensitive.
  //
  // If overlapping names are registered using AddType, those names will
  // take precedence.  There is no check for ambiguous names, and currently
  // there is no mechanism to return an ambiguous type name error.
  //
  // The DescriptorPool can only be set once and cannot be changed.
  // Any types returned from <pool> must stay live for the lifetime of
  // this SimpleCatalog.
  //
  void SetDescriptorPool(const google::protobuf::DescriptorPool* pool)
      ABSL_LOCKS_EXCLUDED(mutex_);
  void SetOwnedDescriptorPool(
      std::unique_ptr<const google::protobuf::DescriptorPool> pool)
      ABSL_LOCKS_EXCLUDED(mutex_);

  // Removes all functions that satisfy <predicate> from this catalog and from
  // all sub-catalogs that are SimpleCatalog. Returns the number of functions
  // removed.
  //
  // Owned functions that are removed are de-allocated after all calls to
  // <predicate> are complete so that <predicate> can safely
  // de-reference Function*s it captures from the invoking context.
  int RemoveFunctions(std::function<bool(const Function*)> predicate)
      ABSL_LOCKS_EXCLUDED(mutex_);
  // Implements RemoveFunction without de-allocating any owned functions.
  // Owned functions are instead transferred to 'removed' and the invoking
  // context is responsible for de-allocation.
  int RemoveFunctions(std::function<bool(const Function*)> predicate,
                      std::vector<std::unique_ptr<const Function>>& removed)
      ABSL_LOCKS_EXCLUDED(mutex_) {
    absl::MutexLock l(&mutex_);
    return RemoveFunctionsLocked(predicate, removed);
  }

  // Removes all tables that satisfy <predicate> from this catalog and from
  // all sub-catalogs that are SimpleCatalog. Returns the number of tables
  // removed.
  //
  // Owned tables that are removed are de-allocated after all calls to
  // <predicate> are complete so that <predicate> can safely
  // de-reference Table*s it captures from the invoking context.
  int RemoveTables(std::function<bool(const Table*)> predicate)
      ABSL_LOCKS_EXCLUDED(mutex_);

  // Implements RemoveTables without de-allocating any owned tables.
  // Owned tables are instead transferred to 'removed' and the invoking
  // context is responsible for de-allocation.
  int RemoveTables(std::function<bool(const Table*)> predicate,
                   std::vector<std::unique_ptr<const Table>>& removed)
      ABSL_LOCKS_EXCLUDED(mutex_) {
    absl::MutexLock l(&mutex_);
    return RemoveTablesLocked(predicate, removed);
  }

  // Removes all constants that satisfy <predicate> from this catalog and from
  // all sub-catalogs that are SimpleCatalog. Returns the number of constants
  // removed.
  //
  // Owned constants that are removed are de-allocated after all calls to
  // <predicate> are complete so that <predicate> can safely
  // de-reference Constant*s it captures from the invoking context.
  int RemoveConstants(std::function<bool(const Constant*)> predicate)
      ABSL_LOCKS_EXCLUDED(mutex_);

  // Implements RemoveConstants without de-allocating any owned constants.
  // Owned constants are instead transferred to 'removed' and the invoking
  // context is responsible for de-allocation.
  int RemoveConstants(std::function<bool(const Constant*)> predicate,
                      std::vector<std::unique_ptr<const Constant>>& removed)
      ABSL_LOCKS_EXCLUDED(mutex_) {
    absl::MutexLock l(&mutex_);
    return RemoveConstantsLocked(predicate, removed);
  }

  // Removes all types that satisfy <predicate> from this catalog and from
  // all sub-catalogs that are SimpleCatalog. Returns the number of types
  // removed.
  //
  // All types continue to be owned by the TypeFactory, so no deallocation
  // will occur.
  int RemoveTypes(std::function<bool(const Type*)> predicate)
      ABSL_LOCKS_EXCLUDED(mutex_);

  // Removes all table valued functions that satisfy <predicate> from this
  // catalog and from all sub-catalogs that are SimpleCatalog. Returns the
  // number of table valued functions removed.
  //
  // Owned table valued functions that are removed are de-allocated after all
  // calls to <predicate> are complete so that <predicate> can safely
  // de-reference TableValuedFunction*s it captures from the invoking context.
  int RemoveTableValuedFunctions(
      std::function<bool(const TableValuedFunction*)> predicate)
      ABSL_LOCKS_EXCLUDED(mutex_);

  // Clear the set of table-valued functions stored in this Catalog and any
  // subcatalogs created for googlesql namespaces. Does not affect any other
  // catalogs.
  //
  // This function is unclear in its definition about whether it should be
  // removing built-in functions, engine-defined functions, user-defined
  // functions or some combination. It also doesn't respect its own
  // documentation reguarding which subcatalogs it removes from. Given the
  // uncertanty around what this function is supposed to do, and a failure to
  // find callsites that make it clear, this is deprecated in favor of
  // 'RemoveTableFunctions'.
  ABSL_DEPRECATED("Use RemoveTableFunctions")
  void ClearTableValuedFunctions() ABSL_LOCKS_EXCLUDED(mutex_);

  // Deserialize SimpleCatalog from proto. Types will be deserialized using
  // the TypeFactory owned by this catalog and given Descriptors from the
  // given DescriptorPools. The DescriptorPools should have been created by
  // type serialization, and all proto types in the catalog are treated as
  // references into these pools. The DescriptorPools must both outlive the
  // result SimpleCatalog.
  static absl::StatusOr<std::unique_ptr<SimpleCatalog>> Deserialize(
      const SimpleCatalogProto& proto,
      absl::Span<const google::protobuf::DescriptorPool* const> pools,
      const ExtendedTypeDeserializer* extended_type_deserializer = nullptr);
  // Same as the `Deserialize()` above, except that callers of this function
  // could provide a `type_factory`. If the provided `type_factory` is nullptr,
  // it will fallback to the above `Deserialize()`.
  static absl::StatusOr<std::unique_ptr<SimpleCatalog>> Deserialize(
      const SimpleCatalogProto& proto,
      absl::Span<const google::protobuf::DescriptorPool* const> pools,
      googlesql::TypeFactory* type_factory,
      const ExtendedTypeDeserializer* extended_type_deserializer);
  ABSL_DEPRECATED("Inline me!")
  static absl::Status Deserialize(
      const SimpleCatalogProto& proto,
      const std::vector<const google::protobuf::DescriptorPool*>& pools,
      std::unique_ptr<SimpleCatalog>* result);

  // Serialize the SimpleCatalog to proto, optionally ignoring built-in
  // functions and recursive subcatalogs. file_descriptor_set_map is used
  // to store serialized FileDescriptorSets, which can be deserialized into
  // separate DescriptorPools in order to reconstruct the Type. The map may
  // be non-empty and may be used across calls to this method in order to
  // serialize multiple types. The map may NOT be null.
  // NOTE: recursion detection is done with seen catalogs pointers, which
  // may effectively detect multiple-step recursions, but also recognize
  // siblings pointing to the same catalog object as false positives.
  absl::Status Serialize(FileDescriptorSetMap* file_descriptor_set_map,
                         SimpleCatalogProto* proto, bool ignore_builtin = true,
                         bool ignore_recursive = true) const
      ABSL_LOCKS_EXCLUDED(mutex_);

  // Return a TypeFactory owned by this SimpleCatalog.
  TypeFactory* type_factory() ABSL_LOCKS_EXCLUDED(mutex_);

  absl::Status GetCatalogs(
      absl::flat_hash_set<const Catalog*>* output) const override;
  absl::Status GetTables(
      absl::flat_hash_set<const Table*>* output) const override;
  absl::Status GetTypes(
      absl::flat_hash_set<const Type*>* output) const override;
  absl::Status GetFunctions(
      absl::flat_hash_set<const Function*>* output) const override;
  absl::Status GetModels(
      absl::flat_hash_set<const Model*>* output) const override;
  absl::Status GetTableValuedFunctions(
      absl::flat_hash_set<const TableValuedFunction*>* output) const override;
  absl::Status GetProcedures(
      absl::flat_hash_set<const Procedure*>* output) const override;

  // Accessors for reading a copy of the object lists in this SimpleCatalog.
  // This is intended primarily for tests.
  std::vector<const Table*> tables() const ABSL_LOCKS_EXCLUDED(mutex_);
  std::vector<const Model*> models() const ABSL_LOCKS_EXCLUDED(mutex_);
  std::vector<const Type*> types() const ABSL_LOCKS_EXCLUDED(mutex_);
  std::vector<const Function*> functions() const ABSL_LOCKS_EXCLUDED(mutex_);
  std::vector<const TableValuedFunction*> table_valued_functions() const
      ABSL_LOCKS_EXCLUDED(mutex_);
  std::vector<const Procedure*> procedures() const ABSL_LOCKS_EXCLUDED(mutex_);
  std::vector<Catalog*> catalogs() const ABSL_LOCKS_EXCLUDED(mutex_);
  std::vector<const Connection*> connections() const
      ABSL_LOCKS_EXCLUDED(mutex_);
  std::vector<const Constant*> constants() const ABSL_LOCKS_EXCLUDED(mutex_);
  std::vector<const PropertyGraph*> property_graphs() const
      ABSL_LOCKS_EXCLUDED(mutex_);

  // Accessors for reading a copy of the key (object-name) lists in this
  // SimpleCatalog. Note that all keys are lower case.
  std::vector<std::string> table_names() const ABSL_LOCKS_EXCLUDED(mutex_);
  std::vector<std::string> model_names() const ABSL_LOCKS_EXCLUDED(mutex_);
  std::vector<std::string> function_names() const ABSL_LOCKS_EXCLUDED(mutex_);
  std::vector<std::string> table_valued_function_names() const
      ABSL_LOCKS_EXCLUDED(mutex_);
  std::vector<std::string> catalog_names() const ABSL_LOCKS_EXCLUDED(mutex_);
  std::vector<std::string> connection_names() const ABSL_LOCKS_EXCLUDED(mutex_);
  std::vector<std::string> constant_names() const ABSL_LOCKS_EXCLUDED(mutex_);
  std::vector<std::string> property_graph_names() const
      ABSL_LOCKS_EXCLUDED(mutex_);

 protected:
  absl::Status DeserializeImpl(const SimpleCatalogProto& proto,
                               const TypeDeserializer& type_deserializer,
                               SimpleCatalog* catalog);
  // Adds non-owned built-in functions, types, and TVFs to the catalog. For
  // functions, owned sub-catalogs are created as required for the first part of
  // their name path. Returns an error if any of the functions or TVFs are not
  // GoogleSQL built-in.
  //
  // This function is protected for internal use, external callers should use
  // the public functions and prefer adding owned objects.
  absl::Status AddNonOwnedBuiltinFunctionsAndTypes(
      const absl::flat_hash_map<std::string, const Function*>& functions,
      const absl::flat_hash_map<std::string, const Type*>& types,
      const absl::flat_hash_map<std::string, const TableValuedFunction*>&
          table_valued_functions);

 private:
  friend class SimpleCatalogTestFriend;
  friend class TestDatabaseCatalog;

  absl::Status SerializeImpl(absl::flat_hash_set<const Catalog*>* seen_catalogs,
                             FileDescriptorSetMap* file_descriptor_set_map,
                             SimpleCatalogProto* proto, bool ignore_builtin,
                             bool ignore_recursive) const
      ABSL_LOCKS_EXCLUDED(mutex_);

  // Helper methods for adding objects while holding <mutex_>.
  void AddCatalogLocked(absl::string_view name, Catalog* catalog)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  void AddOwnedCatalogLocked(absl::string_view name,
                             std::unique_ptr<Catalog> catalog)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  // TODO: Refactor the Add*() methods for other object types
  // to use a common locked implementation, similar to these for Function.
  void AddFunctionLocked(absl::string_view name, const Function* function)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  void AddOwnedFunctionLocked(absl::string_view name,
                              std::unique_ptr<const Function> function)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  void AddTableValuedFunctionLocked(absl::string_view name,
                                    const TableValuedFunction* table_function)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  void AddOwnedTableValuedFunctionLocked(
      absl::string_view name,
      std::unique_ptr<const TableValuedFunction> table_function)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  void AddConstantLocked(absl::string_view name, const Constant* constant)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  void AddPropertyGraphLocked(absl::string_view name,
                              const PropertyGraph* graph)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  int RemoveFunctionsLocked(
      std::function<bool(const Function*)> predicate,
      std::vector<std::unique_ptr<const Function>>& removed)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  int RemoveTablesLocked(std::function<bool(const Table*)> predicate,
                         std::vector<std::unique_ptr<const Table>>& removed)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  int RemoveConstantsLocked(
      std::function<bool(const Constant*)> predicate,
      std::vector<std::unique_ptr<const Constant>>& removed)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Implements RemoveTableValuedFunctions without de-allocating any owned
  // functions. Owned functions are insteawd transerred to 'removed' and the
  // invoking context is responsible for de-allocation.
  int RemoveTableValuedFunctions(
      std::function<bool(const TableValuedFunction*)> predicate,
      std::vector<std::unique_ptr<const TableValuedFunction>>& removed)
      ABSL_LOCKS_EXCLUDED(mutex_) {
    absl::MutexLock l(&mutex_);
    return RemoveTableValuedFunctionsLocked(predicate, removed);
  }
  int RemoveTableValuedFunctionsLocked(
      std::function<bool(const TableValuedFunction*)> predicate,
      std::vector<std::unique_ptr<const TableValuedFunction>>& removed)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Unified implementation of SuggestFunction and SuggestTableValuedFunction.
  std::string SuggestFunctionOrTableValuedFunction(
      bool is_table_valued_function,
      absl::Span<const std::string> mistyped_path);

  absl::Status AddBuiltinFunctionsAndTypesImpl(
      const BuiltinFunctionOptions& options, bool add_types);

  const std::string name_;

  mutable absl::Mutex mutex_;

  // The TypeFactory can be allocated lazily, so may be NULL.
  TypeFactory* type_factory_ ABSL_GUARDED_BY(mutex_);
  std::unique_ptr<TypeFactory> owned_type_factory_ ABSL_GUARDED_BY(mutex_);

  // global_names_ is to avoid naming conflict of top tier objects including
  // tables etc. When inserting into tables_, insert into global_names
  // as well
  absl::flat_hash_set<std::string> global_names_ ABSL_GUARDED_BY(mutex_);
  absl::flat_hash_map<std::string, const Table*> tables_
      ABSL_GUARDED_BY(mutex_);
  absl::flat_hash_map<std::string, const Connection*> connections_
      ABSL_GUARDED_BY(mutex_);
  absl::flat_hash_map<std::string, const Sequence*> sequences_
      ABSL_GUARDED_BY(mutex_);
  absl::flat_hash_map<std::string, const Model*> models_
      ABSL_GUARDED_BY(mutex_);
  // Case-insensitive map of names to Types explicitly added to the Catalog via
  // AddType (including proto an enum types).
  absl::flat_hash_map<std::string, const Type*> types_ ABSL_GUARDED_BY(mutex_);
  absl::flat_hash_map<std::string, const Function*> functions_
      ABSL_GUARDED_BY(mutex_);
  absl::flat_hash_map<std::string, const TableValuedFunction*>
      table_valued_functions_ ABSL_GUARDED_BY(mutex_);
  absl::flat_hash_map<std::string, const Procedure*> procedures_
      ABSL_GUARDED_BY(mutex_);
  absl::flat_hash_map<std::string, Catalog*> catalogs_ ABSL_GUARDED_BY(mutex_);
  absl::flat_hash_map<std::string, const Constant*> constants_
      ABSL_GUARDED_BY(mutex_);
  absl::flat_hash_map<std::string, const PropertyGraph*> property_graphs_
      ABSL_GUARDED_BY(mutex_);

  std::vector<std::unique_ptr<const Table>> owned_tables_
      ABSL_GUARDED_BY(mutex_);
  std::vector<std::unique_ptr<const Model>> owned_models_
      ABSL_GUARDED_BY(mutex_);
  std::vector<std::unique_ptr<const Connection>> owned_connections_
      ABSL_GUARDED_BY(mutex_);
  std::vector<std::unique_ptr<const Sequence>> owned_sequences_
      ABSL_GUARDED_BY(mutex_);
  std::vector<std::unique_ptr<const Function>> owned_functions_
      ABSL_GUARDED_BY(mutex_);
  std::vector<std::unique_ptr<const TableValuedFunction>>
      owned_table_valued_functions_ ABSL_GUARDED_BY(mutex_);
  std::vector<std::unique_ptr<const Procedure>> owned_procedures_
      ABSL_GUARDED_BY(mutex_);
  std::vector<std::unique_ptr<const Catalog>> owned_catalogs_
      ABSL_GUARDED_BY(mutex_);
  std::vector<std::unique_ptr<const Constant>> owned_constants_
      ABSL_GUARDED_BY(mutex_);
  std::vector<std::unique_ptr<const PropertyGraph>> owned_property_graphs_
      ABSL_GUARDED_BY(mutex_);

  // Subcatalogs added for googlesql function namespaces. Kept separate from
  // owned_catalogs_ to keep them as SimpleCatalog types.
  absl::flat_hash_map<std::string, std::unique_ptr<SimpleCatalog>>
      owned_googlesql_subcatalogs_ ABSL_GUARDED_BY(mutex_);

  const google::protobuf::DescriptorPool* descriptor_pool_ ABSL_GUARDED_BY(mutex_) =
      nullptr;
  std::unique_ptr<const google::protobuf::DescriptorPool> ABSL_GUARDED_BY(mutex_)
      owned_descriptor_pool_;
};

// SimpleTable is a concrete implementation of the Table interface.
class SimpleTable : public Table {
 public:
  // Make a table with columns with the given names and types.
  // Crashes if there are duplicate column names.
  //
  // If serialization is to be supported, 'serialization_id' will be returned
  // by calls to GetSerializationId(); it should be unique across all tables in
  // the same catalog.
  typedef std::pair<std::string, const Type*> NameAndType;
  typedef std::pair<std::string, AnnotatedType> NameAndAnnotatedType;
  SimpleTable(absl::string_view name, absl::Span<const NameAndType> columns,
              int64_t serialization_id = 0);
  SimpleTable(absl::string_view name,
              absl::Span<const NameAndAnnotatedType> columns,
              int64_t serialization_id = 0);

  // Make a table with the given Columns.
  // Crashes if there are duplicate column names.
  // Takes ownership of elements of <columns> if <take_ownership> is true.
  SimpleTable(absl::string_view name, const std::vector<const Column*>& columns,
              bool take_ownership = false, int64_t serialization_id = 0);

  // Make a value table with row type <row_type>.
  // This constructor inserts a single column of type <row_type> into
  // <columns_>, with the default name "value".  This column name "value"
  // is not visible in SQL but will be appear in the resolved AST.
  //
  // One implication of this implementation is that FindColumnByName() will
  // only be able to find the column with name "value", not the top level table
  // columns of this SimpleTable (and the top level table columns of this
  // SimpleTable do not have an associated Column).
  SimpleTable(absl::string_view, const Type* row_type, int64_t id = 0);

  // Make a table with no Columns.  (Other constructors are ambiguous for this.)
  explicit SimpleTable(absl::string_view, int64_t id = 0);

  SimpleTable(const SimpleTable&) = delete;
  SimpleTable& operator=(const SimpleTable&) = delete;

  std::string Name() const override { return name_; }
  std::string FullName() const override {
    return full_name_.empty() ? name_ : full_name_;
  }

  int NumColumns() const override { return columns_.size(); }
  const Column* GetColumn(int i) const override {
    // Note: The column interface does not define what should happen
    // when `i` is out of range.
    if (i < 0 || i >= columns_.size()) {
      return nullptr;
    }
    return columns_[i];
  }
  const Column* FindColumnByName(const std::string& name) const override;
  std::optional<std::vector<int>> PrimaryKey() const override {
    return primary_key_;
  };
  std::optional<std::vector<int>> RowIdentityColumns() const override {
    if (!row_identity_.has_value()) {
      return PrimaryKey();
    }
    return row_identity_;
  };

  bool IsValueTable() const override { return is_value_table_; }

  void set_is_value_table(bool value) { is_value_table_ = value; }

  bool AllowAnonymousColumnName() const { return allow_anonymous_column_name_; }

  // Setter for allow_anonymous_column_name_. If the existing condition
  // conflicts with the value to be set, the setting will fail.
  absl::Status set_allow_anonymous_column_name(bool value) {
    GOOGLESQL_RET_CHECK(value || !anonymous_column_seen_);
    allow_anonymous_column_name_ = value;
    return absl::OkStatus();
  }

  bool AllowDuplicateColumnNames() const {
    return allow_duplicate_column_names_;
  }

  // Setter for allow_duplicate_column_names_. If the existing condition
  // conflicts with the value to be set, the setting will fail.
  absl::Status set_allow_duplicate_column_names(bool value) {
    GOOGLESQL_RET_CHECK(value || duplicate_column_names_.empty());
    allow_duplicate_column_names_ = value;
    return absl::OkStatus();
  }

  // Add a column. Returns an error if constraints allow_anonymous_column_name_
  // or allow_duplicate_column_names_ are violated.
  // If is_owned is set to true but an error is returned, the column will be
  // deleted inside this function.
  absl::Status AddColumn(const Column* column, bool is_owned);
  absl::Status AddColumn(std::unique_ptr<const Column> column);

  // Set primary key with given column 0-based indices.
  //
  // This is also used for the row identity columns unless
  // SetRowIdentityColumns() has been called.
  absl::Status SetPrimaryKey(std::vector<int> primary_key);

  // Set row identity with given column 0-based indices.
  absl::Status SetRowIdentityColumns(std::vector<int> row_identity);

  // Set the full name. If empty, name will be used as the full name.
  absl::Status set_full_name(absl::string_view full_name) {
    if (full_name != name_) {
      full_name_ = full_name;
    } else {
      full_name_ = "";
    }
    return absl::OkStatus();
  }

  int64_t GetSerializationId() const override { return id_; }

  // Constructs an EvaluatorTableIterator from a list of column indexes.
  // Represents the signature of Table::CreateEvaluatorTableIterator().
  using EvaluatorTableIteratorFactory =
      std::function<absl::StatusOr<std::unique_ptr<EvaluatorTableIterator>>(
          absl::Span<const int>)>;

  // Sets a factory to be returned by CreateEvaluatorTableIterator().
  // CAVEAT: This is not preserved by serialization/deserialization. It is only
  // relevant to users of the evaluator API defined in public/evaluator.h.
  void SetEvaluatorTableIteratorFactory(
      const EvaluatorTableIteratorFactory& factory) {
    evaluator_table_iterator_factory_ =
        std::make_unique<EvaluatorTableIteratorFactory>(factory);
  }

  // Convenience method that calls SetEvaluatorTableIteratorFactory to
  // correspond to a list of rows. More specifically, sets the table contents
  // to a copy of 'rows' and sets up a callback to return those values when
  // CreateEvaluatorTableIterator() is called.
  // CAVEAT: This is not preserved by serialization/deserialization.  It is only
  // relevant to users of the evaluator API defined in public/evaluator.h.
  void SetContents(absl::Span<const std::vector<Value>> rows);

  // Makes this table queryable as an empty table, returning zero rows.
  void SetContentsEmpty() { SetContents({}); }

  absl::StatusOr<std::unique_ptr<EvaluatorTableIterator>>
  CreateEvaluatorTableIterator(
      absl::Span<const int> column_idxs) const override;

  // Sets the `anonymization_info_` with the specified `userid_column_name`
  // (overwriting any previous anonymization info).  An error is returned if
  // the named column is ambiguous or does not exist in this table.
  //
  // Setting the AnonymizationInfo defines this table as supporting
  // anonymization semantics and containing sensitive private data.
  absl::Status SetAnonymizationInfo(const std::string& userid_column_name);

  // Same as above, but with the specified `userid_column_name_path`.
  absl::Status SetAnonymizationInfo(
      absl::Span<const std::string> userid_column_name_path);

  // Sets the `anonymization_info_` with the specified `anonymization_info`
  // (overwriting any previous anonymization info).
  //
  // Setting the AnonymizationInfo defines this table as supporting
  // anonymization semantics and containing sensitive private data.
  absl::Status SetAnonymizationInfo(
      std::unique_ptr<AnonymizationInfo> anonymization_info);

  // Resets `anonymization_info_`, implying that the table does not support
  // anonymization queries.
  void ResetAnonymizationInfo() { anonymization_info_.reset(); }

  // Returns anonymization info for a table, including a column reference that
  // indicates the userid column for anonymization purposes.
  std::optional<const AnonymizationInfo> GetAnonymizationInfo() const override {
    if (anonymization_info_ != nullptr) {
      return *anonymization_info_;
    }
    return std::optional<const AnonymizationInfo>();
  }

  // Serialize this table into protobuf. The provided map is used to store
  // serialized FileDescriptorSets, which can be deserialized into separate
  // DescriptorPools in order to reconstruct the Type. The map may be
  // non-empty and may be used across calls to this method in order to
  // serialize multiple types. The map may NOT be null.
  absl::Status Serialize(FileDescriptorSetMap* file_descriptor_set_map,
                         SimpleTableProto* proto) const;

  // Deserializes SimpleTable from proto using TypeDeserializer.
  static absl::StatusOr<std::unique_ptr<SimpleTable>> Deserialize(
      const SimpleTableProto& proto, const TypeDeserializer& deserializer);

 protected:
  // Returns the current contents (passed to the last call to SetContents()) in
  // column-major order.
  const std::vector<std::shared_ptr<const std::vector<Value>>>&
  column_major_contents() const {
    return column_major_contents_;
  }

  // Returns the number of rows set in the last call to SetContents().
  int64_t num_rows() const { return num_rows_; }

 private:
  // Insert a column to columns_map_. Return error when
  // allow_anonymous_column_name_ or allow_duplicate_column_names_ are violated.
  // Furthermore, if the column's name is duplicated, it's recorded in
  // duplicate_column_names_ and the original column is removed from
  // columns_map_.
  absl::Status InsertColumnToColumnMap(const Column* column);

  const std::string name_;
  std::string full_name_;
  bool is_value_table_ = false;
  std::vector<const Column*> columns_;
  std::optional<std::vector<int>> primary_key_;
  std::optional<std::vector<int>> row_identity_;
  std::vector<std::unique_ptr<const Column>> owned_columns_;
  absl::flat_hash_map<std::string, const Column*> columns_map_;
  absl::flat_hash_set<std::string> duplicate_column_names_;
  int64_t id_ = 0;
  // Does not own the referenced Column* (if set).
  std::unique_ptr<AnonymizationInfo> anonymization_info_;
  bool allow_anonymous_column_name_ = false;
  bool anonymous_column_seen_ = false;
  bool allow_duplicate_column_names_ = false;

  // We use shared_ptrs to handle calls to SetContets() while there are
  // iterators outstanding.
  int64_t num_rows_ = 0;
  std::vector<std::shared_ptr<const std::vector<Value>>> column_major_contents_;
  std::unique_ptr<EvaluatorTableIteratorFactory>
      evaluator_table_iterator_factory_;

  static absl::Status ValidateNonEmptyColumnName(
      const std::string& column_name);
};

// SimpleSQLView is a concrete implementation of the SQLView interface.
class SimpleSQLView : public SQLView {
 public:
  struct NameAndType {
    std::string name;
    const Type* type;
  };

  // Create a new SQLView object. The lifetime of 'query' must meet or
  // exceed the lifetime of the created SimpleSQLView object. SimpleSQLView does
  // not take ownership of 'query'.
  static absl::StatusOr<std::unique_ptr<SimpleSQLView>> Create(
      absl::string_view name, std::vector<NameAndType> columns,
      SqlSecurity security, bool is_value_table, const ResolvedScan* query);

  SqlSecurity sql_security() const override { return security_; }
  const ResolvedScan* view_query() const override { return query_; }

  std::string Name() const override { return name_; }
  std::string FullName() const override { return name_; }

  int NumColumns() const override {
    return static_cast<int>(owned_columns_.size());
  }
  const Column* GetColumn(int i) const override {
    return owned_columns_[i].get();
  }
  const Column* FindColumnByName(const std::string& name) const override {
    return columns_map_.at(name);
  }
  bool IsValueTable() const override { return is_value_table_; }

 private:
  std::string name_;
  SqlSecurity security_;
  bool is_value_table_;
  const ResolvedScan* query_;
  std::vector<std::unique_ptr<const Column>> owned_columns_;
  absl::flat_hash_map<std::string, const Column*> columns_map_;
  // TODO: b/450100045 - Remove once 'columns_map_' is case insensitive.
  absl::flat_hash_set<std::string, googlesql_base::StringViewCaseHash,
                      googlesql_base::StringViewCaseEqual>
      column_names_;

  SimpleSQLView(absl::string_view name, SqlSecurity security,
                bool is_value_table, const ResolvedScan* query)
      : name_(name),
        security_(security),
        is_value_table_(is_value_table),
        query_(query) {}

  absl::Status AddColumn(std::unique_ptr<const Column> column);
};

// SimpleModel is a concrete implementation of the Model interface.
class SimpleModel : public Model {
 public:
  // Make a model with input and output columns with the given names and types.
  // Crashes if there are duplicate column names.
  typedef std::pair<std::string, const Type*> NameAndType;
  SimpleModel(std::string name, absl::Span<const NameAndType> inputs,
              absl::Span<const NameAndType> outputs, int64_t id = 0);

  // Make a model with the given inputs and outputs.
  // Crashes if there are duplicate column names.
  // Takes ownership of elements of <inputs> and <outputs> if <take_ownership>
  // is true.
  SimpleModel(std::string name, const std::vector<const Column*>& inputs,
              const std::vector<const Column*>& outputs,
              bool take_ownership = false, int64_t id = 0);

  SimpleModel(const SimpleModel&) = delete;
  SimpleModel& operator=(const SimpleModel&) = delete;

  std::string Name() const override { return name_; }
  std::string FullName() const override { return name_; }

  uint64_t NumInputs() const override { return inputs_.size(); }
  // i must be less than NumInputs.
  const Column* GetInput(int i) const override { return inputs_[i]; }
  const Column* FindInputByName(const std::string& name) const override;

  uint64_t NumOutputs() const override { return outputs_.size(); }
  // i must be less than NumOutputs.
  const Column* GetOutput(int i) const override { return outputs_[i]; }
  const Column* FindOutputByName(const std::string& name) const override;

  // Add an input.
  // If is_owned is set to true but an error is returned, the column will be
  // deleted inside this function.
  absl::Status AddInput(const Column* column, bool is_owned);
  absl::Status AddInput(std::unique_ptr<const Column> column);
  // Add an output.
  // If is_owned is set to true but an error is returned, the column will be
  // deleted inside this function.
  absl::Status AddOutput(const Column* column, bool is_owned);
  absl::Status AddOutput(std::unique_ptr<const Column> column);

  int64_t GetSerializationId() const override { return id_; }

  // Serializes this SimpleModel to proto.
  //
  // See SimpleCatalog::Serialize() for details about <file_descriptor_set_map>.
  absl::Status Serialize(FileDescriptorSetMap* file_descriptor_set_map,
                         SimpleModelProto* proto) const;

  // Deserializes a SimpleModel from proto.
  static absl::StatusOr<std::unique_ptr<SimpleModel>> Deserialize(
      const SimpleModelProto& proto, const TypeDeserializer& type_deserializer);

 private:
  const std::string name_;
  // Columns added to the <inputs_>, <outputs_> and the corresponding maps may
  // be owned by this class or not. In case they are owned by this class, they
  // will be added to <owned_inputs_outputs_> and will be deleted at destruction
  // time.
  absl::flat_hash_map<std::string, const Column*> inputs_map_;
  std::vector<const Column*> inputs_;

  absl::flat_hash_map<std::string, const Column*> outputs_map_;
  std::vector<const Column*> outputs_;

  // All the input and output columns that this class owns.
  std::vector<std::unique_ptr<const Column>> owned_inputs_outputs_;
  int64_t id_ = 0;
};

class SimpleConnection : public Connection {
 public:
  explicit SimpleConnection(absl::string_view name) : name_(name) {}
  SimpleConnection(const SimpleConnection&) = delete;
  SimpleConnection& operator=(const Connection&) = delete;

  std::string Name() const override { return name_; }
  std::string FullName() const override { return name_; }

  void Serialize(SimpleConnectionProto* proto) const;

  static std::unique_ptr<SimpleConnection> Deserialize(
      const SimpleConnectionProto& proto);

 private:
  const std::string name_;
};

class SimpleSequence : public Sequence {
 public:
  explicit SimpleSequence(absl::string_view name) : name_(name) {}
  SimpleSequence(const SimpleSequence&) = delete;
  SimpleSequence& operator=(const Sequence&) = delete;

  std::string Name() const override { return name_; }
  std::string FullName() const override { return name_; }

  absl::Status Serialize(SimpleSequenceProto* proto) const;
  static absl::StatusOr<std::unique_ptr<SimpleSequence>> Deserialize(
      const SimpleSequenceProto& proto);

 private:
  const std::string name_;
};

// SimpleColumn is a concrete implementation of the Column interface.
class SimpleColumn : public Column {
 public:
  // Optional column attributes.
  //
  // Example use:
  //   SimpleColumn non_writable_column(
  //       "TABLE", "non_writable", {.is_writable_column = false});
  //   SimpleColumn pseudo_column(
  //       "TABLE", "pseudo", {
  //           .is_pseudo_column = true, .is_writable_column = true});
  struct Attributes {
    // A pseudocolumn is selectable but is not included in default expansions
    // like SELECT *. See (broken link).
    bool is_pseudo_column = false;
    // A writeable column can be set in an INSERT or UPDATE DML statement.
    bool is_writable_column = true;
    // Whether an unwritable column can be set to DEFAULT in an UPDATE DML
    // statement.
    bool can_update_unwritable_to_default = false;

    // Optional attributes to add on the Column.
    std::optional<ExpressionAttributes> column_expression = std::nullopt;
    std::optional<JoinColumnAttributes> join_column = std::nullopt;
  };

  // Constructor.
  // This will soon be replaced by:
  //   SimpleColumn(absl::string_view table_name, absl::string_view name,
  //                const Type* type);
  // Use the Attributes overload below instead of optional args.
  SimpleColumn(absl::string_view table_name, absl::string_view name,
               const Type* type, bool is_pseudo_column = false,
               bool is_writable_column = true)
      : SimpleColumn(table_name, name, type,
                     {.is_pseudo_column = is_pseudo_column,
                      .is_writable_column = is_writable_column}) {}
  SimpleColumn(absl::string_view table_name, absl::string_view name,
               const Type* type, const Attributes& attributes);

  // This will soon be replaced by:
  //   SimpleColumn(absl::string_view table_name, absl::string_view name,
  //                AnnotatedType annotated_type);
  // Use the Attributes overload below instead of optional args.
  SimpleColumn(absl::string_view table_name, absl::string_view name,
               AnnotatedType annotated_type, bool is_pseudo_column = false,
               bool is_writable_column = true)
      : SimpleColumn(table_name, name, annotated_type,
                     {.is_pseudo_column = is_pseudo_column,
                      .is_writable_column = is_writable_column}) {}
  SimpleColumn(absl::string_view table_name, absl::string_view name,
               AnnotatedType annotated_type, const Attributes& attributes);

  SimpleColumn(const SimpleColumn&) = delete;
  SimpleColumn& operator=(const SimpleColumn&) = delete;

  ~SimpleColumn() override = default;

  std::string Name() const override { return name_; }
  std::string FullName() const override { return full_name_; }
  const Type* GetType() const override { return annotated_type_.type; }
  const AnnotationMap* /*absl_nullable*/ GetTypeAnnotationMap() const override {
    return annotated_type_.annotation_map;
  }
  AnnotatedType annotated_type() const { return annotated_type_; }

  const Attributes& attributes() const { return attributes_; }
  bool IsPseudoColumn() const override { return attributes_.is_pseudo_column; }
  bool IsWritableColumn() const override {
    return attributes_.is_writable_column;
  }
  bool CanUpdateUnwritableToDefault() const override {
    return attributes_.can_update_unwritable_to_default;
  }

  std::optional<const ExpressionAttributes> GetExpression() const override {
    return attributes_.column_expression;
  }
  std::optional<const JoinColumnAttributes> GetJoinColumn() const override {
    return attributes_.join_column;
  }

  // Serialize this column into protobuf, the provided map is used to store
  // serialized FileDescriptorSets, which can be deserialized into separate
  // DescriptorPools in order to reconstruct the Type. The map may be
  // non-empty and may be used across calls to this method in order to
  // serialize multiple types. The map may NOT be null.
  absl::Status Serialize(FileDescriptorSetMap* file_descriptor_set_map,
                         SimpleColumnProto* proto) const;

  // Deserializes SimpleColumn from proto using TypeDeserializer.
  static absl::StatusOr<std::unique_ptr<SimpleColumn>> Deserialize(
      const SimpleColumnProto& proto, absl::string_view table_name,
      const TypeDeserializer& type_deserializer);

 private:
  const std::string name_;
  const std::string full_name_;
  AnnotatedType annotated_type_;
  Attributes attributes_;
};

// A named constant with a concrete value in the catalog.
class SimpleConstant : public Constant {
 public:
  // Creates and returns a SimpleConstant, returning an error if <value> is
  // an invalid Value or the <name_path> is empty.
  static absl::Status Create(const std::vector<std::string>& name_path,
                             const Value& value,
                             std::unique_ptr<SimpleConstant>* simple_constant);

  ~SimpleConstant() override = default;

  // This class is neither copyable nor assignable.
  SimpleConstant(const SimpleConstant& other_simple_constant) = delete;
  SimpleConstant& operator=(const SimpleConstant& other_simple_constant) =
      delete;

  // Serializes this SimpleConstant to proto.
  //
  // See SimpleCatalog::Serialize() for details about <file_descriptor_set_map>.
  absl::Status Serialize(FileDescriptorSetMap* file_descriptor_set_map,
                         SimpleConstantProto* simple_constant_proto) const;

  // Deserializes this SimpleConstant from proto.
  //
  // See SimpleCatalog::Deserialize() for details about <descriptor_pools>.
  static absl::StatusOr<std::unique_ptr<SimpleConstant>> Deserialize(
      const SimpleConstantProto& simple_constant_proto,
      const TypeDeserializer& type_deserializer);

  const Type* type() const override { return value_.type(); }

  const Value& value() const { return value_; }

  bool HasValue() const override { return true; }
  absl::StatusOr<googlesql::Value> GetValue() const override { return value_; }

  // Returns a string describing this Constant for debugging purposes.
  std::string DebugString() const override;
  // Same as the previous, but includes the Type debug string.
  std::string VerboseDebugString() const;

  std::string ConstantValueDebugString() const override;

 private:
  SimpleConstant(std::vector<std::string> name_path, Value value)
      : Constant(std::move(name_path)), value_(std::move(value)) {}

  // The value of this Constant. This is the RHS in a CREATE CONSTANT statement.
  Value value_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_SIMPLE_CATALOG_H_
