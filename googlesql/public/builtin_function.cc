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

#include "googlesql/common/builtin_function_internal.h"
#include "googlesql/common/builtins_output_properties.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/substitute.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

class AnalyzerOptions;

using NameToFunctionMap =
    absl::flat_hash_map<std::string, std::unique_ptr<Function>>;
using NameToFunctionPtrMap = absl::flat_hash_map<std::string, const Function*>;
using NameToTableValuedFunctionPtrMap =
    absl::flat_hash_map<std::string, const TableValuedFunction*>;
using NameToTypeMap = absl::flat_hash_map<std::string, const Type*>;
using NameToTableValuedFunctionMap =
    absl::flat_hash_map<std::string, std::unique_ptr<TableValuedFunction>>;

BuiltinFunctionsAndTypes GetDefaultBuiltinFunctionsAndTypes() {
  static const auto kFunctionsAndTypes = []() -> BuiltinFunctionsAndTypes {
    // Process lifetime.
    static auto& type_factory = *(new TypeFactory);
    static auto& types = *(new NameToTypeMap);
    static auto& unowned_functions = *(new NameToFunctionPtrMap);
    static auto& unowned_table_valued_functions =
        *(new NameToTableValuedFunctionPtrMap);

    NameToFunctionMap owned_functions;
    NameToTableValuedFunctionMap owned_table_valued_functions;
    absl::Status status = GetBuiltinFunctionsAndTypes(
        BuiltinFunctionOptions::AllReleasedFunctions(), type_factory,
        owned_functions, types, owned_table_valued_functions);
    // Non-OK status can be returned if the builtins_options is configured
    // incorrectly, or if an internal invariant is broken do to a bug in the
    // GoogleSQL library.
    GOOGLESQL_DCHECK_OK(status);
    for (auto& [name, function] : owned_functions) {
      unowned_functions.emplace(name, function.release());
    }
    for (auto& [name, function] : owned_table_valued_functions) {
      unowned_table_valued_functions.emplace(name, function.release());
    }
    return BuiltinFunctionsAndTypes(unowned_functions, types,
                                    unowned_table_valued_functions);
  }();
  return kFunctionsAndTypes;
}

static const FunctionIdToNameMap& GetFunctionIdToNameMap() {
  static FunctionIdToNameMap* id_map = []() {
    // Initialize map from GoogleSQL function to function names.
    FunctionIdToNameMap* id_map = new FunctionIdToNameMap();
    TypeFactory type_factory;
    NameToTypeMap types_ignored;
    NameToFunctionMap functions;
    NameToTableValuedFunctionMap table_valued_functions;

    // Enable the maximum language features.  This enables retrieving a maximum
    // set of functions and signatures.
    //
    // TODO: Change this to use only stable features and a select list
    //   of "in_development" features that control function signatures as of the
    //   time the change is made. When that list becomes empty, convert this to
    //   use `GetAllBuiltinFunctionsAndTypes`.
    LanguageOptions options;
    options.EnableMaximumLanguageFeaturesForDevelopment();
    options.set_product_mode(PRODUCT_INTERNAL);

    absl::Status status = GetBuiltinFunctionsAndTypes(
        BuiltinFunctionOptions(options), type_factory, functions, types_ignored,
        table_valued_functions);
    GOOGLESQL_DCHECK_OK(status);

    for (const auto& function_entry : functions) {
      for (const FunctionSignature& signature :
           function_entry.second->signatures()) {
        if (signature.options().is_aliased_signature()) {
          continue;
        }
        const bool inserted = googlesql_base::InsertIfNotPresent(
            id_map, static_cast<FunctionSignatureId>(signature.context_id()),
            function_entry.first);
        ABSL_DCHECK(inserted) << "Duplicate signature id: "
                         << signature.context_id();
      }
    }
    for (const auto& table_valued_function_entry : table_valued_functions) {
      for (const FunctionSignature& signature :
           table_valued_function_entry.second->signatures()) {
        if (signature.options().is_aliased_signature()) {
          continue;
        }
        const bool inserted = googlesql_base::InsertIfNotPresent(
            id_map, static_cast<FunctionSignatureId>(signature.context_id()),
            table_valued_function_entry.first);
        ABSL_DCHECK(inserted) << "Duplicate signature id: "
                         << signature.context_id();
      }
    }
    return id_map;
  }();
  return *id_map;
}

std::string FunctionSignatureIdToName(FunctionSignatureId id) {
  const std::string* name = googlesql_base::FindOrNull(GetFunctionIdToNameMap(), id);
  if (name != nullptr) {
    return *name;
  }
  return absl::StrCat("<INVALID FUNCTION ID: ", id, ">");
}

