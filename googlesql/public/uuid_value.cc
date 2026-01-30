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

#include "googlesql/public/uuid_value.h"

#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <ostream>
#include <string>
#include <utility>

#include "googlesql/common/errors.h"
#include "absl/container/flat_hash_map.h"
#include "absl/hash/hash.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/strings/substitute.h"
#include "googlesql/base/endian.h"
#include "googlesql/base/no_destructor.h"
#include "googlesql/base/status_builder.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace {

constexpr int kMaxUuidNumberOfHexDigits = 32;

// Helper function to parse a single hexadecimal block of a UUID.
// A hexadecimal block is a 16-digit hexadecimal number, which is represented
// as 8 bytes.
absl::StatusOr<uint64_t> ParseHexBlock(absl::string_view& str,
                                       absl::string_view original_str) {
  constexpr int kMaxUuidBlockLength = 16;
  static const googlesql_base::NoDestructor<absl::flat_hash_map<char, uint8_t>>
      kCharToHexMap({{'0', 0x00}, {'1', 0x01}, {'2', 0x02}, {'3', 0x03},
                     {'4', 0x04}, {'5', 0x05}, {'6', 0x06}, {'7', 0x07},
                     {'8', 0x08}, {'9', 0x09}, {'a', 0x0a}, {'b', 0x0b},
                     {'c', 0x0c}, {'d', 0x0d}, {'e', 0x0e}, {'f', 0x0f},
                     {'A', 0x0a}, {'B', 0x0b}, {'C', 0x0c}, {'D', 0x0d},
                     {'E', 0x0e}, {'F', 0x0f}});
  uint64_t block = 0;
  for (int j = 0; j < kMaxUuidBlockLength; ++j) {
    absl::ConsumePrefix(&str, "-");
    if (str.empty()) {
      return MakeEvalError() << absl::Substitute(
                 "Invalid input: '$0'. UUID must be at least $1 characters "
                 "long.",
                 original_str, kMaxUuidNumberOfHexDigits);
    } else if (str[0] == '-') {
      // Check for consecutive hyphens
      return MakeEvalError() << absl::Substitute(
                 "Invalid input: '$0'. UUID cannot have multiple "
                 "consecutive hyphens (-).",
                 original_str);
    }

    auto it = kCharToHexMap->find(str[0]);
    if (it == kCharToHexMap->end()) {
      return MakeEvalError() << absl::Substitute(
                 "Invalid input: '$0'. UUID cannot have '$1' character.",
                 original_str, str[0]);
    }
    block = (block << 4) + it->second;
    str.remove_prefix(1);
  }
  return block;
}
}  // namespace

UuidValue UuidValue::MaxValue() {
  return UuidValue(std::numeric_limits<uint64_t>::max(),
                   std::numeric_limits<uint64_t>::max());
}

UuidValue UuidValue::MinValue() {
  return UuidValue(std::numeric_limits<uint64_t>::min(),
                   std::numeric_limits<uint64_t>::min());
}

absl::StatusOr<UuidValue> UuidValue::FromString(absl::string_view str) {
  // Early checks for invalid length or leading hyphen
  if (str.size() < kMaxUuidNumberOfHexDigits) {
    return MakeEvalError() << "Invalid input: '" << str
                           << "'. UUID must be at least "
                           << kMaxUuidNumberOfHexDigits << " characters long.";
  }
  absl::string_view original_str = str;
  // Check and remove optional braces
  bool hasBraces = (str[0] == '{');
  if (hasBraces) {
    if (str[str.size() - 1] != '}') {
      return MakeEvalError()
             << "Invalid input: '" << original_str
             << "'. Mismatched curly braces in UUID, missing closing '}'.";
    }
    str.remove_prefix(1);
    str.remove_suffix(1);
  }

  // Check for leading hyphen after braces.
  if (str[0] == '-') {
    return MakeEvalError() << "Invalid input: '" << original_str
                           << "'. UUID cannot start with a hyphen (-).";
  }

  GOOGLESQL_ASSIGN_OR_RETURN(uint64_t highBits, ParseHexBlock(str, original_str));

  GOOGLESQL_ASSIGN_OR_RETURN(uint64_t lowBits, ParseHexBlock(str, original_str));

  if (!str.empty()) {
    return MakeEvalError() << "Invalid input: '" << original_str << "'. "
                           << str.size()
                           << " extra characters found after parsing UUID.";
  }

  return UuidValue(highBits, lowBits);
}

UuidValue UuidValue::FromPackedInt(__int128 value) {
  UuidValue ret(value);
  return ret;
}

