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

#include "googlesql/public/functions/cast_date_time.h"

#include <string.h>
#include <time.h>

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/common/errors.h"
#include "googlesql/common/utf_util.h"
#include "googlesql/public/functions/date_time_util.h"
#include "googlesql/public/functions/datetime.pb.h"
#include "googlesql/public/functions/input_format_string_max_width.h"
#include "googlesql/public/functions/parse_date_time_utils.h"
#include "googlesql/public/pico_time.h"
#include "googlesql/public/strings.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "absl/base/optimization.h"
#include "absl/container/flat_hash_map.h"
#include "absl/flags/flag.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/time/civil_time.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "unicode/uchar.h"
#include "unicode/utf8.h"
#include "googlesql/base/general_trie.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/mathutil.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace functions {
namespace {

using cast_date_time_internal::DateTimeFormatElement;
using cast_date_time_internal::FormatElementCategory;
using cast_date_time_internal::FormatElementType;
using cast_date_time_internal::GetDateTimeFormatElements;
using parse_date_time_utils::ConvertTimeToTimestamp;
using parse_date_time_utils::ParseInt;
using parse_date_time_utils::ParseSubSeconds;

using CategoryToElementsMap =
    absl::flat_hash_map<FormatElementCategory,
                        std::vector<const DateTimeFormatElement*>>;
using TypeToElementMap =
    absl::flat_hash_map<FormatElementType, const DateTimeFormatElement*>;

static const int64_t powers_of_ten[] = {
    1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000};

constexpr int64_t kNaiveNumSecondsPerMinute = 60;
constexpr int64_t kNaiveNumSecondsPerHour = 60 * kNaiveNumSecondsPerMinute;
constexpr int64_t kNaiveNumSecondsPerDay = 24 * kNaiveNumSecondsPerHour;
constexpr int64_t kNaiveNumMicrosPerDay = kNaiveNumSecondsPerDay * 1000 * 1000;

// Matches <target_str> with string <input_str> in a char-by-char manner. The
// matching is case-insensitive if <ignore_case> is true and case-sensitive
// otherwise. Returns the number of consumed characters upon successful
// matching, and returns absl::string_view::npos otherwise.
size_t ParseStringByExactMatch(absl::string_view input_str,
                               absl::string_view target_str,
                               bool ignore_case = false) {
  if (target_str.empty()) {
    return 0;
  }

  if (ignore_case ? absl::StartsWithIgnoreCase(input_str, target_str)
                  : absl::StartsWith(input_str, target_str)) {
    return target_str.size();
  } else {
    return absl::string_view::npos;
  }
}

struct ParseWithCandidatesResult {
  size_t parsed_length = absl::string_view::npos;
  int matched_candidate_index;
};

// Matches candidate strings inside <candidates> with <input_str> in a
// char-by-char manner. The matching is case-insensitve if <ignore_case> is true
// and case-sensitive otherwise. Sets the <parsed_length> inside returned
// ParseWithCandidatesResult object to be the number of consumed characters
// and <matched_candidate_ind> to be the index of the first matched string upon
// successful matching, and sets <parsed_length> to be absl::string_view::npos
// otherwise.
ParseWithCandidatesResult ParseStringWithCandidates(
    absl::string_view input_str, absl::Span<const absl::string_view> candidates,
    bool ignore_case) {
  for (size_t i = 0; i < candidates.size(); ++i) {
    size_t parsed_length =
        ParseStringByExactMatch(input_str, candidates[i], ignore_case);
    if (parsed_length != absl::string_view::npos) {
      return {.parsed_length = parsed_length,
              .matched_candidate_index = static_cast<int>(i)};
    }
  }

  return {.parsed_length = absl::string_view::npos};
}

// Matches <input_str> with abbreviated or full month names in a
// case-insensitive way. Returns the number of consumed characters and
// produces <month> value upon successful matching, and returns
// absl::string_view::npos otherwise.
size_t ParseMonthNames(absl::string_view timestamp_string, bool abbreviated,
                       int* month) {
  absl::Span<const absl::string_view> month_names;

  if (abbreviated) {
    static const absl::string_view kAbbreviatedMonthNames[] = {
        "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
        "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    month_names = kAbbreviatedMonthNames;
  } else {
    static const absl::string_view kFullMonthNames[] = {
        "JANUARY", "FEBRUARY", "MARCH",     "APRIL",   "MAY",      "JUNE",
        "JULY",    "AUGUST",   "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER"};
    month_names = kFullMonthNames;
  }

  ParseWithCandidatesResult parsing_result =
      ParseStringWithCandidates(timestamp_string, month_names,
                                /*ignore_case=*/true);
  if (parsing_result.parsed_length != absl::string_view::npos) {
    *month = parsing_result.matched_candidate_index + 1;
  }

  return parsing_result.parsed_length;
}

// This functions is similar to parse_date_time_utils::ParseInt function but
// accepts <input_str> of string_view type and we will verify that the number of
// parsed characters is within the range of [<min_width>, <max_width>]. Returns
// the number of consumed characters upon successfully parsing an integer, and
// returns absl::string_view::npos otherwise.
size_t ParseInt(absl::string_view input_str, int min_width, int max_width,
                int64_t min, int64_t max, int* value_ptr) {
  const char* res_dp =
      ParseInt(input_str.data(), input_str.data() + input_str.size(), max_width,
               min, max, value_ptr);
  if (res_dp == nullptr) {
    return absl::string_view::npos;
  }
  size_t parsed_width = res_dp - input_str.data();
  if (parsed_width < min_width || parsed_width > max_width) {
    return absl::string_view::npos;
  }

  return parsed_width;
}

// This functions is similar to parse_date_time_utils::ParseSubSeconds function
// but accepts <input_str> of string_view type and we will verify that the
// number of parsed characters is within the range of
// [<min_width>, <max_width>]. Returns the number of consumed characters upon
// successful parsing, and returns absl::string_view::npos otherwise.
size_t ParseSubSeconds(absl::string_view input_str, int min_width,
                       int max_width, TimestampScale scale,
                       absl::Duration* subseconds, uint32_t* subnanoseconds) {
  const char* res_dp =
      ParseSubSeconds(input_str.data(), input_str.data() + input_str.size(),
                      max_width, scale, subseconds, subnanoseconds);
  if (res_dp == nullptr) {
    return absl::string_view::npos;
  }
  size_t parsed_width = res_dp - input_str.data();
  if (parsed_width < min_width || parsed_width > max_width) {
    return absl::string_view::npos;
  }

  return parsed_width;
}

// Consumes the leading Unicode whitespaces in the string <input_str>. Returns
// the number of consumed characters.
size_t TrimLeadingUnicodeWhiteSpaces(absl::string_view input_str) {
  UChar32 character;
  size_t data_length = input_str.size();
  // Offset for consecutive Unicode whitespaces since <dp>.
  size_t all_uwhitespace_offset = 0;
  size_t offset = 0;
  while (offset < data_length) {
    U8_NEXT(input_str.data(), offset, data_length, character);
    if (u_isUWhiteSpace(character)) {
      all_uwhitespace_offset = offset;
    } else {
      break;
    }
  }
  return all_uwhitespace_offset;
}

std::string FormatElementTypeString(const FormatElementType& type) {
  switch (type) {
    case FormatElementType::kFormatElementTypeUnspecified:
      return "FORMAT_ELEMENT_TYPE_UNSPECIFIED";
    case FormatElementType::kSimpleLiteral:
      return "SIMPLE_LITERAL";
    case FormatElementType::kDoubleQuotedLiteral:
      return "DOUBLE_QUOTED_LITERAL";
    case FormatElementType::kWhitespace:
      return "WHITESPACE";
    case FormatElementType::kYYYY:
      return "YYYY";
    case FormatElementType::kYYY:
      return "YYY";
    case FormatElementType::kYY:
      return "YY";
    case FormatElementType::kY:
      return "Y";
    case FormatElementType::kRRRR:
      return "RRRR";
    case FormatElementType::kRR:
      return "RR";
    case FormatElementType::kYCommaYYY:
      return "Y,YYY";
    case FormatElementType::kIYYY:
      return "IYYY";
    case FormatElementType::kIYY:
      return "IYY";
    case FormatElementType::kIY:
      return "IY";
    case FormatElementType::kI:
      return "I";
    case FormatElementType::kSYYYY:
      return "SYYYY";
    case FormatElementType::kYEAR:
      return "YEAR";
    case FormatElementType::kSYEAR:
      return "SYEAR";
    case FormatElementType::kMM:
      return "MM";
    case FormatElementType::kMON:
      return "MON";
    case FormatElementType::kMONTH:
      return "MONTH";
    case FormatElementType::kRM:
      return "RM";
    case FormatElementType::kDDD:
      return "DDD";
    case FormatElementType::kDD:
      return "DD";
    case FormatElementType::kD:
      return "D";
    case FormatElementType::kDAY:
      return "DAY";
    case FormatElementType::kDY:
      return "DY";
    case FormatElementType::kJ:
      return "J";
    case FormatElementType::kHH:
      return "HH";
    case FormatElementType::kHH12:
      return "HH12";
    case FormatElementType::kHH24:
      return "HH24";
    case FormatElementType::kMI:
      return "MI";
    case FormatElementType::kSS:
      return "SS";
    case FormatElementType::kSSSSS:
      return "SSSSS";
    case FormatElementType::kFFN:
      return "FFN";
    case FormatElementType::kAM:
      return "AM";
    case FormatElementType::kPM:
      return "PM";
    case FormatElementType::kAMWithDots:
      return "A.M.";
    case FormatElementType::kPMWithDots:
      return "P.M.";
    case FormatElementType::kTZH:
      return "TZH";
    case FormatElementType::kTZM:
      return "TZM";
    case FormatElementType::kCC:
      return "CC";
    case FormatElementType::kSCC:
      return "SCC";
    case FormatElementType::kQ:
      return "Q";
    case FormatElementType::kIW:
      return "IW";
    case FormatElementType::kWW:
      return "WW";
    case FormatElementType::kW:
      return "W";
    case FormatElementType::kAD:
      return "AD";
    case FormatElementType::kBC:
      return "BC";
    case FormatElementType::kADWithDots:
      return "A.D.";
    case FormatElementType::kBCWithDots:
      return "B.C.";
    case FormatElementType::kSP:
      return "SP";
    case FormatElementType::kTH:
      return "TH";
    case FormatElementType::kSPTH:
      return "SPTH";
    case FormatElementType::kTHSP:
      return "THSP";
    case FormatElementType::kFM:
      return "FM";
  }
}

FormatElementCategory GetFormatElementCategoryFromType(
    const FormatElementType& type) {
  switch (type) {
    case FormatElementType::kFormatElementTypeUnspecified:
      return FormatElementCategory::kFormatElementCategoryUnspecified;
    case FormatElementType::kSimpleLiteral:
    case FormatElementType::kDoubleQuotedLiteral:
    case FormatElementType::kWhitespace:
      return FormatElementCategory::kLiteral;
    case FormatElementType::kYYYY:
    case FormatElementType::kYYY:
    case FormatElementType::kYY:
    case FormatElementType::kY:
    case FormatElementType::kRRRR:
    case FormatElementType::kRR:
    case FormatElementType::kYCommaYYY:
    case FormatElementType::kIYYY:
    case FormatElementType::kIYY:
    case FormatElementType::kIY:
    case FormatElementType::kI:
    case FormatElementType::kSYYYY:
    case FormatElementType::kYEAR:
    case FormatElementType::kSYEAR:
      return FormatElementCategory::kYear;
    case FormatElementType::kMM:
    case FormatElementType::kMON:
    case FormatElementType::kMONTH:
    case FormatElementType::kRM:
      return FormatElementCategory::kMonth;
    case FormatElementType::kDDD:
    case FormatElementType::kDD:
    case FormatElementType::kD:
    case FormatElementType::kDAY:
    case FormatElementType::kDY:
    case FormatElementType::kJ:
      return FormatElementCategory::kDay;
    case FormatElementType::kHH:
    case FormatElementType::kHH12:
    case FormatElementType::kHH24:
      return FormatElementCategory::kHour;
    case FormatElementType::kMI:
      return FormatElementCategory::kMinute;
    case FormatElementType::kSS:
    case FormatElementType::kSSSSS:
    case FormatElementType::kFFN:
      return FormatElementCategory::kSecond;
    case FormatElementType::kAM:
    case FormatElementType::kPM:
    case FormatElementType::kAMWithDots:
    case FormatElementType::kPMWithDots:
      return FormatElementCategory::kMeridianIndicator;
    case FormatElementType::kTZH:
    case FormatElementType::kTZM:
      return FormatElementCategory::kTimeZone;
    case FormatElementType::kCC:
    case FormatElementType::kSCC:
      return FormatElementCategory::kCentury;
    case FormatElementType::kQ:
      return FormatElementCategory::kQuarter;
    case FormatElementType::kIW:
    case FormatElementType::kWW:
    case FormatElementType::kW:
      return FormatElementCategory::kWeek;
    case FormatElementType::kAD:
    case FormatElementType::kBC:
    case FormatElementType::kADWithDots:
    case FormatElementType::kBCWithDots:
      return FormatElementCategory::kEraIndicator;
    case FormatElementType::kSP:
    case FormatElementType::kTH:
    case FormatElementType::kSPTH:
    case FormatElementType::kTHSP:
    case FormatElementType::kFM:
      return FormatElementCategory::kMisc;
  }
}

std::string FormatElementCategoryString(const FormatElementCategory& category) {
  switch (category) {
    case FormatElementCategory::kFormatElementCategoryUnspecified:
      return "FORMAT_ELEMENT_CATEGORY_UNSPECIFIED";
    case FormatElementCategory::kLiteral:
      return "LITERAL";
    case FormatElementCategory::kYear:
      return "YEAR";
    case FormatElementCategory::kMonth:
      return "MONTH";
    case FormatElementCategory::kDay:
      return "DAY";
    case FormatElementCategory::kHour:
      return "HOUR";
    case FormatElementCategory::kMinute:
      return "MINUTE";
    case FormatElementCategory::kSecond:
      return "SECOND";
    case FormatElementCategory::kMeridianIndicator:
      return "MERIDIAN_INDICATOR";
    case FormatElementCategory::kTimeZone:
      return "TIME_ZONE";
    case FormatElementCategory::kCentury:
      return "CENTURY";
    case FormatElementCategory::kQuarter:
      return "QUARTER";
    case FormatElementCategory::kWeek:
      return "WEEK";
    case FormatElementCategory::kEraIndicator:
      return "ERA_INDICATOR";
    case FormatElementCategory::kMisc:
      return "MISC";
  }
}

// Checks whether the format element is supported for parsing.
bool IsSupportedForParsing(const DateTimeFormatElement& format_element) {
  switch (format_element.type) {
    case FormatElementType::kSimpleLiteral:
    case FormatElementType::kDoubleQuotedLiteral:
    case FormatElementType::kWhitespace:
    case FormatElementType::kYYYY:
    case FormatElementType::kYYY:
    case FormatElementType::kYY:
    case FormatElementType::kY:
    case FormatElementType::kRRRR:
    case FormatElementType::kRR:
    case FormatElementType::kYCommaYYY:
    case FormatElementType::kMM:
    case FormatElementType::kMON:
    case FormatElementType::kMONTH:
    case FormatElementType::kDD:
    case FormatElementType::kHH:
    case FormatElementType::kHH12:
    case FormatElementType::kHH24:
    case FormatElementType::kMI:
    case FormatElementType::kSS:
    case FormatElementType::kSSSSS:
    case FormatElementType::kFFN:
    case FormatElementType::kAMWithDots:
    case FormatElementType::kPMWithDots:
    case FormatElementType::kTZH:
    case FormatElementType::kTZM:
      return true;
    default:
      return false;
  }
}

// Helper function that determines which format elements must have a digit
// immediately following the portion of the input text that they match.

// Format elements that satisfy this property match exactly the number of digits
// indicated in the format element; those that don't allow mapping against a
// smaller number of digits.

// For example, "YYY", standalone, matches 1-3 digits, but in the context of
// "YYYMM", "YYY" must match exactly 3 digits.
absl::StatusOr<std::vector<bool>> ComputeElementPrecedesDigits(
    std::vector<DateTimeFormatElement> format_elements) {
  std::vector<bool> element_precedes_digits;
  element_precedes_digits.resize(format_elements.size(), false);
  for (int i = static_cast<int>(element_precedes_digits.size()) - 2; i >= 0;
       --i) {
    const DateTimeFormatElement& next_format_element = format_elements[i + 1];
    switch (next_format_element.type) {
      case FormatElementType::kDoubleQuotedLiteral:
        if (next_format_element.literal_value.empty()) {
          // The next element is of "kDoubleQuotedLiteral" type with empty
          // <literal_value> and consumes no characters from the input, so the
          // <element_precedes_digit> of the current element is the same as
          // that of the next element.
          element_precedes_digits[i] = element_precedes_digits[i + 1];
        } else if (absl::ascii_isdigit(
                       next_format_element.literal_value.at(0))) {
          element_precedes_digits[i] = true;
        }
        break;
      case FormatElementType::kSimpleLiteral:
      case FormatElementType::kWhitespace:
      case FormatElementType::kMON:
      case FormatElementType::kMONTH:
      case FormatElementType::kAMWithDots:
      case FormatElementType::kPMWithDots:
      case FormatElementType::kTZH:
        element_precedes_digits[i] = false;
        break;
      case FormatElementType::kYYYY:
      case FormatElementType::kYYY:
      case FormatElementType::kYY:
      case FormatElementType::kY:
      case FormatElementType::kRRRR:
      case FormatElementType::kRR:
      case FormatElementType::kYCommaYYY:
      case FormatElementType::kMM:
      case FormatElementType::kDD:
      case FormatElementType::kHH:
      case FormatElementType::kHH12:
      case FormatElementType::kHH24:
      case FormatElementType::kMI:
      case FormatElementType::kSS:
      case FormatElementType::kSSSSS:
      case FormatElementType::kFFN:
      case FormatElementType::kTZM:
        element_precedes_digits[i] = true;
        break;
      default:
        GOOGLESQL_RET_CHECK_FAIL() << "Unexpected FormatElementType: "
                         << FormatElementTypeString(next_format_element.type);
    }
  }
  return element_precedes_digits;
}

// This struct specifies the range of number of digits that an element can
// parse.
struct DigitCountRange {
  int min = 0;
  int max = 0;
};

// Returns a vector of DigitCountRange objects <digit_count_ranges> where
// <digit_count_ranges[i]> indicates the range of number of digits to parse for
// <format_elements[i]>. For elements that do not parse digits (e.g. "-") or
// parse more than one digit blocks (e.g. "Y,YYY"), we set both <min> and
// <max> to be 0, and do not use them in parsing process. Returns an error if
// any format element inside <format_elements> is not supported for parsing.
absl::StatusOr<std::vector<DigitCountRange>> ComputeDigitCountRanges(
    const std::vector<DateTimeFormatElement>& format_elements) {
  std::vector<DigitCountRange> digit_count_ranges;
  digit_count_ranges.resize(format_elements.size());
  GOOGLESQL_ASSIGN_OR_RETURN(const std::vector<bool> element_precedes_digits,
                   ComputeElementPrecedesDigits(format_elements));
  GOOGLESQL_RET_CHECK(digit_count_ranges.size() == element_precedes_digits.size());

  for (size_t i = 0; i < digit_count_ranges.size(); ++i) {
    bool element_precedes_digit = element_precedes_digits[i];
    int element_length = format_elements[i].len_in_format_str;
    switch (format_elements[i].type) {
      case FormatElementType::kSimpleLiteral:
      case FormatElementType::kWhitespace:
      case FormatElementType::kDoubleQuotedLiteral:
      case FormatElementType::kYCommaYYY:
      case FormatElementType::kMON:
      case FormatElementType::kMONTH:
      case FormatElementType::kAMWithDots:
      case FormatElementType::kPMWithDots:
        digit_count_ranges[i] = {.min = 0, .max = 0};
        break;
      // Format elements “RRRR” and “YYYY” parse exactly 4 digits if
      // <element_precedes_digit> is true and can parse up to 5 digits
      // otherwise.
      case FormatElementType::kYYYY:
      case FormatElementType::kRRRR:
        if (element_precedes_digit) {
          digit_count_ranges[i] = {.min = 4, .max = 4};
        } else {
          // "RRRR"/"YYYY" can parse up to 5 digits when
          // <element_precedes_digit> is false.
          digit_count_ranges[i] = {.min = 1, .max = 5};
        }
        break;
      // The elements below parse exactly <element_length> digits if
      // <element_precedes_digit> is true; otherwise, the number of digits
      // they can parse varies between 1 and <element_length>.
      case FormatElementType::kYYY:
      case FormatElementType::kYY:
      case FormatElementType::kY:
      case FormatElementType::kRR:
      case FormatElementType::kMM:
      case FormatElementType::kDD:
      case FormatElementType::kHH:
      case FormatElementType::kMI:
      case FormatElementType::kSS:
      case FormatElementType::kSSSSS:
        if (element_precedes_digit) {
          digit_count_ranges[i] = {.min = element_length,
                                   .max = element_length};
        } else {
          digit_count_ranges[i] = {.min = 1, .max = element_length};
        }
        break;
      case FormatElementType::kHH12:
      case FormatElementType::kHH24:
      case FormatElementType::kTZH:
      case FormatElementType::kTZM:
        if (element_precedes_digit) {
          digit_count_ranges[i] = {.min = 2, .max = 2};
        } else {
          digit_count_ranges[i] = {.min = 1, .max = 2};
        }
        break;
      case FormatElementType::kFFN:
        if (element_precedes_digit) {
          digit_count_ranges[i] = {
              .min = format_elements[i].subsecond_digit_count,
              .max = format_elements[i].subsecond_digit_count};
        } else {
          digit_count_ranges[i] = {
              .min = 1, .max = format_elements[i].subsecond_digit_count};
        }
        break;
      default:
        GOOGLESQL_RET_CHECK_FAIL() << "Unexpected FormatElementType: "
                         << FormatElementTypeString(format_elements[i].type);
    }
  }

  return digit_count_ranges;
}

// Parses <timestamp_string> with a format element of "kRR" type and
// produces <year> value as output. Returns the number of consumed
// characters upon successful parsing, and returns absl::string_view::npos
// otherwise.
size_t ParseWithFormatElementOfTypeRR(absl::string_view timestamp_string,
                                      int current_year,
                                      DigitCountRange digit_count_range,
                                      int* year) {
  int current_year_last_two_digits = current_year % 100;
  int current_year_before_last_two_digits = current_year / 100;
  int year_before_last_two_digits = current_year_before_last_two_digits;
  int year_last_two_digits;
  size_t parsed_length =
      ParseInt(timestamp_string, /*min_width=*/digit_count_range.min,
               /*max_width=*/digit_count_range.max, /*min=*/0,
               /*max=*/99, &year_last_two_digits);

  if (parsed_length != absl::string_view::npos) {
    if (year_last_two_digits < 50 && current_year_last_two_digits >= 50) {
      year_before_last_two_digits += 1;
    } else if (year_last_two_digits >= 50 &&
               current_year_last_two_digits < 50) {
      year_before_last_two_digits -= 1;
    }
    *year = year_before_last_two_digits * 100 + year_last_two_digits;
  }
  return parsed_length;
}

// Parses <timestamp_string> with a format element of "kYCommaYYY" type and
// produces <year> value as output. Returns the number of consumed
// characters upon successful parsing, and returns absl::string_view::npos
// otherwise.
size_t ParseWithFormatElementOfTypeYCommaYYY(absl::string_view timestamp_string,
                                             int* year) {
  // The number of charcters in <timestamp_string> that has been parsed.
  size_t parsed_length = 0;
  size_t parsed_length_temp = absl::string_view::npos;
  int year_first_part;
  int year_last_three_digits;

  absl::string_view timestamp_str_to_parse = timestamp_string;
  // Parses "Y" part of "Y,YYY".
  parsed_length_temp = ParseInt(timestamp_str_to_parse, /*min_width=*/1,
                                /*max_width=*/2, /*min=*/0,
                                /*max=*/10, &year_first_part);
  if (parsed_length_temp == absl::string_view::npos) {
    return absl::string_view::npos;
  }
  parsed_length = parsed_length_temp;
  timestamp_str_to_parse = timestamp_str_to_parse.substr(parsed_length_temp);

  // Parses "," part of "Y,YYY".
  parsed_length_temp = ParseStringByExactMatch(timestamp_str_to_parse, ",");
  if (parsed_length_temp == absl::string_view::npos) {
    return absl::string_view::npos;
  }
  parsed_length += parsed_length_temp;
  timestamp_str_to_parse = timestamp_str_to_parse.substr(parsed_length_temp);

  // Parses "YYY" part of "Y,YYY".
  parsed_length_temp = ParseInt(timestamp_str_to_parse, /*min_width=*/3,
                                /*max_width=*/3, /*min=*/0,
                                /*max=*/999, &year_last_three_digits);
  if (parsed_length_temp == absl::string_view::npos) {
    return absl::string_view::npos;
  }
  *year = year_first_part * 1000 + year_last_three_digits;
  parsed_length += parsed_length_temp;
  return parsed_length;
}

// Parses <timestamp_string> with a format element of "kTZH" type and produces
// <positive_timezone_offset> and <timezone_offset_hour>. Returns the number of
// consumed characters upon successful parsing, and returns
// absl::string_view::npos otherwise.
size_t ParseWithFormatElementOfTypeTZH(absl::string_view timestamp_string,
                                       DigitCountRange digit_count_range,
                                       bool* positive_timezone_offset,
                                       int* timezone_offset_hour) {
  size_t parsed_length = 0;
  absl::string_view timestamp_str_to_parse = timestamp_string;
  ParseWithCandidatesResult parse_result = ParseStringWithCandidates(
      timestamp_str_to_parse, {"-", "+", " "}, /*ignore_case=*/false);
  if (parse_result.parsed_length == absl::string_view::npos) {
    return absl::string_view::npos;
  }
  *positive_timezone_offset = (parse_result.matched_candidate_index != 0);

  parsed_length += parse_result.parsed_length;
  timestamp_str_to_parse =
      timestamp_str_to_parse.substr(parse_result.parsed_length);
  size_t parsed_length_temp =
      ParseInt(timestamp_str_to_parse,
               /*min_width=*/digit_count_range.min,
               /*max_width=*/digit_count_range.max, /*min=*/0,
               /*max=*/14, timezone_offset_hour);
  if (parsed_length_temp == absl::string_view::npos) {
    return absl::string_view::npos;
  }

  parsed_length += parsed_length_temp;
  return parsed_length;
}

// This function conducts the parsing for <timestamp_string> with
// <format_elements>. Upon return, <timestamp> constains the timestamp value
// with nanosecond precision, and sub-nanoseconds value is returned by
// <sub_nanoseconds>.
absl::Status ParseTimeWithFormatElements(
    const std::vector<DateTimeFormatElement>& format_elements,
    absl::string_view timestamp_string, const absl::TimeZone default_timezone,
    const absl::Time current_timestamp, TimestampScale scale,
    absl::Time* timestamp, uint32_t* sub_nanoseconds) {
  // The number of format elements from <format_elements> that have been
  // successfully processed so far.
  size_t processed_format_element_count = 0;
  // The number of characters of <timestamp_string> that have been successfully
  // parsed so far.
  size_t timestamp_str_parsed_length = 0;

  absl::TimeZone::CivilInfo now_info = default_timezone.At(current_timestamp);
  absl::CivilSecond cs_now = now_info.cs;

  int year = static_cast<int>(cs_now.year());
  int month = cs_now.month();
  int mday = 1;
  int hour = 0;
  int min = 0;
  int sec = 0;
  int hour_in_12_hour_clock = 0;
  bool afternoon = false;
  absl::Duration subseconds = absl::ZeroDuration();
  *sub_nanoseconds = 0;

  // Indicates whether TZH or TZM appears in the format string.
  bool timezone_specified_in_format = false;

  bool positive_timezone_offset = true;
  int timezone_offset_hour = 0;
  int timezone_offset_min = 0;

  bool error_in_parsing = false;
  GOOGLESQL_ASSIGN_OR_RETURN(const std::vector<DigitCountRange> digit_count_ranges,
                   ComputeDigitCountRanges(format_elements));

  // Skips leading whitespaces.
  timestamp_str_parsed_length +=
      TrimLeadingUnicodeWhiteSpaces(timestamp_string);
  while (!error_in_parsing &&
         timestamp_str_parsed_length < timestamp_string.size() &&
         processed_format_element_count < format_elements.size()) {
    size_t parsed_length = absl::string_view::npos;
    absl::string_view timestamp_str_to_parse =
        timestamp_string.substr(timestamp_str_parsed_length);
    const DateTimeFormatElement& format_element =
        format_elements[processed_format_element_count];
    DigitCountRange digit_count_range =
        digit_count_ranges[processed_format_element_count];

    switch (format_element.type) {
      case FormatElementType::kSimpleLiteral:
      case FormatElementType::kDoubleQuotedLiteral:
        parsed_length = ParseStringByExactMatch(timestamp_str_to_parse,
                                                format_element.literal_value);
        break;
      case FormatElementType::kWhitespace:
        // Format element of "kWhitespace" type matches 1 or more Unicode
        // whitespaces.
        parsed_length = TrimLeadingUnicodeWhiteSpaces(timestamp_str_to_parse);
        if (parsed_length == 0) {
          // Matches 0 Unicode whitespace, so we set <error_in_parsing> to true
          // to indicate an error.
          error_in_parsing = true;
        }
        break;
      // Parses for entire year value. For example, for input string "1234", the
      // output <year> is 1234.
      case FormatElementType::kYYYY:
      case FormatElementType::kRRRR:
        parsed_length = ParseInt(timestamp_str_to_parse,
                                 /*min_width=*/digit_count_range.min,
                                 /*max_width=*/digit_count_range.max,
                                 /*min=*/0, /*max=*/10000, &year);
        break;
      // Parses for the last 3/2/1 digits of the year value depending on the
      // length of the element. For example, assuming <current_year> is 1970:
      //   - for input "123", the output <year> with "YYY" is 1123,
      //   - for input "12", the output <year> with "YY" is 1912,
      //   - for input "1", the output <year> with "Y" is 1971.
      case FormatElementType::kYYY:
      case FormatElementType::kYY:
      case FormatElementType::kY: {
        int element_length = format_element.len_in_format_str;
        GOOGLESQL_RET_CHECK(element_length >= 0 &&
                  element_length < ABSL_ARRAYSIZE(powers_of_ten));
        int element_length_power_of_ten =
            static_cast<int>(powers_of_ten[element_length]);
        int parsed_year_part;
        parsed_length = ParseInt(
            timestamp_str_to_parse, /*min_width=*/digit_count_range.min,
            /*max_width=*/digit_count_range.max, /*min=*/0,
            /*max=*/element_length_power_of_ten - 1, &parsed_year_part);
        if (parsed_length != absl::string_view::npos) {
          year = year - year % element_length_power_of_ten + parsed_year_part;
        }
        break;
      }
      // Parses for the last 2 digit of the year value. The first 2 digits
      // of the output can be different from that of current year (more
      // details at (broken link)).
      // For example, if the current year is 2002:
      //   - for input "12", the output <year>  is 2012,
      //   - for input "51", the output <year>  is 1951.
      // If the current year is 2299,
      //   - for input "12", the output <year> is 2312,
      //   - for input "51", thr output <year> is 2251.
      case FormatElementType::kRR: {
        parsed_length = ParseWithFormatElementOfTypeRR(
            timestamp_str_to_parse,
            /*current_year=*/year, digit_count_range, &year);
        break;
      }
      // Parses for entire year value with a string in pattern "X,XXX" or
      // "XX,XXX". For example,
      //   - for input "1,234", the output <year> is 1234,
      //   - for input "10,000", the output <year> is 10000.
      case FormatElementType::kYCommaYYY:
        parsed_length = ParseWithFormatElementOfTypeYCommaYYY(
            timestamp_str_to_parse, &year);
        break;
      // Parses for month value 1-12. For example, for input "11", the output
      // <month> is 11.
      case FormatElementType::kMM:
        parsed_length = ParseInt(timestamp_str_to_parse,
                                 /*min_width=*/digit_count_range.min,
                                 /*max_width=*/digit_count_range.max, /*min=*/1,
                                 /*max=*/12, &month);
        break;
      // Parses abbreviated month names with "MON" element and full month
      // names with "MONTH" element. The parsing is case-insensitive.
      // For example,
      //   - for input "Jan"/"jAN", the output <month> with "MON" is 1,
      //   - for input "JUNE"/"juNe", the output <month> with "MONTH" is 6.
      case FormatElementType::kMON:
      case FormatElementType::kMONTH:
        parsed_length = ParseMonthNames(
            timestamp_str_to_parse,
            /*abbreviated=*/format_element.type == FormatElementType::kMON,
            &month);
        break;
      // Parses for day of month value. For example, for input "20", the
      // output <mday> is 20.
      case FormatElementType::kDD:
        parsed_length = ParseInt(timestamp_str_to_parse,
                                 /*min_width=*/digit_count_range.min,
                                 /*max_width=*/digit_count_range.max, /*min=*/1,
                                 /*max=*/31, &mday);
        break;
      // kHH/kHH12 and kAMWithDots/kPMWithDots are used to parse hour value
      // of a 12-hour clock. The matching for meridian indicator part is
      // case-insensitive. For example,
      //   - if input for kHH/kHH12 is "11" and input for
      //     kAMWithDots/kPMWithDots is "A.M."/"A.m.", the output <hour> is 11.
      //   - if input for kHH/kHH12 is "12" and input for
      //     kAMWithDots/kPMWithDots is "a.M."/"a.m.", the output <hour> is 0.
      // string "11", the hour value in the result 12-hour clock is 11.
      case FormatElementType::kHH:
      case FormatElementType::kHH12:
        parsed_length = ParseInt(timestamp_str_to_parse,
                                 /*min_width=*/digit_count_range.min,
                                 /*max_width=*/digit_count_range.max, /*min=*/1,
                                 /*max=*/12, &hour_in_12_hour_clock);
        break;
      case FormatElementType::kAMWithDots:
      case FormatElementType::kPMWithDots: {
        ParseWithCandidatesResult parse_result = ParseStringWithCandidates(
            timestamp_str_to_parse, {"A.M.", "P.M."}, /*ignore_case=*/true);
        parsed_length = parse_result.parsed_length;
        if (parsed_length != absl::string_view::npos) {
          afternoon = (parse_result.matched_candidate_index == 1);
        }
        break;
      }
      // Parses for hour value in a 24-hour clock. For example, for input "12",
      // the output <hour> is 12.
      case FormatElementType::kHH24:
        parsed_length = ParseInt(timestamp_str_to_parse,
                                 /*min_width=*/digit_count_range.min,
                                 /*max_width=*/digit_count_range.max, /*min=*/0,
                                 /*max=*/23, &hour);
        break;
      // Parses for minute value 0-59. For example, for input "20", the output
      // <min> is 20.
      case FormatElementType::kMI:
        parsed_length = ParseInt(timestamp_str_to_parse,
                                 /*min_width=*/digit_count_range.min,
                                 /*max_width=*/digit_count_range.max, /*min=*/0,
                                 /*max=*/59, &min);
        break;
      // Parses for second value 0-59. For example, for input "30", the output
      // <sec> is 30.
      case FormatElementType::kSS:
        parsed_length = ParseInt(timestamp_str_to_parse,
                                 /*min_width=*/digit_count_range.min,
                                 /*max_width=*/digit_count_range.max, /*min=*/0,
                                 /*max=*/59, &sec);
        break;
      // Parses for number of seconds past midnight 0 ~ 2400*60*60-1. For
      // example, for input "3662", the output <hour>, <min> and <sec> are 1, 1,
      // 2 respectively (since 3660 seconds past midnight corresponds to time
      // "01:01:02").
      case FormatElementType::kSSSSS: {
        int sec_of_day;
        parsed_length =
            ParseInt(timestamp_str_to_parse,
                     /*min_width=*/digit_count_range.min,
                     /*max_width=*/digit_count_range.max, /*min=*/0,
                     /*max=*/kNaiveNumSecondsPerDay - 1, &sec_of_day);
        if (parsed_length != absl::string_view::npos) {
          hour = sec_of_day / kNaiveNumSecondsPerHour;
          min = (sec_of_day % kNaiveNumSecondsPerHour) /
                kNaiveNumSecondsPerMinute;
          sec = sec_of_day % kNaiveNumSecondsPerMinute;
        }
        break;
      }
      // Parses for subsecond and sub_nanosecond values. For example, for input
      // "123456789321", scale kPicoseconds, the subsecond
      // will be 123456789 nanoseconds, and sub_nanosecond will be 321.
      //
      // Additional digits beyond the input <scale> are truncated (6 for micros,
      // 9 for nanos). For example, for input "1234567" with format "FF7", the
      // output subsecond is 123456 microseconds under micros scale, or
      // 123456700 nanoseconds under nano scale.
      case FormatElementType::kFFN: {
        GOOGLESQL_RET_CHECK(format_element.subsecond_digit_count > 0 &&
                  format_element.subsecond_digit_count <= 12);
        parsed_length = ParseSubSeconds(timestamp_str_to_parse,
                                        /*min_width=*/digit_count_range.min,
                                        /*max_width=*/digit_count_range.max,
                                        scale, &subseconds, sub_nanoseconds);
        break;
      }
      // Parses for the sign and hour value of the time zone offset. For
      // example,
      //   - for input "+10"/" 10", the sign and hour value of output time zone
      //     are "+10".
      //   - for input "-09", the sign and hour value of output time zone are
      //     "-09".
      case FormatElementType::kTZH: {
        timezone_specified_in_format = true;
        parsed_length = ParseWithFormatElementOfTypeTZH(
            timestamp_str_to_parse, digit_count_range,
            &positive_timezone_offset, &timezone_offset_hour);
        break;
      }
      // Parses for the minute value of the time zone offset. For example, for
      // input "13", the minute value of output time zone is 13.
      case FormatElementType::kTZM:
        timezone_specified_in_format = true;
        parsed_length = ParseInt(timestamp_str_to_parse,
                                 /*min_width=*/digit_count_range.min,
                                 /*max_width=*/digit_count_range.max, /*min=*/0,
                                 /*max=*/59, &timezone_offset_min);
        break;
      default:
        break;
    }

    if (parsed_length == absl::string_view::npos) {
      // If <parsed_length> is absl::string_view::npos, we set
      // <error_in_parsing> to be true to indicate an error.
      error_in_parsing = true;
    }

    if (!error_in_parsing) {
      // We successfully processed a format element, so update the number of
      // elements and characters processed.
      processed_format_element_count++;
      timestamp_str_parsed_length += parsed_length;
    }
  }

  if (error_in_parsing) {
    return MakeEvalError()
           << "Failed to parse input timestamp string at "
           << timestamp_str_parsed_length << " with format element "
           << format_elements[processed_format_element_count].ToString();
  }

  // Skips any remaining whitespace.
  timestamp_str_parsed_length += TrimLeadingUnicodeWhiteSpaces(
      timestamp_string.substr(timestamp_str_parsed_length));

  // Skips trailing empty format elements {kDoubleQuotedLiteral, ""} which match
  // "" in input string.
  while (
      processed_format_element_count < format_elements.size() &&
      format_elements[processed_format_element_count].type ==
          FormatElementType::kDoubleQuotedLiteral &&
      format_elements[processed_format_element_count].literal_value.empty()) {
    processed_format_element_count++;
  }

  if (timestamp_str_parsed_length < timestamp_string.size()) {
    return MakeEvalError() << "Illegal non-space trailing data '"
                           << timestamp_string.substr(
                                  timestamp_str_parsed_length)
                           << "' in timestamp string";
  }

  if (processed_format_element_count < format_elements.size()) {
    return MakeEvalError()
           << "Entire timestamp string has been parsed before dealing with"
           << " format element "
           << format_elements[processed_format_element_count].ToString();
  }

  // Calculates the <hour> in 24-hour clock if hour value of a 12-hour clock is
  // parsed.
  if (hour_in_12_hour_clock != 0) {
    hour = hour_in_12_hour_clock % 12 + (afternoon ? 12 : 0);
  }

  const absl::CivilSecond cs(year, month, mday, hour, min, sec);
  // absl::CivilSecond will 'normalize' its arguments, so we simply compare
  // the input against the result to check whether a YMD is valid.
  if (cs.year() != year || cs.month() != month || cs.day() != mday) {
    return MakeEvalError()
           << "Invalid result from year, month, day values after parsing";
  }

  absl::TimeZone timezone;
  if (timezone_specified_in_format) {
    GOOGLESQL_RETURN_IF_ERROR(MakeTimeZone(
        absl::StrFormat("%c%02d%02d", positive_timezone_offset ? '+' : '-',
                        timezone_offset_hour, timezone_offset_min),
        &timezone));
  } else {
    timezone = default_timezone;
  }
  *timestamp = timezone.At(cs).pre + subseconds;
  if (!IsValidTime(*timestamp)) {
    return MakeEvalError() << "The parsing result is out of valid time range";
  }
  return absl::OkStatus();
}

// Returns an error if more than one format element in the target category exist
// in the format string, i.e. the value of <category> in
// <category_to_elements_map> contains more than one item. For example, you
// cannot have elements "YY" and "RRRR" at the same time since they are both
// in "kYear" category.
absl::Status CheckForDuplicateElementsInCategory(
    FormatElementCategory category,
    const CategoryToElementsMap& category_to_elements_map) {
  if (category_to_elements_map.contains(category) &&
      category_to_elements_map.at(category).size() > 1) {
    return MakeEvalError()
           << "More than one format element in category "
           << FormatElementCategoryString(category)
           << " exist: " << category_to_elements_map.at(category)[0]->ToString()
           << " and " << category_to_elements_map.at(category)[1]->ToString();
  }
  return absl::OkStatus();
}

// Returns an error if the element in the target category exists in the format
// string, i.e. <category> exists in <category_to_elements_map> as a key. For
// example, you cannot have any format element in "kHour" category if the output
// type is DATE.
absl::Status CheckCategoryNotExist(
    FormatElementCategory category,
    const CategoryToElementsMap& category_to_elements_map,
    absl::string_view output_type_name) {
  if (category_to_elements_map.contains(category)) {
    std::string error_reason = absl::Substitute(
        "Format element in category $0 ($1) is not allowed for output type $2",
        FormatElementCategoryString(category),
        category_to_elements_map.at(category)[0]->ToString(), output_type_name);
    return MakeEvalError() << error_reason;
  }
  return absl::OkStatus();
}

// Returns an error if <type> is present in <type_to_element_map> and <category>
// is present in <category_to_elements_map>. For example, if you have a format
// element of "kHH24" type, you cannot have any format element in
// "kMeridianIndicator" category.
absl::Status CheckForMutuallyExclusiveElements(
    FormatElementType type, FormatElementCategory category,
    const TypeToElementMap& type_to_element_map,
    const CategoryToElementsMap& category_to_elements_map) {
  if (type_to_element_map.contains(type) &&
      category_to_elements_map.contains(category)) {
    std::string error_reason = absl::Substitute(
        "Format element in category $0 ($1) and format element $2 cannot exist "
        "simultaneously",
        FormatElementCategoryString(category),
        category_to_elements_map.at(category)[0]->ToString(),
        type_to_element_map.at(type)->ToString());
    return MakeEvalError() << error_reason;
  }
  return absl::OkStatus();
}

// Returns an error if both <type1> and <type2> are present in
// <type_to_element_map>. For example, if you have a format element of "kSSSSS"
// type which indicates seconds in a day, then you cannot have another element
// of "kSS" type to indicate seconds in an hour.
absl::Status CheckForMutuallyExclusiveElements(
    FormatElementType type1, FormatElementType type2,
    const TypeToElementMap& type_to_element_map) {
  if (type_to_element_map.contains(type1) &&
      type_to_element_map.contains(type2)) {
    return MakeEvalError() << "Format elements "
                           << type_to_element_map.at(type1)->ToString()
                           << " and "
                           << type_to_element_map.at(type2)->ToString()
                           << " cannot exist simultaneously";
  }
  return absl::OkStatus();
}

// Confirms that a format element in <category> is present if a format element
// of any type from <types> exists and vice versa. For example, you must have a
// format element in "kMeridianIndicator" category if a format element of "kHH"
// or "kHH12" type is used. Also, if you have a format element in
// "kMeridianIndicator" category, you must have a format element of "kHH" or
// "kHH12" type.
absl::Status CheckForCoexistance(
    std::vector<FormatElementType> types, FormatElementCategory category,
    const TypeToElementMap& type_to_element_map,
    const CategoryToElementsMap& category_to_elements_map) {
  FormatElementType present_type;
  bool type_exists = false;
  for (const FormatElementType& type : types) {
    if (type_to_element_map.contains(type)) {
      type_exists = true;
      present_type = type;
      break;
    }
  }

  if (type_exists && !category_to_elements_map.contains(category)) {
    return MakeEvalError() << "Format element in category "
                           << FormatElementCategoryString(category)
                           << " is required when format element "
                           << type_to_element_map.at(present_type)->ToString()
                           << " exists";
  } else if (category_to_elements_map.contains(category) && !type_exists) {
    std::vector<std::string> format_element_type_strs;
    format_element_type_strs.reserve(types.size());
    for (const FormatElementType& type : types) {
      format_element_type_strs.push_back(FormatElementTypeString(type));
    }

    std::string joined_format_element_type_strs =
        absl::StrJoin(format_element_type_strs, "/");
    std::string error_reason = absl::Substitute(
        "Format element of type $0 is required when format element in "
        "category $1 ($2) exists",
        joined_format_element_type_strs, FormatElementCategoryString(category),
        category_to_elements_map.at(category)[0]->ToString());
    return MakeEvalError() << error_reason;
  }
  return absl::OkStatus();
}

// Conducts basic and common verifications with the format string.
absl::Status ConductBasicFormatStringChecks(absl::string_view format_string) {
  if (!IsWellFormedUTF8(format_string)) {
    return MakeEvalError() << "Format string is not a valid UTF-8 string: "
                           << absl::CEscape(format_string);
  }

  if (format_string.size() >
      absl::GetFlag(FLAGS_googlesql_cast_format_string_max_width)) {
    return MakeEvalError() << "Format string too long; limit "
                           << absl::GetFlag(
                                  FLAGS_googlesql_cast_format_string_max_width);
  }
  return absl::OkStatus();
}

// Validates the elements in <format_elements> with specific rules, and also
// makes sure they are not of any category in <invalid_categories>.
absl::Status ValidateDateTimeFormatElements(
    absl::Span<const DateTimeFormatElement> format_elements,
    absl::Span<const FormatElementCategory> invalid_categories,
    absl::string_view output_type_name) {
  CategoryToElementsMap category_to_elements_map;
  TypeToElementMap type_to_element_map;

  for (const DateTimeFormatElement& format_element : format_elements) {
    if (!IsSupportedForParsing(format_element)) {
      return MakeEvalError() << "Format element " << format_element.ToString()
                             << " is not supported for parsing";
    }

    // We store at most 2 elements inside this map, since this is enough to
    // print in error message when duplicate checks fail for a category.
    std::vector<const DateTimeFormatElement*>& elements =
        category_to_elements_map[format_element.category];
    if (elements.size() < 2) {
      elements.push_back(&format_element);
    }

    if (type_to_element_map.contains(format_element.type)) {
      // We do not allow that more than one non-literal format element of the
      // same type exist at the same time. For example, the format string
      // "MiYYmI" is invalid since two format elements of "kMI" type
      // (appearing as "Mi" and "MI") exist in it.
      if (format_element.category != FormatElementCategory::kLiteral) {
        return MakeEvalError() << absl::Substitute(
                   "Format element $0 appears more than once in the "
                   "format string",
                   format_element.ToString());
      }
    } else {
      type_to_element_map[format_element.type] = &format_element;
    }
  }

  // Checks invalid format element categories for the output type.
  for (const FormatElementCategory& invalid_category : invalid_categories) {
    GOOGLESQL_RETURN_IF_ERROR(CheckCategoryNotExist(
        invalid_category, category_to_elements_map, output_type_name));
  }

  // Checks categories which do not allow duplications.
  const std::vector<FormatElementCategory> categories_to_check_duplicate = {
      FormatElementCategory::kMeridianIndicator,
      FormatElementCategory::kYear,
      FormatElementCategory::kMonth,
      FormatElementCategory::kDay,
      FormatElementCategory::kHour,
      FormatElementCategory::kMinute};

  for (FormatElementCategory category : categories_to_check_duplicate) {
    GOOGLESQL_RETURN_IF_ERROR(CheckForDuplicateElementsInCategory(
        category, category_to_elements_map));
  }

  // Checks mutually exclusive format elements/types.
  // The Check between "kHH24" type and "kHH"/"kHH12" types is included in
  // duplicate check for "kHour" category.
  GOOGLESQL_RETURN_IF_ERROR(CheckForMutuallyExclusiveElements(
      FormatElementType::kHH24, FormatElementCategory::kMeridianIndicator,
      type_to_element_map, category_to_elements_map));
  // A Format element in "kMeridianIndicator" category must exist when a format
  // element of "kHH" or "kHH12" is present. Also, if we have a format element
  // in "kMeridianIndicator" category, a format element of "kHH" or "kHH12" type
  // must exist.
  GOOGLESQL_RETURN_IF_ERROR(
      CheckForCoexistance({FormatElementType::kHH, FormatElementType::kHH12},
                          FormatElementCategory::kMeridianIndicator,
                          type_to_element_map, category_to_elements_map));

  // Format elements of "kSSSSS" type contain Hour, Minute and Second info,
  // therefore elements in "kHour" (along with "kMeridianIndicator") and
  // "kMinute" categories and elements of "kSS" type are disallowed.
  GOOGLESQL_RETURN_IF_ERROR(CheckForMutuallyExclusiveElements(
      FormatElementType::kSSSSS, FormatElementCategory::kHour,
      type_to_element_map, category_to_elements_map));
  GOOGLESQL_RETURN_IF_ERROR(CheckForMutuallyExclusiveElements(
      FormatElementType::kSSSSS, FormatElementCategory::kMinute,
      type_to_element_map, category_to_elements_map));
  GOOGLESQL_RETURN_IF_ERROR(CheckForMutuallyExclusiveElements(
      FormatElementType::kSSSSS, FormatElementType::kSS, type_to_element_map));
  return absl::OkStatus();
}

// The result <timestamp> is always at microseconds precision.
absl::Status ParseTimeWithFormatElements(
    const std::vector<DateTimeFormatElement>& format_elements,
    absl::string_view timestamp_string, const absl::TimeZone default_timezone,
    const absl::Time current_timestamp, int64_t* timestamp_micros) {
  absl::Time base_time;
  uint32_t unused;
  GOOGLESQL_RETURN_IF_ERROR(ParseTimeWithFormatElements(
      format_elements, timestamp_string, default_timezone, current_timestamp,
      kMicroseconds, &base_time, &unused));

  if (!ConvertTimeToTimestamp(base_time, timestamp_micros)) {
    return MakeEvalError() << "Invalid result from parsing function";
  }
  return absl::OkStatus();
}

// Validate format elements "FF<#>": FF10 - FF12 are only valid for kPicoseconds
// scale.
absl::Status ValidatekFFNFormatElements(
    absl::Span<const DateTimeFormatElement> format_elements,
    TimestampScale scale) {
  if (scale != kPicoseconds) {
    for (const DateTimeFormatElement& format_element : format_elements) {
      if (format_element.type == FormatElementType::kFFN &&
          format_element.subsecond_digit_count > 9) {
        return MakeEvalError() << "Format element " << format_element.ToString()
                               << " is not supported";
      }
    }
  }

  return absl::OkStatus();
}

absl::Status ValidateDateTimeFormatElementsForTimestampType(
    absl::Span<const DateTimeFormatElement> format_elements,
    TimestampScale scale = kMicroseconds) {
  GOOGLESQL_RETURN_IF_ERROR(ValidatekFFNFormatElements(format_elements, scale));
  return ValidateDateTimeFormatElements(format_elements, {}, "TIMESTAMP");
}

absl::Status ValidateDateTimeFormatElementsForDateType(
    absl::Span<const DateTimeFormatElement> format_elements) {
  return ValidateDateTimeFormatElements(
      format_elements,
      {FormatElementCategory::kHour, FormatElementCategory::kMinute,
       FormatElementCategory::kSecond,
       FormatElementCategory::kMeridianIndicator,
       FormatElementCategory::kTimeZone},
      "DATE");
}

absl::Status ValidateDateTimeFormatElementsForTimeType(
    absl::Span<const DateTimeFormatElement> format_elements,
    TimestampScale scale = kMicroseconds) {
  GOOGLESQL_RETURN_IF_ERROR(ValidatekFFNFormatElements(format_elements, scale));
  return ValidateDateTimeFormatElements(
      format_elements,
      {FormatElementCategory::kYear, FormatElementCategory::kMonth,
       FormatElementCategory::kDay, FormatElementCategory::kTimeZone,
       FormatElementCategory::kCentury, FormatElementCategory::kQuarter,
       FormatElementCategory::kWeek, FormatElementCategory::kEraIndicator},
      "TIME");
}

absl::Status ValidateDateTimeFormatElementsForDatetimeType(
    absl::Span<const DateTimeFormatElement> format_elements,
    TimestampScale scale = kMicroseconds) {
  GOOGLESQL_RETURN_IF_ERROR(ValidatekFFNFormatElements(format_elements, scale));
  return ValidateDateTimeFormatElements(
      format_elements, {FormatElementCategory::kTimeZone}, "DATETIME");
}

// Checks to see if the format elements are valid for the date or time type.
absl::Status ValidateDateDateTimeFormatElementsForFormatting(
    absl::Span<const DateTimeFormatElement> format_elements) {
  for (const DateTimeFormatElement& element : format_elements) {
    switch (element.category) {
      case FormatElementCategory::kLiteral:
      case FormatElementCategory::kYear:
      case FormatElementCategory::kMonth:
      case FormatElementCategory::kDay:
        continue;
      default:
        return MakeEvalError()
               << "DATE does not support " << element.ToString();
    }
  }
  return absl::OkStatus();
}

absl::Status ValidateTimeDateTimeFormatElementsForFormatting(
    absl::Span<const DateTimeFormatElement> format_elements) {
  for (const DateTimeFormatElement& element : format_elements) {
    switch (element.category) {
      case FormatElementCategory::kLiteral:
      case FormatElementCategory::kHour:
      case FormatElementCategory::kMinute:
      case FormatElementCategory::kSecond:
      case FormatElementCategory::kMeridianIndicator:
        continue;
      default:
        return MakeEvalError()
               << "TIME does not support " << element.ToString();
    }
  }
  return absl::OkStatus();
}

absl::Status ValidateDatetimeDateTimeFormatElementsForFormatting(
    absl::Span<const DateTimeFormatElement> format_elements) {
  for (const DateTimeFormatElement& element : format_elements) {
    switch (element.category) {
      case FormatElementCategory::kLiteral:
      case FormatElementCategory::kYear:
      case FormatElementCategory::kMonth:
      case FormatElementCategory::kDay:
      case FormatElementCategory::kHour:
      case FormatElementCategory::kMinute:
      case FormatElementCategory::kSecond:
      case FormatElementCategory::kMeridianIndicator:
        continue;
      default:
        return MakeEvalError()
               << "DATETIME does not support " << element.ToString();
    }
  }
  return absl::OkStatus();
}

}  // namespace

namespace cast_date_time_internal {

std::string DateTimeFormatElement::ToString() const {
  switch (type) {
    case FormatElementType::kSimpleLiteral:
      return absl::StrCat("\'", literal_value, "\'");
    case FormatElementType::kDoubleQuotedLiteral:
      return absl::StrCat(
          "\'", absl::Substitute("\"$0\"", absl::CEscape(literal_value)), "\'");
    case FormatElementType::kWhitespace: {
      std::string space_chars = "";
      for (int i = 0; i < len_in_format_str; ++i) {
        space_chars.push_back(' ');
      }
      return absl::StrCat("\'", space_chars, "\'");
    }
    case FormatElementType::kFFN:
      return absl::StrCat("\'", "FF", subsecond_digit_count, "\'");
    default:
      return absl::StrCat("\'", FormatElementTypeString(type), "\'");
  }
}

static const FormatElementType kFormatElementTypeNullValue =
    FormatElementType::kFormatElementTypeUnspecified;
using FormatElementTypeTrie =
    googlesql_base::GeneralTrie<FormatElementType, kFormatElementTypeNullValue>;

const FormatElementTypeTrie* InitializeFormatElementTypeTrie() {
  FormatElementTypeTrie* trie = new FormatElementTypeTrie();
  /*Simple Literals*/
  trie->Insert("-", FormatElementType::kSimpleLiteral);
  trie->Insert(".", FormatElementType::kSimpleLiteral);
  trie->Insert("/", FormatElementType::kSimpleLiteral);
  trie->Insert(",", FormatElementType::kSimpleLiteral);
  trie->Insert("'", FormatElementType::kSimpleLiteral);
  trie->Insert(";", FormatElementType::kSimpleLiteral);
  trie->Insert(":", FormatElementType::kSimpleLiteral);

  /*Double Quoted Literal*/
  // For the format element '\"xxxxx\"' (arbitrary text enclosed by ""), we
  // would match '\"' in the trie and then manually search the end of the
  // format element.
  trie->Insert("\"", FormatElementType::kDoubleQuotedLiteral);

  /*Whitespace*/
  // For the format element consisting of a sequence of consecutive ASCII space
  // characters (' '), we would match ' ' in the trie and then manually search
  // the end of the sequence.
  trie->Insert(" ", FormatElementType::kWhitespace);

  /*Year*/
  trie->Insert("YYYY", FormatElementType::kYYYY);
  trie->Insert("YYY", FormatElementType::kYYY);
  trie->Insert("YY", FormatElementType::kYY);
  trie->Insert("Y", FormatElementType::kY);
  trie->Insert("RRRR", FormatElementType::kRRRR);
  trie->Insert("RR", FormatElementType::kRR);
  trie->Insert("Y,YYY", FormatElementType::kYCommaYYY);
  trie->Insert("IYYY", FormatElementType::kIYYY);
  trie->Insert("IYY", FormatElementType::kIYY);
  trie->Insert("IY", FormatElementType::kIY);
  trie->Insert("I", FormatElementType::kI);
  trie->Insert("SYYYY", FormatElementType::kSYYYY);
  trie->Insert("YEAR", FormatElementType::kYEAR);
  trie->Insert("SYEAR", FormatElementType::kSYEAR);

  /*Month*/
  trie->Insert("MM", FormatElementType::kMM);
  trie->Insert("MON", FormatElementType::kMON);
  trie->Insert("MONTH", FormatElementType::kMONTH);
  trie->Insert("RM", FormatElementType::kRM);

  /*Day*/
  trie->Insert("DDD", FormatElementType::kDDD);
  trie->Insert("DD", FormatElementType::kDD);
  trie->Insert("D", FormatElementType::kD);
  trie->Insert("DAY", FormatElementType::kDAY);
  trie->Insert("DY", FormatElementType::kDY);
  trie->Insert("J", FormatElementType::kJ);

  /*Hour*/
  trie->Insert("HH", FormatElementType::kHH);
  trie->Insert("HH12", FormatElementType::kHH12);
  trie->Insert("HH24", FormatElementType::kHH24);

  /*Minute*/
  trie->Insert("MI", FormatElementType::kMI);

  /*Second*/
  trie->Insert("SS", FormatElementType::kSS);
  trie->Insert("SSSSS", FormatElementType::kSSSSS);
  trie->Insert("FF1", FormatElementType::kFFN);
  trie->Insert("FF2", FormatElementType::kFFN);
  trie->Insert("FF3", FormatElementType::kFFN);
  trie->Insert("FF4", FormatElementType::kFFN);
  trie->Insert("FF5", FormatElementType::kFFN);
  trie->Insert("FF6", FormatElementType::kFFN);
  trie->Insert("FF7", FormatElementType::kFFN);
  trie->Insert("FF8", FormatElementType::kFFN);
  trie->Insert("FF9", FormatElementType::kFFN);
  trie->Insert("FF10", FormatElementType::kFFN);
  trie->Insert("FF11", FormatElementType::kFFN);
  trie->Insert("FF12", FormatElementType::kFFN);

  /*Meridian indicator*/
  trie->Insert("AM", FormatElementType::kAM);
  trie->Insert("PM", FormatElementType::kPM);
  trie->Insert("A.M.", FormatElementType::kAMWithDots);
  trie->Insert("P.M.", FormatElementType::kPMWithDots);

  /*Time zone*/
  trie->Insert("TZH", FormatElementType::kTZH);
  trie->Insert("TZM", FormatElementType::kTZM);

  /*Century*/
  trie->Insert("CC", FormatElementType::kCC);
  trie->Insert("SCC", FormatElementType::kSCC);

  /*Quarter*/
  trie->Insert("Q", FormatElementType::kQ);

  /*Week*/
  trie->Insert("IW", FormatElementType::kIW);
  trie->Insert("WW", FormatElementType::kWW);
  trie->Insert("W", FormatElementType::kW);

  /*Era Indicator*/
  trie->Insert("AD", FormatElementType::kAD);
  trie->Insert("BC", FormatElementType::kBC);
  trie->Insert("A.D.", FormatElementType::kADWithDots);
  trie->Insert("B.C.", FormatElementType::kBCWithDots);

  /*Misc*/
  trie->Insert("SP", FormatElementType::kSP);
  trie->Insert("TH", FormatElementType::kTH);
  trie->Insert("SPTH", FormatElementType::kSPTH);
  trie->Insert("THSP", FormatElementType::kTHSP);
  trie->Insert("FM", FormatElementType::kFM);

  return trie;
}

const FormatElementTypeTrie& GetFormatElementTypeTrie() {
  static const FormatElementTypeTrie* format_element_type_trie =
      InitializeFormatElementTypeTrie();
  return *format_element_type_trie;
}

// Decides the <format_casing_type> field for a non-literal format element
// based on its original string and category.
absl::StatusOr<FormatCasingType> GetFormatCasingTypeOfNonLiteralElements(
    absl::string_view format_element_str, FormatElementCategory category) {
  GOOGLESQL_RET_CHECK(category != FormatElementCategory::kLiteral);
  GOOGLESQL_RET_CHECK(!format_element_str.empty() &&
            absl::ascii_isalpha(format_element_str[0]));
  // If the first letter of the element is lowercase, then all the letters in
  // the output are lowercase.
  if (absl::ascii_islower(format_element_str[0])) {
    return FormatCasingType::kAllLettersLowercase;
  }
  // If the elements are in "kMeridianIndicator" or "kEraIndicator" category,
  // or the length of format element string is 1, the first letter indicates the
  // overall casing. Besides "A.M."/"P.M."/"A.D."/"B.C." (that belong to
  // "kMeridianIndicator" or "kEraIndicator" categories), the only element
  // whose second character of the element string is not an alphabet is
  // "Y,YYY", since this element does not output letters, the choice of
  // FormatCasingType should make no difference to the formatting result.
  if (category == FormatElementCategory::kMeridianIndicator ||
      category == FormatElementCategory::kEraIndicator ||
      format_element_str.size() == 1 ||
      absl::AsciiStrToUpper(format_element_str) == "Y,YYY") {
    return FormatCasingType::kAllLettersUppercase;
  }

  GOOGLESQL_RET_CHECK(absl::ascii_isalpha(format_element_str[1]));

  // If the first letter is upper case and the second letter is lowercase, then
  // the first letter of each word in the output is capitalized and the other
  // letters are lowercase.
  if (absl::ascii_isupper(format_element_str[0]) &&
      absl::ascii_islower(format_element_str[1])) {
    return FormatCasingType::kOnlyFirstLetterUppercase;
  }

  // If the first two letters of the element are both upper case, the output is
  // capitalized.
  return FormatCasingType::kAllLettersUppercase;
}

// We need the upper <format_str> to do the search in prefix tree since matching
// are case-sensitive and we need the original <format_str> to extract the
// original_str for the format element object.
absl::StatusOr<DateTimeFormatElement> GetNextDateTimeFormatElement(
    absl::string_view format_str, absl::string_view upper_format_str) {
  DateTimeFormatElement format_element;
  int matched_len;
  const FormatElementTypeTrie& format_element_type_trie =
      GetFormatElementTypeTrie();
  const FormatElementType& type =
      format_element_type_trie.GetDataForMaximalPrefix(
          upper_format_str, matched_len, /*is_terminator=*/{});
  if (type == kFormatElementTypeNullValue) {
    return MakeEvalError() << "Cannot find matched format element";
  }

  format_element.type = type;
  format_element.category = GetFormatElementCategoryFromType(type);

  if (format_element.category != FormatElementCategory::kLiteral) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        format_element.format_casing_type,
        GetFormatCasingTypeOfNonLiteralElements(
            format_str.substr(0, matched_len), format_element.category));
    format_element.len_in_format_str = matched_len;
    if (format_element.type == FormatElementType::kFFN &&
        !absl::SimpleAtoi(format_str.substr(2, matched_len - 2),
                          &format_element.subsecond_digit_count)) {
      return MakeEvalError() << "Failed to parse format element of FFN type";
    }
    return format_element;
  }

