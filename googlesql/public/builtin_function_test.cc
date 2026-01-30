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

#include "googlesql/public/builtin_function.h"

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/enum_utils.h"
#include "googlesql/common/builtin_function_internal.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/types/proto_type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/flat_set.h"
#include "googlesql/base/map_util.h"

namespace googlesql {

using testing::Eq;
using testing::Gt;
using testing::HasSubstr;
using testing::IsEmpty;
using testing::IsNull;
using testing::IsSupersetOf;
using testing::Not;
using testing::NotNull;
using absl_testing::StatusIs;

using NameToFunctionMap =
    absl::flat_hash_map<std::string, std::unique_ptr<Function>>;
using NameToTypeMap = absl::flat_hash_map<std::string, const Type*>;

namespace {

static constexpr auto kApproxDistanceFunctionNames =
    googlesql_base::fixed_flat_set_of<absl::string_view>({"approx_dot_product",
                                               "approx_cosine_distance",
                                               "approx_euclidean_distance"});
static constexpr auto kApproxDistanceFunctionIds =
    googlesql_base::fixed_flat_set_of<FunctionSignatureId>(
        {FN_APPROX_COSINE_DISTANCE_FLOAT_WITH_PROTO_OPTIONS,
         FN_APPROX_COSINE_DISTANCE_DOUBLE_WITH_PROTO_OPTIONS,
         FN_APPROX_EUCLIDEAN_DISTANCE_FLOAT_WITH_PROTO_OPTIONS,
         FN_APPROX_EUCLIDEAN_DISTANCE_DOUBLE_WITH_PROTO_OPTIONS,
         FN_APPROX_DOT_PRODUCT_INT64_WITH_PROTO_OPTIONS,
         FN_APPROX_DOT_PRODUCT_FLOAT_WITH_PROTO_OPTIONS,
         FN_APPROX_DOT_PRODUCT_DOUBLE_WITH_PROTO_OPTIONS});

static constexpr auto kVectorSearchTVFNames =
    googlesql_base::fixed_flat_set_of<absl::string_view>({"vector_search"});
static constexpr auto kVectorSearchTVFFunctionIds =
    googlesql_base::fixed_flat_set_of<FunctionSignatureId>(
        {FN_BATCH_VECTOR_SEARCH_TVF_WITH_PROTO_OPTIONS});

}  // namespace

TEST(SimpleBuiltinFunctionTests, ConstructWithProtoTest) {
  GoogleSQLBuiltinFunctionOptionsProto proto =
      GoogleSQLBuiltinFunctionOptionsProto::default_instance();
  GoogleSQLBuiltinFunctionOptions null_option(proto);
  EXPECT_EQ(0, null_option.exclude_function_ids.size());
  EXPECT_EQ(0, null_option.include_function_ids.size());

  proto.add_include_function_ids(FunctionSignatureId::FN_ABS_DOUBLE);
  proto.add_include_function_ids(FunctionSignatureId::FN_ADD_DOUBLE);
  proto.add_include_function_ids(FunctionSignatureId::FN_EQUAL);
  proto.add_exclude_function_ids(FunctionSignatureId::FN_ABS_DOUBLE);
  proto.add_exclude_function_ids(FunctionSignatureId::FN_AND);

  // Deprecated function.
  proto.add_exclude_function_ids(FunctionSignatureId::FN_ST_ACCUM);

  proto.mutable_language_options()->add_enabled_language_features(
      LanguageFeature::FEATURE_TABLESAMPLE);
  proto.mutable_language_options()->add_supported_statement_kinds(
      ResolvedNodeKind::RESOLVED_AGGREGATE_FUNCTION_CALL);

  EnabledRewriteProto rewrite_proto_1 = EnabledRewriteProto::default_instance();
  rewrite_proto_1.set_key(FunctionSignatureId::FN_ARRAY_FILTER);
  rewrite_proto_1.set_value(false);
  *proto.add_enabled_rewrites_map_entry() = rewrite_proto_1;

  EnabledRewriteProto rewrite_proto_2 = EnabledRewriteProto::default_instance();
  rewrite_proto_2.set_key(FunctionSignatureId::FN_ARRAY_FILTER_WITH_INDEX);
  rewrite_proto_2.set_value(true);
  *proto.add_enabled_rewrites_map_entry() = rewrite_proto_2;

  GoogleSQLBuiltinFunctionOptions option1(proto);
  EXPECT_EQ(3, option1.exclude_function_ids.size());
  EXPECT_EQ(3, option1.include_function_ids.size());
  EXPECT_EQ(2, option1.rewrite_enabled.size());
  EXPECT_TRUE(
      option1.include_function_ids.find(FunctionSignatureId::FN_EQUAL) !=
      option1.include_function_ids.end());
  EXPECT_TRUE(
      option1.include_function_ids.find(FunctionSignatureId::FN_ADD_DOUBLE) !=
      option1.include_function_ids.end());
  EXPECT_TRUE(
      option1.include_function_ids.find(FunctionSignatureId::FN_ABS_DOUBLE) !=
      option1.include_function_ids.end());
  EXPECT_TRUE(
      option1.exclude_function_ids.find(FunctionSignatureId::FN_ABS_DOUBLE) !=
      option1.exclude_function_ids.end());
  EXPECT_TRUE(option1.exclude_function_ids.find(FunctionSignatureId::FN_AND) !=
              option1.exclude_function_ids.end());
  EXPECT_TRUE(option1.language_options.LanguageFeatureEnabled(
      LanguageFeature::FEATURE_TABLESAMPLE));
  EXPECT_FALSE(option1.language_options.LanguageFeatureEnabled(
      LanguageFeature::FEATURE_ANALYTIC_FUNCTIONS));
  EXPECT_TRUE(option1.language_options.SupportsStatementKind(
      ResolvedNodeKind::RESOLVED_AGGREGATE_FUNCTION_CALL));
  EXPECT_FALSE(option1.language_options.SupportsStatementKind(
      ResolvedNodeKind::RESOLVED_ASSERT_ROWS_MODIFIED));
  EXPECT_EQ(option1.rewrite_enabled[FunctionSignatureId::FN_ARRAY_FILTER],
            false);
  EXPECT_EQ(
      option1.rewrite_enabled[FunctionSignatureId::FN_ARRAY_FILTER_WITH_INDEX],
      true);
}

TEST(SimpleBuiltinFunctionTests, ClassAndProtoSize) {
  EXPECT_EQ(4 * sizeof(absl::flat_hash_set<FunctionSignatureId,
                                           FunctionSignatureIdHasher>),
            sizeof(GoogleSQLBuiltinFunctionOptions) - sizeof(LanguageOptions))
      << "The size of GoogleSQLBuiltinFunctionOptions class has changed, "
      << "please also update the proto and serialization code if you "
      << "added/removed fields in it.";
  // TODO: b/332322078 - Implement serialization/deserialization logic for
  // `argument_types`.
  EXPECT_EQ(4,
            GoogleSQLBuiltinFunctionOptionsProto::descriptor()->field_count())
      << "The number of fields in GoogleSQLBuiltinFunctionOptionsProto has "
      << "changed, please also update the serialization code accordingly.";
}

void GetConcreteTypesFromInputArgumentTypeList(
    const FunctionArgumentTypeList& argument_types,
    absl::flat_hash_set<const Type*>& types_out);

void GetConcreteTypesFromInputArgumentType(
    const FunctionArgumentType& argument_type,
    absl::flat_hash_set<const Type*>& types_out) {
  switch (argument_type.kind()) {
    case ARG_TYPE_FIXED: {
      const Type* type = argument_type.type();
      ASSERT_NE(type, nullptr);
      types_out.insert(type);
      break;
    }
    case ARG_TYPE_LAMBDA: {
      GetConcreteTypesFromInputArgumentTypeList(
          argument_type.lambda().argument_types(), types_out);
      GetConcreteTypesFromInputArgumentType(argument_type.lambda().body_type(),
                                            types_out);
      break;
    }
    default:
      break;
  }
  if (argument_type.options().has_relation_input_schema()) {
    const TVFRelation& relation =
        argument_type.options().relation_input_schema();

    for (const TVFSchemaColumn& column : relation.columns()) {
      if (column.type != nullptr) {
        types_out.insert(column.type);
      }
    }
  }
}

void GetConcreteTypesFromInputArgumentTypeList(
    const FunctionArgumentTypeList& argument_types,
    absl::flat_hash_set<const Type*>& types_out) {
  for (const FunctionArgumentType& argument : argument_types) {
    GetConcreteTypesFromInputArgumentType(argument, types_out);
  }
}

// This tries to find all the types in a given function signature. It is
// best effort, failing pretty bad for templated functions, for instance.
void GetConcreteTypesFromSignature(
    const FunctionSignature& signature,
    absl::flat_hash_set<const Type*>& types_out) {
  GetConcreteTypesFromInputArgumentTypeList(signature.arguments(), types_out);
  GetConcreteTypesFromInputArgumentType(signature.result_type(), types_out);
}

void ValidateFunction(const LanguageOptions& language_options,
                      absl::string_view function_name,
                      const Function& function) {
  // GetBuiltinFunctionsAndTypes should all be builtin functions.
  EXPECT_TRUE(function.IsGoogleSQLBuiltin());

  // None of the built-in function names/aliases should start with
  // "[a-zA-Z]_", which is reserved for user-defined objects.
  ASSERT_GE(function_name.size(), 2);
  EXPECT_NE('_', function_name[1]) << function_name;

  absl::string_view alias_name = function.alias_name();
  if (!alias_name.empty()) {
    ASSERT_GE(alias_name.size(), 2);
    EXPECT_NE('_', alias_name[1]) << alias_name;
  }

  for (int i = 0; i < 5; ++i) {
    std::vector<InputArgumentType> args(i);
    if (!args.empty()) {
      args[0] = InputArgumentType::LambdaInputArgumentType();
    }
    function.CheckPreResolutionArgumentConstraints(args, language_options)
        .IgnoreError();
    function.GetNoMatchingFunctionSignatureErrorMessage(
        args, language_options.product_mode(), /*argument_names=*/{});
  }
  // In this file, we don't actually test the output of the supported signatures
  // code. We just test it doesn't crash.
  int ignored;
  function.GetSupportedSignaturesUserFacingText(
      language_options, FunctionArgumentType::NamePrintingStyle::kIfNamedOnly,
      &ignored);
}

TEST(SimpleBuiltinFunctionTests, SanityTests) {
  TypeFactory type_factory;
  NameToFunctionMap functions;
  NameToTypeMap types;
  NameToTableValuedFunctionMap table_valued_functions;

  // These settings retrieve the maximum set of functions/signatures
  // possible.
  LanguageOptions language_options;
  language_options.EnableMaximumLanguageFeaturesForDevelopment();
  language_options.set_product_mode(PRODUCT_INTERNAL);
  GoogleSQLBuiltinFunctionOptions options(language_options);
  // Get all the relevant functions for this 'language_options'.
  GOOGLESQL_EXPECT_OK(GetBuiltinFunctionsAndTypes(options, type_factory, functions, types,
                                        table_valued_functions));

  for (const auto& [name, function] : functions) {
    ValidateFunction(language_options, name, *function);
  }

  // Make sure expected functions are found via name mapping.
  for (FunctionSignatureId id :
       googlesql_base::EnumerateEnumValues<FunctionSignatureId>()) {
    // Skip invalid ids.
    switch (id) {
      case __FunctionSignatureId__switch_must_have_a_default__:
      case FN_INVALID_FUNCTION_ID:
        continue;
      case FN_ST_ACCUM:
        continue;
      // TODO: Remove FN_TIME_FROM_STRING when there are no more
      // references to it.
      case FN_TIME_FROM_STRING:
        continue;
      // These FunctionSignatureIds are not loaded unless `argument_types`
      // is populated with corresponding entries in BuiltinFunctionOptions.
      case FN_APPROX_COSINE_DISTANCE_FLOAT_WITH_PROTO_OPTIONS:
      case FN_APPROX_COSINE_DISTANCE_DOUBLE_WITH_PROTO_OPTIONS:
      case FN_APPROX_EUCLIDEAN_DISTANCE_FLOAT_WITH_PROTO_OPTIONS:
      case FN_APPROX_EUCLIDEAN_DISTANCE_DOUBLE_WITH_PROTO_OPTIONS:
      case FN_APPROX_DOT_PRODUCT_INT64_WITH_PROTO_OPTIONS:
      case FN_APPROX_DOT_PRODUCT_FLOAT_WITH_PROTO_OPTIONS:
      case FN_APPROX_DOT_PRODUCT_DOUBLE_WITH_PROTO_OPTIONS:
      case FN_BATCH_VECTOR_SEARCH_TVF_WITH_PROTO_OPTIONS:
        continue;
      default:
        break;
    }
    const std::string function_name =
        FunctionSignatureIdToName(static_cast<FunctionSignatureId>(id));

    EXPECT_TRUE(functions.contains(function_name) ||
                table_valued_functions.contains(function_name))
        << "Not found (id " << id << "): " << function_name;

    EXPECT_FALSE(functions.contains(function_name) &&
                 table_valued_functions.contains(function_name))
        << "Same function defined both as a function and as a tvf: "
        << function_name;
  }
}

TEST(SimpleBuiltinFunctionTests, BasicTests) {
  TypeFactory type_factory;
  NameToFunctionMap functions;
  NameToTypeMap types;
  GoogleSQLBuiltinFunctionOptions options(LanguageOptions{});

  GOOGLESQL_EXPECT_OK(
      GetBuiltinFunctionsAndTypes(options, type_factory, functions, types));

  std::unique_ptr<Function>* function =
      googlesql_base::FindOrNull(functions, "current_timestamp");
  ASSERT_THAT(function, NotNull());
  EXPECT_EQ(1, (*function)->NumSignatures());

  function = googlesql_base::FindOrNull(functions, "$add");
  ASSERT_THAT(function, NotNull());
  EXPECT_EQ(5, (*function)->NumSignatures());
  EXPECT_FALSE((*function)->SupportsSafeErrorMode());

  function = googlesql_base::FindOrNull(functions, "mod");
  ASSERT_THAT(function, NotNull());
  EXPECT_EQ(2, (*function)->NumSignatures());
  EXPECT_TRUE((*function)->SupportsSafeErrorMode());

  function = googlesql_base::FindOrNull(functions, "$case_with_value");
  ASSERT_THAT(function, NotNull());
  ASSERT_EQ(1, (*function)->NumSignatures());
  ASSERT_EQ(4, (*function)->GetSignature(0)->arguments().size());
  ASSERT_EQ(ARG_TYPE_ANY_1, (*function)->GetSignature(0)->result_type().kind());
  EXPECT_FALSE((*function)->SupportsSafeErrorMode());

  function = googlesql_base::FindOrNull(functions, "$case_no_value");
  ASSERT_THAT(function, NotNull());
  ASSERT_EQ(1, (*function)->NumSignatures());
  ASSERT_EQ(3, (*function)->GetSignature(0)->arguments().size());
  ASSERT_EQ(ARG_TYPE_ANY_1, (*function)->GetSignature(0)->result_type().kind());
  EXPECT_FALSE((*function)->SupportsSafeErrorMode());

  function = googlesql_base::FindOrNull(functions, "concat");
  ASSERT_THAT(function, NotNull());
  ASSERT_EQ(2, (*function)->NumSignatures());
  EXPECT_TRUE((*function)->SupportsSafeErrorMode());
}

TEST(SimpleBuiltinFunctionTests, ExcludedBuiltinFunctionTests) {
  TypeFactory type_factory;
  NameToFunctionMap all_functions;
  NameToFunctionMap functions;
  NameToTypeMap types;

  GoogleSQLBuiltinFunctionOptions options;
  GOOGLESQL_EXPECT_OK(
      GetBuiltinFunctionsAndTypes(options, type_factory, all_functions, types));

  // Remove all CONCAT signatures.
  options.exclude_function_ids.insert(FN_CONCAT_STRING);
  options.exclude_function_ids.insert(FN_CONCAT_BYTES);

  // Remove few signatures for MULTIPLY.
  options.exclude_function_ids.insert(FN_MULTIPLY_INT64);
  options.exclude_function_ids.insert(FN_MULTIPLY_DOUBLE);
  options.exclude_function_ids.insert(FN_MULTIPLY_NUMERIC);
  options.exclude_function_ids.insert(FN_MULTIPLY_BIGNUMERIC);
  options.exclude_function_ids.insert(FN_MULTIPLY_INTERVAL_INT64);
  options.exclude_function_ids.insert(FN_MULTIPLY_INT64_INTERVAL);

  // Remove all signatures for SUBTRACT.
  options.exclude_function_ids.insert(FN_SUBTRACT_DOUBLE);
  options.exclude_function_ids.insert(FN_SUBTRACT_INT64);
  options.exclude_function_ids.insert(FN_SUBTRACT_UINT64);
  options.exclude_function_ids.insert(FN_SUBTRACT_NUMERIC);
  options.exclude_function_ids.insert(FN_SUBTRACT_BIGNUMERIC);
  options.exclude_function_ids.insert(FN_SUBTRACT_DATE_INT64);
  options.exclude_function_ids.insert(FN_SUBTRACT_DATE);
  options.exclude_function_ids.insert(FN_SUBTRACT_TIMESTAMP);
  options.exclude_function_ids.insert(FN_SUBTRACT_DATETIME);
  options.exclude_function_ids.insert(FN_SUBTRACT_TIME);
  options.exclude_function_ids.insert(FN_SUBTRACT_TIMESTAMP_INTERVAL);
  options.exclude_function_ids.insert(FN_SUBTRACT_DATE_INTERVAL);
  options.exclude_function_ids.insert(FN_SUBTRACT_DATETIME_INTERVAL);
  options.exclude_function_ids.insert(FN_SUBTRACT_INTERVAL_INTERVAL);

  // Remove few signature for ADD but not all.
  options.exclude_function_ids.insert(FN_ADD_UINT64);
  options.exclude_function_ids.insert(FN_ADD_NUMERIC);
  options.exclude_function_ids.insert(FN_ADD_BIGNUMERIC);
  options.exclude_function_ids.insert(FN_ADD_DATE_INT64);
  options.exclude_function_ids.insert(FN_ADD_INT64_DATE);
  options.exclude_function_ids.insert(FN_ADD_TIMESTAMP_INTERVAL);
  options.exclude_function_ids.insert(FN_ADD_INTERVAL_TIMESTAMP);
  options.exclude_function_ids.insert(FN_ADD_DATE_INTERVAL);
  options.exclude_function_ids.insert(FN_ADD_INTERVAL_DATE);
  options.exclude_function_ids.insert(FN_ADD_DATETIME_INTERVAL);
  options.exclude_function_ids.insert(FN_ADD_INTERVAL_DATETIME);
  options.exclude_function_ids.insert(FN_ADD_INTERVAL_INTERVAL);

  // Get filtered functions.
  types.clear();
  GOOGLESQL_EXPECT_OK(
      GetBuiltinFunctionsAndTypes(options, type_factory, functions, types));

  std::vector<std::string> functions_not_in_all_functions;
  std::vector<std::string> all_functions_not_in_functions;

  for (const auto& function : all_functions) {
    if (!functions.contains(function.first)) {
      all_functions_not_in_functions.push_back(function.first);
    }
  }
  for (const auto& function : functions) {
    if (!all_functions.contains(function.first)) {
      functions_not_in_all_functions.push_back(function.first);
    }
  }

  // Two functions excluded completely.
  EXPECT_EQ(2, all_functions.size() - functions.size())
      << "all_functions.size(): " << all_functions.size()
      << "\nfunctions.size(): " << functions.size()
      << "\nfunctions not in all_functions: "
      << absl::StrJoin(functions_not_in_all_functions, ",")
      << "\nall_functions not in functions: "
      << absl::StrJoin(all_functions_not_in_functions, ",");

  // The only signature for CONCAT was removed so no CONCAT function.
  std::unique_ptr<Function>* function =
      googlesql_base::FindOrNull(functions, FunctionSignatureIdToName(FN_CONCAT_STRING));
  EXPECT_THAT(function, IsNull());

  // All signatures for SUBTRACT were removed so no SUBTRACT function.
  function =
      googlesql_base::FindOrNull(functions, FunctionSignatureIdToName(FN_SUBTRACT_INT64));
  EXPECT_THAT(function, IsNull());

  // All MULTIPLY signatures except one were removed.
  function =
      googlesql_base::FindOrNull(functions, FunctionSignatureIdToName(FN_MULTIPLY_UINT64));
  EXPECT_THAT(function, NotNull());
  ASSERT_EQ(1, (*function)->NumSignatures());
  EXPECT_EQ("(UINT64, UINT64) -> UINT64",
            (*function)->GetSignature(0)->DebugString());

  // All ADD signatures except two were excluded.
  function =
      googlesql_base::FindOrNull(functions, FunctionSignatureIdToName(FN_ADD_INT64));
  EXPECT_THAT(function, NotNull());
  ASSERT_EQ(2, (*function)->NumSignatures());
  EXPECT_EQ("(INT64, INT64) -> INT64",
            (*function)->GetSignature(0)->DebugString());
  EXPECT_EQ("(DOUBLE, DOUBLE) -> DOUBLE",
            (*function)->GetSignature(1)->DebugString());
}

TEST(SimpleBuiltinFunctionTests, IncludedBuiltinFunctionTests) {
  TypeFactory type_factory;
  NameToFunctionMap functions;
  NameToTypeMap types;
  GoogleSQLBuiltinFunctionOptions options;

  // Include signature for CONCAT.
  options.include_function_ids.insert(FN_CONCAT_STRING);
  // Include one signature for MULTIPLY.
  options.include_function_ids.insert(FN_MULTIPLY_INT64);
  // Include two signatures for ADD.
  options.include_function_ids.insert(FN_ADD_INT64);
  options.include_function_ids.insert(FN_ADD_DOUBLE);

  GOOGLESQL_EXPECT_OK(
      GetBuiltinFunctionsAndTypes(options, type_factory, functions, types));

  EXPECT_EQ(3, functions.size());

  std::unique_ptr<Function>* function =
      googlesql_base::FindOrNull(functions, FunctionSignatureIdToName(FN_CONCAT_STRING));
  EXPECT_THAT(function, NotNull());

  // One MULTIPLY signature was included.
  function =
      googlesql_base::FindOrNull(functions, FunctionSignatureIdToName(FN_MULTIPLY_INT64));
  EXPECT_THAT(function, NotNull());
  EXPECT_EQ(1, (*function)->NumSignatures());
  EXPECT_EQ("(INT64, INT64) -> INT64",
            (*function)->GetSignature(0)->DebugString());

  // Two of five ADD signatures were included.
  function =
      googlesql_base::FindOrNull(functions, FunctionSignatureIdToName(FN_ADD_INT64));
  EXPECT_THAT(function, NotNull());
  EXPECT_EQ(2, (*function)->NumSignatures());
  EXPECT_EQ("(INT64, INT64) -> INT64",
            (*function)->GetSignature(0)->DebugString());
  EXPECT_EQ("(DOUBLE, DOUBLE) -> DOUBLE",
            (*function)->GetSignature(1)->DebugString());
}

TEST(SimpleBuiltinFunctionTests,
     ExcludeBuiltinFunctionSignatureBySupportedTypeTests) {
  TypeFactory type_factory;
  GoogleSQLBuiltinFunctionOptions options;
  // Include signature for timestamp().
  options.include_function_ids.insert(FN_TIMESTAMP_FROM_STRING);
  options.include_function_ids.insert(FN_TIMESTAMP_FROM_DATE);
  options.include_function_ids.insert(FN_TIMESTAMP_FROM_DATETIME);

  {
    NameToFunctionMap functions;
    NameToTypeMap types;
    // FEATURE_CIVIL_TIME is not enabled, the function timestamp() should
    // have only two signatures.
    options.language_options.DisableAllLanguageFeatures();
    GOOGLESQL_EXPECT_OK(
        GetBuiltinFunctionsAndTypes(options, type_factory, functions, types));
    std::unique_ptr<Function>* function =
        googlesql_base::FindOrNull(functions, "timestamp");
    ASSERT_TRUE(function != nullptr);
    ASSERT_EQ(2, (*function)->NumSignatures());
    // Assume the order of the signatures is the same as they are added.
    EXPECT_EQ(FN_TIMESTAMP_FROM_STRING,
              (*function)->GetSignature(0)->context_id());
    EXPECT_EQ(FN_TIMESTAMP_FROM_DATE,
              (*function)->GetSignature(1)->context_id());
  }

  {
    NameToFunctionMap functions;
    NameToTypeMap types;
    // Enabling FEATURE_CIVIL_TIME should allow all three signatures to be
    // included for the function timestamp().
    options.language_options.SetEnabledLanguageFeatures({FEATURE_CIVIL_TIME});
    GOOGLESQL_EXPECT_OK(
        GetBuiltinFunctionsAndTypes(options, type_factory, functions, types));
    std::unique_ptr<Function>* function =
        googlesql_base::FindOrNull(functions, "timestamp");
    ASSERT_TRUE(function != nullptr);
    ASSERT_EQ(3, (*function)->NumSignatures());
    // Assume the order of the signatures is the same as they are added.
    EXPECT_EQ(FN_TIMESTAMP_FROM_STRING,
              (*function)->GetSignature(0)->context_id());
    EXPECT_EQ(FN_TIMESTAMP_FROM_DATE,
              (*function)->GetSignature(1)->context_id());
    EXPECT_EQ(FN_TIMESTAMP_FROM_DATETIME,
              (*function)->GetSignature(2)->context_id());
  }
}

TEST(SimpleBuiltinFunctionTests, IncludedAndExcludedBuiltinFunctionTests) {
  TypeFactory type_factory;
  NameToFunctionMap functions;
  NameToTypeMap types;
  GoogleSQLBuiltinFunctionOptions options;

  // Include two of three signatures for ADD, but exclude one of them.
  options.include_function_ids.insert(FN_ADD_INT64);
  options.include_function_ids.insert(FN_ADD_DOUBLE);
  options.exclude_function_ids.insert(FN_ADD_DOUBLE);

  GOOGLESQL_EXPECT_OK(
      GetBuiltinFunctionsAndTypes(options, type_factory, functions, types));

  EXPECT_EQ(1, functions.size());

  std::unique_ptr<Function>* function =
      googlesql_base::FindOrNull(functions, FunctionSignatureIdToName(FN_ADD_INT64));
  EXPECT_THAT(function, NotNull());
  EXPECT_EQ(1, (*function)->NumSignatures());
  EXPECT_EQ("(INT64, INT64) -> INT64",
            (*function)->GetSignature(0)->DebugString());
}

TEST(SimpleBuiltinFunctionTests, LanguageOptions) {
  LanguageOptions language_options;
  GoogleSQLBuiltinFunctionOptions options(language_options);

  TypeFactory type_factory;
  NameToFunctionMap functions;
  NameToTypeMap types;

  // With default LanguageOptions, we won't get analytic functions.
  GOOGLESQL_EXPECT_OK(
      GetBuiltinFunctionsAndTypes(options, type_factory, functions, types));
  EXPECT_TRUE(functions.contains(FunctionSignatureIdToName(FN_COUNT)));
  EXPECT_FALSE(functions.contains(FunctionSignatureIdToName(FN_RANK)));

  options.language_options.EnableLanguageFeature(FEATURE_ANALYTIC_FUNCTIONS);
  options.language_options.EnableMaximumLanguageFeatures();

  functions.clear();
  types.clear();
  GOOGLESQL_EXPECT_OK(
      GetBuiltinFunctionsAndTypes(options, type_factory, functions, types));
  EXPECT_TRUE(functions.contains(FunctionSignatureIdToName(FN_COUNT)));
  EXPECT_TRUE(functions.contains(FunctionSignatureIdToName(FN_RANK)));

  // Now test combination of LanguageOptions, inclusions and exclusions.
  // Without enabling FEATURE_ANALYTIC_FUNCTIONS, we don't get FN_RANK, even
  // if we put it on the include_function_ids.
  options.language_options = LanguageOptions();
  EXPECT_FALSE(options.language_options.LanguageFeatureEnabled(
      FEATURE_ANALYTIC_FUNCTIONS));
  options.include_function_ids.insert(FN_RANK);
  options.include_function_ids.insert(FN_MAX);

  functions.clear();
  types.clear();
  GOOGLESQL_EXPECT_OK(
      GetBuiltinFunctionsAndTypes(options, type_factory, functions, types));
  EXPECT_TRUE(functions.contains(FunctionSignatureIdToName(FN_MAX)));
  EXPECT_FALSE(functions.contains(FunctionSignatureIdToName(FN_COUNT)));
  EXPECT_FALSE(functions.contains(FunctionSignatureIdToName(FN_RANK)));
  EXPECT_FALSE(functions.contains(FunctionSignatureIdToName(FN_LEAD)));

  // When we enable FEATURE_ANALYTIC_FUNCTIONS, inclusion lists apply to
  // analytic functions.
  options.language_options.EnableLanguageFeature(FEATURE_ANALYTIC_FUNCTIONS);

  functions.clear();
  types.clear();
  GOOGLESQL_EXPECT_OK(
      GetBuiltinFunctionsAndTypes(options, type_factory, functions, types));
  EXPECT_TRUE(functions.contains(FunctionSignatureIdToName(FN_MAX)));
  EXPECT_FALSE(functions.contains(FunctionSignatureIdToName(FN_COUNT)));
  EXPECT_TRUE(functions.contains(FunctionSignatureIdToName(FN_RANK)));
  EXPECT_FALSE(functions.contains(FunctionSignatureIdToName(FN_LEAD)));
}

TEST(SimpleBuiltinFunctionTests, NumericFunctions) {
  TypeFactory type_factory;
  NameToFunctionMap functions;
  NameToTypeMap types;

  // Verify that numeric functions are available when NUMERIC type is enabled.
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_NUMERIC_TYPE);

