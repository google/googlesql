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

#include "googlesql/analyzer/analyzer_test_options.h"

#include <cstddef>

#include "googlesql/base/testing/status_matchers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "file_based_test_driver/test_case_options.h"

namespace googlesql {

using testing::AllOf;
using testing::Contains;
using testing::Ge;
using testing::HasSubstr;
using testing::IsEmpty;
using testing::Not;
using testing::Pair;
using testing::SizeIs;
using testing::UnorderedElementsAre;
using absl_testing::StatusIs;

// These examples may need to change as the default enabled status changes on
// individual rewrites. The rewriter code currently doesn't allow enum entries
// to exist just for the sake of tests, but if this becomes a maintance burden
// perhaps we should make an exception for some enum entries that will be stable
// and can be used here.
static const ResolvedASTRewrite kDefaultEnabledRewrite =
    ResolvedASTRewrite::REWRITE_TYPEOF_FUNCTION;
static const ResolvedASTRewrite kDefaultNotEnabledRewrite =
    ResolvedASTRewrite::REWRITE_ANONYMIZATION;

TEST(AnalyzerTestOptions, EnabledRewritesTest) {
  AnalyzerTestRewriteGroups rewrites;
  file_based_test_driver::TestCaseOptions options;
  options.RegisterString(kEnabledASTRewrites, "");
  for (const char* str : {"NONE", "none", " nonE", "nONE   ", " None "}) {
    options.SetString(kEnabledASTRewrites, str);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(rewrites, GetEnabledRewrites(options));
    EXPECT_THAT(rewrites, IsEmpty());
  }
  for (const char* str :
       {"DEFAULTS", "defaults", " DEFauLTS", "deFAULTs   ", " Defaults "}) {
    options.SetString(kEnabledASTRewrites, str);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(rewrites, GetEnabledRewrites(options));
    ASSERT_THAT(rewrites, SizeIs(1));
    EXPECT_THAT(rewrites, UnorderedElementsAre(Pair(
                              "DEFAULTS", Contains(kDefaultEnabledRewrite))));
  }
  size_t number_of_defaults = rewrites.size();
  for (const char* str : {"ALL", "all", " AlL", "aLl   ", " All "}) {
    options.SetString(kEnabledASTRewrites, str);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(rewrites, GetEnabledRewrites(options));
    ASSERT_THAT(rewrites, SizeIs(1));
    EXPECT_THAT(rewrites,
                UnorderedElementsAre(Pair(
                    "ALL", AllOf(SizeIs(Ge(number_of_defaults)),
                                 AllOf(Contains(kDefaultEnabledRewrite),
                                       Contains(kDefaultNotEnabledRewrite))))));
  }
}

TEST(AnalyzerTestOptions, EnabledRewrites__Reset) {
  AnalyzerTestRewriteGroups rewrites;
  file_based_test_driver::TestCaseOptions options;
  options.RegisterString(kEnabledASTRewrites, "");
  options.SetString(kEnabledASTRewrites, "ALL");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(rewrites, GetEnabledRewrites(options));
  ASSERT_THAT(rewrites, SizeIs(1));
  EXPECT_THAT(rewrites, UnorderedElementsAre(Pair("ALL", Not(IsEmpty()))));
  // Re-running the Get function will reset 'rewrites'.
  options.SetString(kEnabledASTRewrites, "");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(rewrites, GetEnabledRewrites(options));
  ASSERT_THAT(rewrites, IsEmpty());
}

TEST(AnalyzerTestOptions, EnabledRewrites__ExplicitlyEmpty) {
  AnalyzerTestRewriteGroups rewrites;
  file_based_test_driver::TestCaseOptions options;
  options.RegisterString(kEnabledASTRewrites, "");
  options.SetString(kEnabledASTRewrites, "");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(rewrites, GetEnabledRewrites(options));
  ASSERT_THAT(rewrites, IsEmpty());
}

TEST(AnalyzerTestOptions, EnabledRewrites__GarbageEntryError) {
  file_based_test_driver::TestCaseOptions options;
  options.RegisterString(kEnabledASTRewrites, "");
  options.SetString(kEnabledASTRewrites, "GARBAGE!");
  EXPECT_THAT(GetEnabledRewrites(options),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Rewrite list should always start with one of "
                                 "ALL, ALL_MINUS_DEV, DEFAULTS, "
                                 "DEFAULTS_MINUS_DEV, NONE")));
}