  // For literal format elements, we preserve casing of output letters since
  // they are originally from user input format string.
  format_element.format_casing_type = FormatCasingType::kPreserveCase;
  if (format_element.type == FormatElementType::kSimpleLiteral) {
    format_element.len_in_format_str = matched_len;
    format_element.literal_value = format_str.substr(0, matched_len);
    return format_element;
  }

  if (format_element.type == FormatElementType::kWhitespace) {
    // If the matched type is "kWhitespace", we search for the end of sequence
    // of consecutive ' ' (ASCII 32) characters.
    while (matched_len < format_str.length() &&
           format_str[matched_len] == ' ') {
      matched_len++;
    }
    format_element.len_in_format_str = matched_len;
    return format_element;
  }

  GOOGLESQL_RET_CHECK(format_element.type == FormatElementType::kDoubleQuotedLiteral);
  // If the matched type is "kDoubleQuotedLiteral", we search for the end
  // manually and do the unescaping in this process.
  format_element.literal_value = "";
  size_t ind_to_check = 1;
  bool is_escaped = false;
  bool stop_search = false;

  while (ind_to_check < format_str.length() && !stop_search) {
    // Includes the char at position <ind_to_check>.
    matched_len++;
    char char_to_check = format_str[ind_to_check];
    ind_to_check++;
    if (is_escaped) {
      if (char_to_check == '\\' || char_to_check == '\"') {
        is_escaped = false;
      } else {
        return MakeEvalError() << "Unsupported escape sequence \\"
                               << char_to_check << " in text";
      }
    } else if (char_to_check == '\\') {
      is_escaped = true;
      continue;
    } else if (char_to_check == '\"') {
      stop_search = true;
      break;
    }
    format_element.literal_value.push_back(char_to_check);
  }
  if (!stop_search) {
    return MakeEvalError() << "Cannot find matching \" for quoted literal";
  }
  format_element.len_in_format_str = matched_len;
  return format_element;
}

