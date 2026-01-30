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

#include <vector>

#include "googlesql/compliance/functions_testlib.h"
#include "googlesql/testing/test_function.h"
#include "googlesql/testing/using_test_value.cc"
#include "googlesql/base/map_util.h"
#include "googlesql/base/status.h"

namespace googlesql {
namespace {
constexpr absl::StatusCode OUT_OF_RANGE = absl::StatusCode::kOutOfRange;
}  // namespace

std::vector<FunctionTestCall> GetFunctionTestsNet() {
  std::vector<FunctionTestCall> result = {
      // NET.FORMAT_IP.
      {"net.format_ip", {0ll}, "0.0.0.0"},
      {"net.format_ip", {180332553ll}, "10.191.168.9"},
      {"net.format_ip", {2147483647ll}, "127.255.255.255"},
      {"net.format_ip", {2147483648ll}, "128.0.0.0"},
      {"net.format_ip", {4294967295ll}, "255.255.255.255"},
      {"net.format_ip", {4294967296ll}, NullString(), OUT_OF_RANGE},
      {"net.format_ip", {NullInt64()}, NullString()},
      {"net.format_ip", {1ll << 33}, NullString(), OUT_OF_RANGE},
      {"net.format_ip", {-1ll}, NullString(), OUT_OF_RANGE},

      // NET.IPV4_FROM_INT64.
      {"net.ipv4_from_int64", {0LL}, Bytes("\0\0\0\0")},
      {"net.ipv4_from_int64", {1LL}, Bytes("\0\0\0\1")},
      {"net.ipv4_from_int64", {0x13579BDFLL}, Bytes("\x13\x57\x9B\xDF")},
      {"net.ipv4_from_int64", {0x7FFFFFFFLL}, Bytes("\x7F\xFF\xFF\xFF")},
      {"net.ipv4_from_int64", {0x80000000LL}, Bytes("\x80\x00\x00\x00")},
      {"net.ipv4_from_int64", {-0x80000000LL}, Bytes("\x80\x00\x00\x00")},
      {"net.ipv4_from_int64", {0xFFFFFFFFLL}, Bytes("\xFF\xFF\xFF\xFF")},
      {"net.ipv4_from_int64", {-1LL}, Bytes("\xFF\xFF\xFF\xFF")},
      {"net.ipv4_from_int64", {NullInt64()}, NullBytes()},
      {"net.ipv4_from_int64", {0x100000000LL}, NullBytes(), OUT_OF_RANGE},
      {"net.ipv4_from_int64", {-0x80000001LL}, NullBytes(), OUT_OF_RANGE},

      // NET.PARSE_IP.
      {"net.parse_ip", {"0.0.0.0"}, 0ll},
      {"net.parse_ip", {"10.191.168.9"}, 180332553ll},
      {"net.parse_ip", {"127.255.255.255"}, 2147483647ll},
      {"net.parse_ip", {"128.0.0.0"}, 2147483648ll},
      {"net.parse_ip", {"255.255.255.255"}, 4294967295ll},
      {"net.parse_ip", {"255.255.255.256"}, NullInt64(), OUT_OF_RANGE},
      {"net.parse_ip", {"256.0.0.0"}, NullInt64(), OUT_OF_RANGE},
      {"net.parse_ip", {NullString()}, NullInt64()},
      {"net.parse_ip", {"::1"}, NullInt64(), OUT_OF_RANGE},
      {"net.parse_ip", {"180332553"}, NullInt64(), OUT_OF_RANGE},

      // NET.IPV4_TO_INT64.
      {"net.ipv4_to_int64", {Bytes("\0\0\0\0")}, 0LL},
      {"net.ipv4_to_int64", {Bytes("\0\0\0\1")}, 1LL},
      {"net.ipv4_to_int64", {Bytes("\x13\x57\x9B\xDF")}, 0x13579BDFLL},
      {"net.ipv4_to_int64", {Bytes("\x7F\xFF\xFF\xFF")}, 0x7FFFFFFFLL},
      {"net.ipv4_to_int64", {Bytes("\x80\x00\x00\x00")}, 0x80000000LL},
      {"net.ipv4_to_int64", {Bytes("\xFF\xFF\xFF\xFF")}, 0xFFFFFFFFLL},
      {"net.ipv4_to_int64", {NullBytes()}, NullInt64()},
      {"net.ipv4_to_int64", {Bytes("")}, NullInt64(), OUT_OF_RANGE},
      {"net.ipv4_to_int64", {Bytes("\0\0\0")}, NullInt64(), OUT_OF_RANGE},
      {"net.ipv4_to_int64", {Bytes("\0\0\0\0\0")}, NullInt64(), OUT_OF_RANGE},
      {"net.ipv4_to_int64",
       {Bytes("\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0")},
       NullInt64(),
       OUT_OF_RANGE},

      // NET.PARSE_PACKED_IP.
      //    IPv4 Success.
      {"net.parse_packed_ip", {"64.65.66.67"}, Bytes("@ABC")},
      {"net.parse_packed_ip", {"64.0.66.67"}, Bytes("@\0BC")},
      //    IPv4 Failure.
      {"net.parse_packed_ip", {"64.65.66.67 "}, NullBytes(), OUT_OF_RANGE},
      {"net.parse_packed_ip", {" 64.65.66.67"}, NullBytes(), OUT_OF_RANGE},
      //    IPv6 Success.
      {"net.parse_packed_ip",
       {"6465:6667:6869:6a6b:6c6d:6e6f:7071:7172"},
       Bytes("defghijklmnopqqr")},
      {"net.parse_packed_ip",
       {"6465:6667:6869:6A6B:6C6d:6e6f:7071:7172"},
       Bytes("defghijklmnopqqr")},
      {"net.parse_packed_ip",
       {"::1"},
       Bytes("\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\1")},
      {"net.parse_packed_ip",
       {"::ffff:64.65.66.67"},
       Bytes("\0\0\0\0\0\0\0\0\0\0\xff\xff@ABC")},
      //    IPv6 Failure.
      {"net.parse_packed_ip",
       {" 6465:6667:6869:6a6b:6c6d:6e6f:7071:7172"},
       NullBytes(),
       OUT_OF_RANGE},
      {"net.parse_packed_ip",
       {"6465:6667:6869:6a6b:6c6d:6e6f:7071:7172 "},
       NullBytes(),
       OUT_OF_RANGE},
      //    NULL input.
      {"net.parse_packed_ip", {NullString()}, NullBytes()},
      //    Input failure.
      {"net.parse_packed_ip", {"foo"}, NullBytes(), OUT_OF_RANGE},

      // NET.FORMAT_PACKED_IP.
      //    IPv4 Success.
      {"net.format_packed_ip", {Bytes("@ABC")}, "64.65.66.67"},
      {"net.format_packed_ip", {Bytes("@\0BC")}, "64.0.66.67"},
      //    IPv6 Success.
      {"net.format_packed_ip",
       {Bytes("defghijklmnopqqr")},
       "6465:6667:6869:6a6b:6c6d:6e6f:7071:7172"},
      {"net.format_packed_ip",
       {Bytes("defg\0ijklmnopqqr")},
       "6465:6667:69:6a6b:6c6d:6e6f:7071:7172"},
      {"net.format_packed_ip",
       {Bytes("\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\1")},
       "::1"},
      //    NULL input.
      {"net.format_packed_ip", {NullBytes()}, NullString()},
      //    Input failure.
      {"net.format_packed_ip", {Bytes("foo")}, NullString(), OUT_OF_RANGE},
      {"net.format_packed_ip", {Bytes("foobar")}, NullString(), OUT_OF_RANGE},

      // NET.IP_TO_STRING.
      //    IPv4 Success.
      {"net.ip_to_string", {Bytes("@ABC")}, "64.65.66.67"},
      {"net.ip_to_string", {Bytes("@\0BC")}, "64.0.66.67"},
      //    IPv6 Success.
      {"net.ip_to_string",
       {Bytes("defghijklmnopqqr")},
       "6465:6667:6869:6a6b:6c6d:6e6f:7071:7172"},
      {"net.ip_to_string",
       {Bytes("defg\0ijklmnopqqr")},
       "6465:6667:69:6a6b:6c6d:6e6f:7071:7172"},
      {"net.ip_to_string", {Bytes("\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\1")}, "::1"},
      //    NULL input.
      {"net.ip_to_string", {NullBytes()}, NullString()},
      //    Input failure.
      {"net.ip_to_string", {Bytes("foo")}, NullString(), OUT_OF_RANGE},
      {"net.ip_to_string", {Bytes("foobar")}, NullString(), OUT_OF_RANGE},

      // NET.IP_NET_MASK.
      {"net.ip_net_mask", {4LL, 32LL}, Bytes("\xFF\xFF\xFF\xFF")},
      {"net.ip_net_mask", {4LL, 28LL}, Bytes("\xFF\xFF\xFF\xF0")},
      {"net.ip_net_mask", {4LL, 24LL}, Bytes("\xFF\xFF\xFF\x00")},
      {"net.ip_net_mask", {4LL, 17LL}, Bytes("\xFF\xFF\x80\x00")},
      {"net.ip_net_mask", {4LL, 15LL}, Bytes("\xFF\xFE\x00\x00")},
      {"net.ip_net_mask", {4LL, 10LL}, Bytes("\xFF\xC0\x00\x00")},
      {"net.ip_net_mask", {4LL, 0LL}, Bytes("\x00\x00\x00\x00")},
      {"net.ip_net_mask", {16LL, 128LL}, Bytes(std::string(16, '\xFF'))},
      {"net.ip_net_mask",
       {16LL, 100LL},
       Bytes("\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF"
             "\xFF\xFF\xFF\xFF\xF0\x00\x00\x00")},
      {"net.ip_net_mask", {16LL, 0LL}, Bytes(std::string(16, '\0'))},
      //    NULL input.
      {"net.ip_net_mask", {NullInt64(), 0LL}, NullBytes()},
      {"net.ip_net_mask", {4LL, NullInt64()}, NullBytes()},
      //    Input failure.
      {"net.ip_net_mask", {-4LL, 0LL}, NullBytes(), OUT_OF_RANGE},
      {"net.ip_net_mask", {0LL, 0LL}, NullBytes(), OUT_OF_RANGE},
      {"net.ip_net_mask", {3LL, 0LL}, NullBytes(), OUT_OF_RANGE},
      {"net.ip_net_mask", {4LL, -1LL}, NullBytes(), OUT_OF_RANGE},
      {"net.ip_net_mask", {4LL, 33LL}, NullBytes(), OUT_OF_RANGE},
      {"net.ip_net_mask", {16LL, -1LL}, NullBytes(), OUT_OF_RANGE},
      {"net.ip_net_mask", {16LL, 129LL}, NullBytes(), OUT_OF_RANGE},

      // NET.IP_TRUNC.
      {"net.ip_trunc",
       {Bytes("\x12\x34\x56\x78"), 32LL},
       Bytes("\x12\x34\x56\x78")},
      {"net.ip_trunc",
       {Bytes("\x12\x34\x56\x78"), 28LL},
       Bytes("\x12\x34\x56\x70")},
      {"net.ip_trunc",
       {Bytes("\x12\x34\x56\x78"), 24LL},
       Bytes("\x12\x34\x56\x00")},
      {"net.ip_trunc",
       {Bytes("\x12\x34\x56\x78"), 11LL},
       Bytes("\x12\x20\x00\x00")},
      {"net.ip_trunc",
       {Bytes("\x12\x34\x56\x78"), 0LL},
       Bytes("\x00\x00\x00\x00")},
      {"net.ip_trunc",
       {Bytes("defghijklmnopqqr"), 128LL},
       Bytes("defghijklmnopqqr")},
      {"net.ip_trunc",
       {Bytes("defghijklmno\x76\x54\x32\x10"), 100LL},
       Bytes("defghijklmno\x70\x00\x00\x00")},
      {"net.ip_trunc",
       {Bytes("defghijklmnopqqr"), 0LL},
       Bytes(std::string(16, '\0'))},
      //    NULL input.
      {"net.ip_trunc", {NullBytes(), 0LL}, NullBytes()},
      {"net.ip_trunc", {Bytes("\x12\x34\x56\x78"), NullInt64()}, NullBytes()},
      //    Input failure.
      {"net.ip_trunc", {Bytes(""), 0LL}, NullBytes(), OUT_OF_RANGE},
      {"net.ip_trunc", {Bytes("\x12\x34\x56"), 0LL}, NullBytes(), OUT_OF_RANGE},
      {"net.ip_trunc",
       {Bytes("\x12\x34\x56\x78"), -1LL},
       NullBytes(),
       OUT_OF_RANGE},
      {"net.ip_trunc",
       {Bytes("\x12\x34\x56\x78"), 33LL},
       NullBytes(),
       OUT_OF_RANGE},
      {"net.ip_trunc",
       {Bytes("defghijklmnopqqr"), -1LL},
       NullBytes(),
       OUT_OF_RANGE},
      {"net.ip_trunc",
       {Bytes("defghijklmnopqqr"), 129LL},
       NullBytes(),
       OUT_OF_RANGE},

      // NET.MAKE_NET.
      //   IPv4 Success.
      {"net.make_net", {String("10.1.2.3"), 24}, String("10.1.2.0/24")},
      {"net.make_net", {String("10.1.2.3"), 8}, String("10.0.0.0/8")},
      {"net.make_net", {String("10.1.2.3"), 31}, String("10.1.2.2/31")},
      {"net.make_net", {String("10.1.2.3"), 32}, String("10.1.2.3/32")},
      {"net.make_net", {String("10.1.2.3/24"), 8}, String("10.0.0.0/8")},
      {"net.make_net", {String("10.1.2.3/24"), 24}, String("10.1.2.0/24")},
      //   IPv4 Failure.
      {"net.make_net", {String("10.1.2.3"), 33}, NullString(), OUT_OF_RANGE},
      {"net.make_net", {String("10.1.2.3/8"), 24}, NullString(), OUT_OF_RANGE},
      {"net.make_net", {String("10.1.2.3"), -1}, NullString(), OUT_OF_RANGE},
      {"net.make_net", {String(" 10.1.2.3"), 24}, NullString(), OUT_OF_RANGE},
      //   IPv6 Success.
      {"net.make_net", {String("2::1"), 16}, String("2::/16")},
      {"net.make_net", {String("1234::1"), 8}, String("1200::/8")},
      {"net.make_net", {String("2:1::1"), 48}, String("2:1::/48")},
      {"net.make_net", {String("2:1::/32"), 16}, String("2::/16")},
      {"net.make_net", {String("2:1::/32"), 32}, String("2:1::/32")},
      {"net.make_net", {String("2::1"), 128}, String("2::1/128")},
      {"net.make_net",
       {String("::ffff:192.168.1.1"), 112},
       String("::ffff:192.168.0.0/112")},
      //   IPv6 Failure.
      {"net.make_net", {String("2:1::/16"), 32}, NullString(), OUT_OF_RANGE},
      {"net.make_net", {String("2::1"), 129}, NullString(), OUT_OF_RANGE},
      {"net.make_net", {String(" 2::1"), 12}, NullString(), OUT_OF_RANGE},
      {"net.make_net",
       {String("::ffff:192.168.1.1.1"), 112},
       NullString(),
       OUT_OF_RANGE},
      {"net.make_net",
       {String("::ffff:192.168.1"), 112},
       NullString(),
       OUT_OF_RANGE},
      //    NULL input.
      {"net.make_net", {String("10.1.2.0"), NullInt32()}, NullString()},
      {"net.make_net", {String("2::1"), NullInt32()}, NullString()},
      {"net.make_net", {NullString(), 12}, NullString()},
      //   Input Failure.
      {"net.make_net", {String("foo"), 12}, NullString(), OUT_OF_RANGE},

      // NET.IP_IN_NET.
      //   IPv4 Success.
      {"net.ip_in_net", {String("10.1.2.3"), String("10.1.2.0/24")}, true},
      {"net.ip_in_net", {String("10.0.2.3"), String("10.0.2.0/24")}, true},
      {"net.ip_in_net", {String("10.1.3.3"), String("10.1.2.0/24")}, false},
      {"net.ip_in_net", {String("10.1.2.3/28"), String("10.1.2.0/24")}, true},
      {"net.ip_in_net", {String("10.1.2.3/24"), String("10.1.2.0/28")}, false},
      {"net.ip_in_net", {String("10.1.3.3"), String("10.1.2.0/24")}, false},
      //   IPv4 Failure.
      {"net.ip_in_net",
       {String(" 10.1.3.3"), String("10.1.2.0/24")},
       NullBool(),
       OUT_OF_RANGE},
      {"net.ip_in_net",
       {String("10.1.3.3"), String("10.1.2.0/24 ")},
       NullBool(),
       OUT_OF_RANGE},
      {"net.ip_in_net",
       {String("10.1.2.3/33"), String("10.1.2.0/28")},
       NullBool(),
       OUT_OF_RANGE},
      //   IPv6 Success.
      {"net.ip_in_net", {String("1::1"), String("1::/16")}, true},
      {"net.ip_in_net", {String("2::1"), String("1::/16")}, false},
      {"net.ip_in_net", {String("2:1::1/32"), String("2:1::/24")}, true},
      {"net.ip_in_net", {String("2:1::/24"), String("2:1::/48")}, false},
      //   IPv6 Failure.
      {"net.ip_in_net",
       {String(" 2::1"), String("1::/12")},
       NullBool(),
       OUT_OF_RANGE},
      {"net.ip_in_net",
       {String("2::1"), String(" 1::/12")},
       NullBool(),
       OUT_OF_RANGE},
      {"net.ip_in_net",
       {String(" 2::1"), String(" 1::/12")},
       NullBool(),
       OUT_OF_RANGE},
      //    NULL input.
      {"net.ip_in_net", {NullString(), String("1::/12")}, NullBool()},
      {"net.ip_in_net", {String("2::1"), NullString()}, NullBool()},
      {"net.ip_in_net", {NullString(), String("10.1.2.0/28")}, NullBool()},
      {"net.ip_in_net", {String("10.1.2.0/28"), NullString()}, NullBool()},
      //   Input Failure.
      {"net.ip_in_net",
       {String("foo"), String("foobar")},
       NullBool(),
       OUT_OF_RANGE},
  };

  const QueryParamsWithResult ip_from_string_test_items[] = {
      //    IPv4 Success.
      {{"64.65.66.67"}, Bytes("@ABC")},
      {{"64.0.66.67"}, Bytes("@\0BC")},
      //    IPv4 Failure.
      {{"64.65.66.67 "}, NullBytes(), OUT_OF_RANGE},
      {{" 64.65.66.67"}, NullBytes(), OUT_OF_RANGE},
      {{"64.65.66.67\0"}, NullBytes(), OUT_OF_RANGE},
      //    IPv6 Success.
      {{"6465:6667:6869:6a6b:6c6d:6e6f:7071:7172"}, Bytes("defghijklmnopqqr")},
      {{"6465:6667:6869:6A6B:6C6d:6e6f:7071:7172"}, Bytes("defghijklmnopqqr")},
      {{"::1"}, Bytes("\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\1")},
      {{"::ffff:64.65.66.67"}, Bytes("\0\0\0\0\0\0\0\0\0\0\xff\xff@ABC")},
      {{"6465:6667:6869:6a6b:6c6d:FFFF:128.129.130.131"},
       Bytes("defghijklm\xFF\xFF\x80\x81\x82\x83")},
      //    IPv6 Failure.
      {{" 6465:6667:6869:6a6b:6c6d:6e6f:7071:7172"}, NullBytes(), OUT_OF_RANGE},
      {{"6465:6667:6869:6a6b:6c6d:6e6f:7071:7172 "}, NullBytes(), OUT_OF_RANGE},
      {{"::1\0"}, NullBytes(), OUT_OF_RANGE},
      //    NULL input.
      {{NullString()}, NullBytes()},
      //    Input failure.
      {{"foo"}, NullBytes(), OUT_OF_RANGE},
  };
  for (const QueryParamsWithResult& item : ip_from_string_test_items) {
    result.push_back({"net.ip_from_string", item});
    QueryParamsWithResult new_item(item);
    new_item.MutateResult([](QueryParamsWithResult::Result& mutable_result) {
      mutable_result.status = absl::OkStatus();
    });
    result.push_back({"net.safe_ip_from_string", new_item});
  }

  struct UrlTestItem {
    ValueConstructor url;
    ValueConstructor host;
    ValueConstructor reg_domain;
    ValueConstructor public_suffix;
  };
  // These cases are copied from the examples at (broken link).
  const UrlTestItem kUrlTestItems[] = {
      // Input url, expected host, expected reg_domain, expected public_suffix.
      {NullString(), NullString(), NullString(), NullString()},

      // Cases without scheme.
      {"", NullString(), NullString(), NullString()},
      {" ", NullString(), NullString(), NullString()},
      {".", NullString(), NullString(), NullString()},
      {"com", "com", NullString(), "com"},
      {".com", "com", NullString(), "com"},
      {"com.", "com.", NullString(), "com."},
      {"Foo_bar", "Foo_bar", NullString(), NullString()},
      {"Google.COM", "Google.COM", "Google.COM", "COM"},
      {"Google..COM", "Google..COM", NullString(), NullString()},
      {"foo.com:12345", "foo.com", "foo.com", "com"},
      {"foo.com:", "foo.com", "foo.com", "com"},
      {"user:password@foo.com:12345", "foo.com", "foo.com", "com"},
      {"user:password@[2001:0db8:0000:0000:0000:ff00:0042:8329]:12345",
       "[2001:0db8:0000:0000:0000:ff00:0042:8329]", NullString(), NullString()},
      {"[2001:0db8:0000:0000:0000:ff00:0042:8329]",
       "[2001:0db8:0000:0000:0000:ff00:0042:8329]", NullString(), NullString()},
      {"[2001:0db8/0000:0000:0000:ff00:0042:8329]",
       "[2001:0db8", NullString(), NullString()},
      {"foo.com//bar", "foo.com", "foo.com", "com"},
      {"/google", NullString(), NullString(), NullString()},
      {"/Google.COM", NullString(), NullString(), NullString()},
      {"/www.Google.COM", NullString(), NullString(), NullString()},
      {"/://www.Google.COM", NullString(), NullString(), NullString()},
      {":/google", NullString(), NullString(), NullString()},
      {":/Google.COM", NullString(), NullString(), NullString()},
      {":/www.Google.COM", NullString(), NullString(), NullString()},
      {":google", NullString(), NullString(), NullString()},
      {":Google.COM", NullString(), NullString(), NullString()},
      {":www.Google.COM", NullString(), NullString(), NullString()},

      // Cases with relative schemes (starting with //).
      {"//", NullString(), NullString(), NullString()},
      {" // ", NullString(), NullString(), NullString()},
      {"//.", NullString(), NullString(), NullString()},
      {"//com", "com", NullString(), "com"},
      {"//.com", "com", NullString(), "com"},
      {"//com.", "com.", NullString(), "com."},
      {"//Foo_bar", "Foo_bar", NullString(), NullString()},
      // "google" is a registered public suffix, though not provided by Google.
      {"//google", "google", NullString(), "google"},
      {"//Google.COM", "Google.COM", "Google.COM", "COM"},
      {"//Google..COM", "Google..COM", NullString(), NullString()},
      {"//foo.com:12345", "foo.com", "foo.com", "com"},
      {"//foo.com:", "foo.com", "foo.com", "com"},
      {"//user:password@foo.com:12345", "foo.com", "foo.com", "com"},
      {"//user:password@[2001:0db8:0000:0000:0000:ff00:0042:8329]:12345",
       "[2001:0db8:0000:0000:0000:ff00:0042:8329]", NullString(), NullString()},
      {"//foo.com//bar", "foo.com", "foo.com", "com"},
      {"///google", NullString(), NullString(), NullString()},
      {"//:/google", NullString(), NullString(), NullString()},
      {"//:/Google.COM", NullString(), NullString(), NullString()},
      {"//:/www.Google.COM", NullString(), NullString(), NullString()},
      {"//:google", NullString(), NullString(), NullString()},
      {"//:Google.COM", NullString(), NullString(), NullString()},
      {"//:www.Google.COM", NullString(), NullString(), NullString()},

      // Cases with absolute schemes (containing "://")
      {"://google", "google", NullString(), "google"},
      {"://Google.COM", "Google.COM", "Google.COM", "COM"},
      {"://www.Google.COM", "www.Google.COM", "Google.COM", "COM"},
      {"http://", NullString(), NullString(), NullString()},
      {"http://.", NullString(), NullString(), NullString()},
      {"http://.com", "com", NullString(), "com"},
      {"http://com.", "com.", NullString(), "com."},
      {"http://google", "google", NullString(), "google"},
      {"http://Google.COM", "Google.COM", "Google.COM", "COM"},
      {"http://Google.COM..", "Google.COM.", "Google.COM.", "COM."},
      {"http://..Google.COM", "Google.COM", "Google.COM", "COM"},
      {"http://Google..COM", "Google..COM", NullString(), NullString()},
      {"http://www.Google.COM", "www.Google.COM", "Google.COM", "COM"},
      {"  http://www.Google.COM  ", "www.Google.COM", "Google.COM", "COM"},
      {"http://www.Google.co.uk", "www.Google.co.uk", "Google.co.uk", "co.uk"},
      {"http://www.Google.Co.uk", "www.Google.Co.uk", "Google.Co.uk", "Co.uk"},
      {"http://www.Google.co.UK", "www.Google.co.UK", "Google.co.UK", "co.UK"},
      {"http://foo.com:12345", "foo.com", "foo.com", "com"},
      {"http://foo.com:", "foo.com", "foo.com", "com"},
      {"http://www.Google.COM/foo", "www.Google.COM", "Google.COM", "COM"},
      {"http://www.Google.COM#foo", "www.Google.COM", "Google.COM", "COM"},
      {"http://www.Google.COM?foo=bar@", "www.Google.COM", "Google.COM", "COM"},
      {"http://user:password@www.Google.COM", "www.Google.COM", "Google.COM",
       "COM"},
      {"http://user:pass:word@www.Google.COM", "www.Google.COM", "Google.COM",
       "COM"},
      {"http://user:password@www.Goo@gle.COM", "gle.COM", "gle.COM", "COM"},
      {"@://user:password@www.Goo@gle.COM", "gle.COM", "gle.COM", "COM"},
      {"http://[2001:0db8:0000:0000:0000:ff00:0042:8329]:80",
       "[2001:0db8:0000:0000:0000:ff00:0042:8329]", NullString(), NullString()},
      {"http://[2001:0db8:0000:0000:0000:ff00:0042:8329]:80:90",
       "[2001:0db8:0000:0000:0000:ff00:0042:8329]", NullString(), NullString()},
      {"http://[[2001:0db8:0000:0000:0000:ff00:0042:8329]]:80", "[[2001",
       NullString(), NullString()},
      {"http://x@[2001:0db8:0000:0000:0000:ff00:0042:8329]",
       "[2001:0db8:0000:0000:0000:ff00:0042:8329]", NullString(), NullString()},
      {"http://x@[[2001:0db8:0000:0000:0000:ff00:0042:8329]]", "[[2001",
       NullString(), NullString()},
      {"http://1.2.3", "1.2.3", NullString(), NullString()},
      {"http://1.2.3.4", "1.2.3.4", NullString(), NullString()},
      {"http://1.2.3.4.5", "1.2.3.4.5", NullString(), NullString()},
      {"http://1000.2000.3000.4000", "1000.2000.3000.4000", NullString(),
          NullString()},
      {"http://a", "a", NullString(), NullString()},
      {"http://a.b", "a.b", NullString(), NullString()},
      {"http://abc.xyz", "abc.xyz", "abc.xyz", "xyz"},
      // "compute.amazonaws.com" is an entry in the private domain section of
      // the pubic suffix list. It should not be treated as a public suffix.
      {"http://compute.amazonaws.com", "compute.amazonaws.com", "amazonaws.com",
       "com"},
      // *.kh is a public suffix rule with a wildcard (*).
      {"http://foo.bar.kh", "foo.bar.kh", "foo.bar.kh", "bar.kh"},
      {" http://www.Google.COM ", "www.Google.COM", "Google.COM", "COM"},
      {"http://x y.com", "x y.com", "x y.com", "com"},
      {String("http://x\000y.com"), String("x\000y.com"), String("x\000y.com"),
       "com"},
      {"http://~.com", "~.com", "~.com", "com"},
      {"http://café.fr", "café.fr", "café.fr", "fr"},
      {"http://例子.卷筒纸", "例子.卷筒纸", NullString(), NullString()},
      {"http://例子.卷筒纸.中国", "例子.卷筒纸.中国", "卷筒纸.中国", "中国"},
      // "xn--fiqs8s" is the puny encoded result of "中国".
      // The output should be the original form, not the encoded result.
      {"http://例子.卷筒纸.xn--fiqs8s", "例子.卷筒纸.xn--fiqs8s",
       "卷筒纸.xn--fiqs8s", "xn--fiqs8s"},
      {"mailto://somebody@email.com", "email.com", "email.com", "com"},
      {"javascript://alert('hi')", "alert('hi')", NullString(), NullString()},

      // Cases with unsupported schemes.
      {"mailto:x@foo.com,y@bar.com", "bar.com", "bar.com", "com"},
      {"mailto:?to=&subject=&body=", "mailto", NullString(), NullString()},
      // Taken from https://tools.ietf.org/html/rfc3981
      {"iris.beep:dreg1//com/domain/example.com", "iris.beep", NullString(),
       NullString()},
      {"iris.beep:dreg1/bottom/example.com/domain/example.com", "iris.beep",
       NullString(), NullString()},
  };

  for (const UrlTestItem& item : kUrlTestItems) {
    result.push_back({"net.host", {item.url.get()}, item.host.get()});
    result.push_back(
        {"net.reg_domain", {item.url.get()}, item.reg_domain.get()});
    result.push_back(
        {"net.public_suffix", {item.url.get()}, item.public_suffix.get()});
  }
  return result;
}

}  // namespace googlesql
