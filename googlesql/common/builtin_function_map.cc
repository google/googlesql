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

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/common/builtin_function_internal.h"
#include "googlesql/common/errors.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/annotation/collation.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/input_argument_type.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/types/map_type.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "absl/functional/bind_front.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

namespace {
constexpr absl::string_view kMapFromArray = "MAP_FROM_ARRAY";
constexpr absl::string_view kMapEntriesSorted = "MAP_ENTRIES_SORTED";
constexpr absl::string_view kMapEntriesUnsorted = "MAP_ENTRIES_UNSORTED";
}  // namespace

static absl::Status CheckMapFromArrayPreResolutionArguments(
    absl::Span<const InputArgumentType> arguments,
    const LanguageOptions& language_options) {
  if (arguments.size() != 1) {
    return MakeSqlError() << "No matching signature for function "
                          << kMapFromArray
                          << ". Supported signature: " << kMapFromArray
                          << "(ARRAY<STRUCT<T1, T2>>)";
  }

  if (arguments[0].is_untyped()) {
    return MakeSqlError()
           << kMapFromArray
           << " result type cannot be determined from "
              "argument "
           << arguments[0].UserFacingName(language_options.product_mode())
           << ". Consider casting the argument to ARRAY<STRUCT<T1, T2>> so "
              "that key type T1 and value type T2 can be determined from the "
              "argument";
  }
  return absl::OkStatus();
}

static absl::StatusOr<const Type*> ComputeMapFromArrayResultType(
    Catalog* catalog, TypeFactory* type_factory, CycleDetector* cycle_detector,
    const FunctionSignature& signature,
    absl::Span<const InputArgumentType> arguments,
    const AnalyzerOptions& analyzer_options) {
  GOOGLESQL_RET_CHECK_EQ(arguments.size(), 1);
  auto& input_argument = arguments[0];

  auto make_error_struct_arr_expected =
      [&]() {
        return MakeSqlError()
               << kMapFromArray
               << " input argument must be an array of structs, but got type "
               << input_argument.type()->TypeName(
                      analyzer_options.language().product_mode());
      };
  if (!input_argument.type()->IsArray()) {
    return make_error_struct_arr_expected();
  }

  auto* array_element_type = input_argument.type()->AsArray()->element_type();

  if (!array_element_type->IsStruct()) {
    return make_error_struct_arr_expected();
  }

  auto* struct_type = array_element_type->AsStruct();
  if (struct_type->num_fields() != 2) {
    return MakeSqlError()
           << kMapFromArray << " input array must be of type "
           << "ARRAY<STRUCT<T1, T2>>, but found a struct member with "
           << struct_type->num_fields() << " fields";
  }

  if (!struct_type->field(0).type->SupportsGrouping(
          analyzer_options.language())) {
    return MakeSqlError() << kMapFromArray
                          << " expected a groupable key, but got a key of type "
                          << struct_type->field(0).type->TypeName(
                                 analyzer_options.language().product_mode())
                          << ", which does not support grouping";
  }
  return type_factory->MakeMapType(struct_type->field(0).type,
                                   struct_type->field(1).type);
}

static absl::StatusOr<const Type*> ComputeMapEntriesFunctionResultType(
    absl::string_view function_name, bool require_orderable_key,
    Catalog* catalog, TypeFactory* type_factory, CycleDetector* cycle_detector,
    const FunctionSignature& signature,
    absl::Span<const InputArgumentType> arguments,
    const AnalyzerOptions& analyzer_options) {
  GOOGLESQL_RET_CHECK_EQ(arguments.size(), 1);
  auto& input_argument = arguments[0];

  if (!input_argument.type()->IsMap()) {
    return MakeSqlError()
           << function_name
           << " input argument must be of type MAP<K, V>, but got type "
           << arguments[0].type()->TypeName(
                  analyzer_options.language().product_mode());
  }

  const Type* map_key_type = GetMapKeyType(input_argument.type());
  std::string ordering_type_description;
  if (require_orderable_key &&
      !map_key_type->SupportsOrdering(analyzer_options.language(),
                                      &ordering_type_description)) {
    return MakeSqlError() << function_name
                          << " map key type must be orderable, but was not: "
                          << ordering_type_description << " is not orderable";
  }

  const Type* struct_type;
  GOOGLESQL_RET_CHECK_OK(type_factory->MakeStructType(
      {StructType::StructField("key", map_key_type),
       StructType::StructField("value",
                               GetMapValueType(input_argument.type()))},
      &struct_type));

  const Type* array_type;
  GOOGLESQL_RET_CHECK_OK(type_factory->MakeArrayType(struct_type, &array_type));
  return array_type;
}