// We need the upper format_str to do the search in prefix tree since matching
// are case-sensitive and we need the original format_str to extract the
// original_str for the format element object.
absl::StatusOr<std::vector<DateTimeFormatElement>> GetDateTimeFormatElements(
    absl::string_view format_str) {
  std::vector<DateTimeFormatElement> format_elements;
  size_t processed_len = 0;
  std::string upper_format_str_temp = absl::AsciiStrToUpper(format_str);
  absl::string_view upper_format_str = upper_format_str_temp;
  while (processed_len < format_str.size()) {
    auto res =
        GetNextDateTimeFormatElement(format_str.substr(processed_len),
                                     upper_format_str.substr(processed_len));
    if (res.ok()) {
      DateTimeFormatElement& format_element = res.value();
      format_elements.push_back(format_element);
      processed_len += format_element.len_in_format_str;
    } else {
      return MakeEvalError()
             << res.status().message() << " at " << processed_len;
    }
  }

  return format_elements;
}

// Takes a format model vector and rewrites it to be a format element string
// that can be correctly formatted by FormatTime. Any elements that are not
// supported by FormatTime will be formatted manually in this function. Any
// non-literal elements that output strings will be outputted with the first
// letter capitalized and all subsequent letters will be lowercase.
absl::StatusOr<std::string> FromDateTimeFormatElementToFormatString(
    const DateTimeFormatElement& format_element,
    const absl::TimeZone::CivilInfo info) {
  switch (format_element.type) {
    case FormatElementType::kSimpleLiteral:
    case FormatElementType::kDoubleQuotedLiteral:
      return format_element.literal_value;
    case FormatElementType::kWhitespace: {
      std::string res = "";
      for (int i = 0; i < format_element.len_in_format_str; ++i) {
        res.push_back(' ');
      }
      return res;
    }
    case FormatElementType::kYYYY:
    case FormatElementType::kYYY:
    case FormatElementType::kYY:
    case FormatElementType::kY:
    case FormatElementType::kRRRR:
    case FormatElementType::kRR: {
      int element_length = format_element.len_in_format_str;
      // YYYY will output the whole year regardless of how many digits are in
      // the year.
      // FormatTime does not support the year with the last 3 digits.
      int trunc_year =
          static_cast<int>(info.cs.year()) % powers_of_ten[element_length];
      return absl::StrFormat(
          "%0*d", format_element.len_in_format_str,
          (element_length == 4 ? info.cs.year() : trunc_year));
      break;
    }
    case FormatElementType::kMM:
      return "%m";
    case FormatElementType::kMON:
      return "%b";
    case FormatElementType::kMONTH:
      return "%B";
    case FormatElementType::kD:
      return std::to_string(internal_functions::DayOfWeekIntegerSunToSat1To7(
          absl::GetWeekday(info.cs)));
    case FormatElementType::kDD:
      return "%d";
    case FormatElementType::kDDD:
      return "%j";
    case FormatElementType::kDAY:
      return "%A";
    case FormatElementType::kDY:
      return "%a";
    case FormatElementType::kHH:
    case FormatElementType::kHH12:
      return "%I";
    case FormatElementType::kHH24:
      return "%H";
    case FormatElementType::kMI:
      return "%M";
    case FormatElementType::kSS:
      return "%S";
    case FormatElementType::kSSSSS: {
      // FormatTime does not support having 5 digit second of the day.
      int second_of_day = info.cs.hour() * kNaiveNumSecondsPerHour +
                          info.cs.minute() * kNaiveNumSecondsPerMinute +
                          info.cs.second();
      return absl::StrFormat("%05d", second_of_day);
    }
    case FormatElementType::kFFN: {
      // TODO : FormatTime does not round fractional seconds.
      return absl::StrCat("%E", format_element.subsecond_digit_count, "f");
    }
    case FormatElementType::kAM:
    case FormatElementType::kPM: {
      if (info.cs.hour() >= 12) {
        return "PM";
      } else {
        return "AM";
      }
    }
    case FormatElementType::kAMWithDots:
    case FormatElementType::kPMWithDots: {
      if (info.cs.hour() >= 12) {
        return "P.M.";
      } else {
        return "A.M.";
      }
    }
    case FormatElementType::kTZH:
    case FormatElementType::kTZM: {
      bool positive_offset;
      int32_t hour_offset;
      int32_t minute_offset;
      internal_functions::GetSignHourAndMinuteTimeZoneOffset(
          info, &positive_offset, &hour_offset, &minute_offset);
      if (format_element.type == FormatElementType::kTZH) {
        return absl::StrFormat("%c%02d", positive_offset ? '+' : '-',
                               hour_offset);
      } else {
        return absl::StrFormat("%02d", minute_offset);
      }
    }
    default:
      return MakeEvalError()
             << "Unsupported format element " << format_element.ToString();
  }
}

