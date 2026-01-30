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

#ifndef GOOGLESQL_COMMON_BUILTINS_OUTPUT_PROPERTIES_H_
#define GOOGLESQL_COMMON_BUILTINS_OUTPUT_PROPERTIES_H_

#include <utility>

#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/options.pb.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"

namespace googlesql {

// This class collects metadata about the loading of builtin functions and
// types, and is used for validation during a call to
// `GetBuiltinFunctionsAndTypes`.
class BuiltinsOutputProperties {
 public:
  // Marks the function signature as one that accepts a supplied argument
  // at the specified `arg_idx`.
  void MarkSupportsSuppliedArgumentType(FunctionSignatureId id, int arg_idx) {
    supports_supplied_argument_type_.insert({id, arg_idx});
    id_to_idx_map_[id].insert(arg_idx);
  }

  // Returns true if the function signature supports supplied argument types
  // at the specified `arg_idx`.
  bool SupportsSuppliedArgumentType(FunctionSignatureId id, int arg_idx) const {
    return supports_supplied_argument_type_.contains({id, arg_idx});
  }

  // Return a set containing all argument indices for which a supplied argument
  // Type is supported for the given FunctionSignatureId `id`.
  absl::flat_hash_set<int> GetSupportedArgumentIndicesForSuppliedType(
      FunctionSignatureId id) const {
    auto it = id_to_idx_map_.find(id);
    if (it == id_to_idx_map_.end()) {
      return {};
    }
    return it->second;
  }

 private:
  absl::flat_hash_set<std::pair<FunctionSignatureId, int>>
      supports_supplied_argument_type_;
  absl::flat_hash_map<FunctionSignatureId, absl::flat_hash_set<int>>
      id_to_idx_map_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_COMMON_BUILTINS_OUTPUT_PROPERTIES_H_
