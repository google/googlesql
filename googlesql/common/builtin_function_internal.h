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

#ifndef GOOGLESQL_COMMON_BUILTIN_FUNCTION_INTERNAL_H_
#define GOOGLESQL_COMMON_BUILTIN_FUNCTION_INTERNAL_H_

#include <stddef.h>

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/common/builtins_output_properties.h"
#include "googlesql/proto/options.pb.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/type.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace googlesql {

// Wrapper around FunctionSignature that allocates on the heap to reduce the
// impact on stack size while still using the same initializer list.
class FunctionSignatureOnHeap {
 public:
  FunctionSignatureOnHeap(FunctionArgumentType result_type,
                          FunctionArgumentTypeList arguments,
                          int64_t context_id)
      : signature_(new FunctionSignature(std::move(result_type),
                                         std::move(arguments), context_id)) {}

  FunctionSignatureOnHeap(FunctionArgumentType result_type,
                          FunctionArgumentTypeList arguments,
                          int64_t context_id, FunctionSignatureOptions options)
      : signature_(new FunctionSignature(std::move(result_type),
                                         std::move(arguments), context_id,
                                         std::move(options))) {}

  explicit FunctionSignatureOnHeap(const FunctionSignature& signature)
      : signature_(new FunctionSignature(signature)) {}

  const FunctionSignature& Get() const { return *signature_; }

 private:
  std::shared_ptr<FunctionSignature> signature_;
};

// A proxy object representing a simple FunctionArgumentType object, one that
// can be constructed very easily and cheaply, but lacking the full
// expressiveness of an actual FunctionArgumentType.
// If you need to specify a FunctionArgumentTypeOptions, you can't use the
// 'Simple' methods.
struct FunctionArgumentTypeProxy {
  FunctionArgumentTypeProxy(
      SignatureArgumentKind kind,
      FunctionArgumentType::ArgumentCardinality cardinality)
      : kind(kind), cardinality(cardinality) {}
  FunctionArgumentTypeProxy(
      SignatureArgumentKind kind, const Type* type,
      FunctionArgumentType::ArgumentCardinality cardinality)
      : kind(kind), cardinality(cardinality) {}
  // NOLINTNEXTLINE: runtime/explicit
  FunctionArgumentTypeProxy(SignatureArgumentKind kind) : kind(kind) {}

  FunctionArgumentTypeProxy(
      const Type* type, FunctionArgumentType::ArgumentCardinality cardinality)
      : type(type), cardinality(cardinality) {}

  FunctionArgumentTypeProxy(const Type* type)  // NOLINT: runtime/explicit
      : type(type) {}

  const Type* type = nullptr;
  SignatureArgumentKind kind = ARG_TYPE_FIXED;
  FunctionArgumentType::ArgumentCardinality cardinality =
      FunctionEnums::REQUIRED;

  operator FunctionArgumentType() const {  // NOLINT: runtime/explicit
    if (type == nullptr) {
      return FunctionArgumentType(kind, cardinality);
    } else {
      return FunctionArgumentType(type, cardinality);
    }
  }
};

// A proxy object representing a simple FunctionSignature object, one that
// can be constructed very easily and cheaply, but lacking the full
// expressiveness of an actual FunctionSignature.
// If you need to specify a FunctionSignatureOptions, you can't use the 'Simple'
// methods.
struct FunctionSignatureProxy {
  FunctionArgumentTypeProxy result_type;
  std::initializer_list<FunctionArgumentTypeProxy> arguments;
  FunctionSignatureId context_id;

  // Implicit conversion to a FunctionSignature object.
  operator FunctionSignature() const {  // NOLINT: runtime/explicit
    std::vector<FunctionArgumentType> argument_vec(arguments.begin(),
                                                   arguments.end());
    return FunctionSignature(result_type, std::move(argument_vec), context_id);
  }

  // Implicit conversion to a FunctionSignatureOnHeap. This is occasionally
  // useful when you want to _mostly_ use FunctionSignatureProxy, but must
  // fallback to FunctionSignatureOnHeap for a few signatures.
  operator FunctionSignatureOnHeap() const {  // NOLINT: runtime/explicit
    std::vector<FunctionArgumentType> argument_vec(arguments.begin(),
                                                   arguments.end());
    return FunctionSignatureOnHeap(result_type, std::move(argument_vec),
                                   context_id);
  }
};