  // Limit ABS signatures to just one to make it easier to test below.
  GoogleSQLBuiltinFunctionOptions options{language_options};
  options.include_function_ids.insert(FN_ABS_NUMERIC);

  GOOGLESQL_EXPECT_OK(
      GetBuiltinFunctionsAndTypes(options, type_factory, functions, types));
  std::unique_ptr<Function>* function =
      googlesql_base::FindOrNull(functions, FunctionSignatureIdToName(FN_ABS_NUMERIC));
  EXPECT_THAT(function, NotNull());
  EXPECT_EQ(1, (*function)->NumSignatures());
  EXPECT_EQ("(NUMERIC) -> NUMERIC",
            (*function)->GetSignature(0)->DebugString());
}

static ArgumentConstraintsCallback GetPreResolutionArgumentConstraints(
    FunctionSignatureId function_id, const NameToFunctionMap& functions) {
  std::unique_ptr<Function> const* function =
      googlesql_base::FindOrNull(functions, FunctionSignatureIdToName(function_id));
  if (function == nullptr) {
    return nullptr;
  }
  return (*function)->PreResolutionConstraints();
}

static PostResolutionArgumentConstraintsCallback
GetPostResolutionArgumentConstraints(FunctionSignatureId function_id,
                                     const NameToFunctionMap& functions) {
  std::unique_ptr<Function> const* function =
      googlesql_base::FindOrNull(functions, FunctionSignatureIdToName(function_id));
  if (function == nullptr) {
    return nullptr;
  }
  return (*function)->PostResolutionConstraints();
}

