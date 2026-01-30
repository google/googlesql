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

#ifndef GOOGLESQL_COMMON_STRING_UTIL_H_
#define GOOGLESQL_COMMON_STRING_UTIL_H_

#include <cmath>
#include <string>

#include "googlesql/common/canonicalize_signed_zero_to_string.h"
#include "absl/strings/str_cat.h"
#include "googlesql/base/string_numbers.h"

namespace googlesql {

// Converts a float into a string which, if passed to `strtof()`, will produce
// the original float to the same precision.
//
// Exception: for NaN values, `strtod(RoundTripFloatToString(NaN))`
// may produce any NaN value, not necessarily the original NaN value.
//
// No guarantees are made about 'compactness' of the returned string.
// `1.0` may print as `1.0000000000000` or as `1.0`
//
// If this round-trip property is not required, consider simply absl::StrCat(v).
inline std::string RoundTripFloatToString(float f) {
  if (!std::isnan(f)) {
    return googlesql_base::RoundTripFloatToString(f);
  }
  return "nan";
}

// Converts a double into a string which, if passed to `strtod()`, will produce
// the original float to the same precision.
//
// Exception: for NaN values, `strtod(RoundTripDoubleToString(NaN))`
// may produce any NaN value, not necessarily the original NaN value.
//
// No guarantees are made about 'compactness' of the returned string.
// `1.0` may print as `1.0000000000000` or as `1.0`
//
// If this round-trip property is not required, consider simply absl::StrCat(v).
inline std::string RoundTripDoubleToString(double d) {
  if (!std::isnan(d)) {
    return googlesql_base::RoundTripDoubleToString(d);
  }
  return "nan";
}

}  // namespace googlesql

#endif  // GOOGLESQL_COMMON_STRING_UTIL_H_