using FunctionIdToNameMap =
    absl::flat_hash_map<FunctionSignatureId, std::string>;
using NameToFunctionMap =
    absl::flat_hash_map<std::string, std::unique_ptr<Function>>;
using NameToTypeMap = absl::flat_hash_map<std::string, const Type*>;
using NameToTableValuedFunctionMap =
    absl::flat_hash_map<std::string, std::unique_ptr<TableValuedFunction>>;

bool ArgumentsAreComparable(absl::Span<const InputArgumentType> arguments,
                            const LanguageOptions& language_options,
                            int* bad_argument_idx);

// Checks whether all arguments are/are not arrays depending on the value of the
// 'is_array' flag.
bool ArgumentsArrayType(const std::vector<InputArgumentType>& arguments,
                        bool is_array, int* bad_argument_idx);

// The argument <display_name> in the below ...FunctionSQL represents the
// operator and should include space if inputs and operator needs to be space
// separated for pretty printing.
// TODO: Consider removing this callback, since Function now knows
// whether it is operator, and has correct sql_name to print.
std::string InfixFunctionSQL(absl::string_view display_name,
                             absl::Span<const std::string> inputs);

std::string PreUnaryFunctionSQL(absl::string_view display_name,
                                absl::Span<const std::string> inputs);

std::string PostUnaryFunctionSQL(absl::string_view display_name,
                                 absl::Span<const std::string> inputs);

std::string DateAddOrSubFunctionSQL(absl::string_view display_name,
                                    absl::Span<const std::string> inputs);

std::string CountStarFunctionSQL(absl::Span<const std::string> inputs);

std::string AnonCountStarFunctionSQL(absl::Span<const std::string> inputs);

std::string SignatureTextForAnonCountStarFunction(
    const LanguageOptions& language_options, const Function& function,
    const FunctionSignature& signature);

std::string SignatureTextForAnonCountStarWithReportFunction(
    absl::string_view report_format, const LanguageOptions& language_options,
    const Function& function, const FunctionSignature& signature);

std::string SignatureTextForAnonQuantilesWithReportFunction(
    absl::string_view report_format, const LanguageOptions& language_options,
    const Function& function, const FunctionSignature& signature);

std::string AnonSumWithReportJsonFunctionSQL(
    absl::Span<const std::string> inputs);

std::string AnonSumWithReportProtoFunctionSQL(
    absl::Span<const std::string> inputs);

std::string AnonAvgWithReportJsonFunctionSQL(
    absl::Span<const std::string> inputs);

std::string AnonAvgWithReportProtoFunctionSQL(
    const std::vector<std::string>& inputs);

std::string AnonCountWithReportJsonFunctionSQL(
    absl::Span<const std::string> inputs);

std::string AnonCountWithReportProtoFunctionSQL(
    absl::Span<const std::string> inputs);

std::string AnonCountStarWithReportJsonFunctionSQL(
    absl::Span<const std::string> inputs);

std::string AnonCountStarWithReportProtoFunctionSQL(
    absl::Span<const std::string> inputs);

std::string AnonQuantilesWithReportJsonFunctionSQL(
    absl::Span<const std::string> inputs);

std::string AnonQuantilesWithReportProtoFunctionSQL(
    absl::Span<const std::string> inputs);

std::string CaseWithValueFunctionSQL(absl::Span<const std::string> inputs);

std::string CaseNoValueFunctionSQL(absl::Span<const std::string> inputs);

std::string ArrayAtOffsetFunctionSQL(const std::vector<std::string>& inputs);

std::string ArrayAtOrdinalFunctionSQL(const std::vector<std::string>& inputs);

std::string SafeArrayAtOffsetFunctionSQL(absl::Span<const std::string> inputs);

std::string SubscriptFunctionSQL(const std::vector<std::string>& inputs);
std::string SubscriptWithKeyFunctionSQL(bool safe,
                                        absl::Span<const std::string> inputs);