// DEPRECATED
void GetGoogleSQLFunctions(
    TypeFactory* type_factory, const BuiltinFunctionOptions& options,
    std::map<std::string, std::unique_ptr<Function>>* functions) {
  NameToTypeMap types_ignored;
  NameToFunctionMap adequately_efficient_function_map;
  absl::Status status = GetBuiltinFunctionsAndTypes(
      options, *type_factory, adequately_efficient_function_map, types_ignored);
  for (auto& [name, function] : adequately_efficient_function_map) {
    functions->emplace(name, std::move(function));
  }
  GOOGLESQL_DCHECK_OK(status);
}

static absl::Status ValidateBuiltinFunctionsAgainstOptions(
    const BuiltinFunctionOptions& options,
    const BuiltinsOutputProperties& output_properties) {
  for (const auto& [id_idx_pair, argument_type] : options.argument_types) {
    FunctionSignatureId signature_id = id_idx_pair.first;
    int arg_idx = id_idx_pair.second;
    // If we supply a Type, then the signature must support supplying a Type,
    // otherwise it is an error.
    if (!output_properties.SupportsSuppliedArgumentType(signature_id,
                                                        arg_idx)) {
      return absl::InternalError(absl::Substitute(
          "Argument $0 of function signature `$1` does not support a "
          "supplied argument type in BuiltinFunctionOptions",
          arg_idx, FunctionSignatureId_Name(signature_id)));
    }
    // If we exclude a function signature in `exclude_function_ids`, then
    // return an error if we also supplied a Type for it.
    if (options.exclude_function_ids.contains(signature_id)) {
      return absl::InternalError(absl::Substitute(
          "Function signatures in `exclude_function_ids` are mutually "
          "exclusive with signatures in `argument_types`. Exception "
          "found for FunctionSignatureId `$0`",
          FunctionSignatureId_Name(signature_id)));
    }
  }

  // If we include a function signature in `include_function_ids`, then we must
  // supply a Type for every argument index for which a supplied Type
  // is supported, otherwise return an error.
  for (FunctionSignatureId id : options.include_function_ids) {
    absl::flat_hash_set<int> supported_arg_indices =
        output_properties.GetSupportedArgumentIndicesForSuppliedType(id);
    for (int arg_idx : supported_arg_indices) {
      if (auto iter = options.argument_types.find({id, arg_idx});
          iter == options.argument_types.end()) {
        return absl::InternalError(absl::Substitute(
            "Function signatures in `include_function_ids` must define a "
            "supplied argument type for every argument index that supports "
            "supplied argument types. Exception found for FunctionSignatureId "
            "`$0` at argument index $1",
            FunctionSignatureId_Name(id), arg_idx));
      }
    }
  }
  return absl::OkStatus();
}

