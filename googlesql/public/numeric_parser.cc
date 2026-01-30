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

#include "googlesql/public/numeric_parser.h"

#include <array>
#include <cstdint>
#include <limits>

#include "googlesql/common/multiprecision_int.h"
#include "googlesql/public/numeric_constants.h"
#include "absl/base/optimization.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "googlesql/base/status_builder.h"

namespace googlesql {

#define RETURN_FALSE_IF(x) \
  if (ABSL_PREDICT_FALSE(x)) return false

struct ENotationParts {
  bool negative = false;
  absl::string_view int_part;
  absl::string_view fract_part;
  absl::string_view exp_part;
};

bool SplitENotationParts(absl::string_view str, ENotationParts* parts) {
  const char* start = str.data();
  const char* end = str.data() + str.size();

  // Skip whitespace.
  for (; start < end && (absl::ascii_isspace(*start)); ++start) {
  }
  for (; start < end && absl::ascii_isspace(*(end - 1)); --end) {
  }

  // Empty or only spaces
  RETURN_FALSE_IF(start == end);
  *parts = ENotationParts();

  parts->negative = (*start == '-');
  start += (*start == '-' || *start == '+');
  for (const char* c = end; --c >= start;) {
    if (*c == 'e' || *c == 'E') {
      parts->exp_part = absl::string_view(c + 1, end - c - 1);
      RETURN_FALSE_IF(parts->exp_part.empty());
      end = c;
      break;
    }
  }
  for (const char* c = start; c < end; ++c) {
    if (*c == '.') {
      parts->fract_part = absl::string_view(c + 1, end - c - 1);
      end = c;
      break;
    }
  }
  parts->int_part = absl::string_view(start, end - start);
  return true;
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

// Scale the output value by pow(10, extra_exp - decimal_places + type_scale).
// Both exp and decimal_places can come from user inputs, and this
// method detects overflows in the computation.
// Extra_exp is the remaining exponent that wasn't promoted during expansion,
// and type_scale is the scale associated with the type (NUMERIC or BIGNUMERIC)
// being created.
template <int n>
bool ScaleValueToExp(int64_t extra_exp, int64_t decimal_places,
                     int64_t type_scale, FixedUint<64, n>* output) {
  FixedInt<64, 1> scaling_factor(extra_exp);
  RETURN_FALSE_IF(scaling_factor.SubtractOverflow(decimal_places));
  RETURN_FALSE_IF(scaling_factor.AddOverflow(type_scale));
  uint64_t scaling_factor_num = scaling_factor.number()[0];
  for (; scaling_factor_num >= 19; scaling_factor_num -= 19) {
    RETURN_FALSE_IF(output->MultiplyOverflow(internal::k1e19));
  }
  if (scaling_factor_num != 0) {
    static constexpr std::array<uint64_t, 19> kPowers =
        PowersAsc<uint64_t, 1, 10, 19>();
    RETURN_FALSE_IF(output->MultiplyOverflow(kPowers[scaling_factor_num]));
  }
  return true;
}

// Returns true
// iff Concat(high_trailing_digits, low_trailing_digits) == "50...0".
bool IsHalfwayValue(absl::string_view high_trailing_digits,
                    absl::string_view low_trailing_digits) {
  ABSL_DCHECK(!high_trailing_digits.empty());
  if (high_trailing_digits[0] != '5') {
    return false;
  }
  // Remove the first character of the high trailing digits and verify
  // all of the remaining trailing_digits are 0's.
  high_trailing_digits.remove_prefix(1);
  return high_trailing_digits.find_first_not_of('0') ==
             absl::string_view::npos &&
         low_trailing_digits.find_first_not_of('0') == absl::string_view::npos;
}

// Determine if we should round_up when we're in a round_half_even
// situation. This method looks at the entire string to determine
// if we should round up or down.
// Example string value being parsed: "2314.1250" rounding to the hundredth
// digits_to_keep: "231412"
// high_trailing_digits: "50"
// low_trailing_digits: ""
// Example string value being parsed: "2,500.00" rounding to the thousand place
// digits_to_keep: "2"
// high_trailing_digits: "500"
// low_trailing_digits: "00"
bool ShouldRoundUpForHalfEvenRounding(absl::string_view digits_to_keep,
                                      absl::string_view high_trailing_digits,
                                      absl::string_view low_trailing_digits) {
  const bool half_way_value =
      IsHalfwayValue(high_trailing_digits, low_trailing_digits);
  // If we're not halfway between two values, perform standard
  // round_half_away_from_zero rounding.
  if (half_way_value) {
    // Check the last digit in the digits_to_keep, i.e. 2,500 rounded to exp
    // -3, the digits to keep would be "2", and trailing digits would be "500".
    // This code checks whether the previous digit, "2" in this case, is odd
    // or even.
    const char previous_digit =
        digits_to_keep.empty() ? '0' : digits_to_keep.back();
    // If the previous digit is odd we should round up (to the nearest even). If
    // the previous digit is even, return false and we'll round down. Instead of
    // checking whether (previous_digit - '0') is an odd number, we only need to
    // check previous_digit because '0' has an even value (48).
    return previous_digit & 1;
  } else {
    return high_trailing_digits[0] >= '5';
  }
}

// Parses <exp_part> and add <extra_scale> to the result.
// If <exp_part> represents an integer that is below int64min, the result is
// int64min.
// If <exp_part> represents an integer that is above int64max, the result is
// int64max.
bool ParseExponent(absl::string_view exp_part, int64_t extra_scale,
                   int64_t* exp) {
  *exp = extra_scale;
  if (!exp_part.empty()) {
    FixedInt<64, 1> exp_fixed_int;
    if (ABSL_PREDICT_TRUE(exp_fixed_int.ParseFromStringStrict(exp_part))) {
      RETURN_FALSE_IF(exp_fixed_int.AddOverflow(*exp));
      *exp = exp_fixed_int.number()[0];
    } else if (exp_part.size() > 1 && exp_part[0] == '-') {
      // Still need to check whether exp_part contains only digits after '-'.
      exp_part.remove_prefix(1);
      for (char c : exp_part) {
        RETURN_FALSE_IF(!std::isdigit(c));
      }
      *exp = std::numeric_limits<int64_t>::min();
    } else {
      for (char c : exp_part) {
        RETURN_FALSE_IF(!std::isdigit(c));
      }
      *exp = std::numeric_limits<int64_t>::max();
    }
  }
  return true;
}

// Parses <int_part>.<fract_part>E<exp> to FixedUint.
// If rounding_mode is round_half_away_from_zero, it rounds the input away from
// zero to a whole number. If rounding_mode is round_half_even, it rounds
// half-way values to the nearest even digit.
// exp: the decimal place we are rounding to (can be negative) plus the exponent
// piece of the string notation
// scale: the total scale of the DECIMAL type we are evaluating (different
// for numeric and bignumeric).
// Returns true iff the input is valid.
template <int n, internal::DigitTrimMode trim_mode>
bool ParseNumberInternal(absl::string_view int_part,
                         absl::string_view fract_part, int64_t exp,
                         int64_t decimal_places, int32_t scale,
                         FixedUint<64, n>* output) {
  *output = FixedUint<64, n>();
  bool round_up = false;
  // If the exponent is positive, we are dealing with the fract_part of the
  // number.
  if (exp >= 0) {
    size_t num_promoted_fract_digits = fract_part.size();
    // If the exp specified is less than the size of fract_part, we'll need
    // to promote certain digits and figure out whether to round up or down.
    if (exp < fract_part.size()) {
      num_promoted_fract_digits = exp;
      if constexpr (trim_mode ==
                    internal::DigitTrimMode::kTrimRoundHalfAwayFromZero) {
        round_up = fract_part[num_promoted_fract_digits] >= '5';
      } else if constexpr (trim_mode ==
                           internal::DigitTrimMode::kTrimRoundHalfEven) {
        absl::string_view high_trailing_digits = fract_part;
        high_trailing_digits.remove_prefix(num_promoted_fract_digits);
        absl::string_view digits_to_keep =
            num_promoted_fract_digits > 0
                ? absl::string_view(fract_part.data(),
                                    num_promoted_fract_digits)
                : int_part;
        round_up = ShouldRoundUpForHalfEvenRounding(digits_to_keep,
                                                    high_trailing_digits, {});
      }
    }
    absl::string_view promoted_fract_part(fract_part.data(),
                                          num_promoted_fract_digits);
    fract_part.remove_prefix(num_promoted_fract_digits);
    if (int_part.empty()) {
      RETURN_FALSE_IF(!output->ParseFromStringStrict(promoted_fract_part));
    } else {
      RETURN_FALSE_IF(
          !output->ParseFromStringSegments(int_part, {promoted_fract_part}));
      int_part = absl::string_view();
    }
    RETURN_FALSE_IF(round_up && output->AddOverflow(uint64_t{1}));
    // If output is zero avoid scaling.
    if (!output->is_zero()) {
      // Scale by -decimal_places + the scale of the DECIMAL type + the
      // remaining portion of the exponent that wasn't consumed by promoting
      // fractional digits. Scaling_factor should never be negative because
      // we have clamped the high value of decimal_places to be scale.
      int64_t extra_exponent =
          static_cast<int64_t>(exp - num_promoted_fract_digits);
      RETURN_FALSE_IF(
          !ScaleValueToExp(extra_exponent, decimal_places, scale, output));
    }
  } else {  // exp < 0
    RETURN_FALSE_IF(int_part.size() + fract_part.size() == 0);
    // Demote up to -exp digits from int_part
    if (exp >= static_cast<int64_t>(-int_part.size())) {
      size_t int_digits = int_part.size() + exp;
      if constexpr (trim_mode ==
                    internal::DigitTrimMode::kTrimRoundHalfAwayFromZero) {
        round_up = int_part[int_digits] >= '5';
      } else if constexpr (trim_mode ==
                           internal::DigitTrimMode::kTrimRoundHalfEven) {
        absl::string_view high_trailing_digits = int_part;
        high_trailing_digits.remove_prefix(int_digits);
        absl::string_view digits_to_keep(int_part.data(), int_digits);
        round_up = ShouldRoundUpForHalfEvenRounding(
            digits_to_keep, high_trailing_digits, fract_part);
      }
      RETURN_FALSE_IF(int_digits != 0 &&
                      !output->ParseFromStringStrict(
                          absl::string_view(int_part.data(), int_digits)));
      RETURN_FALSE_IF(round_up && output->AddOverflow(uint64_t{1}));
      // If output is zero, avoid scaling. Decimal places is high_value clamped
      // at scale, so scaling_factor can never be negative.
      if (!output->is_zero()) {
        RETURN_FALSE_IF(!ScaleValueToExp(0, decimal_places, scale, output));
      }
      int_part.remove_prefix(int_digits);
    }
  }
  // The remaining characters in int_part and fract_part have not been visited.
  // They represent the fractional digits to be discarded. In strict mode, they
  // must be zeros; otherwise they must be digits.
  if constexpr (trim_mode == internal::DigitTrimMode::kError) {
    for (char c : int_part) {
      RETURN_FALSE_IF(c != '0');
    }
    for (char c : fract_part) {
      RETURN_FALSE_IF(c != '0');
    }
  } else {
    for (char c : int_part) {
      RETURN_FALSE_IF(!std::isdigit(c));
    }
    for (char c : fract_part) {
      RETURN_FALSE_IF(!std::isdigit(c));
    }
  }
  return true;
}

#undef RETURN_FALSE_IF

template <uint32_t word_count, internal::DigitTrimMode trim_mode>
absl::Status ParseNumber(absl::string_view str, int64_t decimal_places,
                         int32_t scale,
                         FixedPointRepresentation<word_count>& parsed) {
  ENotationParts parts;
  int64_t exp;
  // Clamp decimal_places max to scale, a customer rounding to more decimal
  // places than scale will round to max allowed decimal places (scale), or
  // error if the trim_mode is kError.
  if (decimal_places > scale) {
    decimal_places = scale;
  }
  if (ABSL_PREDICT_TRUE(SplitENotationParts(str, &parts)) &&
      ABSL_PREDICT_TRUE(ParseExponent(parts.exp_part, decimal_places, &exp))) {
    const bool success = ParseNumberInternal<word_count, trim_mode>(
        parts.int_part, parts.fract_part, exp, decimal_places, scale,
        &parsed.output);
    if (ABSL_PREDICT_TRUE(success)) {
      parsed.is_negative = parts.negative;
      return absl::OkStatus();
    }
  }
  return ::googlesql_base::InvalidArgumentErrorBuilder()
         << "Failed to parse " << str << " . word_count: " << word_count
         << " scale: " << scale << " trim_mode:" << trim_mode;
}

template <internal::DigitTrimMode trim_mode>
absl::Status ParseNumericWithRounding(absl::string_view str,
                                      int64_t decimal_places,
                                      FixedPointRepresentation<2>& parsed) {
  return ParseNumber<2, trim_mode>(str, decimal_places, 9, parsed);
}

template <internal::DigitTrimMode trim_mode>
absl::Status ParseBigNumericWithRounding(absl::string_view str,
                                         int64_t decimal_places,
                                         FixedPointRepresentation<4>& parsed) {
  return ParseNumber<4, trim_mode>(str, decimal_places, 38, parsed);
}

absl::Status ParseJSONNumber(absl::string_view str,
                             FixedPointRepresentation<79>& parsed) {
  return ParseNumber<79, internal::DigitTrimMode::kError>(str, 1074, 1074,
                                                          parsed);
}

// Explicit template instantiations for NUMERIC
template absl::Status ParseNumericWithRounding<internal::DigitTrimMode::kError>(
    absl::string_view str, int64_t decimal_places,
    FixedPointRepresentation<2>& parsed);
template absl::Status
ParseNumericWithRounding<internal::DigitTrimMode::kTrimRoundHalfAwayFromZero>(
    absl::string_view str, int64_t decimal_places,
    FixedPointRepresentation<2>& parsed);
template absl::Status
ParseNumericWithRounding<internal::DigitTrimMode::kTrimRoundHalfEven>(
    absl::string_view str, int64_t decimal_places,
    FixedPointRepresentation<2>& parsed);

// Explicit template instantiations for BIGNUMERIC
template absl::Status
ParseBigNumericWithRounding<internal::DigitTrimMode::kError>(
    absl::string_view str, int64_t decimal_places,
    FixedPointRepresentation<4>& parsed);
template absl::Status ParseBigNumericWithRounding<
    internal::DigitTrimMode::kTrimRoundHalfAwayFromZero>(
    absl::string_view str, int64_t decimal_places,
    FixedPointRepresentation<4>& parsed);
template absl::Status
ParseBigNumericWithRounding<internal::DigitTrimMode::kTrimRoundHalfEven>(
    absl::string_view str, int64_t decimal_places,
    FixedPointRepresentation<4>& parsed);

}  // namespace googlesql
