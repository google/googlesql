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

// Builtin functions and methods used in the reference implementation.

#ifndef GOOGLESQL_REFERENCE_IMPL_FUNCTION_H_
#define GOOGLESQL_REFERENCE_IMPL_FUNCTION_H_

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/public/cast.h"
#include "googlesql/public/collator.h"
#include "googlesql/public/evaluator_table_iterator.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/functions/array_zip_mode.pb.h"
#include "googlesql/public/functions/date_time_util.h"
#include "googlesql/public/functions/regexp.h"
#include "googlesql/public/interval_value.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/pico_time.h"
#include "googlesql/public/proto/type_annotation.pb.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/reference_impl/common.h"
#include "googlesql/reference_impl/evaluation.h"
#include "googlesql/reference_impl/operator.h"
#include "googlesql/reference_impl/tuple.h"
#include "googlesql/reference_impl/tuple_comparator.h"
#include "googlesql/reference_impl/variable_id.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "absl/base/macros.h"
#include "absl/container/flat_hash_map.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "googlesql/base/source_location.h"
#include "absl/types/span.h"
#include "google/protobuf/descriptor.h"
#include "googlesql/base/optional_ref.h"
#include "re2/re2.h"

namespace googlesql {

enum class FunctionKind {
  // Indicates that a function is not built-in
  kInvalid,
  // Arithmetic functions
  kAdd,
  kSubtract,
  kMultiply,
  kDivide,
  kDiv,
  kSafeAdd,
  kSafeSubtract,
  kSafeMultiply,
  kSafeDivide,
  kMod,
  kUnaryMinus,
  kSafeNegate,
  // Comparison functions
  kEqual,
  kIsDistinct,
  kIsNotDistinct,
  kLess,
  kLessOrEqual,
  // Logical functions
  kAnd,
  kNot,
  kOr,
  // Aggregate functions
  kAndAgg,  // private function that ANDs all input values incl. NULLs
  kAnyValue,
  kApproxCountDistinct,
  kApproxTopSum,
  kArrayAgg,
  kArrayConcatAgg,
  kAvg,
  kBitAnd,
  kBitOr,
  kBitXor,
  kCount,
  kCountIf,
  kCorr,
  kCovarPop,
  kCovarSamp,
  kFirst,
  kLast,
  kLogicalAnd,
  kLogicalOr,
  kMax,
  kMin,
  kOrAgg,  // private function that ORs all input values incl. NULLs
  kStddevPop,
  kStddevSamp,
  kStringAgg,
  kSum,
  kVarPop,
  kVarSamp,
  kElementwiseSum,
  kElementwiseAvg,
  // Anonymization functions (broken link)
  kAnonSum,
  kAnonSumWithReportProto,
  kAnonSumWithReportJson,
  kAnonAvg,
  kAnonAvgWithReportProto,
  kAnonAvgWithReportJson,
  kAnonVarPop,
  kAnonStddevPop,
  kAnonQuantiles,
  kAnonQuantilesWithReportProto,
  kAnonQuantilesWithReportJson,
  // Differential privacy functions (broken link)
  kDifferentialPrivacySum,
  kDifferentialPrivacyAvg,
  kDifferentialPrivacyVarPop,
  kDifferentialPrivacyStddevPop,
  kDifferentialPrivacyQuantiles,
  // Exists function
  kExists,
  // GROUPING function
  kGrouping,
  // IsNull function
  kIsNull,
  kIsTrue,
  kIsFalse,
  // Cast function
  kCast,
  kLike,
  kLikeWithCollation,
  kLikeAny,
  kNotLikeAny,
  kLikeAnyWithCollation,
  kNotLikeAnyWithCollation,
  kLikeAll,
  kNotLikeAll,
  kLikeAllWithCollation,
  kNotLikeAllWithCollation,
  kLikeAnyArray,
  kLikeAnyArrayWithCollation,
  kNotLikeAnyArray,
  kNotLikeAnyArrayWithCollation,
  kLikeAllArray,
  kLikeAllArrayWithCollation,
  kNotLikeAllArray,
  kNotLikeAllArrayWithCollation,
  // BitCast functions
  kBitCastToInt32,
  kBitCastToInt64,
  kBitCastToUint32,
  kBitCastToUint64,
  // Bitwise functions
  kBitwiseNot,
  kBitwiseOr,
  kBitwiseXor,
  kBitwiseAnd,
  kBitwiseLeftShift,
  kBitwiseRightShift,
  // BitCount functions
  kBitCount,
  // Math functions
  kAbs,
  kSign,
  kRound,
  kTrunc,
  kCeil,
  kFloor,
  kIsNan,
  kIsInf,
  kIeeeDivide,
  kSqrt,
  kCbrt,
  kPow,
  kExp,
  kNaturalLogarithm,
  kDecimalLogarithm,
  kLogarithm,
  kCos,
  kCosh,
  kAcos,
  kAcosh,
  kSin,
  kSinh,
  kAsin,
  kAsinh,
  kTan,
  kTanh,
  kAtan,
  kAtanh,
  kAtan2,
  kCsc,
  kSec,
  kCot,
  kCsch,
  kSech,
  kCoth,
  kPi,
  kRadians,
  kDegrees,
  kPiNumeric,
  kPiBigNumeric,
  // Least and greatest functions
  kLeast,
  kGreatest,
  // Array functions
  // Note: All array functions *must* set the EvaluationContext to have
  // non-deterministic output if the output depends on the order of an input
  // array that is not order-preserving. See MaybeSetNonDeterministicArrayOutput
  // in the .cc file, and b/32308061 for an example of how this can cause test
  // failures.
  kArrayConcat,
  kArrayFilter,
  kArrayTransform,
  kArrayLength,
  kArrayToString,
  kArrayReverse,
  kArrayAtOrdinal,
  kArrayAtOffset,
  kSafeArrayAtOrdinal,
  kSafeArrayAtOffset,
  kArrayIsDistinct,
  kArrayDistinct,
  kGenerateArray,
  kGenerateDateArray,
  kGenerateTimestampArray,
  kRangeBucket,
  kArrayIncludes,
  kArrayIncludesAny,
  kArrayIncludesAll,
  kArrayFirst,
  kArrayLast,
  kArraySlice,
  kArrayFirstN,
  kArrayLastN,
  kArrayRemoveFirstN,
  kArrayRemoveLastN,
  kArrayMin,
  kArrayMax,
  kArraySum,
  kArrayAvg,
  kArrayOffset,
  kArrayFind,
  kArrayOffsets,
  kArrayFindAll,
  kArrayZip,

  kApply,

  // Proto map functions. Like array functions, the map functions must use
  // MaybeSetNonDeterministicArrayOutput.
  kProtoMapAtKey,
  kSafeProtoMapAtKey,
  kProtoMapContainsKey,
  kProtoModifyMap,
  // JSON functions
  kJsonExtract,
  kJsonExtractScalar,
  kJsonExtractArray,
  kJsonExtractStringArray,
  kJsonQueryArray,
  kJsonValueArray,
  kJsonQuery,
  kJsonValue,
  kToJson,
  kSafeToJson,
  kToJsonString,
  kParseJson,
  kStringArray,
  kInt32,
  kInt32Array,
  kInt64,
  kInt64Array,
  kUint32,
  kUint32Array,
  kUint64,
  kUint64Array,
  kDouble,
  kDoubleArray,
  kFloat,
  kFloatArray,
  kBool,
  kBoolArray,
  kJsonType,
  kLaxBool,
  kLaxBoolArray,
  kLaxInt32,
  kLaxInt32Array,
  kLaxInt64,
  kLaxInt64Array,
  kLaxUint32,
  kLaxUint32Array,
  kLaxUint64,
  kLaxUint64Array,
  kLaxDouble,
  kLaxDoubleArray,
  kLaxFloat,
  kLaxFloatArray,
  kLaxString,
  kLaxStringArray,
  kJsonArray,
  kJsonObject,
  kJsonRemove,
  kJsonSet,
  kJsonStripNulls,
  kJsonArrayInsert,
  kJsonArrayAppend,
  kJsonSubscript,
  kJsonContains,
  kJsonKeys,
  kJsonFlatten,
  // Proto functions
  kFromProto,
  kToProto,
  kMakeProto,
  kReplaceFields,
  kFilterFields,
  kExtractOneofCase,
  // Enum functions
  kEnumValueDescriptorProto,
  // String functions
  kByteLength,
  kCharLength,
  kConcat,
  kEndsWith,
  kEndsWithWithCollation,
  kFormat,
  kLength,
  kLower,
  kLtrim,
  kNormalize,
  kNormalizeAndCasefold,
  kToBase64,
  kFromBase64,
  kToHex,
  kFromHex,
  kAscii,
  kUnicode,
  kChr,
  kToCodePoints,
  kCodePointsToString,
  kCodePointsToBytes,
  kRegexpExtract,
  kRegexpExtractAll,
  kRegexpExtractGroups,
  kRegexpInstr,
  kRegexpContains,
  kRegexpMatch,
  kRegexpReplace,
  kReplace,
  kReplaceWithCollation,
  kRtrim,
  kSafeConvertBytesToString,
  kSplit,
  kSplitWithCollation,
  kSplitSubstr,
  kSplitSubstrWithCollation,
  kStartsWith,
  kStartsWithWithCollation,
  kStrpos,
  kStrposWithCollation,
  kInstr,
  kInstrWithCollation,
  kSubstr,
  kTrim,
  kUpper,
  kLpad,
  kRpad,
  kLeft,
  kRight,
  kRepeat,
  kReverse,
  kSoundex,
  kTranslate,
  kInitCap,
  kCollationKey,
  kCollate,
  // Date/Time functions
  kDateAdd,
  kDateSub,
  kDateDiff,
  kDateTrunc,
  kDateBucket,
  kLastDay,
  kNextDay,
  kDatetimeAdd,
  kDatetimeSub,
  kDatetimeDiff,
  kDatetimeTrunc,
  kDateTimeBucket,
  kTimeAdd,
  kTimeSub,
  kTimeDiff,
  kTimeTrunc,
  kTimestampAdd,
  kTimestampSub,
  kTimestampDiff,
  kTimestampTrunc,
  kTimestampBucket,
  kAddMonths,
  kMonthsBetween,
  kCurrentDate,
  kCurrentDatetime,
  kCurrentTime,
  kCurrentTimestamp,
  kDateFromUnixDate,
  kUnixDate,
  kExtractFrom,
  kExtractDateFrom,
  kExtractTimeFrom,
  kExtractDatetimeFrom,
  kFormatDate,
  kFormatDatetime,
  kFormatTime,
  kFormatTimestamp,
  kDate,
  kTimestamp,
  kTime,
  kDatetime,
  // Conversion functions
  kTimestampSeconds,
  kTimestampMillis,
  kTimestampMicros,
  kTimestampFromUnixSeconds,
  kTimestampFromUnixMillis,
  kTimestampFromUnixMicros,
  kSecondsFromTimestamp,
  kMillisFromTimestamp,
  kMicrosFromTimestamp,
  kString,
  kParseDate,
  kParseDatetime,
  kParseTime,
  kParseTimestamp,
  // Interval functions
  kIntervalCtor,
  kMakeInterval,
  kJustifyHours,
  kJustifyDays,
  kJustifyInterval,
  kToSecondsInterval,
  // Net functions
  kNetFormatIP,
  kNetParseIP,
  kNetFormatPackedIP,
  kNetParsePackedIP,
  kNetIPInNet,
  kNetMakeNet,
  kNetHost,
  kNetRegDomain,
  kNetPublicSuffix,
  kNetIPFromString,
  kNetSafeIPFromString,
  kNetIPToString,
  kNetIPNetMask,
  kNetIPTrunc,
  kNetIPv4FromInt64,
  kNetIPv4ToInt64,
  // Numbering functions
  kDenseRank,
  kRank,
  kRowNumber,
  kPercentRank,
  kCumeDist,
  kNtile,
  kIsFirst,
  kIsLast,
  // Navigation functions
  kFirstValue,
  kLastValue,
  kNthValue,
  kLead,
  kLag,
  kPercentileCont,
  kPercentileDisc,