// When <sub_nanoseconds> is empty, the timestamp to be formatted has nanosecond
// precision. Otherwise, the timestamp to be formatted has picosecond precision.
//
// REQUIRES: When <sub_nanoseconds> has value, the value is in the range
//   [0, 999].
absl::StatusOr<std::string> ResolveFormatString(
    const DateTimeFormatElement& format_element, absl::Time base_time,
    std::optional<uint32_t> sub_nanoseconds, absl::TimeZone timezone) {
  const absl::TimeZone::CivilInfo info = timezone.At(base_time);
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::string format_string,
      FromDateTimeFormatElementToFormatString(format_element, info));
  // We do not need to go through steps of calling FormatTime function and
  // resolving casing for literal format elements.
  if (format_element.category == FormatElementCategory::kLiteral) {
    return format_string;
  }

  if (sub_nanoseconds.has_value()) {
    GOOGLESQL_ASSIGN_OR_RETURN(format_string,
                     internal_functions::AddPicosecondsInFormatString(
                         format_string, /*subnanosecond=*/*sub_nanoseconds));
  }

  // The following resolves casing for format elements.
  std::string resolved_string =
      absl::FormatTime(format_string, base_time, timezone);

  switch (format_element.format_casing_type) {
    case FormatCasingType::kFormatCasingTypeUnspecified:
      return MakeEvalError() << "Format casing type is unspecified";
    case FormatCasingType::kPreserveCase:
    // For any format element that outputs a string, its formatting result from
    // FormatTime function are already outputted with the first letter
    // capitalized and all subsequent letters being lowercase, so we do not need
    // any extra processing here.
    case FormatCasingType::kOnlyFirstLetterUppercase:
      return resolved_string;
    case FormatCasingType::kAllLettersUppercase:
      return absl::AsciiStrToUpper(resolved_string);
    case FormatCasingType::kAllLettersLowercase:
      return absl::AsciiStrToLower(resolved_string);
  }
}

