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

#include "googlesql/public/functions/comparison.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <random>

#include "gtest/gtest.h"

namespace googlesql {
namespace functions {
namespace {

TEST(Compare64Test, Values) {
  // -1, 0, 1
  EXPECT_EQ(Compare64(0, 0), 0);
  EXPECT_GT(Compare64(1, 0), 0);
  EXPECT_LT(Compare64(-1, 0), 0);

  // -1, ..., 1, 2
  EXPECT_EQ(Compare64(1, 1), 0);
  EXPECT_GT(Compare64(2, 1), 0);
  EXPECT_LT(Compare64(-1, 1), 0);

  // std::numeric_limits<int64>::max() - 1, kint64max
  EXPECT_EQ(Compare64(std::numeric_limits<int64_t>::max(),
                      std::numeric_limits<int64_t>::max()),
            0);
  EXPECT_EQ(Compare64(std::numeric_limits<int64_t>::max() - 1,
                      std::numeric_limits<int64_t>::max() - 1),
            0);
  EXPECT_GT(Compare64(std::numeric_limits<int64_t>::max(),
                      std::numeric_limits<int64_t>::max() - 1),
            0);
  EXPECT_LT(Compare64(std::numeric_limits<int64_t>::max() - 1,
                      std::numeric_limits<int64_t>::max()),
            0);

  // int64::max, int64::max + 1, ..., uint64::max
  EXPECT_LT(
      Compare64(std::numeric_limits<int64_t>::max(),
                static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1),
      0);
  EXPECT_LT(Compare64(std::numeric_limits<int64_t>::max(),
                      std::numeric_limits<uint64_t>::max()),
            0);

  // int64::lowest, int64::lowest + 1, ..., 0, ...,
  // int64::max, ..., uint64::max
  EXPECT_LT(Compare64(std::numeric_limits<int64_t>::lowest(), 0), 0);
  EXPECT_LT(Compare64(std::numeric_limits<int64_t>::lowest() + 1, 0), 0);
  EXPECT_LT(Compare64(std::numeric_limits<int64_t>::lowest(),
                      std::numeric_limits<int64_t>::max()),
            0);
  EXPECT_LT(Compare64(std::numeric_limits<int64_t>::lowest(),
                      std::numeric_limits<uint64_t>::max()),
            0);

  // 0 ... int64::max, int64::max+1, ... uint64::max - 1, uint64::max
  EXPECT_LT(Compare64(0, std::numeric_limits<int64_t>::max()), 0);
  EXPECT_GT(Compare64(std::numeric_limits<int64_t>::max(), 0), 0);
  EXPECT_LT(
      Compare64(0,
                static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1),
      0);
  EXPECT_LT(Compare64(0, std::numeric_limits<uint64_t>::max() - 1), 0);
  EXPECT_LT(Compare64(0, std::numeric_limits<uint64_t>::max()), 0);
}

}  // namespace
}  // namespace functions
}  // namespace googlesql
