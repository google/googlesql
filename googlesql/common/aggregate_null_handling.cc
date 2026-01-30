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

#include "googlesql/common/aggregate_null_handling.h"

#include "absl/container/flat_hash_set.h"
#include "googlesql/base/case.h"

namespace googlesql {
using StringViewCaseHash = ::googlesql_base::StringViewCaseHash;
using StringViewCaseEqual = ::googlesql_base::StringViewCaseEqual;
// TODO: b/403019760 - deprecate this function and migrate to
// `FunctionSignatureOptions::ignores_nulls` instead.
bool IgnoresNullArguments(
    const ResolvedNonScalarFunctionCallBase* aggregate_function) {
  static const absl::flat_hash_set<absl::string_view, StringViewCaseHash,
                                   StringViewCaseEqual>* const
      kFunctionsNotIgnoreNullSet =
          new absl::flat_hash_set<absl::string_view, StringViewCaseHash,
                                  StringViewCaseEqual>(
              {"array_agg", "any_value", "approx_top_count", "approx_top_sum",
               "st_nearest_neighbors", "intersect_agg"});

  switch (aggregate_function->null_handling_modifier()) {
    // Follow any NULL-handling annotations in the query.
    case ResolvedNonScalarFunctionCallBase::RESPECT_NULLS:
      return false;
    case ResolvedNonScalarFunctionCallBase::IGNORE_NULLS:
      return true;
    case ResolvedNonScalarFunctionCallBase::DEFAULT_NULL_HANDLING:
      if (aggregate_function->function()
              ->function_options()
              .default_null_handling ==
          FunctionEnums::DEFAULT_NULL_HANDLING_IGNORE_NULLS) {
        return true;
      }

      // If the functions do not specify their NULL handling,
      // so we cannot guarantee it ignore NULLs.
      if (!aggregate_function->function()->IsGoogleSQLBuiltin()) {
        return false;
      }
      // For builtin functions, we assume they ignore NULLs unless they are in
      // the above list.
      return !kFunctionsNotIgnoreNullSet->contains(
          aggregate_function->function()->Name());
  }
}
}  // namespace googlesql
