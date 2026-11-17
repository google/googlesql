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

#include "googlesql/analyzer/string_literal_table_alias.h"

#include <cstddef>
#include <string>
#include <vector>

#include "googlesql/public/strings.h"
#include "absl/algorithm/container.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"

namespace googlesql {

namespace {

constexpr absl::string_view kGlobWildcardChars = "*?[]{}";

bool IsGlobWildcardChar(char c) {
  return absl::StrContains(kGlobWildcardChars, c);
}

bool IsShardChar(char c) { return absl::ascii_isdigit(c) || c == '*'; }

absl::string_view StripTrailingShardChars(absl::string_view s) {
  while (!s.empty() && IsShardChar(s.back())) {
    s.remove_suffix(1);
  }
  return s;
}

// Strips a trailing shard suffix, `@[0-9*]+` or `-[0-9*]+-of-[0-9*]+`, from
// `stem`.
absl::string_view StripShardSuffix(absl::string_view stem) {
  absl::string_view prefix = StripTrailingShardChars(stem);
  if (prefix.size() == stem.size()) {
    return stem;  // No trailing shard characters.
  }
  if (absl::ConsumeSuffix(&prefix, "@")) {
    return prefix;
  }
  if (absl::ConsumeSuffix(&prefix, "-of-")) {
    const absl::string_view before_shard = prefix;
    prefix = StripTrailingShardChars(prefix);
    if (prefix.size() < before_shard.size() &&
        absl::ConsumeSuffix(&prefix, "-")) {
      return prefix;
    }
  }
  return stem;
}

}  // namespace

std::string DeriveImplicitAliasForStringLiteralTable(
    absl::string_view table_literal) {
  // 1. A comma outside {...} separates multiple paths: no single name. A brace
  // group that spans path segments, or that is never closed and contains a
  // comma, is also a list of paths.
  int brace_depth = 0;
  bool comma_in_open_braces = false;
  for (char c : table_literal) {
    if (c == '{') {
      ++brace_depth;
    } else if (c == '}') {
      if (brace_depth > 0 && --brace_depth == 0) {
        comma_in_open_braces = false;
      }
    } else if (c == ',') {
      if (brace_depth == 0) {
        return "";
      }
      comma_in_open_braces = true;
    } else if ((c == '/' || c == '\\') && brace_depth > 0) {
      return "";
    }
  }
  if (comma_in_open_braces) {
    return "";
  }

  // 2. Find the basename (last non-empty segment) and its immediate parent
  // segment, which may be empty (e.g. 'gs://*.csv').
  std::vector<absl::string_view> segments =
      absl::StrSplit(table_literal, absl::ByAnyChar("/\\"));
  int basename_index = static_cast<int>(segments.size()) - 1;
  while (basename_index >= 0 && segments[basename_index].empty()) {
    --basename_index;
  }
  if (basename_index < 0) {
    return "";
  }
  absl::string_view candidate = segments[basename_index];
  if (IsGlobWildcardChar(candidate.front())) {
    if (basename_index == 0) {
      return "";
    }
    candidate = segments[basename_index - 1];
    if (absl::c_any_of(candidate, IsGlobWildcardChar)) {
      return "";
    }
  }

  // 3. Strip extensions (everything from the first '.'), then shard suffixes.
  absl::string_view stem = candidate.substr(0, candidate.find('.'));
  stem = StripShardSuffix(stem);

  // 4. For a prefix glob, keep the prefix before the first wildcard and trim
  // trailing separators, including a shard '@' followed by a wildcard.
  const size_t wildcard_pos = stem.find_first_of(kGlobWildcardChars);
  if (wildcard_pos != absl::string_view::npos) {
    stem = stem.substr(0, wildcard_pos);
    while (!stem.empty() &&
           (stem.back() == '_' || stem.back() == '-' || stem.back() == '@')) {
      stem.remove_suffix(1);
    }
  }

  // 5. Any non-empty stem is the alias, from a file name or a directory,
  // unless it would be an internal alias, or it ends in ':' and so is a URI
  // scheme or drive letter (e.g. 'gs:/*.csv' or 'C:\*.csv').
  if (IsInternalAlias(stem) || absl::EndsWith(stem, ":")) {
    return "";
  }
  return std::string(stem);
}

}  // namespace googlesql