absl::StatusOr<std::string> FromCastFormatTimestampToStringInternal(
    absl::Span<const DateTimeFormatElement> format_elements,
    absl::Time base_time, std::optional<uint32_t> sub_nanoseconds,
    absl::TimeZone timezone) {
  if (!IsValidTime(base_time)) {
    return MakeEvalError() << "Invalid timestamp value: "
                           << absl::ToUnixMicros(base_time);
  }
  absl::TimeZone normalized_timezone =
      internal_functions::GetNormalizedTimeZone(base_time, timezone);
  std::string updated_format_string;
  for (const DateTimeFormatElement& format_element : format_elements) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::string str_format,
                     ResolveFormatString(format_element, base_time,
                                         sub_nanoseconds, normalized_timezone));
    absl::StrAppend(&updated_format_string, str_format);
  }
  return updated_format_string;
}

}  // namespace cast_date_time_internal

absl::StatusOr<StringToTimestampCaster> StringToTimestampCaster::Create(
    absl::string_view format_string) {
  GOOGLESQL_RETURN_IF_ERROR(ConductBasicFormatStringChecks(format_string));

  GOOGLESQL_ASSIGN_OR_RETURN(auto format_elements,
                   GetDateTimeFormatElements(format_string));
  GOOGLESQL_RETURN_IF_ERROR(
      ValidateDateTimeFormatElementsForTimestampType(format_elements));

  return StringToTimestampCaster(std::move(format_elements));
}