TEST(SimpleFunctionTests, TestCheckArgumentConstraints) {
  TypeFactory type_factory;
  NameToFunctionMap functions;
  NameToTypeMap types;
  GoogleSQLBuiltinFunctionOptions options;

  GOOGLESQL_EXPECT_OK(
      GetBuiltinFunctionsAndTypes(options, type_factory, functions, types));

  const Type* int64_type = types::Int64Type();
  const Type* string_type = types::StringType();
  const Type* bytes_type = types::BytesType();
  const Type* bool_type = types::BoolType();

  const Type* enum_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeEnumType(googlesql_test::TestEnum_descriptor(),
                                      &enum_type));

  const StructType* struct_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeStructType({{"a", string_type}, {"b", bytes_type}},
                                        &struct_type));

  const ArrayType* array_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeArrayType(int64_type, &array_type));

  const ProtoType* proto_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeProtoType(
      googlesql_test::KitchenSinkPB::descriptor(), &proto_type));

  Value delimiter = Value::String(", ");

  FunctionSignature dummy_signature{int64_type,
                                    {{int64_type, /*num_occurrences=*/1},
                                     {int64_type, /*num_occurrences=*/1}},
                                    /*context_id=*/-1};
  const std::vector<FunctionSignatureId> function_ids = {
      FN_LESS,    FN_LESS_OR_EQUAL, FN_GREATER_OR_EQUAL,
      FN_GREATER, FN_EQUAL,         FN_NOT_EQUAL};
  for (const FunctionSignatureId function_id : function_ids) {
    ASSERT_THAT(GetPreResolutionArgumentConstraints(function_id, functions),
                IsNull());
    PostResolutionArgumentConstraintsCallback post_constraints =
        GetPostResolutionArgumentConstraints(function_id, functions);
    ASSERT_THAT(post_constraints, NotNull());
    GOOGLESQL_EXPECT_OK(post_constraints(
        dummy_signature,
        {InputArgumentType(int64_type), InputArgumentType(int64_type)},
        LanguageOptions()));

    GOOGLESQL_EXPECT_OK(post_constraints(
        dummy_signature,
        {InputArgumentType(bool_type), InputArgumentType(bool_type)},
        LanguageOptions()));
    GOOGLESQL_EXPECT_OK(post_constraints(
        dummy_signature,
        {InputArgumentType(enum_type), InputArgumentType(enum_type)},
        LanguageOptions()));

    const absl::Status struct_type_status = post_constraints(
        dummy_signature,
        {InputArgumentType(struct_type), InputArgumentType(struct_type)},
        LanguageOptions());
    LanguageOptions language_options;

    language_options.EnableLanguageFeature(FEATURE_ARRAY_EQUALITY);
    const absl::Status array_type_status = post_constraints(
        dummy_signature,
        {InputArgumentType(array_type), InputArgumentType(array_type)},
        LanguageOptions());
    const absl::Status array_type_status_with_equality_support =
        post_constraints(
            dummy_signature,
            {InputArgumentType(array_type), InputArgumentType(array_type)},
            language_options);

    if (function_id == FN_EQUAL || function_id == FN_NOT_EQUAL) {
      GOOGLESQL_EXPECT_OK(struct_type_status);
      EXPECT_FALSE(array_type_status.ok());
      GOOGLESQL_EXPECT_OK(array_type_status_with_equality_support);
    } else {
      EXPECT_FALSE(struct_type_status.ok());
      EXPECT_FALSE(array_type_status.ok());
    }

    EXPECT_FALSE(post_constraints(dummy_signature,
                                  {InputArgumentType(proto_type),
                                   InputArgumentType(proto_type)},
                                  LanguageOptions())
                     .ok());
  }

  auto constraints = GetPreResolutionArgumentConstraints(FN_MIN, functions);
  ASSERT_THAT(constraints, NotNull());
  GOOGLESQL_EXPECT_OK(constraints({InputArgumentType(int64_type)}, LanguageOptions()));
  GOOGLESQL_EXPECT_OK(constraints({InputArgumentType(enum_type)}, LanguageOptions()));
  EXPECT_FALSE(
      constraints({InputArgumentType(struct_type)}, LanguageOptions()).ok());
  EXPECT_FALSE(
      constraints({InputArgumentType(array_type)}, LanguageOptions()).ok());
  EXPECT_FALSE(
      constraints({InputArgumentType(proto_type)}, LanguageOptions()).ok());

  constraints = GetPreResolutionArgumentConstraints(FN_MAX, functions);
  ASSERT_THAT(constraints, NotNull());
  GOOGLESQL_EXPECT_OK(constraints({InputArgumentType(int64_type)}, LanguageOptions()));
  GOOGLESQL_EXPECT_OK(constraints({InputArgumentType(enum_type)}, LanguageOptions()));
  EXPECT_FALSE(
      constraints({InputArgumentType(struct_type)}, LanguageOptions()).ok());
  EXPECT_FALSE(
      constraints({InputArgumentType(array_type)}, LanguageOptions()).ok());
  EXPECT_FALSE(
      constraints({InputArgumentType(proto_type)}, LanguageOptions()).ok());

  constraints = GetPreResolutionArgumentConstraints(FN_ARRAY_AGG, functions);
  ASSERT_THAT(constraints, NotNull());
  GOOGLESQL_EXPECT_OK(constraints({InputArgumentType(int64_type)}, LanguageOptions()));
  GOOGLESQL_EXPECT_OK(constraints({InputArgumentType(enum_type)}, LanguageOptions()));
  GOOGLESQL_EXPECT_OK(constraints({InputArgumentType(struct_type)}, LanguageOptions()));
  EXPECT_FALSE(
      constraints({InputArgumentType(array_type)}, LanguageOptions()).ok());
  GOOGLESQL_EXPECT_OK(constraints({InputArgumentType(proto_type)}, LanguageOptions()));
}

