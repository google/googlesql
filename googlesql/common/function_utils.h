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

#ifndef GOOGLESQL_COMMON_FUNCTION_UTILS_H_
#define GOOGLESQL_COMMON_FUNCTION_UTILS_H_

#include <string>
#include <vector>

#include "absl/container/flat_hash_set.h"

namespace googlesql {

class Function;
class FunctionSignature;

// Helper function that used to implement Function::is_operator.
bool FunctionIsOperator(const Function& function_name);

// Update arguments to be used in GetSQL.
// This is used to ensure that named arguments are printed prefixed with their
// names.
//
// `unset_arg_indices` specifies the indices of the arguments that are not set.
// This is currently only used for optional relation arguments in TVFs. The
// `args` omits these unset arguments. This set helps determine if an unset
// optional relation argument exists, in which case, the subsequent
// positional_or_named arguments are printed prefixed with the names.
// The total number of concrete arguments from the signature should be the sum
// of args.size() and unset_arg_indices.size().
void UpdateArgsForGetSQL(
    const FunctionSignature* signature, std::vector<std::string>* args,
    const absl::flat_hash_set<int>& unset_arg_indices = {});

}  // namespace googlesql

#endif  // GOOGLESQL_COMMON_FUNCTION_UTILS_H_
