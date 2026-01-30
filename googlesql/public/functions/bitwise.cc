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

#include "googlesql/public/functions/bitwise.h"

#include <stddef.h>
#include <string.h>
#include <sys/types.h>

#include <cstdint>
#include <string>

#include "googlesql/base/bits.h"
#include "googlesql/base/status.h"

namespace googlesql {
namespace functions {

bool BitwiseNotBytes(absl::string_view in, std::string* out,
                     absl::Status* error) {
  out->resize(in.size());
  const char* in_data = in.data();
  for (char& c : *out) {
    c = ~(*in_data);
    ++in_data;
  }
  return true;
}

bool BitwiseLeftShiftBytes(absl::string_view in1, int64_t in2, std::string* out,
                           absl::Status* error) {
  if (ABSL_PREDICT_FALSE(in2 < 0)) {
    internal::UpdateError(error, "Bitwise shift by negative offset.");
    return false;
  }
  out->clear();
  out->resize(in1.size());
  const int64_t shift_bytes = in2 / 8;
  if (ABSL_PREDICT_TRUE(shift_bytes < in1.size())) {
    const size_t bytes_to_copy = in1.size() - shift_bytes;
    // Only use unsigned type here; value conversion from uint8 or uint to char
    // depends on the compiler implementation, according to
    // http://en.cppreference.com/w/cpp/language/implicit_conversion.
    uint8_t* out_data = reinterpret_cast<uint8_t*>(&(*out)[0]);
    const uint8_t shift_bits_across_bytes = in2 % 8;
    if (shift_bits_across_bytes == 0) {
      memcpy(out_data, in1.data() + shift_bytes, bytes_to_copy);
    } else {
      uint c = 0;
      const uint8_t* in_data =
          reinterpret_cast<const uint8_t*>(in1.data()) + in1.size();
      for (uint8_t* p = out_data + bytes_to_copy; --p >= out_data; c >>= 8) {
        --in_data;
        c |= static_cast<uint>(*in_data) << shift_bits_across_bytes;
        *p = static_cast<uint8_t>(c);
      }
    }
  }
  return true;
}

bool BitwiseRightShiftBytes(absl::string_view in1, int64_t in2,
                            std::string* out, absl::Status* error) {
  if (ABSL_PREDICT_FALSE(in2 < 0)) {
    internal::UpdateError(error, "Bitwise shift by negative offset.");
    return false;
  }
  out->clear();
  out->resize(in1.size());
  const int64_t shift_bytes = in2 / 8;
  if (ABSL_PREDICT_TRUE(shift_bytes < in1.size())) {
    const size_t bytes_to_copy = in1.size() - shift_bytes;
    // Only use unsigned type here; value conversion from uint8 or uint to char
    // depends on the compiler implementation, according to
    // http://en.cppreference.com/w/cpp/language/implicit_conversion.
    uint8_t* out_data = reinterpret_cast<uint8_t*>(&(*out)[shift_bytes]);
    const uint8_t* in_data = reinterpret_cast<const uint8_t*>(in1.data());
    const uint8_t shift_bits_across_bytes = in2 % 8;
    if (shift_bits_across_bytes == 0) {
      memcpy(out_data, in_data, bytes_to_copy);
    } else {
      uint c = 0;
      for (uint8_t* out_end = out_data + bytes_to_copy; out_data < out_end;
           ++out_data, ++in_data, c <<= 8) {
        c |= *in_data;
        *out_data = static_cast<uint8_t>(c >> shift_bits_across_bytes);
      }
    }
  }
  return true;
}

int64_t BitCount(absl::string_view in) {
  int64_t result = 0;
  for (unsigned char c : in) {
    result += absl::popcount(c);
  }
  return result;
}
}  // namespace functions
}  // namespace googlesql

