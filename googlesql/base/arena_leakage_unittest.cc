//
// Copyright 2018 Google LLC
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

#include "googlesql/base/arena.h"

#include "gtest/gtest.h"

namespace googlesql_base {

TEST(Arena, Leakage) {
  UnsafeArena arena(32);
  // Grab just 10 bytes.
  EXPECT_EQ(arena.bytes_until_next_allocation(), 32);
  const char* block = arena.Alloc(10);
  EXPECT_NE(block, nullptr);
  EXPECT_EQ(arena.bytes_until_next_allocation(), 22);
  // Grab the rest.
  const char* expected_next_block = block + 10;
  const char* next_block = arena.Alloc(22);
  // If the below test fails, a new block has been allocated for "next_block".
  // This means that the last 22 bytes of the previous block have been lost.
  EXPECT_EQ(next_block, expected_next_block);
  EXPECT_EQ(arena.bytes_until_next_allocation(), 0);
  // Try allocating a 0 bytes block. Arena should remain unchanged.
  const char* null_block = arena.Alloc(0);
  EXPECT_EQ(null_block, nullptr);
  EXPECT_EQ(arena.bytes_until_next_allocation(), 0);
}

}  // namespace googlesql_base
