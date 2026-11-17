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

#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace googlesql {
namespace {

struct DeriveImplicitAliasTestCase {
  absl::string_view literal;
  absl::string_view expected_alias;
};

void ExpectDerivedAliases(
    const std::vector<DeriveImplicitAliasTestCase>& test_cases) {
  for (const DeriveImplicitAliasTestCase& test_case : test_cases) {
    EXPECT_EQ(DeriveImplicitAliasForStringLiteralTable(test_case.literal),
              test_case.expected_alias)
        << "literal: \"" << absl::CEscape(test_case.literal) << "\"";
  }
}

TEST(DeriveImplicitAliasForStringLiteralTableTest, SingleFiles) {
  ExpectDerivedAliases({
      {"users.csv", "users"},
      {"/cns/cell/home/dir/events.tfrecords", "events"},
      {"gs://my-bucket/orders_2024.parquet.gz", "orders_2024"},
      {"logs.tfrecord.zst", "logs"},
      {"events", "events"},
      // Case is preserved.
      {"Users.CSV", "Users"},
      {"EVENTS", "EVENTS"},
  });
}

TEST(DeriveImplicitAliasForStringLiteralTableTest, SeparatorsAndRelativePaths) {
  ExpectDerivedAliases({
      // Repeated and trailing separators are skipped.
      {"dir/events/", "events"},
      {"dir/events//", "events"},
      {"a//b//events.csv", "events"},
      // `.` and `..` segments are not resolved; only the basename matters.
      {"./users.csv", "users"},
      {"../data/users.csv", "users"},
      {"a/../users.csv", "users"},
      {"a/./users.csv", "users"},
      // Backslashes are separators too, and can be mixed with slashes.
      {"C:\\data\\logs\\events.csv", "events"},
      {"\\\\fileserver\\share\\daily-metrics.csv", "daily-metrics"},
      {"a\\b/c\\events.csv", "events"},
      // URI schemes are ordinary segments.
      {"gs://bucket/users.csv", "users"},
      {"file:///tmp/users.csv", "users"},
  });
}

TEST(DeriveImplicitAliasForStringLiteralTableTest, ShardSuffixes) {
  ExpectDerivedAliases({
      {"/cns/cell/logs/events@100.tfrecords", "events"},
      {"/cns/cell/logs/events@*.tfrecords", "events"},
      {"events@100", "events"},
      {"events.tfrecords@100", "events"},
      {"events-00000-of-00010.csv", "events"},
      {"events-*-of-*.csv", "events"},
      // Only one trailing shard suffix is stripped.
      {"events@1@2.csv", "events@1"},
      // Not shard suffixes, so they're kept.
      {"events-2024.csv", "events-2024"},
      {"events-of-1.csv", "events-of-1"},
      {"events-1-of-.csv", "events-1-of-"},
      {"events@.csv", "events@"},
      {"events@1a.csv", "events@1a"},
      {"user@example.com.csv", "user@example"},
  });
}

TEST(DeriveImplicitAliasForStringLiteralTableTest, Globs) {
  ExpectDerivedAliases({
      // A prefix glob uses the prefix, without trailing '_' or '-'.
      {"data/rpc_events_*.tfrecords", "rpc_events"},
      {"events_{a,b}.csv", "events"},
      {"events?.csv", "events"},
      {"events[0-9].csv", "events"},
      {"events__-*.csv", "events"},
      // A shard '@' followed by a wildcard other than '*' is trimmed too.
      {"events@??.csv", "events"},
      {"events@[0-9].csv", "events"},
      {"events@{1,2}.csv", "events"},
      // A basename starting with a wildcard uses the parent directory.
      {"/cns/cell/logs/orders/*.tfrecords", "orders"},
      {"/cns/cell/logs/orders/{a,b}.csv", "orders"},
      {"/cns/cell/logs/orders/[ab].csv", "orders"},
      {"/cns/cell/logs/orders/?.csv", "orders"},
      {"/cns/cell/logs/orders/*", "orders"},
      {"orders/*/", "orders"},
      // The parent directory gets the same extension and shard stripping.
      {"/cns/cell/logs/orders.v2/*.csv", "orders"},
      {"/cns/cell/logs/orders@10/*", "orders"},
      // Brace groups and wildcards in other directories don't matter.
      {"/cns/cell/logs/{2025,2026}/events.tfrecords", "events"},
      {"/cns/cell/{a,b}/logs/{2025,2026}/events-*.tfrecords", "events"},
      {"/cns/cell/logs/*/events.tfrecords", "events"},
  });
}

TEST(DeriveImplicitAliasForStringLiteralTableTest, NamesThatNeedBackticks) {
  // Any non-empty stem is used, even if it needs backticks as an identifier.
  ExpectDerivedAliases({
      // Leading digits, from file names and from parent directories.
      {"logs/2026-09-24.csv", "2026-09-24"},
      {"/cns/cell/logs/2026-09-24/*.tfrecords", "2026-09-24"},
      {"/cns/cell/_tmp/*.tfrecords", "_tmp"},
      // Spaces, quotes and `$` after the first character.
      {"my events.csv", "my events"},
      {"/cns/cell/home/o'brien/it's \"v2\".csv", "it's \"v2\""},
      {"a$b.csv", "a$b"},
      // UTF-8 is passed through unchanged.
      {"数据.csv", "数据"},
      {"/données/événements@3.csv", "événements"},
      {"Ünïcödé-*.csv", "Ünïcödé"},
      {"emoji_😀.csv", "emoji_😀"},
      // Control characters are ordinary characters.
      {"tab\tname.csv", "tab\tname"},
      {"line\nbreak.csv", "line\nbreak"},
      {absl::string_view("nul\000x.csv", 9), absl::string_view("nul\000x", 5)},
  });
}

TEST(DeriveImplicitAliasForStringLiteralTableTest, AnonymousMultiplePaths) {
  ExpectDerivedAliases({
      {"sales.csv,returns.csv", ""},
      {"events_{a,b}.csv,other.csv", ""},
      {",users.csv", ""},
      {"users.csv,", ""},
      {"a,{b}.csv", ""},
      // A brace pattern spanning path segments is also a list of paths.
      {"{a/b,c}.csv", ""},
      {"{x/a,b/*.csv", ""},
      {"{c,a/b}.csv", ""},
      {"{a,b/c}.csv", ""},
      {"{a/b,c/d}.csv", ""},
      {"events_{c,a/b}.csv", ""},
      {"{dir/sales.csv,returns.csv}", ""},
      {"{b,x/a/*.csv", ""},
      {"{a\\b,c}.csv", ""},
      // So is a brace group spanning directories, even without a comma.
      {"{a/b}/users.csv", ""},
      // A brace group that's never closed and contains a comma.
      {"/data/{users.csv,other.csv", ""},
      {"events_{a,{b}.csv", ""},
  });
}

TEST(DeriveImplicitAliasForStringLiteralTableTest, AnonymousNoFixedName) {
  ExpectDerivedAliases({
      // The parent directory has wildcards, or there isn't one.
      {"/cns/cell/logs/*/*.tfrecords", ""},
      {"gs://*.csv", ""},
      // Only the immediate parent is used, even if it's empty.
      {"/x/orders//*.csv", ""},
      // A URI scheme or drive letter isn't a name.
      {"gs:/*.csv", ""},
      {"C:\\*.csv", ""},
      {"C:/*.csv", ""},
      {"/*", ""},
      {"*", ""},
      {"*.csv", ""},
      {"{a,b}.csv", ""},
      // Empty stems.
      {"", ""},
      {"/", ""},
      {"///", ""},
      {"\\", ""},
      {"data/.hidden", ""},
      {".csv", ""},
      {"data/..", ""},
      {"data/.", ""},
      {"..", ""},
      {"@100.tfrecords", ""},
      {"-00000-of-00010.csv", ""},
      {"_*.csv", ""},
      {"-*.csv", ""},
      // A stem starting with `$` would be an internal alias.
      {"$tmp.csv", ""},
      {"/data/$tmp/*.csv", ""},
  });
}

TEST(DeriveImplicitAliasForStringLiteralTableTest, MalformedPatterns) {
  // Unbalanced braces and brackets are still treated as wildcards.
  ExpectDerivedAliases({
      {"events_{a.csv", "events"},
      {"events}.csv", "events"},
      {"events_[a.csv", "events"},
      {"events].csv", "events"},
      {"]x.csv", ""},
      {"}{,}.csv", ""},
      {"{users.csv", ""},
  });
}

TEST(DeriveImplicitAliasForStringLiteralTableTest, LongInputs) {
  std::string deep_path;
  for (int i = 0; i < 10000; ++i) {
    absl::StrAppend(&deep_path, "dir", i, "/");
  }
  EXPECT_EQ(DeriveImplicitAliasForStringLiteralTable(
                absl::StrCat(deep_path, "events.csv")),
            "events");
  EXPECT_EQ(DeriveImplicitAliasForStringLiteralTable(
                absl::StrCat(deep_path, "*.csv")),
            "dir9999");

  const std::string long_name(100000, 'a');
  EXPECT_EQ(DeriveImplicitAliasForStringLiteralTable(
                absl::StrCat("/", long_name, "@1.csv")),
            long_name);
  EXPECT_EQ(DeriveImplicitAliasForStringLiteralTable(std::string(100000, ',')),
            "");
  EXPECT_EQ(DeriveImplicitAliasForStringLiteralTable(std::string(100000, '{')),
            "");
}

}  // namespace
}  // namespace googlesql
