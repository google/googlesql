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

#include "googlesql/compliance/test_database_catalog.h"

#include <iterator>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/common/internal_value.h"
#include "googlesql/common/measure_analysis_utils.h"
#include "googlesql/common/testing/testing_proto_util.h"
#include "googlesql/compliance/test_driver.h"
#include "googlesql/compliance/test_util.h"
#include "googlesql/public/builtin_function.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/function.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/simple_catalog_util.h"
#include "googlesql/public/type.h"
#include "googlesql/public/types/struct_type.h"
#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "googlesql/base/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "google/protobuf/compiler/importer.h"
#include "google/protobuf/descriptor.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

TestDatabaseCatalog::BuiltinFunctionCache::~BuiltinFunctionCache() {
  DumpStats();
}
absl::Status TestDatabaseCatalog::BuiltinFunctionCache::SetLanguageOptions(
    const LanguageOptions& options, SimpleCatalog* catalog) {
  ++total_calls_;
  const CacheEntry* cache_entry = nullptr;
  if (auto it = builtins_cache_.find(options); it != builtins_cache_.end()) {
    cache_hit_++;
    cache_entry = &it->second;
  } else {
    CacheEntry entry;
    // We have to call type_factory() while not holding mutex_.
    TypeFactory* type_factory = catalog->type_factory();
    GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionsAndTypes(
        BuiltinFunctionOptions(options), *type_factory, entry.functions,
        entry.types, entry.table_valued_functions));
    cache_entry =
        &(builtins_cache_.emplace(options, std::move(entry)).first->second);
  }

  auto builtin_function_predicate = [](const Function* fn) {
    return fn->IsGoogleSQLBuiltin();
  };

  // We need to remove all the types that are added along with builtin
  // functions, which, at the moment is limited to opaque enum types.
  auto builtin_type_predicate = [](const Type* type) {
    return type->IsEnum() && type->AsEnum()->IsOpaque();
  };

  auto builtin_tvf_predicate = [](const TableValuedFunction* tvf) {
    return tvf->IsGoogleSQLBuiltin();
  };

  catalog->RemoveFunctions(builtin_function_predicate);
  catalog->RemoveTableValuedFunctions(builtin_tvf_predicate);
  catalog->RemoveTypes(builtin_type_predicate);

  absl::flat_hash_map<std::string, const Function*> raw_functions;
  for (const auto& [name, func] : cache_entry->functions) {
    raw_functions[name] = func.get();
  }

  absl::flat_hash_map<std::string, const TableValuedFunction*> raw_tvfs;
  for (const auto& [name, tvf] : cache_entry->table_valued_functions) {
    raw_tvfs[name] = tvf.get();
  }

  return catalog->AddNonOwnedBuiltinFunctionsAndTypes(
      raw_functions, cache_entry->types, raw_tvfs);
}

absl::Status TestDatabaseCatalog::IsInitialized() const {
  if (!is_initialized_) {
    return absl::FailedPreconditionError(
        "TestDatabaseCatalog is not initialized. "
        "TestDatabaseCatalog::SetTestDatabase() must be called first, "
        "before calling any other methods.");
  }
  return absl::OkStatus();
}

void TestDatabaseCatalog::BuiltinFunctionCache::DumpStats() {
  ABSL_LOG(INFO) << "BuiltinFunctionCache: hit: " << cache_hit_ << " / "
            << total_calls_ << "("
            << (total_calls_ == 0 ? 0 : cache_hit_ * 100. / total_calls_)
            << "%) size: " << builtins_cache_.size();
}

static absl::Status LoadStaticEnum(
    const google::protobuf::EnumDescriptor* enum_descriptor, SimpleCatalog* catalog) {
  const Type* enum_type;
  GOOGLESQL_RETURN_IF_ERROR(
      catalog->type_factory()->MakeEnumType(enum_descriptor, &enum_type));
  catalog->AddTypeIfNotPresent(enum_descriptor->full_name(), enum_type);
  return absl::OkStatus();
}

static absl::Status LoadStaticProto(const google::protobuf::Descriptor* proto_descriptor,
                                    SimpleCatalog* catalog) {
  const Type* proto_type;
  GOOGLESQL_RETURN_IF_ERROR(
      catalog->type_factory()->MakeProtoType(proto_descriptor, &proto_type));
  catalog->AddTypeIfNotPresent(proto_descriptor->full_name(), proto_type);
  return absl::OkStatus();
}

