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

#ifndef GOOGLESQL_COMMON_MATCH_RECOGNIZE_EPSILON_REMOVER_H_
#define GOOGLESQL_COMMON_MATCH_RECOGNIZE_EPSILON_REMOVER_H_

#include <memory>

#include "googlesql/common/match_recognize/nfa.h"
#include "absl/status/statusor.h"

namespace googlesql::functions::match_recognize {

// Consumes an NFA that satisfies the following properties:
// - No edges into the start state
// - No edges out of the final state
// - All edges into the final state are epsilon edges (e.g. edges that do not
//     consume a row).
//
// Transforms the NFA into an equivalent one which still satisfies the above
// properties, but with the additional constraint that the *only* epsilon edges
// are those leading into the final state.
absl::StatusOr<std::unique_ptr<NFA>> RemoveEpsilons(const NFA& nfa);

}  // namespace googlesql::functions::match_recognize
#endif  // GOOGLESQL_COMMON_MATCH_RECOGNIZE_EPSILON_REMOVER_H_
