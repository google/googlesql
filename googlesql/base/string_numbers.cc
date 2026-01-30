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

// This file contains string processing functions related to
// numeric values.

#include "googlesql/base/string_numbers.h"

#include <cassert>
#include <cfloat>  // for DBL_DIG and FLT_DIG
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>

#include "googlesql/base/check.h"
#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/logging.h"

namespace googlesql_base {
namespace {

// Represents integer values of digits.
// Uses 36 to indicate an invalid character since we support
// bases up to 36.
static const int8_t kAsciiToInt[256] = {
    36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36,  // 16 36s.
    36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36,
    36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 0,  1,  2,  3,  4,  5,
    6,  7,  8,  9,  36, 36, 36, 36, 36, 36, 36, 10, 11, 12, 13, 14, 15, 16, 17,
    18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36,
    36, 36, 36, 36, 36, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
    24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 36, 36, 36, 36, 36, 36,
    36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36,
    36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36,
    36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36,
    36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36,
    36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36,
    36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36,
    36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36, 36};

// Parse the sign and optional hex or oct prefix in text.
inline bool safe_parse_sign_and_base(absl::string_view* text /*inout*/,
                                     int* base_ptr /*inout*/,
                                     bool* negative_ptr /*output*/) {
  if (text->data() == nullptr) {
    return false;
  }

  const char* start = text->data();
  const char* end = start + text->size();
  int base = *base_ptr;

  // Consume whitespace.
  while (start < end && absl::ascii_isspace(start[0])) {
    ++start;
  }
  while (start < end && absl::ascii_isspace(end[-1])) {
    --end;
  }
  if (start >= end) {
    return false;
  }

  // Consume sign.
  *negative_ptr = (start[0] == '-');
  if (*negative_ptr || start[0] == '+') {
    ++start;
    if (start >= end) {
      return false;
    }
  }

  // Consume base-dependent prefix.
  //  base 0: "0x" -> base 16, "0" -> base 8, default -> base 10
  //  base 16: "0x" -> base 16
  // Also validate the base.
  if (base == 0) {
    if (end - start >= 2 && start[0] == '0' &&
        (start[1] == 'x' || start[1] == 'X')) {
      base = 16;
      start += 2;
      if (start >= end) {
        // "0x" with no digits after is invalid.
        return false;
      }
    } else if (end - start >= 1 && start[0] == '0') {
      base = 8;
      start += 1;
    } else {
      base = 10;
    }
  } else if (base == 16) {
    if (end - start >= 2 && start[0] == '0' &&
        (start[1] == 'x' || start[1] == 'X')) {
      start += 2;
      if (start >= end) {
        // "0x" with no digits after is invalid.
        return false;
      }
    }
  } else if (base >= 2 && base <= 36) {
    // okay
  } else {
    return false;
  }
  *text = absl::string_view(start, end - start);
  *base_ptr = base;
  return true;
}

// Consume digits.
//
// The classic loop:
//
//   for each digit
//     value = value * base + digit
//   value *= sign
//
// The classic loop needs overflow checking.  It also fails on the most
// negative integer, -2147483648 in 32-bit two's complement representation.
//
// My improved loop:
//
//  if (!negative)
//    for each digit
//      value = value * base
//      value = value + digit
//  else
//    for each digit
//      value = value * base
//      value = value - digit
//
// Overflow checking becomes simple.

// Lookup tables per IntType:
// vmax/base and vmin/base are precomputed because division costs at least 8ns.
// TODO: Doing this per base instead (i.e. an array of structs, not a
// struct of arrays) would probably be better in terms of d-cache for the most
// commonly used bases.
template <typename IntType>
struct LookupTables {
  static const IntType kVmaxOverBase[];
  static const IntType kVminOverBase[];
};

// An array initializer macro for X/base where base in [0, 36].
// However, note that lookups for base in [0, 1] should never happen because
// base has been validated to be in [2, 36] by safe_parse_sign_and_base().
#define X_OVER_BASE_INITIALIZER(X)                                        \
  {                                                                       \
    0, 0, X / 2, X / 3, X / 4, X / 5, X / 6, X / 7, X / 8, X / 9, X / 10, \
        X / 11, X / 12, X / 13, X / 14, X / 15, X / 16, X / 17, X / 18,   \
        X / 19, X / 20, X / 21, X / 22, X / 23, X / 24, X / 25, X / 26,   \
        X / 27, X / 28, X / 29, X / 30, X / 31, X / 32, X / 33, X / 34,   \
        X / 35, X / 36,                                                   \
  }

template <typename IntType>
const IntType LookupTables<IntType>::kVmaxOverBase[] =
    X_OVER_BASE_INITIALIZER(std::numeric_limits<IntType>::max());

template <typename IntType>
const IntType LookupTables<IntType>::kVminOverBase[] =
    X_OVER_BASE_INITIALIZER(std::numeric_limits<IntType>::min());

#undef X_OVER_BASE_INITIALIZER

template <typename IntType>
inline bool safe_parse_positive_int(absl::string_view text, int base,
                                    IntType* value_p) {
  IntType value = 0;
  const IntType vmax = std::numeric_limits<IntType>::max();
  assert(vmax > 0);
  assert(base >= 0);
  assert(vmax >= static_cast<IntType>(base));
  const IntType vmax_over_base = LookupTables<IntType>::kVmaxOverBase[base];
  const char* start = text.data();
  const char* end = start + text.size();
  // loop over digits
  for (; start < end; ++start) {
    unsigned char c = static_cast<unsigned char>(start[0]);
    int digit = kAsciiToInt[c];
    if (digit >= base) {
      *value_p = value;
      return false;
    }
    if (value > vmax_over_base) {
      *value_p = vmax;
      return false;
    }
    value *= base;
    if (value > vmax - digit) {
      *value_p = vmax;
      return false;
    }
    value += digit;
  }
  *value_p = value;
  return true;
}

template <typename IntType>
inline bool safe_parse_negative_int(absl::string_view text, int base,
                                    IntType* value_p) {
  IntType value = 0;
  const IntType vmin = std::numeric_limits<IntType>::min();
  assert(vmin < 0);
  assert(vmin <= 0 - base);
  IntType vmin_over_base = LookupTables<IntType>::kVminOverBase[base];
  // 2003 c++ standard [expr.mul]
  // "... the sign of the remainder is implementation-defined."
  // Although (vmin/base)*base + vmin%base is always vmin.
  // 2011 c++ standard tightens the spec but we cannot rely on it.
  // TODO: Handle this in the lookup table generation.
  if (vmin % base > 0) {
    vmin_over_base += 1;
  }
  const char* start = text.data();
  const char* end = start + text.size();
  // loop over digits
  for (; start < end; ++start) {
    unsigned char c = static_cast<unsigned char>(start[0]);
    int digit = kAsciiToInt[c];
    if (digit >= base) {
      *value_p = value;
      return false;
    }
    if (value < vmin_over_base) {
      *value_p = vmin;
      return false;
    }
    value *= base;
    if (value < vmin + digit) {
      *value_p = vmin;
      return false;
    }
    value -= digit;
  }
  *value_p = value;
  return true;
}

// Input format based on POSIX.1-2008 strtol
// http://pubs.opengroup.org/onlinepubs/9699919799/functions/strtol.html
template <typename IntType>
inline bool safe_int_internal(absl::string_view text, IntType* value_p,
                              int base) {
  *value_p = 0;
  bool negative;
  if (!safe_parse_sign_and_base(&text, &base, &negative)) {
    return false;
  }
  if (!negative) {
    return safe_parse_positive_int(text, base, value_p);
  } else {
    return safe_parse_negative_int(text, base, value_p);
  }
}

template <typename IntType>
inline bool safe_uint_internal(absl::string_view text, IntType* value_p,
                               int base) {
  *value_p = 0;
  bool negative;
  if (!safe_parse_sign_and_base(&text, &base, &negative) || negative) {
    return false;
  }
  return safe_parse_positive_int(text, base, value_p);
}

static constexpr double kDoublePrecisionCheckMax = DBL_MAX / 1.000000000000001;
static constexpr int kFastToBufferSize = 32;

}  // anonymous namespace

std::string RoundTripDoubleToString(double d) {
  // DBL_DIG is 15 for IEEE-754 doubles, which are used on almost all
  // platforms these days.  Just in case some system exists where DBL_DIG
  // is significantly larger -- and risks overflowing our buffer -- we have
  // this assert.
  static_assert(DBL_DIG < 20, "DBL_DIG is too big");
  char buffer[kFastToBufferSize];
  bool full_precision_needed = true;
  if (std::abs(d) <= kDoublePrecisionCheckMax) {
    int snprintf_result =
        snprintf(buffer, kFastToBufferSize, "%.*g", DBL_DIG, d);

    // The snprintf should never overflow because the buffer is significantly
    // larger than the precision we asked for.
    ABSL_CHECK(snprintf_result > 0 && snprintf_result < kFastToBufferSize);

    full_precision_needed = strtod(buffer, nullptr) != d;
  }

  if (full_precision_needed) {
    int snprintf_result =
        snprintf(buffer, kFastToBufferSize, "%.*g", DBL_DIG + 2, d);

    // Should never overflow; see above.
    ABSL_CHECK(snprintf_result > 0 && snprintf_result < kFastToBufferSize);
  }
  return buffer;
}

std::string RoundTripFloatToString(float value) {
  char buffer[kFastToBufferSize];
  // FLT_DIG is 6 for IEEE-754 floats, which are used on almost all
  // platforms these days.  Just in case some system exists where FLT_DIG
  // is significantly larger -- and risks overflowing our buffer -- we have
  // this assert.
  static_assert(FLT_DIG < 10, "FLT_DIG is too big");

  int snprintf_result =
      snprintf(buffer, kFastToBufferSize, "%.*g", FLT_DIG, value);

  // The snprintf should never overflow because the buffer is significantly
  // larger than the precision we asked for.
  ABSL_CHECK(snprintf_result > 0 && snprintf_result < kFastToBufferSize);

  float parsed_value;
  if (!absl::SimpleAtof(buffer, &parsed_value) || parsed_value != value) {
    snprintf_result =
        snprintf(buffer, kFastToBufferSize, "%.*g", FLT_DIG + 2, value);

    // Should never overflow; see above.
    ABSL_DCHECK(snprintf_result > 0 && snprintf_result < kFastToBufferSize);
  }
  return buffer;
}

bool safe_strto32_base(absl::string_view text, int32_t* value, int base) {
  return safe_int_internal<int32_t>(text, value, base);
}

bool safe_strto64_base(absl::string_view text, int64_t* value, int base) {
  return safe_int_internal<int64_t>(text, value, base);
}

bool safe_strtou32_base(absl::string_view text, uint32_t* value, int base) {
  return safe_uint_internal<uint32_t>(text, value, base);
}

bool safe_strtou64_base(absl::string_view text, uint64_t* value, int base) {
  return safe_uint_internal<uint64_t>(text, value, base);
}

}  // namespace googlesql_base