  // Numeric functions
  kParseNumeric,
  kParseBignumeric,

  // Random functions
  kRand,
  kGenerateUuid,
  kNewUuid,

  // Hashing functions
  kMd5,
  kSha1,
  kSha256,
  kSha512,
  kFarmFingerprint,
  kHighwayFingerprint128,

  // Error function
  kError,

  // Range functions
  kRangeCtor,
  kRangeIsStartUnbounded,
  kRangeIsEndUnbounded,
  kRangeStart,
  kRangeEnd,
  kRangeOverlaps,
  kRangeIntersect,
  kGenerateRangeArray,
  kRangeContains,

  // Graph functions
  kPropertyExists,
  kSameGraphElement,
  kAllDifferentGraphElement,
  kIsSourceNode,
  kIsDestNode,
  kLabels,
  kPropertyNames,
  kElementDefinitionName,
  kElementId,
  kSourceNodeId,
  kDestinationNodeId,
  kPathLength,
  kPathNodes,
  kPathEdges,
  kPathFirst,
  kPathLast,
  kPathCreate,
  kUncheckedPathCreate,
  kPathConcat,
  kUncheckedPathConcat,
  kIsTrail,
  kIsAcyclic,
  kIsSimple,
  kDynamicPropertyEquals,

  // Distance functions
  kCosineDistance,
  kApproxCosineDistance,
  kEuclideanDistance,
  kApproxEuclideanDistance,
  kDotProduct,
  kApproxDotProduct,
  kManhattanDistance,
  kL1Norm,
  kL2Norm,
  kEditDistance,

  // Map functions
  kMapFromArray,
  kMapEntriesSorted,
  kMapEntriesUnsorted,
  kMapGet,
  kMapSubscript,
  kMapSubscriptWithKey,
  kMapSafeSubscriptWithKey,
  kMapContainsKey,
  kMapKeysSorted,
  kMapKeysUnsorted,
  kMapValuesSorted,
  kMapValuesUnsorted,
  kMapValuesSortedByKey,
  kMapEmpty,
  kMapInsert,
  kMapInsertOrReplace,
  kMapReplaceKeyValuePairs,
  kMapReplaceKeysAndLambda,
  kMapCardinality,
  kMapDelete,
  kMapFilter,
  // AGG(measure) function
  kMeasureAgg,
  // Compression functions
  kZstdCompress,
  kZstdDecompressToBytes,
  kZstdDecompressToString,

  // Builtin TVFs
  kTumble,
  kHop,
};

// Provides two utility methods to look up a built-in function name or function
// kind.
class BuiltinFunctionCatalog {
 public:
  BuiltinFunctionCatalog(const BuiltinFunctionCatalog&) = delete;
  BuiltinFunctionCatalog& operator=(const BuiltinFunctionCatalog&) = delete;

  static absl::StatusOr<FunctionKind> GetKindByName(absl::string_view name);

  static std::string GetDebugNameByKind(FunctionKind kind);

  // Returns the alias for the given function kind. If the function kind has no
  // alias, returns an empty string.
  static absl::string_view GetAliasByKind(FunctionKind kind);

 private:
  BuiltinFunctionCatalog() = default;
};

// Helper function to convert a series of ValueExpr to AlgebraArg.
// TODO: b/359716173: Remove this function once all usages of ValueExpr to
// represent function arguments are migrated to AlgebraArg.
std::vector<std::unique_ptr<AlgebraArg>> ConvertValueExprsToAlgebraArgs(
    std::vector<std::unique_ptr<ValueExpr>>&& arguments);

// Contains options for creating a built-in scalar function call.
struct BuiltinScalarFunctionCallOptions {
};

// Abstract built-in scalar function.
class BuiltinScalarFunction : public ScalarFunctionBody {
 public:
  BuiltinScalarFunction(const BuiltinScalarFunction&) = delete;
  BuiltinScalarFunction& operator=(const BuiltinScalarFunction&) = delete;

  BuiltinScalarFunction(FunctionKind kind, const Type* output_type)
      : ScalarFunctionBody(output_type), kind_(kind) {}

  ~BuiltinScalarFunction() override = default;

  FunctionKind kind() const { return kind_; }

  std::string debug_name() const override;

  // Returns true if any of the input values is null.
  static bool HasNulls(absl::Span<const Value> args);

  // Validates the input types according to the language options, and returns a
  // ScalarFunctionCallExpr upon success.
  //
  // Each element in 'arguments' is expected to be either a ValueExpr or
  // InlineLambdaExpr.
  static absl::StatusOr<std::unique_ptr<ScalarFunctionCallExpr>> CreateCall(
      FunctionKind kind, const LanguageOptions& language_options,
      const Type* output_type,
      std::vector<std::unique_ptr<AlgebraArg>> arguments,
      ResolvedFunctionCallBase::ErrorMode error_mode =
          ResolvedFunctionCallBase::DEFAULT_ERROR_MODE,
      const BuiltinScalarFunctionCallOptions& options = {});

  // Similar to the above, but for functions which do not accept any lambda
  // arguments.
  // TODO: b/359716173: Remove this function once all usages have been inlined.
  ABSL_DEPRECATED("Inline me!")
  static absl::StatusOr<std::unique_ptr<ScalarFunctionCallExpr>> CreateCall(
      FunctionKind kind, const LanguageOptions& language_options,
      const Type* output_type,
      std::vector<std::unique_ptr<ValueExpr>> arguments,
      ResolvedFunctionCallBase::ErrorMode error_mode =
          ResolvedFunctionCallBase::DEFAULT_ERROR_MODE) {
    return CreateCall(kind, language_options, output_type,
                      ConvertValueExprsToAlgebraArgs(std::move(arguments)),
                      error_mode);
  }

  static absl::StatusOr<std::unique_ptr<ScalarFunctionCallExpr>> CreateCast(
      const LanguageOptions& language_options, const Type* output_type,
      std::unique_ptr<ValueExpr> argument, std::unique_ptr<ValueExpr> format,
      std::unique_ptr<ValueExpr> time_zone, const TypeModifiers& type_modifiers,
      bool return_null_on_error, ResolvedFunctionCallBase::ErrorMode error_mode,
      std::unique_ptr<ExtendedCompositeCastEvaluator> extended_cast_evaluator);

  // If 'arguments' is not empty, validates the types of the inputs. Currently
  // it checks whether the inputs support equality comparison where
  // applicable, and whether civil time types are enabled in the language option
  // if there is any in the input types. Also, rejects arguments which are not
  // either a ValueExpr or InlineLambdaExpr.
  static absl::StatusOr<std::unique_ptr<BuiltinScalarFunction>> CreateValidated(
      FunctionKind kind, const LanguageOptions& language_options,
      const Type* output_type,
      absl::Span<const std::unique_ptr<AlgebraArg>> arguments,
      const BuiltinScalarFunctionCallOptions& options = {});

  // Given a list of the AlgebraArgs to a function, store the arguments which
  // cannot be represented as `googlesql::Value`. Intended to be used only once,
  // immediately after construction. Only arguments which cannot be represented
  // as `googlesql::Value` are included, and the rest are set as nullopt.
  void SetExtendedArgs(
      absl::Span<const std::unique_ptr<AlgebraArg>> arguments) {
    ABSL_DCHECK_EQ(extended_args_.size(), 0)
        << "Function extended_args_ should not be set more than once.";

    extended_args_.clear();
    extended_args_.reserve(arguments.size());

    for (const auto& arg : arguments) {
      if (!arg->has_value_expr()) {
        extended_args_.push_back(arg.get());
      } else {
        extended_args_.push_back(std::nullopt);
      }
    }
  }

 protected:
  // Function arguments which cannot be represented as `googlesql::Value`,
  // such as `InlineLambdaArg`. Order and position of the non-value arguments is
  // preserved by using nullopt in place of Value-coercible arguments.
  const std::vector<googlesql_base::optional_ref<AlgebraArg>>& extended_args() const {
    return extended_args_;
  }

 private:
  // Like CreateValidated(), but returns a raw pointer with ownership.
  static absl::StatusOr<BuiltinScalarFunction*> CreateValidatedRaw(
      FunctionKind kind, const LanguageOptions& language_options,
      const Type* output_type,
      absl::Span<const std::unique_ptr<AlgebraArg>> arguments,
      const BuiltinScalarFunctionCallOptions& options);