absl::Status StringToTimestampCaster::Cast(absl::string_view timestamp_string,
                                           absl::TimeZone default_timezone,
                                           absl::Time current_timestamp,
                                           int64_t* timestamp_micros) const {
  if (!IsWellFormedUTF8(timestamp_string)) {
    return MakeEvalError() << "Input string is not valid UTF-8";
  }

  return ParseTimeWithFormatElements(format_elements_, timestamp_string,
                                     default_timezone, current_timestamp,
                                     timestamp_micros);
}

absl::Status CastStringToTimestamp(absl::string_view format_string,
                                   absl::string_view timestamp_string,
                                   const absl::TimeZone default_timezone,
                                   const absl::Time current_timestamp,
                                   int64_t* timestamp_micros) {
  GOOGLESQL_ASSIGN_OR_RETURN(auto caster, StringToTimestampCaster::Create(format_string));
  return caster.Cast(timestamp_string, default_timezone, current_timestamp,
                     timestamp_micros);
}

absl::Status CastStringToTimestamp(absl::string_view format_string,
                                   absl::string_view timestamp_string,
                                   absl::string_view default_timezone_string,
                                   const absl::Time current_timestamp,
                                   int64_t* timestamp) {
  // Other two input string arguments (<format_string> and <timestamp_string>)
  // are checked in the overload call to CastStringToTimestamp.
  if (!IsWellFormedUTF8(default_timezone_string)) {
    return MakeEvalError() << "Input string is not valid UTF-8";
  }
  absl::TimeZone timezone;
  GOOGLESQL_RETURN_IF_ERROR(MakeTimeZone(default_timezone_string, &timezone));

  return CastStringToTimestamp(format_string, timestamp_string, timezone,
                               current_timestamp, timestamp);
}

