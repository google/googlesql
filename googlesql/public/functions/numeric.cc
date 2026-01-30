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

#include "googlesql/public/functions/numeric.h"

#include <cstddef>
#include <string>

#include "googlesql/base/logging.h"
#include "googlesql/public/functions/util.h"
#include "googlesql/public/numeric_value.h"
#include "absl/strings/ascii.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"

namespace googlesql {
namespace functions {
namespace {

// Skips consecutive whitespaces starting at <index> in the input <str>. Upon
// return, <index> points to the position in <str> after the last whitespace
// that is skpped.
void SkipWhitespaces(absl::string_view str, size_t& index) {
  while (index < str.size() && absl::ascii_isspace(str[index])) {
    ++index;
  }
}

// Copies consecutive digits starting at <index> in the input <str> by appending
// them to <out>. Upon return, <index> points to the position in <str> after the
// last digit that is parsed.
void CopyLeadingDigits(absl::string_view str, size_t& index, std::string& out) {
  for (; index < str.size(); ++index) {
    if (str[index] >= '0' && str[index] <= '9') {
      out.push_back(str[index]);
    } else {
      break;
    }
  }
}

// Processes <str>, which is the input to PARSE_NUMERIC/PARSE_BIGNUMERIC
// function, to generate the output string <out> that will be passed to
// NumericValue/BigNumericValue::FromString().
//
// E.g., for input "  1,2,3.45 -", the output is "-123.45".
//
// The main differences between valid input here and what's accepted by
// NumericValue/BigNumericValue::FromString() are:
// - Group separators (i.e. commas) are allowed, e.g. "1,2,3".
// - The sign can be put after the number, e.g. "1.23-".
// - Spaces are allowed between the sign and the number part, e.g."-  123".
//
// For the detailed spec of the valid format of <str>, see (broken link),
// section "Valid Input Format".
//
// Returns true on success. Returns false if <str> does not have the valid
// format. Note that the function does not detect all format errors.
bool FilterParseNumericString(absl::string_view str, std::string& out) {
  out.reserve(str.size() + 1);

  // Reserve a space for the +/- sign if it's after the number.
  out.push_back(' ');

  // Ignore leading whitespace and find the leading signs.
  // If we add more than one, that's ok since we'll fail to parse the result
  // later.
  size_t index = 0;
  for (; index < str.size(); ++index) {
    char c = str[index];
    if (c == '+' || c == '-') {
      out.push_back(c);
    } else if (!absl::ascii_isspace(c)) {
      break;
    }
  }

  // parse the integer part
  bool integer_part_empty = true;
  bool has_digit = false;
  for (; index < str.size(); ++index) {
    if ((str[index] >= '0' && str[index] <= '9')) {
      out.push_back(str[index]);
      has_digit = true;
    } else if (str[index] == ',') {
    } else {
      break;
    }
    integer_part_empty = false;
  }
  // validate that if integer is not empty, then it must contain at least one
  // digit.
  if (!integer_part_empty && !has_digit) {
    return false;
  }

  // parse the decimal point
  if (index < str.size()) {
    if (str[index] == '.') {
      out.push_back(str[index]);
      ++index;
    }
  }

  // parse the fractional part
  CopyLeadingDigits(str, index, out);

  // parse the exponent
  if (index < str.size() && (str[index] == 'e' || str[index] == 'E')) {
    out.push_back(str[index]);
    ++index;

    // parse optional sign
    if (index < str.size() && (str[index] == '+' || str[index] == '-')) {
      out.push_back(str[index]);
      ++index;
    }

    // parse digits
    CopyLeadingDigits(str, index, out);
  }

  SkipWhitespaces(str, index);

  // parse the sign after the number
  if (index < str.size()) {
    if (str[index] == '+' || str[index] == '-') {
      out[0] = str[index];
      ++index;
    }
  }

  SkipWhitespaces(str, index);

  if (index < str.size()) {
    // the input contains unrecognized chars.
    return false;
  }

  return true;
}

// Implementation of ParseNumeric() and ParseBigNumeric().
// T is either NumericValue or BignumericValue.
template <typename T>
bool ParseNumericImpl(absl::string_view str, absl::string_view func_name,
                      T* out, absl::Status* error) {
  std::string number_string;
  if (FilterParseNumericString(str, number_string)) {
    auto v = T::FromString(number_string);
    if (v.ok()) {
      *out = v.value();
      return true;
    }
  }

  return internal::UpdateError(
      error, absl::Substitute("Invalid input to $0: \"$1\"", func_name, str));
}

}  // anonymous namespace

bool ParseNumeric(absl::string_view str, NumericValue* out,
                  absl::Status* error) {
  return ParseNumericImpl<NumericValue>(str, "PARSE_NUMERIC", out, error);
}

bool ParseBigNumeric(absl::string_view str, BigNumericValue* out,
                     absl::Status* error) {
  return ParseNumericImpl<BigNumericValue>(str, "PARSE_BIGNUMERIC", out, error);
}

}  // namespace functions
}  // namespace googlesql