TEST(SimpleFunctionTests, HideFunctionsForExternalMode) {
  TypeFactory type_factory;
  GoogleSQLBuiltinFunctionOptions options;
  for (int i = 0; i < 2; ++i) {
    bool is_external = (i == 1);
    if (is_external) {
      options.language_options.set_product_mode(PRODUCT_EXTERNAL);
    }
    NameToFunctionMap functions;
    NameToTypeMap types;
    GOOGLESQL_EXPECT_OK(
        GetBuiltinFunctionsAndTypes(options, type_factory, functions, types));

    // NORMALIZE and NORMALIZE_AND_CASEFOLD take an enum type but are expected
    // to be available in external mode.
    EXPECT_THAT(functions, testing::Contains(testing::Key("normalize")));
    EXPECT_THAT(functions,
                testing::Contains(testing::Key("normalize_and_casefold")));

    EXPECT_EQ(is_external,
              googlesql_base::FindOrNull(functions, "net.format_ip") == nullptr);
    EXPECT_EQ(is_external,
              googlesql_base::FindOrNull(functions, "net.parse_ip") == nullptr);
    EXPECT_EQ(is_external,
              googlesql_base::FindOrNull(functions, "net.format_packed_ip") == nullptr);
    EXPECT_EQ(is_external,
              googlesql_base::FindOrNull(functions, "net.parse_packed_ip") == nullptr);
    EXPECT_EQ(is_external,
              googlesql_base::FindOrNull(functions, "net.ip_in_net") == nullptr);
    EXPECT_EQ(is_external,
              googlesql_base::FindOrNull(functions, "net.make_net") == nullptr);
    EXPECT_NE(nullptr, googlesql_base::FindOrNull(functions, "array_length"));
  }
}
class OpaqueTypeTest : public ::testing::TestWithParam<ProductMode> {};

