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

#include <cstdint>
#include <memory>

#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/reference_impl/evaluation.h"
#include "gtest/gtest.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace googlesql {
namespace {

TEST(EvaluationContext, ChildContextTest) {
  EvaluationOptions options;
  options.always_use_stable_sort = true;
  EvaluationContext context = EvaluationContext(options);
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_TEST_IDEALLY_DISABLED);
  language_options.DisableLanguageFeature(
      FEATURE_TEST_IDEALLY_ENABLED_BUT_IN_DEVELOPMENT);
  context.SetLanguageOptions(language_options);
  context.SetStatementEvaluationDeadline(absl::Now() + absl::Seconds(1));
  context.SetSessionUser("test_user");

  // Check that a child context created from the above context inherits
  // expected properties.
  std::unique_ptr<EvaluationContext> child_context = context.MakeChildContext();
  // EvaluationOptions fields should be preserved
  EXPECT_EQ(context.options().always_use_stable_sort,
            child_context->options().always_use_stable_sort);
  // LanguageOptions should be preserved
  EXPECT_EQ(context.GetLanguageOptions().GetEnabledLanguageFeatures(),
            child_context->GetLanguageOptions().GetEnabledLanguageFeatures());
  EXPECT_TRUE(child_context->GetLanguageOptions().LanguageFeatureEnabled(
      FEATURE_TEST_IDEALLY_DISABLED));
  EXPECT_FALSE(child_context->GetLanguageOptions().LanguageFeatureEnabled(
      FEATURE_TEST_IDEALLY_ENABLED_BUT_IN_DEVELOPMENT));
  // Statement deadline, if previously set on the parent, is preserved
  EXPECT_EQ(context.GetStatementEvaluationDeadline(),
            child_context->GetStatementEvaluationDeadline());
  // Session user, if previously set on the parent, is preserved
  EXPECT_EQ(context.GetSessionUser(), child_context->GetSessionUser());
  // Both parent and child context should point to the same MemoryAccountant
  // object
  EXPECT_EQ(context.memory_accountant(), child_context->memory_accountant());
}

TEST(EvaluationContext, ChildContextCalledFirstTest) {
  EvaluationContext context = EvaluationContext(EvaluationOptions());
  std::unique_ptr<EvaluationContext> child_context = context.MakeChildContext();

  // Nondeterminism is propagated upwards from child to parent
  child_context->SetNonDeterministicOutput();
  EXPECT_FALSE(child_context->IsDeterministicOutput());
  EXPECT_FALSE(context.IsDeterministicOutput());

  absl::TimeZone timezone1 = child_context->GetDefaultTimeZone();
  absl::TimeZone timezone2 = context.GetDefaultTimeZone();
  EXPECT_EQ(timezone1, timezone2);

  int64_t ts1 = child_context->GetCurrentTimestamp();
  int64_t ts2 = context.GetCurrentTimestamp();
  EXPECT_EQ(ts1, ts2);
}

TEST(EvaluationContext, ParentContextCalledFirstTest) {
  EvaluationContext context = EvaluationContext(EvaluationOptions());
  std::unique_ptr<EvaluationContext> child_context = context.MakeChildContext();

  // Non-determinism is not propagated downward by default when set on parent
  // context. We need non-determinism to propagate upwards in the case of
  // SQL defined function bodies which use a child context, by contrast
  // propagating downwards doesn't matter as much.
  context.SetNonDeterministicOutput();
  EXPECT_FALSE(context.IsDeterministicOutput());
  EXPECT_TRUE(child_context->IsDeterministicOutput());

  absl::TimeZone timezone1 = context.GetDefaultTimeZone();
  absl::TimeZone timezone2 = child_context->GetDefaultTimeZone();
  EXPECT_EQ(timezone1, timezone2);

  int64_t ts1 = context.GetCurrentTimestamp();
  int64_t ts2 = child_context->GetCurrentTimestamp();
  EXPECT_EQ(ts1, ts2);
}

}  // namespace
}  // namespace googlesql
