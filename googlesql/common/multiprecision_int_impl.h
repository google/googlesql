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

// Implementation utils for multiprecision_int.h.
// Do NOT include this file directly. Include multiprecision_int.h instead.

#ifndef GOOGLESQL_COMMON_MULTIPRECISION_INT_IMPL_H_
#define GOOGLESQL_COMMON_MULTIPRECISION_INT_IMPL_H_
#include <string.h>
#include <sys/types.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>

#include "googlesql/base/logging.h"
#include "absl/base/optimization.h"
#include "absl/numeric/bits.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/bits.h"
#include "googlesql/base/endian.h"

namespace googlesql {
namespace multiprecision_int_impl {

template <int num_bits>  // num_bits must be 32, 64, or 128
struct IntTraits;

// The version of <type_traits> used in googlesql oss does not support
// std::make_unsigned<__int128> or the std::make_signed counterpart,
// so we have to define both Int and Uint explicitly.
template <>
struct IntTraits<32> {
  using Int = int32_t;
  using Uint = uint32_t;
  using HalfUint = uint16_t;
  static constexpr Uint kMaxPowerOf10 = 1000000000;
  static constexpr size_t kMaxWholeDecimalDigits = 9;
  static constexpr size_t kFloorLog2MaxPowerOf10 = 29;
};

template <>
struct IntTraits<64> {
  using Int = int64_t;
  using Uint = uint64_t;
  using HalfUint = uint32_t;
  static constexpr Uint kMaxPowerOf10 = 10000000000000000000U;
  static constexpr size_t kMaxWholeDecimalDigits = 19;
  static constexpr size_t kFloorLog2MaxPowerOf10 = 63;
};

template <>
struct IntTraits<128> {
  using Int = __int128;
  using Uint = unsigned __int128;
  using HalfUint = uint64_t;
};

template <int num_bits>
using Int = typename IntTraits<num_bits>::Int;

template <int num_bits>
using Uint = typename IntTraits<num_bits>::Uint;

inline int FindMSBSetNonZero(uint32_t x) { return googlesql_base::Bits::FindMSBSetNonZero(x); }
inline int FindMSBSetNonZero(uint64_t x) {
  return googlesql_base::Bits::FindMSBSetNonZero64(x);
}

// Builds a std::array<Word, size> with left padding in compile-time.
// For example, LeftPad<uint32, 4>(1, 2, 3) returns {1, 1, 2, 3}.
// The number of arguments cannot exceed <size> + 1.
template <typename Word, int size, typename... T>
inline constexpr std::array<Word, size> LeftPad(Word filler, T... v) {
  if constexpr (sizeof...(T) < size) {
    return LeftPad<Word, size>(filler, filler, v...);
  } else {
    return {v...};
  }
}

// Builds a std::array<Word, size> with right padding in compile-time.
// For example, RightPad<uint32, 4>(1, 2, 3) returns {2, 3, 1, 1}.
// The number of arguments cannot exceed <size> + 1.
template <typename Word, int size, typename... T>
inline constexpr std::array<Word, size> RightPad(Word filler, T... v) {
  if constexpr (sizeof...(T) < size) {
    return RightPad<Word, size>(filler, v..., filler);
  } else {
    return {v...};
  }
}

// Helper functions for converting between a bigger integer type and an array of
// smaller integer type in little-endian order. These functions do not check
// array sizes.
// Requirements: n must be >= m and (n/m) must be a power of 2.
template <int n, int m, int size>
inline constexpr std::array<Uint<m>, size> UintToArray(Uint<n> src,
                                                       Uint<m> extension) {
  if constexpr (n == m) {
    return RightPad<Uint<m>, size>(extension, src);
  } else if constexpr (n == m * 2) {
    return RightPad<Uint<m>, size>(extension, static_cast<Uint<m>>(src),
                                   static_cast<Uint<m>>(src >> m));
  } else if constexpr (n == m * 4) {
    return RightPad<Uint<m>, size>(
        extension, static_cast<Uint<m>>(src), static_cast<Uint<m>>(src >> m),
        static_cast<Uint<m>>(src >> 2 * m), static_cast<Uint<m>>(src >> 3 * m));
  }
}

template <int n, int m>
inline void UintToArray(Uint<n> src, Uint<m> dest[]) {
  if constexpr (n == m) {
    *dest = src;
  } else {
    constexpr int k = n / 2;
    UintToArray<k, m>(static_cast<Uint<k>>(src), dest);
    UintToArray<k, m>(static_cast<Uint<k>>(src >> k), &dest[k / m]);
  }
}

template <int n, int m>
inline constexpr Uint<n> ArrayToUint(const Uint<m> src[]) {
  if constexpr (n == m) {
    return src[0];
  } else {
    constexpr int k = n / 2;
    return (static_cast<Uint<n>>(ArrayToUint<k, m>(&src[k / m])) << k) |
           ArrayToUint<k, m>(src);
  }
}

template <int k>
inline Uint<k * 2> MakeDword(const Uint<k> x[2]) {
  return static_cast<Uint<k * 2>>(x[1]) << k | x[0];
}

// Calculates the new high word from shifting <high, low> up to 63 bits to the
// left.
constexpr uint64_t ShiftLeftAndGetHighWord(uint64_t high, uint64_t low,
                                           uint8_t shift_amount) {
  if (shift_amount == 0) {
    return high;
  }
  return (high << shift_amount) | (low >> (64 - shift_amount));
}

// bits must be > 0 and < 64.
inline uint64_t ShiftRightAndGetLowWord(const uint64_t x[2], uint bits) {
  return (x[1] << (64 - bits)) | (x[0] >> bits);
}

// bits must be < 64.
inline uint32_t ShiftRightAndGetLowWord(const uint32_t x[2], uint bits) {
  return static_cast<uint32_t>(MakeDword<32>(x) >> bits);
}

template <typename Word>
inline void ShiftLeftFast(Word* number, int num_words, uint bits) {
  constexpr int kNumBitsPerWord = sizeof(Word) * 8;
  ABSL_DCHECK_GT(bits, 0);
  ABSL_DCHECK_LT(bits, kNumBitsPerWord);
  int s = kNumBitsPerWord - bits;
  for (int i = num_words - 1; i > 0; --i) {
    number[i] = ShiftRightAndGetLowWord(number + (i - 1), s);
  }
  number[0] <<= bits;
}

template <typename Word>
void ShiftLeft(Word* number, int num_words, uint bits) {
  constexpr int kNumBitsPerWord = sizeof(Word) * 8;
  if (ABSL_PREDICT_FALSE(bits >= num_words * kNumBitsPerWord)) {
    std::fill(number, number + num_words, 0);
    return;
  }
  int q = bits / kNumBitsPerWord;
  int r = bits % kNumBitsPerWord;
  int s = kNumBitsPerWord - r;
  for (int i = num_words - 1; i > q; --i) {
    auto tmp = MakeDword<kNumBitsPerWord>(number + (i - q - 1));
    number[i] = static_cast<Word>(tmp >> s);
  }
  number[q] = number[0] << r;
  for (int i = 0; i < q; ++i) {
    number[i] = 0;
  }
}

template <typename LastWord, typename Word>
inline void ShiftRightFast(Word* number, int num_words, uint bits) {
  constexpr int kNumBitsPerWord = sizeof(Word) * 8;
  ABSL_DCHECK_GT(bits, 0);
  ABSL_DCHECK_LT(bits, kNumBitsPerWord);
  for (int i = 0; i < num_words - 1; ++i) {
    number[i] = ShiftRightAndGetLowWord(number + i, bits);
  }
  number[num_words - 1] = static_cast<LastWord>(number[num_words - 1]) >> bits;
}

template <typename Filler, typename Word>
void ShiftRight(Filler filler, Word* number, int num_words, uint bits) {
  static_assert(sizeof(Filler) == sizeof(Word));
  constexpr int kNumBitsPerWord = sizeof(Word) * 8;
  if (ABSL_PREDICT_FALSE(bits >= num_words * kNumBitsPerWord)) {
    std::fill(number, number + num_words, filler);
    return;
  }
  int q = bits / kNumBitsPerWord;
  int r = bits % kNumBitsPerWord;
  int mid = num_words - q;
  for (int i = 0; i < mid - 1; ++i) {
    auto tmp = MakeDword<kNumBitsPerWord>(number + (i + q));
    number[i] = static_cast<Word>(tmp >> r);
  }
  number[mid - 1] = static_cast<Filler>(number[num_words - 1]) >> r;
  for (int i = mid; i < num_words; ++i) {
    number[i] = filler;
  }
}

template <int k>
bool LessWithVariableSize(const Uint<k>* lhs, const Uint<k>* rhs, int size) {
  for (int i = size - 1; i >= 0; --i) {
    if (lhs[i] != rhs[i]) {
      return lhs[i] < rhs[i];
    }
  }
  return false;
}

// When the size is a small compile-time constant, Less<k, size> is much more
// efficient.
template <int k, int size>
inline bool Less(const Uint<k>* lhs, const Uint<k>* rhs) {
  if constexpr (size <= 0) {
    return false;
  } else if constexpr (size == 1) {
    return lhs[0] < rhs[0];
  } else if constexpr (size == 2) {
    return MakeDword<k>(lhs) < MakeDword<k>(rhs);
  } else if constexpr (size <= 8) {
    auto lh_dword = MakeDword<k>(lhs + size - 2);
    auto rh_dword = MakeDword<k>(rhs + size - 2);
    if (lh_dword != rh_dword) {
      return lh_dword < rh_dword;
    }
    return Less<k, size - 2>(lhs, rhs);
  } else {
    return LessWithVariableSize<k>(lhs, rhs, size);
  }
}

// This version is not optimized for performance, but it can be
// be used to build constexpr variables. Use it only with constexpr inputs.
template <typename Word, size_t size>
constexpr bool Less(const std::array<Word, size>& lhs,
                    const std::array<Word, size>& rhs,
                    ssize_t current_index = size - 1) {
  return current_index >= 0 && (lhs[current_index] == rhs[current_index]
                                    ? Less(lhs, rhs, current_index - 1)
                                    : lhs[current_index] < rhs[current_index]);
}

template <typename Word, int size>
inline int NonZeroLength(const Word src[]) {
  for (int i = size - 1; i >= 0; --i) {
    if (src[i] != 0) {
      return i + 1;
    }
  }
  return 0;
}

template <int k>
inline void Copy(const Uint<k>* src, int src_size, Uint<k>* dest, int dest_size,
                 Uint<k> filler) {
  int size = std::min(src_size, dest_size);
  Uint<k>* p = std::copy(src, src + size, dest);
  std::fill(p, dest + dest_size, filler);
}

template <int k>
inline void Copy(const Uint<k>* src, int src_size, Uint<k * 2>* dest,
                 int dest_size, Uint<k * 2> filler) {
  int copy_size = std::min(src_size / 2, dest_size);
  for (int i = 0; i < copy_size; ++i) {
    dest[i] = (static_cast<Uint<2 * k>>(src[2 * i + 1]) << k) | src[2 * i];
  }
  if ((src_size & 1) != 0 && copy_size < dest_size) {
    dest[copy_size] = (filler << k) | src[2 * copy_size];
    ++copy_size;
  }
  std::fill(dest + copy_size, dest + dest_size, filler);
}

template <int k>
inline void Copy(const Uint<k * 2>* src, int src_size, Uint<k>* dest,
                 int dest_size, Uint<k> filler) {
  int copy_size = std::min(src_size * 2, (dest_size & ~1));
  for (int i = 0; i < copy_size; i += 2) {
    dest[i] = static_cast<Uint<k>>(src[i / 2]);
    dest[i + 1] = static_cast<Uint<k>>(src[i / 2] >> k);
  }
  if (copy_size < dest_size && copy_size < src_size * 2) {
    dest[copy_size] = static_cast<Uint<k>>(src[copy_size / 2]);
    ++copy_size;
  }
  std::fill(dest + copy_size, dest + dest_size, filler);
}

// allow_optimization is used only for testing.
template <int k1, int n1, int k2, int n2, bool allow_optimization = true>
inline std::array<Uint<k1>, n1> Convert(const std::array<Uint<k2>, n2>& src,
                                        bool negative) {
  std::array<Uint<k1>, n1> res;
  Uint<k1> extension = negative ? ~Uint<k1>{0} : 0;
#ifndef ABSL_IS_BIG_ENDIAN
  if (allow_optimization) {
    res.fill(extension);
    memcpy(res.data(), src.data(), std::min(sizeof(src), sizeof(res)));
    return res;
  }
#endif
  Copy<std::min(k1, k2)>(src.data(), n2, res.data(), n1, extension);
  return res;
}

#ifdef __x86_64__
inline uint8_t AddWithCarry(uint32_t* x, uint32_t y, uint8_t carry) {
  return _addcarry_u32(carry, *x, y, x);
}

inline uint8_t AddWithCarry(uint64_t* x, uint64_t y, uint8_t carry) {
  static_assert(sizeof(uint64_t) == sizeof(unsigned long long));  // NOLINT
  unsigned long long tmp;                                         // NOLINT
  carry = _addcarry_u64(carry, *x, y, &tmp);
  *x = tmp;
  return carry;
}

#else

template <typename Word>
inline uint8_t AddWithCarry(Word* x, Word y, uint8_t carry) {
  constexpr int k = sizeof(Word) * 8;
  Uint<k* 2> sum = *x;
  sum += y;
  sum += carry;
  *x = static_cast<Word>(sum);
  return static_cast<uint8_t>(sum >> k);
}

#endif

template <typename Word>
inline uint8_t AddWithVariableSize(Word lhs[], const Word rhs[], int size) {
  uint8_t carry = 0;
  for (int i = 0; i < size; ++i) {
    carry = AddWithCarry(&lhs[i], rhs[i], carry);
  }
  return carry;
}

template <int size>
inline uint8_t Add(std::array<uint32_t, size>& lhs,
                   const std::array<uint32_t, size>& rhs) {
  uint8_t carry = 0;
  for (int i = 0; i < (size & ~1); i += 2) {
    uint64_t tmp = MakeDword<32>(lhs.data() + i);
    carry = AddWithCarry(&tmp, MakeDword<32>(rhs.data() + i), carry);
    lhs[i + 1] = static_cast<uint32_t>(tmp >> 32);
    lhs[i] = static_cast<uint32_t>(tmp);
  }
  if (size & 1) {
    carry = AddWithCarry(&lhs[size - 1], rhs[size - 1], carry);
  }
  return carry;
}

template <int size>
inline uint8_t Add(std::array<uint64_t, size>& lhs,
                   const std::array<uint64_t, size>& rhs) {
  return AddWithVariableSize(lhs.data(), rhs.data(), size);
}

#ifdef __x86_64__
inline uint8_t SubtractWithBorrow(uint32_t* x, uint32_t y, uint8_t borrow) {
  return _subborrow_u32(borrow, *x, y, x);
}

inline uint8_t SubtractWithBorrow(uint64_t* x, uint64_t y, uint8_t borrow) {
  static_assert(sizeof(uint64_t) == sizeof(unsigned long long));  // NOLINT
  unsigned long long tmp;                                         // NOLINT
  borrow = _subborrow_u64(borrow, *x, y, &tmp);
  *x = tmp;
  return borrow;
}

#else

template <typename Word>
inline uint8_t SubtractWithBorrow(Word* x, Word y, uint8_t borrow) {
  constexpr int k = sizeof(Word) * 8;
  Uint<k * 2> lhs = *x;
  Uint<k * 2> rhs = y;
  rhs += borrow;
  *x = static_cast<Word>(lhs - rhs);
  return lhs < rhs;
}

#endif

template <typename Word>
inline uint8_t SubtractWithVariableSize(Word lhs[], const Word rhs[],
                                        int size) {
  uint8_t borrow = 0;
  for (int i = 0; i < size; ++i) {
    borrow = SubtractWithBorrow(&lhs[i], rhs[i], borrow);
  }
  return borrow;
}

template <int size>
inline uint8_t Subtract(std::array<uint32_t, size>& lhs,
                        const std::array<uint32_t, size>& rhs) {
  uint8_t borrow = 0;
  for (int i = 0; i < (size & ~1); i += 2) {
    uint64_t tmp = MakeDword<32>(lhs.data() + i);
    borrow = SubtractWithBorrow(&tmp, MakeDword<32>(rhs.data() + i), borrow);
    lhs[i + 1] = static_cast<uint32_t>(tmp >> 32);
    lhs[i] = static_cast<uint32_t>(tmp);
  }
  if (size & 1) {
    borrow = SubtractWithBorrow(&lhs[size - 1], rhs[size - 1], borrow);
  }
  return borrow;
}

template <int size>
inline uint8_t Subtract(std::array<uint64_t, size>& lhs,
                        const std::array<uint64_t, size>& rhs) {
  return SubtractWithVariableSize(lhs.data(), rhs.data(), size);
}

template <int k, int n1, int n2>
inline std::array<Uint<k>, n1 + n2> ExtendAndMultiply(
    const std::array<Uint<k>, n1>& lh, const std::array<Uint<k>, n2>& rh) {
  using Word = Uint<k>;
  using DWord = Uint<k * 2>;
  std::array<Word, n1 + n2> res;
  res.fill(0);
  for (int j = 0; j < n2; ++j) {
    Word carry = 0;
    for (int i = 0; i < n1; ++i) {
      DWord tmp = static_cast<DWord>(lh[i]) * rh[j] + res[i + j] + carry;
      res[i + j] = static_cast<Word>(tmp);
      carry = static_cast<Word>(tmp >> k);
    }
    res[n1 + j] = carry;
  }
  return res;
}

template <typename Word>
inline Word MulWord(Word lhs[], int size, Word rhs) {
  constexpr int kNumBitsPerWord = sizeof(Word) * 8;
  Word carry = 0;
  for (int i = 0; i < size; ++i) {
    auto tmp = static_cast<Uint<kNumBitsPerWord * 2>>(lhs[i]) * rhs + carry;
    lhs[i] = static_cast<Word>(tmp);
    carry = static_cast<Word>(tmp >> kNumBitsPerWord);
  }
  return carry;
}

// The caller is responsible for ensuring that the divisor is not zero.
template <typename Word>
inline void DivModWord(Word dividend_hi, Word dividend_lo, Word divisor,
                       Word* quotient, Word* remainder) {
  ABSL_DCHECK_LT(dividend_hi, divisor);
  constexpr int kNumBitsPerWord = sizeof(Word) * 8;
  Uint<kNumBitsPerWord* 2> dividend =
      static_cast<Uint<kNumBitsPerWord * 2>>(dividend_hi) << kNumBitsPerWord |
      dividend_lo;
  *quotient = static_cast<Word>(dividend / divisor);
  *remainder = static_cast<Word>(dividend % divisor);
}

#ifdef __x86_64__
// Requires dividend_hi < divisor to prevent overflow.
// When the divisor is a compile-time constant uint32, DivModWord above is much
// more efficient because the compiler can replace division with multiplication.
// In other cases, RawDivModWord is much more efficient.
inline void RawDivModWord(uint32_t dividend_hi, uint32_t dividend_lo,
                          uint32_t divisor, uint32_t* quotient,
                          uint32_t* remainder) {
  ABSL_DCHECK_LT(dividend_hi, divisor);
  __asm__("divl %[v]"
          : "=a"(*quotient), "=d"(*remainder)
          : [ v ] "r"(divisor), "a"(dividend_lo), "d"(dividend_hi));
}

inline void RawDivModWord(uint64_t dividend_hi, uint64_t dividend_lo,
                          uint64_t divisor, uint64_t* quotient,
                          uint64_t* remainder) {
  ABSL_DCHECK_LT(dividend_hi, divisor);
  __asm__("divq %[v]"
          : "=a"(*quotient), "=d"(*remainder)
          : [ v ] "r"(divisor), "a"(dividend_lo), "d"(dividend_hi));
}

#else

template <typename Word>
inline void RawDivModWord(Word dividend_hi, Word dividend_lo, Word divisor,
                          Word* quotient, Word* remainder) {
  DivModWord(dividend_hi, dividend_lo, divisor, quotient, remainder);
}
#endif

// Performs a short division and returns the remainder.
template <typename Word, int size, bool is_divisor_constant = false>
inline Word ShortDivMod(const std::array<Word, size>& dividend, Word divisor,
                        std::array<Word, size>* quotient) {
  if (quotient != nullptr && quotient != &dividend) {
    *quotient = dividend;
  }
  for (int i = size - 1; i >= 0; --i) {
    if (dividend[i] != 0) {
      Word remainder = 0;
      do {
        Word q;
        if (is_divisor_constant && sizeof(Word) == sizeof(uint32_t)) {
          DivModWord(remainder, dividend[i], divisor, &q, &remainder);
        } else {
          RawDivModWord(remainder, dividend[i], divisor, &q, &remainder);
        }
        if (quotient != nullptr) {
          (*quotient)[i] = q;
        }
      } while (--i >= 0);
      return remainder;
    }
  }
  return 0;
}

constexpr uint8_t NormalizedDivisorShiftAmount(uint64_t divisor) {
  return static_cast<uint8_t>(absl::countl_zero(divisor));
}

// This divides two 64bit words by a single 64bit divisor using Algorithm 4
// from "Improved division by invariant integers" by Moller and Grandlund.
// It is up to the caller to make sure that the quotient has been appropriately
// shifted and to unshift the remainder.
template <uint64_t divisor>
inline void DivModWordNormalizedConstant(uint64_t dividend_hi,
                                         uint64_t dividend_lo,
                                         uint64_t* quotient,
                                         uint64_t* remainder) {
  static_assert(divisor != 0);
  // Shift divisor so that the high order bit is 1.
  constexpr uint64_t kNormalizedDivisor =
      divisor << NormalizedDivisorShiftAmount(divisor);
  constexpr __uint128_t kTwoTo64 = static_cast<__uint128_t>(1) << 64;
  // Multiplicative inverse. There are cleaner ways to write this, but old GCC
  // versions seem to have problems with them.
  constexpr __uint128_t kMaxU128 = ~__uint128_t{0};
  constexpr uint64_t inverse =
      static_cast<uint64_t>(kMaxU128 / kNormalizedDivisor - kTwoTo64);
  __uint128_t q = static_cast<__uint128_t>(dividend_hi) * inverse;
  q += ((static_cast<__uint128_t>(dividend_hi) << 64) | dividend_lo);
  uint64_t q1 = static_cast<uint64_t>((q >> 64) + 1);
  uint64_t q0 = static_cast<uint64_t>(q);
  uint64_t local_remainder = dividend_lo - q1 * kNormalizedDivisor;
  if (local_remainder > q0) {
    // Paper says that this should be cmove and not branch for performance
    // but we will let the compiler decide.
    q1--;
    local_remainder += kNormalizedDivisor;
  }
  // This very rare so we should give a hint to the compiler to not use cmove.
  if (ABSL_PREDICT_FALSE(local_remainder >= kNormalizedDivisor)) {
    q1 += 1;
    local_remainder -= kNormalizedDivisor;
  }
  if (quotient != nullptr) {
    *quotient = q1;
  }
  *remainder = local_remainder;
}

template <int n, uint32_t divisor>
inline uint32_t ShortDivModConstant(const std::array<uint32_t, n>& dividend,
                                    std::integral_constant<uint32_t, divisor> d,
                                    std::array<uint32_t, n>* quotient) {
  return ShortDivMod<uint32_t, n, true>(dividend, divisor, quotient);
}

template <int n, uint64_t divisor>
inline uint64_t ShortDivModConstant(const std::array<uint64_t, n>& dividend,
                                    std::integral_constant<uint64_t, divisor> d,
                                    std::array<uint64_t, n>* quotient) {
  uint64_t remainder = 0;
  // Since the divisor is shifted we also must shift the dividend. We do this
  // inline.
  constexpr uint64_t kShiftAmount = NormalizedDivisorShiftAmount(divisor);
  if constexpr (kShiftAmount != 0) {
    // The first division's quotient is always zero, so we throw it away.
    DivModWordNormalizedConstant<divisor>(
        remainder, ShiftLeftAndGetHighWord(0, dividend.back(), kShiftAmount),
        nullptr, &remainder);
  }
  for (int i = n - 1; i > 0; --i) {
    DivModWordNormalizedConstant<divisor>(
        remainder,
        ShiftLeftAndGetHighWord(dividend[i], dividend[i - 1], kShiftAmount),
        quotient != nullptr ? &((*quotient)[i]) : nullptr, &remainder);
  }
  DivModWordNormalizedConstant<divisor>(
      remainder, ShiftLeftAndGetHighWord(dividend[0], 0, kShiftAmount),
      quotient != nullptr ? &((*quotient)[0]) : nullptr, &remainder);
  return remainder >> kShiftAmount;
}

// Computes *quotient = *dividend / *divisor.
// *dividend and *divisor will be shifted to the left for up to 31 bits so that
// the most significant bit of the most significant non-zero Word of *divisor is
// 1. For this reason, *dividend must have an extra 0 element. The shift amount
// is after the call. returned. To get the remainder, shift *dividend to the
// right by this amount.
// n = NonZeroLength(divisor) (the caller typically already has the value).
// No pointer can be null.
template <int size>
int LongDiv(std::array<uint32_t, size + 1>* dividend,
            std::array<uint32_t, size>* divisor, int n,
            std::array<uint32_t, size>* quotient) {
  // Find the actual length of the dividend.
  int m = NonZeroLength<uint32_t, size>(dividend->data());

  // First we need to normalize the divisor to make the most significant digit
  // of it larger than radix/2 (radix being 2^32 in our case). This
  // is necessary for accurate guesses of the quotient digits. See Knuth "The
  // Art of Computer Programming" Vol.2 for details.
  //
  // We perform normalization by finding how far we need to shift to make the
  // most significant bit of the divisor 1. And then we shift both the dividend
  // and the divisor thus preserving the result of the division.
  int non_zero_bit_idx = FindMSBSetNonZero((*divisor)[n - 1]);
  int shift_amount = 32 - non_zero_bit_idx - 1;

  if (shift_amount > 0) {
    ShiftLeftFast(dividend->data(), size + 1, shift_amount);
    ShiftLeftFast(divisor->data(), size, shift_amount);
  }

  quotient->fill(0);
  uint32_t* dividend_data = dividend->data() + (m - n);
  for (int i = m - n; i >= 0; --i, --dividend_data) {
    // Make the guess of the quotient digit. The guess we take here is:
    //
    //   qhat = min((dividend[m] * b + dividend[m-1]) / divisor[n], b-1)
    //
    // where b is the radix, which in our case is 2^32. In "The Art
    // of Computer Programming" Vol.2 Knuth proves that given the normalization
    // above this guess is often accurate and when not it is always larger than
    // the actual quotient digit and is no larger than 2 removed from it.
    uint32_t quotient_candidate = std::numeric_limits<uint32_t>::max();
    if (dividend_data[n] < (*divisor)[n - 1]) {
      uint32_t r;
      RawDivModWord(dividend_data[n], dividend_data[n - 1], (*divisor)[n - 1],
                    &quotient_candidate, &r);
    }
    std::array<uint32_t, size + 1> dp;
    Copy<32>(divisor->data(), size, dp.data(), size + 1, 0);
    MulWord(dp.data(), n + 1, quotient_candidate);

    if (SubtractWithVariableSize(dividend_data, dp.data(), n + 1) != 0) {
      int iter = 0;
      // If dividend_data was less than dp, meaning the guess was not accurate,
      // then adjust quotient_candidate. As stated above, at the worst, the
      // original guess qhat is q + 2, where q is the actual quotient
      // digit, so this loop will not be executed more than 2 iterations.
      uint8_t carry;
      do {
        ABSL_DCHECK_LE(++iter, 2);
        --quotient_candidate;
        carry = AddWithVariableSize(dividend_data, divisor->data(), n);
        carry = AddWithCarry(&dividend_data[n], uint32_t{0}, carry);
      } while (!carry);
    }
    (*quotient)[i] = quotient_candidate;
  }
  return shift_amount;
}

template <int n>
inline void DivMod(const std::array<uint32_t, n>& dividend,
                   const std::array<uint32_t, n>& divisor,
                   std::array<uint32_t, n>* quotient,
                   std::array<uint32_t, n>* remainder) {
  int divisor_non_zero_len = NonZeroLength<uint32_t, n>(divisor.data());
  if (divisor_non_zero_len <= 1) {
    uint32_t r = ShortDivMod<uint32_t, n>(dividend, divisor[0], quotient);
    if (remainder != nullptr) {
      (*remainder)[0] = r;
      std::fill(remainder->begin() + 1, remainder->end(), 0);
    }
    return;
  }
  std::array<uint32_t, n + 1> extended_dividend;
  Copy<32>(dividend.data(), n, extended_dividend.data(), n + 1, 0);
  std::array<uint32_t, n> divisor_copy = divisor;
  std::array<uint32_t, n> local_quotient;
  int shift_amount =
      LongDiv<n>(&extended_dividend, &divisor_copy, divisor_non_zero_len,
                 quotient != nullptr ? quotient : &local_quotient);
  if (remainder != nullptr) {
    if (shift_amount > 0) {
      ShiftRightFast<uint32_t>(extended_dividend.data(), n + 1, shift_amount);
    }
    Copy<32>(extended_dividend.data(), n, remainder->data(), n, 0);
  }
}

template <int n>
inline void DivMod(const std::array<uint64_t, n>& dividend,
                   const std::array<uint64_t, n>& divisor,
                   std::array<uint64_t, n>* quotient,
                   std::array<uint64_t, n>* remainder) {
  using Array32 = std::array<uint32_t, n * 2>;
#ifdef ABSL_IS_BIG_ENDIAN
  Array32 dividend32 = Convert<32, n * 2, 64, n>(dividend);
  Array32 divisor32 = Convert<32, n * 2, 64, n>(divisor);
  Array32 quotient32;
  Array32 remainder32;
  DivMod<n * 2>(dividend32, divisor32,
                quotient != nullptr ? &quotient32 : nullptr,
                remainder != nullptr ? &remainder32 : nullptr);
  if (quotient != nullptr) {
    *quotient = Convert<64, n, 32, n * 2>(quotient32);
  }
  if (remainder != nullptr) {
    *remainder = Convert<64, n, 32, n * 2>(remainder32);
  }
#else
  DivMod<n * 2>(reinterpret_cast<const Array32&>(dividend),
                reinterpret_cast<const Array32&>(divisor),
                reinterpret_cast<Array32*>(quotient),
                reinterpret_cast<Array32*>(remainder));
#endif
}

template <typename V>
inline constexpr std::make_unsigned_t<V> SafeAbs(V x) {
  // Must cast to unsigned type before negation, or otherwise the result is
  // undefined when V is signed and x has the minimum value.
  return ABSL_PREDICT_TRUE(x >= 0) ? static_cast<std::make_unsigned_t<V>>(x)
                                   : -static_cast<std::make_unsigned_t<V>>(x);
}

// Serializes number and append to *result.
template <bool is_signed, typename Word>
void SerializeNoOptimization(absl::Span<const Word> number,
                             std::string* bytes) {
  ABSL_DCHECK(!number.empty());
  const char extension =
      is_signed && static_cast<std::make_signed_t<Word>>(number.back()) < 0
          ? '\xff'
          : '\x0';
  const size_t old_size = bytes->size();
  bytes->resize(old_size + sizeof(Word) * number.size());
  char* dest = bytes->data() + old_size;
  for (Word word : number) {
    static_assert(sizeof(Word) == sizeof(uint32_t) ||
                  sizeof(Word) == sizeof(uint64_t));
    if (sizeof(Word) == sizeof(uint32_t)) {
      word = googlesql_base::LittleEndian::FromHost32(word);
    } else {
      word = googlesql_base::LittleEndian::FromHost64(word);
    }
    memcpy(dest, &word, sizeof(Word));
    dest += sizeof(Word);
  }
  dest = bytes->data() + old_size;
  char* last_byte = bytes->data() + bytes->size() - 1;
  // Skip last extension characters if they are redundant.
  while (last_byte > dest && *last_byte == extension) {
    --last_byte;
  }
  // If signed and the last byte has a different sign bit than the extension,
  // add one extension byte to preserve the sign bit.
  if (is_signed && ((*last_byte ^ extension) & '\x80') != 0) {
    ++last_byte;
  }
  bytes->resize(old_size + last_byte - dest + 1);
}

// Parse an unsigned string with digits only into result. This function returns
// false only when there are non-numeric characters in the string and does not
// check overflow.
template <typename Word>
bool ParseFromBase10UnsignedString(absl::string_view str, Word* result) {
  *result = 0;
  Word base = 10;
  for (char c : str) {
    if (ABSL_PREDICT_FALSE(!std::isdigit(c))) {
      return false;
    }
    *result *= base;
    *result += c - '0';
  }
  return true;
}

template <typename UnsignedWord>
// Appends segments to result in decimal format. For uint64_t words, each
// segment is 19 digits and must be <= 9999999999999999999.
//  For uint64_t words, each segment is 9 digits and must be <= 999999999. They
//  must be in little endian order.
void AppendSegmentsToString(const UnsignedWord segments[], size_t num_segments,
                            std::string* result);

// The following functions are not optimized for performance, but they can be
// be used to build constexpr variables. Use them only with constexpr inputs.

// MulWord(src, multiplier, carry) returns an array representing
// src * multiplier + carry.
template <typename Word, size_t size, typename... T>
constexpr std::array<Word, size> MulWord(const std::array<Word, size>& src,
                                         Word multiplier, Word carry, T... v) {
  if constexpr (sizeof...(T) < size) {
    static_assert(std::is_unsigned_v<Word>, "Only unsigned Word is supported");
    constexpr int kNumBitsPerWord = sizeof(Word) * 8;
    const auto product =
        static_cast<Uint<kNumBitsPerWord * 2>>(src[sizeof...(T)]) * multiplier +
        carry;
    return MulWord(src, multiplier,
                   static_cast<Word>(product >> kNumBitsPerWord), v...,
                   static_cast<Word>(product));
  } else {
    return std::array<Word, size>{v...};
  }
}

// PowersAsc<Word, first_value, base, size>() returns a std::array<Word, size>
// {first_value, first_value * base, ..., first_value * pow(base, size - 1)}.
template <typename Word, Word first_value, Word base, int size, typename... T>
constexpr std::array<Word, size> PowersAsc(T... v) {
  if constexpr (sizeof...(T) < size) {
    return PowersAsc<Word, first_value, base, size>(first_value, v * base...);
  } else {
    return std::array<Word, size>{v...};
  }
}

extern template void AppendSegmentsToString<uint64_t>(const uint64_t segments[],
                                                      size_t num_segments,
                                                      std::string* result);
extern template void AppendSegmentsToString<uint32_t>(const uint32_t segments[],
                                                      size_t num_segments,
                                                      std::string* result);

}  // namespace multiprecision_int_impl
}  // namespace googlesql

#endif  // GOOGLESQL_COMMON_MULTIPRECISION_INT_IMPL_H_