absl::Status TestDatabaseCatalog::LoadProtoEnumTypes(
    const std::set<std::string>& filenames,
    const std::set<std::string>& proto_names,
    const std::set<std::string>& enum_names) {
  for (const std::string& filename : filenames) {
    GOOGLESQL_RETURN_IF_ERROR(importer_->Import(filename));
  }
  for (const auto& [name, enum_descriptor] : GetBuiltinEnumDescriptors()) {
    GOOGLESQL_RETURN_IF_ERROR(LoadStaticEnum(enum_descriptor, catalog_.get()));
  }
  for (const auto& [name, proto_descriptor] : GetBuiltinProtoDescriptors()) {
    GOOGLESQL_RETURN_IF_ERROR(LoadStaticProto(proto_descriptor, catalog_.get()));
  }

  std::set<std::string> proto_closure;
  std::set<std::string> enum_closure;
  GOOGLESQL_RETURN_IF_ERROR(ComputeTransitiveClosure(descriptor_pool(), proto_names,
                                           enum_names, &proto_closure,
                                           &enum_closure));

  const absl::flat_hash_map<absl::string_view, const google::protobuf::Descriptor*>&
      builtin_proto_descriptors = GetBuiltinProtoDescriptors();

  for (const std::string& proto : proto_closure) {
    const google::protobuf::Descriptor* descriptor =
        descriptor_pool()->FindMessageTypeByName(proto);
    auto it = builtin_proto_descriptors.find(proto);
    if (it != builtin_proto_descriptors.end()) {
      // Builtin protos should only use the global descriptor.
      GOOGLESQL_RETURN_IF_ERROR(LoadStaticProto(it->second, catalog_.get()));
    } else {
      if (!descriptor) {
        return ::googlesql_base::NotFoundErrorBuilder()
               << "Proto Message Type: " << proto;
      }
      const ProtoType* proto_type;
      GOOGLESQL_RETURN_IF_ERROR(
          catalog_->type_factory()->MakeProtoType(descriptor, &proto_type));
      catalog_->AddType(descriptor->full_name(), proto_type);
    }
  }

  const absl::flat_hash_map<absl::string_view, const google::protobuf::EnumDescriptor*>&
      builtin_enum_descriptors = GetBuiltinEnumDescriptors();

  for (const std::string& enum_name : enum_closure) {
    auto it = builtin_enum_descriptors.find(enum_name);
    if (it != builtin_enum_descriptors.end()) {
      // Builtin enums should only use the global descriptor.
      GOOGLESQL_RETURN_IF_ERROR(LoadStaticEnum(it->second, catalog_.get()));
    } else {
      const google::protobuf::EnumDescriptor* enum_descriptor =
          descriptor_pool()->FindEnumTypeByName(enum_name);
      if (!enum_descriptor) {
        return ::googlesql_base::NotFoundErrorBuilder() << "Enum Type: " << enum_name;
      }
      const EnumType* enum_type;
      GOOGLESQL_RETURN_IF_ERROR(
          catalog_->type_factory()->MakeEnumType(enum_descriptor, &enum_type));
      catalog_->AddType(enum_descriptor->full_name(), enum_type);
    }
  }
  return absl::OkStatus();
}

static std::unique_ptr<SimpleTable> MakeSimpleTable(
    const std::string& table_name, const TestTable& table) {
  const Value& array_value = table.table_as_value;
  ABSL_CHECK(array_value.type()->IsArray())
      << table_name << " " << array_value.DebugString(true);
  auto element_type = array_value.type()->AsArray()->element_type();
  std::unique_ptr<SimpleTable> simple_table;
  if (!table.options.is_value_table()) {
    // Non-value tables are represented as arrays of structs.
    const StructType* row_type = element_type->AsStruct();
    std::vector<SimpleTable::NameAndAnnotatedType> columns;
    const std::vector<const AnnotationMap*>& column_annotations =
        table.options.column_annotations();
    ABSL_CHECK(column_annotations.empty() ||
          column_annotations.size() == row_type->num_fields());
    columns.reserve(row_type->num_fields());
    for (int i = 0; i < row_type->num_fields(); i++) {
      columns.push_back(
          {row_type->field(i).name,
           {row_type->field(i).type,
            column_annotations.empty() ? nullptr : column_annotations[i]}});
    }
    simple_table = std::make_unique<SimpleTable>(table_name, columns);
  } else {
    // We got a value table. Create a table with a single column named "value".
    ABSL_CHECK(table.measure_column_defs.empty());
    std::vector<SimpleTable::NameAndAnnotatedType> columns;
    columns.push_back(
        std::make_pair("value", AnnotatedType(element_type, nullptr)));
    simple_table =
        std::make_unique<SimpleTable>(table_name, std::move(columns));
    simple_table->set_is_value_table(true);
  }
  if (!table.options.userid_column().empty()) {
    GOOGLESQL_CHECK_OK(simple_table->SetAnonymizationInfo(table.options.userid_column()));
  }
  return simple_table;
}