  // Creates a like function.
  static absl::StatusOr<std::unique_ptr<BuiltinScalarFunction>>
  CreateLikeFunction(FunctionKind kind, const Type* output_type,
                     absl::Span<const std::unique_ptr<AlgebraArg>> arguments);

  // Creates a like any/all function.
  static absl::StatusOr<std::unique_ptr<BuiltinScalarFunction>>
  CreateLikeAnyAllFunction(
      FunctionKind kind, const Type* output_type,
      absl::Span<const std::unique_ptr<AlgebraArg>> arguments);

  // Creates a like any/all array function.
  static absl::StatusOr<std::unique_ptr<BuiltinScalarFunction>>
  CreateLikeAnyAllArrayFunction(
      FunctionKind kind, const Type* output_type,
      absl::Span<const std::unique_ptr<AlgebraArg>> arguments);

  // Creates a regexp function.
  static absl::StatusOr<std::unique_ptr<BuiltinScalarFunction>>
  CreateRegexpFunction(FunctionKind kind, const Type* output_type,
                       absl::Span<const std::unique_ptr<AlgebraArg>> arguments);

  FunctionKind kind_;
  std::vector<googlesql_base::optional_ref<AlgebraArg>> extended_args_;
};

// Abstract built-in Table Valued Function.
class BuiltinTableValuedFunction : public TableValuedFunctionBody {
 public:
  BuiltinTableValuedFunction(const BuiltinTableValuedFunction&) = delete;
  BuiltinTableValuedFunction& operator=(const BuiltinTableValuedFunction&) =
      delete;

  explicit BuiltinTableValuedFunction(FunctionKind kind) : kind_(kind) {}

  ~BuiltinTableValuedFunction() override = default;

  FunctionKind kind() const { return kind_; }

  static absl::StatusOr<std::unique_ptr<TableValuedFunctionCallExpr>>
  CreateCall(FunctionKind kind, std::vector<TvfAlgebraArgument> arguments,
             std::vector<TVFSchemaColumn> output_columns,
             std::vector<VariableId> variables,
             std::shared_ptr<FunctionSignature> function_call_signature);

  static absl::StatusOr<std::unique_ptr<BuiltinTableValuedFunction>> Create(
      FunctionKind kind);

 private:
  FunctionKind kind_;
};

// Alternate form of BuiltinScalarFunction that is easier to implement for
// functions that are slow enough that return absl::StatusOr<Value> from
// Eval() doesn't really matter.
class SimpleBuiltinScalarFunction : public BuiltinScalarFunction {
 public:
  using BuiltinScalarFunction::BuiltinScalarFunction;

  using BuiltinScalarFunction::Eval;
  virtual absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                                     absl::Span<const Value> args,
                                     EvaluationContext* context) const = 0;

  bool Eval(absl::Span<const TupleData* const> params,
            absl::Span<const Value> args, EvaluationContext* context,
            Value* result, absl::Status* status) const override {
    auto status_or_value = Eval(params, args, context);
    if (!status_or_value.ok()) {
      *status = status_or_value.status();
      return false;
    }
    *result = std::move(status_or_value.value());
    return true;
  }
};

// Abstract built-in aggregate function.
class BuiltinAggregateFunction : public AggregateFunctionBody {
 public:
  BuiltinAggregateFunction(FunctionKind kind, const Type* output_type,
                           int num_input_fields, const Type* input_type,
                           bool ignores_null = true)
      : AggregateFunctionBody(output_type, num_input_fields, input_type,
                              ignores_null),
        kind_(kind) {}

  BuiltinAggregateFunction(const BuiltinAggregateFunction&) = delete;
  BuiltinAggregateFunction& operator=(const BuiltinAggregateFunction&) = delete;

  FunctionKind kind() const { return kind_; }

  std::string debug_name() const override;

  absl::StatusOr<std::unique_ptr<AggregateAccumulator>> CreateAccumulator(
      absl::Span<const Value> args, absl::Span<const TupleData* const> params,
      CollatorList collator_list, EvaluationContext* context) const override;

 private:
  const FunctionKind kind_;
};

class BinaryStatFunction : public BuiltinAggregateFunction {
 public:
  BinaryStatFunction(FunctionKind kind, const Type* output_type,
                     const Type* input_type)
      : BuiltinAggregateFunction(kind, output_type, /*num_input_fields=*/2,
                                 input_type, /*ignores_null=*/true) {}

  BinaryStatFunction(const BinaryStatFunction&) = delete;
  BinaryStatFunction& operator=(const BinaryStatFunction&) = delete;

  absl::StatusOr<std::unique_ptr<AggregateAccumulator>> CreateAccumulator(
      absl::Span<const Value> args, absl::Span<const TupleData* const> params,
      CollatorList collator_list, EvaluationContext* context) const override;
};

// Adds additional members to `AggregateFunctionEvaluator` that the reference
// implementation needs in order to enable SQL evaluation during aggregation.
class SqlDefinedAggregateFunctionEvaluator : public AggregateFunctionEvaluator {
 public:
  ~SqlDefinedAggregateFunctionEvaluator() override = default;

  // Sets an evaluation context.
  virtual void SetEvaluationContext(
      EvaluationContext* context,
      absl::Span<const TupleData* const> params) = 0;
};

using ContextAwareFunctionEvaluator = std::function<absl::StatusOr<Value>(
    const absl::Span<const Value> arguments, EvaluationContext& context)>;

class UserDefinedScalarFunction : public ScalarFunctionBody {
 public:
  UserDefinedScalarFunction(const ContextAwareFunctionEvaluator& evaluator,
                            const Type* output_type,
                            absl::string_view function_name)
      : ScalarFunctionBody(output_type),
        evaluator_(evaluator),
        function_name_(function_name) {}
  std::string debug_name() const override;
  bool Eval(absl::Span<const TupleData* const> params,
            absl::Span<const Value> args, EvaluationContext* context,
            Value* result, absl::Status* status) const override;

 private:
  ContextAwareFunctionEvaluator evaluator_;
  const std::string function_name_;
};

// Factory method to create a UserDefinedAggregateFunction
absl::StatusOr<std::unique_ptr<AggregateFunctionBody>>
MakeUserDefinedAggregateFunction(
    AggregateFunctionEvaluatorFactory evaluator_factory,
    const FunctionSignature& function_signature, TypeFactory* type_factory,
    absl::string_view function_name, bool ignores_null);

// Abstract built-in (non-aggregate) analytic function.
class BuiltinAnalyticFunction : public AnalyticFunctionBody {
 public:
  BuiltinAnalyticFunction(FunctionKind kind, const Type* output_type)
      : AnalyticFunctionBody(output_type), kind_(kind) {}

  BuiltinAnalyticFunction(const BuiltinAnalyticFunction&) = delete;
  BuiltinAnalyticFunction& operator=(const BuiltinAnalyticFunction&) = delete;

  std::string debug_name() const override;

 private:
  FunctionKind kind_;
};

// Provides a method to look up the implementation class for built-in functions.
// Functions and TVFs should be registered with BuiltinFunctionRegistry when
// they have large/expensive dependencies, this allows users of evaluator_lite
// to avoid picking up the deps for functions they don't need.
class BuiltinFunctionRegistry {
 public:
  BuiltinFunctionRegistry(const BuiltinFunctionRegistry&) = delete;
  BuiltinFunctionRegistry& operator=(const BuiltinFunctionRegistry&) = delete;

  // Returns an unowned pointer to the function. The caller is responsible for
  // cleanup.
  static absl::StatusOr<BuiltinScalarFunction*> GetScalarFunction(
      FunctionKind kind, const Type* output_type,
      absl::Span<const std::unique_ptr<AlgebraArg>> arguments);

  // Registers a function implementation for one or more FunctionKinds.
  static void RegisterScalarFunction(
      std::initializer_list<FunctionKind> kinds,
      const std::function<BuiltinScalarFunction*(FunctionKind, const Type*)>&
          constructor);

  // Returns an unowned pointer to the TVF. The caller is responsible for
  // cleanup.
  static absl::StatusOr<BuiltinTableValuedFunction*> GetTableValuedFunction(
      FunctionKind kind);

  // Registers a TVF implementation for one or more FunctionKinds.
  static void RegisterTableValuedFunction(
      std::initializer_list<FunctionKind> kinds,
      const std::function<BuiltinTableValuedFunction*(FunctionKind)>&
          constructor);

 private:
  BuiltinFunctionRegistry() = default;

  using ScalarFunctionConstructor =
      std::function<BuiltinScalarFunction*(const Type*)>;
  static absl::flat_hash_map<FunctionKind, ScalarFunctionConstructor>&
  GetFunctionMap();
  using TableValuedFunctionConstructor =
      std::function<BuiltinTableValuedFunction*()>;
  static absl::flat_hash_map<FunctionKind, TableValuedFunctionConstructor>&
  GetTableValuedFunctionMap();

  static absl::Mutex mu_;
};

class ArithmeticFunction : public BuiltinScalarFunction {
 public:
  using BuiltinScalarFunction::BuiltinScalarFunction;
  bool Eval(absl::Span<const TupleData* const> params,
            absl::Span<const Value> args, EvaluationContext* context,
            Value* result, absl::Status* status) const override;

 private:
  // Helper function to add/subtract INTERVAL.
  absl::Status AddIntervalHelper(const Value& arg,
                                 const IntervalValue& interval, Value* result,
                                 EvaluationContext* context) const;
};

class ComparisonFunction : public BuiltinScalarFunction {
 public:
  using BuiltinScalarFunction::BuiltinScalarFunction;
  bool Eval(absl::Span<const TupleData* const> params,
            absl::Span<const Value> args, EvaluationContext* context,
            Value* result, absl::Status* status) const override;
};

class LogicalFunction : public BuiltinScalarFunction {
 public:
  using BuiltinScalarFunction::BuiltinScalarFunction;
  bool Eval(absl::Span<const TupleData* const> params,
            absl::Span<const Value> args, EvaluationContext* context,
            Value* result, absl::Status* status) const override;
};

class ExistsFunction : public BuiltinScalarFunction {
 public:
  using BuiltinScalarFunction::BuiltinScalarFunction;
  bool Eval(absl::Span<const TupleData* const> params,
            absl::Span<const Value> args, EvaluationContext* context,
            Value* result, absl::Status* status) const override;
};