INSTANTIATE_TEST_SUITE_P(OpaqueTypeTest, OpaqueTypeTest,
                         ::testing::Values(PRODUCT_INTERNAL, PRODUCT_EXTERNAL));

TEST_P(OpaqueTypeTest, TestOpaqueTypeConsistency) {
  TypeFactory type_factory;
  // Builtin functions that include an opaque type transitively in their
  // signature should also add that type with several exceptions.
  // Conversely, all added types should appear in some function signature.
  GoogleSQLBuiltinFunctionOptions options;
  options.language_options.EnableMaximumLanguageFeaturesForDevelopment();
  options.language_options.set_product_mode(GetParam());
  NameToFunctionMap functions;
  NameToTypeMap types;
  GOOGLESQL_ASSERT_OK(
      GetBuiltinFunctionsAndTypes(options, type_factory, functions, types));
  absl::flat_hash_set<const Type*> types_in_catalog;
  for (const auto& [_, type] : types) {
    types_in_catalog.insert(type);
  }

  // Don't add an entry for every opaque type, this is just a one-of to make
  // sure _something_ is being returned as expected.  This type of testing
  // should be added to compliance tests.
  EXPECT_THAT(types_in_catalog,
              testing::Contains(types::RoundingModeEnumType()));

  absl::flat_hash_set<const Type*> all_referenced_types;
  for (const auto& [name, function] : functions) {
    for (const FunctionSignature& sig : function->signatures()) {
      absl::flat_hash_set<const Type*> types_in_signature;
      GetConcreteTypesFromSignature(sig, types_in_signature);
      for (const Type* type : types_in_signature) {
        if (type->IsEnum() && type->AsEnum()->IsOpaque()) {
          EXPECT_THAT(types_in_catalog, testing::Contains(type))
              << "function " << name << " with signature " << sig.DebugString()
              << " references opaque enum type " << type->DebugString()
              << " which is not returned by GetBuiltinFunctionsAndTypes";
        }
      }
      all_referenced_types.insert(types_in_signature.begin(),
                                  types_in_signature.end());
    }
  }

  // Make sure that every type in the catalog is referenced at least once.
  EXPECT_THAT(types_in_catalog, testing::IsSubsetOf(all_referenced_types));
}

TEST(SimpleFunctionTests, TestFunctionSignaturesForUnintendedCoercion) {
  TypeFactory type_factory;
  const Type* int32_type = type_factory.get_int32();
  const Type* int64_type = type_factory.get_int64();
  const Type* uint32_type = type_factory.get_uint32();
  const Type* uint64_type = type_factory.get_uint64();
  const Type* float_type = type_factory.get_float();
  const Type* double_type = type_factory.get_double();

  const Function::Mode SCALAR = Function::SCALAR;

  // This is what the original, problematic signature for the 'sign'
  // function was.  We detect that this function has a risky signature
  // of signed integer and floating point, without unsigned integer
  // signatures.
  Function function("sign", Function::kGoogleSQLFunctionGroupName, SCALAR,
                    {{int32_type, {int32_type}, FN_SIGN_INT32},
                     {int64_type, {int64_type}, FN_SIGN_INT64},
                     {float_type, {float_type}, FN_SIGN_FLOAT},
                     {double_type, {double_type}, FN_SIGN_DOUBLE}},
                    FunctionOptions());
  EXPECT_TRUE(FunctionMayHaveUnintendedArgumentCoercion(&function))
      << function.DebugString();

  // The checks work across corresponding arguments.  This fails because the
  // second argument only supports signed and floating point types.
  Function function2(
      "fn2", Function::kGoogleSQLFunctionGroupName, SCALAR,
      {{int32_type, {int32_type, int32_type}, nullptr /* context_ptr */},
       {int64_type, {int64_type, int64_type}, nullptr /* context_ptr */},
       {uint32_type, {uint32_type, int32_type}, nullptr /* context_ptr */},
       {uint64_type, {uint64_type, int64_type}, nullptr /* context_ptr */},
       {float_type, {float_type, float_type}, nullptr /* context_ptr */},
       {double_type, {double_type, double_type}, nullptr /* context_ptr */}},
      FunctionOptions());
  EXPECT_TRUE(FunctionMayHaveUnintendedArgumentCoercion(&function2))
      << function2.DebugString();

  // The check works across signatures with different numbers of arguments.
  // For this function, the problematic argument is the third argument, and
  // only some of the signatures have a third argument.
  Function function3(
      "fn3", Function::kGoogleSQLFunctionGroupName, SCALAR,
      {{int32_type,
        {int64_type, int32_type, int32_type},
        nullptr /* context_ptr */},
       {int64_type, {int64_type, int64_type}, nullptr /* context_ptr */},
       {uint32_type,
        {int32_type, uint32_type, int32_type},
        nullptr /* context_ptr */},
       {uint64_type, {uint64_type, int64_type}, nullptr /* context_ptr */},
       {float_type,
        {uint32_type, float_type, float_type},
        nullptr /* context_ptr */},
       {double_type, {double_type, double_type}, nullptr /* context_ptr */}},
      FunctionOptions());
  EXPECT_TRUE(FunctionMayHaveUnintendedArgumentCoercion(&function3))
      << function3.DebugString();

  // The test function does not currently catch a potential problem in this
  // function's signatures since it has all of signed, unsigned, and floating
  // point arguments... even though it allows possibly inadvertent coercions
  // from an UINT64 argument to the DOUBLE signature.
  Function function4("fn4", Function::kGoogleSQLFunctionGroupName, SCALAR,
                     {{int32_type, {int32_type}, nullptr /* context_ptr */},
                      {int64_type, {int64_type}, nullptr /* context_ptr */},
                      {uint32_type, {uint32_type}, nullptr /* context_ptr */},
                      {float_type, {float_type}, nullptr /* context_ptr */},
                      {double_type, {double_type}, nullptr /* context_ptr */}},
                     FunctionOptions());
  EXPECT_FALSE(FunctionMayHaveUnintendedArgumentCoercion(&function4))
      << function4.DebugString();
}