TEST(AnalyzerTestOptions, EnabledRewrites__IncludedRewritePrefixError) {
  file_based_test_driver::TestCaseOptions options;
  options.RegisterString(kEnabledASTRewrites, "");
  options.SetString(kEnabledASTRewrites, "NONE,+REWRITE_TYPEOF_FUNCTION");
  EXPECT_THAT(
      GetEnabledRewrites(options),
      StatusIs(
          absl::StatusCode::kInternal,
          HasSubstr("For consistency, do not include the REWRITE_ prefix")));
}

TEST(AnalyzerTestOptions, EnabledRewrites__RedundantOverrideError) {
  file_based_test_driver::TestCaseOptions options;
  options.RegisterString(kEnabledASTRewrites, "");
  for (const char* str : {"NONE,+TYPEOF_FUNCTION,+TYPEOF_FUNCTION",
                          "ALL,-TYPEOF_FUNCTION,-TYPEOF_FUNCTION",
                          "DEFAULTS,+TYPEOF_FUNCTION,-TYPEOF_FUNCTION"}) {
    options.SetString(kEnabledASTRewrites, str);
    EXPECT_THAT(
        GetEnabledRewrites(options),
        StatusIs(
            absl::StatusCode::kInternal,
            HasSubstr("Duplicate entry for Rewrite: REWRITE_TYPEOF_FUNCTION")));
  }
}

TEST(AnalyzerTestOptions, EnabledRewrites__AllPlusRewriteError) {
  file_based_test_driver::TestCaseOptions options;
  options.RegisterString(kEnabledASTRewrites, "");
  options.SetString(kEnabledASTRewrites, "ALL,+TYPEOF_FUNCTION");
  EXPECT_THAT(
      GetEnabledRewrites(options),
      StatusIs(absl::StatusCode::kInternal,
               HasSubstr(
                   "Attempting to add Rewrite, but already started from ALL")));
}

TEST(AnalyzerTestOptions, EnabledRewrites__NoneMinusRewriteError) {
  file_based_test_driver::TestCaseOptions options;
  options.RegisterString(kEnabledASTRewrites, "");
  options.SetString(kEnabledASTRewrites, "NONE,-TYPEOF_FUNCTION");
  EXPECT_THAT(
      GetEnabledRewrites(options),
      StatusIs(
          absl::StatusCode::kInternal,
          HasSubstr(
              "Attempting to remove Rewrite, but already started from NONE.")));
}

TEST(AnalyzerTestOptions, EnabledRewrites__WhitespaceCapsInsensitvity) {
  AnalyzerTestRewriteGroups rewrites;
  file_based_test_driver::TestCaseOptions options;
  options.RegisterString(kEnabledASTRewrites, "");
  options.SetString(kEnabledASTRewrites,
                    "NONE,  +typeof_function   ,  +Anonymization");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(rewrites, GetEnabledRewrites(options));
  EXPECT_THAT(
      rewrites,
      UnorderedElementsAre(Pair(
          "NONE,+ANONYMIZATION,+TYPEOF_FUNCTION",
          AllOf(SizeIs(2), UnorderedElementsAre(
                               ResolvedASTRewrite::REWRITE_TYPEOF_FUNCTION,
                               ResolvedASTRewrite::REWRITE_ANONYMIZATION)))));
}

