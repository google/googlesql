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

#include "googlesql/public/collator.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "googlesql/base/testing/status_matchers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"

namespace googlesql {
using ::absl_testing::StatusIs;
enum class CompareType { kSortKey, kCompare };

class CollatorTest : public ::testing::TestWithParam<CompareType> {
 protected:
  void TestEquals(absl::string_view s1, absl::string_view s2,
                  const GoogleSqlCollator* collator) {
    switch (GetParam()) {
      case CompareType::kCompare: {
        absl::Status error;
        EXPECT_EQ(0, collator->CompareUtf8(s1, s2, &error)) << s1 << "==" << s2;
        GOOGLESQL_EXPECT_OK(error);
        EXPECT_EQ(0, collator->CompareUtf8(s2, s1, &error)) << s2 << "==" << s1;
        GOOGLESQL_EXPECT_OK(error);
        break;
      }
      case CompareType::kSortKey: {
        absl::Cord c1, c2;
        GOOGLESQL_EXPECT_OK(collator->GetSortKeyUtf8(s1, &c1));
        GOOGLESQL_EXPECT_OK(collator->GetSortKeyUtf8(s2, &c2));
        EXPECT_EQ(c1, c2) << s1 << "==" << s2;
        break;
      }
    }
  }

  void TestLessThan(absl::string_view s1, absl::string_view s2,
                    const GoogleSqlCollator* collator) {
    switch (GetParam()) {
      case CompareType::kCompare: {
        absl::Status error;
        EXPECT_EQ(-1, collator->CompareUtf8(s1, s2, &error)) << s1 << "<" << s2;
        GOOGLESQL_EXPECT_OK(error);
        EXPECT_EQ(1, collator->CompareUtf8(s2, s1, &error)) << s1 << ">" << s2;
        GOOGLESQL_EXPECT_OK(error);
        break;
      }

      case CompareType::kSortKey: {
        absl::Cord c1, c2;
        GOOGLESQL_EXPECT_OK(collator->GetSortKeyUtf8(s1, &c1));
        GOOGLESQL_EXPECT_OK(collator->GetSortKeyUtf8(s2, &c2));
        EXPECT_LT(std::string(c1).compare(std::string(c2)), 0)
            << s1 << "<" << s2;
        break;
      }
    }
  }
};

TEST_P(CollatorTest, MakeSqlCollator) {
  const std::vector<std::string> valid_collation_names = {
      "unicode", "en", "zh-cmn", "en_US", "zh_Hans_HK", "zh_Hant_HK",
      "de@collation=phonebook", "binary",
      // Collation names are case insensitive.
      "EN", "EN_us",
      // Collation names with attributes.
      "en:ci", "en_us:cs", "zh_Hans_HK:ci"};
  for (const std::string& name : valid_collation_names) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<const GoogleSqlCollator> collator,
                         MakeSqlCollator(name));
    EXPECT_NE(nullptr, collator.get()) << "name=" << name;
  }
}

TEST_P(CollatorTest, MakeSqlCollatorErrors) {
  EXPECT_THAT(MakeSqlCollator({}), StatusIs(absl::StatusCode::kOutOfRange));
  EXPECT_THAT(MakeSqlCollator(""), StatusIs(absl::StatusCode::kOutOfRange));
  EXPECT_THAT(MakeSqlCollator(":"), StatusIs(absl::StatusCode::kOutOfRange));

  EXPECT_THAT(MakeSqlCollator(":cs"), StatusIs(absl::StatusCode::kOutOfRange));
  EXPECT_THAT(MakeSqlCollator("binary:cs"),
              StatusIs(absl::StatusCode::kOutOfRange));
  EXPECT_THAT(MakeSqlCollator("binary:ci"),
              StatusIs(absl::StatusCode::kOutOfRange));

  EXPECT_THAT(MakeSqlCollator("en_US:nonsense_case"),
              StatusIs(absl::StatusCode::kOutOfRange));
  EXPECT_THAT(MakeSqlCollator("en_US:cs:extra_attr"),
              StatusIs(absl::StatusCode::kOutOfRange));
}