constexpr UuidValue::UuidValue(__int128 value)
    : high_bits_(static_cast<__int128>(value) >> 64),
      low_bits_(value & std::numeric_limits<uint64_t>::max()) {}

UuidValue::UuidValue(uint64_t high_bits, uint64_t low_bits)
    : high_bits_(high_bits), low_bits_(low_bits) {}

bool UuidValue::operator==(const UuidValue& rh) const {
  return as_packed_int() == rh.as_packed_int();
}

bool UuidValue::operator!=(const UuidValue& rh) const {
  return as_packed_int() != rh.as_packed_int();
}

bool UuidValue::operator<(const UuidValue& rh) const {
  if (high_bits_ == rh.high_bits_) {
    return low_bits_ < rh.low_bits_;
  }
  return high_bits_ < rh.high_bits_;
}

bool UuidValue::operator>(const UuidValue& rh) const {
  if (high_bits_ == rh.high_bits_) {
    return low_bits_ > rh.low_bits_;
  }
  return high_bits_ > rh.high_bits_;
}

bool UuidValue::operator>=(const UuidValue& rh) const {
  if (high_bits_ == rh.high_bits_) {
    return low_bits_ >= rh.low_bits_;
  }
  return high_bits_ >= rh.high_bits_;
}

bool UuidValue::operator<=(const UuidValue& rh) const {
  if (high_bits_ == rh.high_bits_) {
    return low_bits_ <= rh.low_bits_;
  }
  return high_bits_ <= rh.high_bits_;
}

__int128 UuidValue::as_packed_int() const {
  return (static_cast<__int128>(high_bits_) << 64) + low_bits_;
}

std::string UuidValue::ToString() const {
  std::string result;
  AppendToString(&result);
  return result;
}

size_t UuidValue::HashCode() const {
  return absl::Hash<std::pair<uint64_t, uint64_t>>()(
      std::make_pair(this->high_bits_, this->low_bits_));
}

void UuidValue::AppendToString(std::string* output) const {
  constexpr int kUuidStringLen = 36;
  constexpr int kHyphenPos[] = {8, 13, 18, 23};
  auto to_hex = [](uint64_t v, int start_index, int end_index, char* out) {
    static constexpr char hex_char[] = {'0', '1', '2', '3', '4', '5', '6', '7',
                                        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    for (int i = start_index; i >= end_index; --i) {
      *out++ = hex_char[(v >> (i * 4)) & 0xf];
    }
  };
  output->resize(output->size() + kUuidStringLen);
  char* target = &((*output)[output->size() - kUuidStringLen]);
  to_hex(high_bits_, 15, 8, target);
  *(target + kHyphenPos[0]) = '-';
  to_hex(high_bits_, 7, 4, target + kHyphenPos[0] + 1);
  *(target + kHyphenPos[1]) = '-';
  to_hex(high_bits_, 3, 0, target + kHyphenPos[1] + 1);
  *(target + kHyphenPos[2]) = '-';
  to_hex(low_bits_, 15, 12, target + kHyphenPos[2] + 1);
  *(target + kHyphenPos[3]) = '-';
  to_hex(low_bits_, 11, 0, target + kHyphenPos[3] + 1);
}

void UuidValue::SerializeAndAppendToBytes(std::string* bytes) const {
  int64_t high_bits = googlesql_base::BigEndian::FromHost64(high_bits_);
  bytes->append(reinterpret_cast<const char*>(&high_bits), sizeof(high_bits));
  int64_t low_bits = googlesql_base::BigEndian::FromHost64(low_bits_);
  bytes->append(reinterpret_cast<const char*>(&low_bits), sizeof(low_bits));
}

absl::StatusOr<UuidValue> UuidValue::DeserializeFromBytes(
    absl::string_view bytes) {
  if (bytes.size() != sizeof(UuidValue)) {
    return absl::OutOfRangeError(absl::StrCat(
        "Invalid serialized UUID size, expected ", sizeof(UuidValue),
        " bytes, but got ", bytes.size(), " bytes."));
  }
  const char* data = bytes.data();
  UuidValue uuid;
  uuid.high_bits_ = googlesql_base::BigEndian::Load<uint64_t>(data);
  uuid.low_bits_ = googlesql_base::BigEndian::Load<uint64_t>(data + sizeof(uuid.high_bits_));
  return uuid;
}

std::ostream& operator<<(std::ostream& out, UuidValue value) {
  return out << value.ToString();
}
}  // namespace googlesql
