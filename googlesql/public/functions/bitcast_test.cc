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

#include "googlesql/public/functions/bitcast.h"

#include <cstdint>
#include <limits>

#include "gtest/gtest.h"
#include "googlesql/base/status.h"

namespace googlesql {
namespace functions {

const int32_t int32min = std::numeric_limits<int32_t>::min();
const int32_t int32max = std::numeric_limits<int32_t>::max();
const int64_t int64min = std::numeric_limits<int64_t>::min();
const int64_t int64max = std::numeric_limits<int64_t>::max();
const uint32_t uint32max = std::numeric_limits<uint32_t>::max();
const uint64_t uint64max = std::numeric_limits<uint64_t>::max();

template <typename TIN, typename TOUT>
void TestBitCast(const TIN& in, const TOUT& expected) {
  TOUT out = 0;
  absl::Status status;
  BitCast<TIN, TOUT>(in, &out, &status);
  EXPECT_EQ(expected, out);
}

TEST(BitCast, Test) {
  // INT32 -> INT32
  TestBitCast<int32_t, int32_t>(static_cast<int32_t>(0),
                                static_cast<int32_t>(0));
  TestBitCast<int32_t, int32_t>(static_cast<int32_t>(int32max),
                                static_cast<int32_t>(int32max));
  TestBitCast<int32_t, int32_t>(static_cast<int32_t>(int32min),
                                static_cast<int32_t>(int32min));
  TestBitCast<int32_t, int32_t>(static_cast<int32_t>(3),
                                static_cast<int32_t>(3));
  TestBitCast<int32_t, int32_t>(static_cast<int32_t>(-3),
                                static_cast<int32_t>(-3));

  // UINT32 -> INT32
  TestBitCast<uint32_t, int32_t>(static_cast<uint32_t>(0),
                                 static_cast<int32_t>(0));
  TestBitCast<uint32_t, int32_t>(static_cast<uint32_t>(uint32max),
                                 static_cast<int32_t>(-1));
  TestBitCast<uint32_t, int32_t>(static_cast<uint32_t>(3),
                                 static_cast<int32_t>(3));
  TestBitCast<uint32_t, int32_t>(static_cast<uint32_t>(uint32max - 3),
                                 static_cast<int32_t>(-4));
  TestBitCast<uint32_t, int32_t>(static_cast<uint32_t>(uint32max >> 1),
                                 static_cast<int32_t>(int32max));

  // INT64 -> INT64
  TestBitCast<int64_t, int64_t>(static_cast<int64_t>(0),
                                static_cast<int64_t>(0));
  TestBitCast<int64_t, int64_t>(static_cast<int64_t>(int64max),
                                static_cast<int64_t>(int64max));
  TestBitCast<int64_t, int64_t>(static_cast<int64_t>(int64min),
                                static_cast<int64_t>(int64min));
  TestBitCast<int64_t, int64_t>(static_cast<int64_t>(3),
                                static_cast<int64_t>(3));
  TestBitCast<int64_t, int64_t>(static_cast<int64_t>(-3),
                                static_cast<int64_t>(-3));

  // UINT64 -> INT64
  TestBitCast<uint64_t, int64_t>(static_cast<uint64_t>(0),
                                 static_cast<int64_t>(0));
  TestBitCast<uint64_t, int64_t>(static_cast<uint64_t>(uint64max),
                                 static_cast<int64_t>(-1));
  TestBitCast<uint64_t, int64_t>(static_cast<uint64_t>(3),
                                 static_cast<int64_t>(3));
  TestBitCast<uint64_t, int64_t>(static_cast<uint64_t>(uint64max - 3),
                                 static_cast<int64_t>(-4));
  TestBitCast<uint64_t, int64_t>(static_cast<uint64_t>(uint64max >> 1),
                                 static_cast<int64_t>(int64max));

  // UINT32 -> UINT32
  TestBitCast<uint32_t, uint32_t>(static_cast<uint32_t>(0),
                                  static_cast<uint32_t>(0));
  TestBitCast<uint32_t, uint32_t>(static_cast<uint32_t>(uint32max),
                                  static_cast<uint32_t>(uint32max));
  TestBitCast<uint32_t, uint32_t>(static_cast<uint32_t>(3),
                                  static_cast<uint32_t>(3));

  // INT32 -> UINT32
  TestBitCast<int32_t, uint32_t>(static_cast<int32_t>(0),
                                 static_cast<uint32_t>(0));
  TestBitCast<int32_t, uint32_t>(static_cast<int32_t>(int32max),
                                 static_cast<uint32_t>(int32max));
  TestBitCast<int32_t, uint32_t>(static_cast<int32_t>(3),
                                 static_cast<uint32_t>(3));
  TestBitCast<int32_t, uint32_t>(static_cast<int32_t>(-3),
                                 static_cast<uint32_t>(-3));
  TestBitCast<int32_t, uint32_t>(static_cast<int32_t>(int32min),
                                 static_cast<uint32_t>(int32min));
  TestBitCast<int32_t, uint32_t>(static_cast<int32_t>(int32min + 3),
                                 static_cast<uint32_t>(2147483651));

  // UINT64 -> UINT64
  TestBitCast<uint64_t, uint64_t>(static_cast<uint64_t>(0),
                                  static_cast<uint64_t>(0));
  TestBitCast<uint64_t, uint64_t>(static_cast<uint64_t>(uint64max),
                                  static_cast<uint64_t>(uint64max));
  TestBitCast<uint64_t, uint64_t>(static_cast<uint64_t>(3),
                                  static_cast<uint64_t>(3));

  // INT64 -> UINT64
  TestBitCast<int64_t, uint64_t>(static_cast<int64_t>(0),
                                 static_cast<uint64_t>(0));
  TestBitCast<int64_t, uint64_t>(static_cast<int64_t>(int64max),
                                 static_cast<uint64_t>(int64max));
  TestBitCast<int64_t, uint64_t>(static_cast<int64_t>(3),
                                 static_cast<uint64_t>(3));
  TestBitCast<int64_t, uint64_t>(static_cast<int64_t>(-3),
                                 static_cast<uint64_t>(-3));
  TestBitCast<int64_t, uint64_t>(static_cast<int64_t>(int64min),
                                 static_cast<uint64_t>(int64min));
  TestBitCast<int64_t, uint64_t>(
      static_cast<int64_t>(int64min + 3),
      static_cast<uint64_t>(uint64_t{9223372036854775811u}));
}

}  // namespace functions
}  // namespace googlesql