std::string SubscriptWithOffsetFunctionSQL(
    bool safe, absl::Span<const std::string> inputs);
std::string SubscriptWithOrdinalFunctionSQL(
    bool safe, absl::Span<const std::string> inputs);

std::string SafeArrayAtOrdinalFunctionSQL(
    const std::vector<std::string>& inputs);

std::string ProtoMapAtKeySQL(const std::vector<std::string>& inputs);

std::string SafeProtoMapAtKeySQL(absl::Span<const std::string> inputs);

std::string GenerateDateTimestampArrayFunctionSQL(
    absl::string_view function_name, absl::Span<const std::string> inputs);

// For MakeArray we explicitly prepend the array type to the function sql, thus
// array-type-name is expected to be passed as the first element of inputs.
std::string MakeArrayFunctionSQL(absl::Span<const std::string> inputs);

std::string ExtractFunctionSQL(absl::Span<const std::string> inputs);

std::string ExtractDateOrTimeFunctionSQL(absl::string_view date_part,
                                         absl::Span<const std::string> inputs);

bool ArgumentIsStringLiteral(const InputArgumentType& argument);

absl::Status CheckBitwiseOperatorArgumentsHaveSameType(
    absl::string_view operator_string,
    const std::vector<InputArgumentType>& arguments,
    const LanguageOptions& language_options);

absl::Status CheckBitwiseOperatorFirstArgumentIsIntegerOrBytes(
    absl::string_view operator_string,
    const std::vector<InputArgumentType>& arguments,
    const LanguageOptions& language_options);

absl::Status CheckDateDatetimeTimeTimestampTruncArguments(
    absl::string_view function_name,
    const std::vector<InputArgumentType>& arguments,
    const LanguageOptions& language_options);

absl::Status CheckLastDayArguments(
    const std::string& function_name,
    const std::vector<InputArgumentType>& arguments,
    const LanguageOptions& language_options);

absl::Status CheckExtractPreResolutionArguments(
    absl::Span<const InputArgumentType> arguments,
    const LanguageOptions& language_options);

absl::Status CheckExtractPostResolutionArguments(
    const googlesql::FunctionSignature& signature,
    absl::Span<const InputArgumentType> arguments,
    const LanguageOptions& language_options);

absl::Status CheckDateDatetimeTimestampAddSubArguments(
    absl::string_view function_name,
    const std::vector<InputArgumentType>& arguments,
    const LanguageOptions& language_options);

absl::Status CheckDateDatetimeTimeTimestampDiffArguments(
    const std::string& function_name,
    const std::vector<InputArgumentType>& arguments,
    const LanguageOptions& language_options);

// This function returns a GoogleSQL ProtoType for
// google::protobuf::EnumValueDescriptorProto, which is either from the <catalog> (if this
// ProtoType is present) or newly created in the <type_factory>.
absl::StatusOr<const Type*> GetOrMakeEnumValueDescriptorType(
    Catalog* catalog, TypeFactory* type_factory, CycleDetector* cycle,
    const FunctionSignature& /*signature*/,
    const std::vector<InputArgumentType>& arguments,
    const AnalyzerOptions& analyzer_options);

absl::Status CheckTimeAddSubArguments(
    absl::string_view function_name,
    const std::vector<InputArgumentType>& arguments,
    const LanguageOptions& language_options);

absl::Status CheckGenerateDateArrayArguments(
    absl::Span<const InputArgumentType> arguments,
    const LanguageOptions& language_options);

absl::Status CheckGenerateTimestampArrayArguments(
    absl::Span<const InputArgumentType> arguments,
    const LanguageOptions& language_options);

absl::Status CheckFormatPostResolutionArguments(
    const FunctionSignature& /*signature*/,
    absl::Span<const InputArgumentType> arguments,
    const LanguageOptions& language_options);

absl::Status CheckIsSupportedKeyType(
    absl::string_view function_name,
    const std::set<std::string>& supported_key_types,
    int key_type_argument_index, absl::Span<const InputArgumentType> arguments,
    const LanguageOptions& language_options);

const std::set<std::string>& GetSupportedKeyTypes();

const std::set<std::string>& GetSupportedRawKeyTypes();