static inline absl::StatusOr<const MapType*> GetMapTypeFromInputArg(
    const InputArgumentType& map_arg) {
  GOOGLESQL_RET_CHECK(map_arg.type() != nullptr && map_arg.type()->IsMap())
      << "Input must be a map";
  return map_arg.type()->AsMap();
}

static inline absl::Status MakeErrorIfMapElementTypeNotOrderable(
    absl::string_view function_name, const Type* orderable_type,
    const LanguageOptions& language_options) {
  std::string ordering_type_description;
  if (!orderable_type->SupportsOrdering(language_options,
                                        &ordering_type_description)) {
    return MakeSqlError() << function_name << ": MAP element type "
                          << ordering_type_description << " is not orderable";
  }
  return absl::OkStatus();
}

enum class KeyOrValueSelector {
  kKey,
  kValue,
};

static absl::Status CheckOrderableMapArgumentConstraint(
    absl::string_view fn_name, int map_arg_idx, KeyOrValueSelector key_or_value,
    const FunctionSignature& unused_matched_signature,
    absl::Span<const InputArgumentType> arguments,
    const LanguageOptions& language_options) {
  GOOGLESQL_RET_CHECK_EQ(arguments.size(), 1);
  GOOGLESQL_ASSIGN_OR_RETURN(const MapType* map_type,
                   GetMapTypeFromInputArg(arguments[map_arg_idx]));

  const Type* orderable_type = (key_or_value == KeyOrValueSelector::kKey)
                                   ? map_type->key_type()
                                   : map_type->value_type();

  return MakeErrorIfMapElementTypeNotOrderable(fn_name, orderable_type,
                                               language_options);
}

// The `ComputeResultAnnotationsCallback` used by MAP_FROM_ARRAY().
// It cannot express the full templated relationship between arguments because
// the key and value types are hidden under a struct type on the input side.
static absl::StatusOr<const AnnotationMap*>
ComputeMapFromArrayResultAnnotations(
    const ResolvedFunctionCallBase& function_call, TypeFactory& type_factory) {
  const Type* result_type = function_call.type();
  GOOGLESQL_RET_CHECK(result_type->IsMap());

  GOOGLESQL_RET_CHECK_EQ(function_call.argument_list_size(), 1);
  const AnnotationMap* arg_annotation_map =
      function_call.argument_list(0)->type_annotation_map();

  if (arg_annotation_map == nullptr) {
    // Nothing to propagate.
    return nullptr;
  }

  // The input is an ARRAY<STRUCT<K, V>>. The top level is an array, so its
  // annotation map must have exactly 1 field.
  GOOGLESQL_RET_CHECK(arg_annotation_map->IsStructMap());
  GOOGLESQL_RET_CHECK_EQ(arg_annotation_map->AsStructMap()->num_fields(), 1);

  // The AnnotationMap structure is the same for the output MapType, after
  // removing the wrapper corresponding to the ARRAY.
  const AnnotationMap* element_annotation_map =
      arg_annotation_map->AsStructMap()->field(0);
  GOOGLESQL_RET_CHECK(element_annotation_map->IsStructMap());
  GOOGLESQL_RET_CHECK_EQ(element_annotation_map->AsStructMap()->num_fields(), 2);
  GOOGLESQL_RET_CHECK(element_annotation_map->HasCompatibleStructure(result_type));

  // Finally, make sure there's no collation anywhere on the key, not even on
  // its component types (e.g. if the key is STRUCT<STRING, INT64>).
  const AnnotationMap* key_annotation_map =
      element_annotation_map->AsStructMap()->field(0);

  if (CollationAnnotation::ExistsIn(key_annotation_map)) {
    return MakeSqlError()
           << "Collation is not allowed on the key of a MAP or any part of it. "
              "Use COLLATE(str, '') to remove collation from string values";
  }

  return element_annotation_map;
}

