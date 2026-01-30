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

#include "googlesql/reference_impl/functions/string_with_collation.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "googlesql/public/collator.h"
#include "googlesql/public/functions/string_with_collation.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/reference_impl/evaluation.h"
#include "googlesql/reference_impl/function.h"
#include "googlesql/reference_impl/tuple.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_builder.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace {

// Function used to switch on a (function kind, output type, args.size())
// triple.
static constexpr uint64_t FCT_TYPE_ARITY(FunctionKind function_kind,
                                         TypeKind type_kind, size_t arity) {
  return (static_cast<uint64_t>(function_kind) << 32) + (type_kind << 16) +
         arity;
};

class StringWithCollationFunction : public BuiltinScalarFunction {
 public:
  using BuiltinScalarFunction::BuiltinScalarFunction;
  bool Eval(absl::Span<const TupleData* const> params,
            absl::Span<const Value> args, EvaluationContext* context,
            Value* result, absl::Status* status) const override;
};

class SplitWithCollationFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

class CollationKeyFunction : public SimpleBuiltinScalarFunction {
 public:
  using SimpleBuiltinScalarFunction::SimpleBuiltinScalarFunction;
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

template <typename OutType, typename FunctionType, class... Args>
bool EvaluateFunctionWithCollation(FunctionType function,
                                   const GoogleSqlCollator& collator,
                                   Args&&... args, OutType* out,
                                   absl::Status* status) {
  if constexpr (std::is_invocable_v<decltype(function),
                                    const GoogleSqlCollator&, Args..., OutType*,
                                    absl::Status*>) {
    return function(collator, args..., out, status);
  } else {
    *status = function(collator, args..., out);
    return status->ok();
  }
}

template <typename OutType, typename FunctionType, class... Args>
bool InvokeWithCollation(FunctionType function, Value* result,
                         absl::Status* status, absl::string_view collation_name,
                         Args... args) {
  absl::StatusOr<std::unique_ptr<const GoogleSqlCollator>> collator_or_status =
      MakeSqlCollator(collation_name);
  if (!collator_or_status.ok()) {
    *status = collator_or_status.status();
    return false;
  }
  OutType out;
  if (!EvaluateFunctionWithCollation<OutType, FunctionType, Args...>(
          function, *(collator_or_status.value()), std::forward<Args>(args)...,
          &out, status)) {
    return false;
  }

  *result = Value::Make<OutType>(out);
  return true;
}

template <typename FunctionType, class... Args>
bool InvokeStringWithCollation(FunctionType function, Value* result,
                               absl::Status* status,
                               absl::string_view collation_name, Args... args) {
  absl::StatusOr<std::unique_ptr<const GoogleSqlCollator>> collator_or_status =
      MakeSqlCollator(collation_name);
  if (!collator_or_status.ok()) {
    *status = collator_or_status.status();
    return false;
  }
  std::string out;
  if (!EvaluateFunctionWithCollation<std::string, FunctionType, Args...>(
          function, *(collator_or_status.value()), std::forward<Args>(args)...,
          &out, status)) {
    return false;
  }
  *result = Value::String(out);
  return true;
}

bool StringWithCollationFunction::Eval(
    absl::Span<const TupleData* const> params, absl::Span<const Value> args,
    EvaluationContext* context, Value* result, absl::Status* status) const {
  if (HasNulls(args)) {
    *result = Value::Null(output_type());
    return true;
  }
  switch (FCT_TYPE_ARITY(kind(), args[0].type_kind(), args.size())) {
    case FCT_TYPE_ARITY(FunctionKind::kStrposWithCollation, TYPE_STRING, 3):
      return InvokeWithCollation<int64_t>(
          &functions::StrPosOccurrenceUtf8WithCollation, result, status,
          args[0].string_value(), args[1].string_value(),
          args[2].string_value(),
          /*pos=*/1, /*occurrence=*/1);
    case FCT_TYPE_ARITY(FunctionKind::kInstrWithCollation, TYPE_STRING, 3):
      return InvokeWithCollation<int64_t>(
          &functions::StrPosOccurrenceUtf8WithCollation, result, status,
          args[0].string_value(), args[1].string_value(),
          args[2].string_value(),
          /*pos=*/1, /*occurrence=*/1);
    case FCT_TYPE_ARITY(FunctionKind::kInstrWithCollation, TYPE_STRING, 4):
      return InvokeWithCollation<int64_t>(
          &functions::StrPosOccurrenceUtf8WithCollation, result, status,
          args[0].string_value(), args[1].string_value(),
          args[2].string_value(),
          /*pos=*/args[3].int64_value(), /*occurrence=*/1);
    case FCT_TYPE_ARITY(FunctionKind::kInstrWithCollation, TYPE_STRING, 5):
      return InvokeWithCollation<int64_t>(
          &functions::StrPosOccurrenceUtf8WithCollation, result, status,
          args[0].string_value(), args[1].string_value(),
          args[2].string_value(),
          /*pos=*/args[3].int64_value(), /*occurrence=*/args[4].int64_value());
    case FCT_TYPE_ARITY(FunctionKind::kStartsWithWithCollation, TYPE_STRING, 3):
      return InvokeWithCollation<bool>(&functions::StartsWithUtf8WithCollation,
                                       result, status, args[0].string_value(),
                                       args[1].string_value(),
                                       args[2].string_value());
    case FCT_TYPE_ARITY(FunctionKind::kEndsWithWithCollation, TYPE_STRING, 3):
      return InvokeWithCollation<bool>(&functions::EndsWithUtf8WithCollation,
                                       result, status, args[0].string_value(),
                                       args[1].string_value(),
                                       args[2].string_value());
    case FCT_TYPE_ARITY(FunctionKind::kReplaceWithCollation, TYPE_STRING, 4):
      return InvokeStringWithCollation(
          &functions::ReplaceUtf8WithCollation, result, status,
          args[0].string_value(), args[1].string_value(),
          args[2].string_value(), args[3].string_value());
    case FCT_TYPE_ARITY(FunctionKind::kSplitSubstrWithCollation, TYPE_STRING,
                        4):
      return InvokeStringWithCollation(
          &functions::SplitSubstrWithCollation, result, status,
          args[0].string_value(), args[1].string_value(),
          args[2].string_value(), args[3].int64_value(),
          /*count=*/std::numeric_limits<int64_t>::max());
    case FCT_TYPE_ARITY(FunctionKind::kSplitSubstrWithCollation, TYPE_STRING,
                        5):
      return InvokeStringWithCollation(
          &functions::SplitSubstrWithCollation, result, status,
          args[0].string_value(), args[1].string_value(),
          args[2].string_value(), args[3].int64_value(), args[4].int64_value());
  }
  *status = ::googlesql_base::UnimplementedErrorBuilder()
            << "Unsupported string function: " << debug_name();
  return false;
}

absl::StatusOr<Value> SplitWithCollationFunction::Eval(
    absl::Span<const TupleData* const> params, absl::Span<const Value> args,
    EvaluationContext* context) const {
  GOOGLESQL_RET_CHECK_GE(args.size(), 2);
  GOOGLESQL_RET_CHECK_LE(args.size(), 3);
  if (HasNulls(args)) return Value::Null(output_type());
  absl::Status status;
  std::vector<absl::string_view> parts;
  std::vector<Value> values;

  absl::StatusOr<std::unique_ptr<const GoogleSqlCollator>> collator_or_status =
      MakeSqlCollator(args[0].string_value());
  if (!collator_or_status.ok()) {
    return collator_or_status.status();
  }

  const std::string& delimiter =
      (args.size() == 2) ? "," : args[2].string_value();
  if (!functions::SplitUtf8WithCollation(*(collator_or_status.value()),
                                         args[1].string_value(), delimiter,
                                         &parts, &status)) {
    return status;
  }

  values.reserve(parts.size());

  for (absl::string_view s : parts) {
    values.push_back(Value::String(s));
  }
  return Value::Array(types::StringArrayType(), values);
}

absl::StatusOr<Value> CollationKeyFunction::Eval(
    absl::Span<const TupleData* const> params, absl::Span<const Value> args,
    EvaluationContext* context) const {
  GOOGLESQL_RET_CHECK_EQ(args.size(), 2);
  if (HasNulls(args)) {
    return Value::NullBytes();
  }

  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const GoogleSqlCollator> collator,
                   MakeSqlCollator(args[1].string_value()));
  absl::Cord cord;
  GOOGLESQL_RETURN_IF_ERROR(collator->GetSortKeyUtf8(args[0].string_value(), &cord));
  return Value::Bytes(cord.Flatten());
}

}  // namespace

void RegisterBuiltinStringWithCollationFunctions() {
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kEndsWithWithCollation,
       FunctionKind::kReplaceWithCollation, FunctionKind::kSplitWithCollation,
       FunctionKind::kStartsWithWithCollation,
       FunctionKind::kStrposWithCollation, FunctionKind::kInstrWithCollation,
       FunctionKind::kSplitSubstrWithCollation},
      [](FunctionKind kind, const Type* output_type) {
        return new StringWithCollationFunction(kind, output_type);
      });

  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kSplitWithCollation},
      [](FunctionKind kind, const Type* output_type) {
        return new SplitWithCollationFunction(kind, output_type);
      });
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kCollationKey},
      [](FunctionKind kind, const Type* output_type) {
        return new CollationKeyFunction(kind, output_type);
      });
}

}  // namespace googlesql