absl::Status CastStringToTimestamp(absl::string_view format_string,
                                   absl::string_view timestamp_string,
                                   const absl::TimeZone default_timezone,
                                   const absl::Time current_timestamp,
                                   absl::Time* timestamp) {
  if (!IsWellFormedUTF8(timestamp_string)) {
    return MakeEvalError() << "Input string is not valid UTF-8";
  }
  GOOGLESQL_RETURN_IF_ERROR(ConductBasicFormatStringChecks(format_string));

  GOOGLESQL_ASSIGN_OR_RETURN(const std::vector<DateTimeFormatElement> format_elements,
                   GetDateTimeFormatElements(format_string));
  GOOGLESQL_RETURN_IF_ERROR(
      ValidateDateTimeFormatElementsForTimestampType(format_elements));

  uint32_t unused;
  return ParseTimeWithFormatElements(format_elements, timestamp_string,
                                     default_timezone, current_timestamp,
                                     kNanoseconds, timestamp, &unused);
}

absl::Status CastStringToTimestamp(absl::string_view format_string,
                                   absl::string_view timestamp_string,
                                   absl::string_view default_timezone_string,
                                   const absl::Time current_timestamp,
                                   absl::Time* timestamp) {
  // Other two input string arguments (<format_string> and <timestamp_string>)
  // are checked in the overload call to CastStringToTimestamp.
  if (!IsWellFormedUTF8(default_timezone_string)) {
    return MakeEvalError() << "Input string is not valid UTF-8";
  }
  absl::TimeZone timezone;
  GOOGLESQL_RETURN_IF_ERROR(MakeTimeZone(default_timezone_string, &timezone));

  return CastStringToTimestamp(format_string, timestamp_string, timezone,
                               current_timestamp, timestamp);
}

absl::StatusOr<PicoTime> CastStringToTimestamp(
    absl::string_view format_string, absl::string_view timestamp_string,
    absl::string_view default_timezone_string, absl::Time current_timestamp) {
  // Other two input string arguments (<format_string> and <timestamp_string>)
  // are checked in the overload call to CastStringToTimestamp.
  if (!IsWellFormedUTF8(default_timezone_string)) {
    return MakeEvalError() << "Timezone string is not valid UTF-8: "
                           << absl::CEscape(default_timezone_string);
  }
  absl::TimeZone timezone;
  GOOGLESQL_RETURN_IF_ERROR(MakeTimeZone(default_timezone_string, &timezone));

  return CastStringToTimestamp(format_string, timestamp_string, timezone,
                               current_timestamp);
}

absl::StatusOr<PicoTime> CastStringToTimestamp(
    absl::string_view format_string, absl::string_view timestamp_string,
    absl::TimeZone default_timezone, absl::Time current_timestamp) {
  if (!IsWellFormedUTF8(timestamp_string)) {
    return MakeEvalError() << "Timestamp string is not valid UTF-8: "
                           << absl::CEscape(timestamp_string);
  }
  GOOGLESQL_RETURN_IF_ERROR(ConductBasicFormatStringChecks(format_string));
  GOOGLESQL_ASSIGN_OR_RETURN(const std::vector<DateTimeFormatElement> format_elements,
                   GetDateTimeFormatElements(format_string));
  GOOGLESQL_RETURN_IF_ERROR(ValidateDateTimeFormatElementsForTimestampType(
      format_elements, kPicoseconds));

  uint32_t sub_nanoseconds;
  absl::Time time;
  GOOGLESQL_RETURN_IF_ERROR(ParseTimeWithFormatElements(
      format_elements, timestamp_string, default_timezone, current_timestamp,
      kPicoseconds, &time, &sub_nanoseconds));
  GOOGLESQL_ASSIGN_OR_RETURN(PicoTime pico_time, PicoTime::Create(time));
  return PicoTime::FromUnixPicos(pico_time.ToUnixPicos() + sub_nanoseconds);
}

absl::StatusOr<StringToDateCaster> StringToDateCaster::Create(
    absl::string_view format_string) {
  GOOGLESQL_RETURN_IF_ERROR(ConductBasicFormatStringChecks(format_string));

  GOOGLESQL_ASSIGN_OR_RETURN(auto format_elements,
                   GetDateTimeFormatElements(format_string));
  GOOGLESQL_RETURN_IF_ERROR(ValidateDateTimeFormatElementsForDateType(format_elements));

  return StringToDateCaster(std::move(format_elements));
}

absl::Status StringToDateCaster::Cast(absl::string_view date_string,
                                      int32_t current_date,
                                      int32_t* date) const {
  if (!IsWellFormedUTF8(date_string)) {
    return MakeEvalError() << "Input string is not valid UTF-8";
  }

  absl::Time current_date_utc_ts;
  int64_t timestamp;
  GOOGLESQL_RETURN_IF_ERROR(ConvertDateToTimestamp(current_date, absl::UTCTimeZone(),
                                         &current_date_utc_ts));
  // We use <current_date_utc_ts> (constructed with <current_date> and "UTC")
  // as <current_timestamp> and "UTC" as <default_timezone>, so the
  // <current_year> and <current_date> used in ParseTimeWithFormatElements
  // function would be the same as year and month in <current_date>.
  GOOGLESQL_RETURN_IF_ERROR(ParseTimeWithFormatElements(format_elements_, date_string,
                                              absl::UTCTimeZone(),
                                              current_date_utc_ts, &timestamp));
  GOOGLESQL_RETURN_IF_ERROR(ExtractFromTimestamp(DATE, timestamp, kMicroseconds,
                                       absl::UTCTimeZone(), date));
  return absl::OkStatus();
}

absl::Status CastStringToDate(absl::string_view format_string,
                              absl::string_view date_string,
                              int32_t current_date, int32_t* date) {
  GOOGLESQL_ASSIGN_OR_RETURN(auto caster, StringToDateCaster::Create(format_string));
  return caster.Cast(date_string, current_date, date);
}

absl::Status CastStringToTime(absl::string_view format_string,
                              absl::string_view time_string,
                              TimestampScale scale, TimeValue* time) {
  GOOGLESQL_ASSIGN_OR_RETURN(auto caster, StringToTimeCaster::Create(format_string));
  return caster.Cast(time_string, scale, time);
}

absl::StatusOr<StringToTimeCaster> StringToTimeCaster::Create(
    absl::string_view format_string) {
  GOOGLESQL_RETURN_IF_ERROR(ConductBasicFormatStringChecks(format_string));
  GOOGLESQL_ASSIGN_OR_RETURN(auto format_elements,
                   GetDateTimeFormatElements(format_string));
  GOOGLESQL_RETURN_IF_ERROR(ValidateDateTimeFormatElementsForTimeType(format_elements));

  return StringToTimeCaster(std::move(format_elements));
}

absl::Status StringToTimeCaster::Cast(absl::string_view time_string,
                                      TimestampScale scale,
                                      TimeValue* time) const {
  if (!IsWellFormedUTF8(time_string)) {
    return MakeEvalError() << "Input string is not valid UTF-8";
  }

  GOOGLESQL_RETURN_IF_ERROR(ValidateTimeScale(scale));

  absl::Time timestamp;
  uint32_t unused;
  // We use "1970-01-01 utc" as the <current_timestamp> argument for
  // ParseTimeWithFormatElements function, and it actually has no effect for the
  // final output since we derive default values for time parts from
  // "00:00:00:000000000".
  GOOGLESQL_RETURN_IF_ERROR(ParseTimeWithFormatElements(
      format_elements_, time_string, absl::UTCTimeZone(),
      /*current_timestamp=*/absl::UnixEpoch(), scale, &timestamp, &unused));
  GOOGLESQL_RETURN_IF_ERROR(
      ConvertTimestampToTime(timestamp, absl::UTCTimeZone(), scale, time));

  return absl::OkStatus();
}

absl::StatusOr<StringToDatetimeCaster> StringToDatetimeCaster::Create(
    absl::string_view format_string) {
  GOOGLESQL_RETURN_IF_ERROR(ConductBasicFormatStringChecks(format_string));
  GOOGLESQL_ASSIGN_OR_RETURN(auto format_elements,
                   GetDateTimeFormatElements(format_string));
  GOOGLESQL_RETURN_IF_ERROR(
      ValidateDateTimeFormatElementsForDatetimeType(format_elements));

  return StringToDatetimeCaster(std::move(format_elements));
}

absl::Status StringToDatetimeCaster::Cast(absl::string_view datetime_string,
                                          TimestampScale scale,
                                          int32_t current_date,
                                          DatetimeValue* datetime) const {
  if (!IsWellFormedUTF8(datetime_string)) {
    return MakeEvalError() << "Input string is not valid UTF-8";
  }

  GOOGLESQL_RETURN_IF_ERROR(ValidateTimeScale(scale));

  absl::Time current_date_utc_ts;
  absl::Time timestamp;
  uint32_t unused;
  GOOGLESQL_RETURN_IF_ERROR(ConvertDateToTimestamp(current_date, absl::UTCTimeZone(),
                                         &current_date_utc_ts));
  // We use <current_date_utc_ts> (constructed with <current_date> and "UTC")
  // as <current_timestamp> and "UTC" as <default_timezone>, so the
  // <current_year> and <current_date> used in ParseTimeWithFormatElements
  // function would be the same as year and month in <current_date>.
  GOOGLESQL_RETURN_IF_ERROR(ParseTimeWithFormatElements(
      format_elements_, datetime_string, absl::UTCTimeZone(),
      current_date_utc_ts, scale, &timestamp, &unused));
  GOOGLESQL_RETURN_IF_ERROR(
      ConvertTimestampToDatetime(timestamp, absl::UTCTimeZone(), datetime));
  return absl::OkStatus();
}

