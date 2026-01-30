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

#include "googlesql/public/functions/uuid.h"

#include <cstdint>
#include <string>

#include "googlesql/base/logging.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/random/mock_distributions.h"
#include "absl/random/mocking_bit_gen.h"
#include "absl/random/random.h"

namespace googlesql::functions {
namespace {

using ::testing::Return;

void ExpectUuid(uint64_t low, uint64_t high, std::string expected) {
  absl::MockingBitGen mockgen;
  EXPECT_CALL(absl::MockUniform<uint64_t>(), Call(mockgen))
      .WillOnce(Return(low))
      .WillOnce(Return(high));

  EXPECT_EQ(expected, GenerateUuid(mockgen));
}

TEST(UuidTest, String) {
  uint64_t low = 0x0706050403020100ULL;
  uint64_t high = 0x0f0e0d0c0b0a0908ULL;
  ExpectUuid(low, high, "03020100-0504-4706-8908-0f0e0d0c0b0a");
}

TEST(UuidTest, SimpleStr) {
  // the first hex-char of the 3rd part should always be exactly 4
  // As this is uuid v4
  // the first hex char of the 4th part should encode the variant
  // as 10xx (where xx is part of the encoded data).
  // For zero input, the variant character is thus: 1000 => '8'
  ExpectUuid(uint64_t{0x0000000000000000}, uint64_t{0x0000000000000000},
             "00000000-0000-4000-8000-000000000000");

  // For ffff input, the variant character is thus: 1011 => 'b'
  ExpectUuid(uint64_t{0xffffffffffffffffu}, uint64_t{0xffffffffffffffffu},
             "ffffffff-ffff-4fff-bfff-ffffffffffff");
}

}  // namespace
}  // namespace googlesql::functions
