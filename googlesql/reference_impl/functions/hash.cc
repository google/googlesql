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

#include "googlesql/reference_impl/functions/hash.h"

#include <memory>
#include <string>

#include "googlesql/public/functions/hash.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/reference_impl/evaluation.h"
#include "googlesql/reference_impl/function.h"
#include "googlesql/reference_impl/tuple.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace {

static functions::Hasher::Algorithm GetHasherAlgorithm(FunctionKind kind) {
  switch (kind) {
    case FunctionKind::kMd5:
      return functions::Hasher::kMd5;
    case FunctionKind::kSha1:
      return functions::Hasher::kSha1;
    case FunctionKind::kSha256:
      return functions::Hasher::kSha256;
    case FunctionKind::kSha512:
      return functions::Hasher::kSha512;
    default:
      // Crash in debug mode, for non-debug mode fall back to MD5.
      ABSL_DLOG(FATAL) << "Unexpected function kind: " << static_cast<int>(kind);
      return functions::Hasher::kMd5;
  }
}

class HashFunction : public SimpleBuiltinScalarFunction {
 public:
  explicit HashFunction(FunctionKind kind);
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
 private:
  const functions::Hasher::Algorithm algorithm_;
};

class FarmFingerprintFunction : public SimpleBuiltinScalarFunction {
 public:
  FarmFingerprintFunction()
      : SimpleBuiltinScalarFunction(FunctionKind::kFarmFingerprint,
                                    types::Int64Type()) {}
  absl::StatusOr<Value> Eval(absl::Span<const TupleData* const> params,
                             absl::Span<const Value> args,
                             EvaluationContext* context) const override;
};

HashFunction::HashFunction(FunctionKind kind)
    : SimpleBuiltinScalarFunction(kind, types::BytesType()),
      algorithm_(GetHasherAlgorithm(kind)) {}

absl::StatusOr<Value> HashFunction::Eval(
    absl::Span<const TupleData* const> params, absl::Span<const Value> args,
    EvaluationContext* context) const {
  GOOGLESQL_RET_CHECK_EQ(1, args.size());
  if (args[0].is_null()) {
    return Value::Null(output_type());
  }

  const absl::string_view input = args[0].type_kind() == TYPE_BYTES
                                      ? args[0].bytes_value()
                                      : args[0].string_value();

  // Create a new hasher on every invocation for thread safety, because the
  // HashFunction instance is global (b/299648584)
  return Value::Bytes(functions::Hasher::Create(algorithm_)->Hash(input));
}

absl::StatusOr<Value> FarmFingerprintFunction::Eval(
    absl::Span<const TupleData* const> params, absl::Span<const Value> args,
    EvaluationContext* context) const {
  GOOGLESQL_RET_CHECK_EQ(1, args.size());
  if (args[0].is_null()) {
    return Value::Null(output_type());
  }

  const absl::string_view input = args[0].type_kind() == TYPE_BYTES
                                      ? args[0].bytes_value()
                                      : args[0].string_value();

  return Value::Int64(functions::FarmFingerprint(input));
}

}  // namespace

void RegisterBuiltinHashFunctions() {
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kMd5, FunctionKind::kSha1, FunctionKind::kSha256,
       FunctionKind::kSha512},
      [](FunctionKind kind, const Type* output_type) {
        return new HashFunction(kind);
      });
  BuiltinFunctionRegistry::RegisterScalarFunction(
      {FunctionKind::kFarmFingerprint},
      [](FunctionKind kind, const Type* output_type) {
        return new FarmFingerprintFunction();
      });
}

}  // namespace googlesql