absl::Status CastStringToDatetime(absl::string_view format_string,
                                  absl::string_view datetime_string,
                                  TimestampScale scale, int32_t current_date,
                                  DatetimeValue* datetime) {
  GOOGLESQL_ASSIGN_OR_RETURN(auto caster, StringToDatetimeCaster::Create(format_string));
  return caster.Cast(datetime_string, scale, current_date, datetime);
}

absl::Status ValidateFormatStringForParsing(absl::string_view format_string,
                                            googlesql::TypeKind out_type) {
  GOOGLESQL_RETURN_IF_ERROR(ConductBasicFormatStringChecks(format_string));

  GOOGLESQL_ASSIGN_OR_RETURN(const std::vector<DateTimeFormatElement> format_elements,
                   GetDateTimeFormatElements(format_string));
  if (out_type == TYPE_TIMESTAMP) {
    return ValidateDateTimeFormatElementsForTimestampType(format_elements);
  } else if (out_type == TYPE_DATE) {
    return ValidateDateTimeFormatElementsForDateType(format_elements);
  } else if (out_type == TYPE_TIME) {
    return ValidateDateTimeFormatElementsForTimeType(format_elements);
  } else if (out_type == TYPE_DATETIME) {
    return ValidateDateTimeFormatElementsForDatetimeType(format_elements);
  } else {
    return MakeSqlError() << "Unsupported output type for validation";
  }
}

absl::Status ValidateFormatStringForFormatting(absl::string_view format_string,
                                               googlesql::TypeKind out_type) {
  GOOGLESQL_RETURN_IF_ERROR(ConductBasicFormatStringChecks(format_string));

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::vector<cast_date_time_internal::DateTimeFormatElement>
          format_elements,
      cast_date_time_internal::GetDateTimeFormatElements(format_string));
  switch (out_type) {
    case TYPE_DATE:
      return ValidateDateDateTimeFormatElementsForFormatting(format_elements);
    case TYPE_DATETIME:
      return ValidateDatetimeDateTimeFormatElementsForFormatting(
          format_elements);
    case TYPE_TIME:
      return ValidateTimeDateTimeFormatElementsForFormatting(format_elements);
    case TYPE_TIMESTAMP:
      return absl::OkStatus();
    default:
      return MakeSqlError() << "Unsupported output type for validation";
  }
}

absl::Status ValidateFormatStringContainsYearAndMonth(
    absl::string_view format_string) {
  GOOGLESQL_RETURN_IF_ERROR(ConductBasicFormatStringChecks(format_string));

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::vector<cast_date_time_internal::DateTimeFormatElement>
          format_elements,
      cast_date_time_internal::GetDateTimeFormatElements(format_string));
  bool year_seen = false;
  bool month_seen = false;
  for (const DateTimeFormatElement& element : format_elements) {
    switch (element.category) {
      case FormatElementCategory::kYear:
        year_seen = true;
        break;
      case FormatElementCategory::kMonth:
        month_seen = true;
        break;
      default:
        continue;
    }
  }
  if (year_seen && month_seen) {
    return absl::OkStatus();
  }
  return MakeEvalError() << "Year and month format elements are required.";
}

absl::StatusOr<DateToStringCaster> DateToStringCaster::Create(
    absl::string_view format_string) {
  GOOGLESQL_RETURN_IF_ERROR(ConductBasicFormatStringChecks(format_string));
  GOOGLESQL_ASSIGN_OR_RETURN(
      auto format_elements,
      cast_date_time_internal::GetDateTimeFormatElements(format_string));
  GOOGLESQL_RETURN_IF_ERROR(
      ValidateDateDateTimeFormatElementsForFormatting(format_elements));
  return DateToStringCaster(std::move(format_elements));
}

absl::Status DateToStringCaster::Cast(int32_t date, std::string* out) const {
  if (!IsValidDate(date)) {
    return MakeEvalError() << "Invalid date value: " << date;
  }

  // Treats it as a timestamp at midnight on that date and invokes the
  // format_timestamp function.
  int64_t date_timestamp = static_cast<int64_t>(date) * kNaiveNumMicrosPerDay;
  GOOGLESQL_ASSIGN_OR_RETURN(
      *out, cast_date_time_internal::FromCastFormatTimestampToStringInternal(
                format_elements_, MakeTime(date_timestamp, kMicroseconds),
                /*sub_nanoseconds=*/std::nullopt, absl::UTCTimeZone()));
  return absl::OkStatus();
}

absl::Status CastFormatDateToString(absl::string_view format_string,
                                    int32_t date, std::string* out) {
  GOOGLESQL_ASSIGN_OR_RETURN(auto caster, DateToStringCaster::Create(format_string));
  GOOGLESQL_RETURN_IF_ERROR(caster.Cast(date, out));
  return absl::OkStatus();
}

absl::StatusOr<DatetimeToStringCaster> DatetimeToStringCaster::Create(
    absl::string_view format_string) {
  GOOGLESQL_RETURN_IF_ERROR(ConductBasicFormatStringChecks(format_string));
  GOOGLESQL_ASSIGN_OR_RETURN(
      auto format_elements,
      cast_date_time_internal::GetDateTimeFormatElements(format_string));
  GOOGLESQL_RETURN_IF_ERROR(
      ValidateDatetimeDateTimeFormatElementsForFormatting(format_elements));
  return DatetimeToStringCaster(std::move(format_elements));
}

absl::Status DatetimeToStringCaster::Cast(const DatetimeValue& datetime,
                                          std::string* out) const {
  if (!datetime.IsValid()) {
    return MakeEvalError() << "Invalid datetime value: "
                           << datetime.DebugString();
  }

  absl::Time datetime_in_utc =
      absl::UTCTimeZone().At(datetime.ConvertToCivilSecond()).pre;
  datetime_in_utc += absl::Nanoseconds(datetime.Nanoseconds());

  GOOGLESQL_ASSIGN_OR_RETURN(
      *out, cast_date_time_internal::FromCastFormatTimestampToStringInternal(
                format_elements_, datetime_in_utc,
                /*sub_nanoseconds=*/std::nullopt, absl::UTCTimeZone()));
  return absl::OkStatus();
}

absl::Status CastFormatDatetimeToString(absl::string_view format_string,
                                        const DatetimeValue& datetime,
                                        std::string* out) {
  GOOGLESQL_ASSIGN_OR_RETURN(auto caster, DatetimeToStringCaster::Create(format_string));
  return caster.Cast(datetime, out);
}

absl::StatusOr<TimeToStringCaster> TimeToStringCaster::Create(
    absl::string_view format_string) {
  GOOGLESQL_RETURN_IF_ERROR(ConductBasicFormatStringChecks(format_string));
  GOOGLESQL_ASSIGN_OR_RETURN(
      auto format_elements,
      cast_date_time_internal::GetDateTimeFormatElements(format_string));
  GOOGLESQL_RETURN_IF_ERROR(
      ValidateTimeDateTimeFormatElementsForFormatting(format_elements));
  return TimeToStringCaster(std::move(format_elements));
}

absl::Status TimeToStringCaster::Cast(const TimeValue& time,
                                      std::string* out) const {
  if (!time.IsValid()) {
    return MakeEvalError() << "Invalid time value: " << time.DebugString();
  }

  absl::Time time_in_epoch_day =
      absl::UTCTimeZone()
          .At(absl::CivilSecond(1970, 1, 1, time.Hour(), time.Minute(),
                                time.Second()))
          .pre;
  time_in_epoch_day += absl::Nanoseconds(time.Nanoseconds());

  GOOGLESQL_ASSIGN_OR_RETURN(
      *out, cast_date_time_internal::FromCastFormatTimestampToStringInternal(
                format_elements_, time_in_epoch_day,
                /*sub_nanoseconds=*/std::nullopt, absl::UTCTimeZone()));
  return absl::OkStatus();
}

absl::Status CastFormatTimeToString(absl::string_view format_string,
                                    const TimeValue& time, std::string* out) {
  GOOGLESQL_ASSIGN_OR_RETURN(auto caster, TimeToStringCaster::Create(format_string));
  return caster.Cast(time, out);
}

absl::StatusOr<TimestampToStringCaster> TimestampToStringCaster::Create(
    absl::string_view format_string) {
  GOOGLESQL_RETURN_IF_ERROR(ConductBasicFormatStringChecks(format_string));

  GOOGLESQL_ASSIGN_OR_RETURN(
      auto format_elements,
      cast_date_time_internal::GetDateTimeFormatElements(format_string));
  return TimestampToStringCaster(std::move(format_elements));
}

absl::Status TimestampToStringCaster::Cast(int64_t timestamp_micros,
                                           absl::TimeZone timezone,
                                           std::string* out) const {
  GOOGLESQL_ASSIGN_OR_RETURN(
      *out, cast_date_time_internal::FromCastFormatTimestampToStringInternal(
                format_elements_, MakeTime(timestamp_micros, kMicroseconds),
                /*sub_nanoseconds=*/std::nullopt, timezone));
  return absl::OkStatus();
}

absl::Status TimestampToStringCaster::Cast(absl::Time timestamp,
                                           absl::TimeZone timezone,
                                           std::string* out) const {
  GOOGLESQL_ASSIGN_OR_RETURN(
      *out, cast_date_time_internal::FromCastFormatTimestampToStringInternal(
                format_elements_, timestamp, /*sub_nanoseconds=*/std::nullopt,
                timezone));
  return absl::OkStatus();
}

absl::StatusOr<std::string> TimestampToStringCaster::Cast(
    PicoTime timestamp, absl::TimeZone timezone) const {
  return cast_date_time_internal::FromCastFormatTimestampToStringInternal(
      format_elements_, timestamp.ToAbslTime(), timestamp.SubNanoseconds(),
      timezone);
}

absl::Status CastFormatTimestampToString(absl::string_view format_string,
                                         int64_t timestamp_micros,
                                         absl::TimeZone timezone,
                                         std::string* out) {
  // <format_string> is checked in the overload call to
  // CastFormatTimestampToString.
  return CastFormatTimestampToString(
      format_string, MakeTime(timestamp_micros, kMicroseconds), timezone, out);
}

absl::Status CastFormatTimestampToString(absl::string_view format_string,
                                         int64_t timestamp_micros,
                                         absl::string_view timezone_string,
                                         std::string* out) {
  GOOGLESQL_RETURN_IF_ERROR(ConductBasicFormatStringChecks(format_string));

  if (!IsWellFormedUTF8(timezone_string)) {
    return MakeEvalError() << "Timezone string is not a valid UTF-8 string.";
  }
  absl::TimeZone timezone;
  GOOGLESQL_RETURN_IF_ERROR(MakeTimeZone(timezone_string, &timezone));
  return CastFormatTimestampToString(format_string, timestamp_micros, timezone,
                                     out);
}

absl::Status CastFormatTimestampToString(absl::string_view format_string,
                                         absl::Time timestamp,
                                         absl::string_view timezone_string,
                                         std::string* out) {
  // <format_string> is checked in the overload call to
  // CastFormatTimestampToString.
  absl::TimeZone timezone;
  GOOGLESQL_RETURN_IF_ERROR(MakeTimeZone(timezone_string, &timezone));

  return CastFormatTimestampToString(format_string, timestamp, timezone, out);
}

absl::Status CastFormatTimestampToString(absl::string_view format_string,
                                         absl::Time timestamp,
                                         absl::TimeZone timezone,
                                         std::string* out) {
  GOOGLESQL_ASSIGN_OR_RETURN(auto caster, TimestampToStringCaster::Create(format_string));
  return caster.Cast(timestamp, timezone, out);
}

absl::StatusOr<std::string> CastFormatTimestampToString(
    absl::string_view format_string, PicoTime timestamp,
    absl::TimeZone timezone) {
  GOOGLESQL_ASSIGN_OR_RETURN(auto caster, TimestampToStringCaster::Create(format_string));
  return caster.Cast(timestamp, timezone);
}

absl::StatusOr<std::string> CastFormatTimestampToString(
    absl::string_view format_string, PicoTime timestamp,
    absl::string_view timezone_string) {
  absl::TimeZone timezone;
  GOOGLESQL_RETURN_IF_ERROR(MakeTimeZone(timezone_string, &timezone));
  return CastFormatTimestampToString(format_string, timestamp, timezone);
}

}  // namespace functions
}  // namespace googlesql
