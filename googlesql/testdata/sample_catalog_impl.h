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

#ifndef GOOGLESQL_TESTDATA_SAMPLE_CATALOG_IMPL_H_
#define GOOGLESQL_TESTDATA_SAMPLE_CATALOG_IMPL_H_

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "googlesql/common/measure_analysis_utils.h"
#include "googlesql/public/analyzer_output.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/property_graph.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/type.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "absl/base/nullability.h"
#include "absl/container/btree_set.h"
#include "absl/container/node_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/descriptor_database.h"

namespace googlesql {

// This is the internal implementation of SampleCatalog.
//
// It differs from SampleCatalog in that it requires a call to
// LoadCatalogImpl to finish initialization, and that returns an absl::Status.
//
// The SampleCatalog class initializes itself in its constructor, and crashes
// if initialization fails.  SampleCatalog is testonly.
class SampleCatalogImpl {
 public:
  // Constructor given an optional 'type_factory'.
  // If 'type_factory' is specified then it must outlive this catalog.
  // The catalog is not usable until LoadCatalogImpl is called.
  explicit SampleCatalogImpl(TypeFactory* type_factory = nullptr);

  // This initializes SampleCatalogImpl and returns a Status.
  // In SampleCatalog, this is called by the constructor and will
  // check-fail on error.
  // Here, it returns errors in some cases, but still has many check-fail cases.
  absl::Status LoadCatalogImpl(
      const GoogleSQLBuiltinFunctionOptions& builtin_function_options);

  SampleCatalogImpl(const SampleCatalogImpl&) = delete;
  SampleCatalogImpl& operator=(const SampleCatalogImpl&) = delete;
  virtual ~SampleCatalogImpl();

  SimpleCatalog* catalog() { return catalog_.get(); }

  TypeFactory* type_factory() { return types_; }

  // Useful for configuring EvaluatorTableIterators for tables in the catalog.
  SimpleTable* GetTableOrDie(absl::string_view name);
  absl::StatusOr<SimpleTable*> GetTable(absl::string_view name);

 private:
  std::unique_ptr<google::protobuf::DescriptorPoolDatabase> alt_descriptor_database_;
  std::unique_ptr<google::protobuf::DescriptorPool> alt_descriptor_pool_;
  std::unique_ptr<google::protobuf::DescriptorPool> ambiguous_has_descriptor_pool_;
  std::unique_ptr<TypeFactory> internal_type_factory_;
  std::unique_ptr<SimpleCatalog> catalog_;
  TypeFactory* types_;  // Not owned.

  // Returns a copy of `options` that supplies default types for function
  // signatures that expect them. This can override map entries in
  // the original `options`'s `supplied_argument_types`.
  absl::StatusOr<GoogleSQLBuiltinFunctionOptions> LoadDefaultSuppliedTypes(
      const GoogleSQLBuiltinFunctionOptions& options);
  absl::Status LoadCatalogBuiltins(
      const GoogleSQLBuiltinFunctionOptions& builtin_function_options);
  absl::Status LoadTypes();
  absl::Status LoadTables();
  absl::Status LoadProtoTables();

  // Loads tables resolvable via STRING literal table references, such as
  // FROM '/cns/cell/logs/events@100.tfrecords'.
  void LoadStringLiteralTables();

  absl::Status LoadMeasureTables(const LanguageOptions& language_options);
  absl::Status LoadBasicMeasureTables(
      const LanguageOptions& language_options,
      AnalyzerOptions& analyzer_options,
      const std::vector<std::vector<Value>>& singlekey_data,
      const std::vector<std::vector<Value>>& twokeys_data);
  absl::Status LoadDerivedMeasureTables(
      const LanguageOptions& language_options,
      AnalyzerOptions& analyzer_options,
      const std::vector<std::vector<Value>>& derived_data,
      const std::vector<std::vector<Value>>& twokeys_data,
      const std::vector<std::vector<Value>>& sales_facts_data);
  absl::Status LoadMeasureTablesWithAdvancedExprs(
      const LanguageOptions& language_options,
      AnalyzerOptions& analyzer_options,
      const std::vector<std::vector<Value>>& singlekey_data,
      const std::vector<std::vector<Value>>& complexexprs_data);
  absl::Status LoadMeasureValueTables(
      const LanguageOptions& language_options,
      AnalyzerOptions& analyzer_options,
      const std::vector<std::vector<Value>>& struct_value_data,
      const std::vector<std::vector<Value>>& int64_value_data);
  absl::Status LoadInvalidMeasureTables(AnalyzerOptions& analyzer_options);
  absl::Status LoadRowTypeMeasureTables(const LanguageOptions& language_options,
                                        AnalyzerOptions& analyzer_options);
  void LoadViews(const LanguageOptions& language_options);
  absl::Status LoadNestedCatalogs();