// The `ComputeResultAnnotationsCallback` used by MAP_ENTRIES_[UN]SORTED().
// It cannot express the full templated relationship between arguments because
// the key and value types are hidden under a struct type on the input side.
// This is the opposite of MAP_FROM_ARRAY, above, where it's the output that
// is ARRAY<STRUCT<K, V>>.
static absl::StatusOr<const AnnotationMap*> ComputeMapEntriesResultAnnotations(
    const ResolvedFunctionCallBase& function_call, TypeFactory& type_factory) {
  const Type* result_type = function_call.type();
  GOOGLESQL_RET_CHECK(result_type->IsArray());
  GOOGLESQL_RET_CHECK(result_type->AsArray()->element_type()->IsStruct());
  GOOGLESQL_RET_CHECK_EQ(result_type->AsArray()->element_type()->AsStruct()->num_fields(),
               2);

  GOOGLESQL_RET_CHECK_EQ(function_call.argument_list_size(), 1);
  const AnnotationMap* arg_annotation_map =
      function_call.argument_list(0)->type_annotation_map();

  if (arg_annotation_map == nullptr) {
    // Nothing to propagate.
    return nullptr;
  }

  // The input is MAP<K, V>, which is represented as a composite annotation
  // map with 2 fields.
  GOOGLESQL_RET_CHECK(arg_annotation_map->IsStructMap());
  GOOGLESQL_RET_CHECK_EQ(arg_annotation_map->AsStructMap()->num_fields(), 2);

  // The input map corresponds to STRUCT<K, V>. Now we need to wrap it in an
  // AnnotationMap corresponding to the ARRAY<STRUCT<K, V>>.
  std::unique_ptr<AnnotationMap> output_annotation_map =
      AnnotationMap::Create(result_type);

  GOOGLESQL_RET_CHECK(output_annotation_map->IsStructMap());
  GOOGLESQL_RET_CHECK_EQ(output_annotation_map->AsStructMap()->num_fields(), 1);
  GOOGLESQL_RETURN_IF_ERROR(output_annotation_map->AsStructMap()->CloneIntoField(
      0, arg_annotation_map));

  GOOGLESQL_RET_CHECK(output_annotation_map->HasCompatibleStructure(result_type));

  // The AnnotationMap structure is the same for the output MapType, after
  // removing the wrapper corresponding to the ARRAY.
  return type_factory.TakeOwnership(std::move(output_annotation_map));
}

