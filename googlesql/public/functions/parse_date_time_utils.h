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

#ifndef GOOGLESQL_PUBLIC_FUNCTIONS_PARSE_DATE_TIME_UTILS_H_
#define GOOGLESQL_PUBLIC_FUNCTIONS_PARSE_DATE_TIME_UTILS_H_

#include <cstdint>
#include <string>

#include "googlesql/public/functions/date_time_util.h"
#include "absl/time/time.h"

namespace googlesql {
namespace functions {
namespace parse_date_time_utils {

// Converts Time to int64 microseconds and validates the value is within
// the supported GoogleSQL range.
bool ConvertTimeToTimestamp(absl::Time time, int64_t* timestamp);

// Parses an integer number from the string in the range [<dp>, <end_of_data>).
// We allow the string to have a leading "-" to indicate minus sign.
// The number of consumed chars is MIN(<max_width>, number of valid numeric
// characters starting from <dp>).
//
// The input const char* <dp> must not be nullptr and it must be smaller than
// <end_of_data>. The <max_width> must be a positive number and the parsed
// string must be within range [<min>, <max>]. Returns nullptr if these
// conditions are not met or if a valid integer could not be parsed.
//
// Returns a pointer to the first unparsed character upon successfully parsing
// an integer, and returns nullptr otherwise.
const char* ParseInt(const char* dp, const char* end_of_data, int max_width,
                     int64_t min, int64_t max, int64_t* vp);

// This function is similar to the above function but result is of int* type.
const char* ParseInt(const char* dp, const char* end_of_data, int max_width,
                     int64_t min, int64_t max, int* vp);

// Parses up to <max_digits> (0 means unbounded) from chars in range [<dp>,
// <end_of_data>), and returns a Duration (digits beyond the given <scale> are
// truncated), and sub-nanoseconds value is returned by <sub_nanoseconds>.
//
// The input const char* <dp> must not be nullptr and it must be smaller than
// <end_of_data>. The <max_width> must be non-negative. Returns nullptr if any
// of these conditions are not met, or if a valid integer cannot be parsed.
//
// Returns a pointer to the first unparsed character upon successfully parsing
// an integer, and returns nullptr otherwise.
const char* ParseSubSeconds(const char* dp, const char* end_of_data,
                            int max_digits, TimestampScale scale,
                            absl::Duration* subseconds,
                            uint32_t* sub_nanoseconds);
}  // namespace parse_date_time_utils
}  // namespace functions
}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_FUNCTIONS_PARSE_DATE_TIME_UTILS_H_
