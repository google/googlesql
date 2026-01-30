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

#include "googlesql/base/bits.h"

#include <string.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

#include "gtest/gtest.h"
#include "absl/numeric/int128.h"

// Number of test iterations to run.
constexpr int32_t kTestNumIterations = 10000;
// Maximum number of bytes to use in tests.
constexpr int32_t kTestMaxBytes = 100;

static const int kMaxBytes = 128;
static const int kNumReverseBitsRandomTests = 10;

namespace googlesql_base {

class BitsTest : public testing::Test {

 protected:
  template<typename T>
  static void CheckUnsignedType() {
    typedef typename Bits::UnsignedType<T>::Type UnsignedT;
    EXPECT_EQ(sizeof(T), sizeof(UnsignedT));
    EXPECT_FALSE(std::numeric_limits<UnsignedT>::is_signed);
  }

  // Wrapper for Bits::SetBits with a slightly different interface for
  // testing.  Instead of modifying a scalar, it returns a new value
  // with some bits replaced.
  template<typename T>
  static T SetBits(T dest,
                   const typename Bits::UnsignedType<T>::Type src,
                   const int offset,
                   const int nbits) {
    Bits::SetBits(src, offset, nbits, &dest);
    return dest;
  }

  template<typename T>
  void TestGetLowBitsType();
};

// For each type, test GetLowBits 0b1111..., for each bit position.
template <typename T>
inline void BitsTest::TestGetLowBitsType() {
  typedef typename Bits::UnsignedType<T>::Type UnsignedT;
  constexpr size_t bit_size = sizeof(UnsignedT) * 8;
  UnsignedT n = std::numeric_limits<UnsignedT>::max();
  UnsignedT mask = 0;
  for (int idx = 0; idx <= bit_size; ++idx) {
    EXPECT_EQ(Bits::GetLowBits(n, idx), n & mask);
    mask = (mask << 1) | 1;
  }
}

TEST_F(BitsTest, BitCountingEdgeCases) {
  EXPECT_EQ(0, Bits::CountOnes(0));
  EXPECT_EQ(1, Bits::CountOnes(1));
  EXPECT_EQ(32, Bits::CountOnes(static_cast<uint32_t>(~0U)));
  EXPECT_EQ(1, Bits::CountOnes(0x8000000));

  for (int i = 0; i < 32; i++) {
    EXPECT_EQ(1, Bits::CountOnes(1U << i));
    EXPECT_EQ(31, Bits::CountOnes(static_cast<uint32_t>(~0U) ^ (1U << i)));
  }

  EXPECT_EQ(0, Bits::CountOnes64(0LL));
  EXPECT_EQ(1, Bits::CountOnes64(1LL));
  EXPECT_EQ(64, Bits::CountOnes64(static_cast<uint64_t>(~0ULL)));
  EXPECT_EQ(1, Bits::CountOnes64(0x8000000LL));

  for (int i = 0; i < 64; i++) {
    EXPECT_EQ(1, Bits::CountOnes64(1LLU << i));
    EXPECT_EQ(63, Bits::CountOnes64(static_cast<uint64_t>(~(1LLU << i))));
  }

  EXPECT_EQ(0, Bits::CountOnes128(absl::uint128(0)));
  EXPECT_EQ(1, Bits::CountOnes128(absl::uint128(1)));
  EXPECT_EQ(128, Bits::CountOnes128(~absl::uint128(0)));

  for (int i = 0; i < 128; i++) {
    EXPECT_EQ(1, Bits::CountOnes128(absl::uint128(1) << i));
    EXPECT_EQ(127,
              Bits::CountOnes128(~absl::uint128(0) ^ (absl::uint128(1) << i)));
  }

  EXPECT_EQ(0, Bits::Count("", 0));
  for (int i = 0; i <= std::numeric_limits<int8_t>::max(); i++) {
    uint8_t b[1];
    b[0] = i;
    EXPECT_EQ(Bits::Count(b, 1), Bits::CountOnes(i));
  }
}

TEST_F(BitsTest, BitCountLeadingZeros) {
  EXPECT_EQ(32, Bits::CountLeadingZeros32(static_cast<uint32_t>(0)));
  EXPECT_EQ(64, Bits::CountLeadingZeros64(static_cast<uint64_t>(0)));
  EXPECT_EQ(128, Bits::CountLeadingZeros128(absl::uint128(0)));
  EXPECT_EQ(0, Bits::CountLeadingZeros32(~static_cast<uint32_t>(0)));
  EXPECT_EQ(0, Bits::CountLeadingZeros64(~static_cast<uint64_t>(0)));
  EXPECT_EQ(0, Bits::CountLeadingZeros128(~absl::uint128(0)));

  for (int i = 0; i < 32; i++) {
    EXPECT_EQ(31 - i, Bits::CountLeadingZeros32(static_cast<uint32_t>(1) << i));
  }

  for (int i = 0; i < 64; i++) {
    EXPECT_EQ(63 - i, Bits::CountLeadingZeros64(static_cast<uint64_t>(1) << i));
  }

  for (int i = 0; i < 128; i++) {
    EXPECT_EQ(127 - i, Bits::CountLeadingZeros128(absl::uint128(1) << i));
  }
}

TEST_F(BitsTest, GetBits) {
  const int8_t s8_src = 0x12;
  EXPECT_EQ(0x2, Bits::GetBits(s8_src, 0, 4));
  EXPECT_EQ(0x1, Bits::GetBits(s8_src, 4, 4));
  EXPECT_EQ(0, Bits::GetBits(s8_src, 0, 0));

  const uint8_t u8_src = 0x12;
  EXPECT_EQ(0x2, Bits::GetBits(u8_src, 0, 4));
  EXPECT_EQ(0x1, Bits::GetBits(u8_src, 4, 4));
  EXPECT_EQ(0, Bits::GetBits(u8_src, 0, 0));
  EXPECT_EQ(u8_src, Bits::GetBits(u8_src, 0, 8));

  const int16_t s16_src = 0x1234;
  EXPECT_EQ(0x34, Bits::GetBits(s16_src, 0, 8));
  EXPECT_EQ(0x23, Bits::GetBits(s16_src, 4, 8));
  EXPECT_EQ(0x12, Bits::GetBits(s16_src, 8, 8));
  EXPECT_EQ(0, Bits::GetBits(s16_src, 0, 0));

  const uint16_t u16_src = 0x1234;
  EXPECT_EQ(0x34, Bits::GetBits(u16_src, 0, 8));
  EXPECT_EQ(0x23, Bits::GetBits(u16_src, 4, 8));
  EXPECT_EQ(0x12, Bits::GetBits(u16_src, 8, 8));
  EXPECT_EQ(0, Bits::GetBits(u16_src, 0, 0));
  EXPECT_EQ(u16_src, Bits::GetBits(u16_src, 0, 16));

  const int32_t s32_src = 0x12345678;
  EXPECT_EQ(0x5678, Bits::GetBits(s32_src, 0, 16));
  EXPECT_EQ(0x3456, Bits::GetBits(s32_src, 8, 16));
  EXPECT_EQ(0x1234, Bits::GetBits(s32_src, 16, 16));
  EXPECT_EQ(0, Bits::GetBits(s32_src, 0, 0));

  const uint32_t u32_src = 0x12345678;
  EXPECT_EQ(0x5678, Bits::GetBits(u32_src, 0, 16));
  EXPECT_EQ(0x3456, Bits::GetBits(u32_src, 8, 16));
  EXPECT_EQ(0x1234, Bits::GetBits(u32_src, 16, 16));
  EXPECT_EQ(0, Bits::GetBits(u32_src, 0, 0));
  EXPECT_EQ(u32_src, Bits::GetBits(u32_src, 0, 32));

  const int64_t s64_src = 0x123456789abcdef0LL;
  EXPECT_EQ(0x9abcdef0, Bits::GetBits(s64_src, 0, 32));
  EXPECT_EQ(0x56789abc, Bits::GetBits(s64_src, 16, 32));
  EXPECT_EQ(0x12345678, Bits::GetBits(s64_src, 32, 32));
  EXPECT_EQ(0, Bits::GetBits(s64_src, 0, 0));

  const uint64_t u64_src = 0x123456789abcdef0ULL;
  EXPECT_EQ(0x9abcdef0, Bits::GetBits(u64_src, 0, 32));
  EXPECT_EQ(0x56789abc, Bits::GetBits(u64_src, 16, 32));
  EXPECT_EQ(0x12345678, Bits::GetBits(u64_src, 32, 32));
  EXPECT_EQ(0, Bits::GetBits(u64_src, 0, 0));
  EXPECT_EQ(u64_src, Bits::GetBits(u64_src, 0, 64));

  const absl::uint128 u128_src =
      absl::MakeUint128(0x123456789abcdef0ULL, 0x123456789abcdef0ULL);
  EXPECT_EQ(absl::uint128(0x9abcdef0), Bits::GetBits(u128_src, 0, 32));
  EXPECT_EQ(absl::uint128(0x56789abc), Bits::GetBits(u128_src, 16, 32));
  EXPECT_EQ(absl::uint128(0x12345678), Bits::GetBits(u128_src, 32, 32));
  EXPECT_EQ(absl::uint128(0x9abcdef012345678ULL),
            Bits::GetBits(u128_src, 32, 64));
  EXPECT_EQ(absl::uint128(0x123456789abcdef0ULL),
            Bits::GetBits(u128_src, 64, 64));
  EXPECT_EQ(u128_src, Bits::GetBits(u128_src, 0, 128));
  EXPECT_EQ(0, Bits::GetBits(u128_src, 0, 0));
  EXPECT_EQ(absl::MakeUint128(0x000000009abcdef0ULL, 0x123456789abcdef0ULL),
            Bits::GetBits(u128_src, 0, 96));
}

TEST_F(BitsTest, SetBits) {
  const int8_t s8_dest = 0x12;
  EXPECT_EQ(0, SetBits(s8_dest, 0, 0, 8));
  EXPECT_EQ(-1, SetBits(s8_dest, 0xff, 0, 8));
  EXPECT_EQ(0x32, SetBits(s8_dest, 0xf3, 4, 4));

  const uint8_t u8_dest = 0x12;
  EXPECT_EQ(0, SetBits(u8_dest, 0, 0, 8));
  EXPECT_EQ(0xff, SetBits(u8_dest, 0xff, 0, 8));
  // Should only write the lower 4 bits of value.
  EXPECT_EQ(0x32, SetBits(u8_dest, 0xf3, 4, 4));

  const int16_t s16_dest = 0x1234;
  EXPECT_EQ(0, SetBits(s16_dest, 0, 0, 16));
  EXPECT_EQ(-1, SetBits(s16_dest, 0xffff, 0, 16));
  EXPECT_EQ(0x1254, SetBits(s16_dest, 0xf5, 4, 4));

  const uint16_t u16_dest = 0x1234;
  EXPECT_EQ(0, SetBits(u16_dest, 0, 0, 16));
  EXPECT_EQ(0xffff, SetBits(u16_dest, 0xffff, 0, 16));
  EXPECT_EQ(0x1254, SetBits(u16_dest, 0xf5, 4, 4));

  const int32_t s32_dest = 0x12345678;
  EXPECT_EQ(0, SetBits(s32_dest, 0, 0, 32));
  EXPECT_EQ(-1, SetBits(s32_dest, 0xffffffff, 0, 32));
  EXPECT_EQ(0x12345698, SetBits(s32_dest, 0xf9, 4, 4));

  const uint32_t u32_dest = 0x12345678;
  EXPECT_EQ(0, SetBits(u32_dest, 0, 0, 32));
  EXPECT_EQ(0xffffffff, SetBits(u32_dest, 0xffffffff, 0, 32));
  EXPECT_EQ(0x12345698, SetBits(u32_dest, 0xf9, 4, 4));

  const int64_t s64_dest = 0x123456789abcdef0LL;
  EXPECT_EQ(0x0000000000000000LL, SetBits(s64_dest, 0ULL, 0, 64));
  EXPECT_EQ(-1, SetBits(s64_dest, 0xffffffffffffffffULL, 0, 64));
  EXPECT_EQ(0x123456789abcde10LL, SetBits(s64_dest, 0xf1, 4, 4));

  const uint64_t u64_dest = 0x123456789abcdef0ULL;
  EXPECT_EQ(0, SetBits(u64_dest, 0x00000000, 0, 64));
  EXPECT_EQ(0xffffffffffffffffULL,
            SetBits(u64_dest, 0xffffffffffffffffULL, 0, 64));
  EXPECT_EQ(0x123456789abcde10, SetBits(u64_dest, 0xf1, 4, 4));

  const absl::uint128 u128_dest =
      absl::MakeUint128(0x123456789abcdef0ULL, 0x123456789abcdef0ULL);
  EXPECT_EQ(0, SetBits(u128_dest, absl::uint128(0), 0, 128));
  const absl::uint128 u128_all_bits =
      absl::MakeUint128(0xffffffffffffffffULL, 0xffffffffffffffffULL);
  EXPECT_EQ(absl::MakeUint128(0xffffffffffffffffULL, 0xffffffffffffffffULL),
            SetBits(u128_dest, u128_all_bits, 0, 128));
  EXPECT_EQ(absl::MakeUint128(0x123456789abcdef0ULL, 0x123456789abcde10),
            SetBits(u128_dest, absl::uint128(0xf1), 4, 4));
}

TEST_F(BitsTest, CopyBits) {
  int8_t s8_dest = 0x12;
  Bits::CopyBits(&s8_dest, 0, 0, 0, 8);
  EXPECT_EQ(0, s8_dest);
  s8_dest = 0x12;
  Bits::CopyBits(&s8_dest, 0, -1, 0, 8);
  EXPECT_EQ(-1, s8_dest);
  s8_dest = 0x12;
  Bits::CopyBits(&s8_dest, 4, 0xf3ff, 8, 4);
  EXPECT_EQ(0x32, s8_dest);

  int16_t s16_dest = 0x1234;
  Bits::CopyBits(&s16_dest, 0, 0, 0, 16);
  EXPECT_EQ(0, s16_dest);
  s16_dest = 0x1234;
  Bits::CopyBits(&s16_dest, 0, -1, 0, 16);
  EXPECT_EQ(-1, s16_dest);
  s16_dest = 0x1234;
  Bits::CopyBits(&s16_dest, 8, 0xf5fff, 12, 4);
  EXPECT_EQ(0x1534, s16_dest);

  int32_t s32_dest = 0x12345678;
  Bits::CopyBits(&s32_dest, 0, 0, 0, 32);
  EXPECT_EQ(0, s32_dest);
  s32_dest = 0x12345678;
  Bits::CopyBits(&s32_dest, 0, -1, 0, 32);
  EXPECT_EQ(-1, s32_dest);
  s32_dest = 0x12345678;
  Bits::CopyBits(&s32_dest, 12, 0xf9ffff, 16, 4);
  EXPECT_EQ(0x12349678, s32_dest);

  int64_t s64_dest = 0x123456789abcdef0LL;
  Bits::CopyBits(&s64_dest, 0, int64_t{0}, 0, 64);
  EXPECT_EQ(0, s64_dest);
  s64_dest = 0x123456789abcdef0LL;
  Bits::CopyBits(&s64_dest, 0, int64_t{-1}, 0, 64);
  EXPECT_EQ(-1, s64_dest);
  s64_dest = 0x123456789abcdef0LL;
  Bits::CopyBits(&s64_dest, 16, 0xf1fffff, 20, 4);
  EXPECT_EQ(0x123456789ab1def0, s64_dest);
}

TEST_F(BitsTest, GetLowBits) {
  TestGetLowBitsType<int8_t>();
  TestGetLowBitsType<uint8_t>();
  TestGetLowBitsType<int16_t>();
  TestGetLowBitsType<uint16_t>();
  TestGetLowBitsType<int32_t>();
  TestGetLowBitsType<uint32_t>();
  TestGetLowBitsType<int64_t>();
  TestGetLowBitsType<uint64_t>();
  TestGetLowBitsType<absl::uint128>();
}

TEST(Bits, Port32) {
  for (int shift = 0; shift < 32; shift++) {
    for (int delta = -1; delta <= +1; delta++) {
      const uint32_t v = (static_cast<uint32_t>(1) << shift) + delta;
      EXPECT_EQ(Bits::Log2Floor_Portable(v), Bits::Log2Floor(v)) << v;
      if (v != 0) {
        EXPECT_EQ(Bits::Log2FloorNonZero_Portable(v),
                  Bits::Log2FloorNonZero(v)) << v;
        EXPECT_EQ(Bits::FindLSBSetNonZero_Portable(v),
                  Bits::FindLSBSetNonZero(v)) << v;
      }
    }
  }
  static const uint32_t M32 = std::numeric_limits<uint32_t>::max();
  EXPECT_EQ(Bits::Log2Floor_Portable(M32), Bits::Log2Floor(M32)) << M32;
  EXPECT_EQ(Bits::Log2FloorNonZero_Portable(M32),
            Bits::Log2FloorNonZero(M32)) << M32;
  EXPECT_EQ(Bits::FindLSBSetNonZero_Portable(M32),
            Bits::FindLSBSetNonZero(M32)) << M32;
}

TEST(Bits, Port64) {
  for (int shift = 0; shift < 64; shift++) {
    for (int delta = -1; delta <= +1; delta++) {
      const uint64_t v = (static_cast<uint64_t>(1) << shift) + delta;
      EXPECT_EQ(Bits::Log2Floor64_Portable(v), Bits::Log2Floor64(v)) << v;
      if (v != 0) {
        EXPECT_EQ(Bits::Log2FloorNonZero64_Portable(v),
                  Bits::Log2FloorNonZero64(v)) << v;
        EXPECT_EQ(Bits::FindLSBSetNonZero64_Portable(v),
                  Bits::FindLSBSetNonZero64(v)) << v;
      }
    }
  }
  static const uint64_t M64 = std::numeric_limits<uint64_t>::max();
  EXPECT_EQ(Bits::Log2Floor64_Portable(M64), Bits::Log2Floor64(M64)) << M64;
  EXPECT_EQ(Bits::Log2FloorNonZero64_Portable(M64),
            Bits::Log2FloorNonZero64(M64)) << M64;
  EXPECT_EQ(Bits::FindLSBSetNonZero64_Portable(M64),
            Bits::FindLSBSetNonZero64(M64)) << M64;
}

TEST(CountOnes, InByte) {
  for (int i = 0; i < 256; i++) {
    unsigned char c = static_cast<unsigned char>(i);
    int expected = 0;
    for (int pos = 0; pos < 8; pos++) {
      expected += (c & (1 << pos)) ? 1 : 0;
    }
    EXPECT_EQ(expected, Bits::CountOnesInByte(c))
      << std::hex << static_cast<int>(c);
  }
}

TEST(FindLSBSetNonZero, OneAllOrSomeBitsSet) {
  uint32_t testone = 0x00000001;
  uint32_t testall = 0xFFFFFFFF;
  uint32_t testsome = 0x87654321;
  for (int i = 0; i < 32; ++i) {
    EXPECT_EQ(i, Bits::FindLSBSetNonZero(testone));
    EXPECT_EQ(i, Bits::FindLSBSetNonZero(testall));
    EXPECT_EQ(i, Bits::FindLSBSetNonZero(testsome));
    testone <<= 1;
    testall <<= 1;
    testsome <<= 1;
  }
}

TEST(FindLSBSetNonZero64, OneAllOrSomeBitsSet) {
  uint64_t testone = 0x0000000000000001ULL;
  uint64_t testall = 0xFFFFFFFFFFFFFFFFULL;
  uint64_t testsome = 0x0FEDCBA987654321ULL;
  for (int i = 0; i < 64; ++i) {
    EXPECT_EQ(i, Bits::FindLSBSetNonZero64(testone));
    EXPECT_EQ(i, Bits::FindLSBSetNonZero64(testall));
    EXPECT_EQ(i, Bits::FindLSBSetNonZero64(testsome));
    testone <<= 1;
    testall <<= 1;
    testsome <<= 1;
  }
}

TEST(FindLSBSetNonZero128, OneAllOrSomeBitsSet) {
  absl::uint128 testone = absl::uint128(1);
  absl::uint128 testall = ~absl::uint128(0);
  absl::uint128 testsome =
      absl::MakeUint128(0x0FEDCBA987654321ULL, 0x0FEDCBA987654321ULL);
  for (int i = 0; i < 128; ++i) {
    EXPECT_EQ(i, Bits::FindLSBSetNonZero128(testone));
    EXPECT_EQ(i, Bits::FindLSBSetNonZero128(testall));
    EXPECT_EQ(i, Bits::FindLSBSetNonZero128(testsome));
    testone <<= 1;
    testall <<= 1;
    testsome <<= 1;
  }
}

TEST(FindMSBSetNonZero, OneAllOrSomeBitsSet) {
  uint32_t testone = 0x80000000;
  uint32_t testall = 0xFFFFFFFF;
  uint32_t testsome = 0x87654321;
  for (int i = 31; i >= 0; --i) {
    EXPECT_EQ(i, Bits::FindMSBSetNonZero(testone));
    EXPECT_EQ(i, Bits::FindMSBSetNonZero(testall));
    EXPECT_EQ(i, Bits::FindMSBSetNonZero(testsome));
    testone >>= 1;
    testall >>= 1;
    testsome >>= 1;
  }
}

TEST(FindMSBSetNonZero64, OneAllOrSomeBitsSet) {
  uint64_t testone = 0x8000000000000000ULL;
  uint64_t testall = 0xFFFFFFFFFFFFFFFFULL;
  uint64_t testsome = 0xFEDCBA9876543210ULL;
  for (int i = 63; i >= 0; --i) {
    EXPECT_EQ(i, Bits::FindMSBSetNonZero64(testone));
    EXPECT_EQ(i, Bits::FindMSBSetNonZero64(testall));
    EXPECT_EQ(i, Bits::FindMSBSetNonZero64(testsome));
    testone >>= 1;
    testall >>= 1;
    testsome >>= 1;
  }
}

TEST(FindMSBSetNonZero128, OneAllOrSomeBitsSet) {
  absl::uint128 testone = absl::MakeUint128(0x8000000000000000ULL, 0);
  absl::uint128 testall = ~absl::uint128(0);
  absl::uint128 testsome =
      absl::MakeUint128(0xFEDCBA9876543210ULL, 0xFEDCBA9876543210ULL);
  for (int i = 127; i >= 0; --i) {
    EXPECT_EQ(i, Bits::FindMSBSetNonZero128(testone));
    EXPECT_EQ(i, Bits::FindMSBSetNonZero128(testall));
    EXPECT_EQ(i, Bits::FindMSBSetNonZero128(testsome));
    testone >>= 1;
    testall >>= 1;
    testsome >>= 1;
  }
}

// Function that does what ReverseBits*() do, but doing a bit-by-bit walk.
// The ReverseBits*() functions are much more efficient.
template <class T>
static T ExpectedReverseBits(T n) {
  T r = 0;
  for (int i = 0; i < sizeof(T) << 3 ; ++i) {
    r = (r << 1) | (n & 1);
    n >>= 1;
  }
  return r;
}

TEST(ReverseBits, InByte) {
  EXPECT_EQ(0, Bits::ReverseBits8(0));
  EXPECT_EQ(0xff, Bits::ReverseBits8(0xff));
  EXPECT_EQ(0x80, Bits::ReverseBits8(0x01));
  EXPECT_EQ(0x01, Bits::ReverseBits8(0x80));
}

TEST(ReverseBits, In32BitWord) {
  EXPECT_EQ(0, Bits::ReverseBits32(0));
  EXPECT_EQ(0xffffffff, Bits::ReverseBits32(0xffffffff));
  EXPECT_EQ(0x80000000, Bits::ReverseBits32(0x00000001));
  EXPECT_EQ(0x00000001, Bits::ReverseBits32(0x80000000));
  EXPECT_EQ(0x55555555, Bits::ReverseBits32(0xaaaaaaaa));
  EXPECT_EQ(0xaaaaaaaa, Bits::ReverseBits32(0x55555555));
  EXPECT_EQ(0xcafebabe, Bits::ReverseBits32(0x7d5d7f53));
  EXPECT_EQ(0x7d5d7f53, Bits::ReverseBits32(0xcafebabe));
}

TEST(ReverseBits, In64BitWord) {
  EXPECT_EQ(0ull, Bits::ReverseBits64(0ull));
  EXPECT_EQ(0xffffffffffffffffull, Bits::ReverseBits64(0xffffffffffffffffull));
  EXPECT_EQ(0x8000000000000000ull, Bits::ReverseBits64(0x0000000000000001ull));
  EXPECT_EQ(0x0000000000000001ull, Bits::ReverseBits64(0x8000000000000000ull));
  EXPECT_EQ(0x5555555555555555ull, Bits::ReverseBits64(0xaaaaaaaaaaaaaaaaull));
  EXPECT_EQ(0xaaaaaaaaaaaaaaaaull, Bits::ReverseBits64(0x5555555555555555ull));
  // Use a non-constant expression to avoid constant-folding optimizations.
  // NOLINTNEXTLINE(runtime/deprecated_fn)
  EXPECT_EQ(0xfedcba9876543210ull, Bits::ReverseBits64(std::strtoull(
                                       "084c2a6e195d3b7f", nullptr, 16)));
}

TEST(ReverseBits, In128BitWord) {
  EXPECT_EQ(absl::uint128(0), Bits::ReverseBits128(absl::uint128(0)));
  EXPECT_EQ(~absl::uint128(0), Bits::ReverseBits128(~absl::uint128(0)));
  EXPECT_EQ(absl::MakeUint128(0x8000000000000000ull, 0),
            Bits::ReverseBits128(absl::uint128(1)));
  EXPECT_EQ(absl::uint128(1),
            Bits::ReverseBits128(absl::MakeUint128(0x8000000000000000ull, 0)));
  EXPECT_EQ(absl::MakeUint128(0x5555555555555555ull, 0x5555555555555555ull),
            Bits::ReverseBits128(absl::MakeUint128(0xaaaaaaaaaaaaaaaaull,
                                                   0xaaaaaaaaaaaaaaaaull)));
  EXPECT_EQ(absl::MakeUint128(0xaaaaaaaaaaaaaaaaull, 0xaaaaaaaaaaaaaaaaull),
            Bits::ReverseBits128(absl::MakeUint128(0x5555555555555555ull,
                                                   0x5555555555555555ull)));
}

}  // namespace googlesql_base
