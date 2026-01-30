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

#include "googlesql/public/signature_match_result.h"

#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/type.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

namespace googlesql {

bool SignatureMatchResult::IsCloserMatchThan(
    const SignatureMatchResult& other_result) const {
  if (non_literals_coerced() != other_result.non_literals_coerced()) {
    return non_literals_coerced() < other_result.non_literals_coerced();
  }
  if (non_literals_distance() != other_result.non_literals_distance()) {
    return non_literals_distance() < other_result.non_literals_distance();
  }
  if (literals_coerced() != other_result.literals_coerced()) {
    return literals_coerced() < other_result.literals_coerced();
  }
  return literals_distance() < other_result.literals_distance();
}

void SignatureMatchResult::UpdateFromResult(
    const SignatureMatchResult& other_result) {
  data_->non_matched_arguments += other_result.non_matched_arguments();
  data_->non_literals_coerced += other_result.non_literals_coerced();
  data_->non_literals_distance += other_result.non_literals_distance();
  data_->literals_coerced += other_result.literals_coerced();
  data_->literals_distance += other_result.literals_distance();
  data_->mismatch_message = other_result.mismatch_message();
  data_->bad_argument_index = other_result.bad_argument_index();
  data_->tvf_relation_coercion_map = other_result.tvf_relation_coercion_map();
}

std::string SignatureMatchResult::DebugString() const {
  std::string result =
      absl::StrCat("non-matched arguments: ", non_matched_arguments(),
                   ", non-literals coerced: ", non_literals_coerced(),
                   ", non-literals distance: ", non_literals_distance(),
                   ", literals coerced: ", literals_coerced(),
                   ", literals distance: ", literals_distance());
  if (!mismatch_message().empty()) {
    absl::StrAppend(&result, ", mismatch message: \"", mismatch_message(),
                    "\"");
  }
  if (!tvf_relation_coercion_map().empty()) {
    std::vector<std::string> entries;
    for (const auto& [key, value] : tvf_relation_coercion_map()) {
      entries.push_back(absl::StrCat("(arg: ", key.argument_index,
                                     ", col: ", key.column_index, ")->",
                                     value->DebugString(true)));
    }
    absl::StrAppend(&result, "\", tvf relation coercion map: [",
                    absl::StrJoin(entries, ", "), "]");
  }
  return result;
}

}  // namespace googlesql
