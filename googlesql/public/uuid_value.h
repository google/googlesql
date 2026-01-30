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

#ifndef GOOGLESQL_PUBLIC_UUID_VALUE_H_
#define GOOGLESQL_PUBLIC_UUID_VALUE_H_

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>

#include "absl/base/port.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace googlesql {

// This class represents values of the GoogleSQL UUID type. Such values are
// stored as fixed size 16-byte values (and often represented as 32-digit
// hexadecimal strings).
//
// Internally UUID values are stored using two 64-bit integers.
class UuidValue final {
 public:
  // Returns the maximum and minimum UUID values.
  // Minimum UUID value is 00000000-0000-0000-0000-000000000000.
  // Maximum UUID value is ffffffff-ffff-ffff-ffff-ffffffffffff.
  static UuidValue MaxValue();
  static UuidValue MinValue();

  // Parses a textual representation of a UUID value. Returns an error if the
  // given string cannot be parsed as a UUID value.
  //
  // This method accepts the UUID value in following formats:
  // 1. Permitting upper-case & lower-case characters
  // 2. Permitting braces around the UUID string
  // 3. Permitting single hyphens between any pair of hex-digits
  // For examples: following string representations are allowed -
  // a0eebc999c0b4ef8bb6d6bb9bd380a11 - hex-digits without any hyphen.
  // A0eeBc99-9c0b-4ef8-bb6d-6Bb9bd380a11 - combination of upper & lower cases.
  // {a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11} - braces around the string value.
  // {a-0e-eb-c99-9c-0-b-4e-f-8-bb6-d-6b-b9bd3-80-a11} - single hyphen between
  // any pair of hex-digits.
  static absl::StatusOr<UuidValue> FromString(absl::string_view str);

  // Constructs a UUID object using its packed representation.
  static UuidValue FromPackedInt(__int128 value);

  // Default constructor, constructs a zero value -
  // UuidValue(00000000-0000-0000-0000-000000000000).
  constexpr UuidValue() = default;
  UuidValue(const UuidValue&) = default;
  UuidValue(UuidValue&&) = default;
  UuidValue& operator=(const UuidValue&) = default;
  UuidValue& operator=(UuidValue&&) = default;

  // Comparison operators.
  bool operator==(const UuidValue& rh) const;
  bool operator!=(const UuidValue& rh) const;
  bool operator<(const UuidValue& rh) const;
  bool operator>(const UuidValue& rh) const;
  bool operator>=(const UuidValue& rh) const;
  bool operator<=(const UuidValue& rh) const;

  // Converts the UUID value into a lowercase string formatted as follows:
  // [8 hex-digits]-[4 hex-digits]-[4 hex-digits]-[4 hex-digits]-[12 hex-digits]
  // Example: a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11
  std::string ToString() const;

  // Appends a lowercase string representation of this UUID to the `output`
  // string.
  void AppendToString(std::string* output) const;

  // Returns the packed UUID value.
  __int128 as_packed_int() const;

  // Serializes the UUID into a fixed-size 16-byte representation and appends
  // it directly to the provided 'bytes' string.
  void SerializeAndAppendToBytes(std::string* bytes) const;

  // Serializes the UUID into a fixed-size 16-byte representation and returns
  // it as a string.
  std::string SerializeAsBytes() const {
    std::string result;
    SerializeAndAppendToBytes(&result);
    return result;
  }

  // Deserializes a UUID from its fixed-size 16-byte representation. Returns an
  // error if the given input doesn't contains exactly 16 bytes of data.
  static absl::StatusOr<UuidValue> DeserializeFromBytes(
      absl::string_view bytes);

  // Returns hash code for the value.
  size_t HashCode() const;

  template <typename H>
  friend H AbslHashValue(H h, const UuidValue& v) {
    return H::combine(std::move(h), v.high_bits_, v.low_bits_);
  }

 private:
  explicit constexpr UuidValue(__int128 value);
  UuidValue(uint64_t high_bits, uint64_t low_bits);
  // A UUID value is stored as a scaled integer. The intended representation is
  // __int128, but since __int128 causes crashes for loads and stores that are
  // not 16-byte aligned, it is split into two 64-bit components here.
  uint64_t high_bits_ = 0;
  uint64_t low_bits_ = 0;
};
static_assert(sizeof(class UuidValue) == 16, "Enforce 16-byte UUID values.");

// Allow UUID values to be logged.
std::ostream& operator<<(std::ostream& out, UuidValue value);

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_UUID_VALUE_H_