bool IsStringLiteralComparedToBytes(const InputArgumentType& lhs_arg,
                                    const InputArgumentType& rhs_arg);

std::string NoMatchingSignatureForCaseNoValueFunction(
    absl::string_view qualified_function_name,
    absl::Span<const InputArgumentType> arguments, ProductMode product_mode);

std::string NoMatchingSignatureForFunctionUsingInterval(
    const std::string& qualified_function_name,
    absl::Span<const InputArgumentType> arguments, ProductMode product_mode,
    int index_of_interval_argument);

std::string NoMatchingSignatureForDateOrTimeAddOrSubFunction(
    const std::string& qualified_function_name,
    const std::vector<InputArgumentType>& arguments, ProductMode product_mode);

std::string NoMatchingSignatureForGenerateDateOrTimestampArrayFunction(
    const std::string& qualified_function_name,
    absl::Span<const InputArgumentType> arguments, ProductMode product_mode);

std::string NoMatchingSignatureForSubscript(
    absl::string_view offset_or_ordinal, absl::string_view operator_name,
    const std::vector<InputArgumentType>& arguments, ProductMode product_mode);

absl::Status CheckArgumentsSupportEquality(
    absl::string_view comparison_name, const FunctionSignature& /*signature*/,
    const std::vector<InputArgumentType>& arguments,
    const LanguageOptions& language_options);

absl::Status CheckArgumentsSupportGrouping(
    absl::string_view comparison_name, const FunctionSignature& signature,
    const std::vector<InputArgumentType>& arguments,
    const LanguageOptions& language_options);

absl::Status CheckArgumentsSupportComparison(
    absl::string_view comparison_name, const FunctionSignature& /*signature*/,
    const std::vector<InputArgumentType>& arguments,
    const LanguageOptions& language_options);

absl::Status CheckMinMaxArguments(
    const std::string& function_name,
    const std::vector<InputArgumentType>& arguments,
    const LanguageOptions& language_options);

absl::Status CheckGreatestLeastArguments(
    absl::string_view function_name,
    const std::vector<InputArgumentType>& arguments,
    const LanguageOptions& language_options);

absl::Status CheckArrayAggArguments(
    const std::vector<InputArgumentType>& arguments,
    const LanguageOptions& language_options);

absl::Status CheckArrayConcatArguments(
    const std::vector<InputArgumentType>& arguments,
    const LanguageOptions& language_options);

absl::Status CheckArrayIsDistinctArguments(
    absl::Span<const InputArgumentType> arguments,
    const LanguageOptions& language_options);

absl::Status CheckArrayDistinctArguments(
    absl::Span<const InputArgumentType> arguments,
    const LanguageOptions& language_options);

absl::Status CheckRangeBucketArguments(
    const std::vector<InputArgumentType>& arguments,
    const LanguageOptions& language_options);

std::string AnonCountStarBadArgumentErrorPrefix(const FunctionSignature&,
                                                int idx);

// Construct FunctionOptions for aggregate functions with default settings for
// an OVER clause.
FunctionOptions DefaultAggregateFunctionOptions();

// Create a `FunctionSignatureOptions` with the given SQL definition that
// will be inlined by `REWRITE_BUILTIN_FUNCTION_INLINER`.
FunctionSignatureOptions SetDefinitionForInlining(absl::string_view sql,
                                                  bool enabled = true);

// Checks if an arithmetic operation has a floating point type as its
// input.
std::string CheckHasFloatingPointArgument(
    const FunctionSignature& matched_signature,
    absl::Span<const InputArgumentType> arguments);

// Checks if at least one input argument has NUMERIC type.
std::string CheckHasNumericTypeArgument(
    const FunctionSignature& matched_signature,
    absl::Span<const InputArgumentType> arguments);

// Checks if all input arguments have NUMERIC or BIGNUMERIC type,
// including the case without input arguments.
std::string CheckAllArgumentsHaveNumericOrBigNumericType(
    const FunctionSignature& matched_signature,
    absl::Span<const InputArgumentType> arguments);

// Checks if there is at least one input argument and the last argument
// has NUMERIC type or BIGNUMERIC type.
std::string CheckLastArgumentHasNumericOrBigNumericType(
    const FunctionSignature& matched_signature,
    absl::Span<const InputArgumentType> arguments);

