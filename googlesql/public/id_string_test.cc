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

#include "googlesql/public/id_string.h"

#include <algorithm>
#include <cstddef>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include "googlesql/common/unicode_utils.h"
#include "googlesql/base/case.h"
#include "gtest/gtest.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/node_hash_set.h"
#include "absl/flags/flag.h"
#include "absl/hash/hash.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/map_util.h"

namespace googlesql {
namespace {
// Shorthand to construct (and leak) a global IdString.
static IdString ID(absl::string_view str) { return IdString::MakeGlobal(str); }

TEST(IdString, Test) {
  IdString empty;
  IdString empty_str = ID(std::string());

  EXPECT_EQ("", empty.ToString());
  EXPECT_EQ("", empty_str.ToString());
  EXPECT_TRUE(empty == empty_str);

  EXPECT_EQ(0, empty.size());
  EXPECT_EQ(0, empty_str.size());

  const IdString s1 = ID(std::string("s1"));  // NOLINT: using string on purpose
  EXPECT_EQ("s1", s1.ToString());

  const IdString s1_copy = s1;
  EXPECT_EQ("s1", s1_copy.ToString());
  EXPECT_TRUE(s1 == s1_copy);

  const IdString s1_alt =
      ID(std::string("s1"));  // NOLINT: using string on purpose
  EXPECT_TRUE(s1 == s1_alt);

  EXPECT_EQ(2, s1.size());

  const IdString abcde = ID(absl::string_view("abcde"));
  EXPECT_TRUE(ID("abcde") == abcde);
  EXPECT_FALSE(s1 == abcde);
  EXPECT_FALSE(empty == abcde);

  EXPECT_EQ("bcd", abcde.substr(1, 3));

  char buf[10] = {0};
  EXPECT_EQ(3, abcde.copy(buf, 3));
  EXPECT_EQ(std::string("abc"), buf);

  EXPECT_EQ(2, abcde.copy(buf, 3, 3));
  // "de" got copied over "abc", with no zero-terminator.
  EXPECT_EQ(std::string("dec"), buf);

  const IdString values[] = {empty, abcde, s1, s1_alt};
  for (const IdString v1 : values) {
    for (const IdString v2 : values) {
      EXPECT_EQ(v1.ToString() < v2.ToString(), v1 < v2);
      EXPECT_EQ(v1.ToString() == v2.ToString(), v1 == v2);
    }
  }

  IdString copy = ID("copy");
  IdString copy2 = copy;
  EXPECT_EQ("copy", copy.ToString());
  EXPECT_EQ("copy", copy2.ToString());
  copy = abcde;
  EXPECT_EQ("abcde", copy.ToString());
  EXPECT_EQ("copy", copy2.ToString());

  copy.clear();
  EXPECT_EQ("", copy.ToString());
  EXPECT_EQ("copy", copy2.ToString());
}

// Test cases for IdString comparison. In the tests below, each of the strings
// here is compared to every other string, both using IdString and a "standard"
// comparison. The strings are chosen to exercise combinations of various
// properties:
// - Strings that are different only in case
// - Strings that are prefixes of other strings
// - Strings that are 0, 1, 2, 3, or 4 64-bit words. (The implementation has
//   special "fast" cases for 1, 2, and 3 words, so we have to use 4 words to
//   test the generic path.)
// - In which word the first difference occurs.
// - Strings that are an exact multiple of 8 bytes versus not.
std::vector<std::string> GetComparisonTestCases() {
  static const char base_text[] = "abcdefghijklmnopqrstuvwxyzabcdefghij";

  // As the basis for all test cases, generate strings of a particular length in
  // words, plus strings that are three bytes longer.
  std::vector<std::string> test_cases;
  for (int length_words = 0; length_words <= 4; ++length_words) {
    for (int extra_bytes = 0; extra_bytes <= 3; extra_bytes += 3) {
      const std::string base(base_text, length_words * 8 + extra_bytes);
      test_cases.push_back(base);

      // Create cases with a different byte. The location of the difference is
      // always the second byte in a word. That exists in every word in every
      // base case because partial words are always of length 3.
      for (int difference_word = 0;
           difference_word < length_words + (extra_bytes > 0 ? 1 : 0);
           ++difference_word) {
        std::string different = base;
        // Difference only in case.
        different[difference_word * 8 + 1] =
            absl::ascii_toupper(different[difference_word * 8 + 1]);
        test_cases.push_back(different);
        // Non-alpha character difference. These are chosen to be higher than
        // all alpha characters (~), in between the uppercase and lowercase
        // characters (^) and lower than all alpha characters (#).
        different[difference_word * 8 + 1] = '~';
        test_cases.push_back(different);
        different[difference_word * 8 + 1] = '^';
        test_cases.push_back(different);
        different[difference_word * 8 + 1] = '#';
        test_cases.push_back(different);
      }
    }
  }

  const int num_original_test_cases = test_cases.size();
  for (int i = 0; i < num_original_test_cases; ++i) {
    const std::string base_case = test_cases[i];
    // Create versions with an extra suffix of length 3 and 5. If "+0" indicates
    // the number of trailing bytes after the set of complete words, for "+0"
    // base cases this will add +3 and +5 cases, leading to comparisons between
    // "rounded" +0 strings and strings with trailing bytes, as well as
    // comparisons between multiple strings with trailing bytes. For test cases
    // that start with +3 trailing bytes, it will create comparisons between
    // +3 and +6, and +3 and +8 (another whole word size).
    test_cases.push_back(absl::StrCat(base_case, "abc"));
    test_cases.push_back(absl::StrCat(base_case, "abcde"));

    // Also test with trailing NUL bytes. This matches the padding that we add.
    test_cases.push_back(
        absl::StrCat(base_case, absl::string_view("\0\0\0", 3)));
    test_cases.push_back(
        absl::StrCat(base_case, absl::string_view("\0\0\0\0\0", 5)));
    test_cases.push_back(
        absl::StrCat(base_case, absl::string_view("\0\0\0\0\0\0\0\0", 8)));
  }
  return test_cases;
}

TEST(IdString, Equals) {
  IdStringPool pool;

  std::vector<std::string> test_cases = GetComparisonTestCases();

  // Make two IdString copies of the test cases. This ensures that even when we
  // compare a string to itself, it will be using two different IdString
  // instances.
  std::vector<IdString> test_id_string1;
  std::vector<IdString> test_id_string2;
  for (int i = 0; i < test_cases.size(); ++i) {
    test_id_string1.push_back(pool.Make(test_cases[i]));
    test_id_string2.push_back(pool.Make(test_cases[i]));
  }

  for (int i = 0; i < test_cases.size(); ++i) {
    // Self equality with shortcut.
    EXPECT_TRUE(test_id_string1[i].Equals(test_id_string1[i]));

    for (int j = 0; j < test_cases.size(); ++j) {
      EXPECT_EQ(test_id_string1[i].Equals(test_id_string2[j]),
                test_cases[i] == test_cases[j])
          << "input1 = " << test_cases[i] << ", input2 = " << test_cases[j];
    }
  }
}

TEST(IdString, CaseEquals) {
  IdStringPool pool;

  std::vector<std::string> test_cases = GetComparisonTestCases();

  // Make two IdString copies of the test cases. This ensures that even when we
  // compare a string to itself, it will be using two different IdString
  // instances.
  std::vector<IdString> test_id_string1;
  std::vector<IdString> test_id_string2;
  for (int i = 0; i < test_cases.size(); ++i) {
    test_id_string1.push_back(pool.Make(test_cases[i]));
    test_id_string2.push_back(pool.Make(test_cases[i]));
  }

  for (int i = 0; i < test_cases.size(); ++i) {
    // Self equality with shortcut.
    EXPECT_TRUE(test_id_string1[i].CaseEquals(test_id_string1[i]));

    for (int j = 0; j < test_cases.size(); ++j) {
      EXPECT_EQ(test_id_string1[i].CaseEquals(test_id_string2[j]),
                googlesql_base::CaseEqual(test_cases[i], test_cases[j]))
          << "input1 = " << test_cases[i] << ", input2 = " << test_cases[j];
    }
  }
}

TEST(IdString, LessThan) {
  IdStringPool pool;

  std::vector<std::string> test_cases = GetComparisonTestCases();

  // Make two IdString copies of the test cases. This ensures that even when we
  // compare a string to itself, it will be using two different IdString
  // instances.
  std::vector<IdString> test_id_string1;
  std::vector<IdString> test_id_string2;
  for (int i = 0; i < test_cases.size(); ++i) {
    test_id_string1.push_back(pool.Make(test_cases[i]));
    test_id_string2.push_back(pool.Make(test_cases[i]));
  }

  for (int i = 0; i < test_cases.size(); ++i) {
    // Self inequality with shortcut.
    EXPECT_FALSE(test_id_string1[i].LessThan(test_id_string1[i]));

    for (int j = 0; j < test_cases.size(); ++j) {
      EXPECT_EQ(test_id_string1[i].LessThan(test_id_string2[j]),
                test_cases[i] < test_cases[j])
          << "input1 = " << test_cases[i] << ", input2 = " << test_cases[j];
    }
  }
}

TEST(IdString, CaseLessThan) {
  IdStringPool pool;

  std::vector<std::string> test_cases = GetComparisonTestCases();

  // Make two IdString copies of the test cases. This ensures that even when we
  // compare a string to itself, it will be using two different IdString
  // instances.
  std::vector<IdString> test_id_string1;
  std::vector<IdString> test_id_string2;
  for (int i = 0; i < test_cases.size(); ++i) {
    test_id_string1.push_back(pool.Make(test_cases[i]));
    test_id_string2.push_back(pool.Make(test_cases[i]));
  }

  for (int i = 0; i < test_cases.size(); ++i) {
    // Self inequality with shortcut.
    EXPECT_FALSE(test_id_string1[i].CaseLessThan(test_id_string1[i]));

    for (int j = 0; j < test_cases.size(); ++j) {
      EXPECT_EQ(
          test_id_string1[i].CaseLessThan(test_id_string2[j]),
          googlesql_base::CaseLess()(test_cases[i], test_cases[j]))
          << "input1 = " << test_cases[i] << ", input2 = " << test_cases[j];
    }
  }
}

TEST(IdString, Equals_Unicode) {
  absl::SetFlag(&FLAGS_googlesql_idstring_allow_unicode_characters, true);
  IdStringPool pool;

  // Test cases covers
  // - Unicode strings that are different only in case
  // - Unicode strings that are different both in case and encoding
  // - Long unicode strings that has one different character in case
  // - Unicode strings that have different trailing paddings
  std::vector<std::string> test_string_1 = {"å", "ô", "𐌰𐌰𐌰𐌰ô𐌰𐌰𐌰𐌰",
                                            "𐌰𐌰𐌰𐌰ô𐌰𐌰𐌰𐌰\0\0\0"};
  std::vector<std::string> test_string_2 = {"Å", "O\u0302", "𐌰𐌰𐌰𐌰Ô𐌰𐌰𐌰𐌰",
                                            "𐌰𐌰𐌰𐌰Ô𐌰𐌰𐌰𐌰\0\0\0\0\0"};

  for (int i = 0; i < test_string_1.size(); ++i) {
    IdString id_string_1 = pool.Make(test_string_1[i]);
    IdString id_string_2 = pool.Make(test_string_2[i]);
    EXPECT_FALSE(id_string_1.Equals(id_string_2));
  }
}

TEST(IdString, CaseEquals_Unicode) {
  absl::SetFlag(&FLAGS_googlesql_idstring_allow_unicode_characters, true);
  IdStringPool pool;

  // Test cases covers
  // - Unicode strings that are different only in case
  // - Unicode strings that are different both in case and encoding
  // - Unicode strings that are the same
  // - Long unicode strings that has one different character in case
  // - Unicode strings that have different trailing paddings
  // - Unicode strings with character expansion like ß => ss
  std::vector<std::string> test_string_1 = {
      "å", "ô", "学", "𐌰𐌰𐌰𐌰ô𐌰𐌰𐌰𐌰", "𐌰𐌰𐌰𐌰ô𐌰𐌰𐌰𐌰\0\0\0", "ßſA"};
  std::vector<std::string> test_string_2 = {
      "Å", "O\u0302", "学", "𐌰𐌰𐌰𐌰Ô𐌰𐌰𐌰𐌰", "𐌰𐌰𐌰𐌰Ô𐌰𐌰𐌰𐌰\0\0\0\0\0", "sssa"};

  for (int i = 0; i < test_string_1.size(); ++i) {
    IdString id_string_1 = pool.Make(test_string_1[i]);
    IdString id_string_2 = pool.Make(test_string_2[i]);
    EXPECT_TRUE(id_string_1.CaseEquals(id_string_2));
  }
}

TEST(IdString, CaseEquals_InvalidUTF8) {
  // Test cases cover two ill-formed UTF-8 strings, in 1 byte and 4
  // bytes. Why they are ill-formed can be found in
  // https://en.wikipedia.org/wiki/UTF-8#Encoding. This test verifies that they
  // can be handled correctly.
  absl::SetFlag(&FLAGS_googlesql_idstring_allow_unicode_characters, true);
  IdStringPool pool;

  // 11000000
  const std::string invalid_utf8_1 = {(char)0xc0};

  // 11111110 11111110 11111111 11111111
  const std::string invalid_utf8_2 = {(char)0xfe, (char)0xfe, (char)0xff,
                                      (char)0xff};
  std::vector<std::string> test_strings = {invalid_utf8_1, invalid_utf8_2};

  for (int i = 0; i < test_strings.size(); ++i) {
    IdString id_string_1 = pool.Make(test_strings[i]);
    IdString id_string_2 = pool.Make(test_strings[i]);
    EXPECT_TRUE(id_string_1.CaseEquals(id_string_2));
  }
}

// Extract sorted vector of IdStrings from set elements.
template <class SET_TYPE>
static std::vector<IdString> SortedKeys(const SET_TYPE& input_values) {
  std::vector<IdString> values(input_values.begin(), input_values.end());
  std::sort(values.begin(), values.end());
  return values;
}

// Test a set and a case insensitive set type.
template <class SET_TYPE, class SET_TYPE_CASE>
static void TestIdStringSet() {
  std::vector<IdString> values = {
    ID("abC"),
    ID("Abc"),
    ID("abc"),
    ID(""),
    ID(""),
    ID("xxxxxxxxxxxxxxxxxxxxxxxxxxxxXX"),
    ID("abc"),
  };

  SET_TYPE id_set;
  SET_TYPE_CASE id_set_case;

  for (const IdString value : values) {
    id_set.insert(value);
    id_set_case.insert(value);
  }
  EXPECT_EQ(5, id_set.size());
  EXPECT_EQ(3, id_set_case.size());

  EXPECT_EQ(",Abc,abC,abc,xxxxxxxxxxxxxxxxxxxxxxxxxxxxXX",
            absl::StrJoin(SortedKeys(id_set), ",", IdStringFormatter));
  EXPECT_EQ(",abC,xxxxxxxxxxxxxxxxxxxxxxxxxxxxXX",
            absl::StrJoin(SortedKeys(id_set_case), ",", IdStringFormatter));

  for (const IdString value : values) {
    EXPECT_TRUE(googlesql_base::ContainsKey(id_set, value));
    EXPECT_TRUE(googlesql_base::ContainsKey(id_set_case, value));
  }

  const IdString aBc = ID("aBc");
  EXPECT_FALSE(googlesql_base::ContainsKey(id_set, aBc));
  EXPECT_TRUE(googlesql_base::ContainsKey(id_set_case, aBc));

  const IdString bad = ID("bad");
  EXPECT_FALSE(googlesql_base::ContainsKey(id_set, bad));
  EXPECT_FALSE(googlesql_base::ContainsKey(id_set_case, bad));
}

TEST(IdString, FlatHashSet) {
  TestIdStringSet<
      absl::flat_hash_set<IdString, IdStringHash>,
      absl::flat_hash_set<IdString, IdStringCaseHash, IdStringCaseEqualFunc>>();
}

TEST(IdString, Set) {
  TestIdStringSet<
      std::set<IdString>,
      std::set<IdString, IdStringCaseLess>>();
}

static const char kPoolIsDeadMsg[] =
    "IdString was accessed after its IdStringPool .* was destructed";

TEST(IdStringPool, Lifetime) {
  IdString global = IdString::MakeGlobal("global");
  global.CheckAlive();
  IdString global2 = global;
  global2.CheckAlive();

  IdStringPool pool1;
  IdString s1 = pool1.Make("s1");
  IdString s2 = s1;

  EXPECT_EQ("s1", s1.ToStringView());
  EXPECT_EQ("s1", s2.ToStringView());

  IdString s3;
  IdString s4;
  IdString s5;
  IdString s6;
  s3.CheckAlive();
  {
    IdStringPool pool2;
    s3 = pool2.Make("s3");
    s4 = s3;
    s5 = pool2.Make(s2.ToStringView());

    s3.CheckAlive();
    EXPECT_EQ("s3", s3.ToStringView());
    EXPECT_EQ("s3", s4.ToStringView());
    EXPECT_EQ("s1", s5.ToStringView());

    // The IdString object is copied into a heap-allocated object, but the value
    // lives in the pool.
    IdString* ptr = new IdString(pool2.Make("ptr"));
    EXPECT_EQ("ptr", ptr->ToStringView());
    s6 = *ptr;
    delete ptr;
    EXPECT_EQ("ptr", s6.ToStringView());
  }
  s1.CheckAlive();
  s2.CheckAlive();

  // In debug mode, these will all crash with a message about dead IdStringPool.
  // In opt mode, they might run, but have undefined behavior.
#ifdef NDEBUG
  s3.CheckAlive();  // This is a no-op in non-debug mode.
#else
  EXPECT_DEATH(s3.CheckAlive(), kPoolIsDeadMsg);
  EXPECT_DEATH(s4.CheckAlive(), kPoolIsDeadMsg);
  EXPECT_DEATH(s5.CheckAlive(), kPoolIsDeadMsg);
  EXPECT_DEATH(s6.CheckAlive(), kPoolIsDeadMsg);

  // All methods that access the value of a dead IdString should crash.
  EXPECT_DEATH(s5.ToString(), kPoolIsDeadMsg);
  EXPECT_DEATH(s5.ToStringView(), kPoolIsDeadMsg);
  EXPECT_DEATH(s5.empty(), kPoolIsDeadMsg);
  EXPECT_DEATH(s5.size(), kPoolIsDeadMsg);
  EXPECT_DEATH(s5.length(), kPoolIsDeadMsg);
  EXPECT_DEATH(s5[0], kPoolIsDeadMsg);
  EXPECT_DEATH(s5.substr(0, 1), kPoolIsDeadMsg);
  EXPECT_DEATH({ char buffer[2]; s5.copy(buffer, 1, 0); },
                     kPoolIsDeadMsg);
  global.CheckAlive();
  EXPECT_DEATH(global.Equals(s5), kPoolIsDeadMsg);
  EXPECT_DEATH(s5.Equals(global), kPoolIsDeadMsg);
  EXPECT_DEATH(global.LessThan(s5), kPoolIsDeadMsg);
  EXPECT_DEATH(s5.LessThan(global), kPoolIsDeadMsg);
  EXPECT_DEATH(s5.ToLower(&pool1), kPoolIsDeadMsg);
  EXPECT_DEATH(s5.Hash(), kPoolIsDeadMsg);
  EXPECT_DEATH(s5.HashCase(), kPoolIsDeadMsg);
#endif

  // Reassignment methods are allowed on dead strings.
  s3.clear();
  s4 = global;
  s3.CheckAlive();
  s4.CheckAlive();
}

TEST(IdString, ToLower) {
  IdString s1 = ID("ABcd");
  IdString s2;
  {
    IdStringPool pool;
    s2 = s1.ToLower(&pool);
    EXPECT_EQ("abcd", s2.ToStringView());
  }
  EXPECT_DEBUG_DEATH(s2.ToStringView(), kPoolIsDeadMsg);
}

STATIC_IDSTRING(kStaticOutside, "outside");
TEST(IdString, Static) {
  STATIC_IDSTRING(kStaticInside, "inside");
  EXPECT_EQ("outside", kStaticOutside.ToStringView());
  EXPECT_EQ("inside", kStaticInside.ToStringView());
}

}  // namespace

TEST(IdString, SharedSizeAlignment) {
  // If the size of Shared changes, make sure that the changed size is
  // reasonable and then update this test.
  EXPECT_EQ(64, sizeof(IdString::Shared));

  // The Shared struct and the strings within Shared are placed within an
  // arena-allocated buffer, and the strings are placed right after the Shared
  // struct. It is important that the size of Shared is a multiple of 8 so
  // that the string data is aligned.
  size_t padded_size = (sizeof(IdString::Shared) + 7) & ~7;
  EXPECT_EQ(padded_size, sizeof(IdString::Shared));
}

}  // namespace googlesql
