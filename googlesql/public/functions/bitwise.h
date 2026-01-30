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

// This file implements basic bitwise operations. The following functions
// are defined:
//
//   bool BitwiseNot(T in1, T* out, absl::Status* error);
//   bool BitwiseNotBytes(absl::string_view in, string* out,
//                        absl::Status* error);
//   bool BitwiseOr(T in1, T in2, T* out, absl::Status* error);
//   bool BitwiseXor(T in1, T in2, T* out, absl::Status* error);
//   bool BitwiseAnd(T in1, T in2, T* out, absl::Status* error);
//   bool BitwiseBinaryOpBytes(absl::string_view in1, absl::string_view in2,
//                             string* out, absl::Status* error);
//   bool BitwiseLeftShift(T in1, int64 in2, T* out, absl::Status* error);
//   bool BitwiseLeftShiftBytes(absl::string_view in1, int64 in2, string* out,
//                              absl::Status* error);
//   bool BitwiseRightShift(T in1, int64 in2, T* out, absl::Status* error);
//   bool BitwiseRightShiftBytes(absl::string_view in1, int64 in2, string* out,
//                               absl::Status* error);
//   int64 BitCount(T in);
//
// Here T can be one of the following types: int32, int64, uint32, uint64.
// BitCount supports only int32, int64, uint64, and absl::string_view, however.
// All Bitwise functions return true on success and fill out *error and return
// false otherwise.
// Shift operations have the following semantics:
//  1. If offset is negative an error is returned.
//  2. Shifting by more than type size gives 0.
//  3. Shifting right does not do sign extension.
// BitCount returns the number of bits set in two's complement form of the
// input.

#ifndef GOOGLESQL_PUBLIC_FUNCTIONS_BITWISE_H_
#define GOOGLESQL_PUBLIC_FUNCTIONS_BITWISE_H_

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>

#include "googlesql/public/functions/bitwise_agg_mode.pb.h"
#include "googlesql/public/functions/util.h"
#include "absl/base/attributes.h"
#include "absl/base/casts.h"
#include "absl/base/optimization.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/bits.h"
#include "googlesql/base/status.h"