// Checks if at least one input argument has BIGNUMERIC type.
std::string CheckHasBigNumericTypeArgument(
    const FunctionSignature& matched_signature,
    absl::Span<const InputArgumentType> arguments);

// Checks if at least one input argument has INTERVAL type.
std::string CheckHasIntervalTypeArgument(
    const FunctionSignature& matched_signature,
    const std::vector<InputArgumentType>& arguments);

// Returns true if FN_CONCAT_STRING function can coerce argument of given type
// to STRING.
bool CanStringConcatCoerceFrom(const googlesql::Type* arg_type);

// Compute the result type for TOP_COUNT and TOP_SUM.
// The output type is
//   ARRAY<
//     STRUCT<`value` <arguments[0].type()>,
//            `<field2_name>` <arguments[1].type()> > >
absl::StatusOr<const Type*> ComputeResultTypeForTopStruct(
    const std::string& field2_name, Catalog* catalog, TypeFactory* type_factory,
    CycleDetector* cycle_detector, const FunctionSignature& /*signature*/,
    absl::Span<const InputArgumentType> arguments,
    const AnalyzerOptions& analyzer_options);

// Compute the result type for ST_NEAREST_NEIGHBORS.
// The output type is
//   ARRAY<
//     STRUCT<`neighbor` <arguments[0].type>,
//            `distance` Double> >
absl::StatusOr<const Type*> ComputeResultTypeForNearestNeighborsStruct(
    Catalog* catalog, TypeFactory* type_factory, CycleDetector* cycle_detector,
    const FunctionSignature& /*signature*/,
    absl::Span<const InputArgumentType> arguments,
    const AnalyzerOptions& analyzer_options);

void InsertCreatedFunction(NameToFunctionMap* functions,
                           const GoogleSQLBuiltinFunctionOptions& options,
                           Function* function_in);

void InsertFunction(NameToFunctionMap* functions,
                    const GoogleSQLBuiltinFunctionOptions& options,
                    absl::string_view name, Function::Mode mode,
                    const std::vector<FunctionSignatureOnHeap>& signatures,
                    FunctionOptions function_options);

// Inserts the given function if enabled with the given options, otherwise
// does nothing and returns absl::OkStatus();
//
// `types_to_insert` will also be inserted if the function is inserted, and the
// type are supported based on the options.
//
// If any error is returned, outputs are left in an undefined state.
//
// Note: This is currently based on at least one function signature being
// enabled.
absl::Status InsertFunctionAndTypes(
    NameToFunctionMap* functions, NameToTypeMap* types,
    const GoogleSQLBuiltinFunctionOptions& options, absl::string_view name,
    Function::Mode mode, const std::vector<FunctionSignatureOnHeap>& signatures,
    FunctionOptions function_options, std::vector<const Type*> types_to_insert);

// Note: This function is intentionally overloaded to prevent a default
// FunctionOptions object to be allocated on the callers stack.
void InsertFunction(NameToFunctionMap* functions,
                    const GoogleSQLBuiltinFunctionOptions& options,
                    absl::string_view name, Function::Mode mode,
                    const std::vector<FunctionSignatureOnHeap>& signatures);

void InsertSimpleFunction(
    NameToFunctionMap* functions,
    const GoogleSQLBuiltinFunctionOptions& options, absl::string_view name,
    Function::Mode mode,
    std::initializer_list<FunctionSignatureProxy> signatures,
    const FunctionOptions& function_options);

// Note: This function is intentionally overloaded to prevent a default
// FunctionOptions object to be allocated on the callers stack.
void InsertSimpleFunction(
    NameToFunctionMap* functions,
    const GoogleSQLBuiltinFunctionOptions& options, absl::string_view name,
    Function::Mode mode,
    std::initializer_list<FunctionSignatureProxy> signatures);

// Note: This function is intentionally overloaded to prevent a default
// FunctionOptions object to be allocated on the callers stack.
void InsertNamespaceFunction(
    NameToFunctionMap* functions,
    const GoogleSQLBuiltinFunctionOptions& options, absl::string_view space,
    absl::string_view name, Function::Mode mode,
    const std::vector<FunctionSignatureOnHeap>& signatures,
    FunctionOptions function_options);