class ArrayLengthFunction : public BuiltinScalarFunction {
 public:
  using BuiltinScalarFunction::BuiltinScalarFunction;
  bool Eval(absl::Span<const TupleData* const> params,
            absl::Span<const Value> args, EvaluationContext* context,
            Value* result, absl::Status* status) const override;
};

// Contains all information necessary to evaluate a lambda, given a list of
// arguments.
class LambdaEvaluationContext {
 public:
  LambdaEvaluationContext(absl::Span<const TupleData* const> params,
                          EvaluationContext* context)
      : params_(params), context_(context) {}

 public:
  absl::StatusOr<Value> EvaluateLambda(const InlineLambdaExpr* lambda,
                                       absl::Span<const Value> args);

 private:
  // Params to be passed to lambda. Used when a lambda needs to fetch a value
  // outside of its argument list, for example, a query parameter.
  absl::Span<const TupleData* const> params_;
  EvaluationContext* context_;
  std::shared_ptr<TupleSlot::SharedProtoState> shared_proto_state_;
};

class ArrayFilterFunction : public SimpleBuiltinScalarFunction {
 public:
  ArrayFilterFunction(FunctionKind kind, const Type* output_type,
                      const InlineLambdaExpr* lambda)
      : SimpleBuiltinScalarFunction(kind, output_type), lambda_(lambda) {}

  absl::StatusOr<Value> Eval(
      absl::Span<const TupleData* const> params, absl::Span<const Value> args,
      EvaluationContext* evaluation_context) const override;

 private:
  const InlineLambdaExpr* lambda_;
};

class ArrayTransformFunction : public SimpleBuiltinScalarFunction {
 public:
  ArrayTransformFunction(FunctionKind kind, const Type* output_type,
                         const InlineLambdaExpr* lambda)
      : SimpleBuiltinScalarFunction(kind, output_type), lambda_(lambda) {}

  absl::StatusOr<Value> Eval(
      absl::Span<const TupleData* const> params, absl::Span<const Value> args,
      EvaluationContext* evaluation_context) const override;

 private:
  const InlineLambdaExpr* lambda_;
};

class ArrayIncludesFunctionWithLambda : public SimpleBuiltinScalarFunction {
 public:
  ArrayIncludesFunctionWithLambda(FunctionKind kind, const Type* output_type,
                                  const InlineLambdaExpr* lambda)
      : SimpleBuiltinScalarFunction(kind, output_type), lambda_(lambda) {}

  absl::StatusOr<Value> Eval(
      absl::Span<const TupleData* const> params, absl::Span<const Value> args,
      EvaluationContext* evaluation_context) const override;

 private:
  const InlineLambdaExpr* lambda_;
};

class ArrayConcatFunction : public BuiltinScalarFunction {
 public:
  using BuiltinScalarFunction::BuiltinScalarFunction;
  bool Eval(absl::Span<const TupleData* const> params,
            absl::Span<const Value> args, EvaluationContext* context,
            Value* result, absl::Status* status) const override;
};

class ArrayToStringFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class ArrayReverseFunction : public SimpleBuiltinScalarFunction {
 public:
  explicit ArrayReverseFunction(const Type* output_type)
      : SimpleBuiltinScalarFunction(FunctionKind::kArrayReverse, output_type) {}
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class ArrayIsDistinctFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class ArrayDistinctFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

// Implementation for ARRAY_INCLUDES(ARRAY<T1>, T1) -> BOOL.
// For lambda version, see `ArrayIncludesLambdaEvaluationHandler`.
class ArrayIncludesFunction : public SimpleBuiltinScalarFunction {
 public:
  explicit ArrayIncludesFunction() : ArrayIncludesFunction(CollatorList()) {}

  explicit ArrayIncludesFunction(CollatorList collator_list)
      : SimpleBuiltinScalarFunction(FunctionKind::kArrayIncludes,
                                    types::BoolType()),
        collator_list_(std::move(collator_list)) {}

  static absl::StatusOr<std::unique_ptr<ScalarFunctionCallExpr>> CreateCall(
      const LanguageOptions& language_options,
      std::vector<std::unique_ptr<AlgebraArg>> arguments,
      ResolvedFunctionCallBase::ErrorMode error_mode,
      CollatorList collator_list);

  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;

 private:
  CollatorList collator_list_;
};

// Implementation for ARRAY_INCLUDES_(ANY|ALL)(ARRAY<T1>, ARRAY<T1>) -> BOOL.
class ArrayIncludesArrayFunction : public SimpleBuiltinScalarFunction {
 public:
  explicit ArrayIncludesArrayFunction(bool require_all)
      : ArrayIncludesArrayFunction(require_all, CollatorList()) {}

  ArrayIncludesArrayFunction(bool require_all, CollatorList collator_list)
      : SimpleBuiltinScalarFunction(require_all
                                        ? FunctionKind::kArrayIncludesAll
                                        : FunctionKind::kArrayIncludesAny,
                                    types::BoolType()),
        collator_list_(std::move(collator_list)) {}

  static absl::StatusOr<std::unique_ptr<ScalarFunctionCallExpr>> CreateCall(
      bool require_all, const LanguageOptions& language_options,
      std::vector<std::unique_ptr<AlgebraArg>> arguments,
      ResolvedFunctionCallBase::ErrorMode error_mode,
      CollatorList collator_list);

  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;

 private:
  CollatorList collator_list_;
};

// Implementation for ARRAY_(FIRST|LAST)(ARRAY<T1>) -> T1.
class ArrayFirstLastFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;

  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

// Implementation for ARRAY_SLICE(ARRAY<T1>, INT64, INT64) -> ARRAY<T1>.
class ArraySliceFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;

  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

// Implementation for ARRAY_(MIN|MAX)(ARRAY<T1>) -> T1.
class ArrayMinMaxFunction : public SimpleBuiltinScalarFunction {
 public:
  ArrayMinMaxFunction(FunctionKind kind, const Type* output_type)
      : ArrayMinMaxFunction(kind, output_type, CollatorList()) {}

  ArrayMinMaxFunction(FunctionKind kind, const Type* output_type,
                      CollatorList collator_list)
      : SimpleBuiltinScalarFunction(kind, output_type),
        collator_list_(std::move(collator_list)) {}

  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;

  static absl::StatusOr<std::unique_ptr<ScalarFunctionCallExpr>> CreateCall(
      FunctionKind kind, const LanguageOptions& language_options,
      const Type* output_type,
      std::vector<std::unique_ptr<AlgebraArg>> arguments,
      ResolvedFunctionCallBase::ErrorMode error_mode,
      CollatorList collator_list);

 private:
  CollatorList collator_list_;
};

// Implementation for ARRAY_(SUM|AVG)(ARRAY<T>) -> U
class ArraySumAvgFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;

  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

// Implementation for
//   ARRAY_OFFSET(ARRAY<T>, T [ , ARRAY_FIND_MODE ]) -> INT64.
//   ARRAY_FIND(ARRAY<T>, T [ , ARRAY_FIND_MODE ]) -> T.
//   ARRAY_OFFSETS(ARRAY<T>, T) -> ARRAY<INT64>.
//   ARRAY_FIND_ALL(ARRAY<T>, T) -> ARRAY<T>.
class ArrayFindFunctions : public SimpleBuiltinScalarFunction {
 public:
  ArrayFindFunctions(FunctionKind kind, const Type* output_type,
                     CollatorList collator_list,
                     const InlineLambdaExpr* lambda = nullptr)
      : SimpleBuiltinScalarFunction(kind, output_type),
        collator_list_(std::move(collator_list)),
        lambda_(lambda) {}

  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;

  static absl::StatusOr<std::unique_ptr<ScalarFunctionCallExpr>> CreateCall(
      FunctionKind kind, const LanguageOptions& language_options,
      const Type* output_type,
      std::vector<std::unique_ptr<AlgebraArg>> arguments,
      ResolvedFunctionCallBase::ErrorMode error_mode,
      CollatorList collator_list);

 private:
  CollatorList collator_list_;
  const InlineLambdaExpr* lambda_;
};

class IsFunction : public BuiltinScalarFunction {
 public:
  using BuiltinScalarFunction::BuiltinScalarFunction;
  bool Eval(absl::Span<const TupleData* const> params,
            absl::Span<const Value> args, EvaluationContext* context,
            Value* result, absl::Status* status) const override;
};

class CastFunction : public SimpleBuiltinScalarFunction {
 public:
  CastFunction(
      const Type* input_type, const Type* output_type,
      std::unique_ptr<ExtendedCompositeCastEvaluator> extended_cast_evaluator,
      const TypeParameters type_params)
      : SimpleBuiltinScalarFunction(FunctionKind::kCast, output_type),
        extended_cast_evaluator_(std::move(extended_cast_evaluator)),
        type_params_(std::move(type_params)),
        has_potential_nondeterminism_(
            HasPotentialNondeterminism(input_type, output_type)) {}
  CastFunction(FunctionKind kind, const Type* input_type,
               const Type* output_type)
      : SimpleBuiltinScalarFunction(kind, output_type),
        has_potential_nondeterminism_(
            HasPotentialNondeterminism(input_type, output_type)) {}

  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;

 private:
  // Returns true if the cast can be non-deterministic based on the input and
  // output types.
  static bool HasPotentialNondeterminism(const Type* input_type,
                                         const Type* output_type);

  std::unique_ptr<ExtendedCompositeCastEvaluator> extended_cast_evaluator_;
  const TypeParameters type_params_;

  // Cast non-determinism is a function of the input and output types. We
  // use this field to avoid re-calculating the value on every call to
  // `Eval()`.
  bool has_potential_nondeterminism_ = false;
};

class BitCastFunction : public BuiltinScalarFunction {
 public:
  using BuiltinScalarFunction::BuiltinScalarFunction;
  bool Eval(absl::Span<const TupleData* const> params,
            absl::Span<const Value> args, EvaluationContext* context,
            Value* result, absl::Status* status) const override;
};

class LikeFunction : public SimpleBuiltinScalarFunction {
 public:
  LikeFunction(FunctionKind kind, const Type* output_type,
               std::unique_ptr<RE2> regexp)
      : SimpleBuiltinScalarFunction(kind, output_type) {
    regexp_.push_back(std::move(regexp));
    has_collation_ = kind == FunctionKind::kLikeWithCollation;
  }
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;