void TestDatabaseCatalog::AddTable(const std::string& table_name,
                                   const TestTable& table) {
  if (!table.measure_column_defs.empty()) {
    return;
  }
  std::unique_ptr<SimpleTable> simple_table =
      MakeSimpleTable(table_name, table);
  catalog_->AddOwnedTable(simple_table.release());
}

absl::Status TestDatabaseCatalog::AddTableWithStatus(
    const std::string& table_name, const TestTable& table,
    const LanguageOptions& language_options) {
  if (table.measure_column_defs.empty()) {
    std::unique_ptr<SimpleTable> simple_table =
        MakeSimpleTable(table_name, table);
    catalog_->AddOwnedTable(simple_table.release());
    return absl::OkStatus();
  }

  if (table.row_identity_columns.empty()) {
    // We are processing measure tables, validate row identity columns first.
    GOOGLESQL_RET_CHECK(
        absl::c_all_of(table.measure_column_defs,
                       [](const MeasureColumnDef& def) {
                         return def.row_identity_column_indices.has_value();
                       }))
        << "Table " << table_name
        << " does not have row identity columns, so all of its measure "
           "columns must specify column-level row identity columns";
  }
  // TODO: b/350555383 - Value tables should be supported. Remove this.
  GOOGLESQL_RET_CHECK(!table.options.is_value_table());
  const Value& array_value = table.table_as_value;
  GOOGLESQL_RET_CHECK(array_value.type()->IsArray())
      << table_name << " " << array_value.DebugString(true);
  std::unique_ptr<SimpleTable> simple_table =
      MakeSimpleTable(table_name, table);
  if (!table.row_identity_columns.empty()) {
    GOOGLESQL_RET_CHECK_OK(
        simple_table->SetRowIdentityColumns(table.row_identity_columns));
  }
  AnalyzerOptions analyzer_options(language_options);
  // Make sure that the test table's required features are enabled.
  for (LanguageFeature feature : table.options.required_features()) {
    analyzer_options.mutable_language()->EnableLanguageFeature(feature);
  }
  GOOGLESQL_ASSIGN_OR_RETURN(
      auto measure_expr_analyzer_outputs,
      AddMeasureColumnsToTable(*simple_table, table.measure_column_defs,
                               *type_factory_, *catalog_, analyzer_options));
  sql_object_artifacts_.insert(
      sql_object_artifacts_.end(),
      std::make_move_iterator(measure_expr_analyzer_outputs.begin()),
      std::make_move_iterator(measure_expr_analyzer_outputs.end()));
  catalog_->AddOwnedTable(simple_table.release());
  return absl::OkStatus();
}

absl::Status TestDatabaseCatalog::AddUdfsForMeasureDefinitions(
    const TestDatabase& test_db, const LanguageOptions& language_options) {
  GOOGLESQL_RETURN_IF_ERROR(IsInitialized());
  if (!test_db.measure_function_defs.empty()) {
    AnalyzerOptions analyzer_options(language_options);
    analyzer_options.mutable_language()->AddSupportedStatementKind(
        RESOLVED_CREATE_FUNCTION_STMT);
    auto func_opts = FunctionOptions()
                         // TODO: Relax this once IFERROR
                         //     non-determinism is mitigated.
                         .set_supports_safe_error_mode(false)
                         .set_supports_distinct_modifier(false)
                         .set_supports_having_modifier(false);
    for (const auto& stmt : test_db.measure_function_defs) {
      std::unique_ptr<const AnalyzerOutput> analyzer_output;
      GOOGLESQL_RETURN_IF_ERROR(
          AddFunctionFromCreateFunction(stmt, analyzer_options,
                                        /*allow_persistent_function=*/false,
                                        /*function_options=*/&func_opts,
                                        analyzer_output, *catalog_, *catalog_));
      sql_object_artifacts_.push_back(std::move(analyzer_output));
    }
  }
  return absl::OkStatus();
}