void InsertSimpleNamespaceFunction(
    NameToFunctionMap* functions,
    const GoogleSQLBuiltinFunctionOptions& options, absl::string_view space,
    absl::string_view name, Function::Mode mode,
    std::initializer_list<FunctionSignatureProxy> signatures);

// Note: This function is intentionally overloaded to prevent a default
// FunctionOptions object to be allocated on the callers stack.
void InsertSimpleNamespaceFunction(
    NameToFunctionMap* functions,
    const GoogleSQLBuiltinFunctionOptions& options, absl::string_view space,
    absl::string_view name, Function::Mode mode,
    std::initializer_list<FunctionSignatureProxy> signatures,
    FunctionOptions function_options);

absl::Status InsertSimpleTableValuedFunction(
    NameToTableValuedFunctionMap* table_valued_functions,
    const GoogleSQLBuiltinFunctionOptions& options, absl::string_view name,
    const std::vector<FunctionSignatureOnHeap>& signatures,
    TableValuedFunctionOptions table_valued_function_options);

absl::Status InsertType(NameToTypeMap* types,
                        const GoogleSQLBuiltinFunctionOptions& options,
                        const Type* type);

void GetDatetimeExtractFunctions(TypeFactory* type_factory,
                                 const GoogleSQLBuiltinFunctionOptions& options,
                                 NameToFunctionMap* functions);

void GetDatetimeConversionFunctions(
    TypeFactory* type_factory, const GoogleSQLBuiltinFunctionOptions& options,
    NameToFunctionMap* functions);

void GetTimeAndDatetimeConstructionAndConversionFunctions(
    TypeFactory* type_factory, const GoogleSQLBuiltinFunctionOptions& options,
    NameToFunctionMap* functions);

void GetDatetimeCurrentFunctions(TypeFactory* type_factory,
                                 const GoogleSQLBuiltinFunctionOptions& options,
                                 NameToFunctionMap* functions);

void GetDatetimeAddSubFunctions(TypeFactory* type_factory,
                                const GoogleSQLBuiltinFunctionOptions& options,
                                NameToFunctionMap* functions);

void GetDatetimeDiffTruncLastNextFunctions(
    TypeFactory* type_factory, const GoogleSQLBuiltinFunctionOptions& options,
    NameToFunctionMap* functions);

void GetDatetimeBucketFunctions(TypeFactory* type_factory,
                                const GoogleSQLBuiltinFunctionOptions& options,
                                NameToFunctionMap* functions);

void GetDatetimeFormatFunctions(TypeFactory* type_factory,
                                const GoogleSQLBuiltinFunctionOptions& options,
                                NameToFunctionMap* functions);

void GetDatetimeFunctions(TypeFactory* type_factory,
                          const GoogleSQLBuiltinFunctionOptions& options,
                          NameToFunctionMap* functions);

void GetIntervalFunctions(TypeFactory* type_factory,
                          const GoogleSQLBuiltinFunctionOptions& options,
                          NameToFunctionMap* functions);

void GetArithmeticFunctions(TypeFactory* type_factory,
                            const GoogleSQLBuiltinFunctionOptions& options,
                            NameToFunctionMap* functions);

void GetBitwiseFunctions(TypeFactory* type_factory,
                         const GoogleSQLBuiltinFunctionOptions& options,
                         NameToFunctionMap* functions);

void GetAggregateFunctions(TypeFactory* type_factory,
                           const GoogleSQLBuiltinFunctionOptions& options,
                           NameToFunctionMap* functions);

void GetApproxFunctions(TypeFactory* type_factory,
                        const GoogleSQLBuiltinFunctionOptions& options,
                        NameToFunctionMap* functions);

void GetStatisticalFunctions(TypeFactory* type_factory,
                             const GoogleSQLBuiltinFunctionOptions& options,
                             NameToFunctionMap* functions);

void GetAnalyticFunctions(TypeFactory* type_factory,
                          const GoogleSQLBuiltinFunctionOptions& options,
                          NameToFunctionMap* functions);

