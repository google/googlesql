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

#include "googlesql/public/catalog_helper.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/public/strings.h"
#include "googlesql/base/case.h"
#include "absl/flags/flag.h"
#include "absl/functional/function_ref.h"
#include "absl/strings/ascii.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/edit_distance.h"

ABSL_FLAG(int64_t, googlesql_min_length_required_for_edit_distance, 3,
          "Minimum length required of the input string to find its closest "
          "match based on edit distance.");

namespace googlesql {

template <typename NameListT>
typename NameListT::value_type ClosestNameImpl(
    absl::string_view mistyped_name, const NameListT& possible_names,
    absl::FunctionRef<bool(char, char)> char_equal_fn,
    absl::FunctionRef<int(absl::string_view, absl::string_view)>
        string_compare_fn) {
  if (mistyped_name.size() <
      absl::GetFlag(FLAGS_googlesql_min_length_required_for_edit_distance)) {
    return {};
  }

  // Allow ~20% edit distance for suggestions.
  const int distance_threshold = (mistyped_name.size() < 5)
                                 ? mistyped_name.size() / 2
                                 : mistyped_name.size() / 5 + 2;
  int min_edit_distance = distance_threshold + 1;

  int closest_name_index = -1;
  for (int i = 0; i < possible_names.size(); ++i) {
    // Exclude internal names (with '$' prefix).
    if (IsInternalAlias(possible_names[i])) continue;

    const int edit_distance = googlesql_base::CappedLevenshteinDistance(
        mistyped_name.begin(), mistyped_name.end(), possible_names[i].begin(),
        possible_names[i].end(), char_equal_fn, distance_threshold + 1);
    if (edit_distance < min_edit_distance) {
      min_edit_distance = edit_distance;
      closest_name_index = i;
    } else if (edit_distance == min_edit_distance && closest_name_index != -1 &&
               string_compare_fn(possible_names[i],
                                 possible_names[closest_name_index]) < 0) {
      // As a tie-breaker we chose the string which occurs lexicographically
      // first.
      closest_name_index = i;
    }
  }

  if (closest_name_index == -1) {
    // No match found within the allowed ~20% edit distance.
    return {};
  }

  ABSL_DCHECK_GE(closest_name_index, 0);
  ABSL_DCHECK_LT(closest_name_index, possible_names.size());
  return possible_names[closest_name_index];
}

std::string ClosestName(absl::string_view mistyped_name,
                        const std::vector<std::string>& possible_names) {
  // TODO: Should this be case insensitive like SuggestEnumValue?
  return ClosestNameImpl(
      mistyped_name, possible_names, std::equal_to<char>(),
      [](absl::string_view a, absl::string_view b) { return a.compare(b); });
}

std::string SuggestEnumValue(const EnumType* type,
                             absl::string_view mistyped_value) {
  if (type == nullptr || mistyped_value.empty()) {
    // Nothing to suggest here.
    return {};
  }
  std::vector<absl::string_view> suggest_strings;
  const google::protobuf::EnumDescriptor* type_descriptor = type->enum_descriptor();
  for (int i = 0; i < type_descriptor->value_count(); ++i) {
    const google::protobuf::EnumValueDescriptor* value_descriptor =
        type_descriptor->value(i);
    if (type->IsValidEnumValue(value_descriptor)) {
      suggest_strings.emplace_back(value_descriptor->name());
    }
  }

  // We implement a case-insensitive matching for purposes of suggesting enums.
  return std::string(ClosestNameImpl(
      mistyped_value, suggest_strings,
      // Case insensitive single character equals lambda.
      [](char a, char b) {
        return absl::ascii_toupper(a) == absl::ascii_toupper(b);
      },
      // Case insensitive string comparison.
      [](absl::string_view a, absl::string_view b) {
        return googlesql_base::CaseCompare(a, b);
      }));
}

}  // namespace googlesql