absl::Status TestDatabaseCatalog::FindTable(
    const absl::Span<const std::string> path, const Table** table,
    const Catalog::FindOptions& options) {
  GOOGLESQL_RETURN_IF_ERROR(IsInitialized());
  return catalog_->FindTable(path, table, options);
}

absl::Status TestDatabaseCatalog::GetTables(
    absl::flat_hash_set<const Table*>* output) const {
  GOOGLESQL_RETURN_IF_ERROR(IsInitialized());
  GOOGLESQL_RET_CHECK_NE(output, nullptr);
  GOOGLESQL_RET_CHECK(output->empty());
  return catalog_->GetTables(output);
}

absl::Status TestDatabaseCatalog::GetTypes(
    absl::flat_hash_set<const Type*>* output) const {
  GOOGLESQL_RETURN_IF_ERROR(IsInitialized());
  GOOGLESQL_RET_CHECK_NE(output, nullptr);
  GOOGLESQL_RET_CHECK(output->empty());
  return catalog_->GetTypes(output);
}

absl::Status TestDatabaseCatalog::SetTestDatabaseWithLeakyDescriptors(
    const TestDatabase& test_db) {
  importer_ = std::make_unique<ProtoImporter>(test_db.runs_as_test);
  catalog_ =
      std::make_unique<SimpleCatalog>("root_catalog", type_factory_.get());
  // Load protos and enums.
  GOOGLESQL_RETURN_IF_ERROR(LoadProtoEnumTypes(test_db.proto_files, test_db.proto_names,
                                     test_db.enum_names));

  // Add tables to the catalog.
  for (const auto& t : test_db.tables) {
    const std::string& table_name = t.first;
    const TestTable& test_table = t.second;
    AddTable(table_name, test_table);
  }
  is_initialized_ = true;
  return absl::OkStatus();
}

absl::Status TestDatabaseCatalog::SetTestDatabase(
    const TestDatabaseProto& test_db_proto,
    const LanguageOptions& language_options) {
  importer_ = std::make_unique<ProtoImporter>(test_db_proto.runs_as_test());
  catalog_ =
      std::make_unique<SimpleCatalog>("root_catalog", type_factory_.get());

  std::set<std::string> proto_files(test_db_proto.proto_files().begin(),
                                    test_db_proto.proto_files().end());
  std::set<std::string> proto_names(test_db_proto.proto_names().begin(),
                                    test_db_proto.proto_names().end());
  std::set<std::string> enum_names(test_db_proto.enum_names().begin(),
                                   test_db_proto.enum_names().end());

  // Load protos and enums.
  GOOGLESQL_RETURN_IF_ERROR(LoadProtoEnumTypes(proto_files, proto_names, enum_names));

  // With a fresh TypeFactory and DescriptorPool, we can now deserialize the
  // test database.
  GOOGLESQL_ASSIGN_OR_RETURN(TestDatabase test_db,
                   DeserializeTestDatabase(test_db_proto, type_factory_.get(),
                                           descriptor_pool()));

  // Measure definitions may use built-in functions and UDF/UDAs, add them
  // first before adding functions.
  GOOGLESQL_RETURN_IF_ERROR(SetLanguageOptions(language_options));

  // The catalog and type setup has been finished. We set `is_initialized_`
  // to true here because AddUdfsForMeasureDefinitions() checks IsInitialized()
  // and requires the catalog and types to be set up.
  is_initialized_ = true;
  if (!test_db.measure_function_defs.empty()) {
    // The UDFs and UDAs referenced by measure definitions must be added to the
    // catalog before the tables are added, so that the measures can be
    // analyzed.
    GOOGLESQL_RETURN_IF_ERROR(AddUdfsForMeasureDefinitions(test_db, language_options));
  }

  // Add tables to the catalog.
  for (const auto& t : test_db.tables) {
    const std::string& table_name = t.first;
    const TestTable& test_table = t.second;
    GOOGLESQL_RETURN_IF_ERROR(
        AddTableWithStatus(table_name, test_table, language_options));
  }
  return absl::OkStatus();
}

absl::Status TestDatabaseCatalog::SetLanguageOptions(
    const LanguageOptions& language_options) {
  if (catalog_ == nullptr) {
    return absl::FailedPreconditionError(
        "Cannot set language options since underlying catalog is unexpectedly "
        "null. TestDatabaseCatalog::SetTestDatabase() must be called first, "
        "before calling TestDatabaseCatalog::SetLanguageOptions().");
  }
  return function_cache_->SetLanguageOptions(language_options, catalog_.get());
}
}  // namespace googlesql