  absl::Status AddTableWithMeasures(
      AnalyzerOptions& analyzer_options, absl::string_view table_name,
      std::vector<const Column*> columns_not_owned,
      std::optional<absl::btree_set<int>> row_identity_column_indices,
      std::vector<MeasureColumnDef> measures, bool is_value_table,
      std::optional<std::vector<std::vector<Value>>> rows = std::nullopt);

  void AddFunctionWithArgumentType(std::string type_name, const Type* arg_type);

  // Creates and adds the Function to the catalog.
  // This performs some basic validation.
  // The group used is 'sample_functions'.
  absl::Status AddFunction(absl::string_view name, Function::Mode mode,
                           std::vector<FunctionSignature> function_signatures,
                           FunctionOptions function_options = {});

  absl::Status LoadFunctionsWithStructArgs();
  // Do not add more functions to `LoadFunctions` and `LoadFunctions2`, use
  // `RegisterForSampleCatalog` instead.
  absl::Status LoadFunctions();
  absl::Status LoadFunctions2();
  // Use `RegisterForSampleCatalog` in the impl file to register a lambda which
  // will add a catalog object - `LoadAllRegisteredCatalogChanges` calls all
  // registered lambdas to add the objects to the catalog.
  void LoadAllRegisteredCatalogChanges();
  absl::Status LoadExtendedSubscriptFunctions(
      const LanguageOptions& language_options);
  absl::Status LoadFunctionsWithDefaultArguments();
  absl::Status LoadTemplatedSQLUDFs();
  absl::Status LoadAmlBasedPropertyGraphs(
      const LanguageOptions& language_options);

  // The basic "aml" property graph is primarily used in all our analyzer tests.
  // Keeping this succinct is useful as it makes sure our analyzer tests do not
  // have very large trees while also being flexible enough to test for the
  // right cases.
  absl::Status LoadBasicAmlPropertyGraph();
  absl::Status LoadBasicAmlWithTimestampsPropertyGraph();
  absl::Status LoadAmlDmlPropertyGraph();
  // `with_timestamps` is used to determine whether to include timestamps
  // columns and properties in the property graph.
  // `includes_dml_schema` is used to determine whether to include the
  // DML test tables, columns, and properties in the property graph.
  absl::Status LoadBasicAmlPropertyGraphImpl(
      std::string property_graph_name_path, bool with_timestamps,
      bool includes_dml_schema);
  struct AmlDmlNodeTables {
    const GraphNodeTable* node_table = nullptr;
    const GraphNodeTable* node_table_no_writable_key = nullptr;
    const GraphNodeTable* node_table_no_key_prop = nullptr;
  };
  absl::StatusOr<AmlDmlNodeTables> LoadAmlDmlNodeTables(
      const std::vector<std::string>& property_graph_name_path,
      const GraphPropertyDeclaration* id_prop_dcl_raw,
      std::vector<std::unique_ptr<const GraphNodeTable>>& node_tables,
      std::vector<std::unique_ptr<const GraphElementLabel>>& labels,
      std::vector<std::unique_ptr<const GraphPropertyDeclaration>>&
          property_dcls);
  absl::Status LoadAmlDmlEdgeTables(
      const std::vector<std::string>& property_graph_name_path,
      const GraphPropertyDeclaration* id_prop_dcl_raw,
      const AmlDmlNodeTables& node_tables_ref,
      std::vector<std::unique_ptr<const GraphEdgeTable>>& edge_tables,
      std::vector<std::unique_ptr<const GraphElementLabel>>& labels,
      std::vector<std::unique_ptr<const GraphPropertyDeclaration>>&
          property_dcls);
  absl::Status LoadAmlDmlSchema(
      const std::vector<std::string>& property_graph_name_path,
      const GraphPropertyDeclaration* id_prop_dcl_raw,
      std::vector<std::unique_ptr<const GraphNodeTable>>& node_tables,
      std::vector<std::unique_ptr<const GraphEdgeTable>>& edge_tables,
      std::vector<std::unique_ptr<const GraphElementLabel>>& labels,
      std::vector<std::unique_ptr<const GraphPropertyDeclaration>>&
          property_dcls);

  absl::Status LoadEnhancedAmlPropertyGraph();

