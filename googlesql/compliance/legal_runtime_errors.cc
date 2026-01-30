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

#include "googlesql/compliance/legal_runtime_errors.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/compliance/matchers.h"
#include "absl/memory/memory.h"
#include "googlesql/base/status.h"

namespace googlesql {

std::unique_ptr<MatcherCollection<absl::Status>> LegalRuntimeErrorMatcher(
    std::string matcher_name) {
  std::vector<std::unique_ptr<MatcherBase<absl::Status>>> matchers;

  matchers.emplace_back(std::make_unique<StatusErrorCodeMatcher>(
      absl::StatusCode::kInvalidArgument));
  matchers.emplace_back(
      std::make_unique<StatusErrorCodeMatcher>(absl::StatusCode::kOutOfRange));
  // TODO: Consider removing this and changing the reference
  // implementation to return ALREADY_EXISTS for primary key collisions. The
  // current convention is to always return INVALID_ARGUMENT for analysis
  // errors and OUT_OF_RANGE for runtime errors.
  matchers.emplace_back(std::make_unique<StatusErrorCodeMatcher>(
      absl::StatusCode::kAlreadyExists));

  return std::make_unique<MatcherCollection<absl::Status>>(matcher_name,
                                                           std::move(matchers));
}

}  // namespace googlesql