TEST(AnalyzerTestOptions, EnabledRewrites__AdditionAndSubtraction) {
  AnalyzerTestRewriteGroups rewrites;
  file_based_test_driver::TestCaseOptions options;
  options.RegisterString(kEnabledASTRewrites, "");
  options.SetString(kEnabledASTRewrites,
                    "DEFAULTS,-typeof_function,+Anonymization");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(rewrites, GetEnabledRewrites(options));
  ASSERT_THAT(rewrites, SizeIs(1));
  EXPECT_THAT(
      rewrites,
      UnorderedElementsAre(Pair(
          "DEFAULTS,+ANONYMIZATION,-TYPEOF_FUNCTION",
          AllOf(SizeIs(Ge(1)),
                Contains(ResolvedASTRewrite::REWRITE_ANONYMIZATION),
                Not(Contains(ResolvedASTRewrite::REWRITE_TYPEOF_FUNCTION))))));
}

TEST(AnalyzerTestOptions, EnabledRewrites__AddAlreadyIncludedFeature) {
  AnalyzerTestRewriteGroups rewrites;
  file_based_test_driver::TestCaseOptions options;
  options.RegisterString(kEnabledASTRewrites, "");
  options.SetString(kEnabledASTRewrites, "DEFAULTS");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(rewrites, GetEnabledRewrites(options));
  ASSERT_THAT(rewrites, SizeIs(1));
  EXPECT_THAT(
      rewrites,
      UnorderedElementsAre(
          Pair("DEFAULTS",
               AllOf(SizeIs(Ge(1)),
                     Contains(ResolvedASTRewrite::REWRITE_TYPEOF_FUNCTION)))));
  // This is okay. Even though the feature is default and thus already enabled
  // it may not have been default enabled when this test file was written. We
  // don't want to force test file updates when we change the default status
  // of a rewriter from DEFAULT off to DEFAULT on.
  options.SetString(kEnabledASTRewrites, "DEFAULTS,+TYPEOF_FUNCTION");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(rewrites, GetEnabledRewrites(options));
  ASSERT_THAT(rewrites, SizeIs(1));
  EXPECT_THAT(
      rewrites,
      UnorderedElementsAre(
          Pair("DEFAULTS,+TYPEOF_FUNCTION",
               AllOf(SizeIs(Ge(1)),
                     Contains(ResolvedASTRewrite::REWRITE_TYPEOF_FUNCTION)))));
}

TEST(AnalyzerTestOptions, EnabledRewrites__MultipleGroups) {
  AnalyzerTestRewriteGroups rewrites;
  file_based_test_driver::TestCaseOptions options;
  options.RegisterString(kEnabledASTRewrites, "");
  options.SetString(kEnabledASTRewrites, "DEFAULTS|DEFAULTS,+TYPEOF_FUNCTION");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(rewrites, GetEnabledRewrites(options));
  ASSERT_THAT(rewrites, SizeIs(2));
  EXPECT_THAT(
      rewrites,
      UnorderedElementsAre(
          Pair("DEFAULTS",
               AllOf(SizeIs(Ge(1)),
                     Contains(ResolvedASTRewrite::REWRITE_TYPEOF_FUNCTION))),
          Pair("DEFAULTS,+TYPEOF_FUNCTION",
               AllOf(SizeIs(Ge(1)),
                     Contains(ResolvedASTRewrite::REWRITE_TYPEOF_FUNCTION)))));
}

TEST(AnalyzerTestOptions, EnabledRewrites__MultipleRedundantGroups) {
  AnalyzerTestRewriteGroups rewrites;
  file_based_test_driver::TestCaseOptions options;
  options.RegisterString(kEnabledASTRewrites, "");
  options.SetString(
      kEnabledASTRewrites,
      "DEFAULTS|DEFAULTS,+TYPEOF_FUNCTION|DEFAULTS,+typeof_function");
  EXPECT_THAT(GetEnabledRewrites(options),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Multiple rewrite groups canonicalize to: "
                                 "DEFAULTS,+TYPEOF_FUNCTION")));
}

}  // namespace googlesql