  LikeFunction(const LikeFunction&) = delete;
  LikeFunction& operator=(const LikeFunction&) = delete;

 private:
  // Regexp precompiled at prepare time; null if cannot be precompiled.
  std::vector<std::unique_ptr<RE2>> regexp_;
  bool has_collation_;
};

// Invoked by expression such as:
//   <expr> [NOT] LIKE ANY|ALL (pattern1, pattern2, ...)
class LikeAnyAllFunction : public SimpleBuiltinScalarFunction {
 public:
  LikeAnyAllFunction(FunctionKind kind, const Type* output_type,
                     std::vector<std::unique_ptr<RE2>> regexp)
      : SimpleBuiltinScalarFunction(kind, output_type),
        regexp_(std::move(regexp)) {
    ABSL_CHECK(kind == FunctionKind::kLikeAny || kind == FunctionKind::kNotLikeAny ||
          kind == FunctionKind::kLikeAnyWithCollation ||
          kind == FunctionKind::kNotLikeAnyWithCollation ||
          kind == FunctionKind::kLikeAll || kind == FunctionKind::kNotLikeAll ||
          kind == FunctionKind::kLikeAllWithCollation ||
          kind == FunctionKind::kNotLikeAllWithCollation);
    has_collation_ = kind == FunctionKind::kLikeAnyWithCollation ||
                     kind == FunctionKind::kNotLikeAnyWithCollation ||
                     kind == FunctionKind::kLikeAllWithCollation ||
                     kind == FunctionKind::kNotLikeAllWithCollation;
    is_not_ = kind == FunctionKind::kNotLikeAny ||
              kind == FunctionKind::kNotLikeAll ||
              kind == FunctionKind::kNotLikeAllWithCollation ||
              kind == FunctionKind::kNotLikeAnyWithCollation;
  }

  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;

  LikeAnyAllFunction(const LikeAnyAllFunction&) = delete;
  LikeAnyAllFunction& operator=(const LikeAnyAllFunction&) = delete;

 private:
  std::vector<std::unique_ptr<RE2>> regexp_;
  bool has_collation_;
  bool is_not_;
};

// Invoked by expression such as:
//   <expr> [NOT] LIKE ANY|ALL UNNEST(<array-expression>)
class LikeAnyAllArrayFunction : public SimpleBuiltinScalarFunction {
 public:
  LikeAnyAllArrayFunction(FunctionKind kind, const Type* output_type,
                          std::vector<std::unique_ptr<RE2>> regexp)
      : SimpleBuiltinScalarFunction(kind, output_type),
        regexp_(std::move(regexp)) {
    ABSL_CHECK(kind == FunctionKind::kLikeAnyArray ||
          kind == FunctionKind::kLikeAnyArrayWithCollation ||
          kind == FunctionKind::kNotLikeAnyArray ||
          kind == FunctionKind::kNotLikeAnyArrayWithCollation ||
          kind == FunctionKind::kLikeAllArray ||
          kind == FunctionKind::kLikeAllArrayWithCollation ||
          kind == FunctionKind::kNotLikeAllArray ||
          kind == FunctionKind::kNotLikeAllArrayWithCollation);
    is_not_ = kind == FunctionKind::kNotLikeAnyArray ||
              kind == FunctionKind::kNotLikeAnyArrayWithCollation ||
              kind == FunctionKind::kNotLikeAllArray ||
              kind == FunctionKind::kNotLikeAllArrayWithCollation;
    has_collation_ = kind == FunctionKind::kLikeAnyArrayWithCollation ||
                     kind == FunctionKind::kLikeAllArrayWithCollation ||
                     kind == FunctionKind::kNotLikeAnyArrayWithCollation ||
                     kind == FunctionKind::kNotLikeAllArrayWithCollation;
  }

  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;

  LikeAnyAllArrayFunction(const LikeAnyAllArrayFunction&) = delete;
  LikeAnyAllArrayFunction& operator=(const LikeAnyAllArrayFunction&) = delete;

 private:
  std::vector<std::unique_ptr<RE2>> regexp_;
  bool is_not_;
  bool has_collation_;
};

class BitwiseFunction : public BuiltinScalarFunction {
 public:
  using BuiltinScalarFunction::BuiltinScalarFunction;
  bool Eval(absl::Span<const TupleData* const> params,
            absl::Span<const Value> args, EvaluationContext* context,
            Value* result, absl::Status* status) const override;
};

class BitCountFunction : public BuiltinScalarFunction {
 public:
  BitCountFunction()
      : BuiltinScalarFunction(FunctionKind::kBitCount, types::Int64Type()) {}
  bool Eval(absl::Span<const TupleData* const> params,
            absl::Span<const Value> args, EvaluationContext* context,
            Value* result, absl::Status* status) const override;
};

class ArrayElementFunction : public BuiltinScalarFunction {
 public:
  ArrayElementFunction(int base, bool safe, const Type* output_type)
      : BuiltinScalarFunction(
            safe ? (base == 0 ? FunctionKind::kSafeArrayAtOffset
                              : FunctionKind::kSafeArrayAtOrdinal)
                 : (base == 0 ? FunctionKind::kArrayAtOffset
                              : FunctionKind::kArrayAtOrdinal),
            output_type),
        base_(base),
        safe_(safe) {
    ABSL_CHECK(base_ == 0 || base_ == 1) << base_;
  }
  bool Eval(absl::Span<const TupleData* const> params,
            absl::Span<const Value> args, EvaluationContext* context,
            Value* result, absl::Status* status) const override;