  absl::Status LoadMultiSrcDstEdgePropertyGraphs();
  absl::Status LoadCompositeKeyPropertyGraphs();
  absl::Status LoadPropertyGraphWithDynamicLabelAndProperties();
  absl::Status LoadPropertyGraphWithReadOnlyDynamicProperties();
  absl::Status LoadPropertyGraphWithDynamicMultiLabelsAndProperties();
  absl::Status LoadPropertyGraphWithMixedDynamicAndStaticTables();

  // Loads several table-valued functions into the sample catalog. For a full
  // list of the signatures added, please see the beginning of the method
  // definition. LoadTableValuedFunctions() has gotten so large that we have to
  // split it up in order to avoid lint warnings.
  absl::Status LoadTableValuedFunctions1();
  absl::Status LoadTableValuedFunctions2();
  void LoadTableValuedFunctionsWithEvaluators();
  void LoadTVFWithExtraColumns();
  void LoadConnectionTableValuedFunctions();
  void LoadDescriptorTableValuedFunctions();
  void LoadTableValuedFunctionsWithDeprecationWarnings();
  void LoadTableValuedFunctionsWithMultipleSignatures();
  absl::Status LoadTvfsWithTableSchema();

  // Add a SQL table function to catalog starting from a full create table
  // function statement.
  absl::Status AddSqlDefinedTableFunctionFromCreate(
      absl::string_view create_table_function,
      const LanguageOptions& language_options,
      absl::string_view user_id_column = "");
  absl::Status LoadNonTemplatedSqlTableValuedFunctions(
      const LanguageOptions& language_options);
  absl::Status LoadTemplatedSQLTableValuedFunctions();
  absl::Status LoadTableValuedFunctionsWithAnonymizationUid();
  void LoadTableValuedFunctionsWithOptionalRelations();

  void AddProcedureWithArgumentType(std::string type_name,
                                    const Type* arg_type);
  absl::Status LoadProcedures();
  absl::Status LoadConstants();
  void LoadConnections();
  void LoadSequences();
  // Load signatures for well known functional programming functions for example
  // FILTER, TRANSFORM, REDUCE.
  absl::Status LoadWellKnownLambdaArgFunctions();
  // Contrived signatures are loaded in order to demonstrate the behavior of
  // lambda signature matching and resolving for unusual cases.
  // This include:
  //  * Using lambda with repeated arguments.
  //  * Using lambda with named arguments.
  //  * Possible signatures that could result in type inference failure for
  //  various combinations of templated lambda arguments and other arguments.
  absl::Status LoadContrivedLambdaArgFunctions();

  void AddOwnedTable(std::unique_ptr<SimpleTable> table);
  void AddOwnedTable(SimpleTable* table);
  absl::Status AddGeneratedColumnToTable(
      std::string column_name, std::vector<std::string> expression_columns,
      std::string generated_expr, SimpleTable* table);

  // Add a SQLFunction to catalog_ with a SQL expression as the function body.
  absl::Status AddSqlDefinedFunction(
      absl::string_view name, FunctionSignature signature,
      const std::vector<std::string>& argument_names,
      absl::string_view function_body_sql,
      const LanguageOptions& language_options);
  // Add a SQL function to catalog starting from a full create_function
  // statement.
  absl::Status AddSqlDefinedFunctionFromCreate(
      absl::string_view create_function,
      const LanguageOptions& language_options, bool inline_sql_functions = true,
      const FunctionOptions* /*absl_nullable*/ function_options = nullptr);

  absl::Status LoadSqlFunctions(const LanguageOptions& language_options);

  // Helpers for LoadSqlFunctions so that its both logically broken up and
  // so that its less troublesome for dbg build stacks.
  absl::Status LoadScalarSqlFunctions(const LanguageOptions& language_options);
  absl::Status LoadScalarSqlFunctionsFromStandardModule(
      const LanguageOptions& language_options);
  absl::Status LoadDeepScalarSqlFunctions(
      const LanguageOptions& language_options);
  absl::Status LoadScalarSqlFunctionTemplates(
      const LanguageOptions& language_options);
  absl::Status LoadAggregateSqlFunctions(
      const LanguageOptions& language_options);

  // Load objects for testing ROW type ((broken link)). Some of the
  // objects here are experimental, their behavior and interfaces may change.
  // TODO: b/452955184 - Update this comment once proposal is finalized.
  absl::Status LoadRowTypeObjects(const LanguageOptions& language_options);

  // This can be used force linking of a proto for the generated_pool.
  // This may be required if a proto is referenced in file-based tests
  // (such as analyzer test), but not otherwise directly linked.
  // We don't force linking the entire test_schema since we may need
  // to test this partial-linkage in other contexts (and it's expensive).
  // Note, this function isn't actually called. But it _does_ need to be
  // defined in the class to ensure it can't be pruned.
  // This is a all weird linker magic.
  void ForceLinkProtoTypes();

