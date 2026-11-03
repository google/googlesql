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

}  // namespace

void RegisterBuiltinVariantFunctions() {
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kIsVariantNull},
      [](FunctionKind kind, const Type* output_type) {
        return new IsVariantNullFunction(kind, output_type);
      });
}

}  // namespace googlesql