TEST(SimpleFunctionTests,
     TestGoogleSQLBuiltinFunctionSignaturesForUnintendedCoercion) {
  // This test is intended to catch signatures that may have unintended
  // argument coercions.  Specifically, it looks for functions that have
  // INT64 and DOUBLE signatures that allow arguments to be coerced, but
  // no UINT64 signature.  Such cases are risky, since they allow implicit
  // coercion from UINT64 to DOUBLE, which in most cases is unintended.
  //
  // There are currently no GoogleSQL functions that violate this test.
  // If at some point there needs to be, then we can add an exception for
  // those functions.
  //
  // Note that this analysis does not currently apply any argument
  // constraints that might be present, so it's possible that a function
  // could have arguments that violate this test but whose argument
  // constraints later exclude types the avoid unintended coercion.
  //
  // Note that this only tests GoogleSQL functions - it does not test
  // any function signatures that engines may have added for extensions.
  TypeFactory type_factory;
  NameToFunctionMap functions;
  NameToTypeMap types;

  // These settings retrieve all the functions and signatures.
  LanguageOptions options;
  options.EnableMaximumLanguageFeatures();
  options.set_product_mode(PRODUCT_INTERNAL);

  GOOGLESQL_EXPECT_OK(GetBuiltinFunctionsAndTypes(BuiltinFunctionOptions(options),
                                        type_factory, functions, types));

  for (const auto& function_entry : functions) {
    const Function* function = function_entry.second.get();
    // If this test fails, then the function must be examined closely
    // to determine whether or not its signatures are correct, and that
    // there is no unintended argument coercion being allowed (particularly
    // for unsigned integer arguments).
    // TODO - Fix FunctionMayHaveUnintendedArgumentCoercion so that
    // it doesn't detect false positives.

    // The divide function triggers a false positive, as the method incorrectly
    // detects an unintended coercion for the following divide signatures :
    // (DOUBLE, DOUBLE), (INTERVAL, INT64).
    if (function->Name() == "$divide") continue;
    EXPECT_FALSE(FunctionMayHaveUnintendedArgumentCoercion(function))
        << function->DebugString();
  }
}

TEST(SimpleFunctionTests, TestRewriteEnabled) {
  TypeFactory type_factory;
  NameToFunctionMap functions;
  NameToTypeMap types;
  GoogleSQLBuiltinFunctionOptions options;

  // Override the rewriters for ARRAY_FIRST and ARRAY_LAST.
  options.rewrite_enabled[FN_ARRAY_FIRST] = true;
  options.rewrite_enabled[FN_ARRAY_LAST] = false;

  GOOGLESQL_EXPECT_OK(
      GetBuiltinFunctionsAndTypes(options, type_factory, functions, types));

  std::unique_ptr<Function>* function =
      googlesql_base::FindOrNull(functions, FunctionSignatureIdToName(FN_ARRAY_FIRST));
  EXPECT_THAT(function, NotNull());
  for (const FunctionSignature& signature : (*function)->signatures()) {
    EXPECT_TRUE(signature.options().rewrite_options()->enabled());
  }

  function =
      googlesql_base::FindOrNull(functions, FunctionSignatureIdToName(FN_ARRAY_LAST));
  EXPECT_THAT(function, NotNull());
  for (const FunctionSignature& signature : (*function)->signatures()) {
    EXPECT_FALSE(signature.options().rewrite_options()->enabled());
  }
}

TEST(SimpleFunctionTests, TestSuppliedArgumentTypes) {
  {
    // Test loading builtins without any `argument_types` specified.
    TypeFactory type_factory;
    NameToFunctionMap functions;
    NameToTypeMap types;
    BuiltinFunctionOptions options;
    GOOGLESQL_EXPECT_OK(
        GetBuiltinFunctionsAndTypes(options, type_factory, functions, types));

    for (const auto& fn_name : kApproxDistanceFunctionNames) {
      std::unique_ptr<Function>* function = googlesql_base::FindOrNull(functions, fn_name);
      EXPECT_THAT(function, NotNull());
      for (int i = 0; i < function->get()->NumSignatures(); ++i) {
        const FunctionSignature& signature = function->get()->signatures()[i];
        if (signature.arguments().size() == 3) {
          EXPECT_TRUE(signature.argument(2).type()->IsJson());
        }
      }
    }
  }
  {
    // Test supplying an invalid Type in `argument_types`
    TypeFactory type_factory;
    NameToFunctionMap functions;
    NameToTypeMap types;
    BuiltinFunctionOptions options;
    options
        .argument_types[{FN_APPROX_DOT_PRODUCT_INT64_WITH_PROTO_OPTIONS, 2}] =
        type_factory.get_double();
    EXPECT_THAT(
        GetBuiltinFunctionsAndTypes(options, type_factory, functions, types),
        StatusIs(absl::StatusCode::kInvalidArgument,
                 HasSubstr("Supplied argument type for the `options` argument "
                           "of function APPROX_DOT_PRODUCT must be a proto")));
  }
  {
    // Test supplying a function signature that does not support supplied
    // Types.
    TypeFactory type_factory;
    NameToFunctionMap functions;
    NameToTypeMap types;
    BuiltinFunctionOptions options;
    options.argument_types[{FN_ADD_DOUBLE, 0}] = type_factory.get_double();
    EXPECT_THAT(
        GetBuiltinFunctionsAndTypes(options, type_factory, functions, types),
        StatusIs(
            absl::StatusCode::kInternal,
            HasSubstr(
                "Argument 0 of function signature `FN_ADD_DOUBLE` does not "
                "support a supplied argument type in BuiltinFunctionOptions")));
  }
  {
    // Test supplying a Function Signature that does support supplied Types
    // but the incorrect index is specified.
    TypeFactory type_factory;
    NameToFunctionMap functions;
    NameToTypeMap types;
    BuiltinFunctionOptions options;
    googlesql_test::TestApproxDistanceFunctionOptionsProto options_proto;
    const ProtoType* proto_type;
    GOOGLESQL_EXPECT_OK(
        type_factory.MakeProtoType(options_proto.descriptor(), &proto_type));
    options
        .argument_types[{FN_APPROX_DOT_PRODUCT_INT64_WITH_PROTO_OPTIONS, 15}] =
        proto_type;
    EXPECT_THAT(
        GetBuiltinFunctionsAndTypes(options, type_factory, functions, types),
        StatusIs(
            absl::StatusCode::kInternal,
            HasSubstr(
                "Argument 15 of function signature "
                "`FN_APPROX_DOT_PRODUCT_INT64_WITH_PROTO_OPTIONS` does not "
                "support a supplied argument type in BuiltinFunctionOptions")));
  }
  {
    // Test supplying a valid Type in `argument_types` but that
    // conflicts with a function signature marked in `exclude_function_ids`
    TypeFactory type_factory;
    NameToFunctionMap functions;
    NameToTypeMap types;
    BuiltinFunctionOptions options;
    googlesql_test::TestApproxDistanceFunctionOptionsProto options_proto;
    const ProtoType* proto_type;
    GOOGLESQL_EXPECT_OK(
        type_factory.MakeProtoType(options_proto.descriptor(), &proto_type));
    options
        .argument_types[{FN_APPROX_DOT_PRODUCT_INT64_WITH_PROTO_OPTIONS, 2}] =
        proto_type;
    options.exclude_function_ids.insert(
        FN_APPROX_DOT_PRODUCT_INT64_WITH_PROTO_OPTIONS);
    EXPECT_THAT(
        GetBuiltinFunctionsAndTypes(options, type_factory, functions, types),
        StatusIs(
            absl::StatusCode::kInternal,
            HasSubstr(
                "Function signatures in `exclude_function_ids` are mutually "
                "exclusive with signatures in `argument_types`. "
                "Exception found for FunctionSignatureId "
                "`FN_APPROX_DOT_PRODUCT_INT64_WITH_PROTO_OPTIONS`")));
  }
  {
    // When specifying a signature in `include_function_ids`, a Type must be
    // supplied for all argument indices supporting a supplied Type, otherwise
    // it is an error.
    TypeFactory type_factory;
    NameToFunctionMap functions;
    NameToTypeMap types;
    BuiltinFunctionOptions options;
    googlesql_test::TestApproxDistanceFunctionOptionsProto options_proto;
    const ProtoType* proto_type;
    GOOGLESQL_EXPECT_OK(
        type_factory.MakeProtoType(options_proto.descriptor(), &proto_type));
    options
        .argument_types[{FN_APPROX_DOT_PRODUCT_INT64_WITH_PROTO_OPTIONS, 2}] =
        proto_type;
    options.include_function_ids.insert(
        FN_APPROX_DOT_PRODUCT_DOUBLE_WITH_PROTO_OPTIONS);
    EXPECT_THAT(
        GetBuiltinFunctionsAndTypes(options, type_factory, functions, types),
        StatusIs(absl::StatusCode::kInternal,
                 HasSubstr("Function signatures in `include_function_ids` must "
                           "define a supplied argument type for every argument "
                           "index that supports supplied argument types. "
                           "Exception found for FunctionSignatureId "
                           "`FN_APPROX_DOT_PRODUCT_DOUBLE_WITH_PROTO_OPTIONS` "
                           "at argument index 2")));
  }
  {
    // Test loading builtins with `argument_types` specified
    // for all approximate distance functions. Also test specifying
    // entries in `include_function_ids`.
    TypeFactory type_factory;
    NameToFunctionMap functions;
    NameToTypeMap types;
    BuiltinFunctionOptions options;
    googlesql_test::TestApproxDistanceFunctionOptionsProto options_proto;
    const ProtoType* proto_type;
    GOOGLESQL_EXPECT_OK(
        type_factory.MakeProtoType(options_proto.descriptor(), &proto_type));
    for (const auto& id : kApproxDistanceFunctionIds) {
      options.argument_types[{id, 2}] = proto_type;
      options.include_function_ids.insert(id);
    }
    GOOGLESQL_EXPECT_OK(
        GetBuiltinFunctionsAndTypes(options, type_factory, functions, types));

    for (const auto& fn_name : kApproxDistanceFunctionNames) {
      std::unique_ptr<Function>* function = googlesql_base::FindOrNull(functions, fn_name);
      EXPECT_THAT(function, NotNull());
      for (int i = 0; i < function->get()->NumSignatures(); ++i) {
        const FunctionSignature& signature = function->get()->signatures()[i];
        if (signature.arguments().size() == 3) {
          EXPECT_TRUE(signature.argument(2).type()->IsJson() ||
                      signature.argument(2).type()->IsProto());
          if (signature.argument(2).type()->IsProto()) {
            EXPECT_EQ(signature.argument(2).type()->AsProto()->descriptor(),
                      options_proto.descriptor());
          }
        }
      }
    }
  }
  {
    // Test loading builtins with `argument_types` specified
    // for some approximate distance functions, and with different PROTO types.
    TypeFactory type_factory;
    NameToFunctionMap functions;
    NameToTypeMap types;
    BuiltinFunctionOptions options;
    const ProtoType* proto_type_1;
    GOOGLESQL_EXPECT_OK(type_factory.MakeProtoType(
        googlesql_test::KitchenSinkPB::GetDescriptor(), &proto_type_1));
    const ProtoType* proto_type_2;
    GOOGLESQL_EXPECT_OK(type_factory.MakeProtoType(
        googlesql_test::TestExtraPB::GetDescriptor(), &proto_type_2));
    options.argument_types[{FN_APPROX_COSINE_DISTANCE_DOUBLE_WITH_PROTO_OPTIONS,
                            2}] = proto_type_1;
    options.argument_types[{
        FN_APPROX_EUCLIDEAN_DISTANCE_DOUBLE_WITH_PROTO_OPTIONS, 2}] =
        proto_type_2;
    GOOGLESQL_EXPECT_OK(
        GetBuiltinFunctionsAndTypes(options, type_factory, functions, types));

    for (const auto& fn_name : kApproxDistanceFunctionNames) {
      std::unique_ptr<Function>* function = googlesql_base::FindOrNull(functions, fn_name);
      EXPECT_THAT(function, NotNull());
      for (int i = 0; i < function->get()->NumSignatures(); ++i) {
        const FunctionSignature& signature = function->get()->signatures()[i];
        if (signature.arguments().size() == 3) {
          EXPECT_TRUE(signature.argument(2).type()->IsJson() ||
                      signature.argument(2).type()->IsProto());
          if (signature.argument(2).type()->IsProto()) {
            EXPECT_TRUE(
                signature.context_id() ==
                    FN_APPROX_COSINE_DISTANCE_DOUBLE_WITH_PROTO_OPTIONS ||
                signature.context_id() ==
                    FN_APPROX_EUCLIDEAN_DISTANCE_DOUBLE_WITH_PROTO_OPTIONS);
          }
        }
      }
    }
  }
}

