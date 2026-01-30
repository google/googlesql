//
// Copyright 2005 Google LLC
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

/////////////////////////////////////////////////////////////////////////////
//
// Author: enge@google.com (Lars Engebretsen)
//
// Unit test for edit_distance.h
//
/////////////////////////////////////////////////////////////////////////////

#include "googlesql/base/edit_distance.h"

#include <ctype.h>

#include <algorithm>
#include <cstring>
#include <functional>
#include <vector>

#include "gtest/gtest.h"
#include "googlesql/base/logging.h"

namespace googlesql_base {

class LevenshteinTest : public testing::Test {
 protected:
  struct NoCaseCmp {
    bool operator()(char c1, char c2) const {
      return std::tolower(c1) == std::tolower(c2);
    }
  };

  std::vector<char> empty;
  std::string s1234;
  std::string s567;
  std::string kilo;
  std::string kilogram;
  std::string mother;
  std::string grandmother;
  std::string lower;
  std::string upper;
  const char* algorithm_begin;
  const char* algorithm_end;
  const char* altruistic_begin;
  const char* altruistic_end;
  std::vector<char> algorithm;
  std::vector<char> altruistic;

  virtual void SetUp() {
    s1234 = "1234";
    s567 = "567";
    kilo = "kilo";
    kilogram = "kilogram";
    mother = "mother";
    grandmother = "grandmother";
    lower = "lower case";
    upper = "UPPER case";
    algorithm_begin = "algorithm";
    algorithm_end = algorithm_begin + std::strlen(algorithm_begin);
    altruistic_begin = "altruistic";
    altruistic_end = altruistic_begin + std::strlen(altruistic_begin);
    std::copy(algorithm_begin, algorithm_end, std::back_inserter(algorithm));
    std::copy(altruistic_begin, altruistic_end, std::back_inserter(altruistic));
  }
};
class CappedLevenshteinDistanceTest : public LevenshteinTest {};

TEST_F(CappedLevenshteinDistanceTest, BothEmpty) {
  ASSERT_EQ(CappedLevenshteinDistance(empty.begin(), empty.end(),
                                      empty.begin(), empty.end(),
                                      std::equal_to<char>(),
                                      0),
            0);
  ASSERT_EQ(CappedLevenshteinDistance(empty.begin(), empty.end(),
                                      empty.begin(), empty.end(),
                                      std::equal_to<char>(),
                                      1),
            0);
  ASSERT_EQ(CappedLevenshteinDistance(empty.begin(), empty.end(),
                                      empty.begin(), empty.end(),
                                      std::equal_to<char>(),
                                      17),
            0);
}

TEST_F(CappedLevenshteinDistanceTest, OneEmpty) {
  ASSERT_EQ(CappedLevenshteinDistance(s1234.begin(), s1234.end(),
                                      empty.begin(), empty.end(),
                                      std::equal_to<char>(),
                                      0),
            0);
  ASSERT_EQ(CappedLevenshteinDistance(s1234.begin(), s1234.end(),
                                      empty.begin(), empty.end(),
                                      std::equal_to<char>(),
                                      1),
            1);
  ASSERT_EQ(CappedLevenshteinDistance(s1234.begin(), s1234.end(),
                                      empty.begin(), empty.end(),
                                      std::equal_to<char>(),
                                      2),
            2);
  ASSERT_EQ(CappedLevenshteinDistance(s1234.begin(), s1234.end(),
                                      empty.begin(), empty.end(),
                                      std::equal_to<char>(),
                                      3),
            3);
  ASSERT_EQ(CappedLevenshteinDistance(s1234.begin(), s1234.end(),
                                      empty.begin(), empty.end(),
                                      std::equal_to<char>(),
                                      4),
            4);
  ASSERT_EQ(CappedLevenshteinDistance(s1234.begin(), s1234.end(),
                                      empty.begin(), empty.end(),
                                      std::equal_to<char>(),
                                      5),
            4);
  ASSERT_EQ(CappedLevenshteinDistance(s1234.begin(), s1234.end(),
                                      empty.begin(), empty.end(),
                                      std::equal_to<char>(),
                                      6),
            4);
  ASSERT_EQ(CappedLevenshteinDistance(empty.begin(), empty.end(),
                                      s567.begin(), s567.end(),
                                      std::equal_to<char>(),
                                      0),
            0);
  ASSERT_EQ(CappedLevenshteinDistance(empty.begin(), empty.end(),
                                      s567.begin(), s567.end(),
                                      std::equal_to<char>(),
                                      1),
            1);
  ASSERT_EQ(CappedLevenshteinDistance(empty.begin(), empty.end(),
                                      s567.begin(), s567.end(),
                                      std::equal_to<char>(),
                                      2),
            2);
  ASSERT_EQ(CappedLevenshteinDistance(empty.begin(), empty.end(),
                                      s567.begin(), s567.end(),
                                      std::equal_to<char>(),
                                      3),
            3);
  ASSERT_EQ(CappedLevenshteinDistance(empty.begin(), empty.end(),
                                      s567.begin(), s567.end(),
                                      std::equal_to<char>(),
                                      4),
            3);
  ASSERT_EQ(CappedLevenshteinDistance(empty.begin(), empty.end(),
                                      s567.begin(), s567.end(),
                                      std::equal_to<char>(),
                                      5),
            3);
}

TEST_F(CappedLevenshteinDistanceTest, Prefix) {
  ASSERT_EQ(CappedLevenshteinDistance(kilo.begin(), kilo.end(),
                                      kilogram.begin(), kilogram.end(),
                                      std::equal_to<char>(),
                                      0),
            0);
  ASSERT_EQ(CappedLevenshteinDistance(kilo.begin(), kilo.end(),
                                      kilogram.begin(), kilogram.end(),
                                      std::equal_to<char>(),
                                      2),
            2);
  ASSERT_EQ(CappedLevenshteinDistance(kilo.begin(), kilo.end(),
                                      kilogram.begin(), kilogram.end(),
                                      std::equal_to<char>(),
                                      3),
            3);
  ASSERT_EQ(CappedLevenshteinDistance(kilo.begin(), kilo.end(),
                                      kilogram.begin(), kilogram.end(),
                                      std::equal_to<char>(),
                                      4),
            4);
  ASSERT_EQ(CappedLevenshteinDistance(kilo.begin(), kilo.end(),
                                      kilogram.begin(), kilogram.end(),
                                      std::equal_to<char>(),
                                      5),
            4);
  ASSERT_EQ(CappedLevenshteinDistance(kilo.begin(), kilo.end(),
                                      kilogram.begin(), kilogram.end(),
                                      std::equal_to<char>(),
                                      9),
            4);
  ASSERT_EQ(CappedLevenshteinDistance(kilogram.begin(), kilogram.end(),
                                      kilo.begin(), kilo.end(),
                                      std::equal_to<char>(),
                                      0),
            0);
  ASSERT_EQ(CappedLevenshteinDistance(kilogram.begin(), kilogram.end(),
                                      kilo.begin(), kilo.end(),
                                      std::equal_to<char>(),
                                      1),
            1);
  ASSERT_EQ(CappedLevenshteinDistance(kilogram.begin(), kilogram.end(),
                                      kilo.begin(), kilo.end(),
                                      std::equal_to<char>(),
                                      3),
            3);
  ASSERT_EQ(CappedLevenshteinDistance(kilogram.begin(), kilogram.end(),
                                      kilo.begin(), kilo.end(),
                                      std::equal_to<char>(),
                                      4),
            4);
  ASSERT_EQ(CappedLevenshteinDistance(kilogram.begin(), kilogram.end(),
                                      kilo.begin(), kilo.end(),
                                      std::equal_to<char>(),
                                      5),
            4);
  ASSERT_EQ(CappedLevenshteinDistance(kilogram.begin(), kilogram.end(),
                                      kilo.begin(), kilo.end(),
                                      std::equal_to<char>(),
                                      7),
            4);
}

TEST_F(CappedLevenshteinDistanceTest, Suffix) {
  ASSERT_EQ(CappedLevenshteinDistance(mother.begin(), mother.end(),
                                      grandmother.begin(), grandmother.end(),
                                      std::equal_to<char>(),
                                      0),
            0);
  ASSERT_EQ(CappedLevenshteinDistance(mother.begin(), mother.end(),
                                      grandmother.begin(), grandmother.end(),
                                      std::equal_to<char>(),
                                      1),
            1);
  ASSERT_EQ(CappedLevenshteinDistance(mother.begin(), mother.end(),
                                      grandmother.begin(), grandmother.end(),
                                      std::equal_to<char>(),
                                      4),
            4);
  ASSERT_EQ(CappedLevenshteinDistance(mother.begin(), mother.end(),
                                      grandmother.begin(), grandmother.end(),
                                      std::equal_to<char>(),
                                      5),
            5);
  ASSERT_EQ(CappedLevenshteinDistance(mother.begin(), mother.end(),
                                      grandmother.begin(), grandmother.end(),
                                      std::equal_to<char>(),
                                      6),
            5);
  ASSERT_EQ(CappedLevenshteinDistance(mother.begin(), mother.end(),
                                      grandmother.begin(), grandmother.end(),
                                      std::equal_to<char>(),
                                      9),
            5);
  ASSERT_EQ(CappedLevenshteinDistance(grandmother.begin(), grandmother.end(),
                                      mother.begin(), mother.end(),
                                      std::equal_to<char>(),
                                      0),
            0);
  ASSERT_EQ(CappedLevenshteinDistance(grandmother.begin(), grandmother.end(),
                                      mother.begin(), mother.end(),
                                      std::equal_to<char>(),
                                      1),
            1);
  ASSERT_EQ(CappedLevenshteinDistance(grandmother.begin(), grandmother.end(),
                                      mother.begin(), mother.end(),
                                      std::equal_to<char>(),
                                      4),
            4);
  ASSERT_EQ(CappedLevenshteinDistance(grandmother.begin(), grandmother.end(),
                                      mother.begin(), mother.end(),
                                      std::equal_to<char>(),
                                      5),
            5);
  ASSERT_EQ(CappedLevenshteinDistance(grandmother.begin(), grandmother.end(),
                                      mother.begin(), mother.end(),
                                      std::equal_to<char>(),
                                      6),
            5);
  ASSERT_EQ(CappedLevenshteinDistance(grandmother.begin(), grandmother.end(),
                                      mother.begin(), mother.end(),
                                      std::equal_to<char>(),
                                      9),
            5);
}

TEST_F(CappedLevenshteinDistanceTest, DifferentComparisons) {
  ASSERT_EQ(CappedLevenshteinDistance(lower.begin(), lower.end(),
                                      upper.begin(), upper.end(),
                                      std::equal_to<char>(),
                                      3),
            3);
  ASSERT_EQ(CappedLevenshteinDistance(lower.begin(), lower.end(),
                                      upper.begin(), upper.end(),
                                      std::equal_to<char>(),
                                      5),
            5);
  ASSERT_EQ(CappedLevenshteinDistance(lower.begin(), lower.end(),
                                      upper.begin(), upper.end(),
                                      std::equal_to<char>(),
                                      7),
            5);
  ASSERT_EQ(CappedLevenshteinDistance(upper.begin(), upper.end(),
                                      lower.begin(), lower.end(),
                                      std::equal_to<char>(),
                                      3),
            3);
  ASSERT_EQ(CappedLevenshteinDistance(upper.begin(), upper.end(),
                                      lower.begin(), lower.end(),
                                      std::equal_to<char>(),
                                      5),
            5);
  ASSERT_EQ(CappedLevenshteinDistance(upper.begin(), upper.end(),
                                      lower.begin(), lower.end(),
                                      std::equal_to<char>(),
                                      7),
            5);
  ASSERT_EQ(CappedLevenshteinDistance(lower.begin(), lower.end(),
                                      upper.begin(), upper.end(),
                                      NoCaseCmp(),
                                      2),
            2);
  ASSERT_EQ(CappedLevenshteinDistance(lower.begin(), lower.end(),
                                      upper.begin(), upper.end(),
                                      NoCaseCmp(),
                                      3),
            3);
  ASSERT_EQ(CappedLevenshteinDistance(lower.begin(), lower.end(),
                                      upper.begin(), upper.end(),
                                      NoCaseCmp(),
                                      4),
            3);
  ASSERT_EQ(CappedLevenshteinDistance(upper.begin(), upper.end(),
                                      lower.begin(), lower.end(),
                                      NoCaseCmp(),
                                      2),
            2);
  ASSERT_EQ(CappedLevenshteinDistance(upper.begin(), upper.end(),
                                      lower.begin(), lower.end(),
                                      NoCaseCmp(),
                                      3),
            3);
  ASSERT_EQ(CappedLevenshteinDistance(upper.begin(), upper.end(),
                                      lower.begin(), lower.end(),
                                      NoCaseCmp(),
                                      4),
            3);
}

TEST_F(CappedLevenshteinDistanceTest, DifferentIterators) {
  ASSERT_EQ(CappedLevenshteinDistance(algorithm_begin, algorithm_end,
                                      altruistic_begin, altruistic_end,
                                      std::equal_to<char>(),
                                      2),
            2);
  ASSERT_EQ(CappedLevenshteinDistance(algorithm.begin(), algorithm.end(),
                                      altruistic.begin(), altruistic.end(),
                                      std::equal_to<char>(),
                                      5),
            5);
  ASSERT_EQ(CappedLevenshteinDistance(algorithm_begin, algorithm_end,
                                      altruistic.begin(), altruistic.end(),
                                      std::equal_to<char>(),
                                      6),
            6);
  ASSERT_EQ(CappedLevenshteinDistance(algorithm.begin(), algorithm.end(),
                                      altruistic_begin, altruistic_end,
                                      std::equal_to<char>(),
                                      7),
            6);
}

}  // namespace googlesql_base
