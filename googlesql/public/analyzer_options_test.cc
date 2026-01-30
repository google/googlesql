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

#include "googlesql/public/analyzer_options.h"

#include <memory>
#include <utility>

#include "googlesql/base/atomic_sequence_num.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"

namespace googlesql {

using ::testing::HasSubstr;
using ::absl_testing::StatusIs;

TEST(AnalyzerOptionsTest, CollateFeatureValidation) {
  AnalyzerOptions options;
  GOOGLESQL_EXPECT_OK(ValidateAnalyzerOptions(options));

  LanguageOptions language;
  language.SetEnabledLanguageFeatures({FEATURE_COLLATION_SUPPORT});
  options.set_language(language);
  EXPECT_TRUE(
      options.language().LanguageFeatureEnabled(FEATURE_COLLATION_SUPPORT));
  EXPECT_FALSE(
      options.language().LanguageFeatureEnabled(FEATURE_ANNOTATION_FRAMEWORK));
  EXPECT_THAT(ValidateAnalyzerOptions(options),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("COLLATION_SUPPORT language feature requires "
                                 "the ANNOTATION_FRAMEWORK language feature")));
}

TEST(AnalyzerOptionsTest, SetColumnIdSequenceNumber) {
  AnalyzerOptions options;
  EXPECT_EQ(options.column_id_sequence_number(), nullptr);

  googlesql_base::SequenceNumber sequence;
  options.set_column_id_sequence_number(&sequence);
  EXPECT_EQ(options.column_id_sequence_number(), &sequence);
  EXPECT_EQ(options.column_id_sequence_number()->GetNext(), 0);
}

TEST(AnalyzerOptionsTest, SetSharedColumnIdSequenceNumber) {
  AnalyzerOptions options;
  EXPECT_EQ(options.column_id_sequence_number(), nullptr);

  auto sequence = std::make_shared<googlesql_base::SequenceNumber>();
  sequence->GetNext();
  options.SetSharedColumnIdSequenceNumber(std::move(sequence));
  EXPECT_EQ(options.column_id_sequence_number()->GetNext(), 1);
}

}  // namespace googlesql