TEST(SimpleFunctionTests, TestSuppliedArgumentTypesTVFs) {
  {
    // Test loading builtins without any `argument_types` specified.
    TypeFactory type_factory;
    NameToFunctionMap functions;
    NameToTypeMap types;
    LanguageOptions language_options;
    BuiltinFunctionOptions options(language_options);
    NameToTableValuedFunctionMap table_valued_functions;
    GOOGLESQL_EXPECT_OK(GetBuiltinFunctionsAndTypes(options, type_factory, functions,
                                          types, table_valued_functions));

    for (const auto& fn_name : kVectorSearchTVFNames) {
      std::unique_ptr<TableValuedFunction>* tvf =
          googlesql_base::FindOrNull(table_valued_functions, fn_name);
      EXPECT_THAT(tvf, IsNull());
    }
  }
  {
    // Test supplying an invalid Type in `argument_types`
    TypeFactory type_factory;
    NameToFunctionMap functions;
    NameToTypeMap types;
    LanguageOptions language_options;
    language_options.EnableLanguageFeature(FEATURE_VECTOR_SEARCH_TVF);
    BuiltinFunctionOptions options(language_options);
    NameToTableValuedFunctionMap table_valued_functions;
    options.argument_types[{FN_BATCH_VECTOR_SEARCH_TVF_WITH_PROTO_OPTIONS, 4}] =
        type_factory.get_double();
    EXPECT_THAT(
        GetBuiltinFunctionsAndTypes(options, type_factory, functions, types,
                                    table_valued_functions),
        StatusIs(absl::StatusCode::kInvalidArgument,
                 HasSubstr("Supplied argument type for the `options` argument "
                           "of function vector_search must be a proto")));
  }
  {
    // Test supplying a function signature that does not support supplied
    // Types.
    TypeFactory type_factory;
    NameToFunctionMap functions;
    NameToTypeMap types;
    LanguageOptions language_options;
    language_options.EnableLanguageFeature(FEATURE_VECTOR_SEARCH_TVF);
    BuiltinFunctionOptions options(language_options);
    NameToTableValuedFunctionMap table_valued_functions;
    options.argument_types[{FN_TUMBLE, 0}] = type_factory.get_double();
    EXPECT_THAT(
        GetBuiltinFunctionsAndTypes(options, type_factory, functions, types,
                                    table_valued_functions),
        StatusIs(
            absl::StatusCode::kInternal,
            HasSubstr(
                "Argument 0 of function signature `FN_TUMBLE` does not "
                "support a supplied argument type in BuiltinFunctionOptions")));
  }
  {
    // Test supplying a Function Signature that does support supplied Types
    // but the incorrect index is specified.
    TypeFactory type_factory;
    NameToFunctionMap functions;
    NameToTypeMap types;
    LanguageOptions language_options;
    language_options.EnableLanguageFeature(FEATURE_VECTOR_SEARCH_TVF);
    BuiltinFunctionOptions options(language_options);
    NameToTableValuedFunctionMap table_valued_functions;
    googlesql_test::TestApproxDistanceFunctionOptionsProto options_proto;
    const ProtoType* proto_type;
    GOOGLESQL_EXPECT_OK(
        type_factory.MakeProtoType(options_proto.descriptor(), &proto_type));
    options
        .argument_types[{FN_BATCH_VECTOR_SEARCH_TVF_WITH_PROTO_OPTIONS, 10}] =
        proto_type;
    EXPECT_THAT(
        GetBuiltinFunctionsAndTypes(options, type_factory, functions, types,
                                    table_valued_functions),
        StatusIs(
            absl::StatusCode::kInternal,
            HasSubstr(
                "Argument 10 of function signature "
                "`FN_BATCH_VECTOR_SEARCH_TVF_WITH_PROTO_OPTIONS` does not "
                "support a supplied argument type in BuiltinFunctionOptions")));
  }
  {
    // Test supplying a valid Type in `argument_types` but that
    // conflicts with a function signature marked in `exclude_function_ids`
    TypeFactory type_factory;
    NameToFunctionMap functions;
    NameToTypeMap types;
    LanguageOptions language_options;
    language_options.EnableLanguageFeature(FEATURE_VECTOR_SEARCH_TVF);
    BuiltinFunctionOptions options(language_options);
    NameToTableValuedFunctionMap table_valued_functions;
    googlesql_test::TestApproxDistanceFunctionOptionsProto options_proto;
    const ProtoType* proto_type;
    GOOGLESQL_EXPECT_OK(
        type_factory.MakeProtoType(options_proto.descriptor(), &proto_type));
    options.argument_types[{FN_BATCH_VECTOR_SEARCH_TVF_WITH_PROTO_OPTIONS, 4}] =
        proto_type;
    options.exclude_function_ids.insert(
        FN_BATCH_VECTOR_SEARCH_TVF_WITH_PROTO_OPTIONS);
    EXPECT_THAT(
        GetBuiltinFunctionsAndTypes(options, type_factory, functions, types,
                                    table_valued_functions),
        StatusIs(
            absl::StatusCode::kInternal,
            HasSubstr(
                "Function signatures in `exclude_function_ids` are mutually "
                "exclusive with signatures in `argument_types`. "
                "Exception found for FunctionSignatureId "
                "`FN_BATCH_VECTOR_SEARCH_TVF_WITH_PROTO_OPTIONS`")));
  }
  {
    // When specifying a signature in `include_function_ids`, a Type must be
    // supplied for all argument indices supporting a supplied Type, otherwise
    // it is an error.
    TypeFactory type_factory;
    NameToFunctionMap functions;
    NameToTypeMap types;
    LanguageOptions language_options;
    language_options.EnableLanguageFeature(FEATURE_VECTOR_SEARCH_TVF);
    BuiltinFunctionOptions options(language_options);
    NameToTableValuedFunctionMap table_valued_functions;
    googlesql_test::TestApproxDistanceFunctionOptionsProto options_proto;
    const ProtoType* proto_type;
    GOOGLESQL_EXPECT_OK(
        type_factory.MakeProtoType(options_proto.descriptor(), &proto_type));
    options.include_function_ids.insert(
        FN_BATCH_VECTOR_SEARCH_TVF_WITH_PROTO_OPTIONS);
    EXPECT_THAT(
        GetBuiltinFunctionsAndTypes(options, type_factory, functions, types,
                                    table_valued_functions),
        StatusIs(absl::StatusCode::kInternal,
                 HasSubstr("Function signatures in `include_function_ids` must "
                           "define a supplied argument type for every argument "
                           "index that supports supplied argument types. "
                           "Exception found for FunctionSignatureId "
                           "`FN_BATCH_VECTOR_SEARCH_TVF_WITH_PROTO_OPTIONS` "
                           "at argument index 4")));
  }
  {
    // Test loading builtins with `argument_types` specified
    // for all approximate distance functions. Also test specifying
    // entries in `include_function_ids`.
    TypeFactory type_factory;
    NameToFunctionMap functions;
    NameToTypeMap types;
    LanguageOptions language_options;
    language_options.EnableLanguageFeature(FEATURE_VECTOR_SEARCH_TVF);
    BuiltinFunctionOptions options(language_options);
    NameToTableValuedFunctionMap table_valued_functions;
    googlesql_test::TestApproxDistanceFunctionOptionsProto options_proto;
    const ProtoType* proto_type;
    GOOGLESQL_EXPECT_OK(
        type_factory.MakeProtoType(options_proto.descriptor(), &proto_type));
    for (const auto& id : kVectorSearchTVFFunctionIds) {
      options.argument_types[{id, 4}] = proto_type;
      options.include_function_ids.insert(id);
    }
    GOOGLESQL_EXPECT_OK(GetBuiltinFunctionsAndTypes(options, type_factory, functions,
                                          types, table_valued_functions));

    for (const auto& fn_name : kVectorSearchTVFNames) {
      std::unique_ptr<TableValuedFunction>* tvf =
          googlesql_base::FindOrNull(table_valued_functions, fn_name);
      EXPECT_THAT(tvf, NotNull());
      for (int i = 0; i < tvf->get()->NumSignatures(); ++i) {
        const FunctionSignature& signature = tvf->get()->signatures()[i];
        if (signature.arguments().size() == 8) {
          EXPECT_TRUE(signature.argument(4).type()->IsProto());
          if (signature.argument(4).type()->IsProto()) {
            EXPECT_EQ(signature.argument(4).type()->AsProto()->descriptor(),
                      options_proto.descriptor());
          }
        }
      }
    }
  }
}

