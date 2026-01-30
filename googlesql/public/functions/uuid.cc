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

#include "googlesql/public/functions/uuid.h"

#include <cstdint>
#include <string>

#include "googlesql/public/uuid_value.h"
#include "absl/random/bit_gen_ref.h"
#include "absl/random/distributions.h"
#include "absl/strings/string_view.h"

namespace googlesql {
namespace functions {

constexpr absl::string_view kInvalidRepr = "Uid::kInvalid";

static std::string GenerateUuid(uint64_t high, uint64_t low) {
  if (high == 0 && low == 0) return std::string(kInvalidRepr);

  auto to_hex = [](uint64_t v, int num_chars, char *out) {
    static constexpr char hex_char[] = {'0', '1', '2', '3', '4', '5', '6', '7',
                                        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    for (int i = num_chars - 1; i >= 0; --i) {
      *out++ = hex_char[(v >> (i * 4)) & 0xf];
    }
  };
  uint32_t time_low = low & 0xffffffff;
  uint32_t time_mid = (low >> 32) & 0xffff;
  uint32_t time_high = (low >> 48) & 0xffff;
  uint32_t sequence = high & 0xffff;
  uint64_t node = (high >> 16) & 0xffffffffffffULL;
  char buf[36];
  char *ptr = buf;
  to_hex(time_low, 8, ptr);
  ptr += 8;
  *ptr++ = '-';
  to_hex(time_mid, 4, ptr);
  ptr += 4;
  *ptr++ = '-';
  to_hex(time_high, 4, ptr);
  ptr += 4;
  *ptr++ = '-';
  to_hex(sequence, 4, ptr);
  ptr += 4;
  *ptr++ = '-';
  to_hex(node, 12, ptr);
  return std::string(buf, 36);
}

std::string GenerateUuid(absl::BitGenRef gen) {
  uint64_t low = absl::Uniform<uint64_t>(gen);
  uint64_t high = absl::Uniform<uint64_t>(gen);

  // The byte format for high and low uint64 used by V4 Uuid for string form
  // "AAAAAAAA-BBBB-4CCC-yDDD-EEEEEEEEEEEE" is specified as:
  //  low:  4CCCBBBBAAAAAAAA
  //  high: EEEEEEEEEEEEyDDD
  //
  // Use bit masks to fit the pattern described in the header file into the byte
  // format above. All of instances of ABCDE can be filled with random bits.
  low = (low & 0x0fffffffffffffffULL) | 0x4000000000000000ULL;
  high = (high & 0xffffffffffffbfffULL) | 0x8000ULL;

  return GenerateUuid(high, low);
}

UuidValue NewUuid(absl::BitGenRef gen) {
  uint64_t high = absl::Uniform<uint64_t>(gen);
  uint64_t low = absl::Uniform<uint64_t>(gen);
  // The byte format for high and low uint64 used by V4 Uuid for string form
  // "AAAAAAAA-BBBB-4CCC-yDDD-EEEEEEEEEEEE" is specified as:
  //  high: AAAAAAAABBBB4CCC
  //  low: yDDDEEEEEEEEEEEE
  //
  // Use bit masks to fit the pattern described in the header file into the byte
  // format above. All of instances of ABCDE can be filled with random bits.
  high = (high & 0xffffffffffff0fffULL) | 0x4000ULL;
  low = (low & 0xbfffffffffffffffULL) | 0x8000000000000000ULL;
  return UuidValue::FromPackedInt((static_cast<__int128>(high) << 64) + low);
}

}  // namespace functions
}  // namespace googlesql
