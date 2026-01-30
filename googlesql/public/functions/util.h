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

#ifndef GOOGLESQL_PUBLIC_FUNCTIONS_UTIL_H_
#define GOOGLESQL_PUBLIC_FUNCTIONS_UTIL_H_

#include <cstdint>
#include <string>

#include "absl/base/attributes.h"
#include "absl/numeric/int128.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/status.h"

namespace googlesql {
namespace functions {
namespace internal {

// Contains a string constant with the name of the template parameter.
template <typename T>
struct ArithmeticType;

template <>
struct ArithmeticType<int32_t> {
  static constexpr char kName[] = "int32";
};
template <>
struct ArithmeticType<int64_t> {
  static constexpr char kName[] = "int64";
};
template <>
struct ArithmeticType<uint64_t> {
  static constexpr char kName[] = "uint64";
};
template <>
struct ArithmeticType<float> {
  static constexpr char kName[] = "float";
};
template <>
struct ArithmeticType<double> {
  static constexpr char kName[] = "double";
};
template <>
struct ArithmeticType<long double> {
  static constexpr char kName[] = "long double";
};
template <>
struct ArithmeticType<absl::int128> {
  static constexpr char kName[] = "int128";
};

// Error message assembly is defined out-of-line to extra avoid overhead in the
// common case where there is no overflow.
template <typename T>
std::string UnaryOverflowMessage(T in, absl::string_view operator_symbol);

extern template std::string UnaryOverflowMessage<int32_t>(
    int32_t in, absl::string_view operator_symbol);
extern template std::string UnaryOverflowMessage<int64_t>(
    int64_t in, absl::string_view operator_symbol);

template <typename T>
std::string BinaryOverflowMessage(T in1, T in2,
                                  absl::string_view operator_symbol);

extern template std::string BinaryOverflowMessage<int32_t>(
    int32_t in1, int32_t in2, absl::string_view operator_symbol);
extern template std::string BinaryOverflowMessage<int64_t>(
    int64_t in1, int64_t in2, absl::string_view operator_symbol);
extern template std::string BinaryOverflowMessage<uint64_t>(
    uint64_t in1, uint64_t in2, absl::string_view operator_symbol);
extern template std::string BinaryOverflowMessage<float>(
    float in1, float in2, absl::string_view operator_symbol);
extern template std::string BinaryOverflowMessage<double>(
    double in1, double in2, absl::string_view operator_symbol);

template <>
std::string BinaryOverflowMessage<long double>(
    long double in1, long double in2, absl::string_view operator_symbol);

template <typename T>
std::string DivisionByZeroMessage(T in1, T in2);

extern template std::string DivisionByZeroMessage<int64_t>(int64_t in1,
                                                           int64_t in2);
extern template std::string DivisionByZeroMessage<uint64_t>(uint64_t in1,
                                                            uint64_t in2);
extern template std::string DivisionByZeroMessage<double>(double in1,
                                                          double in2);
template <>
std::string DivisionByZeroMessage<long double>(long double in1,
                                               long double in2);

// Updates `status` with `msg` and force code to kOutOfRange.
// Additionally, coerces msg to be valid UTF-8 by replacing any
// ill-formed subsequences with the Unicode REPLACEMENT CHARACTER (U+FFFD).
// Does nothing if `status` == `nullptr` or `!status->ok()`
// Returns false for all inputs (for convenience).
bool UpdateError(absl::Status* status, absl::string_view msg);

// Returns a Status with `msg` and with a code of kOutOfRange.
// Additionally, coerces msg to be valid UTF-8 by replacing any
// ill-formed subsequences with the Unicode REPLACEMENT CHARACTER (U+FFFD).
absl::Status CreateFunctionError(absl::string_view msg);

// Returns an ok status if `position` and `occurrence` are valid 1-based indices
// else returns an error.
absl::Status ValidatePositionAndOccurrence(int64_t position,
                                           int64_t occurrence);

}  // namespace internal
}  // namespace functions
}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_FUNCTIONS_UTIL_H_
