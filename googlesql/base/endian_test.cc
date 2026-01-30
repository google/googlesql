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

#include "googlesql/base/endian.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <random>
#include <vector>

#include "gtest/gtest.h"
#include "absl/base/config.h"
#include "absl/numeric/int128.h"
#include "googlesql/base/unaligned_access.h"

namespace googlesql_base {
namespace {

const uint64_t kInitialNumber{0x0123456789abcdef};
const uint64_t k64Value{kInitialNumber};
const int64_t k64IValue = static_cast<int64_t>(0xa123456789abcdef);
const uint32_t k32Value{0x01234567};
const uint16_t k16Value{0x0123};
const uint8_t k8Value{0x12};
const int kNumValuesToTest = 1000000;
const int kRandomSeed = 12345;
const absl::uint128 k128Value = absl::MakeUint128(k64Value, k64IValue);

#if defined(ABSL_IS_BIG_ENDIAN)
const uint64_t kInitialInNetworkOrder{kInitialNumber};
const uint64_t k64ValueLE{0xefcdab8967452301};
const uint64_t k64IValueLE{0xefcdab89674523a1};
const uint32_t k32ValueLE{0x67452301};
const uint16_t k16ValueLE{0x2301};
const uint8 k8ValueLE = k8Value;
const absl::uint128 k128ValueLE = absl::MakeUint128(k64IValueLE, k64ValueLE);

const uint64_t k64ValueBE{kInitialNumber};
const uint32_t k32ValueBE{k32Value};
const uint16_t k16ValueBE{k16Value};

#elif defined(ABSL_IS_LITTLE_ENDIAN)
const uint64_t kInitialInNetworkOrder{0xefcdab8967452301};
const uint64_t k64ValueLE{kInitialNumber};
const uint64_t k64IValueLE = static_cast<int64_t>(0xa123456789abcdef);
const uint32_t k32ValueLE{k32Value};
const uint16_t k16ValueLE{k16Value};
const uint8_t k8ValueLE = k8Value;
const absl::uint128 k128ValueLE = absl::MakeUint128(k64ValueLE, k64IValueLE);

const uint64_t k64ValueBE{0xefcdab8967452301};
const uint32_t k32ValueBE{0x67452301};
const uint16_t k16ValueBE{0x2301};
#endif

template<typename T>
std::vector<T> GenerateAllValuesForType() {
  std::vector<T> result;
  T next = std::numeric_limits<T>::min();
  while (true) {
    result.push_back(next);
    if (next == std::numeric_limits<T>::max()) {
      return result;
    }
    ++next;
  }
}

template<typename T>
std::vector<T> GenerateRandomIntegers(size_t numValuesToTest) {
  std::vector<T> result;
  std::mt19937_64 rng(kRandomSeed);
  for (size_t i = 0; i < numValuesToTest; ++i) {
    result.push_back(rng());
  }
  return result;
}

template<typename T>
std::vector<T> GenerateRandomFloatingPointNumbers(size_t numValuesToTest) {
  std::vector<T> result;  // For RVO.
  std::mt19937_64 rng(kRandomSeed);
  std::uniform_real_distribution<double> real_distribution;
  for (size_t i = 0; i < numValuesToTest; ++i) {
    result.push_back(real_distribution(rng));
  }
  return result;
}

template <typename T, typename ByteSwapper>
static void GenericLoadStoreHelper(const std::vector<T>& host_values_to_test,
                                   const ByteSwapper& byte_swapper) {
  // Compare the result of LittleEndian::Store<T> to blob of
  // independently-calculated bytes.
  for (const T host_value : host_values_to_test) {
    // Perform LittleEndian conversion.
    char le_wire_value[sizeof(host_value)];
    LittleEndian::Store(host_value, le_wire_value);

    // Calculate expected results for LittleEndian conversion.
    char expected_le_wire_value[sizeof(host_value)];
    memcpy(expected_le_wire_value, &host_value, sizeof(host_value));

    // These are asserts rather than expects because if they fail, a *lot* of
    // them will fail, and there's no point in wading through bazillions of
    // pages of logs.
    ASSERT_EQ(0, memcmp(le_wire_value,
                        expected_le_wire_value,
                        sizeof(le_wire_value)));
  }

  // Round-trip test.
  for (const T host_value : host_values_to_test) {
    char le_wire_value[sizeof(host_value)];
    LittleEndian::Store(host_value, le_wire_value);
    T host_from_le = LittleEndian::Load<T>(le_wire_value);
    ASSERT_EQ(host_value, host_from_le);
  }
}

void ManualByteSwap(char* bytes, int length) {
  if (length == 1)
    return;

  EXPECT_EQ(0, length % 2);
  for (int i = 0; i < length / 2; ++i) {
    int j = (length - 1) - i;
    using std::swap;
    swap(bytes[i], bytes[j]);
  }
}

template <typename T>
inline T UnalignedLoad(const char* p);

template <>
uint16_t UnalignedLoad<uint16_t>(const char* p) {
  return GOOGLESQL_INTERNAL_UNALIGNED_LOAD16(p);
}

template <>
uint32_t UnalignedLoad<uint32_t>(const char* p) {
  return GOOGLESQL_INTERNAL_UNALIGNED_LOAD32(p);
}

template <>
uint64_t UnalignedLoad<uint64_t>(const char* p) {
  return GOOGLESQL_INTERNAL_UNALIGNED_LOAD64(p);
}

template <typename T, typename ByteSwapper>
static void GBSwapHelper(const std::vector<T>& host_values_to_test,
                         const ByteSwapper& byte_swapper) {
  // Test byte_swapper against a manual byte swap.
  for (typename std::vector<T>::const_iterator it = host_values_to_test.begin();
       it != host_values_to_test.end(); ++it) {
    T host_value = *it;

    char actual_value[sizeof(host_value)];
    memcpy(actual_value, &host_value, sizeof(host_value));
    byte_swapper(actual_value);

    char expected_value[sizeof(host_value)];
    memcpy(expected_value, &host_value, sizeof(host_value));
    ManualByteSwap(expected_value, sizeof(host_value));

    ASSERT_EQ(0, memcmp(actual_value, expected_value, sizeof(host_value)))
        << "Swap output for 0x" << std::hex << host_value << " does not match. "
        << "Expected: 0x" << UnalignedLoad<T>(expected_value) << "; "
        << "actual: 0x" <<  UnalignedLoad<T>(actual_value);
  }
}

void Swap8(char* bytes) {
  /* do nothing */
}

void Swap16(char* bytes) {
  GOOGLESQL_INTERNAL_UNALIGNED_STORE16(
      bytes, gbswap_16(GOOGLESQL_INTERNAL_UNALIGNED_LOAD16(bytes)));
}

void Swap32(char* bytes) {
  GOOGLESQL_INTERNAL_UNALIGNED_STORE32(
      bytes, gbswap_32(GOOGLESQL_INTERNAL_UNALIGNED_LOAD32(bytes)));
}

void Swap64(char* bytes) {
  GOOGLESQL_INTERNAL_UNALIGNED_STORE64(
      bytes, gbswap_64(GOOGLESQL_INTERNAL_UNALIGNED_LOAD64(bytes)));
}

TEST(EndianessTest, Uint16) {
  GBSwapHelper(GenerateAllValuesForType<uint16_t>(), &Swap16);
}

TEST(EndianessTest, Uint32) {
  GBSwapHelper(GenerateRandomIntegers<uint32_t>(kNumValuesToTest), &Swap32);
}

TEST(EndianessTest, Uint64) {
  GBSwapHelper(GenerateRandomIntegers<uint64_t>(kNumValuesToTest), &Swap64);
}

TEST(EndianessTest, ghtonll_gntohll) {
  // Test that ghtonl compiles correctly
  uint32_t test = 0x01234567;
  EXPECT_EQ(gntohl(ghtonl(test)), test);

  uint64_t comp = ghtonll(kInitialNumber);
  EXPECT_EQ(comp, kInitialInNetworkOrder);
  comp = gntohll(kInitialInNetworkOrder);
  EXPECT_EQ(comp, kInitialNumber);

  // Test that htonll and ntohll are each others' inverse functions on a
  // somewhat assorted batch of numbers. 37 is chosen to not be anything
  // particularly nice base 2.
  uint64_t value = 1;
  for (int i = 0; i < 100; ++i) {
    comp = ghtonll(gntohll(value));
    EXPECT_EQ(value, comp);
    comp = gntohll(ghtonll(value));
    EXPECT_EQ(value, comp);
    value *= 37;
  }
}

TEST(GenericLoadStore, Test8BitTypes) {
  GenericLoadStoreHelper(GenerateAllValuesForType<uint8_t>(), &Swap8);
  GenericLoadStoreHelper(GenerateAllValuesForType<int8_t>(), &Swap8);
}

TEST(GenericLoadStore, Test16BitTypes) {
  GenericLoadStoreHelper(GenerateAllValuesForType<uint16_t>(), &Swap16);
  GenericLoadStoreHelper(GenerateAllValuesForType<int16_t>(), &Swap16);
}

TEST(GenericLoadStore, Test32BitTypes) {
  GenericLoadStoreHelper(GenerateRandomIntegers<uint32_t>(kNumValuesToTest),
                         &Swap32);
  GenericLoadStoreHelper(GenerateRandomIntegers<int32_t>(kNumValuesToTest),
                         &Swap32);
}

TEST(GenericLoadStore, Test64BitTypes) {
  GenericLoadStoreHelper(GenerateRandomIntegers<uint64_t>(kNumValuesToTest),
                         &Swap64);
  GenericLoadStoreHelper(GenerateRandomIntegers<int64_t>(kNumValuesToTest),
                         &Swap64);
}


TEST(EndianessTest, LittleEndian) {
  // Check LittleEndian uint8.
  uint64_t comp = LittleEndian::FromHost<uint8_t>(k8Value);
  EXPECT_EQ(comp, k8Value);
  comp = LittleEndian::ToHost<uint8_t>(k8Value);
  EXPECT_EQ(comp, k8Value);

  // Check LittleEndian uint16.
  comp = LittleEndian::FromHost16(k16Value);
  EXPECT_EQ(comp, k16ValueLE);
  comp = LittleEndian::ToHost16(k16ValueLE);
  EXPECT_EQ(comp, k16Value);

  // Check LittleEndian uint32.
  comp = LittleEndian::FromHost32(k32Value);
  EXPECT_EQ(comp, k32ValueLE);
  comp = LittleEndian::ToHost32(k32ValueLE);
  EXPECT_EQ(comp, k32Value);

  // Check LittleEndian uint64.
  comp = LittleEndian::FromHost64(k64Value);
  EXPECT_EQ(comp, k64ValueLE);
  comp = LittleEndian::ToHost64(k64ValueLE);
  EXPECT_EQ(comp, k64Value);

  // Check LittleEndian uint128.
  absl::uint128 comp128 = LittleEndian::FromHost128(k128Value);
  EXPECT_EQ(comp128, k128ValueLE);
  comp128 = LittleEndian::ToHost128(k128ValueLE);
  EXPECT_EQ(comp128, k128Value);

  // Check little-endian Load and store functions.
  uint16_t u16Buf;
  uint32_t u32Buf;
  uint64_t u64Buf;
  absl::uint128 u128Buf;

  LittleEndian::Store16(&u16Buf, k16Value);
  EXPECT_EQ(u16Buf, k16ValueLE);
  comp = LittleEndian::Load16(&u16Buf);
  EXPECT_EQ(comp, k16Value);

  LittleEndian::Store32(&u32Buf, k32Value);
  EXPECT_EQ(u32Buf, k32ValueLE);
  comp = LittleEndian::Load32(&u32Buf);
  EXPECT_EQ(comp, k32Value);

  LittleEndian::Store64(&u64Buf, k64Value);
  EXPECT_EQ(u64Buf, k64ValueLE);
  comp = LittleEndian::Load64(&u64Buf);
  EXPECT_EQ(comp, k64Value);

  LittleEndian::Store128(&u128Buf, k128Value);
  EXPECT_EQ(u128Buf, k128ValueLE);
  comp128 = LittleEndian::Load128(&u128Buf);
  EXPECT_EQ(comp128, k128Value);
}

TEST(BigEndianTest, Load128) {
  uint8_t ascending_values[16] = {0};
  for (int i = 0; i < 16; ++i) {
    ascending_values[i] = i;
  }
  const absl::uint128 value_h = BigEndian::Load128(&ascending_values[0]);
  const absl::uint128 reference_h =
      absl::MakeUint128(0x0001020304050607ULL, 0x08090a0b0c0d0e0fULL);
  EXPECT_EQ(reference_h, value_h);
}

TEST(EndianessTest, BigEndian) {
  // Check BigEndian uint16.
  uint64_t comp = BigEndian::FromHost16(k16Value);
  EXPECT_EQ(comp, k16ValueBE);
  comp = BigEndian::ToHost16(k16ValueBE);
  EXPECT_EQ(comp, k16Value);

  // Check BigEndian uint32.
  comp = BigEndian::FromHost32(k32Value);
  EXPECT_EQ(comp, k32ValueBE);
  comp = BigEndian::ToHost32(k32ValueBE);
  EXPECT_EQ(comp, k32Value);

  // Check BigEndian uint64.
  comp = BigEndian::FromHost64(k64Value);
  EXPECT_EQ(comp, k64ValueBE);
  comp = BigEndian::ToHost64(k64ValueBE);
  EXPECT_EQ(comp, k64Value);

  // Check little-endian Load and store functions.
  uint16_t u16Buf;
  uint32_t u32Buf;
  uint64_t u64Buf;

  BigEndian::Store16(&u16Buf, k16Value);
  EXPECT_EQ(u16Buf, k16ValueBE);
  comp = BigEndian::Load16(&u16Buf);
  EXPECT_EQ(comp, k16Value);

  BigEndian::Store32(&u32Buf, k32Value);
  EXPECT_EQ(u32Buf, k32ValueBE);
  comp = BigEndian::Load32(&u32Buf);
  EXPECT_EQ(comp, k32Value);

  BigEndian::Store64(&u64Buf, k64Value);
  EXPECT_EQ(u64Buf, k64ValueBE);
  comp = BigEndian::Load64(&u64Buf);
  EXPECT_EQ(comp, k64Value);
}

}  // namespace
}  // namespace googlesql_base
