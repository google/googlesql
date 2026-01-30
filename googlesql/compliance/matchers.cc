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

#include "googlesql/compliance/matchers.h"

#include <map>
#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "googlesql/base/map_util.h"
#include "re2/re2.h"

namespace googlesql {

RegexMatcher::RegexMatcher(
    absl::string_view re2_message_pattern,
    std::map<int, std::unique_ptr<MatcherBase<std::string>>> group_matchers,
    int max_samples)
    : re2_message_pattern_(re2_message_pattern),
      group_matchers_(std::move(group_matchers)),
      group_cnt_(re2_message_pattern_.NumberOfCapturingGroups()),
      max_samples_(max_samples),
      groups_list_(group_cnt_),
      argv_(group_cnt_),
      string_groups_(group_cnt_),
      args_(new RE2::Arg*[group_cnt_]) {
  for (int group_idx = 0; group_idx < group_cnt_; ++group_idx) {
    args_[group_idx] = &argv_[group_idx];
    argv_[group_idx] = &string_groups_[group_idx];
  }
}
bool RegexMatcher::MatchesStringViewImpl(absl::string_view candidate) {
  if (!RE2::PartialMatchN(candidate, re2_message_pattern_, args_.get(),
                          group_cnt_)) {
    return false;
  }
  for (int group_idx = 0; group_idx < group_cnt_; ++group_idx) {
    // First, check if there is a matcher for this group.
    if (googlesql_base::ContainsKey(group_matchers_, group_idx)) {
      // There is a matcher, if it doesn't match then this matcher doesn't
      // match.
      if (!group_matchers_.at(group_idx)->Matches(string_groups_[group_idx])) {
        return false;
      }
    }
    // If the key doesn't exist yet and we have too many samples don't add
    // another one.
    if (googlesql_base::ContainsKey(groups_list_[group_idx], string_groups_[group_idx]) ||
        groups_list_[group_idx].size() < max_samples_) {
      ++groups_list_[group_idx][string_groups_[group_idx]];
    }
  }
  return true;
}

std::string RegexMatcher::MatcherSummary() const {
  std::string summary = absl::StrCat(
      "Matched ", MatchCount(), " time(s) with the following group counts:\n");
  for (int group_idx = 0; group_idx < group_cnt_; ++group_idx) {
    absl::StrAppend(&summary, "  Group ", group_idx, ":\n");
    for (const auto& match : groups_list_[group_idx]) {
      absl::StrAppend(&summary, "    ", match.second, ": ", match.first, "\n");
    }
    if (googlesql_base::ContainsKey(group_matchers_, group_idx)) {
      absl::StrAppend(&summary, "With group_matcher ",
                      group_matchers_.at(group_idx)->MatcherName(), ":\n",
                      group_matchers_.at(group_idx)->MatcherSummary());
    }
  }
  return summary;
}

bool SubstringMatcher::MatchesStringViewImpl(absl::string_view candidate) {
  if (!absl::StrContains(candidate, matching_substring_view_)) {
    return false;
  }
  const std::string candidate_str = std::string(candidate);

  if (googlesql_base::ContainsKey(matching_strings_, candidate_str) ||
      matching_strings_.size() < max_samples_) {
    ++matching_strings_[std::move(candidate_str)];
  }
  return true;
}

std::string SubstringMatcher::MatcherSummary() const {
  std::string summary =
      absl::StrCat("Matched ", MatchCount(), " time(s). Matching strings:\n");
  for (const auto& matching_string : matching_strings_) {
    const std::string& str = matching_string.first;
    const int count = matching_string.second;
    absl::StrAppend(&summary, "  ", str, ": ", count, "\n");
  }
  return summary;
}

bool StatusRegexMatcher::MatchesImpl(const absl::Status& candidate) {
  return error_code_matcher_.Matches(candidate.code()) &&
         regex_matcher_.Matches(candidate.message());
}

bool StatusSubstringMatcher::MatchesImpl(const absl::Status& candidate) {
  return error_code_matcher_.Matches(candidate.code()) &&
         substring_matcher_.Matches(candidate.message());
}

bool StatusErrorCodeMatcher::MatchesImpl(const absl::Status& candidate) {
  return error_code_matcher_.Matches(candidate);
}

}  // namespace googlesql
