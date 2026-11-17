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

#include <string>
#include <utility>
#include <vector>

#include "googlesql/common/builtin_function_internal.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/types/value_representations.h"
#include "googlesql/public/types/vector_type_util.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
absl::Status GetVectorFunctions(TypeFactory* type_factory,
                                const GoogleSQLBuiltinFunctionOptions& options,
                                NameToFunctionMap* functions,
                                NameToTypeMap* types) {
  if (!options.language_options.LanguageFeatureEnabled(FEATURE_VECTOR_TYPE)) {
    return absl::OkStatus();
  }

  GOOGLESQL_ASSIGN_OR_RETURN(const Type* vector_type, MakeVectorType(type_factory));
  const auto& [it, inserted] =
      types->insert({std::string(kVectorTypeName), vector_type});
  GOOGLESQL_RET_CHECK(inserted);
  GOOGLESQL_RET_CHECK_EQ(it->second, vector_type);

  // FN_ENCODE_VECTOR
  InsertFunction(functions, options, "encode_vector", Function::SCALAR,
                 {{vector_type,
                   {types::FloatArrayType(),
                    {types::Int64Type(), FunctionArgumentType::OPTIONAL},
                    {types::StringType(), FunctionArgumentType::OPTIONAL}},
                   FN_ENCODE_VECTOR,
                   AddVectorTypeRequiredOptions(FunctionSignatureOptions())}});

  // FN_DECODE_VECTOR
  InsertFunction(functions, options, "decode_vector", Function::SCALAR,
                 {{types::FloatArrayType(),
                   {vector_type},
                   FN_DECODE_VECTOR,
                   AddVectorTypeRequiredOptions(FunctionSignatureOptions())}});

  // FN_VECTOR_DIMENSION_COUNT
  InsertFunction(functions, options, "vector_dimension_count", Function::SCALAR,
                 {{types::Int64Type(),
                   {vector_type},
                   FN_VECTOR_DIMENSION_COUNT,
                   AddVectorTypeRequiredOptions(FunctionSignatureOptions())}});

  // FN_VECTOR_ENCODING
  InsertFunction(functions, options, "vector_encoding", Function::SCALAR,
                 {{types::StringType(),
                   {vector_type},
                   FN_VECTOR_ENCODING,
                   AddVectorTypeRequiredOptions(FunctionSignatureOptions())}});

  return absl::OkStatus();
}

}  // namespace googlesql
