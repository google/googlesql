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

#include "googlesql/common/function_utils.h"

#include <string>
#include <vector>

#include "googlesql/public/function.h"
#include "googlesql/public/function_signature.h"
#include "absl/container/flat_hash_set.h"
#include "googlesql/base/check.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"

namespace googlesql {

bool FunctionIsOperator(const Function& function) {
  return function.IsGoogleSQLBuiltin() &&
         absl::StartsWith(function.Name(), "$") &&
         function.Name() != "$count_star" &&
         !absl::StartsWith(function.Name(), "$extract");
}

void UpdateArgsForGetSQL(const FunctionSignature* signature,
                         std::vector<std::string>* args,
                         const absl::flat_hash_set<int>& unset_arg_indices) {
  if (signature != nullptr) {
    int arg_index = 0;
    // If the argument is mandatory-named, we have to use that name. Once we
    // specify a named argument, all subsequent arguments must be named.
    bool has_named_only_argument = false;
    // If there is an unset optional relation argument, which is omitted in the
    // SQLBuilder output, we specify the name for the subsequent
    // positional_or_named arguments. This is because in cases where
    // multiple optional positional arguments exists, we do not know which
    // position an argument is intended for if an earlier one is missing.
    bool has_unset_relation = false;
    for (int i = 0;
         i < signature->arguments().size() && arg_index < args->size(); ++i) {
      const FunctionArgumentType& arg = signature->argument(i);
      if (unset_arg_indices.contains(i)) {
        // "repeated" is not considered here because unset_arg_indices can
        // currently only contain relation arguments, which cannot be repeated
        // and cannot follow a repeated argument.
        has_unset_relation = true;
        continue;
      }
      if (!arg.repeated()) {
        if (arg.options().named_argument_kind() == kNamedOnly ||
            has_named_only_argument ||
            (arg.options().named_argument_kind() == kPositionalOrNamed &&
             has_unset_relation)) {
          ABSL_DCHECK(!arg.argument_name().empty());
          ABSL_DCHECK_NE(arg.options().named_argument_kind(), kPositionalOnly)
              << "Positional only argument found after named only argument";
          has_named_only_argument = true;
          (*args)[arg_index] =
              absl::StrCat(signature->argument(i).argument_name(), " => ",
                           (*args)[arg_index]);
        }
        ++arg_index;
        continue;
      }
      if (!signature->IsConcrete()) {
        // In order to properly match the inputs to arguments positions
        // we must have a concrete signature, otherwise we don't know
        // how many occurrences we have and we might get 'lost'. So, in this
        // case, we just give up and hope for the best.
        break;
      }
      // Note, the actual pattern is ..., A, B, A, B, A, B
      // But since we don't actually care about the repeated arguments
      // and they must all have matching num_occurrences, we can simplify
      // the logic by just pretending is ..., A, A, A, B, B, B
      arg_index += arg.num_occurrences();
    }
  }
}

}  // namespace googlesql
