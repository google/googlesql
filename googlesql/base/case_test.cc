//
// Copyright 2018 Google LLC
// Copyright 2017 The Abseil Authors.
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
// This file tests string processing functions related to case:
// uppercase, lowercase, etc.

#include "googlesql/base/case.h"

#include <algorithm>
#include <cstring>

#include "gtest/gtest.h"

namespace googlesql_base {

TEST(stringtest, StringCaseCompare) {
  const std::string a("abc");
  const std::string b("abC");
  ASSERT_EQ(StringCaseCompare(a, b), 0);
  ASSERT_TRUE(StringCaseEqual(a, b));
  ASSERT_FALSE(StringCaseLess()(a, b));

  const std::string c("abCa");
  ASSERT_LT(StringCaseCompare(a, c), 0);
  ASSERT_FALSE(StringCaseEqual(a, c));
  ASSERT_TRUE(StringCaseLess()(a, c));

  const std::string d("bcd");
  ASSERT_LT(StringCaseCompare(a, d), 0);
  ASSERT_FALSE(StringCaseEqual(a, d));
  ASSERT_TRUE(StringCaseLess()(a, d));

  const std::string e("aBd");
  ASSERT_LT(StringCaseCompare(a, e), 0);
  ASSERT_FALSE(StringCaseEqual(a, e));
  ASSERT_TRUE(StringCaseLess()(a, e));

  ASSERT_LT(StringCaseCompare("X_Z", "XYZ"), 0);
  ASSERT_FALSE(StringCaseEqual("X_Z", "XYZ"));
  ASSERT_TRUE(StringCaseLess()("X_Z", "XYZ"));
}

TEST(stringtest, CaseCompare) {
  const std::string a("abc");
  const std::string b("abC");
  ASSERT_EQ(CaseCompare(a, b), 0);
  ASSERT_TRUE(CaseEqual(a, b));
  ASSERT_FALSE(CaseLess()(a, b));

  const std::string c("abCa");
  ASSERT_LT(CaseCompare(a, c), 0);
  ASSERT_FALSE(CaseEqual(a, c));
  ASSERT_TRUE(CaseLess()(a, c));

  const std::string d("bcd");
  ASSERT_LT(CaseCompare(a, d), 0);
  ASSERT_FALSE(CaseEqual(a, d));
  ASSERT_TRUE(CaseLess()(a, d));

  const std::string e("aBd");
  ASSERT_LT(CaseCompare(a, e), 0);
  ASSERT_FALSE(CaseEqual(a, e));
  ASSERT_TRUE(CaseLess()(a, e));

  const std::string f("Ab");
  ASSERT_GT(CaseCompare(a, f), 0);
  ASSERT_FALSE(CaseEqual(a, f));
  ASSERT_FALSE(CaseLess()(a, f));

  static const absl::string_view g("a\0B", 3);
  static const absl::string_view h("A\0b", 3);
  ASSERT_EQ(0, CaseCompare(g, h));
  ASSERT_TRUE(CaseEqual(g, h));
  ASSERT_FALSE(CaseLess()(g, h));

  static const absl::string_view i("A\0cD", 4);
  ASSERT_LT(CaseCompare(h, i), 0);
  ASSERT_FALSE(CaseEqual(h, i));
  ASSERT_TRUE(CaseLess()(h, i));

  static const absl::string_view j("a\0c", 3);
  ASSERT_GT(CaseCompare(i, j), 0);
  ASSERT_FALSE(CaseEqual(i, j));
  ASSERT_FALSE(CaseLess()(i, j));

  static const absl::string_view k("\0a", 2);
  ASSERT_GT(CaseCompare(j, k), 0);
  ASSERT_FALSE(CaseEqual(j, k));
  ASSERT_TRUE(CaseLess()(k, j));
}

}  // namespace googlesql_base