TEST_P(CollatorTest, Comparison) {
  std::unique_ptr<const GoogleSqlCollator> collator;

  // Comparison with "unicode" collation. Is same as the comparison with no
  // collation.
  for (auto name : {"unicode", "binary"}) {
    collator = MakeSqlCollator(name).value();
    ASSERT_NE(collator.get(), nullptr);
    TestEquals("\u205abc", "\u205abc", collator.get());
    TestLessThan("", "a", collator.get());
    TestLessThan("B", "a", collator.get());
    TestLessThan("a", "aa", collator.get());
    TestLessThan("@", "a", collator.get());
    TestLessThan("Case sensitive", "case sensitive", collator.get());
  }
  // Comparison with "unicode:ci" collation.
  collator = MakeSqlCollator("unicode:ci").value();
  ASSERT_NE(collator.get(), nullptr);
  TestLessThan("@", "a", collator.get());
  TestEquals("Case sensitive", "case sensitive", collator.get());
  // Greek.
  TestEquals(absl::StrFormat("%c%c%c", 0xCE, 0x86, 'h'),  // Άh
             absl::StrFormat("%c%c%c", 0xCE, 0xAC, 'h'),  // άh
             collator.get());

  // Comparison with "unicode:cs" collation.
  collator = MakeSqlCollator("unicode:cs").value();
  ASSERT_NE(collator.get(), nullptr);
  TestLessThan("Case sensitive", "case sensitive", collator.get());
  // Greek.
  TestLessThan(absl::StrFormat("%c%c%c", 0xCE, 0x86, 'h'),  // Άh
               absl::StrFormat("%c%c%c", 0xCE, 0xAC, 'h'),  // άh
               collator.get());

  // Comparison with "en_US" collation.
  collator = MakeSqlCollator("en_US").value();
  ASSERT_NE(nullptr, collator.get());
  TestEquals("hello", "hello", collator.get());
  TestLessThan("", "a", collator.get());
  TestLessThan("a", "B", collator.get());
  TestLessThan("a", "aa", collator.get());
  TestLessThan("@", "a", collator.get());
  TestLessThan("case sensitive", "Case sensitive", collator.get());

  // Comparison with "en_US:ci" collation.
  collator = MakeSqlCollator("en_US:ci").value();
  ASSERT_NE(collator.get(), nullptr);
  TestEquals("case sensitive", "Case sensitive", collator.get());

  // Comparison with "en_US:cs" collation.
  collator = MakeSqlCollator("en_US:cs").value();
  ASSERT_NE(collator.get(), nullptr);
  TestLessThan("case sensitive", "Case sensitive", collator.get());

  // Comparison with "cs" collation. cs is Czech. In Czech "ch" is considered
  // as a single character and comes after h.
  collator = MakeSqlCollator("cs").value();
  ASSERT_NE(nullptr, collator.get());
  TestLessThan("h", "ch", collator.get());
  TestLessThan("ci", "h", collator.get());
  TestLessThan("ci", "ch", collator.get());

  // Comparison with "de:ci" collation. de is German.
  collator = MakeSqlCollator("de:ci").value();
  ASSERT_NE(collator.get(), nullptr);
  TestEquals("Ä", "ä", collator.get());

  // Comparison with "de:cs" collation. de is German.
  collator = MakeSqlCollator("de:cs").value();
  ASSERT_NE(collator.get(), nullptr);
  TestLessThan("ä", "Ä", collator.get());

  // Very long string.
  collator = MakeSqlCollator("en_US").value();
  for (int32_t length = 100; length < 10000; ++length) {
    TestEquals(std::string(length, 'a'), std::string(length, 'a'),
               collator.get());
    TestLessThan(std::string(length, 'a'), std::string(length + 1, 'a'),
                 collator.get());
  }
}

INSTANTIATE_TEST_SUITE_P(CollatorTest, CollatorTest,
                         ::testing::Values(
                             CompareType::kSortKey,
                             CompareType::kCompare));

void TestLegacyUnicodeModeIsConstructErr(absl::string_view collation_name,
                                         CollatorLegacyUnicodeMode mode) {
  absl::StatusOr<std::unique_ptr<const GoogleSqlCollator>> collator =
      MakeSqlCollator(collation_name, mode);
  EXPECT_EQ(collator.ok(), false) << collator.status();
}