void GetMapCoreFunctions(TypeFactory* type_factory,
                         const GoogleSQLBuiltinFunctionOptions& options,
                         NameToFunctionMap* functions) {
  // MAP_FROM_ARRAY(ARRAY<STRUCT<K,V>> entries) -> MAP<K,V>
  InsertFunction(
      functions, options, "map_from_array", Function::SCALAR,
      {{ARG_TYPE_ARBITRARY,
        {ARG_ARRAY_TYPE_ANY_1},
        FN_MAP_FROM_ARRAY,
        // TODO: Collation support for MAP<> type.
        FunctionSignatureOptions()
            .set_rejects_collation()
            .set_compute_result_annotations_callback(
                &ComputeMapFromArrayResultAnnotations)}},
      FunctionOptions()
          .set_compute_result_type_callback(&ComputeMapFromArrayResultType)
          .set_pre_resolution_argument_constraint(
              &CheckMapFromArrayPreResolutionArguments)
          .AddRequiredLanguageFeature(FEATURE_MAP_TYPE));

  // MAP_ENTRIES_SORTED(MAP<K,V> input_map) -> ARRAY<STRUCT<K,V>>
  InsertFunction(
      functions, options, "map_entries_sorted", Function::SCALAR,
      {{ARG_TYPE_ARBITRARY,
        {ARG_MAP_TYPE_ANY_1_2},
        FN_MAP_ENTRIES_SORTED,
        FunctionSignatureOptions()
            .set_rejects_collation()
            .set_compute_result_annotations_callback(
                &ComputeMapEntriesResultAnnotations)}},
      FunctionOptions()
          .set_compute_result_type_callback(absl::bind_front(
              &ComputeMapEntriesFunctionResultType, kMapEntriesSorted,
              /*require_orderable_key=*/true))
          .AddRequiredLanguageFeature(FEATURE_MAP_TYPE));

  InsertFunction(
      functions, options, "map_entries_unsorted", Function::SCALAR,
      {
          {ARG_TYPE_ARBITRARY,
           {ARG_MAP_TYPE_ANY_1_2},
           FN_MAP_ENTRIES_UNSORTED,
           FunctionSignatureOptions()
               .set_rejects_collation()
               .set_compute_result_annotations_callback(
                   &ComputeMapEntriesResultAnnotations)},
      },
      FunctionOptions()
          .set_compute_result_type_callback(absl::bind_front(
              &ComputeMapEntriesFunctionResultType, kMapEntriesUnsorted,
              /*require_orderable_key=*/false))
          .AddRequiredLanguageFeature(FEATURE_MAP_TYPE));

  const FunctionArgumentType input_map_argument_type{
      ARG_MAP_TYPE_ANY_1_2, FunctionArgumentTypeOptions().set_argument_name(
                                "input_map", kPositionalOnly)};

  constexpr absl::string_view kMapGetSql = R"sql(
      IF(
        input_map IS NULL,
        NULL,
        IF(
          NOT EXISTS(
            SELECT entries
            FROM UNNEST(MAP_ENTRIES_UNSORTED(input_map)) AS entries
            WHERE entries.key IS NOT DISTINCT FROM lookup_key),
          default_value,
          (SELECT entries.value
            FROM UNNEST(MAP_ENTRIES_UNSORTED(input_map)) AS entries
            WHERE entries.key IS NOT DISTINCT FROM lookup_key)
      ))
    )sql";
  InsertFunction(
      functions, options, "map_get", Function::SCALAR,
      {
          {ARG_TYPE_ANY_2,
           {input_map_argument_type,
            {ARG_TYPE_ANY_1, FunctionArgumentTypeOptions().set_argument_name(
                                 "lookup_key", kPositionalOnly)},
            {ARG_TYPE_ANY_2,
             FunctionArgumentTypeOptions()
                 .set_argument_name("default_value", kPositionalOnly)
                 .set_cardinality(FunctionEnums::OPTIONAL)}},
           FN_MAP_GET,
           FunctionSignatureOptions()
               .set_rejects_collation()
               .set_rewrite_options(
                   FunctionSignatureRewriteOptions()
                       .set_rewriter(REWRITE_BUILTIN_FUNCTION_INLINER)
                       .set_sql(kMapGetSql))},
      },
      FunctionOptions().AddRequiredLanguageFeature(FEATURE_MAP_TYPE));

  constexpr absl::string_view kMapContainsKeySql = R"sql(
      IF(
        input_map IS NULL,
        NULL,
        EXISTS(
          SELECT entries
          FROM UNNEST(MAP_ENTRIES_UNSORTED(input_map)) AS entries
          WHERE entries.key IS NOT DISTINCT FROM lookup_key))
    )sql";
  InsertFunction(
      functions, options, "map_contains_key", Function::SCALAR,
      {
          {type_factory->get_bool(),
           {input_map_argument_type,
            {ARG_TYPE_ANY_1, FunctionArgumentTypeOptions().set_argument_name(
                                 "lookup_key", kPositionalOnly)}},
           FN_MAP_CONTAINS_KEY,
           FunctionSignatureOptions()
               .set_rejects_collation()
               .set_rewrite_options(
                   FunctionSignatureRewriteOptions()
                       .set_rewriter(REWRITE_BUILTIN_FUNCTION_INLINER)
                       .set_sql(kMapContainsKeySql))},
      },
      FunctionOptions().AddRequiredLanguageFeature(FEATURE_MAP_TYPE));

  constexpr absl::string_view kMapKeysSortedSql = R"sql(
      (SELECT FLATTEN(MAP_ENTRIES_SORTED(input_map).key))
    )sql";
  InsertFunction(
      functions, options, "map_keys_sorted", Function::SCALAR,
      {
          {ARG_ARRAY_TYPE_ANY_1,
           {input_map_argument_type},
           FN_MAP_KEYS_SORTED,
           FunctionSignatureOptions()
               .set_rejects_collation()
               .set_rewrite_options(
                   FunctionSignatureRewriteOptions()
                       .set_rewriter(REWRITE_BUILTIN_FUNCTION_INLINER)
                       .set_sql(kMapKeysSortedSql))},
      },
      FunctionOptions()
          .set_post_resolution_argument_constraint(absl::bind_front(
              CheckOrderableMapArgumentConstraint, "MAP_KEYS_SORTED",
              /*map_arg_idx=*/0, KeyOrValueSelector::kKey))
          .AddRequiredLanguageFeature(FEATURE_MAP_TYPE));

  constexpr absl::string_view kMapKeysUnsortedSql = R"sql(
      (SELECT FLATTEN(MAP_ENTRIES_UNSORTED(input_map).key))
    )sql";
  InsertFunction(
      functions, options, "map_keys_unsorted", Function::SCALAR,
      {
          {ARG_ARRAY_TYPE_ANY_1,
           {input_map_argument_type},
           FN_MAP_KEYS_UNSORTED,
           FunctionSignatureOptions()
               .set_rejects_collation()
               .set_rewrite_options(
                   FunctionSignatureRewriteOptions()
                       .set_rewriter(REWRITE_BUILTIN_FUNCTION_INLINER)
                       .set_sql(kMapKeysUnsortedSql))},
      },
      FunctionOptions().AddRequiredLanguageFeature(FEATURE_MAP_TYPE));

  constexpr absl::string_view kMapValuesSortedSql = R"sql(
      (
        SELECT IF(
          input_map IS NULL,
          NULL,
          ARRAY(SELECT entries.value
                  FROM UNNEST(MAP_ENTRIES_UNSORTED(input_map)) AS entries
                  ORDER BY entries.value))
      )
    )sql";
  InsertFunction(
      functions, options, "map_values_sorted", Function::SCALAR,
      {
          {ARG_ARRAY_TYPE_ANY_2,
           {input_map_argument_type},
           FN_MAP_VALUES_SORTED,
           FunctionSignatureOptions()
               .set_rejects_collation()
               .set_rewrite_options(
                   FunctionSignatureRewriteOptions()
                       .set_rewriter(REWRITE_BUILTIN_FUNCTION_INLINER)
                       .set_sql(kMapValuesSortedSql))},
      },
      FunctionOptions()
          .set_post_resolution_argument_constraint(absl::bind_front(
              CheckOrderableMapArgumentConstraint, "MAP_VALUES_SORTED", 0,
              KeyOrValueSelector::kValue))
          .AddRequiredLanguageFeature(FEATURE_MAP_TYPE));

  constexpr absl::string_view kMapValuesUnsortedSql = R"sql(
      (SELECT FLATTEN(MAP_ENTRIES_UNSORTED(input_map).value))
    )sql";
  InsertFunction(
      functions, options, "map_values_unsorted", Function::SCALAR,
      {
          {ARG_ARRAY_TYPE_ANY_2,
           {input_map_argument_type},
           FN_MAP_VALUES_UNSORTED,
           FunctionSignatureOptions()
               .set_rejects_collation()
               .set_rewrite_options(
                   FunctionSignatureRewriteOptions()
                       .set_rewriter(REWRITE_BUILTIN_FUNCTION_INLINER)
                       .set_sql(kMapValuesUnsortedSql))},
      },
      FunctionOptions().AddRequiredLanguageFeature(FEATURE_MAP_TYPE));

  constexpr absl::string_view kMapValuesSortedByKeySql = R"sql(
      (SELECT FLATTEN(MAP_ENTRIES_SORTED(input_map).value))
    )sql";
  InsertFunction(
      functions, options, "map_values_sorted_by_key", Function::SCALAR,
      {
          {ARG_ARRAY_TYPE_ANY_2,
           {input_map_argument_type},
           FN_MAP_VALUES_SORTED_BY_KEY,
           FunctionSignatureOptions()
               .set_rejects_collation()
               .set_rewrite_options(
                   FunctionSignatureRewriteOptions()
                       .set_rewriter(REWRITE_BUILTIN_FUNCTION_INLINER)
                       .set_sql(kMapValuesSortedByKeySql))},
      },
      FunctionOptions()
          .set_post_resolution_argument_constraint(absl::bind_front(
              CheckOrderableMapArgumentConstraint, "MAP_VALUES_SORTED_BY_KEY",
              0, KeyOrValueSelector::kKey))
          .AddRequiredLanguageFeature(FEATURE_MAP_TYPE));

  constexpr absl::string_view kMapEmptySql = R"sql(
      (SELECT ARRAY_LENGTH(MAP_ENTRIES_UNSORTED(input_map)) = 0)
    )sql";
  InsertFunction(
      functions, options, "map_empty", Function::SCALAR,
      {
          {type_factory->get_bool(),
           {input_map_argument_type},
           FN_MAP_EMPTY,
           FunctionSignatureOptions()
               .set_rejects_collation()
               .set_rewrite_options(
                   FunctionSignatureRewriteOptions()
                       .set_rewriter(REWRITE_BUILTIN_FUNCTION_INLINER)
                       .set_sql(kMapEmptySql))},
      },
      FunctionOptions().AddRequiredLanguageFeature(FEATURE_MAP_TYPE));

  const FunctionArgumentTypeList arglist_map_and_kv_pairs = {
      input_map_argument_type,
      {ARG_TYPE_ANY_1,
       FunctionArgumentTypeOptions().set_argument_name("key", kPositionalOnly)},
      {ARG_TYPE_ANY_2, FunctionArgumentTypeOptions().set_argument_name(
                           "value", kPositionalOnly)},
      // The function library treats multiple repeated arguments as interleaved.
      // Thus, this creates the signature of (MAP<K,V>, K, V, K, V, ...).
      {ARG_TYPE_ANY_1, FunctionArgumentTypeOptions()
                           .set_argument_name("repeated_key", kPositionalOnly)
                           .set_cardinality(FunctionEnums::REPEATED)},
      {ARG_TYPE_ANY_2, FunctionArgumentTypeOptions()
                           .set_argument_name("repeated_value", kPositionalOnly)
                           .set_cardinality(FunctionEnums::REPEATED)},
  };

  FunctionOptions map_insert_function_options =
      FunctionOptions().AddRequiredLanguageFeature(FEATURE_MAP_TYPE);
  InsertFunction(
      functions, options, "map_insert", Function::SCALAR,
      {{ARG_MAP_TYPE_ANY_1_2, arglist_map_and_kv_pairs, FN_MAP_INSERT,
        FunctionSignatureOptions().set_rejects_collation()}},
      map_insert_function_options);
  InsertFunction(functions, options, "map_insert_or_replace", Function::SCALAR,
                 {{ARG_MAP_TYPE_ANY_1_2, arglist_map_and_kv_pairs,
                   FN_MAP_INSERT_OR_REPLACE,
                   FunctionSignatureOptions().set_rejects_collation()}},
                 map_insert_function_options);
  InsertFunction(
      functions, options, "map_replace", Function::SCALAR,
      {
          {ARG_MAP_TYPE_ANY_1_2, arglist_map_and_kv_pairs,
           FN_MAP_REPLACE_KV_PAIRS,
           FunctionSignatureOptions().set_rejects_collation()},
          {ARG_MAP_TYPE_ANY_1_2,
           {input_map_argument_type,
            {ARG_TYPE_ANY_1, FunctionArgumentTypeOptions().set_argument_name(
                                 "key", kPositionalOnly)},
            {ARG_TYPE_ANY_1,
             FunctionArgumentTypeOptions()
                 .set_argument_name("repeated_key", kPositionalOnly)
                 .set_cardinality(FunctionEnums::REPEATED)},
            FunctionArgumentType::Lambda(
                {ARG_TYPE_ANY_2}, ARG_TYPE_ANY_2,
                FunctionArgumentTypeOptions().set_argument_name(
                    "updater_lambda", kPositionalOnly))},
           FN_MAP_REPLACE_K_REPEATED_V_LAMBDA,
           FunctionSignatureOptions().set_rejects_collation()},
      },
      map_insert_function_options);
  InsertFunction(
      functions, options, "map_cardinality", Function::SCALAR,
      {{type_factory->get_int64(),
        {input_map_argument_type},
        FN_MAP_CARDINALITY,
        FunctionSignatureOptions().set_rejects_collation().set_rewrite_options(
            FunctionSignatureRewriteOptions()
                .set_rewriter(REWRITE_BUILTIN_FUNCTION_INLINER)
                .set_sql(R"sql(
                    (SELECT ARRAY_LENGTH(MAP_ENTRIES_UNSORTED(input_map)))
                  )sql"))}},
      FunctionOptions().AddRequiredLanguageFeature(FEATURE_MAP_TYPE));
  InsertFunction(
      functions, options, "map_delete", Function::SCALAR,
      {{ARG_MAP_TYPE_ANY_1_2,
        {input_map_argument_type,
         {ARG_TYPE_ANY_1,
          FunctionArgumentTypeOptions().set_argument_name(
              "key", kPositionalOnly)},  // At least one key must be provided.
         {ARG_TYPE_ANY_1,
          FunctionArgumentTypeOptions()
              .set_argument_name("repeated_key", kPositionalOnly)
              .set_cardinality(FunctionEnums::REPEATED)}},
        FN_MAP_DELETE,
        FunctionSignatureOptions().set_rejects_collation()}},
      FunctionOptions().AddRequiredLanguageFeature(FEATURE_MAP_TYPE));
  // TODO: b/431223433 - Ideally, rewrite should use ARRAY_FILTER, but rewriter
  // calling a lambda within a lambda shape is not supported yet.
  constexpr absl::string_view kMapFilterSql = R"sql(
    (
      IF(
        input_map IS NULL,
        NULL,
        MAP_FROM_ARRAY(
          ARRAY_FILTER(
              MAP_ENTRIES_UNSORTED(input_map),
              map_entry -> WITH(
                              key AS map_entry.key,
                              value AS map_entry.value,
                              condition(key,value))
          )
        )
      )
    )
    )sql";
  InsertFunction(
      functions, options, "map_filter", Function::SCALAR,
      {{ARG_MAP_TYPE_ANY_1_2,
        {input_map_argument_type,
         FunctionArgumentType::Lambda(
             {ARG_TYPE_ANY_1, ARG_TYPE_ANY_2}, type_factory->get_bool(),
             FunctionArgumentTypeOptions().set_argument_name("condition",
                                                             kPositionalOnly))},
        FN_MAP_FILTER,
        FunctionSignatureOptions().set_rejects_collation().set_rewrite_options(
            FunctionSignatureRewriteOptions()
                .set_rewriter(REWRITE_BUILTIN_FUNCTION_INLINER)
                .set_sql(kMapFilterSql))}},
      FunctionOptions()
          .AddRequiredLanguageFeature(FEATURE_MAP_TYPE)
          .set_supports_safe_error_mode(
              options.language_options.LanguageFeatureEnabled(
                  FEATURE_SAFE_FUNCTION_CALL_WITH_LAMBDA_ARGS)));
}

}  // namespace googlesql
