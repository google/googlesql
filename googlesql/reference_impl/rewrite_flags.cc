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

#include "googlesql/reference_impl/rewrite_flags.h"

#include <string>

#include "googlesql/common/options_utils.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/options.pb.h"
#include "absl/container/btree_set.h"
#include "absl/flags/flag.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"

namespace googlesql {

static RewriteSet DefaultRewrites() {
  return RewriteSet(AnalyzerOptions().enabled_rewrites());
}

// Rewrites for constructs which the reference implementation is not able to
// successfully execute using a direct implementation, and thus requires the
// rewriter. These rewrites are enabled for the reference even when using the
// reference impl as a baseline for compliance tests.
absl::btree_set<ResolvedASTRewrite> MinimalRewritesForReference() {
  return {
      // Probably don't add to this list without a very good reason to do so.
      // Features that are rewrite *only* without direct implementation support
      // in the reference implementation are not as well tested as features with
      // both implementations.
      // clang-format off
      // (broken link) start
      // TODO: Remove this once the aggregation threshold rewriter
      // and anonymization rewriter are updated to follow the correct pattern.
      REWRITE_AGGREGATION_THRESHOLD,
      REWRITE_GENERALIZED_QUERY_STMT,
      REWRITE_INLINE_SQL_VIEWS,
      // (broken link) end
      // clang-format on
  };
}

const RewriteSet& MinimalRewrites() {
  static const auto* minimal_rewrites =
      new RewriteSet({MinimalRewritesForReference()});
  return *minimal_rewrites;
}

// Returns a textual flag value corresponding to a rewrite set.
std::string AbslUnparseFlag(RewriteSet set) {
  if (set.empty()) {
    return "none";
  } else if (set == DefaultRewrites()) {
    return "default";
  } else if (set == MinimalRewrites()) {
    return "minimal";
  } else if (set == internal::GetAllRewrites()) {
    return "all";
  }
  return absl::StrJoin(set, ",",
                       [](std::string* out, ResolvedASTRewrite rewrite) {
                         absl::StrAppend(out, ResolvedASTRewrite_Name(rewrite));
                       });
}

// Parses a rewrite set from the command line flag value `text`.
// Returns true and sets `*flag` on success; returns false and sets `*error`
// on failure.
bool AbslParseFlag(absl::string_view text, RewriteSet* set,
                   std::string* error) {
  if (text == "default") {
    *set = DefaultRewrites();
    return true;
  }
  if (text == "none") {
    *set = {};
    return true;
  }
  if (text == "minimal") {
    *set = MinimalRewrites();
    return true;
  }
  if (text == "all") {
    *set = RewriteSet(internal::GetAllRewrites());
    return true;
  }
  auto statusor_parsed_rewrites = internal::ParseEnabledAstRewrites(text);
  if (statusor_parsed_rewrites.ok()) {
    *set = RewriteSet(statusor_parsed_rewrites->options);
    return true;
  }
  *error = statusor_parsed_rewrites.status().message();
  return false;
}
}  // namespace googlesql

ABSL_FLAG(
    googlesql::RewriteSet, rewrites, googlesql::MinimalRewrites(),
    "Which rewrites to enable. Possible values:\n"
    "  minimal: Enable only rewrites which are on by default and no native\n"
    "    implementation exists in the reference impl.\n"
    "  default: Enable only rewrites which are on by default\n"
    "  none: Disable all rewrites\n"
    "  all: Enable all rewrites\n"
    "  DEFAULTS,+FOO,-BAR,...: Enables default rewrites, with REWRITE_FOO\n"
    "    added and REWRITE_BAR removed.");
