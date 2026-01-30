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

#include "googlesql/common/multiprecision_int_impl.h"

#include <cstdint>
#include <string>

namespace googlesql {
namespace multiprecision_int_impl {

template <typename UnsignedWord, int kNumberDigits>
inline int PrintDigits(UnsignedWord digits, bool skip_leading_zeros,
                       char output[kNumberDigits]) {
  for (int j = kNumberDigits - 1; j >= 0; --j) {
    output[j] = static_cast<char>(digits % 10) + '0';
    digits /= 10;
    if (skip_leading_zeros && digits == 0) {
      memmove(output, output + j, kNumberDigits - j);
      return kNumberDigits - j;
    }
  }
  return kNumberDigits;
}

template <typename UnsignedWord>
void AppendSegmentsToString(const UnsignedWord segments[], size_t num_segments,
                            std::string* result) {
  if (num_segments == 0) {
    result->push_back('0');
    return;
  }
  size_t old_size = result->size();
  constexpr UnsignedWord kBitWidth =
      static_cast<UnsignedWord>(sizeof(UnsignedWord) * 8);
  constexpr int kMaxWholeDecimalDigits =
      IntTraits<kBitWidth>::kMaxWholeDecimalDigits;
  size_t new_size = old_size + num_segments * kMaxWholeDecimalDigits;
  result->resize(new_size);
  char* output = result->data() + old_size;
  const UnsignedWord* segment = &segments[num_segments - 1];
  int num_digits_in_first_segment =
      PrintDigits<UnsignedWord, kMaxWholeDecimalDigits>(
          *segment, /* skip_leading_zeros */ true, output);
  output += num_digits_in_first_segment;
  while (segment != segments) {
    --segment;
    output += PrintDigits<UnsignedWord, kMaxWholeDecimalDigits>(
        *segment, /* skip_leading_zeros */ false, output);
  }
  result->resize(new_size -
                 (kMaxWholeDecimalDigits - num_digits_in_first_segment));
}

template void AppendSegmentsToString<uint64_t>(const uint64_t segments[],
                                               size_t num_segments,
                                               std::string* result);
template void AppendSegmentsToString<uint32_t>(const uint32_t segments[],
                                               size_t num_segments,
                                               std::string* result);

}  // namespace multiprecision_int_impl
}  // namespace googlesql
