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

#include "googlesql/reference_impl/functions/variant.h"

#include <optional>

#include "googlesql/public/type.h"
#include "googlesql/public/value.h"
#include "googlesql/public/variant_value.h"
#include "googlesql/reference_impl/evaluation.h"
#include "googlesql/reference_impl/function.h"
#include "googlesql/reference_impl/tuple.h"
#include "googlesql/base/status_macros.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"

namespace googlesql {
namespace {

// Implements IS_VARIANT_NULL(VARIANT) -> BOOL.
// Returns true if the input holds a Variant null, and false for all other
// values (including when the input is a SQL NULL Variant).
class IsVariantNullFunction : public SimpleBuiltinScalarFunction {
 public:
  IsVariantNullFunction(FunctionKind kind, const Type* output_type)
      : SimpleBuiltinScalarFunction(kind, output_type) {}

  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override {
    GOOGLESQL_RET_CHECK_EQ(args.size(), 1);
    const Value& variant_arg = args[0];

    GOOGLESQL_RET_CHECK(variant_arg.is_valid());
    GOOGLESQL_RET_CHECK(variant_arg.type()->IsVariant());
    if (variant_arg.is_null()) {
      return Value::Bool(false);
    }

    GOOGLESQL_ASSIGN_OR_RETURN(VariantValueView view, variant_arg.variant_value());
    return Value::Bool(view.IsVariantNull());
  }
};

// Extracts the child VariantValueView from `current` for the given
// `subscript_arg` (a STRING object key or INT64 0-based array index).
// Returns std::nullopt if the target type does not match, the key is missing,
// or the index is out of bounds.
absl::StatusOr<std::optional<VariantValueView>> ExtractSubscriptChild(
    const VariantValueView& current, const Value& subscript_arg) {
  if (subscript_arg.type_kind() == TYPE_STRING) {
    if (!current.IsObject()) {
      return std::nullopt;
    }
    return current.GetKeyValue(subscript_arg.string_value());
  }
  if (subscript_arg.type_kind() == TYPE_INT64) {
    if (!current.IsArray()) {
      return std::nullopt;
    }
    return current.GetElement(subscript_arg.int64_value());
  }
  GOOGLESQL_RET_CHECK_FAIL() << "Unsupported subscript type: "
                   << subscript_arg.type()->ShortTypeName(PRODUCT_INTERNAL);
}

// Implements VARIANT subscript access for string (object key) and int64 (array
// offset).
class VariantSubscriptFunction : public SimpleBuiltinScalarFunction {
 public:
  VariantSubscriptFunction(FunctionKind kind, const Type* output_type)
      : SimpleBuiltinScalarFunction(kind, output_type) {}

  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override {
    GOOGLESQL_RET_CHECK_EQ(args.size(), 2);
    const Value& variant_arg = args[0];
    const Value& subscript_arg = args[1];

    GOOGLESQL_RET_CHECK(variant_arg.is_valid());
    GOOGLESQL_RET_CHECK(subscript_arg.is_valid());
    GOOGLESQL_RET_CHECK(variant_arg.type()->IsVariant());
    if (variant_arg.is_null() || subscript_arg.is_null()) {
      return Value::Null(output_type());
    }

    GOOGLESQL_ASSIGN_OR_RETURN(VariantValueView current, variant_arg.variant_value());
    GOOGLESQL_ASSIGN_OR_RETURN(std::optional<VariantValueView> child,
                     ExtractSubscriptChild(current, subscript_arg));
    if (!child.has_value()) {
      return Value::Null(output_type());
    }
    return child->ToVariantValue();
  }
};

}  // namespace

void RegisterBuiltinVariantFunctions() {
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kIsVariantNull},
      [](FunctionKind kind, const Type* output_type) {
        return new IsVariantNullFunction(kind, output_type);
      });
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kVariantSubscript},
      [](FunctionKind kind, const Type* output_type) {
        return new VariantSubscriptFunction(kind, output_type);
      });
}

}  // namespace googlesql