 protected:
  // This function supports both 0 based offset and 1 based ordinals, the value
  // of base_ can be either 0 or 1.
  int base_;
  // Safe accesses will return NULL rather than raising an error on an out-of-
  // bounds position.
  const bool safe_;
};

class LeastFunction : public BuiltinScalarFunction {
 public:
  explicit LeastFunction(const Type* output_type)
      : BuiltinScalarFunction(FunctionKind::kLeast, output_type) {}
  bool Eval(absl::Span<const TupleData* const> params,
            absl::Span<const Value> args, EvaluationContext* context,
            Value* result, absl::Status* status) const override;
};

class GreatestFunction : public BuiltinScalarFunction {
 public:
  explicit GreatestFunction(const Type* output_type)
      : BuiltinScalarFunction(FunctionKind::kGreatest, output_type) {}
  bool Eval(absl::Span<const TupleData* const> params,
            absl::Span<const Value> args, EvaluationContext* context,
            Value* result, absl::Status* status) const override;
};

class ToCodePointsFunction : public SimpleBuiltinScalarFunction {
 public:
  ToCodePointsFunction()
      : SimpleBuiltinScalarFunction(FunctionKind::kToCodePoints,
                                    types::Int64ArrayType()) {}
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class CodePointsToFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class FormatFunction : public SimpleBuiltinScalarFunction {
 public:
  explicit FormatFunction(const Type* output_type)
      : SimpleBuiltinScalarFunction(FunctionKind::kFormat, output_type) {}
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class GenerateArrayFunction : public SimpleBuiltinScalarFunction {
 public:
  explicit GenerateArrayFunction(const Type* output_type)
      : SimpleBuiltinScalarFunction(FunctionKind::kGenerateArray, output_type) {
  }
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class RangeBucketFunction : public SimpleBuiltinScalarFunction {
 public:
  RangeBucketFunction()
      : SimpleBuiltinScalarFunction(FunctionKind::kRangeBucket,
                                    types::Int64Type()) {}
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class MathFunction : public BuiltinScalarFunction {
 public:
  using BuiltinScalarFunction::BuiltinScalarFunction;
  bool Eval(absl::Span<const TupleData* const> params,
            absl::Span<const Value> args, EvaluationContext* context,
            Value* result, absl::Status* status) const override;
};

class NetFunction : public BuiltinScalarFunction {
 public:
  using BuiltinScalarFunction::BuiltinScalarFunction;
  bool Eval(absl::Span<const TupleData* const> params,
            absl::Span<const Value> args, EvaluationContext* context,
            Value* result, absl::Status* status) const override;
};

class StringFunction : public BuiltinScalarFunction {
 public:
  using BuiltinScalarFunction::BuiltinScalarFunction;
  bool Eval(absl::Span<const TupleData* const> params,
            absl::Span<const Value> args, EvaluationContext* context,
            Value* result, absl::Status* status) const override;
};

class NumericFunction : public BuiltinScalarFunction {
 public:
  using BuiltinScalarFunction::BuiltinScalarFunction;
  bool Eval(absl::Span<const TupleData* const> params,
            absl::Span<const Value> args, EvaluationContext* context,
            Value* result, absl::Status* status) const override;
};

class CaseConverterFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class RegexpFunction : public SimpleBuiltinScalarFunction {
 public:
  // regexp precompiled at prepare time; null if cannot be precompiled.
  RegexpFunction(std::unique_ptr<const functions::RegExp> const_regexp,
                 FunctionKind kind, const Type* output_type)
      : SimpleBuiltinScalarFunction(kind, output_type),
        const_regexp_(std::move(const_regexp)) {}

  RegexpFunction(const RegexpFunction&) = delete;
  RegexpFunction& operator=(const RegexpFunction&) = delete;

  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;

 private:
  // Regexp precompiled at prepare time; null if cannot be precompiled.
  const std::unique_ptr<const functions::RegExp> const_regexp_;
};

class SplitFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class ConcatFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

// The field descriptors passed to the constructor must outlive the function
// object. The output ProtoType has a pointer to the google::protobuf::Descriptor that
// owns the field descriptors, i.e., their life span is tied to that of
// 'output_type'.
class MakeProtoFunction : public SimpleBuiltinScalarFunction {
 public:
  typedef std::pair<const google::protobuf::FieldDescriptor*, FieldFormat::Format>
      FieldAndFormat;

  MakeProtoFunction(const ProtoType* output_type,
                    const std::vector<FieldAndFormat>& fields)
      : SimpleBuiltinScalarFunction(FunctionKind::kMakeProto, output_type),
        fields_(fields) {}
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;

 private:
  std::vector<FieldAndFormat> fields_;  // Not owned.
};

// This class is used to evaluate the FILTER_FIELDS() SQL function. The resolved
// argument list contains only the root proto to be modified, the field paths of
// the root object must be added before evaluation.
class FilterFieldsFunction : public SimpleBuiltinScalarFunction {
 public:
  FilterFieldsFunction(const Type* output_type,
                       bool reset_cleared_required_fields);

  ~FilterFieldsFunction() override;

  // Add a field path that denotes the path to a proto field, `include` denotes
  // whether the proto field is include or exclude.
  absl::Status AddFieldPath(
      bool include,
      const std::vector<const google::protobuf::FieldDescriptor*>& field_path);

  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;

 private:
  struct FieldPathTrieNode;
  using TagToNodeMap =
      absl::flat_hash_map<int, std::unique_ptr<FieldPathTrieNode>>;

  // Recursively prune on a node. It is guaranteed that this node has children.
  absl::Status RecursivelyPrune(const FieldPathTrieNode* node,
                                google::protobuf::Message* message) const;

  // Prune on an excluded message, may prune recursively on child nodes.
  absl::Status HandleExcludedMessage(const TagToNodeMap& child_nodes,
                                     google::protobuf::Message* message) const;

  // Prune on an included message, may prune recursively on child nodes.
  absl::Status HandleIncludedMessage(const TagToNodeMap& child_nodes,
                                     google::protobuf::Message* message) const;

  // Prune recursively on a message field.
  absl::Status PruneOnMessageField(
      const google::protobuf::Reflection& reflection, const FieldPathTrieNode* child,
      const google::protobuf::FieldDescriptor* field_descriptor,
      google::protobuf::Message* message) const;

  std::unique_ptr<FieldPathTrieNode> root_node_;
  const bool reset_cleared_required_fields_;
};

// This class is used to evaluate the REPLACE_FIELDS() SQL function, given
// resolved arguments. The field paths to be modified in the root object must be
// passed in the constructor. The resolved arguments list to evaluate should
// consist of the root object and the new field values (in the order
// corresponding to the initialized field paths).
class ReplaceFieldsFunction : public SimpleBuiltinScalarFunction {
 public:
  //  A pair of paths that together represent a single field path of a Struct or
  //  Proto type.
  //  If only 'struct_index_path' is non-empty, then the field path only
  //  references top-level and nested struct fields.
  //
  //  If only 'field_descriptor_path' is non-empty, then the field path only
  //  references top-level and nested message fields.
  //
  //  If both path vectors are non-empty, the field path should be expanded
  //  starting with the 'struct_index_path'. The Struct field corresponding to
  //  the last index in 'struct_index_path' will be the proto from which the
  //  first field in 'field_descriptor_path' is looked up with regards to.
  struct StructAndProtoPath {
    StructAndProtoPath(
        std::vector<int> input_struct_index_path,
        std::vector<const google::protobuf::FieldDescriptor*> input_field_descriptor_path)
        : struct_index_path(input_struct_index_path),
          field_descriptor_path(input_field_descriptor_path) {}

    // A vector of indexes (0-based) that denotes the path to a struct field
    // that will be modified.
    std::vector<int> struct_index_path;

    // A vector of FieldDescriptors that denotes the path to a proto field that
    // will be modified
    std::vector<const google::protobuf::FieldDescriptor*> field_descriptor_path;
  };

  ReplaceFieldsFunction(const Type* output_type,
                        const std::vector<StructAndProtoPath>& field_paths)
      : SimpleBuiltinScalarFunction(FunctionKind::kReplaceFields, output_type),
        field_paths_(field_paths) {}

  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;

 private:
  const std::vector<StructAndProtoPath> field_paths_;
};

class NullaryFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class DateTimeUnaryFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class FormatDateDatetimeTimestampFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class FormatTimeFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class TimestampFromIntFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class IntFromTimestampFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class StringConversionFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class FromProtoFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class ToProtoFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class EnumValueDescriptorProtoFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class ParseDateFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class ParseDatetimeFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class ParseTimeFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class ParseTimestampFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class DateTimeDiffFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class DateTimeTruncFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class LastDayFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class NextDayFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class AddMonthsFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class MonthsBetweenFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class ExtractFromFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class TimestampConversionFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class CivilTimeConstructionAndConversionFunction
    : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class ExtractDateFromFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class ExtractTimeFromFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class ExtractDatetimeFromFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class IntervalFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class DateTimeBucketFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class CollateFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

// Evaluates an EXTRACT(ONEOF_CASE(one_of) from proto) expression. The input
// <args> vector will contain a proto value that has a Oneof described by
// <oneof_desc_>.
class ExtractOneofCaseFunction : public SimpleBuiltinScalarFunction {
 public:
  ExtractOneofCaseFunction(const Type* output_type,
                           const google::protobuf::OneofDescriptor* oneof_desc)
      : SimpleBuiltinScalarFunction(FunctionKind::kExtractOneofCase,
                                    output_type),
        oneof_desc_(oneof_desc) {}

  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;

 private:
  // Describes the Oneof for which the set field name will be returned.
  const google::protobuf::OneofDescriptor* oneof_desc_;
};

class RandFunction : public SimpleBuiltinScalarFunction {
 public:
  RandFunction()
      : SimpleBuiltinScalarFunction(FunctionKind::kRand, types::DoubleType()) {}
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class ErrorFunction : public SimpleBuiltinScalarFunction {
 public:
  explicit ErrorFunction(const Type* output_type)
      : SimpleBuiltinScalarFunction(FunctionKind::kError, output_type) {}
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

// Returns the ordinal (1-based) rank of each row. All rows in the same ordering
// group are peers and receive the same rank value, and the subsequent rank
// value is incremented by 1.
class DenseRankFunction : public BuiltinAnalyticFunction {
 public:
  DenseRankFunction()
      : BuiltinAnalyticFunction(FunctionKind::kDenseRank, types::Int64Type()) {}

  bool RequireTupleComparator() const override { return true; }

  absl::Status Eval(const TupleSchema& schema,
                    const absl::Span<const TupleData* const>& tuples,
                    const absl::Span<const std::vector<Value>>& args,
                    const absl::Span<const AnalyticWindow>& windows,
                    const TupleComparator* comparator,
                    ResolvedFunctionCallBase::ErrorMode error_mode,
                    EvaluationContext* context,
                    std::vector<Value>* result) const override;
};

// Returns the ordinal (1-based) rank of each row. All rows in the same ordering
// group are peers and receive the same rank value, and the subsequent rank
// value is offset by the number of peers.
class RankFunction : public BuiltinAnalyticFunction {
 public:
  RankFunction()
      : BuiltinAnalyticFunction(FunctionKind::kRank, types::Int64Type()) {}

  bool RequireTupleComparator() const override { return true; }

  absl::Status Eval(const TupleSchema& schema,
                    const absl::Span<const TupleData* const>& tuples,
                    const absl::Span<const std::vector<Value>>& args,
                    const absl::Span<const AnalyticWindow>& windows,
                    const TupleComparator* comparator,
                    ResolvedFunctionCallBase::ErrorMode error_mode,
                    EvaluationContext* context,
                    std::vector<Value>* result) const override;
};

// Returns the sequential row ordinal (1-based) of each row.
class RowNumberFunction : public BuiltinAnalyticFunction {
 public:
  RowNumberFunction()
      : BuiltinAnalyticFunction(FunctionKind::kRowNumber, types::Int64Type()) {}

  // The comparator is used only for nondeterminism detection.
  bool RequireTupleComparator() const override { return true; }

  absl::Status Eval(const TupleSchema& schema,
                    const absl::Span<const TupleData* const>& tuples,
                    const absl::Span<const std::vector<Value>>& args,
                    const absl::Span<const AnalyticWindow>& windows,
                    const TupleComparator* comparator,
                    ResolvedFunctionCallBase::ErrorMode error_mode,
                    EvaluationContext* context,
                    std::vector<Value>* result) const override;
};

// Return the percentile rank of a row defined as (RK-1)/(NR-1), where RK is
// the RANK of the row and NR is the number of rows in the partition.
// If NR=1, returns 0.
class PercentRankFunction : public BuiltinAnalyticFunction {
 public:
  PercentRankFunction()
      : BuiltinAnalyticFunction(FunctionKind::kPercentRank,
                                types::DoubleType()) {}

  bool RequireTupleComparator() const override { return true; }

  absl::Status Eval(const TupleSchema& schema,
                    const absl::Span<const TupleData* const>& tuples,
                    const absl::Span<const std::vector<Value>>& args,
                    const absl::Span<const AnalyticWindow>& windows,
                    const TupleComparator* comparator,
                    ResolvedFunctionCallBase::ErrorMode error_mode,
                    EvaluationContext* context,
                    std::vector<Value>* result) const override;
};

// Returns the relative rank of a row defined as NP/NR, where NP is defined to
// be the number of rows preceding or peer with the current row in the window
// ordering of the partition and NR is the number of rows in the partition.
class CumeDistFunction : public BuiltinAnalyticFunction {
 public:
  CumeDistFunction()
      : BuiltinAnalyticFunction(FunctionKind::kCumeDist, types::DoubleType()) {}

  bool RequireTupleComparator() const override { return true; }

  absl::Status Eval(const TupleSchema& schema,
                    const absl::Span<const TupleData* const>& tuples,
                    const absl::Span<const std::vector<Value>>& args,
                    const absl::Span<const AnalyticWindow>& windows,
                    const TupleComparator* comparator,
                    ResolvedFunctionCallBase::ErrorMode error_mode,
                    EvaluationContext* context,
                    std::vector<Value>* result) const override;
};

// NTILE(<constant integer expression>)
// Divides the rows into  <constant integer expression> buckets based on row
// ordering and returns the 1-based bucket number that is assigned to each row.
// The number of rows in the buckets can differ by at most 1. The remainder
// values (the remainder of number of rows divided by buckets) are distributed
// one for each bucket, starting with bucket 1.
class NtileFunction : public BuiltinAnalyticFunction {
 public:
  NtileFunction()
      : BuiltinAnalyticFunction(FunctionKind::kNtile, types::Int64Type()) {}

  bool RequireTupleComparator() const override { return true; }

  absl::Status Eval(const TupleSchema& schema,
                    const absl::Span<const TupleData* const>& tuples,
                    const absl::Span<const std::vector<Value>>& args,
                    const absl::Span<const AnalyticWindow>& windows,
                    const TupleComparator* comparator,
                    ResolvedFunctionCallBase::ErrorMode error_mode,
                    EvaluationContext* context,
                    std::vector<Value>* result) const override;

 private:
  // Returns true if tuples in 'tuples' that are peers with the tuple at
  // 'key_tuple_id' via 'comparator' are not equal.
  //
  // Requires that 'tuples' are ordered by 'comparator'. 'key_tuple_id' is
  // an index for 'tuples'.
  static bool OrderingPeersAreNotEqual(
      const TupleSchema& schema, int key_tuple_id,
      absl::Span<const TupleData* const> tuples,
      const TupleComparator& comparator);
};

// IS_FIRST(<value expression>)
// Returns a boolean indicating whether the current row is in the first N rows
// in the partition, where N is the value of the argument, according to the
// window ordering.
class IsFirstFunction : public BuiltinAnalyticFunction {
 public:
  IsFirstFunction()
      : BuiltinAnalyticFunction(FunctionKind::kIsFirst, types::BoolType()) {}

  bool RequireTupleComparator() const override { return true; }

  absl::Status Eval(const TupleSchema& schema,
                    const absl::Span<const TupleData* const>& tuples,
                    const absl::Span<const std::vector<Value>>& args,
                    const absl::Span<const AnalyticWindow>& windows,
                    const TupleComparator* comparator,
                    ResolvedFunctionCallBase::ErrorMode error_mode,
                    EvaluationContext* context,
                    std::vector<Value>* result) const override;
};

// IS_LAST(<value expression>)
// Returns a boolean indicating whether the current row is in the last N rows
// in the partition, where N is the value of the argument, according to the
// window ordering.
class IsLastFunction : public BuiltinAnalyticFunction {
 public:
  IsLastFunction()
      : BuiltinAnalyticFunction(FunctionKind::kIsLast, types::BoolType()) {}

  bool RequireTupleComparator() const override { return true; }

  absl::Status Eval(const TupleSchema& schema,
                    const absl::Span<const TupleData* const>& tuples,
                    const absl::Span<const std::vector<Value>>& args,
                    const absl::Span<const AnalyticWindow>& windows,
                    const TupleComparator* comparator,
                    ResolvedFunctionCallBase::ErrorMode error_mode,
                    EvaluationContext* context,
                    std::vector<Value>* result) const override;
};

// FIRST_VALUE(<value expression>)
// Returns the value of the <value expression> for the first row in the
// window frame.
class FirstValueFunction : public BuiltinAnalyticFunction {
 public:
  explicit FirstValueFunction(
      const Type* output_type,
      ResolvedAnalyticFunctionCall::NullHandlingModifier null_handling_modifier)
      : BuiltinAnalyticFunction(FunctionKind::kFirstValue, output_type),
        ignore_nulls_(null_handling_modifier ==
                      ResolvedAnalyticFunctionCall::IGNORE_NULLS) {}

  bool RequireTupleComparator() const override { return true; }

  absl::Status Eval(const TupleSchema& schema,
                    const absl::Span<const TupleData* const>& tuples,
                    const absl::Span<const std::vector<Value>>& args,
                    const absl::Span<const AnalyticWindow>& windows,
                    const TupleComparator* comparator,
                    ResolvedFunctionCallBase::ErrorMode error_mode,
                    EvaluationContext* context,
                    std::vector<Value>* result) const override;

 private:
  const bool ignore_nulls_;
};

// LAST_VALUE(<value expression>)
// Returns the value of the <value expression> for the last row in the
// window frame.
class LastValueFunction : public BuiltinAnalyticFunction {
 public:
  explicit LastValueFunction(
      const Type* output_type,
      ResolvedAnalyticFunctionCall::NullHandlingModifier null_handling_modifier)
      : BuiltinAnalyticFunction(FunctionKind::kLastValue, output_type),
        ignore_nulls_(null_handling_modifier ==
                      ResolvedAnalyticFunctionCall::IGNORE_NULLS) {}

  bool RequireTupleComparator() const override { return true; }

  absl::Status Eval(const TupleSchema& schema,
                    const absl::Span<const TupleData* const>& tuples,
                    const absl::Span<const std::vector<Value>>& args,
                    const absl::Span<const AnalyticWindow>& windows,
                    const TupleComparator* comparator,
                    ResolvedFunctionCallBase::ErrorMode error_mode,
                    EvaluationContext* context,
                    std::vector<Value>* result) const override;

 private:
  const bool ignore_nulls_;
};

// NTH_VALUE(<value expression>, <constant integer expression>)
// Returns the value of <value expression> at the Nth row of the window frame,
// where Nth is defined by the <constant integer expression>.
class NthValueFunction : public BuiltinAnalyticFunction {
 public:
  explicit NthValueFunction(
      const Type* output_type,
      ResolvedAnalyticFunctionCall::NullHandlingModifier null_handling_modifier)
      : BuiltinAnalyticFunction(FunctionKind::kNthValue, output_type),
        ignore_nulls_(null_handling_modifier ==
                      ResolvedAnalyticFunctionCall::IGNORE_NULLS) {}

  bool RequireTupleComparator() const override { return true; }

  absl::Status Eval(const TupleSchema& schema,
                    const absl::Span<const TupleData* const>& tuples,
                    const absl::Span<const std::vector<Value>>& args,
                    const absl::Span<const AnalyticWindow>& windows,
                    const TupleComparator* comparator,
                    ResolvedFunctionCallBase::ErrorMode error_mode,
                    EvaluationContext* context,
                    std::vector<Value>* result) const override;

 private:
  const bool ignore_nulls_;
};

// LEAD(<value expression>, <offset>, <default expression>)
// Returns the value of the <value expression> on a row that is <offset> number
// of rows after the current row r. The value of <default expression> is
// returned as the result if there is no row corresponding to the <offset>
// number of rows after the current row.  At <offset> 0, <value expression> is
// computed on the current row. If <offset> is null or negative, an error is
// produced.
class LeadFunction : public BuiltinAnalyticFunction {
 public:
  explicit LeadFunction(const Type* output_type)
      : BuiltinAnalyticFunction(FunctionKind::kLead, output_type) {}

  bool RequireTupleComparator() const override { return true; }

  absl::Status Eval(const TupleSchema& schema,
                    const absl::Span<const TupleData* const>& tuples,
                    const absl::Span<const std::vector<Value>>& args,
                    const absl::Span<const AnalyticWindow>& windows,
                    const TupleComparator* comparator,
                    ResolvedFunctionCallBase::ErrorMode error_mode,
                    EvaluationContext* context,
                    std::vector<Value>* result) const override;
};

// LAG(<value expression>, <offset>, <default expression>)
// Returns the value of the <value expression> on a row that is <offset> number
// of rows before the current row r. The value of <default expression> is
// returned as the result if there is no row corresponding to the <offset>
// number of rows before the current row.  At <offset> 0, <value expression> is
// computed on the current row. If <offset> is null or negative, an error is
// produced.
class LagFunction : public BuiltinAnalyticFunction {
 public:
  explicit LagFunction(const Type* output_type)
      : BuiltinAnalyticFunction(FunctionKind::kLead, output_type) {}

  bool RequireTupleComparator() const override { return true; }

  absl::Status Eval(const TupleSchema& schema,
                    const absl::Span<const TupleData* const>& tuples,
                    const absl::Span<const std::vector<Value>>& args,
                    const absl::Span<const AnalyticWindow>& windows,
                    const TupleComparator* comparator,
                    ResolvedFunctionCallBase::ErrorMode error_mode,
                    EvaluationContext* context,
                    std::vector<Value>* result) const override;
};

// PERCENTILE_CONT(<value expression>, <percentile>)
// Returns the percentile from <value expression> at given <percentile>, with
// possible linear interpolation. When <percentile> is 0, returns the min value;
// when <percentile> is 1, returns the max value; when <percentile> is 0.5,
// returns the median.
class PercentileContFunction : public BuiltinAnalyticFunction {
 public:
  PercentileContFunction(
      const Type* output_type,
      ResolvedAnalyticFunctionCall::NullHandlingModifier null_handling_modifier)
      : BuiltinAnalyticFunction(FunctionKind::kPercentileCont, output_type),
        ignore_nulls_(null_handling_modifier !=
                      ResolvedAnalyticFunctionCall::RESPECT_NULLS) {}

  bool RequireTupleComparator() const override { return false; }

  absl::Status Eval(const TupleSchema& schema,
                    const absl::Span<const TupleData* const>& tuples,
                    const absl::Span<const std::vector<Value>>& args,
                    const absl::Span<const AnalyticWindow>& windows,
                    const TupleComparator* comparator,
                    ResolvedFunctionCallBase::ErrorMode error_mode,
                    EvaluationContext* context,
                    std::vector<Value>* result) const override;

 private:
  bool ignore_nulls_;
};

// PERCENTILE_DISC(<value expression>, <percentile>)
// Returns the discrete percentile from <value expression>.
class PercentileDiscFunction : public BuiltinAnalyticFunction {
 public:
  PercentileDiscFunction(
      const Type* output_type,
      ResolvedAnalyticFunctionCall::NullHandlingModifier null_handling_modifier,
      std::unique_ptr<const GoogleSqlCollator> collator)
      : BuiltinAnalyticFunction(FunctionKind::kPercentileDisc, output_type),
        ignore_nulls_(null_handling_modifier !=
                      ResolvedAnalyticFunctionCall::RESPECT_NULLS),
        collator_(std::move(collator)) {}

  bool RequireTupleComparator() const override { return false; }

  absl::Status Eval(const TupleSchema& schema,
                    const absl::Span<const TupleData* const>& tuples,
                    const absl::Span<const std::vector<Value>>& args,
                    const absl::Span<const AnalyticWindow>& windows,
                    const TupleComparator* comparator,
                    ResolvedFunctionCallBase::ErrorMode error_mode,
                    EvaluationContext* context,
                    std::vector<Value>* result) const override;

 private:
  bool ignore_nulls_;
  std::unique_ptr<const GoogleSqlCollator> collator_;
};

class CosineDistanceFunctionDense : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class ApproxCosineDistanceFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class CosineDistanceFunctionSparseInt64Key
    : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class CosineDistanceFunctionSparseStringKey
    : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class EuclideanDistanceFunctionDense : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class EuclideanDistanceFunctionSparseInt64Key
    : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class EuclideanDistanceFunctionSparseStringKey
    : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class ApproxEuclideanDistanceFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class DotProductFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class ApproxDotProductFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class ManhattanDistanceFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class L1NormFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class L2NormFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class EditDistanceFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

// Evaluates the function calls to all the signatures of `ARRAY_ZIP`.
class ArrayZipFunction : public SimpleBuiltinScalarFunction {
 public:
  ArrayZipFunction(FunctionKind kind, const Type* output_type,
                   const InlineLambdaExpr* lambda)
      : SimpleBuiltinScalarFunction(kind, output_type), lambda_(lambda) {}

  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;

 private:
  // Groups the input arguments according to their types.
  struct CategorizedArguments {
    absl::Span<const Value> arrays;
    functions::ArrayZipEnums::ArrayZipMode array_zip_mode;
  };

  // Validates the input `args` and convert it to `CategorizedArguments` format
  // for easier processing.
  absl::StatusOr<CategorizedArguments> GetCategorizedArguments(
      absl::Span<const Value> args) const;

  // Returns the result array length based on the input `arrays` and the
  // `array_zip_mode`. If `array_zip_mode` == STRICT and the input arrays have
  // different lengths, a SQL error will be returned.
  absl::StatusOr<int> GetZippedArrayLength(
      absl::Span<const Value> arrays,
      functions::ArrayZipEnums::ArrayZipMode array_zip_mode) const;

  // Evaluates the function calls to the signatures without lambda arguments.
  // `zipped_array_length` is the length of the result array.
  absl::StatusOr<Value> EvalNoLambda(const CategorizedArguments& args,
                                     int zipped_array_length) const;

  // Evaluates the function calls to the signatures with lambda arguments.
  // `zipped_array_length` is the length of the result array.
  absl::StatusOr<Value> EvalLambda(
      const CategorizedArguments& args, int zipped_array_length,
      LambdaEvaluationContext& lambda_context) const;

  // Returns a struct value of the given `struct_type`, whose i-th field value
  // equals to the element at `element_index` of the i-th `arrays`, or NULL if
  // the i-th array does not have an element at `element_index`.
  absl::StatusOr<Value> ToStructValue(const StructType* struct_type,
                                      absl::Span<const Value> arrays,
                                      int element_index) const;

  // Returns the evaluation result of `lambda_` under the given
  // `lambda_context`. The i-th input argument to `lambda_` is
  // - the element of `arrays[i]` at `element_index`,
  // - or NULL if `element_index` is out of boundary for `arrays[i]`.
  absl::StatusOr<Value> ToLambdaReturnValue(
      absl::Span<const Value> arrays, int element_index,
      LambdaEvaluationContext& lambda_context) const;

  // Returns the maximum length of the arrays in `arrays`.
  int MaxArrayLength(absl::Span<const Value> arrays) const;

  // Returns the minimum length of the arrays in `arrays`.
  int MinArrayLength(absl::Span<const Value> arrays) const;

  // Returns true if all the arrays in `arrays` have the same length.
  bool EqualArrayLength(absl::Span<const Value> arrays) const;

  const InlineLambdaExpr* lambda_;
};

class ApplyFunction : public SimpleBuiltinScalarFunction {
 public:
  ApplyFunction(FunctionKind kind, const Type* output_type,
                const InlineLambdaExpr* lambda)
      : SimpleBuiltinScalarFunction(kind, output_type), lambda_(lambda) {}

  absl::StatusOr<Value> Eval(
      absl::Span<const TupleData* const> params, absl::Span<const Value> args,
      EvaluationContext* evaluation_context) const override;

 private:
  const InlineLambdaExpr* lambda_;
};

class TumbleTVF : public BuiltinTableValuedFunction {
 public:
  explicit TumbleTVF(FunctionKind kind) : BuiltinTableValuedFunction(kind) {}

  absl::StatusOr<std::unique_ptr<EvaluatorTableIterator>> CreateEvaluator(
      std::vector<TableValuedFunction::TvfEvaluatorArg> args,
      std::shared_ptr<FunctionSignature> function_call_signature,
      EvaluationContext* context) override;

  std::string debug_name() const override { return "TumbleTVF"; }

 private:
  class TumbleResultIterator : public EvaluatorTableIterator {
   public:
    explicit TumbleResultIterator(
        std::unique_ptr<EvaluatorTableIterator> input_iterator,
        std::vector<TVFSchemaColumn> output_columns,
        std::vector<bool> included_columns, int timestamp_column_index,
        googlesql::IntervalValue window_size, googlesql::PicoTime origin)
        : input_(std::move(input_iterator)),
          output_columns_(std::move(output_columns)),
          included_columns_(std::move(included_columns)),
          timestamp_column_index_(timestamp_column_index),
          window_size_(std::move(window_size)),
          origin_(origin),
          current_output_values_(output_columns_.size()) {}

    int NumColumns() const override {
      return static_cast<int>(output_columns_.size());
    }

    std::string GetColumnName(int i) const override {
      return output_columns_[i].name;
    }

    const Type* GetColumnType(int i) const override {
      return output_columns_[i].type;
    }

    const Value& GetValue(int i) const override {
      return current_output_values_[i];
    }

    bool NextRow() override;

    absl::Status Status() const override { return status_; }
    absl::Status Cancel() override { return input_->Cancel(); }
    void SetDeadline(absl::Time deadline) override {
      input_->SetDeadline(deadline);
    }

   private:
    std::unique_ptr<EvaluatorTableIterator> input_;
    std::vector<TVFSchemaColumn> output_columns_;
    std::vector<bool> included_columns_;
    int timestamp_column_index_;
    googlesql::IntervalValue window_size_;
    googlesql::PicoTime origin_;
    std::vector<Value> current_output_values_;
    absl::Status status_;
  };
};

class HopTVF : public BuiltinTableValuedFunction {
 public:
  explicit HopTVF(FunctionKind kind) : BuiltinTableValuedFunction(kind) {}

  absl::StatusOr<std::unique_ptr<EvaluatorTableIterator>> CreateEvaluator(
      std::vector<TableValuedFunction::TvfEvaluatorArg> args,
      std::shared_ptr<FunctionSignature> function_call_signature,
      EvaluationContext* context) override;

  std::string debug_name() const override { return "HopTVF"; }

 private:
  class HopResultIterator : public EvaluatorTableIterator {
   public:
    explicit HopResultIterator(
        std::unique_ptr<EvaluatorTableIterator> input_iterator,
        std::vector<TVFSchemaColumn> output_columns,
        std::vector<bool> included_columns, int timestamp_column_index,
        googlesql::IntervalValue window_size,
        googlesql::IntervalValue step_size, googlesql::PicoTime origin)
        : input_(std::move(input_iterator)),
          output_columns_(std::move(output_columns)),
          included_columns_(std::move(included_columns)),
          timestamp_column_index_(timestamp_column_index),
          window_size_(std::move(window_size)),
          step_size_(std::move(step_size)),
          origin_(origin),
          current_output_values_(output_columns_.size()) {}

    int NumColumns() const override {
      return static_cast<int>(output_columns_.size());
    }

    std::string GetColumnName(int i) const override {
      return output_columns_[i].name;
    }

    const Type* GetColumnType(int i) const override {
      return output_columns_[i].type;
    }

    const Value& GetValue(int i) const override {
      return current_output_values_[i];
    }

    bool NextRow() override;

    absl::Status Status() const override { return status_; }
    absl::Status Cancel() override { return input_->Cancel(); }
    void SetDeadline(absl::Time deadline) override {
      input_->SetDeadline(deadline);
    }

   private:
    bool PopulateCurrentOutputFromPending();

    std::unique_ptr<EvaluatorTableIterator> input_;
    std::vector<TVFSchemaColumn> output_columns_;
    std::vector<bool> included_columns_;
    int timestamp_column_index_;
    googlesql::IntervalValue window_size_;
    googlesql::IntervalValue step_size_;
    googlesql::PicoTime origin_;
    // Queue of {start, end} for windows overlapping one input row.
    std::queue<std::pair<googlesql::PicoTime, googlesql::PicoTime>>
        pending_windows_;
    std::vector<Value> current_output_values_;
    absl::Status status_;
  };
};

// This method is used only for setting non-deterministic output.
// This method does not detect floating point types within STRUCTs or PROTOs,
// which would be too expensive to call for each row.
// Geography type internally contains floating point data, and thus treated
// the same way for this purpose.
bool HasFloatingPoint(const Type* type);

// Sets the provided EvaluationContext to have non-deterministic output if the
// given array has more than one element and is not order-preserving.
void MaybeSetNonDeterministicArrayOutput(const Value& array,
                                         EvaluationContext* context);

// Helper to validate that time related values that are arguments to certain
// functions don't have significant precision beyond micros when the nanos
// feature is not turned on.
absl::Status ValidateMicrosPrecision(const Value& value,
                                     EvaluationContext* context);

// Helper to create an absl::Status with an error indicating that a value
// reached the size limit.
absl::Status MakeMaxArrayValueByteSizeExceededError(
    int64_t max_value_byte_size, const googlesql_base::SourceLocation& source_loc);

// Returns TimestampScale to use based on language options.
// `support_picos` should be set to true when the caller operates on types that
// support Picosecond precision, such as TIMESTAMP, and should be set to false
// for other types such as DATETIME, TIME, INTERVAL, etc.
functions::TimestampScale GetTimestampScale(const LanguageOptions& options,
                                            bool support_picos = false);

template <typename OutType, typename InType1, typename InType2>
struct BinaryExecutor {
  typedef bool (*ptr)(InType1, InType2, OutType*, absl::Status* error);
};

template <typename OutType, typename InType1 = OutType,
          typename InType2 = OutType>
bool InvokeBinary(
    typename BinaryExecutor<OutType, InType1, InType2>::ptr function,
    absl::Span<const Value> args, Value* result, absl::Status* status);

}  // namespace googlesql

#endif  // GOOGLESQL_REFERENCE_IMPL_FUNCTION_H_
