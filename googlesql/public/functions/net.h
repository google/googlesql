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

#ifndef GOOGLESQL_PUBLIC_FUNCTIONS_NET_H_
#define GOOGLESQL_PUBLIC_FUNCTIONS_NET_H_

#include <cstdint>
#include <string>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace googlesql {
namespace functions {
namespace net {

// See (broken link) for full spec and examples for IP functions.

// NET.FORMAT_IP(int64) -> String.
// DEPRECATED. Use IPToString(IPv4FromInt64()) instead.
// Takes a host-byte-order 32 bit integer and transforms it into a dotted
// quad string representation of the same IP address. If <in> is negative
// or larger than std::numeric_limits<uint32>::max() false is returned and
// <*error> will be set to OUT_OF_RANGE.
bool FormatIP(int64_t in, std::string* out, absl::Status* error);

// NET.IPV4_FROM_INT64(int64) -> Bytes.
// Takes a host-byte-order integer and transforms it into binary
// representation in network-byte-order.
// Different from FormatIP, this function allows the high 33 bits in <in> to
// be all ones (i.e, the valid range is [-0x80000000, 0xFFFFFFFF]), to handle
// the case where <in> is implicitly cast from an int32. If <in> is out of
// this valid range, false is returned and <*error> will be set to OUT_OF_RANGE.
bool IPv4FromInt64(int64_t in, std::string* out, absl::Status* error);

// NET.PARSE_IP(String) -> int64.
// DEPRECATED. Use IPv4ToInt64(IPFromString()) instead.
// Takes a dotted-quad IP address string and returns in <*out> a
// host-byte-order 32-bit integer representation of the same IPv4 address. If
// <in> is not a valid, dotted-quad IP address, false is returned and <*error>
// will be set to OUT_OF RANGE.
bool ParseIP(absl::string_view in, int64_t* out, absl::Status* error);

// NET.IPV4_TO_INT64(Bytes) -> int64.
// Takes an IPv4 address in binary representation in network-byte-order and
// transforms it into a host-byte-order integer in the range [0, 0xFFFFFFFF].
// If the length of <in> is not 4, false is returned and <*error> will be set
// to OUT_OF_RANGE.
bool IPv4ToInt64(absl::string_view in, int64_t* out, absl::Status* error);

// NET.FORMAT_PACKED_IP(Bytes) -> String.
// DEPRECATED. Equivalent to IPToString.
bool FormatPackedIP(absl::string_view in, std::string* out,
                    absl::Status* error);

// NET.IP_TO_STRING(Bytes) -> String.
// Takes an IPv4 or IPv6 address in binary representation in network-byte-order
// and transforms it to text representation (dotted-quad for IPv4 or colon-
// separated for IPv6). If the length of <in> is not 4 or 16, false is returned
// and <*error> will be set to OUT_OF RANGE.
bool IPToString(absl::string_view in, std::string* out, absl::Status* error);

// NET.PARSE_PACKED_IP(String) -> Bytes.
// DEPRECATED. Use IPFromString instead.
// Takes a string representation of an IPv4 (dotted-quad) or IPv6
// (colon-separated) address and returns in <*out> the 4 or 16 byte packed form
// of that address. <in> is truncated at the first '\0'. If the truncated input
// cannot be parsed as an IPv4 or IPv6 address, false is returned and <*error>
// will be set to OUT_OF RANGE.
bool ParsePackedIP(absl::string_view in, std::string* out, absl::Status* error);

// NET.IP_FROM_STRING(String) -> Bytes.
// Takes a string representation of an IPv4 (dotted-quad) or IPv6
// (colon-separated) address and returns in <*out> the 4 or 16 byte packed form
// of that address. If <in> cannot be parsed as an IPv4 or IPv6 address
// (including the case where <in> contains '\0'), false is returned and
// <*error> will be set to OUT_OF RANGE.
bool IPFromString(absl::string_view in, std::string* out, absl::Status* error);

// NET.SAFE_IP_FROM_STRING(String) -> Bytes.
// Like IPFromString, but produces NULL in case of invalid input.
// Always returns absl::Status::OK.
absl::Status SafeIPFromString(absl::string_view in, std::string* out,
                              bool* is_null);

// NET.IP_NET_MASK(Int64, Int64) -> Bytes.
// Returns a subnet mask with <output_length_bytes> bytes and with the leftmost
// (most significant) <prefix_length_bits> bits set to 1 and other bits set to
// 0. If <output_length_bytes> is not 4 or 16, or if <prefix_length_bits> is
// negative or greater than <output_length_bytes> * 8, false is returned and
// <*error> will be set to OUT_OF RANGE.
bool IPNetMask(int64_t output_length_bytes, int64_t prefix_length_bits,
               std::string* out, absl::Status* error);

// NET.IP_TRUNC(Bytes, Int64) -> Bytes.
// Takes an IPv4 or IPv6 address in binary representation in network-byte-order
// and sets all except the leftmost (most significant) <prefix_length_bits> bits
// to 0. The output length is same as the input length. If the length of <in> is
// not 4 or 16, false is returned and <*error> will be set to OUT_OF RANGE.
bool IPTrunc(absl::string_view in, int64_t prefix_length_bits, std::string* out,
             absl::Status* error);

// NET.IP_IN_NET(String, String) -> bool.
// Takes a string representation of an IPv4 or IPv6 address and a subnet in CIDR
// notation (e.g. 10.0.0.0/8 or 1234::/16) and returns true in <*out> if the ip
// address is contained within the subnet. If either <ip> or <net> fail to
// parse, false is returned from the function and <*error> will be set to
// OUT_OF RANGE.
bool IPInNet(absl::string_view ip, absl::string_view net, bool* out,
             absl::Status* error);

// NET.MAKE_NET(String, int32) -> String.
// Takes a string representation of an IPv4 or IPv6 address (or CIDR subnet) and
// returns in <*out> a CIDR subnet representation truncated to <subnet_size>
// bits. For example NET.MAKE_NET("192.168.1.1", 16) -> "192.168.0.0/16". If
// <ip_string> fails to parse, or <size> is negative or wider than the subnet
// specified in <ip_string>, false is returned from the function and <*error> is
// set to OUT_OF_RANGE.
bool MakeNet(absl::string_view ip_string, int32_t subnet_size, std::string* out,
             absl::Status* error);

// The following 3 functions (Host, RegDomain, and PublicSuffix) always return
// OK, and set *is_null to out->empty().
// *out is valid as long as the input is valid.
// See (broken link) for the full spec and examples.

// NET.HOST(String) -> String.
// Takes a url and extracts the host component.
absl::Status Host(absl::string_view url, absl::string_view* out, bool* is_null);

// NET.REG_DOMAIN(String) -> String.
// Takes a url and extracts the registered or registrable domain.
absl::Status RegDomain(absl::string_view url, absl::string_view* out,
                       bool* is_null);

// NET.PUBLIC_SUFFIX(String) -> String.
// Takes a url and extracts the public suffix.
absl::Status PublicSuffix(absl::string_view url, absl::string_view* out,
                          bool* is_null);

}  // namespace net
}  // namespace functions
}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_FUNCTIONS_NET_H_