TEST(SimpleFunctionTests, TestAllReleasedFunctions) {
  auto options_all = BuiltinFunctionOptions::AllReleasedFunctions();
  TypeFactory type_factory;
  absl::flat_hash_map<std::string, const Type*> all_types;
  absl::flat_hash_map<std::string, std::unique_ptr<Function>> all_functions;
  GOOGLESQL_ASSERT_OK(GetBuiltinFunctionsAndTypes(options_all, type_factory,
                                        all_functions, all_types));

  absl::flat_hash_map<std::string, const Type*> min_types;
  absl::flat_hash_map<std::string, std::unique_ptr<Function>> min_functions;
  GOOGLESQL_ASSERT_OK(
      GetBuiltinFunctionsAndTypes(BuiltinFunctionOptions(LanguageOptions()),
                                  type_factory, min_functions, min_types));

  EXPECT_THAT(all_functions.size(), Gt(min_functions.size()));
  absl::flat_hash_set<absl::string_view> all_function_names;
  for (const auto& [name, function] : all_functions) {
    all_function_names.insert(name);
  }

  // The primary function names are controlled by a language option, so we have
  // to exclude them from the test.
  constexpr auto kFunctionsToIgnore = googlesql_base::fixed_flat_set_of<absl::string_view>(
      {"kll_quantiles.merge_point_double", "kll_quantiles.init_double",
       "kll_quantiles.extract_point_double", "kll_quantiles.merge_double",
       "kll_quantiles.extract_double",
       "kll_quantiles.extract_relative_rank_double",
       "kll_quantiles.merge_relative_rank_double"});
  absl::flat_hash_set<absl::string_view> min_function_names;
  for (const auto& [name, function] : min_functions) {
    if (!kFunctionsToIgnore.contains(name)) {
      min_function_names.insert(name);
    }
  }
  EXPECT_THAT(all_function_names, IsSupersetOf(min_function_names));
  EXPECT_THAT(min_function_names, Not(IsSupersetOf(all_function_names)));
}

TEST(SimpleFunctionTests, TestReturningAPI) {
  auto options = BuiltinFunctionOptions::AllReleasedFunctions();
  TypeFactory type_factory;
  absl::flat_hash_map<std::string, const Type*> types;
  absl::flat_hash_map<std::string, std::unique_ptr<Function>> functions;
  GOOGLESQL_ASSERT_OK(
      GetBuiltinFunctionsAndTypes(options, type_factory, functions, types));
  EXPECT_THAT(functions, Not(IsEmpty()));

  types.clear();
  functions.clear();
  options.language_options.EnableLanguageFeature(
      FEATURE_ROUND_WITH_ROUNDING_MODE);
  GOOGLESQL_ASSERT_OK(
      GetBuiltinFunctionsAndTypes(options, type_factory, functions, types));
  EXPECT_THAT(types, Not(IsEmpty()));
  EXPECT_THAT(functions, Not(IsEmpty()));
}

TEST(SimpleFunctionTests, TestGetAllAPI) {
  auto builtins1 = GetDefaultBuiltinFunctionsAndTypes();
  EXPECT_GE(builtins1.functions().size(), 0);
  // There shouldn't builtin TVFs.
  // TODO b/436522497: Change it to EXPECT_GE once a GoogleSQL-builtin TVF is
  // added.
  EXPECT_EQ(builtins1.table_valued_functions().size(), 0);
  // This will need changed when one of the language features that controls a
  // built-in enum is moved out of 'in_development'.
  EXPECT_GE(builtins1.types().size(), 0);
  for (const auto& [name, function] : builtins1.functions()) {
    EXPECT_NE(function, nullptr);
    // Call any API to let sanitizers detect an improperly initialized object.
    EXPECT_FALSE(function->Name().empty());
  }
  for (const auto& [name, type] : builtins1.types()) {
    EXPECT_NE(type, nullptr);
    // Call any API to let sanitizers detect an improperly initialized object.
    EXPECT_FALSE(type->TypeName(PRODUCT_INTERNAL).empty());
  }
  for (const auto& [name, tvf] : builtins1.table_valued_functions()) {
    EXPECT_NE(tvf, nullptr);
    // Call any API to let sanitizers detect an improperly initialized object.
    EXPECT_FALSE(tvf->Name().empty());
  }

  auto builtins2 = GetDefaultBuiltinFunctionsAndTypes();
  // Multiple calls should return the same pointers.
  EXPECT_THAT(builtins1.functions(), Eq(builtins2.functions()));
  EXPECT_THAT(builtins1.types(), Eq(builtins2.types()));
  EXPECT_THAT(builtins1.table_valued_functions(),
              Eq(builtins2.table_valued_functions()));
}

}  // namespace googlesql
