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

// The tests are organized as following:
// 1) TYPED_TEST(Fixed{Uint|Int}GoldenDataTest, *) tests
//    Fixed{Int|Uint}<64,2> and Fixed{Int|Uint}<32,4> using hand-crafted
//    golden inputs and expected outputs;
// 2) TYPED_TEST(FixedIntGeneratedDataTest, *) tests
//    Fixed{Int|Uint}{<64,2>|<64,3>|<32,4>|<32,5>} with generated inputs
//    and derived expectations using native types;
// 3) TEST(Fixed{Uint|Int}Test, *) tests Fixed{Int|Uint} with template
//    parameters not fit into either of the above 2 categories.
//
// Most methods should be tested with 2), which has higher coverage than 1), but
// if the derivation logic is not trivial, then 1) is also necessary.

#include "googlesql/common/multiprecision_int.h"

#include <array>
#include <cstdint>
#include <functional>
#include <ios>
#include <limits>
#include <optional>
#include <ostream>
#include <random>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "googlesql/common/multiprecision_int_impl.h"
#include "gtest/gtest.h"
#include "absl/base/macros.h"
#include "absl/hash/hash.h"
#include "absl/hash/hash_testing.h"
#include "absl/numeric/int128.h"
#include "absl/random/random.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "googlesql/base/mathutil.h"

namespace std {
std::ostream& operator<<(std::ostream& o, unsigned __int128 x) {
  return o << absl::uint128(x);
}

std::ostream& operator<<(std::ostream& o, __int128 x) {
  return x < 0 ? (o << '-' << -absl::uint128(x)) : (o << absl::uint128(x));
}
}  // namespace std