  absl::StatusOr<const ProtoType*> GetProtoType(
      const google::protobuf::Descriptor* descriptor);
  absl::StatusOr<const EnumType*> GetEnumType(
      const google::protobuf::EnumDescriptor* descriptor);

  const ArrayType* int32array_type_;
  const ArrayType* int64array_type_;
  const ArrayType* uint32array_type_;
  const ArrayType* uint64array_type_;
  const ArrayType* bytes_array_type_;
  const ArrayType* bool_array_type_;
  const ArrayType* float_array_type_;
  const ArrayType* double_array_type_;
  const ArrayType* date_array_type_;
  const ArrayType* string_array_type_;
  const ArrayType* timestamp_array_type_;
  const ArrayType* proto_array_type_;
  const ArrayType* struct_array_type_;
  const ArrayType* json_array_type_;
  const ArrayType* numeric_array_type_;
  const ArrayType* bignumeric_array_type_;
  const ArrayType* interval_array_type_;

  const EnumType* enum_TestEnum_;
  const EnumType* enum_AnotherTestEnum_;
  const EnumType* enum_TestEnumWithAnnotations_;
  const ProtoType* proto_KitchenSinkPB_;
  const ProtoType* proto_MessageWithKitchenSinkPB_;
  const ProtoType* proto_CivilTimeTypesSinkPB_;
  const ProtoType* proto_TestExtraPB_;
  const ProtoType* proto_TimeSeriesPB_;
  const ProtoType* proto_abPB_;
  const ProtoType* proto_bcPB_;

  const ProtoType* proto_EmptyMessage_;
  const ProtoType* proto3_KitchenSinkPB_;
  const ProtoType* proto3_MessageWithInvalidMap_;
  const ProtoType* proto_ambiguous_has_;
  const ProtoType* proto_field_formats_proto_;
  const ProtoType* proto_MessageWithMapField_;
  const ProtoType* proto_approx_distance_function_options_;
  const ProtoType* proto_test_hop_and_tumble_pb_;

  // STRUCT<a INT32, b STRING>
  const StructType* struct_type_;
  // STRUCT<c INT32, d STRUCT<a INT32, b STRING>>
  const StructType* nested_struct_type_;
  // STRUCT<e INT32, f STRUCT<c INT32, d STRUCT<a INT32, b STRING>>>
  const StructType* doubly_nested_struct_type_;
  // STRUCT<ambiguous_field INT32, ambiguous_field STRING,
  //        proto_field TestHopAndTumblePB>
  const StructType* struct_with_proto_type_;
  // STRUCT<x INT64, y STRUCT<a INT32, b STRING>,
  //        z ARRAY<STRUCT<a INT32, b STRING>>>
  const StructType* struct_with_array_field_type_;
  // STRUCT<x INT64>
  const StructType* struct_with_one_field_type_;
  // STRUCT<kitchen_sink KitchenSinkPB, s STRUCT<kitchen_sink KitchenSinkPB>>
  const StructType* struct_with_kitchen_sink_type_;
  // STRUCT<a INT64, b ARRAY<STRUCT<kitchen_sink KitchenSinkPB>>>
  const StructType* struct_of_array_of_struct_with_kitchen_sink_type_;

  const Type* int32map_type_;
  const Type* int64map_type_;
  const Type* bytesmap_type_;

  const SimpleTable* key_value_table_;

  // A constant to load. Owned by this catalog to get coverage for
  // SimpleCatalog::AddConstant().
  std::unique_ptr<SimpleConstant> owned_constant_;

  // Pointers are owned by 'catalog_'.
  absl::node_hash_map<std::string, SimpleTable*> tables_;

  // Connections owned by this catalog.
  std::unordered_map<std::string, std::unique_ptr<SimpleConnection>>
      owned_connections_;

  // Sequences owned by this catalog.
  std::unordered_map<std::string, std::unique_ptr<SimpleSequence>>
      owned_sequences_;

  // Manages the lifetime of ResolvedAST objects for SQL defined statements like
  // views, SQL functions, column expressions, or SQL TVFs.
  std::vector<std::unique_ptr<const AnalyzerOutput>> sql_object_artifacts_;

  std::vector<std::unique_ptr<const ResolvedExpr>>
      owned_resolved_graph_property_definitions_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_TESTDATA_SAMPLE_CATALOG_IMPL_H_
