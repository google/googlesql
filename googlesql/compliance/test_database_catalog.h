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

#ifndef GOOGLESQL_COMPLIANCE_TEST_DATABASE_CATALOG_H_
#define GOOGLESQL_COMPLIANCE_TEST_DATABASE_CATALOG_H_

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "googlesql/compliance/test_driver.h"
#include "googlesql/public/analyzer_output.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/function.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/type.h"
#include "googlesql/public/types/type_factory.h"
#include "absl/base/attributes.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/types/span.h"
#include "google/protobuf/compiler/importer.h"
#include "google/protobuf/descriptor.h"

namespace googlesql {

// Class which manages a Catalog constructed from a TestDatabase and
// LanguageOptions.
class TestDatabaseCatalog {
 public:
  SimpleCatalog* catalog() const { return catalog_.get(); }

  // Sets up the catalog, types, and tables. The provided `language_options`
  // is used to analyze measure expressions if any of the tables contain measure
  // columns.
  absl::Status SetTestDatabase(const TestDatabaseProto& test_db_proto,
                               const LanguageOptions& language_options = {});

  absl::Status SetLanguageOptions(const LanguageOptions& language_options);

  // Add UDFs and UDAs referenced by measure definitions to the catalog
  absl::Status AddUdfsForMeasureDefinitions(
      const TestDatabase& test_db, const LanguageOptions& language_options);

  absl::Status IsInitialized() const;

  // Does not add measure tables.
  ABSL_DEPRECATED("Use AddTableWithStatus instead")
  void AddTable(const std::string& table_name, const TestTable& table);

  // Adds a table to the catalog.
  //
  // The provided `language_options` is used to analyze the expressions of the
  // measure columns of the table, if any.
  absl::Status AddTableWithStatus(const std::string& table_name,
                                  const TestTable& table,
                                  const LanguageOptions& language_options);

  absl::Status LoadProtoEnumTypes(const std::set<std::string>& filenames,
                                  const std::set<std::string>& proto_names,
                                  const std::set<std::string>& enum_names);

  // Catalog- and EnumerableCatalog-analogous functions needed by callers.
  absl::Status FindTable(
      absl::Span<const std::string> path, const Table** table,
      const Catalog::FindOptions& options = Catalog::FindOptions());
  absl::Status GetTables(absl::flat_hash_set<const Table*>* output) const;
  absl::Status GetTypes(absl::flat_hash_set<const Type*>* output) const;

  const google::protobuf::DescriptorPool* descriptor_pool() const {
    return importer_->importer()->pool();
  }

  TypeFactory* type_factory() const { return type_factory_.get(); }

  std::vector<std::unique_ptr<const AnalyzerOutput>>& sql_object_artifacts() {
    return sql_object_artifacts_;
  }

 private:
  ABSL_DEPRECATED(
      "DO NOT USE THIS. USED ONLY FOR AN EXTERNAL LEGACY TEST"
      "WHICH IS USING FAKE IN-MEMORY DESCRIPTORS")
  absl::Status SetTestDatabaseWithLeakyDescriptors(const TestDatabase& test_db);
  friend class ReferenceDriver;

  class BuiltinFunctionCache {
   public:
    ~BuiltinFunctionCache();
    absl::Status SetLanguageOptions(const LanguageOptions& options,
                                    SimpleCatalog* catalog);
    void DumpStats();

   private:
    using BuiltinFunctionMap =
        absl::flat_hash_map<std::string, std::unique_ptr<Function>>;
    using BuiltinTypeMap = absl::flat_hash_map<std::string, const Type*>;
    using BuiltinTableValuedFunctionMap =
        absl::flat_hash_map<std::string, std::unique_ptr<TableValuedFunction>>;
    struct CacheEntry {
      BuiltinFunctionMap functions;
      BuiltinTableValuedFunctionMap table_valued_functions;
      BuiltinTypeMap types;
    };
    int total_calls_ = 0;
    int cache_hit_ = 0;
    absl::flat_hash_map<LanguageOptions, CacheEntry> builtins_cache_;
  };

  // Only true after `SetTestDatabase` is called.
  bool is_initialized_ = false;
  std::unique_ptr<ProtoImporter> importer_;
  std::unique_ptr<BuiltinFunctionCache> function_cache_ =
      std::make_unique<BuiltinFunctionCache>();
  std::unique_ptr<SimpleCatalog> catalog_;
  std::unique_ptr<TypeFactory> type_factory_ = std::make_unique<TypeFactory>();
  // Holds the analyzer outputs for measure definitions as well as UDFs and UDAs
  // needed for measure definitions.
  std::vector<std::unique_ptr<const AnalyzerOutput>> sql_object_artifacts_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_COMPLIANCE_TEST_DATABASE_CATALOG_H_