absl::Status GetBuiltinFunctionsAndTypes(
    const BuiltinFunctionOptions& options, TypeFactory& type_factory,
    NameToFunctionMap& functions, NameToTypeMap& types,
    NameToTableValuedFunctionMap& table_valued_functions) {
  // TODO: Enable these preconditions with global presubmit.
  // GOOGLESQL_RET_CHECK(types.empty());
  // GOOGLESQL_RET_CHECK(functions.empty());
  BuiltinsOutputProperties output_properties;
  GetDatetimeFunctions(&type_factory, options, &functions);
  GetIntervalFunctions(&type_factory, options, &functions);
  GetArithmeticFunctions(&type_factory, options, &functions);
  GetBitwiseFunctions(&type_factory, options, &functions);
  GetAggregateFunctions(&type_factory, options, &functions);
  GetApproxFunctions(&type_factory, options, &functions);
  GetStatisticalFunctions(&type_factory, options, &functions);
  GOOGLESQL_RETURN_IF_ERROR(GetComparisonFunctions(&type_factory, options, &functions));
  GetLogicFunctions(&type_factory, options, &functions);
  GetStringFunctions(&type_factory, options, &functions);
  GetRegexFunctions(&type_factory, options, &functions);
  GetErrorHandlingFunctions(&type_factory, options, &functions);
  GetConditionalFunctions(&type_factory, options, &functions);
  GetMiscellaneousFunctions(&type_factory, options, &functions);
  GOOGLESQL_RETURN_IF_ERROR(GetDistanceFunctions(&type_factory, options, &functions,
                                       output_properties));
  GetArrayMiscFunctions(&type_factory, options, &functions);
  GetArrayAggregationFunctions(&type_factory, options, &functions);
  GetSubscriptFunctions(&type_factory, options, &functions);
  GOOGLESQL_RETURN_IF_ERROR(GetJSONFunctions(&type_factory, options, &functions, &types));
  GOOGLESQL_RETURN_IF_ERROR(GetMathFunctions(&type_factory, options, &functions, &types));
  GetHllCountFunctions(&type_factory, options, &functions);
  GetKllQuantilesFunctions(&type_factory, options, &functions);
  GOOGLESQL_RETURN_IF_ERROR(
      GetProto3ConversionFunctions(&type_factory, options, &functions));
  GetAnalyticFunctions(&type_factory, options, &functions);

  GetNetFunctions(&type_factory, options, &functions);
  GetHashingFunctions(&type_factory, options, &functions);
  GetEncryptionFunctions(&type_factory, options, &functions);
  GetGeographyFunctions(&type_factory, options, &functions);
  GetCompressionFunctions(&type_factory, options, &functions);
  GetAnonFunctions(&type_factory, options, &functions);
  GOOGLESQL_RETURN_IF_ERROR(GetDifferentialPrivacyFunctions(&type_factory, options,
                                                  &functions, &types));
  GetTypeOfFunction(&type_factory, options, &functions);
  GetFilterFieldsFunction(&type_factory, options, &functions);
  GetRangeFunctions(&type_factory, options, &functions);
  GetArraySlicingFunctions(&type_factory, options, &functions);
  GetArrayFilteringFunctions(&type_factory, options, &functions);
  GetArrayTransformFunctions(&type_factory, options, &functions);
  GetArrayIncludesFunctions(&type_factory, options, &functions);
  GetElementWiseAggregationFunctions(&type_factory, options, &functions);
  GOOGLESQL_RETURN_IF_ERROR(
      GetArrayFindFunctions(&type_factory, options, &functions, &types));
  GOOGLESQL_RETURN_IF_ERROR(
      GetArrayZipFunctions(&type_factory, options, &functions, &types));
  GOOGLESQL_RETURN_IF_ERROR(
      GetStandaloneBuiltinEnumTypes(&type_factory, options, &types));
  GetGraphFunctions(&type_factory, options, &functions);
  GetMapCoreFunctions(&type_factory, options, &functions);
  GetMeasureFunctions(&type_factory, options, &functions);
  GetMatchRecognizeFunctions(&type_factory, options, &functions);
  GOOGLESQL_RETURN_IF_ERROR(GetVectorSearchTableValuedFunctions(
      &type_factory, options, &table_valued_functions, output_properties));
  GOOGLESQL_RETURN_IF_ERROR(GetTimeSeriesTableValuedFunctions(&type_factory, options,
                                                    &table_valued_functions));

  return ValidateBuiltinFunctionsAgainstOptions(options, output_properties);
}

absl::Status GetBuiltinFunctionsAndTypes(const BuiltinFunctionOptions& options,
                                         TypeFactory& type_factory,
                                         NameToFunctionMap& functions,
                                         NameToTypeMap& types) {
  NameToTableValuedFunctionMap table_valued_functions;
  return GetBuiltinFunctionsAndTypes(options, type_factory, functions, types,
                                     table_valued_functions);
}

bool FunctionMayHaveUnintendedArgumentCoercion(const Function* function) {
  if (function->NumSignatures() == 0 || !function->ArgumentsAreCoercible()) {
    return false;
  }
  // This only tests between signature arguments at the same argument
  // index.  It would not correctly analyze multiple signatures whose
  // corresponding arguments are not related to each other, but that
  // is not an issue at the time of the initial implementation.
  int max_num_arguments = 0;
  for (int signature_idx = 0; signature_idx < function->NumSignatures();
       ++signature_idx) {
    const FunctionSignature* signature = function->GetSignature(signature_idx);
    if (signature->arguments().size() > max_num_arguments) {
      max_num_arguments = signature->arguments().size();
    }
  }
  for (int argument_idx = 0; argument_idx < max_num_arguments; ++argument_idx) {
    bool has_signed_arguments = false;
    bool has_unsigned_arguments = false;
    bool has_floating_point_arguments = false;
    for (int signature_idx = 0; signature_idx < function->NumSignatures();
         ++signature_idx) {
      const FunctionSignature* signature =
          function->GetSignature(signature_idx);
      if (argument_idx < signature->arguments().size()) {
        const FunctionArgumentType& argument_type =
            signature->argument(argument_idx);
        if (argument_type.type() != nullptr) {
          if (argument_type.type()->IsSignedInteger()) {
            has_signed_arguments = true;
          } else if (argument_type.type()->IsUnsignedInteger()) {
            has_unsigned_arguments = true;
          } else if (argument_type.type()->IsFloatingPoint()) {
            has_floating_point_arguments = true;
          }
        }
      }
    }
    if (has_signed_arguments && has_floating_point_arguments &&
        !has_unsigned_arguments) {
      return true;
    }
  }
  return false;
}

}  // namespace googlesql