absl::Status GetComparisonFunctions(
    TypeFactory* type_factory, const GoogleSQLBuiltinFunctionOptions& options,
    NameToFunctionMap* functions);

void GetLogicFunctions(TypeFactory* type_factory,
                       const GoogleSQLBuiltinFunctionOptions& options,
                       NameToFunctionMap* functions);

void GetStringFunctions(TypeFactory* type_factory,
                        const GoogleSQLBuiltinFunctionOptions& options,
                        NameToFunctionMap* functions);

void GetRegexFunctions(TypeFactory* type_factory,
                       const GoogleSQLBuiltinFunctionOptions& options,
                       NameToFunctionMap* functions);

absl::Status GetProto3ConversionFunctions(
    TypeFactory* type_factory, const GoogleSQLBuiltinFunctionOptions& options,
    NameToFunctionMap* functions);

void GetErrorHandlingFunctions(TypeFactory* type_factory,
                               const GoogleSQLBuiltinFunctionOptions& options,
                               NameToFunctionMap* functions);

void GetConditionalFunctions(TypeFactory* type_factory,
                             const GoogleSQLBuiltinFunctionOptions& options,
                             NameToFunctionMap* functions);

void GetMiscellaneousFunctions(TypeFactory* type_factory,
                               const GoogleSQLBuiltinFunctionOptions& options,
                               NameToFunctionMap* functions);

absl::Status GetDistanceFunctions(
    TypeFactory* type_factory, const GoogleSQLBuiltinFunctionOptions& options,
    NameToFunctionMap* functions, BuiltinsOutputProperties& output_properties);

void GetArrayMiscFunctions(TypeFactory* type_factory,
                           const GoogleSQLBuiltinFunctionOptions& options,
                           NameToFunctionMap* functions);

void GetArrayAggregationFunctions(
    TypeFactory* type_factory, const GoogleSQLBuiltinFunctionOptions& options,
    NameToFunctionMap* functions);

void GetArraySlicingFunctions(TypeFactory* type_factory,
                              const GoogleSQLBuiltinFunctionOptions& options,
                              NameToFunctionMap* functions);

void GetArrayFilteringFunctions(TypeFactory* type_factory,
                                const GoogleSQLBuiltinFunctionOptions& options,
                                NameToFunctionMap* functions);

absl::Status GetArrayFindFunctions(
    TypeFactory* type_factory, const GoogleSQLBuiltinFunctionOptions& options,
    NameToFunctionMap* functions, NameToTypeMap* types);

void GetArrayFilteringFunctions(TypeFactory* type_factory,
                                const GoogleSQLBuiltinFunctionOptions& options,
                                NameToFunctionMap* functions);

void GetArrayTransformFunctions(TypeFactory* type_factory,
                                const GoogleSQLBuiltinFunctionOptions& options,
                                NameToFunctionMap* functions);

void GetArrayIncludesFunctions(TypeFactory* type_factory,
                               const GoogleSQLBuiltinFunctionOptions& options,
                               NameToFunctionMap* functions);

absl::Status GetArrayZipFunctions(
    TypeFactory* type_factory, const GoogleSQLBuiltinFunctionOptions& options,
    NameToFunctionMap* functions, NameToTypeMap* types);

absl::Status GetStandaloneBuiltinEnumTypes(
    TypeFactory* type_factory, const GoogleSQLBuiltinFunctionOptions& options,
    NameToTypeMap* types);

void GetSubscriptFunctions(TypeFactory* type_factory,
                           const GoogleSQLBuiltinFunctionOptions& options,
                           NameToFunctionMap* functions);

absl::Status GetJSONFunctions(TypeFactory* type_factory,
                              const GoogleSQLBuiltinFunctionOptions& options,
                              NameToFunctionMap* functions,
                              NameToTypeMap* types);

absl::Status GetNumericFunctions(TypeFactory* type_factory,
                                 const GoogleSQLBuiltinFunctionOptions& options,
                                 NameToFunctionMap* functions,
                                 NameToTypeMap* types);

void GetTrigonometricFunctions(TypeFactory* type_factory,
                               const GoogleSQLBuiltinFunctionOptions& options,
                               NameToFunctionMap* functions);