void TestLegacyUnicodeMode(absl::string_view collation_name,
                           CollatorLegacyUnicodeMode mode,
                           bool expect_is_binary, bool expect_has_icu,
                           bool expect_collated_compare,
                           bool expect_sort_key_ok) {
  absl::StatusOr<std::unique_ptr<const GoogleSqlCollator>> collator =
      MakeSqlCollator(collation_name, mode);
  GOOGLESQL_EXPECT_OK(collator);
  EXPECT_EQ(expect_is_binary, (*collator)->IsBinaryComparison());
  EXPECT_EQ(expect_has_icu, (*collator)->GetIcuCollator() != nullptr);

  absl::Status status;
  if (expect_collated_compare) {
    EXPECT_GT(0, (*collator)->CompareUtf8("ä", "z", &status));
  } else {
    EXPECT_LT(0, (*collator)->CompareUtf8("ä", "z", &status));
  }
  GOOGLESQL_EXPECT_OK(status);

  absl::Cord ignore;
  absl::Status sort_key_status = (*collator)->GetSortKeyUtf8("", &ignore);
  EXPECT_EQ(expect_sort_key_ok, sort_key_status.ok()) << sort_key_status;
  if (expect_sort_key_ok) {
    absl::Cord a_umlaut_sort_key;
    absl::Cord z_sort_key;
    GOOGLESQL_ASSERT_OK((*collator)->GetSortKeyUtf8("ä", &a_umlaut_sort_key));
    GOOGLESQL_ASSERT_OK((*collator)->GetSortKeyUtf8("z", &z_sort_key));
    if (expect_has_icu) {
      EXPECT_GT(0, a_umlaut_sort_key.Compare(z_sort_key));
    } else {
      EXPECT_LT(0, a_umlaut_sort_key.Compare(z_sort_key));
    }
  }
}

TEST_F(CollatorTest, LegacyUnicodeModes) {
  constexpr bool kIsBinary = true;
  constexpr bool kNotBinary = false;
  constexpr bool kHasIcu = true;
  constexpr bool kNoIcu = false;
  constexpr bool kCollatedCompare = true;
  constexpr bool kCodepointCompare = false;
  constexpr bool kSortKeyOk = true;

  auto TestIsErr = TestLegacyUnicodeModeIsConstructErr;
  auto Test = TestLegacyUnicodeMode;
  // clang-format off
  // binary is supported by everything.
  Test("binary", CollatorLegacyUnicodeMode::kError,
       kIsBinary, kNoIcu, kCodepointCompare, kSortKeyOk);
  Test("binary", CollatorLegacyUnicodeMode::kLegacyIcuOnly,
       kIsBinary, kNoIcu, kCodepointCompare, kSortKeyOk);


  // unicode is always the same as binary, except for kError
  TestIsErr("unicode", CollatorLegacyUnicodeMode::kError);
  Test("unicode", CollatorLegacyUnicodeMode::kLegacyIcuOnly,
       kIsBinary, kNoIcu, kCodepointCompare, kSortKeyOk);


  // This is a synonym for 'unicode' in every way.
  TestIsErr("unicode:cs", CollatorLegacyUnicodeMode::kError);
  Test("unicode:cs", CollatorLegacyUnicodeMode::kLegacyIcuOnly,
       kIsBinary, kNoIcu, kCodepointCompare, kSortKeyOk);


  // This is the weird case
  TestIsErr("unicode:ci", CollatorLegacyUnicodeMode::kError);
  Test("unicode:ci", CollatorLegacyUnicodeMode::kLegacyIcuOnly,
       kNotBinary, kHasIcu, kCollatedCompare, kSortKeyOk);


  // Normal case, should perform the same in all cases
  Test("und", CollatorLegacyUnicodeMode::kError,
       kNotBinary, kHasIcu, kCollatedCompare, kSortKeyOk);
  Test("und", CollatorLegacyUnicodeMode::kLegacyIcuOnly,
       kNotBinary, kHasIcu, kCollatedCompare, kSortKeyOk);

  // clang-format on
}

}  // namespace googlesql