namespace googlesql {
namespace {
using int128 = __int128;
using uint128 = unsigned __int128;
constexpr uint128 kuint128max = ~static_cast<uint128>(0);
constexpr int128 kint128max = kuint128max >> 1;
constexpr int128 kint128min = ~kint128max;
constexpr uint64_t kuint64max = ~static_cast<uint64_t>(0);
constexpr int64_t kint64max = kuint64max >> 1;
constexpr int64_t kint64min = ~kint64max;
constexpr uint64_t kuint32max = ~static_cast<uint32_t>(0);
constexpr int64_t kint32max = kuint32max >> 1;
constexpr int64_t kint32min = ~kint32max;
constexpr uint32_t k1e9 = 1000000000;
constexpr uint64_t k1e18 = static_cast<uint64_t>(k1e9) * k1e9;
constexpr uint64_t k1e19 = k1e18 * 10;
static constexpr std::array<uint128, 39> kPowersOf10 =
    multiprecision_int_impl::PowersAsc<uint128, 1, 10, 39>();

template <typename V>
constexpr V max128();

template <>
constexpr int128 max128<int128>() {
  return kint128max;
}

template <>
constexpr uint128 max128<uint128>() {
  return kuint128max;
}

template <typename V>
constexpr V min128();

template <>
constexpr int128 min128<int128>() {
  return kint128min;
}

template <>
constexpr uint128 min128<uint128>() {
  return 0;
}

// Cannot use multiprecision_int_impl::SafeAbs because
// std::make_unsigned_t<int128> doesn't compile in googlesql oss.
inline uint128 SafeAbs(int128 x) {
  return x < 0 ? -static_cast<uint128>(x) : x;
}
inline uint128 SafeAbs(uint128 x) { return x; }

constexpr uint64_t kSorted64BitValues[] = {
    0,          1,
    999,        1000,
    kint32max,  (1ull << 31),
    kuint32max, (1ull << 32),
    kint64max,  (1ull << 63),
    kuint64max,
};

template <typename V>
struct BinaryOpTestData {
  static_assert(std::is_same_v<V, int128> || std::is_same_v<V, uint128>);
  V input1;
  V input2;
  V expected_result;
  bool expected_overflow = false;
};

// The TYPED_TEST(FixedUintGoldenDataTest, Add) will test input1 + input2 and
// input2 + input1, so the entries below should have abs(input1) <= abs(input2).
constexpr BinaryOpTestData<uint128> kUnsignedAddTestData[] = {
    {0, 0, 0},
    {0, 1, 1},
    {1, 1, 2},
    {10, 20, 30},
    {1, kint32max, static_cast<uint128>(1) << 31},
    {1, kuint32max, static_cast<uint128>(1) << 32},
    {1, kint64max, static_cast<uint128>(1) << 63},
    {1, kuint64max, static_cast<uint128>(1) << 64},
    {1, kint128max, static_cast<uint128>(1) << 127},
    {0, kuint128max, kuint128max},
    {1, kuint128max - 1, kuint128max},
    {1, kuint128max, 0, /* overflow */ true},
    {kint128max, kint128max, kuint128max - 1},
    {kint128max / 2, kint128max / 2 + 1, kint128max},
    {kuint128max / 2 + 1, kint128max, kuint128max},
    {kint128max, kuint128max, kint128max - 1, /* overflow */ true},
    {kuint128max, kuint128max, kuint128max - 1, /* overflow */ true},
};

// The TYPED_TEST(FixedIntGoldenDataTest, Add) will test input1 + input2 and
// input2 + input1, as well as the negated values (as long as int128min is not
// involved), so the entries below should have abs(input1) <= abs(input2),
// and should not include negative inputs unless one of values is int128min.
constexpr BinaryOpTestData<int128> kSignedAddTestData[] = {
    {0, 0, 0},
    {0, 1, 1},
    {1, 1, 2},
    {10, 20, 30},
    {1, kint32max, static_cast<int128>(1) << 31},
    {1, kuint32max, static_cast<int128>(1) << 32},
    {1, kint64max, static_cast<int128>(1) << 63},
    {1, kuint64max, static_cast<int128>(1) << 64},
    {0, kint128max, kint128max},
    {1, kint128max - 1, kint128max},
    {kint128max / 2, kint128max / 2 + 1, kint128max},
    {kint128max, kint128max, -2, /* overflow */ true},

    // Values involing kint128min.
    {1, kint128max, kint128min, /* overflow */ true},
    {-1, -kint128max, kint128min},
    {kint128min / 2, kint128min / 2, kint128min},
    {0, kint128min, kint128min},
    {-1, kint128min + 1, kint128min},
    {-1, kint128min, kint128max, /* overflow */ true},
    {kint128min, kint128min, 0, /* overflow */ true},
    {kint128max, kint128min, -1},
};

constexpr BinaryOpTestData<uint128> kUnsignedMultiplyTestData[] = {
    {0, 0, 0},
    {0, 1, 0},
    {0, kint32max, 0},
    {0, kuint32max, 0},
    {0, kint64max, 0},
    {0, kuint64max, 0},
    {0, kint128max, 0},
    {0, kuint128max, 0},
    {1, 1, 1},
    {1, kint32max, kint32max},
    {1, kuint32max, kuint32max},
    {1, kint64max, kint64max},
    {1, kuint64max, kuint64max},
    {1, kint128max, kint128max},
    {1, kuint128max, kuint128max},
    {2, 5, 10},
    {2, kint32max, kuint32max - 1},
    {2, kuint32max, 2 * static_cast<uint128>(kuint32max)},
    {2, kint64max, kuint64max - 1},
    {2, kuint64max, 2 * static_cast<uint128>(kuint64max)},
    {2, kint128max, kuint128max - 1},
    {2, kuint128max, kuint128max - 1, /* overflow */ true},
    {uint128{1} << 38, uint128{19073486328125ULL} * 19073486328125ULL,
     uint128{10000000000000000000ULL} * 10000000000000000000ULL},
    {uint128{10000000000000000000ULL}, uint128{10000000000000000000ULL},
     uint128{10000000000000000000ULL} * 10000000000000000000ULL},
    {kint32max, kint64max, (uint128{1} << 94) - kint32max - kint64max - 1},
    {kuint32max, kuint64max, (uint128{1} << 96) - kuint32max - kuint64max - 1},
    {kuint64max, kuint64max, kuint128max - kuint64max - kuint64max},
    {uint128{1} << 64, uint128{1} << 64, 0, /* overflow */ true},
    {kint128max, kint128max, 1, /* overflow */ true},
    {uint128{1} << 127, uint128{1} << 127, 0, /* overflow */ true},
    {kuint128max, kuint128max, 1, /* overflow */ true},
};

constexpr BinaryOpTestData<int128> kSignedMultiplyTestData[] = {
    {0, 0, 0},
    {0, 1, 0},
    {0, kint32max, 0},
    {0, kuint32max, 0},
    {0, kint64max, 0},
    {0, kuint64max, 0},
    {0, kint128max, 0},
    {1, 1, 1},
    {1, kint32max, kint32max},
    {1, kuint32max, kuint32max},
    {1, kint64max, kint64max},
    {1, kuint64max, kuint64max},
    {1, kint128max, kint128max},
    {2, 5, 10},
    {2, kint32max, kuint32max - 1},
    {2, kuint32max, 2 * static_cast<int128>(kuint32max)},
    {2, kint64max, kuint64max - 1},
    {2, kuint64max, 2 * static_cast<int128>(kuint64max)},
    {2, kint128max, -2, /* overflow */ true},
    {int128{1} << 38, int128{19073486328125LL} * 19073486328125LL,
     uint128{10000000000000000000ULL} * 10000000000000000000ULL},
    {int128{1000000000000000000LL}, int128{1000000000000000000LL},
     int128{1000000000000000000LL} * 1000000000000000000LL},
    {kint32max, kint64max, (int128{1} << 94) - kint32max - kint64max - 1},
    {kuint32max, kuint64max, (int128{1} << 96) - kuint32max - kuint64max - 1},
    {kint64max, kint64max, (int128{1} << 126) - kint64max - kint64max - 1},
    {kuint64max, kuint64max, int128{-1} - kuint64max - kuint64max,
     /* overflow */ true},
    {kint128max, kint128max, 1, /* overflow */ true},

    {0, kint128min, 0},
    {1, kint128min, kint128min},
    {2, kint128min, 0, /* overflow */ true},
    {kint32max, kint128min, kint128min, /* overflow */ true},
    {kint128min, kint128min, 0, /* overflow */ true},
};

template <typename V>
struct DivModTestData {
  V dividend;
  V divisor;
  V expected_quotient;
  V expected_remainder;
  V expected_rounded_quotient;
  bool expected_overflow = false;
};

constexpr DivModTestData<uint128> kUnsignedDivModTestData[] = {
    {0, 1, 0, 0, 0},
    {0, 2, 0, 0, 0},
    {0, kuint32max, 0, 0, 0},
    {0, uint128{1} << 32, 0, 0, 0},
    {0, kuint128max, 0, 0, 0},
    {1, 1, 1, 0, 1},
    {1, 2, 0, 1, 1},
    {1, 3, 0, 1, 0},
    {1, kuint32max, 0, 1, 0},
    {1, uint128{1} << 32, 0, 1, 0},
    {1, kuint128max, 0, 1, 0},
    {2, 1, 2, 0, 2},
    {2, 2, 1, 0, 1},
    {2, 3, 0, 2, 1},
    {2, kuint32max, 0, 2, 0},
    {2, uint128{1} << 32, 0, 2, 0},
    {2, kuint128max, 0, 2, 0},
    {kint32max, kuint32max, 0, kint32max, 0},
    {uint128{kint32max} + 1, kuint32max, 0, uint128{kint32max} + 1, 1},
    {kuint32max, 1, kuint32max, 0, kuint32max},
    {kuint32max, 2, kint32max, 1, uint128{1} << 31},
    {kuint32max, kuint32max, 1, 0, 1},
    {kuint32max, uint128{1} << 32, 0, kuint32max, 1},
    {kuint32max, kuint128max, 0, kuint32max, 0},
    {kuint64max, 1, kuint64max, 0, kuint64max},
    {kuint64max, 2, kint64max, 1, uint128{1} << 63},
    {kuint64max, kuint32max, uint128{kuint32max} + 2, 0,
     uint128{kuint32max} + 2},
    {kuint64max, uint128{1} << 32, kuint32max, kuint32max, uint128{1} << 32},
    {kuint64max, kuint128max, 0, kuint64max, 0},
    {uint128{1} << 64, 1, uint128{1} << 64, 0, uint128{1} << 64},
    {uint128{1} << 64, 2, uint128{1} << 63, 0, uint128{1} << 63},
    {uint128{1} << 64, kuint32max, uint128{kuint32max} + 2, 1,
     uint128{kuint32max} + 2},
    {uint128{1} << 64, uint128{1} << 32, uint128{1} << 32, 0, uint128{1} << 32},
    {uint128{1} << 64, kuint128max, 0, uint128{1} << 64, 0},
    {kint128max, kuint128max, 0, kint128max, 0},
    {uint128{1} << 127, kuint128max, 0, uint128{1} << 127, 1},
    {kuint128max, 1, kuint128max, 0, kuint128max},
    {kuint128max, 2, kint128max, 1, uint128{1} << 127},
    {kuint128max, uint128{1} << 32, kuint128max >> 32, kuint32max,
     uint128{1} << 96},
    {kuint128max, kuint128max, 1, 0, 1},

    {1234567890123456789LL, 10, 123456789012345678LL, 9, 123456789012345679LL},
    {1234567890123456789LL, 100, 12345678901234567LL, 89, 12345678901234568LL},
    {1234567890123456789LL, 1000, 1234567890123456LL, 789, 1234567890123457LL},
    {1234567890123456789LL, 10000, 123456789012345LL, 6789, 123456789012346LL},
    {1234567890123456789LL, 100000, 12345678901234LL, 56789, 12345678901235LL},
    {1234567890123456789LL, 1000000, 1234567890123LL, 456789, 1234567890123LL},
    {1234567890123456789LL, 10000000, 123456789012LL, 3456789, 123456789012LL},
    {1234567890123456789LL, 100000000, 12345678901LL, 23456789, 12345678901LL},
    {1234567890123456789LL, 1000000000, 1234567890LL, 123456789, 1234567890LL},
    {1234567890123456789LL, 10000000000LL, 123456789, 123456789, 123456789},
    {1234567890123456789LL, 100000000000LL, 12345678, 90123456789LL, 12345679},
    {1234567890123456789LL, 1000000000000LL, 1234567, 890123456789LL, 1234568},
    {1234567890123456789LL, 10000000000000LL, 123456, 7890123456789LL, 123457},
    {1234567890123456789LL, 100000000000000LL, 12345, 67890123456789LL, 12346},
    {1234567890123456789LL, 1000000000000000LL, 1234, 567890123456789LL, 1235},
    {1234567890123456789LL, 10000000000000000LL, 123, 4567890123456789LL, 123},
    {1234567890123456789LL, 100000000000000000LL, 12, 34567890123456789LL, 12},
    {1234567890123456789LL, 1000000000000000000LL, 1, 234567890123456789LL, 1},
    {1234567890123456789LL, 10000000000000000000ULL, 0, 1234567890123456789LL,
     0},
};

constexpr DivModTestData<int128> kSignedDivModTestData[] = {
    {0, 1, 0, 0, 0},
    {0, 2, 0, 0, 0},
    {0, kint32max, 0, 0, 0},
    {0, kuint32max, 0, 0, 0},
    {0, int128{1} << 32, 0, 0, 0},
    {0, kint128max, 0, 0, 0},
    {1, 1, 1, 0, 1},
    {1, 2, 0, 1, 1},
    {1, 3, 0, 1, 0},
    {1, kint32max, 0, 1, 0},
    {1, kuint32max, 0, 1, 0},
    {1, int128{1} << 32, 0, 1, 0},
    {1, kint128max, 0, 1, 0},
    {2, 1, 2, 0, 2},
    {2, 2, 1, 0, 1},
    {2, 3, 0, 2, 1},
    {2, kint32max, 0, 2, 0},
    {2, kuint32max, 0, 2, 0},
    {2, int128{1} << 32, 0, 2, 0},
    {2, kint128max, 0, 2, 0},
    {kint32max, kuint32max, 0, kint32max, 0},
    {int128{kint32max} + 1, kuint32max, 0, int128{kint32max} + 1, 1},
    {kuint32max, 1, kuint32max, 0, kuint32max},
    {kuint32max, 2, kint32max, 1, int128{1} << 31},
    {kuint32max, kint32max, 2, 1, 2},
    {kuint32max, kuint32max, 1, 0, 1},
    {kuint32max, int128{1} << 32, 0, kuint32max, 1},
    {kuint32max, kint128max, 0, kuint32max, 0},
    {kuint64max, 1, kuint64max, 0, kuint64max},
    {kuint64max, 2, kint64max, 1, int128{1} << 63},
    {kuint64max, kint32max, (int128{1} << 33) + 4, 3, (int128{1} << 33) + 4},
    {kuint64max, kuint32max, int128{kuint32max} + 2, 0, int128{kuint32max} + 2},
    {kuint64max, int128{1} << 32, kuint32max, kuint32max, int128{1} << 32},
    {kuint64max, kint128max, 0, kuint64max, 0},
    {int128{1} << 64, 1, int128{1} << 64, 0, int128{1} << 64},
    {int128{1} << 64, 2, int128{1} << 63, 0, int128{1} << 63},
    {int128{1} << 64, kint32max, (int128{1} << 33) + 4, 4,
     (int128{1} << 33) + 4},
    {int128{1} << 64, kuint32max, int128{kuint32max} + 2, 1,
     int128{kuint32max} + 2},
    {int128{1} << 64, int128{1} << 32, int128{1} << 32, 0, int128{1} << 32},
    {int128{1} << 64, kint128max, 0, int128{1} << 64, 0},
    {kint128max >> 1, kint128max, 0, kint128max >> 1, 0},
    {uint128{1} << 126, kint128max, 0, uint128{1} << 126, 1},
    {kint128max, 1, kint128max, 0, kint128max},
    {kint128max, 2, kint128max >> 1, 1, int128{1} << 126},
    {kint128max, kint32max,
     (int128{1} << 96) + (int128{1} << 65) + (int128{1} << 34) + 8, 7,
     (int128{1} << 96) + (int128{1} << 65) + (int128{1} << 34) + 8},
    {kint128max, kuint32max,
     (int128{1} << 95) + (int128{1} << 63) + (int128{1} << 31), kint32max,
     (int128{1} << 95) + (int128{1} << 63) + (int128{1} << 31)},
    {kint128max, int128{1} << 32, kint128max >> 32, kuint32max,
     int128{1} << 95},
    {kint128max, kint128max, 1, 0, 1},

    {1234567890123456789LL, 10, 123456789012345678LL, 9, 123456789012345679LL},
    {1234567890123456789LL, 100, 12345678901234567LL, 89, 12345678901234568LL},
    {1234567890123456789LL, 1000, 1234567890123456LL, 789, 1234567890123457LL},
    {1234567890123456789LL, 10000, 123456789012345LL, 6789, 123456789012346LL},
    {1234567890123456789LL, 100000, 12345678901234LL, 56789, 12345678901235LL},
    {1234567890123456789LL, 1000000, 1234567890123LL, 456789, 1234567890123LL},
    {1234567890123456789LL, 10000000, 123456789012LL, 3456789, 123456789012LL},
    {1234567890123456789LL, 100000000, 12345678901LL, 23456789, 12345678901LL},
    {1234567890123456789LL, 1000000000, 1234567890LL, 123456789, 1234567890LL},
    {1234567890123456789LL, 10000000000LL, 123456789, 123456789, 123456789},
    {1234567890123456789LL, 100000000000LL, 12345678, 90123456789LL, 12345679},
    {1234567890123456789LL, 1000000000000LL, 1234567, 890123456789LL, 1234568},
    {1234567890123456789LL, 10000000000000LL, 123456, 7890123456789LL, 123457},
    {1234567890123456789LL, 100000000000000LL, 12345, 67890123456789LL, 12346},
    {1234567890123456789LL, 1000000000000000LL, 1234, 567890123456789LL, 1235},
    {1234567890123456789LL, 10000000000000000LL, 123, 4567890123456789LL, 123},
    {1234567890123456789LL, 100000000000000000LL, 12, 34567890123456789LL, 12},
    {1234567890123456789LL, 1000000000000000000LL, 1, 234567890123456789LL, 1},
    {1234567890123456789LL, 10000000000000000000ULL, 0, 1234567890123456789LL,
     0},

    {0, kint128min, 0, 0, 0},
    {1, kint128min, 0, 1, 0},
    {-1, kint128min, 0, -1, 0},
    {kint128max, kint128min, 0, kint128max, -1},
    {-kint128max, kint128min, 0, -kint128max, 1},
    {kint128min, 1, kint128min, 0, kint128min},
    {kint128min, -1, kint128min, 0, kint128min, /* overflow */ true},
    {kint128min, 2, -(int128{1} << 126), 0, -(int128{1} << 126)},
    {kint128min, -2, (int128{1} << 126), 0, (int128{1} << 126)},
    {kint128min, kint32max,
     -(int128{1} << 96) - (int128{1} << 65) - (int128{1} << 34) - 8, -8,
     -(int128{1} << 96) - (int128{1} << 65) - (int128{1} << 34) - 8},
    {kint128min, -kint32max,
     (int128{1} << 96) + (int128{1} << 65) + (int128{1} << 34) + 8, -8,
     (int128{1} << 96) + (int128{1} << 65) + (int128{1} << 34) + 8},
    {kint128min, kuint32max,
     -(int128{1} << 95) - (int128{1} << 63) - (int128{1} << 31),
     -(int128{1} << 31),
     -(int128{1} << 95) - (int128{1} << 63) - (int128{1} << 31) - 1},
    {kint128min, -int128{kuint32max},
     (int128{1} << 95) + (int128{1} << 63) + (int128{1} << 31),
     -(int128{1} << 31),
     (int128{1} << 95) + (int128{1} << 63) + (int128{1} << 31) + 1},
    {kint128min, int128{1} << 32, -(int128{1} << 95), 0, -(int128{1} << 95)},
    {kint128min, -(int128{1} << 32), int128{1} << 95, 0, int128{1} << 95},
    {kint128min, kint128min, 1, 0, 1},
};

constexpr int64_t kCastDoubleTestData64[] = {
    0,
    // * Lowest 53 bits set
    0x1fffffffffffff,
    // * Lowest 54 bits set
    0x3fffffffffffff,
    // * Lowest 55 bits set
    0x7fffffffffffff,
    // Rounding to nearest and ties to even, bits in comments are the 53rd to
    // 56th bits.
    // Round down to 0x7ffffffffffff800
    0x7ffffffffffff880,  // 0, 0, 0, 1
    0x7ffffffffffff900,  // 0, 0, 1, 0
    0x7ffffffffffff9ff,  // 0, 0, 1, 1
    0x7ffffffffffffa00,  // 0, 1, 0, 0 with trailing zeros. Ties to even
    // Round up/down to 0x7ffffffffffffc00
    0x7ffffffffffffa01,  // 0, 1, 0, 0 with one at the end
    0x7ffffffffffffa80,  // 0, 1, 0, 1
    0x7ffffffffffffd00,  // 1, 0, 1, 0
    0x7ffffffffffffdff,  // 1, 0, 1, 1 with trailing ones
    // Round up to 0x8000000000000000
    0x7ffffffffffffe00,  // 1, 1, 0, 0 with trailing zeros. Ties to even
    0x7ffffffffffffe01,  // 1, 1, 0, 0 with one at the end
    // Most significant bit at different position of words (for 32-bit width)
    0x1ffffffff,
    0x1ffffffffffff,
};

constexpr int128 kCastDoubleTestData128[] = {
    static_cast<int128>(absl::MakeUint128(0, 0)),
    // * Lowest 53 bits set
    static_cast<int128>(absl::MakeUint128(0, 0x1fffffffffffff)),
    // * Lowest 54 bits set
    static_cast<int128>(absl::MakeUint128(0, 0x3fffffffffffff)),
    // * Lowest 55 bits set
    static_cast<int128>(absl::MakeUint128(0, 0x7fffffffffffff)),
    // Rounding to nearest and ties to even, bits in comments are the 53rd to
    // 56th bits.
    // Round down to 0x7ffffffffffff800 for first 64 bits
    static_cast<int128>(
        absl::MakeUint128(0x7ffffffffffff880, 0)),  // 0, 0, 0, 1
    static_cast<int128>(
        absl::MakeUint128(0x7ffffffffffff900, 0)),  // 0, 0, 1, 0
    static_cast<int128>(
        absl::MakeUint128(0x7ffffffffffff9ff, 0)),  // 0, 0, 1, 1
    static_cast<int128>(
        absl::MakeUint128(0x7ffffffffffffa00,
                          0)),  // 0, 1, 0, 0 with trailing zeros. Ties to even
    // Round up/down to 0x7ffffffffffffc00 for first 64 bits
    static_cast<int128>(
        absl::MakeUint128(0x7ffffffffffffa00,
                          1)),  // 0, 1, 0, 0 with one at the end
    static_cast<int128>(
        absl::MakeUint128(0x7ffffffffffffa80, 0)),  // 0, 1, 0, 1
    static_cast<int128>(
        absl::MakeUint128(0x7ffffffffffffd00, 0)),  // 1, 0, 1, 0
    static_cast<int128>(absl::MakeUint128(
        0x7ffffffffffffdff,
        0xffffffffffffffff)),  // 1, 0, 1, 1 with trailing ones
    // Round up to 0x8000000000000000 for first 64 bits
    static_cast<int128>(
        absl::MakeUint128(0x7ffffffffffffe00,
                          0)),  // 1, 1, 0, 0 with trailing zeros. Ties to even
    static_cast<int128>(
        absl::MakeUint128(0x7ffffffffffffe00,
                          1)),  // 1, 1, 0, 0 with one at the end
    // Most significant bit at different position of words (for 32/64-bit word)
    static_cast<int128>(absl::MakeUint128(0, 0x1ffffffff)),
    static_cast<int128>(absl::MakeUint128(0, 0x1ffffffffffff)),
    static_cast<int128>(absl::MakeUint128(0, 0xffffffffffffffff)),
    static_cast<int128>(absl::MakeUint128(1, 0xffffffffffffffff)),
    static_cast<int128>(absl::MakeUint128(0xffffffff, 0xffffffff00000000)),
    static_cast<int128>(absl::MakeUint128(0x1ffffffff, 0xffffffff00000000)),
    // 53rd significant bit at different position of words (for 32/64-bit word)
    static_cast<int128>(absl::MakeUint128(0, 0x001fffffffffffff)),
    static_cast<int128>(absl::MakeUint128(0, 0x1fffffffffffff00)),
    static_cast<int128>(absl::MakeUint128(0, 0x1fffffffffffffff)),
    static_cast<int128>(absl::MakeUint128(0, 0xffffffffffffffff)),
    static_cast<int128>(absl::MakeUint128(0xfffff, 0xffffffff80000000)),
    static_cast<int128>(absl::MakeUint128(0x1fffff, 0xffffffff00000000)),
    static_cast<int128>(absl::MakeUint128(0xfffffffffffff, 0x8000000000000000)),
    static_cast<int128>(absl::MakeUint128(0x1fffffffffffff, 0)),
};

constexpr std::pair<int128, absl::string_view> kSerializedSignedValues[] = {
    {kint128min,
     {"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x80", 16}},
    {kint64min, {"\x00\x00\x00\x00\x00\x00\x00\x80", 8}},
    {-4294967296, {"\x00\x00\x00\x00\xff", 5}},
    {-16777216, {"\x00\x00\x00\xff", 4}},
    {-65536, {"\x00\x00\xff", 3}},
    {-32512, {"\x00\x81", 2}},
    {-32768, {"\x00\x80", 2}},
    {-1000, {"\x18\xfc", 2}},
    {-256, {"\x00\xff", 2}},
    {-1, {"\xff", 1}},
    {0, {"\x00", 1}},
    {1, {"\x01", 1}},
    {255, {"\xff\x00", 2}},
    {256, {"\x00\x01", 2}},
    {1000, {"\xe8\x03", 2}},
    {32768, {"\x00\x80\x00", 3}},
    {65536, {"\x00\x00\x01", 3}},
    {16777216, {"\x00\x00\x00\x01", 4}},
    {(1LL << 32) - 1, {"\xff\xff\xff\xff\x00", 5}},
    {(1LL << 32), {"\x00\x00\x00\x00\x01", 5}},
    {kint64max, "\xff\xff\xff\xff\xff\xff\xff\x7f"},
    {kint128max,
     "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\x7f"},
};

constexpr std::pair<uint128, absl::string_view> kSerializedUnsignedValues[] = {
    {0, {"\x00", 1}},
    {1, {"\x01", 1}},
    {255, {"\xff", 1}},
    {256, {"\x00\x01", 2}},
    {1000, {"\xe8\x03", 2}},
    {32768, {"\x00\x80", 2}},
    {65536, {"\x00\x00\x01", 3}},
    {16777216, {"\x00\x00\x00\x01", 4}},
    {(1ULL << 32) - 1, {"\xff\xff\xff\xff", 4}},
    {(1ULL << 32), {"\x00\x00\x00\x00\x01", 5}},
    {(1ULL << 63) - 1, {"\xff\xff\xff\xff\xff\xff\xff\x7f", 8}},
    {(1ULL << 63), {"\x00\x00\x00\x00\x00\x00\x00\x80", 8}},
    {kuint64max, "\xff\xff\xff\xff\xff\xff\xff\xff"},
    {kuint128max,
     "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff"},
};

constexpr absl::string_view kInvalidSerialized128BitValues[] = {
    "",
    "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff",
};

constexpr std::pair<int128, absl::string_view> kSignedValueStrings[] = {
    {kint128min, "-170141183460469231731687303715884105728"},
    {kint64min, "-9223372036854775808"},
    {-281474976710656, "-281474976710656"},
    {-4294967296, "-4294967296"},
    {-268435456, "-268435456"},
    {-16777216, "-16777216"},
    {-65536, "-65536"},
    {-32512, "-32512"},
    {-32768, "-32768"},
    {-1000, "-1000"},
    {-256, "-256"},
    {-1, "-1"},
    {0, "0"},
    {1, "1"},
    {255, "255"},
    {256, "256"},
    {1000, "1000"},
    {32768, "32768"},
    {65536, "65536"},
    {16777216, "16777216"},
    {268435456, "268435456"},
    {(1LL << 32) - 1, "4294967295"},
    {(1LL << 32), "4294967296"},
    {281474976710656, "281474976710656"},
    {kint64max, "9223372036854775807"},
    {kint128max, "170141183460469231731687303715884105727"},
};

constexpr std::pair<uint128, absl::string_view> kUnsignedValueStrings[] = {
    {0, "0"},
    {1, "1"},
    {9, "9"},
    {10, "10"},
    {11, "11"},
    {255, "255"},
    {256, "256"},
    {999, "999"},
    {1000, "1000"},
    {32768, "32768"},
    {65536, "65536"},
    {16777216, "16777216"},
    {268435456, "268435456"},
    {k1e9 - 1, "999999999"},
    {k1e9, "1000000000"},
    {k1e9 + 1, "1000000001"},
    {(1LL << 32) - 1, "4294967295"},
    {(1LL << 32), "4294967296"},
    {281474976710656, "281474976710656"},
    {(1ULL << 63) - 1, "9223372036854775807"},
    {(1ULL << 63), "9223372036854775808"},
    {kPowersOf10[19] - 1, "9999999999999999999"},
    {kPowersOf10[19], "10000000000000000000"},
    {kPowersOf10[19] + 1, "10000000000000000001"},
    {kuint64max, "18446744073709551615"},
    {kPowersOf10[38] - 1, "99999999999999999999999999999999999999"},
    {kPowersOf10[38], "100000000000000000000000000000000000000"},
    {kPowersOf10[38] + 1, "100000000000000000000000000000000000001"},
    {kuint128max, "340282366920938463463374607431768211455"},
};

constexpr std::pair<uint128, absl::string_view>
    kNonCanonicalUnsignedValueStrings[] = {
        // Here number with 0 prefixes will not be parsed as an octal number.
        {0, "0000000000000000000000000"},
        {1, "0000000000000000000000001"},
        {kuint64max, "0000000000000000000018446744073709551615"},
        {kuint128max, "000000000000340282366920938463463374607431768211455"},
};

constexpr std::pair<int128, absl::string_view>
    kNonCanonicalSignedValueStrings[] = {
        // Here number with 0 prefixes will not be parsed as an octal number.
        {0, "0000000000000000000000000"},
        {0, "-0"},
        {0, "-000"},
        {1, "0000000000000000000000001"},
        {1, "+1"},
        {1, "+01"},
        {kuint64max, "0000000000000000000018446744073709551615"},
        {kuint64max, "+18446744073709551615"},
        {kuint64max, "+0000000000000000000018446744073709551615"},
        {kint128max, "000000000000170141183460469231731687303715884105727"},
        {kint128max, "+170141183460469231731687303715884105727"},
        {kint128max, "+000000000000170141183460469231731687303715884105727"},
};

constexpr absl::string_view kSigned128BitValueInvalidStrings[] = {
    "",
    "-",
    "1.0",
    "-1.0",
    "1e2",
    "-1e2",
    "1.0e2",
    "-1.0e2",
    "+-1",
    "-+1",
    " 1",
    "1 ",
    "1 1",
    "0b1",
    "0x1",
    // -2^127 - 1
    "-170141183460469231731687303715884105729",
    // 2^127
    "170141183460469231731687303715884105728",
    // Valid numeric string + non-numeric characters
    "-170141183460469231731687303715884105728Invalid",
    "170141183460469231731687303715884105727Invalid",
    // Non-numeric string
    "Invalid",
    "-Invalid",
    "nan",
    "inf",
    "-inf",
};

constexpr absl::string_view kUnsigned128BitValueInvalidStrings[] = {
    "",
    "-",
    "-1",
    "1.0",
    "1e2",
    "1.0e2",
    "+1",
    " 1",
    "1 ",
    "1 1",
    "0b1",
    "0x1",
    // 2^128
    "340282366920938463463374607431768211456",
    // Valid numeric string + non-numeric characters
    "340282366920938463463374607431768211455Invalid"
    // Non-numeric string
    "Invalid",
    "nan",
    "inf",
};

constexpr std::pair<int128, absl::string_view> kSignedValueStringSegments[] = {
    {kint128min, "-1,70141183460469231731687303715884105728"},
    {kint64min, "-092233,72036854775808"},
    {-1000, "-1,0,00"},
    {-256, "-2,5,6"},
    {-1, "-0,1"},
    {0, "0,0"},
    {1, "+0,01"},
    {256, "2,56,"},
    {1000, "01,000"},
    {kint64max, "+009,223,372,036,854,775,807"},
    {kint128max, "000000000000,17014118346046923173168730371588410572,7"},
};

constexpr std::pair<uint128, absl::string_view> kUnsignedValueStringSegments[] =
    {
        {0, "0,0,"},
        {1, "00,01"},
        {256, "2,5,6"},
        {1000, "1,000"},
        {kuint64max, "18,446,744,073,709,551,615"},
        {kuint128max, "3,40282366920938463463374607431768211455,"},
        {kuint128max, "34028236692093846346337460743176821145,5"},
        {kuint128max, "000000000000,340282366920938463463374607431768211455"},
        {kuint128max, "0,0,0,0,0,0,0340282366920938463463374607431768211455"},
};

constexpr absl::string_view kSigned128BitValueInvalidStringSegments[] = {
    "",
    "-,1",
    "+,1",
    "1,.0",
    "-1,.0",
    "1,e2",
    "-1,e2",
    "1,.0e2",
    "-1,.0e2",
    "0, 1",
    "1 ,",
    "1 ,1",
    "0,b1",
    "0x,1",
    // -2^127 - 1
    "-17014118346046923173168730371588410572,9",
    // 2^127
    "17014118346046923173168730371588410572,8",
    // Valid numeric string + non-numeric characters
    "-170141183460469231731687303715884105728,Invalid",
    "170141183460469231731687303715884105727,Invalid",
    // Non-numeric string
    "In,valid",
    "-In,valid",
};

constexpr absl::string_view kUnsigned128BitValueInvalidStringSegments[] = {
    "",
    "-,1",
    "1,.0",
    "1,e2",
    "1.,0e2",
    "+,1",
    " ,1",
    "1, ",
    "1 ,1",
    "0,b1",
    "0x,1",
    // 2^128
    "3,40282366920938463463374607431768211456",
    // Overflow when adding the second segment
    "34028236692093846346337460743176821145,6",
    // Overflow when scaling up the first segment
    "34028236692093846346337460743176821146,0",
    // Valid numeric string + non-numeric characters
    "34028236692093846346337460743176821145,5Invalid"
    // Non-numeric string
    "In,valid",
};

constexpr std::pair<uint128, int> kFindMSBSetNonZeroTestData[] = {
    {0, 0},
    {1, 0},
    {0x10000, 16},
    {0x1ffff, 16},
    {0x40000, 18},
    {0x4ff00, 18},
    {0x10000000000000, 52},
    {0x123456789abcde, 52},
    {kuint64max, 63},
    {uint128{1} << 127, 127},
    {kuint128max, 127},
};

constexpr std::pair<std::pair<bool, uint128>, std::pair<bool, int128>>
    kSetSignAndAbsTestData[] = {
        {{false, 0}, {true, 0}},
        {{true, 0}, {true, 0}},
        {{false, 1}, {true, 1}},
        {{true, 1}, {true, -1}},
        {{false, 1000}, {true, 1000}},
        {{true, 1000}, {true, -1000}},
        {{false, kuint64max}, {true, kuint64max}},
        {{true, kuint64max}, {true, -static_cast<int128>(kuint64max)}},
        {{false, kint128max}, {true, kint128max}},
        {{true, kint128max}, {true, -kint128max}},
        {{false, uint128{1} << 127}, {false, uint128{1} << 127}},
        {{true, uint128{1} << 127}, {true, kint128min}},
        {{false, (uint128{1} << 127) + 1}, {false, (uint128{1} << 127) + 1}},
        {{true, (uint128{1} << 127) + 1}, {false, kint128max}},
        {{false, kuint128max}, {false, kuint128max}},
        {{true, kuint128max}, {false, 1}},
};

template <typename TypeParam>
class FixedUintGoldenDataTest : public ::testing::Test {};

template <typename TypeParam>
class FixedIntGoldenDataTest : public ::testing::Test {};

using FixedUint128Types = ::testing::Types<FixedUint<64, 2>, FixedUint<32, 4>>;
TYPED_TEST_SUITE(FixedUintGoldenDataTest, FixedUint128Types);

using FixedInt128Types = ::testing::Types<FixedInt<64, 2>, FixedInt<32, 4>>;
TYPED_TEST_SUITE(FixedIntGoldenDataTest, FixedInt128Types);

template <typename T, typename V, typename RhsType>
void TestAdd(V x, V y, T expected_sum, bool expected_overflow) {
  T lhs(x);
  const RhsType rhs(y);
  T& result = (lhs += rhs);
  EXPECT_EQ(&result, &lhs);
  EXPECT_EQ(lhs, expected_sum) << std::hex << std::showbase << x << " + " << y;

  lhs = T(x);
  EXPECT_EQ(lhs.AddOverflow(rhs), expected_overflow)
      << std::hex << std::showbase << x << " AddOverflow " << y;
  EXPECT_EQ(lhs, expected_sum)
      << std::hex << std::showbase << x << " AddOverflow " << y;
}

template <typename T, typename V>
void TestAddWithVariousRhsTypes(V x, V y, T expected_sum,
                                bool expected_overflow) {
  TestAdd<T, V, T>(x, y, expected_sum, expected_overflow);
  using Word = typename T::Word;
  if (y >= std::numeric_limits<Word>::min() &&
      y <= std::numeric_limits<Word>::max()) {
    TestAdd<T, V, Word>(x, y, expected_sum, expected_overflow);
  }
  using UnsignedWord = typename T::UnsignedWord;
  if (!std::is_same_v<UnsignedWord, Word> &&
      y >= std::numeric_limits<UnsignedWord>::min() &&
      y <= std::numeric_limits<UnsignedWord>::max()) {
    TestAdd<T, V, UnsignedWord>(x, y, expected_sum, expected_overflow);
  }
}

TYPED_TEST(FixedUintGoldenDataTest, Add) {
  using T = TypeParam;
  for (const BinaryOpTestData<uint128>& item : kUnsignedAddTestData) {
    const uint128 x = item.input1;
    const uint128 y = item.input2;
    const uint128 sum = item.expected_result;
    TestAddWithVariousRhsTypes(x, y, T(sum), item.expected_overflow);
    TestAddWithVariousRhsTypes(y, x, T(sum), item.expected_overflow);
  }
}

TYPED_TEST(FixedIntGoldenDataTest, Add) {
  using T = TypeParam;
  for (const BinaryOpTestData<int128>& item : kSignedAddTestData) {
    const int128 x = item.input1;
    const int128 y = item.input2;
    const int128 sum = item.expected_result;
    TestAddWithVariousRhsTypes(x, y, T(sum), item.expected_overflow);
    TestAddWithVariousRhsTypes(y, x, T(sum), item.expected_overflow);

    if (x != kint128min && y != kint128min && sum != kint128min) {
      TestAddWithVariousRhsTypes(-x, -y, T(-sum), item.expected_overflow);
      TestAddWithVariousRhsTypes(-y, -x, T(-sum), item.expected_overflow);

      TestAddWithVariousRhsTypes(sum, -x, T(y), item.expected_overflow);
      TestAddWithVariousRhsTypes(-x, sum, T(y), item.expected_overflow);
      TestAddWithVariousRhsTypes(sum, -y, T(x), item.expected_overflow);
      TestAddWithVariousRhsTypes(-y, sum, T(x), item.expected_overflow);

      TestAddWithVariousRhsTypes(-sum, x, T(-y), item.expected_overflow);
      TestAddWithVariousRhsTypes(x, -sum, T(-y), item.expected_overflow);
      TestAddWithVariousRhsTypes(-sum, y, T(-x), item.expected_overflow);
      TestAddWithVariousRhsTypes(y, -sum, T(-x), item.expected_overflow);
    }
  }
}

template <typename T, typename V, typename RhsType>
void TestSubtract(V x, V y, T expected_sum, bool expected_overflow) {
  T lhs(x);
  const RhsType rhs(y);
  T& result = (lhs -= rhs);
  EXPECT_EQ(&result, &lhs);
  EXPECT_EQ(lhs, expected_sum) << std::hex << std::showbase << x << " - " << y;

  lhs = T(x);
  EXPECT_EQ(lhs.SubtractOverflow(rhs), expected_overflow)
      << std::hex << std::showbase << x << " SubtractOverflow " << y;
  EXPECT_EQ(lhs, expected_sum)
      << std::hex << std::showbase << x << " SubtractOverflow " << y;
}

template <typename T, typename V>
void TestSubtractWithVariousRhsTypes(V x, V y, T expected_sum,
                                     bool expected_overflow) {
  TestSubtract<T, V, T>(x, y, expected_sum, expected_overflow);
  using Word = typename T::Word;
  if (y >= std::numeric_limits<Word>::min() &&
      y <= std::numeric_limits<Word>::max()) {
    TestSubtract<T, V, Word>(x, y, expected_sum, expected_overflow);
  }
  using UnsignedWord = typename T::UnsignedWord;
  if (!std::is_same_v<UnsignedWord, Word> &&
      y >= std::numeric_limits<UnsignedWord>::min() &&
      y <= std::numeric_limits<UnsignedWord>::max()) {
    TestSubtract<T, V, UnsignedWord>(x, y, expected_sum, expected_overflow);
  }
}

TYPED_TEST(FixedUintGoldenDataTest, Subtract) {
  using T = TypeParam;
  for (const BinaryOpTestData<uint128>& item : kUnsignedAddTestData) {
    const uint128 x = item.input1;
    const uint128 y = item.input2;
    const uint128 sum = item.expected_result;
    TestSubtractWithVariousRhsTypes(sum, x, T(y), item.expected_overflow);
    TestSubtractWithVariousRhsTypes(sum, y, T(x), item.expected_overflow);
  }
}

TYPED_TEST(FixedIntGoldenDataTest, Subtract) {
  using T = TypeParam;
  for (const BinaryOpTestData<int128>& item : kSignedAddTestData) {
    const int128 x = item.input1;
    const int128 y = item.input2;
    const int128 sum = item.expected_result;
    TestSubtractWithVariousRhsTypes(sum, x, T(y), item.expected_overflow);
    TestSubtractWithVariousRhsTypes(sum, y, T(x), item.expected_overflow);
    if (x != kint128min && y != kint128min && sum != kint128min) {
      TestSubtractWithVariousRhsTypes(-sum, -x, T(-y), item.expected_overflow);
      TestSubtractWithVariousRhsTypes(-sum, -y, T(-x), item.expected_overflow);

      TestSubtractWithVariousRhsTypes(x, -y, T(sum), item.expected_overflow);
      TestSubtractWithVariousRhsTypes(y, -x, T(sum), item.expected_overflow);
      TestSubtractWithVariousRhsTypes(-x, y, T(-sum), item.expected_overflow);
      TestSubtractWithVariousRhsTypes(-y, x, T(-sum), item.expected_overflow);

      TestSubtractWithVariousRhsTypes(x, sum, T(-y), item.expected_overflow);
      TestSubtractWithVariousRhsTypes(y, sum, T(-x), item.expected_overflow);
      TestSubtractWithVariousRhsTypes(-x, -sum, T(y), item.expected_overflow);
      TestSubtractWithVariousRhsTypes(-y, -sum, T(x), item.expected_overflow);
    }
  }
}

template <typename T, typename V, typename RhsType>
void TestMultiply(V x, V y, T expected_product, bool expected_overflow) {
  T lhs(x);
  const RhsType rhs(y);
  T& result = (lhs *= rhs);
  EXPECT_EQ(&result, &lhs);
  EXPECT_EQ(lhs, expected_product)
      << std::hex << std::showbase << x << " * " << y;

  lhs = T(x);
  EXPECT_EQ(lhs.MultiplyOverflow(rhs), expected_overflow)
      << std::hex << std::showbase << x << " MultiplyOverflow " << y;
  EXPECT_EQ(lhs, expected_product)
      << std::hex << std::showbase << x << " MultiplyOverflow " << y;
}

template <typename T, typename V>
void TestMultiplyWithVariousRhsTypes(V x, V y, T expected_product,
                                     bool expected_overflow) {
  TestMultiply<T, V, T>(x, y, expected_product, expected_overflow);
  using Word = typename T::Word;
  if (y >= std::numeric_limits<Word>::min() &&
      y <= std::numeric_limits<Word>::max()) {
    TestMultiply<T, V, Word>(x, y, expected_product, expected_overflow);
  }
  using UnsignedWord = typename T::UnsignedWord;
  if (!std::is_same_v<UnsignedWord, Word> &&
      y >= std::numeric_limits<UnsignedWord>::min() &&
      y <= std::numeric_limits<UnsignedWord>::max()) {
    TestMultiply<T, V, UnsignedWord>(x, y, expected_product, expected_overflow);
  }

  // Test ExtendAndMultiply.

  const T tx(x);
  const T ty(y);
  // Truncate the result of ExtendAndMultiply to T.
  EXPECT_EQ(T(ExtendAndMultiply(tx, ty)), expected_product);

  // When x has <= 64 bits and y has <= T::kNumBits - 64 bits, test
  // ExtendAndMultiply(64-bit, T::kNumBits - 64 bits) and
  // ExtendAndMultiply(T::kNumBits - 64 bits, 64-bit) without truncating
  // the result.
  constexpr int kNumBitsPerWord = sizeof(Word) * 8;
  constexpr int kWordCount1 = sizeof(int64_t) / sizeof(Word);
  using T1 = std::conditional_t<std::is_signed_v<Word>,
                                FixedInt<kNumBitsPerWord, kWordCount1>,
                                FixedUint<kNumBitsPerWord, kWordCount1>>;
  constexpr int kWordCount2 = (sizeof(T) - sizeof(int64_t)) / sizeof(Word);
  using T2 = std::conditional_t<std::is_signed_v<Word>,
                                FixedInt<kNumBitsPerWord, kWordCount2>,
                                FixedUint<kNumBitsPerWord, kWordCount2>>;
  static_assert(sizeof(T1) + sizeof(T2) == sizeof(T));
  T1 truncated_x(tx);
  T2 truncated_y(ty);
  if (T(truncated_x) == tx && T(truncated_y) == ty) {
    EXPECT_EQ(ExtendAndMultiply(truncated_x, truncated_y), expected_product);
    EXPECT_EQ(ExtendAndMultiply(truncated_y, truncated_x), expected_product);
  }
}

TYPED_TEST(FixedUintGoldenDataTest, Multiply) {
  using T = TypeParam;
  for (const BinaryOpTestData<uint128>& item : kUnsignedMultiplyTestData) {
    const uint128 x = item.input1;
    const uint128 y = item.input2;
    const uint128 product = item.expected_result;
    TestMultiplyWithVariousRhsTypes(x, y, T(product), item.expected_overflow);
    TestMultiplyWithVariousRhsTypes(y, x, T(product), item.expected_overflow);
  }
}

TYPED_TEST(FixedIntGoldenDataTest, Multiply) {
  using T = TypeParam;
  for (const BinaryOpTestData<int128>& item : kSignedMultiplyTestData) {
    const int128 x = item.input1;
    const int128 y = item.input2;
    const int128 product = item.expected_result;
    bool expected_overflow = item.expected_overflow;
    TestMultiplyWithVariousRhsTypes(x, y, T(product), expected_overflow);
    TestMultiplyWithVariousRhsTypes(y, x, T(product), expected_overflow);

    if (x != kint128min && y != kint128min && product != kint128min) {
      TestMultiplyWithVariousRhsTypes(-x, y, T(-product), expected_overflow);
      TestMultiplyWithVariousRhsTypes(y, -x, T(-product), expected_overflow);
      TestMultiplyWithVariousRhsTypes(x, -y, T(-product), expected_overflow);
      TestMultiplyWithVariousRhsTypes(-y, x, T(-product), expected_overflow);
      TestMultiplyWithVariousRhsTypes(-x, -y, T(product), expected_overflow);
      TestMultiplyWithVariousRhsTypes(-y, -x, T(product), expected_overflow);
    }
  }
}

template <typename T, typename V, typename RhsType>
void TestDivModOps(V x, V y, T expected_quotient, T expected_remainder,
                   std::optional<T> expected_rounded_quotient) {
  T lhs(x);
  const RhsType rhs(y);
  T& quotient = (lhs /= rhs);
  EXPECT_EQ(&quotient, &lhs);
  EXPECT_EQ(lhs, expected_quotient)
      << std::hex << std::showbase << x << " / " << y;

  lhs = T(x);
  T& remainder = (lhs %= rhs);
  EXPECT_EQ(&remainder, &lhs);
  EXPECT_EQ(lhs, expected_remainder)
      << std::hex << std::showbase << x << " % " << y;

  if (expected_rounded_quotient.has_value()) {
    lhs = T(x);
    T& rounded_quotient = (lhs.DivAndRoundAwayFromZero(rhs));
    EXPECT_EQ(&rounded_quotient, &lhs);
    EXPECT_EQ(lhs, expected_rounded_quotient.value())
        << std::hex << std::showbase << x << " DivAndRoundAwayFromZero " << y;
  }
}

template <typename T, typename V, typename RhsType>
void TestDivMod(V x, V y, T expected_quotient, RhsType expected_remainder) {
  T lhs(x);
  const RhsType rhs(y);
  T quotient;
  RhsType remainder;
  lhs.DivMod(rhs, &quotient, &remainder);
  EXPECT_EQ(quotient, expected_quotient)
      << std::hex << std::showbase << x << " DivMod " << y;
  EXPECT_EQ(remainder, expected_remainder)
      << std::hex << std::showbase << x << " DivMod " << y;
}

template <typename T, typename V>
void TestDivModWithVariousRhsTypes(V x, V y, T expected_quotient,
                                   V expected_remainder,
                                   std::optional<T> expected_rounded_quotient) {
  TestDivModOps<T, V, T>(x, y, expected_quotient, T(expected_remainder),
                         expected_rounded_quotient);
  TestDivMod<T, V, T>(x, y, expected_quotient, T(expected_remainder));
  using Word = typename T::Word;
  if (y >= std::numeric_limits<Word>::min() &&
      y <= std::numeric_limits<Word>::max()) {
    TestDivModOps<T, V, Word>(x, y, expected_quotient, T(expected_remainder),
                              expected_rounded_quotient);
    TestDivMod<T, V, Word>(x, y, expected_quotient, expected_remainder);
  }
  using UnsignedWord = typename T::UnsignedWord;
  if (!std::is_same_v<UnsignedWord, Word> &&
      y >= std::numeric_limits<UnsignedWord>::min() &&
      y <= std::numeric_limits<UnsignedWord>::max()) {
    TestDivModOps<T, V, UnsignedWord>(x, y, expected_quotient,
                                      T(expected_remainder),
                                      expected_rounded_quotient);
  }
}

TYPED_TEST(FixedUintGoldenDataTest, DivMod) {
  using T = TypeParam;
  for (const DivModTestData<uint128>& item : kUnsignedDivModTestData) {
    const uint128 x = item.dividend;
    const uint128 y = item.divisor;
    const uint128 quotient = item.expected_quotient;
    const uint128 remainder = item.expected_remainder;
    const uint128 rounded_quotient = item.expected_rounded_quotient;
    TestDivModWithVariousRhsTypes<T>(x, y, T(quotient), remainder,
                                     T(rounded_quotient));
    if (quotient != 0) {
      TestDivModWithVariousRhsTypes<T>(x, quotient, T(y + remainder / quotient),
                                       remainder % quotient, std::nullopt);
    }
  }
  for (const BinaryOpTestData<uint128>& item : kUnsignedMultiplyTestData) {
    const uint128 x = item.input1;
    const uint128 y = item.input2;
    const uint128 product = item.expected_result;
    if (!item.expected_overflow) {
      if (x != 0) {
        TestDivModWithVariousRhsTypes<T>(product, x, T(y), uint128{0}, T(y));
      }
      if (y != 0) {
        TestDivModWithVariousRhsTypes<T>(product, y, T(x), uint128{0}, T(x));
      }
    }
  }
}

TYPED_TEST(FixedIntGoldenDataTest, DivMod) {
  using T = TypeParam;
  for (const DivModTestData<int128>& item : kSignedDivModTestData) {
    const int128 x = item.dividend;
    const int128 y = item.divisor;
    const int128 quotient = item.expected_quotient;
    const int128 remainder = item.expected_remainder;
    const int128 rounded_quotient = item.expected_rounded_quotient;
    TestDivModWithVariousRhsTypes<T>(x, y, T(quotient), remainder,
                                     T(rounded_quotient));
    if (quotient != 0 && !item.expected_overflow) {
      TestDivModWithVariousRhsTypes<T>(x, quotient, T(y + remainder / quotient),
                                       remainder % quotient, std::nullopt);
    }
    if (x != kint128min && y != kint128min && quotient != kint128min) {
      TestDivModWithVariousRhsTypes<T>(x, -y, T(-quotient), remainder,
                                       T(-rounded_quotient));
      TestDivModWithVariousRhsTypes<T>(-x, y, T(-quotient), -remainder,
                                       T(-rounded_quotient));
      TestDivModWithVariousRhsTypes<T>(-x, -y, T(quotient), -remainder,
                                       T(rounded_quotient));
    }
  }
  for (const BinaryOpTestData<int128>& item : kSignedMultiplyTestData) {
    const int128 x = item.input1;
    const int128 y = item.input2;
    const int128 product = item.expected_result;
    if (!item.expected_overflow) {
      if (x != 0) {
        TestDivModWithVariousRhsTypes<T>(product, x, T(y), int128{0}, T(y));
        if (x != kint128min && y != kint128min && product != kint128min) {
          TestDivModWithVariousRhsTypes<T>(-product, x, T(-y), int128{0},
                                           T(-y));
          TestDivModWithVariousRhsTypes<T>(product, -x, T(-y), int128{0},
                                           T(-y));
          TestDivModWithVariousRhsTypes<T>(-product, -x, T(y), int128{0}, T(y));
        }
      }
      if (y != 0) {
        TestDivModWithVariousRhsTypes<T>(product, y, T(x), int128{0}, T(x));
        if (x != kint128min && y != kint128min && product != kint128min) {
          TestDivModWithVariousRhsTypes<T>(-product, y, T(-x), int128{0},
                                           T(-x));
          TestDivModWithVariousRhsTypes<T>(product, -y, T(-x), int128{0},
                                           T(-x));
          TestDivModWithVariousRhsTypes<T>(-product, -y, T(x), int128{0}, T(x));
        }
      }
    }
  }
}

template <typename T, typename V>
void TestNegate(V x, T expected_result, bool expected_overflow) {
  T value(x);
  auto number = value.number();
  T negated_value = -value;
  EXPECT_EQ(negated_value, expected_result)
      << std::hex << std::showbase << "-(" << x << ")";

  EXPECT_EQ(value.NegateOverflow(), expected_overflow)
      << std::hex << std::showbase << "-(" << x << ")";
  EXPECT_EQ(value, expected_result)
      << std::hex << std::showbase << "-(" << x << ")";

  VarIntRef<sizeof(typename T::Word) * 8> ref(number);
  ref.Negate();
  EXPECT_EQ(T(number), expected_result)
      << std::hex << std::showbase << "-(" << x << ")";
}

TYPED_TEST(FixedIntGoldenDataTest, Negate) {
  static constexpr int128 kTestData[] = {0,         1,          1000,
                                         kint64max, kuint64max, kint128max};
  for (int128 input : kTestData) {
    TestNegate(input, TypeParam(-input), false);
    TestNegate(-input, TypeParam(input), false);
  }
  TestNegate(kint128min, TypeParam(kint128min), true);
}

void CompareDouble(double actual, double expect) {
  EXPECT_EQ(actual, expect);
  googlesql_base::MathUtil::DoubleParts decomposed_actual = googlesql_base::MathUtil::Decompose(actual);
  googlesql_base::MathUtil::DoubleParts decomposed_expect = googlesql_base::MathUtil::Decompose(expect);
  EXPECT_EQ(decomposed_actual.mantissa, decomposed_expect.mantissa);
  EXPECT_EQ(decomposed_actual.exponent, decomposed_expect.exponent);
}

TYPED_TEST(FixedUintGoldenDataTest, CastDouble) {
  using T = TypeParam;
  for (uint64_t val : kCastDoubleTestData64) {
    CompareDouble(static_cast<double>(T(val)), static_cast<double>(val));
  }
  for (uint128 val : kCastDoubleTestData128) {
    CompareDouble(static_cast<double>(T(val)), static_cast<double>(val));
  }
}

TYPED_TEST(FixedIntGoldenDataTest, CastDouble) {
  using T = TypeParam;
  for (int64_t val : kCastDoubleTestData64) {
    CompareDouble(static_cast<double>(T(val)), static_cast<double>(val));
    CompareDouble(static_cast<double>(T(-val)), static_cast<double>(-val));
  }
  for (int128 val : kCastDoubleTestData128) {
    CompareDouble(static_cast<double>(T(val)), static_cast<double>(val));
    CompareDouble(static_cast<double>(T(-val)), static_cast<double>(-val));
  }
}

TEST(FixedUintTest, CastDoubleMax) {
  // Given the kNumBitsPerWord can only be 32 or 64, to be casted to double, the
  // actual max bits a FixedUint can have is 32 * 31 = 992
  double double_max = static_cast<double>(FixedUint<32, 31>::max());
  // Rounded up to 2^992
  double expect = googlesql_base::MathUtil::IPow(2.0, 992);
  CompareDouble(double_max, expect);
}

TEST(FixedIntTest, CastDoubleMax) {
  // Given the kNumBitsPerWord can only be 32 or 64, to be casted to double, the
  // actual max bits a FixedUint can have is 32 * 31 = 992
  double double_max = static_cast<double>(FixedInt<32, 31>::max());
  double double_min = static_cast<double>(FixedInt<32, 31>::min());
  // Rounded up to 2^991
  double expect_max = googlesql_base::MathUtil::IPow(2.0, 991);
  double expect_min = googlesql_base::MathUtil::IPow(-2.0, 991);
  CompareDouble(double_max, expect_max);
  CompareDouble(double_min, expect_min);
}

TYPED_TEST(FixedUintGoldenDataTest, SerializeToBytes) {
  constexpr absl::string_view kExistingValue("\xff\0", 2);
  for (auto pair : kSerializedUnsignedValues) {
    std::string output(kExistingValue);
    TypeParam fixed_uint(pair.first);
    fixed_uint.SerializeToBytes(&output);
    EXPECT_EQ(output, absl::StrCat(kExistingValue, pair.second));
    std::string output2(kExistingValue);
    multiprecision_int_impl::SerializeNoOptimization<false>(
        absl::MakeSpan(fixed_uint.number().data(), fixed_uint.number().size()),
        &output2);
    EXPECT_EQ(output, output2);
  }
}

TYPED_TEST(FixedUintGoldenDataTest, DeserializeFromBytes) {
  for (auto pair : kSerializedUnsignedValues) {
    TypeParam deserialized;
    EXPECT_TRUE(deserialized.DeserializeFromBytes(pair.second));
    EXPECT_EQ(deserialized, TypeParam(pair.first));
  }
  for (absl::string_view str : kInvalidSerialized128BitValues) {
    EXPECT_FALSE(TypeParam().DeserializeFromBytes(str));
  }
}

TYPED_TEST(FixedIntGoldenDataTest, SerializeToBytes) {
  constexpr absl::string_view kExistingValue("\xff\0", 2);
  for (auto pair : kSerializedSignedValues) {
    std::string output(kExistingValue);
    TypeParam fixed_int(pair.first);
    fixed_int.SerializeToBytes(&output);
    EXPECT_EQ(output, absl::StrCat(kExistingValue, pair.second));
    std::string output2(kExistingValue);
    multiprecision_int_impl::SerializeNoOptimization<true>(
        absl::MakeSpan(fixed_int.number().data(), fixed_int.number().size()),
        &output2);
    EXPECT_EQ(output, output2);
  }
}

TYPED_TEST(FixedIntGoldenDataTest, DeserializeFromBytes) {
  for (auto pair : kSerializedSignedValues) {
    TypeParam deserialized;
    EXPECT_TRUE(deserialized.DeserializeFromBytes(pair.second));
    EXPECT_EQ(deserialized, TypeParam(pair.first));
  }
  for (absl::string_view str : kInvalidSerialized128BitValues) {
    EXPECT_FALSE(TypeParam().DeserializeFromBytes(str));
  }
}

TYPED_TEST(FixedUintGoldenDataTest, ToString) {
  for (auto pair : kUnsignedValueStrings) {
    TypeParam value(pair.first);
    EXPECT_EQ(pair.second, value.ToString());
    if constexpr (sizeof(typename TypeParam::Word) == sizeof(uint64_t)) {
      auto number = value.number();
      EXPECT_EQ(pair.second, VarUintRef<64>(number).ToString());
    }
  }
  for (size_t i = 1; i < kPowersOf10.size(); ++i) {
    uint128 v = kPowersOf10[i];
    EXPECT_EQ(absl::StrCat("1", std::string(i, '0')), TypeParam(v).ToString());
    EXPECT_EQ(std::string(i, '9'), TypeParam(v - 1).ToString());
  }
}

TYPED_TEST(FixedIntGoldenDataTest, ToString) {
  for (auto pair : kSignedValueStrings) {
    TypeParam value(pair.first);
    EXPECT_EQ(pair.second, value.ToString());
    if constexpr (sizeof(typename TypeParam::Word) == sizeof(uint64_t)) {
      auto number = value.number();
      EXPECT_EQ(pair.second, VarIntRef<64>(number).ToString());
    }
  }
  for (size_t i = 1; i < kPowersOf10.size(); ++i) {
    int128 v = kPowersOf10[i];
    EXPECT_EQ(absl::StrCat("1", std::string(i, '0')), TypeParam(v).ToString());
    EXPECT_EQ(absl::StrCat("-1", std::string(i, '0')),
              TypeParam(-v).ToString());
    EXPECT_EQ(std::string(i, '9'), TypeParam(v - 1).ToString());
    EXPECT_EQ(absl::StrCat("-", std::string(i, '9')),
              TypeParam(1 - v).ToString());
  }
}

TYPED_TEST(FixedUintGoldenDataTest, ParseFromStringStrict) {
  for (auto pair : kUnsignedValueStrings) {
    TypeParam value;
    EXPECT_TRUE(value.ParseFromStringStrict(pair.second)) << pair.second;
    EXPECT_EQ(TypeParam(pair.first), value);
  }
  for (auto pair : kNonCanonicalUnsignedValueStrings) {
    TypeParam value;
    EXPECT_TRUE(value.ParseFromStringStrict(pair.second)) << pair.second;
    EXPECT_EQ(TypeParam(pair.first), value);
  }
  for (absl::string_view str : kUnsigned128BitValueInvalidStrings) {
    EXPECT_FALSE(TypeParam().ParseFromStringStrict(str)) << str;
  }
}

TYPED_TEST(FixedIntGoldenDataTest, ParseFromStringStrict) {
  for (auto pair : kSignedValueStrings) {
    TypeParam value;
    EXPECT_TRUE(value.ParseFromStringStrict(pair.second)) << pair.second;
    EXPECT_EQ(TypeParam(pair.first), value);
  }
  for (auto pair : kNonCanonicalSignedValueStrings) {
    TypeParam value;
    EXPECT_TRUE(value.ParseFromStringStrict(pair.second)) << pair.second;
    EXPECT_EQ(TypeParam(static_cast<int128>(pair.first)), value);
  }
  for (absl::string_view str : kSigned128BitValueInvalidStrings) {
    EXPECT_FALSE(TypeParam().ParseFromStringStrict(str)) << str;
  }
}

template <typename TypeParam>
bool SplitAndParseStringSegments(absl::string_view str, TypeParam* value) {
  std::vector<absl::string_view> parts = absl::StrSplit(str, ',');
  absl::Span<const absl::string_view> extra_segments = parts;
  extra_segments.remove_prefix(1);
  return value->ParseFromStringSegments(parts[0], extra_segments);
}

template <typename TypeParam>
void TestParseFromTrivialStringSegments(absl::string_view valid_str,
                                        TypeParam expected_output) {
  TypeParam value;
  EXPECT_TRUE(value.ParseFromStringSegments(valid_str, {})) << valid_str;
  EXPECT_EQ(expected_output, value);

  EXPECT_TRUE(value.ParseFromStringSegments(valid_str, {""})) << valid_str;
  EXPECT_EQ(expected_output, value);

  EXPECT_TRUE(value.ParseFromStringSegments(valid_str, {"", ""})) << valid_str;
  EXPECT_EQ(expected_output, value);
}

TYPED_TEST(FixedUintGoldenDataTest, ParseFromStringSegments) {
  for (auto pair : kUnsignedValueStrings) {
    TestParseFromTrivialStringSegments(pair.second, TypeParam(pair.first));
  }
  for (auto pair : kNonCanonicalUnsignedValueStrings) {
    TestParseFromTrivialStringSegments(pair.second, TypeParam(pair.first));
  }
  for (auto pair : kUnsignedValueStringSegments) {
    TypeParam value;
    EXPECT_TRUE(SplitAndParseStringSegments(pair.second, &value));
    EXPECT_EQ(TypeParam(pair.first), value);
  }
  for (absl::string_view str : kUnsigned128BitValueInvalidStrings) {
    TypeParam value;
    EXPECT_FALSE(SplitAndParseStringSegments(str, &value));
  }
  for (absl::string_view str : kUnsigned128BitValueInvalidStringSegments) {
    TypeParam value;
    EXPECT_FALSE(SplitAndParseStringSegments(str, &value));
  }
}

TYPED_TEST(FixedIntGoldenDataTest, ParseFromStringSegments) {
  for (auto pair : kSignedValueStrings) {
    TestParseFromTrivialStringSegments(pair.second, TypeParam(pair.first));
  }
  for (auto pair : kNonCanonicalSignedValueStrings) {
    TestParseFromTrivialStringSegments(pair.second, TypeParam(pair.first));
  }
  for (auto pair : kSignedValueStringSegments) {
    TypeParam value;
    EXPECT_TRUE(SplitAndParseStringSegments(pair.second, &value));
    EXPECT_EQ(TypeParam(pair.first), value);
  }
  for (absl::string_view str : kSigned128BitValueInvalidStrings) {
    TypeParam value;
    EXPECT_FALSE(SplitAndParseStringSegments(str, &value));
  }
  for (absl::string_view str : kSigned128BitValueInvalidStringSegments) {
    TypeParam value;
    EXPECT_FALSE(SplitAndParseStringSegments(str, &value));
  }
}

TYPED_TEST(FixedUintGoldenDataTest, CountDecimalDigits) {
  for (auto pair : kUnsignedValueStrings) {
    TypeParam value(pair.first);
    EXPECT_EQ(pair.second.size(), value.CountDecimalDigits()) << pair.second;
  }
  for (size_t i = 1; i < kPowersOf10.size(); ++i) {
    uint128 v = kPowersOf10[i];
    EXPECT_EQ(i, TypeParam(v / 2).CountDecimalDigits()) << i;
    EXPECT_EQ(i, TypeParam(v - 1).CountDecimalDigits()) << i;
    EXPECT_EQ(i + 1, TypeParam(v).CountDecimalDigits()) << i;
    EXPECT_EQ(i + 1, TypeParam(v + 1).CountDecimalDigits()) << i;
    EXPECT_EQ(i + 1, TypeParam(v * 2).CountDecimalDigits()) << i;
  }
}

TYPED_TEST(FixedIntGoldenDataTest, CountDecimalDigits) {
  for (auto pair : kSignedValueStrings) {
    TypeParam value(pair.first);
    EXPECT_EQ(pair.second.size() - (pair.second[0] == '-'),
              value.CountDecimalDigits());
  }
  for (size_t i = 1; i < kPowersOf10.size(); ++i) {
    int128 v = kPowersOf10[i];
    EXPECT_EQ(i, TypeParam(v / 2).CountDecimalDigits()) << i;
    EXPECT_EQ(i, TypeParam(-v / 2).CountDecimalDigits()) << i;
    EXPECT_EQ(i, TypeParam(v - 1).CountDecimalDigits()) << i;
    EXPECT_EQ(i, TypeParam(1 - v).CountDecimalDigits()) << i;
    EXPECT_EQ(i + 1, TypeParam(v).CountDecimalDigits()) << i;
    EXPECT_EQ(i + 1, TypeParam(-v).CountDecimalDigits()) << i;
    EXPECT_EQ(i + 1, TypeParam(v + 1).CountDecimalDigits()) << i;
    EXPECT_EQ(i + 1, TypeParam(-v - 1).CountDecimalDigits()) << i;
  }
}

TYPED_TEST(FixedUintGoldenDataTest, PowersOf10) {
  for (uint i = 0; i < kPowersOf10.size(); ++i) {
    uint128 v = kPowersOf10[i];
    EXPECT_EQ(TypeParam(v), TypeParam::PowerOf10(i)) << i;
  }
}

TYPED_TEST(FixedIntGoldenDataTest, PowersOf10) {
  for (uint i = 0; i < kPowersOf10.size(); ++i) {
    int128 v = kPowersOf10[i];
    EXPECT_EQ(TypeParam(v), TypeParam::PowerOf10(i)) << i;
  }
}

TYPED_TEST(FixedUintGoldenDataTest, FindMSBSetNonZero) {
  for (auto pair : kFindMSBSetNonZeroTestData) {
    EXPECT_EQ(TypeParam(pair.first).FindMSBSetNonZero(), pair.second);
  }
}

TYPED_TEST(FixedIntGoldenDataTest, SetSignAndAbs) {
  for (auto pair : kSetSignAndAbsTestData) {
    TypeParam v;
    constexpr int kNumBitsPerWord = sizeof(typename TypeParam::Word) * 8;
    constexpr int kNumWords = TypeParam::kNumBits / kNumBitsPerWord;
    FixedUint<kNumBitsPerWord, kNumWords> input_abs(pair.first.second);
    bool expect_success = pair.second.first;
    EXPECT_EQ(v.SetSignAndAbs(pair.first.first, input_abs), expect_success);
    EXPECT_EQ(v, TypeParam(pair.second.second));
    if (expect_success) {
      EXPECT_EQ(v.abs(), input_abs);
    }
  }
}

std::vector<uint128> GenerateTestInputs() {
  std::vector<uint128> result;
  for (uint64_t x_lo : kSorted64BitValues) {
    for (uint64_t x_hi : kSorted64BitValues) {
      uint128 x = uint128{x_hi} << 64 | x_lo;
      result.emplace_back(x);
    }
  }
  absl::BitGen random;
  for (int i = 0; i < 1000; ++i) {
    result.emplace_back(absl::Uniform<uint16_t>(random));
    result.emplace_back(absl::Uniform<uint32_t>(random));
    result.emplace_back(absl::Uniform<uint64_t>(random));
    uint64_t x_hi = absl::Uniform<uint64_t>(random);
    uint64_t x_lo = absl::Uniform<uint64_t>(random);
    result.emplace_back(uint128{x_hi} << 64 | x_lo);
    uint64_t bits = absl::Uniform<uint16_t>(random);
    uint8_t shift = absl::Uniform<uint8_t>(random, 0, 112);
    result.emplace_back(uint128{bits} << shift);
    bits = absl::Uniform<uint32_t>(random);
    shift = absl::Uniform<uint8_t>(random, 0, 96);
    result.emplace_back(uint128{bits} << shift);
    bits = absl::Uniform<uint64_t>(random);
    shift = absl::Uniform<uint8_t>(random, 0, 64);
    result.emplace_back(uint128{bits} << shift);
  }
  return result;
}

const std::vector<uint128>& GetTestInputs() {
  static auto* result = new std::vector<uint128>(GenerateTestInputs());
  return *result;
}

std::vector<std::pair<uint128, uint128>> GenerateTestInputPairs() {
  std::vector<std::pair<uint128, uint128>> result;
  for (uint64_t x_lo : kSorted64BitValues) {
    for (uint64_t x_hi : kSorted64BitValues) {
      uint128 x = uint128{x_hi} << 64 | x_lo;
      for (uint64_t y_lo : kSorted64BitValues) {
        for (uint64_t y_hi : kSorted64BitValues) {
          uint128 y = uint128{y_hi} << 64 | y_lo;
          result.emplace_back(x, y);
        }
      }
    }
  }
  absl::BitGen random;
  for (int i = 0; i < 10000; ++i) {
    uint64_t x_hi = absl::Uniform<uint64_t>(random);
    uint64_t x_lo = absl::Uniform<uint64_t>(random);
    uint128 x = uint128{x_hi} << 64 | x_lo;
    uint64_t y_hi = absl::Uniform<uint64_t>(random);
    uint64_t y_lo = absl::Uniform<uint64_t>(random);
    uint128 y = uint128{y_hi} << 64 | y_lo;
    result.emplace_back(x, y);
  }
  return result;
}

const std::vector<std::pair<uint128, uint128>>& GetTestInputPairs() {
  static auto* result =
      new std::vector<std::pair<uint128, uint128>>(GenerateTestInputPairs());
  return *result;
}

template <typename TypeParam>
class FixedIntGeneratedDataTest : public ::testing::Test {};

using FixedIntExtendedTypes =
    ::testing::Types<FixedUint<64, 2>, FixedUint<64, 3>, FixedUint<32, 4>,
                     FixedUint<32, 5>, FixedInt<64, 2>, FixedInt<64, 3>,
                     FixedInt<32, 4>, FixedInt<32, 5>>;
TYPED_TEST_SUITE(FixedIntGeneratedDataTest, FixedIntExtendedTypes);

template <typename T>
using V128 =
    std::conditional_t<std::is_signed_v<typename T::Word>, int128, uint128>;

TYPED_TEST(FixedIntGeneratedDataTest, Compare) {
  for (std::pair<uint128, uint128> input : GetTestInputPairs()) {
    const V128<TypeParam> x = input.first;
    const V128<TypeParam> y = input.second;
    TypeParam tx(x);
    TypeParam ty(y);
    EXPECT_EQ(tx == ty, x == y) << x << " == " << y;
    EXPECT_EQ(tx != ty, x != y) << x << " != " << y;
    EXPECT_EQ(tx < ty, x < y) << x << " < " << y;
    EXPECT_EQ(tx > ty, x > y) << x << " > " << y;
    EXPECT_EQ(tx <= ty, x <= y) << x << " <= " << y;
    EXPECT_EQ(tx >= ty, x >= y) << x << " >= " << y;
#ifdef __cpp_impl_three_way_comparison
    EXPECT_EQ(tx <=> ty, x <=> y) << x << " <=> " << y;
#endif
  }
}

template <int kNumBitsPerWord, int kNumWords>
void TestComparisonToZero(
    const FixedUint<kNumBitsPerWord, kNumWords>& fixed_uint, uint128 value) {
  EXPECT_EQ(fixed_uint.is_zero(), value == 0);
}

template <int kNumBitsPerWord, int kNumWords>
void TestComparisonToZero(const FixedInt<kNumBitsPerWord, kNumWords>& fixed_int,
                          int128 value) {
  EXPECT_EQ(fixed_int.is_zero(), value == 0);
  EXPECT_EQ(fixed_int.is_negative(), value < 0);
  FixedUint<kNumBitsPerWord, kNumWords> abs(SafeAbs(value));
  EXPECT_EQ(fixed_int.abs(), abs);
  FixedInt<kNumBitsPerWord, kNumWords> fixed_int2;
  EXPECT_TRUE(fixed_int2.SetSignAndAbs(value < 0, abs));
  EXPECT_EQ(fixed_int2, fixed_int);
}

TYPED_TEST(FixedIntGeneratedDataTest, CompareToZero) {
  for (V128<TypeParam> x : GetTestInputs()) {
    TestComparisonToZero(TypeParam(x), x);
  }
}

TYPED_TEST(FixedIntGeneratedDataTest, Shift) {
  using T = TypeParam;
  for (V128<T> x : GetTestInputs()) {
    V128<T> filler = x < 0 ? -1 : 0;

    for (uint i = 0; i < 128; ++i) {
      T v = (T(x) >>= i);
      EXPECT_EQ(v, T(x >> i)) << std::hex << std::showbase << x << " >> " << i;

      v = (T(x) <<= i);
      V128<T> mask = static_cast<V128<T>>(~uint128{0} >> i);
      EXPECT_EQ(static_cast<V128<T>>(v), (x & mask) << i)
          << std::hex << std::showbase << x << " << " << i;
      if (T::kNumBits > 128) {
        using Word = typename T::Word;
        EXPECT_EQ(static_cast<Word>(v.number()[128 / sizeof(Word) / 8]),
                  i == 0 ? filler : static_cast<Word>(x >> (128 - i)))
            << std::hex << std::showbase << x << " << " << i;
      }
    }

    for (uint i : {static_cast<uint>(T::kNumBits), 0xffffffffu}) {
      T v = (T(x) >>= i);
      EXPECT_EQ(v, T(filler)) << std::hex << std::showbase << x << " >> " << i;

      v = (T(x) <<= i);
      EXPECT_EQ(v, T()) << std::hex << std::showbase << x << " << " << i;
    }
  }
}

template <typename T, typename V>
T GetExpectedSum(V x, V y, bool* expected_overflow) {
  bool add128_overflow = y >= 0 ? x > max128<V>() - y : x < min128<V>() - y;
  *expected_overflow = add128_overflow && sizeof(T) <= sizeof(int128);
  if (!add128_overflow) {
    return T(x + y);
  }
  T expected(static_cast<V>(y >= 0 ? 1LL : -1LL));
  expected <<= 128;
  return expected += T(static_cast<V>(static_cast<uint128>(x) + y));
}

template <typename T, typename V>
void TestAddWithSelfInput(V x) {
  T t(x);
  bool expected_overflow = false;
  T expected_sum = GetExpectedSum<T>(x, x, &expected_overflow);
  EXPECT_EQ(t += t, expected_sum)
      << std::hex << std::showbase << x << " + " << x;
  t = T(x);
  EXPECT_EQ(t.AddOverflow(t), expected_overflow)
      << std::hex << std::showbase << x << " AddOverflow " << x;
  EXPECT_EQ(t, expected_sum)
      << std::hex << std::showbase << x << " AddOverflow " << x;
}

TYPED_TEST(FixedIntGeneratedDataTest, Add) {
  using T = TypeParam;
  for (V128<T> input : GetTestInputs()) {
    TestAddWithSelfInput<T>(input);
  }
  for (std::pair<uint128, uint128> input : GetTestInputPairs()) {
    const V128<T> x = input.first;
    const V128<T> y = input.second;
    bool expected_overflow = false;
    T expected_sum = GetExpectedSum<T>(x, y, &expected_overflow);
    TestAddWithVariousRhsTypes(x, y, expected_sum, expected_overflow);
  }
}

template <typename T, typename V>
T GetExpectedDiff(V x, V y, bool* expected_overflow) {
  bool sub128_overflow = y >= 0 ? x < min128<V>() + y : x > max128<V>() + y;
  *expected_overflow =
      sub128_overflow &&
      (sizeof(T) <= sizeof(int128) || std::is_unsigned_v<typename T::Word>);
  if (!sub128_overflow) {
    return T(x - y);
  }
  T expected(static_cast<V>(y >= 0 ? -1LL : 1LL));
  expected <<= 128;
  return expected +=
         T(static_cast<V>(static_cast<uint128>(x) - static_cast<uint128>(y)));
}

template <typename T, typename V>
void TestSubtractWithSelfInput(V x) {
  T t(x);
  EXPECT_EQ(t -= t, T()) << std::hex << std::showbase << x << " - " << x;
  t = T(x);
  EXPECT_FALSE(t.SubtractOverflow(t))
      << std::hex << std::showbase << x << " SubtractOverflow " << x;
  EXPECT_EQ(t, T()) << std::hex << std::showbase << x << " SubtractOverflow "
                    << x;
}

TYPED_TEST(FixedIntGeneratedDataTest, Subtract) {
  using T = TypeParam;
  for (V128<T> input : GetTestInputs()) {
    TestSubtractWithSelfInput<T>(input);
  }
  for (std::pair<uint128, uint128> input : GetTestInputPairs()) {
    const V128<T> x = input.first;
    const V128<T> y = input.second;
    bool expected_overflow = false;
    T expected_diff = GetExpectedDiff<T>(x, y, &expected_overflow);
    TestSubtractWithVariousRhsTypes(x, y, expected_diff, expected_overflow);
  }
}

template <typename T>
void TestNegate(uint128 value) {
  static_assert(std::is_unsigned_v<typename T::Word>);
}

template <typename T>
void TestNegate(int128 value) {
  static_assert(std::is_signed_v<typename T::Word>);
  bool expected_overflow = false;
  T expected_result = GetExpectedDiff<T>(int128{0}, value, &expected_overflow);
  TestNegate(value, expected_result, expected_overflow);
}

TYPED_TEST(FixedIntGeneratedDataTest, Negate) {
  for (V128<TypeParam> x : GetTestInputs()) {
    TestNegate<TypeParam>(x);
  }
}

template <typename T, typename V>
T GetExpectedProduct(V x, V y, bool* expected_overflow) {
  uint128 abs_x = SafeAbs(x);
  uint128 abs_y = SafeAbs(y);
  uint64_t abs_x_lo = static_cast<uint64_t>(abs_x);
  uint64_t abs_x_hi = static_cast<uint64_t>(abs_x >> 64);
  uint64_t abs_y_lo = static_cast<uint64_t>(abs_y);
  uint64_t abs_y_hi = static_cast<uint64_t>(abs_y >> 64);
  uint128 product_hi = static_cast<uint128>(abs_x_hi) * abs_y_hi;
  uint128 product_lo = static_cast<uint128>(abs_x_lo) * abs_y_lo;
  uint128 product_mid1 = static_cast<uint128>(abs_x_hi) * abs_y_lo;
  uint128 product_mid2 = static_cast<uint128>(abs_x_lo) * abs_y_hi;
  product_hi += (product_mid1 >> 64) + (product_mid2 >> 64);
  product_mid1 <<= 64;
  product_mid2 <<= 64;
  product_lo += product_mid1;
  product_hi += (product_lo < product_mid1);
  product_lo += product_mid2;
  product_hi += (product_lo < product_mid2);
  if ((x < 0) != (y < 0)) {
    product_lo = -product_lo;
    product_hi = (~product_hi) + (product_lo == 0);
  }
  constexpr int kNumBitsPerWord = sizeof(typename T::Word) * 8;
  constexpr int kNumWords = sizeof(T) / sizeof(typename T::Word);
  uint128 product[2] = {product_lo, product_hi};
  using UnsignedWord = typename T::UnsignedWord;
  std::array<UnsignedWord, kNumWords> number;
  for (int i = 0; i < T::kNumBits; i += kNumBitsPerWord) {
    number[i / kNumBitsPerWord] =
        static_cast<UnsignedWord>(product[i / 128] >> (i % 128));
  }
  T result(number);
  UnsignedWord extended_hi_bits = result < T() ? ~UnsignedWord{0} : 0;
  *expected_overflow = false;
  for (int i = T::kNumBits; i < 256; i += kNumBitsPerWord) {
    if (static_cast<UnsignedWord>(product[i / 128] >> (i % 128)) !=
        extended_hi_bits) {
      *expected_overflow = true;
      break;
    }
  }
  return result;
}

template <typename T, typename V>
void TestMultiplyWithSelfInput(V x) {
  T t(x);
  bool expected_overflow;
  T expected_product = GetExpectedProduct<T>(x, x, &expected_overflow);
  EXPECT_EQ(t *= t, expected_product)
      << std::hex << std::showbase << x << " * " << x;
  t = T(x);
  EXPECT_EQ(t.MultiplyOverflow(t), expected_overflow)
      << std::hex << std::showbase << x << " MultiplyOverflow " << x;
  EXPECT_EQ(t, expected_product)
      << std::hex << std::showbase << x << " MultiplyOverflow " << x;
}

TYPED_TEST(FixedIntGeneratedDataTest, Multiply) {
  using T = TypeParam;
  for (V128<T> input : GetTestInputs()) {
    TestMultiplyWithSelfInput<T>(input);
  }
  for (std::pair<uint128, uint128> input : GetTestInputPairs()) {
    const V128<T> x = input.first;
    const V128<T> y = input.second;
    bool expected_overflow;
    T expected_product = GetExpectedProduct<T>(x, y, &expected_overflow);
    TestMultiplyWithVariousRhsTypes(x, y, expected_product, expected_overflow);
  }
}

template <typename T, typename V>
T GetExpectedQuotient(V x, V y) {
  if (std::is_same_v<V, int128> && x == kint128min && y == -1) {
    // This the only case that causes 128-bit division overflow.
    return T() -= T(x);
  }
  return T(x / y);
}

template <typename T, typename V>
V GetExpectedRemainder(V x, V y) {
  if (std::is_same_v<V, int128> && x == kint128min && y == -1) {
    // This the only case that causes 128-bit division overflow.
    return 0;
  }
  return x % y;
}

template <typename T, typename V>
T GetExpectedRoundedQuotient(V x, V y) {
  if (std::is_same_v<V, int128> && x == kint128min && y == -1) {
    // This the only case that causes 128-bit division overflow.
    return T() -= T(x);
  }
  V quotient = x / y;
  V remainder = x % y;
  auto abs_remainder = SafeAbs(remainder);
  auto abs_y = SafeAbs(y);
  if (abs_remainder > (abs_y - 1) / 2) {
    quotient += (x < 0) == (y < 0) ? 1 : -1;
  }
  return T(quotient);
}

template <typename T, typename V>
void TestDivModWithSelfInput(V x) {
  T t(x);
  EXPECT_EQ(t /= t, T(V{1})) << std::hex << std::showbase << x << " / " << x;

  t = T(x);
  EXPECT_EQ(t %= t, T()) << std::hex << std::showbase << x << " % " << x;

  t = T(x);
  EXPECT_EQ(t.DivAndRoundAwayFromZero(t), T(V{1}))
      << std::hex << std::showbase << x << " DivAndRoundAwayFromZero " << x;

  t = T(x);
  T remainder;
  t.DivMod(t, &t, &remainder);
  EXPECT_EQ(t, T(V{1})) << std::hex << std::showbase << x << " DivMod " << x;
  EXPECT_EQ(remainder, T())
      << std::hex << std::showbase << x << " DivMod " << x;

  t = T(x);
  T quotient;
  t.DivMod(t, &quotient, &t);
  EXPECT_EQ(quotient, T(V{1}))
      << std::hex << std::showbase << x << " DivMod " << x;
  EXPECT_EQ(t, T()) << std::hex << std::showbase << x << " DivMod " << x;

  t = T(x);
  t.DivMod(t, &t, &t);
  EXPECT_EQ(t, T()) << std::hex << std::showbase << x << " DivMod " << x;

  typename T::Word divisor_word = static_cast<typename T::Word>(x);
  if (divisor_word != 0) {
    t = T(x);
    typename T::Word remainder_word;
    t.DivMod(divisor_word, &t, &remainder_word);
    T expected_quotient = GetExpectedQuotient<T, V>(x, divisor_word);
    typename T::Word expected_remainder = static_cast<typename T::Word>(
        GetExpectedRemainder<T, V>(x, divisor_word));
    EXPECT_EQ(t, expected_quotient)
        << std::hex << std::showbase << x << " DivMod " << divisor_word;
    EXPECT_EQ(remainder_word, expected_remainder)
        << std::hex << std::showbase << x << " DivMod " << divisor_word;
  }
}

template <typename T, typename W, W divisor, typename V>
void TestArithmeticOperatorsWithConstWordForType(V x) {
  using DivisorWord = std::conditional_t<std::is_signed_v<W>, typename T::Word,
                                         typename T::UnsignedWord>;
  std::integral_constant<W, divisor> const_divisor;
  EXPECT_EQ(T(x) /= const_divisor, T(x) /= DivisorWord{divisor});
  EXPECT_EQ(T(x) %= const_divisor, T(x) %= DivisorWord{divisor});
  EXPECT_EQ(T(x).DivAndRoundAwayFromZero(const_divisor),
            T(x).DivAndRoundAwayFromZero(DivisorWord{divisor}));

  // FixedInt::DivMod(std::integral_constant<UnsignedWord, divisor>)
  // requires highest bit of divisor is not set.
  if constexpr (!(std::is_signed_v<typename T::Word> && !std::is_signed_v<W> &&
                  static_cast<typename T::Word>(divisor) < 0)) {
    T t(x);
    T quotient1, quotient2;
    typename T::Word remainder1;
    typename T::Word remainder2;
    t.DivMod(const_divisor, &quotient1, &remainder1);
    t.DivMod(divisor, &quotient2, &remainder2);
    EXPECT_EQ(quotient1, quotient2);
    EXPECT_EQ(remainder1, remainder2);

    t.DivMod(const_divisor, &quotient1, nullptr);
    EXPECT_EQ(quotient1, quotient2);

    t.DivMod(const_divisor, nullptr, &remainder1);
    EXPECT_EQ(remainder1, remainder2);

    t.DivMod(const_divisor, &t, &remainder1);
    EXPECT_EQ(t, quotient2);
    EXPECT_EQ(remainder1, remainder2);

    t = T(x);
    t.DivMod(const_divisor, &t, nullptr);
    EXPECT_EQ(t, quotient2);

    // VarUintRef::DivMod has only an unsigned signature.
    if constexpr (!std::is_signed_v<typename T::Word> && !std::is_signed_v<W>) {
      auto number = T(x).number();
      VarUintRef<sizeof(typename T::Word) * 8> ref(number);
      EXPECT_EQ(ref.DivMod(const_divisor), remainder2);
      EXPECT_EQ(T(number), quotient2);
    }
  }
}

template <typename T>
void TestArithmeticOperatorsWithConstWord(uint128 x) {
  using W = typename T::Word;
  TestArithmeticOperatorsWithConstWordForType<T, W, 1>(x);
  TestArithmeticOperatorsWithConstWordForType<T, W, 2>(x);
  // Divisor 34 helps cover a rare but important condition in
  // DivModWordNormalizedConstant.
  TestArithmeticOperatorsWithConstWordForType<T, W, 34>(x);
  TestArithmeticOperatorsWithConstWordForType<T, W, 100>(x);
  TestArithmeticOperatorsWithConstWordForType<T, W, kint32max>(x);
  TestArithmeticOperatorsWithConstWordForType<T, W, 0x80000000U>(x);
  TestArithmeticOperatorsWithConstWordForType<T, W, kuint32max>(x);
  if constexpr (std::is_same_v<W, uint64_t>) {
    TestArithmeticOperatorsWithConstWordForType<T, W, k1e9>(x);
    TestArithmeticOperatorsWithConstWordForType<T, W, k1e18>(x);
    TestArithmeticOperatorsWithConstWordForType<T, W, k1e18>(x);
    TestArithmeticOperatorsWithConstWordForType<
        T, W, std::numeric_limits<int64_t>::max()>(x);
    TestArithmeticOperatorsWithConstWordForType<T, W, k1e19>(x);
    TestArithmeticOperatorsWithConstWordForType<T, W,
                                                std::numeric_limits<W>::max()>(
        x);
  }
}

template <typename T>
void TestArithmeticOperatorsWithConstWord(int128 x) {
  using Word = typename T::Word;
  TestArithmeticOperatorsWithConstWordForType<T, Word, 1>(x);
  TestArithmeticOperatorsWithConstWordForType<T, Word, 2>(x);
  TestArithmeticOperatorsWithConstWordForType<T, Word, 100>(x);
  TestArithmeticOperatorsWithConstWordForType<T, Word, kint32max>(x);
  TestArithmeticOperatorsWithConstWordForType<T, Word, -1>(x);
  TestArithmeticOperatorsWithConstWordForType<T, Word, -2>(x);
  TestArithmeticOperatorsWithConstWordForType<T, Word, -100>(x);
  TestArithmeticOperatorsWithConstWordForType<T, Word, -kint32max>(x);
  TestArithmeticOperatorsWithConstWordForType<T, Word, kint32min>(x);

  using UnsignedWord = typename T::UnsignedWord;
  TestArithmeticOperatorsWithConstWordForType<T, UnsignedWord, 1>(x);
  TestArithmeticOperatorsWithConstWordForType<T, UnsignedWord, 2>(x);
  TestArithmeticOperatorsWithConstWordForType<T, UnsignedWord, 100>(x);
  TestArithmeticOperatorsWithConstWordForType<T, UnsignedWord, kint32max>(x);
  TestArithmeticOperatorsWithConstWordForType<T, UnsignedWord, 0x80000000U>(x);
  TestArithmeticOperatorsWithConstWordForType<T, UnsignedWord, kuint32max>(x);
}

#if !defined(ADDRESS_SANITIZER)
TYPED_TEST(FixedIntGeneratedDataTest, DivMod) {
  using T = TypeParam;
  for (V128<T> input : GetTestInputs()) {
    if (input != 0) {
      TestDivModWithSelfInput<T>(input);
      TestArithmeticOperatorsWithConstWord<T>(input);
    }
  }
  for (std::pair<uint128, uint128> input : GetTestInputPairs()) {
    const V128<T> x = input.first;
    const V128<T> y = input.second;
    if (y != 0) {
      TestDivModWithVariousRhsTypes<T>(x, y, GetExpectedQuotient<T>(x, y),
                                       GetExpectedRemainder<T>(x, y),
                                       GetExpectedRoundedQuotient<T>(x, y));
    }
  }
}
#endif

TYPED_TEST(FixedIntGeneratedDataTest, CastInt128) {
  using T = TypeParam;
  for (V128<T> input : GetTestInputs()) {
    EXPECT_EQ(static_cast<V128<T>>(T(input)), input);
  }
}

TYPED_TEST(FixedIntGeneratedDataTest, CastDouble) {
  using T = TypeParam;
  for (V128<T> input : GetTestInputs()) {
    CompareDouble(static_cast<double>(T(input)), static_cast<double>(input));
  }
}

TYPED_TEST(FixedIntGeneratedDataTest, SerializationRoundTrip) {
  for (V128<TypeParam> input : GetTestInputs()) {
    TypeParam t(input);
    TypeParam deserialized;
    std::string serialized;
    t.SerializeToBytes(&serialized);
    EXPECT_TRUE(deserialized.DeserializeFromBytes(serialized));
    EXPECT_EQ(deserialized, t);

    constexpr bool is_signed = std::is_signed_v<typename TypeParam::Word>;
    std::string serialized2;
    multiprecision_int_impl::SerializeNoOptimization<is_signed>(
        absl::MakeSpan(t.number().data(), t.number().size()), &serialized2);
    EXPECT_EQ(serialized2, serialized);
  }
}

TYPED_TEST(FixedIntGeneratedDataTest, HashCode) {
  std::vector<TypeParam> values;
  for (uint64_t x_lo : kSorted64BitValues) {
    for (uint64_t x_hi : kSorted64BitValues) {
      V128<TypeParam> x = uint128{x_hi} << 64 | x_lo;
      values.emplace_back(x);
    }
  }
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(values));
}

TYPED_TEST(FixedIntGeneratedDataTest, ToString) {
  using T = TypeParam;
  for (V128<T> value : GetTestInputs()) {
    std::ostringstream oss;
    oss << value;
    std::string expect = oss.str();
    T v(value);
    std::string actual = v.ToString();
    EXPECT_EQ(expect, actual);
    auto number = v.number();
    actual =
        VarIntBase<std::is_signed_v<typename T::Word>,
                   typename std::make_unsigned<typename T::Word>::type>(number)
            .ToString();
    EXPECT_EQ(expect, actual);
  }
}

TYPED_TEST(FixedIntGeneratedDataTest, StringRoundTrip) {
  using T = TypeParam;
  for (V128<T> value : GetTestInputs()) {
    T expect(value);
    std::string str = expect.ToString();
    T actual;
    EXPECT_TRUE(actual.ParseFromStringStrict(str));
    EXPECT_EQ(expect, actual);

    TestParseFromTrivialStringSegments(str, expect);
  }
}

TYPED_TEST(FixedIntGeneratedDataTest, CountDecimalDigits) {
  using T = TypeParam;
  for (V128<T> value : GetTestInputs()) {
    std::ostringstream oss;
    oss << value;
    std::string expect_str = oss.str();
    size_t expected = expect_str.size() - (expect_str[0] == '-');
    size_t actual = T(value).CountDecimalDigits();
    EXPECT_EQ(expected, actual) << expect_str;
  }
}

template <template <int, int> class Dest, int k1, int n1, int k2, int n2,
          template <int, int> class Src>
Dest<k1, n1> Convert(const Src<k2, n2>& src, bool negative) {
  return Dest<k1, n1>(multiprecision_int_impl::Convert<k1, n1, k2, n2, false>(
      src.number(), negative));
}

template <template <int, int> class Dest, template <int, int> class Src,
          typename V128, typename V64>
void TestConversion(V128 x) {
  std::ostringstream s;
  s << "x = " << std::hex << std::showbase << x;
  SCOPED_TRACE(s.str());
  Src<64, 3> x_64_3(x);
  Src<64, 2> x_64_2(x);
  Src<64, 1> x_64_1(static_cast<V64>(x));
  Src<32, 5> x_32_5(x);
  Src<32, 4> x_32_4(x);
  Src<32, 3> x_32_3(std::array<uint32_t, 3>{static_cast<uint32_t>(x),
                                            static_cast<uint32_t>(x >> 32),
                                            static_cast<uint32_t>(x >> 64)});
  constexpr V128 kMask = (V128{1} << 96) - 1;
  // Interpret the low 96 bits as int96 or uint96 depending on whether V128 is
  // signed, and cast the int96/uint96 to V128 by extending the sign bit if
  // signed.
  V128 x_lo96 = ((x & kMask) << 32) >> 32;

  EXPECT_EQ((Dest<64, 3>(x_64_2).number()), x_64_3.number());
  EXPECT_EQ((Dest<64, 2>(x_64_2).number()), x_64_2.number());
  EXPECT_EQ((Dest<64, 1>(x_64_2).number()), x_64_1.number());
  EXPECT_EQ((Dest<32, 5>(x_64_2).number()), x_32_5.number());
  EXPECT_EQ((Dest<32, 4>(x_64_2).number()), x_32_4.number());
  EXPECT_EQ((Dest<32, 3>(x_64_2).number()), x_32_3.number());

  EXPECT_EQ((Dest<64, 3>(x_32_4).number()), x_64_3.number());
  EXPECT_EQ((Dest<64, 2>(x_32_4).number()), x_64_2.number());
  EXPECT_EQ((Dest<64, 1>(x_64_2).number()), x_64_1.number());
  EXPECT_EQ((Dest<32, 5>(x_32_4).number()), x_32_5.number());
  EXPECT_EQ((Dest<32, 4>(x_32_4).number()), x_32_4.number());
  EXPECT_EQ((Dest<32, 3>(x_64_2).number()), x_32_3.number());

  EXPECT_EQ((Dest<64, 3>(x_32_3).number()), (Src<64, 3>(x_lo96).number()));
  EXPECT_EQ((Dest<64, 2>(x_32_3).number()), (Src<64, 2>(x_lo96).number()));
  EXPECT_EQ((Dest<64, 1>(x_32_3).number()), x_64_1.number());
  EXPECT_EQ((Dest<32, 5>(x_32_3).number()), (Src<32, 5>(x_lo96).number()));
  EXPECT_EQ((Dest<32, 4>(x_32_3).number()), (Src<32, 4>(x_lo96).number()));
  EXPECT_EQ((Dest<32, 3>(x_32_3).number()), x_32_3.number());

  // Test the path without optimization.
  bool negative = x < 0;
  EXPECT_EQ((Convert<Dest, 64, 3>(x_64_2, negative).number()), x_64_3.number());
  EXPECT_EQ((Convert<Dest, 64, 2>(x_64_2, negative).number()), x_64_2.number());
  EXPECT_EQ((Convert<Dest, 64, 1>(x_64_2, negative).number()), x_64_1.number());
  EXPECT_EQ((Convert<Dest, 32, 5>(x_64_2, negative).number()), x_32_5.number());
  EXPECT_EQ((Convert<Dest, 32, 4>(x_64_2, negative).number()), x_32_4.number());
  EXPECT_EQ((Convert<Dest, 32, 3>(x_64_2, negative).number()), x_32_3.number());

  EXPECT_EQ((Convert<Dest, 64, 3>(x_32_4, negative).number()), x_64_3.number());
  EXPECT_EQ((Convert<Dest, 64, 2>(x_32_4, negative).number()), x_64_2.number());
  EXPECT_EQ((Convert<Dest, 64, 1>(x_32_4, negative).number()), x_64_1.number());
  EXPECT_EQ((Convert<Dest, 32, 5>(x_32_4, negative).number()), x_32_5.number());
  EXPECT_EQ((Convert<Dest, 32, 4>(x_32_4, negative).number()), x_32_4.number());
  EXPECT_EQ((Convert<Dest, 32, 3>(x_32_4, negative).number()), x_32_3.number());

  negative = x_lo96 < 0;
  EXPECT_EQ((Convert<Dest, 64, 3>(x_32_3, negative).number()),
            (Src<64, 3>(x_lo96).number()));
  EXPECT_EQ((Convert<Dest, 64, 2>(x_32_3, negative).number()),
            (Src<64, 2>(x_lo96).number()));
  EXPECT_EQ((Convert<Dest, 64, 1>(x_32_3, negative).number()), x_64_1.number());
  EXPECT_EQ((Convert<Dest, 32, 5>(x_32_3, negative).number()),
            (Src<32, 5>(x_lo96).number()));
  EXPECT_EQ((Convert<Dest, 32, 4>(x_32_3, negative).number()),
            (Src<32, 4>(x_lo96).number()));
  EXPECT_EQ((Convert<Dest, 32, 3>(x_32_3, negative).number()), x_32_3.number());
}

TEST(FixedUintTest, Conversion) {
  for (uint128 input : GetTestInputs()) {
    TestConversion<FixedUint, FixedUint, uint128, uint64_t>(input);
  }
}

TEST(FixedIntTest, Conversion) {
  for (int128 input : GetTestInputs()) {
    TestConversion<FixedInt, FixedInt, int128, int64_t>(input);
    TestConversion<FixedInt, FixedUint, uint128, uint64_t>(input);
  }
}

TEST(FixedUintTest, MinMax) {
  EXPECT_EQ((FixedUint<32, 2>::min()), (FixedUint<32, 2>(uint64_t{0})));
  EXPECT_EQ((FixedUint<32, 2>::max()), (FixedUint<32, 2>(kuint64max)));
  EXPECT_EQ((FixedUint<64, 1>::min()), (FixedUint<64, 1>(uint64_t{0})));
  EXPECT_EQ((FixedUint<64, 1>::max()), (FixedUint<64, 1>(kuint64max)));
  EXPECT_EQ((FixedUint<32, 4>::min()), (FixedUint<32, 4>(uint128{0})));
  EXPECT_EQ((FixedUint<32, 4>::max()), (FixedUint<32, 4>(kuint128max)));
  EXPECT_EQ((FixedUint<64, 2>::min()), (FixedUint<64, 2>(uint128{0})));
  EXPECT_EQ((FixedUint<64, 2>::max()), (FixedUint<64, 2>(kuint128max)));
}

TEST(FixedIntTest, MinMax) {
  EXPECT_EQ((FixedInt<32, 2>::min()), (FixedInt<32, 2>(kint64min)));
  EXPECT_EQ((FixedInt<32, 2>::max()), (FixedInt<32, 2>(kint64max)));
  EXPECT_EQ((FixedInt<64, 1>::min()), (FixedInt<64, 1>(kint64min)));
  EXPECT_EQ((FixedInt<64, 1>::max()), (FixedInt<64, 1>(kint64max)));
  EXPECT_EQ((FixedInt<32, 4>::min()), (FixedInt<32, 4>(kint128min)));
  EXPECT_EQ((FixedInt<32, 4>::max()), (FixedInt<32, 4>(kint128max)));
  EXPECT_EQ((FixedInt<64, 2>::min()), (FixedInt<64, 2>(kint128min)));
  EXPECT_EQ((FixedInt<64, 2>::max()), (FixedInt<64, 2>(kint128max)));
}
}  // namespace
}  // namespace googlesql