absl::Status GetMathFunctions(TypeFactory* type_factory,
                              const GoogleSQLBuiltinFunctionOptions& options,
                              NameToFunctionMap* functions,
                              NameToTypeMap* types);

void GetNetFunctions(TypeFactory* type_factory,
                     const GoogleSQLBuiltinFunctionOptions& options,
                     NameToFunctionMap* functions);

void GetHllCountFunctions(TypeFactory* type_factory,
                          const GoogleSQLBuiltinFunctionOptions& options,
                          NameToFunctionMap* functions);

void GetD3ACountFunctions(TypeFactory* type_factory,
                          const GoogleSQLBuiltinFunctionOptions& options,
                          NameToFunctionMap* functions);

void GetKllQuantilesFunctions(TypeFactory* type_factory,
                              const GoogleSQLBuiltinFunctionOptions& options,
                              NameToFunctionMap* functions);

void GetHashingFunctions(TypeFactory* type_factory,
                         const GoogleSQLBuiltinFunctionOptions& options,
                         NameToFunctionMap* functions);

void GetEncryptionFunctions(TypeFactory* type_factory,
                            const GoogleSQLBuiltinFunctionOptions& options,
                            NameToFunctionMap* functions);

void GetGeographyFunctions(TypeFactory* type_factory,
                           const GoogleSQLBuiltinFunctionOptions& options,
                           NameToFunctionMap* functions);

void GetCompressionFunctions(TypeFactory* type_factory,
                             const GoogleSQLBuiltinFunctionOptions& options,
                             NameToFunctionMap* functions);

void GetAnonFunctions(TypeFactory* type_factory,
                      const GoogleSQLBuiltinFunctionOptions& options,
                      NameToFunctionMap* functions);

absl::Status GetDifferentialPrivacyFunctions(
    TypeFactory* type_factory, const GoogleSQLBuiltinFunctionOptions& options,
    NameToFunctionMap* functions, NameToTypeMap* types);

void GetTypeOfFunction(TypeFactory* type_factory,
                       const GoogleSQLBuiltinFunctionOptions& options,
                       NameToFunctionMap* functions);

void GetFilterFieldsFunction(TypeFactory* type_factory,
                             const GoogleSQLBuiltinFunctionOptions& options,
                             NameToFunctionMap* functions);

void GetRangeFunctions(TypeFactory* type_factory,
                       const GoogleSQLBuiltinFunctionOptions& options,
                       NameToFunctionMap* functions);

void GetElementWiseAggregationFunctions(
    TypeFactory* type_factory, const GoogleSQLBuiltinFunctionOptions& options,
    NameToFunctionMap* functions);

void GetGraphFunctions(TypeFactory* type_factory,
                       const GoogleSQLBuiltinFunctionOptions& options,
                       NameToFunctionMap* functions);

void GetMapCoreFunctions(TypeFactory* type_factory,
                         const GoogleSQLBuiltinFunctionOptions& options,
                         NameToFunctionMap* functions);

// Add MEASURE-type functions to the given map.
void GetMeasureFunctions(TypeFactory* type_factory,
                         const GoogleSQLBuiltinFunctionOptions& options,
                         NameToFunctionMap* functions);

// Add MATCH_RECOGNIZE functions to the given map.
void GetMatchRecognizeFunctions(TypeFactory* type_factory,
                                const GoogleSQLBuiltinFunctionOptions& options,
                                NameToFunctionMap* functions);

// Adds VECTOR_SEARCH TVF to the given map.
absl::Status GetVectorSearchTableValuedFunctions(
    TypeFactory* type_factory, const GoogleSQLBuiltinFunctionOptions& options,
    NameToTableValuedFunctionMap* table_valued_functions,
    BuiltinsOutputProperties& output_properties);

absl::Status GetTimeSeriesTableValuedFunctions(
    TypeFactory* type_factory, const GoogleSQLBuiltinFunctionOptions& options,
    NameToTableValuedFunctionMap* table_valued_functions);
}  // namespace googlesql

#endif  // GOOGLESQL_COMMON_BUILTIN_FUNCTION_INTERNAL_H_
