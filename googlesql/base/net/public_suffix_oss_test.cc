//
// Copyright 2020 Google LLC
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

#include "googlesql/base/net/public_suffix_oss.h"
#include <vector>

#include "gtest/gtest.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/net/idn_oss.h"
#include "googlesql/base/net/public_suffix_test_psl.h"

namespace googlesql::internal {
namespace {

class PublicSuffixRulesTest : public ::testing::Test {
 protected:
  const PublicSuffixRules& Build() {
    rules_.reset(builder_.Build());
    return *rules_;
  }

  PublicSuffixRulesBuilder builder_;
  std::unique_ptr<PublicSuffixRules> rules_;
};

TEST_F(PublicSuffixRulesTest, Empty) {
  builder_.AddRules("");
  EXPECT_TRUE(Build().IsEmpty());
}

TEST_F(PublicSuffixRulesTest, EmptyLine) {
  builder_.AddRules("aa\n"
                    "\n"
                    "bb\n");
  absl::flat_hash_set<std::string> expected_rules = {"aa", "bb"};
  EXPECT_EQ(expected_rules, Build().icann_domains());
}

TEST_F(PublicSuffixRulesTest, NewLines) {
  // Test LF, CR, CRLF and EOF.
  builder_.AddRules("aa\n"
                    "bb\r"
                    "cc\r\n"
                    "dd");
  absl::flat_hash_set<std::string> expected_rules = {"aa", "bb", "cc", "dd"};
  EXPECT_EQ(expected_rules, Build().icann_domains());
}

TEST_F(PublicSuffixRulesTest, CommentsAndLoneSlashes) {
  // Test // comments and lone /.
  builder_.AddRules("//aa\n"
                    "bb//cc\n"
                    "/dd\n"
                    "ee/ff\n");
  absl::flat_hash_set<std::string> expected_rules = {"bb", "ee"};
  EXPECT_EQ(expected_rules, Build().icann_domains());
}

TEST_F(PublicSuffixRulesTest, WhiteSpace) {
  // Test Space and Tab.
  builder_.AddRules("aa \n"
                    "bb\t\n"
                    " cc\n"
                    "\tdd\n");
  absl::flat_hash_set<std::string> expected_rules = {"aa", "bb"};
  EXPECT_EQ(expected_rules, Build().icann_domains());
}

TEST_F(PublicSuffixRulesTest, LeadingDot) {
  builder_.AddRules(".foo.bar");
  EXPECT_TRUE(Build().IsEmpty());
}

TEST_F(PublicSuffixRulesTest, TrailingDot) {
  builder_.AddRules("foo.bar.");
  absl::flat_hash_set<std::string> expected_rules = {"foo.bar"};
  EXPECT_EQ(expected_rules, Build().icann_domains());
}

TEST_F(PublicSuffixRulesTest, AdjacentDots) {
  builder_.AddRules("foo..bar");
  EXPECT_TRUE(Build().IsEmpty());
}

TEST_F(PublicSuffixRulesTest, IntlDomainNameWithException) {
  builder_.AddRules("!\u0440\u0444");
  absl::flat_hash_set<std::string> expected_rules = {"!xn--p1ai"};
  EXPECT_EQ(expected_rules, Build().icann_domains());
}

TEST_F(PublicSuffixRulesTest, IllegalIntlDomainNameWithException) {
  builder_.AddRules("!.");
  EXPECT_TRUE(Build().IsEmpty());
}

TEST_F(PublicSuffixRulesTest, IntlDomainNameWithTransitionalCharacter) {
  builder_.AddRules("\u00DF");
  absl::flat_hash_set<std::string> expected_rules = {"xn--zca"};
  EXPECT_EQ(expected_rules, Build().icann_domains());
}

TEST_F(PublicSuffixRulesTest, AllowedWildcard) {
  builder_.AddRules("*.foo.bar\n"
                    "foo.bar\n");
  absl::flat_hash_set<std::string> expected_rules = {"*.foo.bar", "foo.bar"};
  EXPECT_EQ(expected_rules, Build().icann_domains());
}

TEST(GetTopPrivateDomain, SampleTests) {
  std::vector<std::string> v = absl::StrSplit(googlesql::internal::kTestPsl,
                                              '\n', absl::SkipWhitespace());
  for (absl::string_view line : v) {
    line = absl::StripAsciiWhitespace(line);

    // Ignore comments that start with "//".
    if (absl::StartsWith(line, "//")) {
      continue;
    }

    // Lines look like this:
    //   checkPublicSuffix('xn--fiqs8s', null);

    EXPECT_TRUE(absl::ConsumePrefix(&line, "checkPublicSuffix("));
    EXPECT_TRUE(absl::ConsumeSuffix(&line, ");"));

    const size_t comma = line.find(',');
    EXPECT_NE(absl::string_view::npos, comma);
    EXPECT_LT(0, comma);
    EXPECT_GT(line.size() - 1, comma);
    EXPECT_EQ(comma, line.rfind(','));

    absl::string_view in = line.substr(0, comma);
    absl::string_view out = absl::ClippedSubstr(line, comma + 1);

    in = absl::StripAsciiWhitespace(in);
    out = absl::StripAsciiWhitespace(out);

    if (in == "null") {
      in = absl::string_view();
    }
    if (out == "null") {
      out = absl::string_view();
    }
    absl::ConsumePrefix(&in, "'");
    absl::ConsumeSuffix(&in, "'");
    absl::ConsumePrefix(&out, "'");
    absl::ConsumeSuffix(&out, "'");

    if (absl::EndsWith(in, "example.example")) {
      // Skip unknown domains, we don't support them
      continue;
    }
    if (absl::EndsWith(in, "uk.com")) {
      // Skip private domains, we don't support them
      continue;
    }

    std::string in_str;
    if (!in.empty() && !ToASCII(in, &in_str)) {
      // When the input starts with '.', ToASCII fails.
      // The test cases have "null" in those cases, so check for that here.
      EXPECT_TRUE(out.empty());
      continue;
    }

    std::string out_str;
    if (!out.empty()) {
      EXPECT_TRUE(ToASCII(out, &out_str)) << out;
    }

    EXPECT_EQ(out_str, GetTopPrivateDomain(in_str)) << in_str;

    if (!out_str.empty()) {
      const auto dot = out_str.find('.');
      std::string pub =
          (dot == std::string::npos ? "" : out_str.substr(dot + 1));
      EXPECT_EQ(pub, GetPublicSuffix(in_str)) << in_str;
    }
  }
}

TEST(GetTopPrivateDomain, Dots) {
  // Trailing dot is OK
  EXPECT_EQ("foo.com.", googlesql::internal::GetTopPrivateDomain("foo.com."));

  // Leading dot is bad
  EXPECT_EQ("", googlesql::internal::GetTopPrivateDomain(".com"));

  // Leading dot is bad, but we ignore it if we have a result
  EXPECT_EQ("foo.com", googlesql::internal::GetTopPrivateDomain(".foo.com"));

  // Two adjacent dots are bad
  EXPECT_EQ("", googlesql::internal::GetTopPrivateDomain("foo..com"));

  // Two adjacent dots are bad, but we ignore them if we have a result
  EXPECT_EQ("foo.com",
            googlesql::internal::GetTopPrivateDomain("bar..foo.com"));
}

TEST(Options, UnknownTLD) {
  struct {
    const char* name;
    const char* unknown_top_priv;
    const char* unknown_pub_suf;
    const char* known_top_priv;
    const char* known_pub_suf;
  } tests[] = {
    { "",                 "",             "",         "", "" },
    { "invalid",          "",             "invalid",  "", "" },
    { "bar.invalid",      "bar.invalid",  "invalid",  "", "" },
    { "foo.bar.invalid",  "bar.invalid",  "invalid",  "", "" },
    { ".",                "",             "",         "", "" },
    { "invalid.",         "",             "invalid.", "", "" },
    { "bar.invalid.",     "bar.invalid.", "invalid.", "", "" },
    { "foo.bar.invalid.", "bar.invalid.", "invalid.", "", "" },
  };

  for (const auto& t : tests) {
    EXPECT_EQ(t.known_top_priv, GetTopPrivateDomain(t.name));
    EXPECT_EQ(t.known_pub_suf, GetPublicSuffix(t.name));
  }
}

void TestIL(const PublicSuffixRules& rules) {
  EXPECT_EQ("il", rules.GetPublicSuffix("il"));
  EXPECT_EQ("co.il", rules.GetPublicSuffix("co.il"));
  EXPECT_EQ("co.il", rules.GetPublicSuffix("foo.co.il"));
  EXPECT_EQ("", rules.GetTopPrivateDomain("il"));
  EXPECT_EQ("", rules.GetTopPrivateDomain("co.il"));
  EXPECT_EQ("foo.co.il", rules.GetTopPrivateDomain("foo.co.il"));
}

TEST_F(PublicSuffixRulesTest, Wildcard2LD) {
  builder_.AddRule("*.il", PublicSuffixRules::kIcannDomain);
  Build();
  TestIL(*rules_);
}

TEST_F(PublicSuffixRulesTest, TLDandWildcard2LD) {
  builder_.AddRule("il", PublicSuffixRules::kIcannDomain);
  builder_.AddRule("*.il", PublicSuffixRules::kIcannDomain);
  Build();
  TestIL(*rules_);
}

TEST_F(PublicSuffixRulesTest, TLDand2LD) {
  builder_.AddRule("il", PublicSuffixRules::kIcannDomain);
  builder_.AddRule("co.il", PublicSuffixRules::kIcannDomain);
  Build();
  TestIL(*rules_);
}

TEST_F(PublicSuffixRulesTest, ExceptionTLD) {
  builder_.AddRule("!youtube", PublicSuffixRules::kIcannDomain);
  Build();

  EXPECT_EQ("", rules_->GetPublicSuffix("youtube"));
  EXPECT_EQ("youtube", rules_->GetTopPrivateDomain("youtube"));
}

TEST(GetTopPrivateDomain, Private2LD) {
  // Make sure we can exclude a 2LD (blogspot.com) in the PRIVATE section.
  EXPECT_EQ("blogspot.com",
            googlesql::internal::GetTopPrivateDomain("foo.blogspot.com"));
}

TEST(GetTopPrivateDomain, Private3LD) {
  // Make sure we can exclude a 3LD (s3.amazonaws.com) in the PRIVATE section.
  EXPECT_EQ("amazonaws.com",
            googlesql::internal::GetTopPrivateDomain("foo.s3.amazonaws.com"));
}

TEST_F(PublicSuffixRulesTest, Private4LD) {
  // Make sure we can exclude a 4LD (a.b.c.com) in the PRIVATE section.
  builder_.AddRule("com", PublicSuffixRules::kIcannDomain);
  builder_.AddRule("a.b.c.com", PublicSuffixRules::kPrivateDomain);
  EXPECT_EQ("com", Build().GetPublicSuffix("a.b.c.com"));
}

TEST_F(PublicSuffixRulesTest, Lookup3LDNearPrivate3LD) {
  // Make sure a private 3LD does not affect a nearby 3LD lookup.
  builder_.AddRule("name", PublicSuffixRules::kIcannDomain);
  builder_.AddRule("forgot.her.name", PublicSuffixRules::kPrivateDomain);
  EXPECT_EQ("her.name", Build().GetTopPrivateDomain("www.her.name"));
}

TEST_F(PublicSuffixRulesTest, Lookup3LDNearWildcard2LDAndPrivate3LD) {
  // Make sure a wildcard 2LD and private 3LD do not affect a nearby 3LD lookup.
  builder_.AddRule("*.il", PublicSuffixRules::kIcannDomain);
  builder_.AddRule("blogspot.co.il", PublicSuffixRules::kPrivateDomain);
  EXPECT_EQ("foo.co.il", Build().GetTopPrivateDomain("foo.co.il"));
}

TEST_F(PublicSuffixRulesTest, TLDandWildcard3LD) {
  // Note: There is no "platform.sh" rule. Make sure this works correctly.
  builder_.AddRule("sh", PublicSuffixRules::kIcannDomain);
  builder_.AddRule("*.platform.sh", PublicSuffixRules::kPrivateDomain);
  Build();

  EXPECT_EQ("sh", rules_->GetPublicSuffix("sh"));
  EXPECT_EQ("sh", rules_->GetPublicSuffix("platform.sh"));
  EXPECT_EQ("sh", rules_->GetPublicSuffix("x.platform.sh"));
  EXPECT_EQ("sh", rules_->GetPublicSuffix("x.y.platform.sh"));
  EXPECT_EQ("sh", rules_->GetPublicSuffix("x.y.z.platform.sh"));

  EXPECT_EQ("", rules_->GetTopPrivateDomain("sh"));
  EXPECT_EQ("platform.sh", rules_->GetTopPrivateDomain("platform.sh"));
  EXPECT_EQ("platform.sh", rules_->GetTopPrivateDomain("x.platform.sh"));
  EXPECT_EQ("platform.sh", rules_->GetTopPrivateDomain("x.y.platform.sh"));
  EXPECT_EQ("platform.sh", rules_->GetTopPrivateDomain("x.y.z.platform.sh"));
}

TEST_F(PublicSuffixRulesTest, TLD2LDandWildcard3LD) {
  builder_.AddRule("jp", PublicSuffixRules::kIcannDomain);
  builder_.AddRule("kobe.jp", PublicSuffixRules::kIcannDomain);
  builder_.AddRule("*.kobe.jp", PublicSuffixRules::kIcannDomain);
  Build();

  EXPECT_EQ("jp", rules_->GetPublicSuffix("jp"));
  EXPECT_EQ("kobe.jp", rules_->GetPublicSuffix("kobe.jp"));
  EXPECT_EQ("c.kobe.jp", rules_->GetPublicSuffix("c.kobe.jp"));
  EXPECT_EQ("c.kobe.jp", rules_->GetPublicSuffix("b.c.kobe.jp"));
  EXPECT_EQ("c.kobe.jp", rules_->GetPublicSuffix("a.b.c.kobe.jp"));
  EXPECT_EQ("", rules_->GetTopPrivateDomain("jp"));
  EXPECT_EQ("", rules_->GetTopPrivateDomain("kobe.jp"));
  EXPECT_EQ("", rules_->GetTopPrivateDomain("c.kobe.jp"));
  EXPECT_EQ("b.c.kobe.jp", rules_->GetTopPrivateDomain("b.c.kobe.jp"));
  EXPECT_EQ("b.c.kobe.jp", rules_->GetTopPrivateDomain("a.b.c.kobe.jp"));
}

TEST_F(PublicSuffixRulesTest, Deep) {
  builder_.AddRule("a.b.c", PublicSuffixRules::kIcannDomain);
  Build();

  EXPECT_EQ("c", rules_->GetPublicSuffix("b.c"));
}

TEST_F(PublicSuffixRulesTest, SecondLevelDomainIsSuffixOfAnotherTLD) {
  builder_.AddRule("fit", PublicSuffixRules::kIcannDomain);
  builder_.AddRule("fitness", PublicSuffixRules::kIcannDomain);
  Build();

  EXPECT_EQ("fit", rules_->GetPublicSuffix("ness.fit"));
}

}  // namespace
}  // namespace googlesql::internal