namespace googlesql {
namespace functions {

template <typename T>
bool BitwiseNot(T in, T* out, absl::Status* error);
bool BitwiseNotBytes(absl::string_view in, std::string* out,
                     absl::Status* error);
template <typename T>
bool BitwiseOr(T in1, T in2, T* out, absl::Status* error);
template <typename T>
bool BitwiseXor(T in1, T in2, T* out, absl::Status* error);
template <typename T>
bool BitwiseAnd(T in1, T in2, T* out, absl::Status* error);

// Op can be std::bit_and, std::bit_or, or std::bit_xor.
template <template <typename T> class Op>
bool BitwiseBinaryOpBytesWithMode(absl::string_view in1, absl::string_view in2,
                                  BitwiseAggEnums::BitwiseAggMode mode,
                                  std::string* out, absl::Status* error);

// Op can be std::bit_and, std::bit_or, or std::bit_xor. This signature always
// uses BitwiseAggEnums::STRICT mode.
template <template <typename T> class Op>
bool BitwiseBinaryOpBytes(absl::string_view in1, absl::string_view in2,
                          std::string* out, absl::Status* error);
template <typename T>
bool BitwiseLeftShift(T in1, int64_t in2, T* out, absl::Status* error);
bool BitwiseLeftShiftBytes(absl::string_view in1, int64_t in2, std::string* out,
                           absl::Status* error);
template <typename T>
bool BitwiseRightShift(T in1, int64_t in2, T* out, absl::Status* error);
bool BitwiseRightShiftBytes(absl::string_view in1, int64_t in2,
                            std::string* out, absl::Status* error);
int64_t BitCount(int32_t in);
int64_t BitCount(int64_t in);
int64_t BitCount(uint64_t in);
int64_t BitCount(absl::string_view in);

template <typename T>
bool BitwiseNot(T in, T* out, absl::Status* error) {
  *out = ~in;
  return true;
}

template <typename T>
bool BitwiseOr(T in1, T in2, T* out, absl::Status* error) {
  *out = in1 | in2;
  return true;
}

template <typename T>
bool BitwiseXor(T in1, T in2, T* out, absl::Status* error) {
  *out = in1 ^ in2;
  return true;
}

template <typename T>
bool BitwiseAnd(T in1, T in2, T* out, absl::Status* error) {
  *out = in1 & in2;
  return true;
}

template <template <typename T> class Op>
bool BitwiseBinaryOpBytesWithMode(absl::string_view in1, absl::string_view in2,
                                  BitwiseAggEnums::BitwiseAggMode mode,
                                  std::string* out, absl::Status* error) {
  std::string in1_str(in1);
  std::string in2_str(in2);

  size_t max_size = std::max(in1.size(), in2.size());

  switch (mode) {
    case BitwiseAggEnums::BITWISE_AGG_MODE_INVALID:
      internal::UpdateError(error, "Invalid bitwise aggregation mode.");
      return false;
    case BitwiseAggEnums::STRICT:
      if (in1_str.size() != in2_str.size()) {
        internal::UpdateError(
            error, absl::StrCat("Bitwise binary operator for BYTES requires "
                                "equal length of the "
                                "inputs. Got ",
                                in1.size(), " bytes on the left hand side and ",
                                in2.size(), " bytes on the right hand side."));
        return false;
      }
      break;
    case BitwiseAggEnums::PAD:
      // Pad the inputs to the same length if PAD mode is used.
      if (mode == BitwiseAggEnums::PAD) {
        std::string padded_in1(max_size, '\0');  // Pad with 0s
        std::string padded_in2(max_size, '\0');  // Pad with 0s
        std::memcpy(&padded_in1[0], in1_str.data(), in1_str.size());
        std::memcpy(&padded_in2[0], in2_str.data(), in2_str.size());
        in1_str = padded_in1;
        in2_str = padded_in2;
      }
      break;
  }

  out->resize(max_size);
  const char* in1_data = in1_str.data();
  const char* in2_data = in2_str.data();
  Op<char> op;
  for (char& c : *out) {
    c = op(*in1_data, *in2_data);
    ++in1_data;
    ++in2_data;
  }
  return true;
}

template <template <typename T> class Op>
bool BitwiseBinaryOpBytes(absl::string_view in1, absl::string_view in2,
                          std::string* out, absl::Status* error) {
  return BitwiseBinaryOpBytesWithMode<Op>(in1, in2, BitwiseAggEnums::STRICT,
                                          out, error);
}

template <typename T>
bool BitwiseLeftShift(T in1, int64_t in2, T* out, absl::Status* error) {
  typedef typename std::make_unsigned<T>::type UnsignedT;
  if (ABSL_PREDICT_FALSE(in2 < 0)) {
    internal::UpdateError(error, "Bitwise shift by negative offset.");
    return false;
  } else if (ABSL_PREDICT_FALSE(in2 >=
                                std::numeric_limits<UnsignedT>::digits)) {
    *out = 0;
    return true;
  }
  *out = static_cast<T>(static_cast<UnsignedT>(in1) << in2);
  return true;
}

template <typename T>
bool BitwiseRightShift(T in1, int64_t in2, T* out, absl::Status* error) {
  typedef typename std::make_unsigned<T>::type UnsignedT;
  if (ABSL_PREDICT_FALSE(in2 < 0)) {
    internal::UpdateError(error, "Bitwise shift by negative offset.");
    return false;
  } else if (ABSL_PREDICT_FALSE(in2 >=
                                std::numeric_limits<UnsignedT>::digits)) {
    *out = 0;
    return true;
  }
  *out = static_cast<T>(static_cast<UnsignedT>(in1) >> in2);
  return true;
}

inline int64_t BitCount(int32_t in) {
  return absl::popcount(absl::bit_cast<uint32_t>(in));
}

inline int64_t BitCount(int64_t in) {
  return absl::popcount(absl::bit_cast<uint64_t>(in));
}

inline int64_t BitCount(uint64_t in) { return absl::popcount(in); }

}  // namespace functions
}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_FUNCTIONS_BITWISE_H_
