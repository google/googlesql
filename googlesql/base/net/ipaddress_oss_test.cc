//
// Copyright 2008 Google LLC
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
//

#include "googlesql/base/net/ipaddress_oss.h"

#include <arpa/inet.h>
#include <net/if.h>
#include <netdb.h>
#include <unistd.h>

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/container/fixed_array.h"
#include "absl/numeric/int128.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_replace.h"
#include "googlesql/base/endian.h"
#include "googlesql/base/logging.h"

namespace googlesql::internal {
namespace {


// TODO: delete this
class ScopedMockLogVerifier {
 public:
  explicit ScopedMockLogVerifier(const std::string& substr) {}
};

IPAddress MakeScopedIP(const IPAddress& addr, uint32_t scope_id) {
  if (scope_id == 0) return addr;

  ABSL_CHECK_EQ(AF_INET6, addr.address_family());
  return MakeIPAddressWithScopeId(addr.ipv6_address(), scope_id).value();
}

// Tests for IPAddress.
TEST(IPAddressTest, BasicTests) {
  in_addr addr4;
  in6_addr addr6;

  inet_pton(AF_INET, "1.2.3.4", &addr4);
  inet_pton(AF_INET6, "2001:700:300:1800::f", &addr6);

  IPAddress addr(addr4);
  in_addr returned_addr4 = addr.ipv4_address();
  ASSERT_EQ(AF_INET, addr.address_family());
  EXPECT_TRUE(addr.is_ipv4());
  EXPECT_FALSE(addr.is_ipv6());
  EXPECT_EQ(0, memcmp(&addr4, &returned_addr4, sizeof(addr4)));

  addr = IPAddress(addr6);
  in6_addr returned_addr6 = addr.ipv6_address();
  ASSERT_EQ(AF_INET6, addr.address_family());
  EXPECT_FALSE(addr.is_ipv4());
  EXPECT_TRUE(addr.is_ipv6());
  EXPECT_EQ(0, memcmp(&addr6, &returned_addr6, sizeof(addr6)));

  addr = IPAddress();
  ASSERT_EQ(AF_UNSPEC, addr.address_family());
}

TEST(IPAddressTest, ConstexprIPv4) {
  constexpr IPAddress addr4(in_addr{0x12345678});
  ASSERT_EQ(addr4, IPAddress(in_addr{0x12345678}));
}

TEST(IPAddressTest, ToAndFromString4) {
  const std::string kIPString = "1.2.3.4";
  const std::string kBogusIPString = "1.2.3.256";
  in_addr addr4;
  ABSL_CHECK_GT(inet_pton(AF_INET, kIPString.c_str(), &addr4), 0);

  IPAddress addr;
  EXPECT_FALSE(StringToIPAddress(kBogusIPString, nullptr));
  EXPECT_FALSE(StringToIPAddress(kBogusIPString, &addr));
  ASSERT_TRUE(StringToIPAddress(kIPString, nullptr));
  ASSERT_TRUE(StringToIPAddress(kIPString, &addr));

  in_addr returned_addr4 = addr.ipv4_address();
  EXPECT_EQ(AF_INET, addr.address_family());
  EXPECT_EQ(0, memcmp(&addr4, &returned_addr4, sizeof(addr4)));

  std::string packed = addr.ToPackedString();
  EXPECT_EQ(sizeof(addr4), packed.length());
  EXPECT_EQ(0, memcmp(packed.data(), &addr4, sizeof(addr4)));

  EXPECT_TRUE(PackedStringToIPAddress(packed, nullptr));
  IPAddress unpacked;
  EXPECT_TRUE(PackedStringToIPAddress(packed, &unpacked));
  EXPECT_EQ(addr, unpacked);

  EXPECT_EQ(kIPString, addr.ToString());
}

TEST(IPAddressTest, ThoroughToString4) {
  // This thoroughly tests the internal function FastOctetToBuffer by
  // evaluating every possible number in every possible position.
  for (int i = 0; i < 256; ++i) {
    const std::string expected = absl::StrCat(i, ".0.0.0");
    EXPECT_EQ(expected, StringToIPAddressOrDie(expected).ToString());
  }
  for (int i = 0; i < 256; ++i) {
    const std::string expected = absl::StrCat("0.", i, ".0.0");
    EXPECT_EQ(expected, StringToIPAddressOrDie(expected).ToString());
  }
  for (int i = 0; i < 256; ++i) {
    const std::string expected = absl::StrCat("0.0.", i, ".0");
    EXPECT_EQ(expected, StringToIPAddressOrDie(expected).ToString());
  }
  for (int i = 0; i < 256; ++i) {
    const std::string expected = absl::StrCat("0.0.0.", i);
    EXPECT_EQ(expected, StringToIPAddressOrDie(expected).ToString());
  }
}

TEST(IPAddressTest, UnsafeIPv4Strings) {
  // These IPv4 string literal formats are supported by inet_aton(3).
  // They are one source of "spoofed" addresses in URLs and generally
  // considered unsafe.  We explicitly do not support them
  // (thankfully inet_pton(3) is significantly more sane).
  std::vector<std::string> kUnsafeIPv4Strings = {
    "016.016.016",          // 14.14.0.14
    "016.016",              // 14.0.0.14
    "016",                  // 0.0.0.14
    "0x0a.0x0a.0x0a.0x0a",  // 10.10.10.10
    "0x0a.0x0a.0x0a",       // 10.10.0.10
    "0x0a.0x0a",            // 10.0.0.10
    "0x0a",                 // 0.0.0.10
    "42.42.42",             // 42.42.0.42
    "42.42",                // 42.0.0.42
    "42",                   // 0.0.0.42
  // On Darwin inet_pton ignores leading zeros so this would be a valid
  // 16.16.16.16 address.
#if !defined(__APPLE__)
    "016.016.016.016",  // 14.14.14.14
#endif
  };

  IPAddress ip;
  for (const std::string& unsafe : kUnsafeIPv4Strings) {
    EXPECT_FALSE(StringToIPAddress(unsafe, &ip));
  }
}

TEST(IPAddressTest, ToAndFromString6) {
  const std::string kIPString = "2001:db8:300:1800::f";
  const std::string kBogusIPString = "2001:db8:300:1800:1:2:3:4:5";
  const std::string kBogusIPString2 = "2001:db8::g";

  in6_addr addr6;
  ABSL_CHECK_GT(inet_pton(AF_INET6, kIPString.c_str(), &addr6), 0);

  IPAddress addr;
  EXPECT_FALSE(StringToIPAddress(kBogusIPString, nullptr));
  EXPECT_FALSE(StringToIPAddress(kBogusIPString, &addr));
  EXPECT_FALSE(StringToIPAddress(kBogusIPString2, nullptr));
  EXPECT_FALSE(StringToIPAddress(kBogusIPString2, &addr));
  ASSERT_TRUE(StringToIPAddress(kIPString, nullptr));
  ASSERT_TRUE(StringToIPAddress(kIPString, &addr));

  in6_addr returned_addr6 = addr.ipv6_address();
  EXPECT_EQ(AF_INET6, addr.address_family());
  EXPECT_EQ(0, memcmp(&addr6, &returned_addr6, sizeof(addr6)));

  std::string packed = addr.ToPackedString();
  EXPECT_EQ(sizeof(addr6), packed.length());
  EXPECT_EQ(0, memcmp(packed.data(), &addr6, sizeof(addr6)));

  EXPECT_TRUE(PackedStringToIPAddress(packed, nullptr));
  IPAddress unpacked;
  EXPECT_TRUE(PackedStringToIPAddress(packed, &unpacked));
  EXPECT_EQ(addr, unpacked);

  EXPECT_EQ(kIPString, addr.ToString());
}

// The main purpose of this test is to validate that
// StringToIPAddressWithOptionalScope has feature parity with StringToIPAddress.
TEST(IPAddressTest, ToAndFromString6WithOptionalScope) {
  constexpr char kIPString[] = "2001:db8:300:1800::f";
  constexpr char const* kBogusIPStrings[] = {
      "2001:db8:300:1800:1:2:3:4:5", "2001:db8::g",
      "2001:db8:300:1800:1:2:3:4:5%ifacename", "2001:db8::g%ifacename"};

  in6_addr addr6;
  ABSL_CHECK_GT(inet_pton(AF_INET6, kIPString, &addr6), 0);

  for (const auto& bogus_ip_string : kBogusIPStrings) {
    // This sets the environment for an error-handling bug which would only
    // trigger when errno == 0.  The bug has since been fixed, and this allows
    // us to detect regressions.
    errno = 0;
    EXPECT_FALSE(StringToIPAddressWithOptionalScope(bogus_ip_string).ok())
        << "failed to reject bogus IP string \"" << bogus_ip_string << "\"";
  }

  auto addr_or = StringToIPAddressWithOptionalScope(kIPString);
  ASSERT_TRUE(addr_or.status().ok());
  IPAddress addr = addr_or.value();

  in6_addr returned_addr6 = addr.ipv6_address();
  EXPECT_EQ(AF_INET6, addr.address_family());
  EXPECT_EQ(0, memcmp(&addr6, &returned_addr6, sizeof(addr6)));

  std::string packed = addr.ToPackedString();
  EXPECT_EQ(sizeof(addr6), packed.length());
  EXPECT_EQ(0, memcmp(packed.data(), &addr6, sizeof(addr6)));

  EXPECT_TRUE(PackedStringToIPAddress(packed, nullptr));
  IPAddress unpacked;
  EXPECT_TRUE(PackedStringToIPAddress(packed, &unpacked));
  EXPECT_EQ(addr, unpacked);

  EXPECT_EQ(kIPString, addr.ToString());
}

TEST(IPAddressTest, ToAndFromString6EightColons) {
  IPAddress addr;
  IPAddress expected;

  EXPECT_TRUE(StringToIPAddress("::7:6:5:4:3:2:1", &addr));
  EXPECT_TRUE(StringToIPAddress("0:7:6:5:4:3:2:1", &expected));
  EXPECT_EQ(expected, addr);

  EXPECT_TRUE(StringToIPAddress("7:6:5:4:3:2:1::", &addr));
  EXPECT_TRUE(StringToIPAddress("7:6:5:4:3:2:1:0", &expected));
  EXPECT_EQ(expected, addr);
}

TEST(IPAddressTest, EmptyStrings) {
  IPAddress ip;
  EXPECT_FALSE(StringToIPAddress("", &ip));
  std::string empty;
  EXPECT_FALSE(StringToIPAddress(empty, &ip));
}

TEST(IPAddressTest, SameAsInetNToP6) {
  // Test that for various classes of IP addresses IPAddress::ToString generates
  // the same result as inet_ntop.
  const std::string cases[] = {
    "ffff:ffff:100::808:808",
    "::1",
    "::",
    "1:2:3:4:5:6:7:8",
    "2001:0:0:4::8",
    "2001::4:5:6:7:8",
    "2001:2:3:4:5:6:7:8",
    "0:0:3::ffff",
    "::4:0:0:0:ffff",
    "::5:0:0:ffff",
    "1::4:0:0:7:8",
    "2001:658:22a:cafe::",
    "::1.2.3.4",
    "::ffff:1.2.3.4",
    "::ffff:ffff:1:1",
    "::0.1.0.0",
    "1234:abcd::",
    "1234::abcd:0:0:5678",
    "1234:0:0:abcd::5678",
    "::192.168.90.1",
    "::ffff:192.168.90.1",
    "1234:0:0:abcd::5678",
    "1234:5678:2:9abc:def0:3:1234:5678",
// Darwin's inet_ntop does not follow RFC 5952 so we skip following tests on
// __APPLE__.
#if !defined(__APPLE__)
    "::ffff",
    "2001:0:3:4:5:6:7:8",
    "::abcd",
    "1234:5678:0:9abc:def0:0:1234:5678",
#endif
  };
  char buf[INET6_ADDRSTRLEN];
  IPAddress addr;

  for (const auto& c : cases) {
    EXPECT_TRUE(StringToIPAddress(c, &addr));
    EXPECT_EQ(addr.ToString(), c);
    std::string packed = addr.ToPackedString();
    inet_ntop(AF_INET6, packed.data(), buf, INET6_ADDRSTRLEN);
    EXPECT_EQ(addr.ToString(), std::string(buf));
  }
}

TEST(IPAddressTest, Equality) {
  const std::string kIPv4String1 = "1.2.3.4";
  const std::string kIPv4String2 = "2.3.4.5";
  const std::string kIPv6String1 = "2001:700:300:1800::f";
  const std::string kIPv6String2 = "2001:700:300:1800:0:0:0:f";
  const std::string kIPv6String3 = "::1";

  IPAddress empty;
  IPAddress addr4_1, addr4_2;
  IPAddress addr6_1, addr6_2, addr6_3;

  ASSERT_TRUE(StringToIPAddress(kIPv4String1, &addr4_1));
  ASSERT_TRUE(StringToIPAddress(kIPv4String2, &addr4_2));
  ASSERT_TRUE(StringToIPAddress(kIPv6String1, &addr6_1));
  ASSERT_TRUE(StringToIPAddress(kIPv6String2, &addr6_2));
  ASSERT_TRUE(StringToIPAddress(kIPv6String3, &addr6_3));

  // operator==
  EXPECT_TRUE(empty == empty);
  EXPECT_FALSE(empty == addr4_1);
  EXPECT_FALSE(empty == addr4_2);
  EXPECT_FALSE(empty == addr6_1);
  EXPECT_FALSE(empty == addr6_2);
  EXPECT_FALSE(empty == addr6_3);

  EXPECT_FALSE(addr4_1 == empty);
  EXPECT_TRUE(addr4_1 == addr4_1);
  EXPECT_FALSE(addr4_1 == addr4_2);
  EXPECT_FALSE(addr4_1 == addr6_1);
  EXPECT_FALSE(addr4_1 == addr6_2);
  EXPECT_FALSE(addr4_1 == addr6_3);

  EXPECT_FALSE(addr4_2 == empty);
  EXPECT_FALSE(addr4_2 == addr4_1);
  EXPECT_TRUE(addr4_2 == addr4_2);
  EXPECT_FALSE(addr4_2 == addr6_1);
  EXPECT_FALSE(addr4_2 == addr6_2);
  EXPECT_FALSE(addr4_2 == addr6_3);

  EXPECT_FALSE(addr6_1 == empty);
  EXPECT_FALSE(addr6_1 == addr4_1);
  EXPECT_FALSE(addr6_1 == addr4_2);
  EXPECT_TRUE(addr6_1 == addr6_1);
  EXPECT_TRUE(addr6_1 == addr6_2);
  EXPECT_FALSE(addr6_1 == addr6_3);

  EXPECT_FALSE(addr6_2 == empty);
  EXPECT_FALSE(addr6_2 == addr4_1);
  EXPECT_FALSE(addr6_2 == addr4_2);
  EXPECT_TRUE(addr6_2 == addr6_1);
  EXPECT_TRUE(addr6_2 == addr6_2);
  EXPECT_FALSE(addr6_2 == addr6_3);

  EXPECT_FALSE(addr6_3 == empty);
  EXPECT_FALSE(addr6_3 == addr4_1);
  EXPECT_FALSE(addr6_3 == addr4_2);
  EXPECT_FALSE(addr6_3 == addr6_1);
  EXPECT_FALSE(addr6_3 == addr6_2);
  EXPECT_TRUE(addr6_3 == addr6_3);

  // operator!= (same tests, just inverted)
  EXPECT_FALSE(empty != empty);
  EXPECT_TRUE(empty != addr4_1);
  EXPECT_TRUE(empty != addr4_2);
  EXPECT_TRUE(empty != addr6_1);
  EXPECT_TRUE(empty != addr6_2);
  EXPECT_TRUE(empty != addr6_3);

  EXPECT_TRUE(addr4_1 != empty);
  EXPECT_FALSE(addr4_1 != addr4_1);
  EXPECT_TRUE(addr4_1 != addr4_2);
  EXPECT_TRUE(addr4_1 != addr6_1);
  EXPECT_TRUE(addr4_1 != addr6_2);
  EXPECT_TRUE(addr4_1 != addr6_3);

  EXPECT_TRUE(addr4_2 != empty);
  EXPECT_TRUE(addr4_2 != addr4_1);
  EXPECT_FALSE(addr4_2 != addr4_2);
  EXPECT_TRUE(addr4_2 != addr6_1);
  EXPECT_TRUE(addr4_2 != addr6_2);
  EXPECT_TRUE(addr4_2 != addr6_3);

  EXPECT_TRUE(addr6_1 != empty);
  EXPECT_TRUE(addr6_1 != addr4_1);
  EXPECT_TRUE(addr6_1 != addr4_2);
  EXPECT_FALSE(addr6_1 != addr6_1);
  EXPECT_FALSE(addr6_1 != addr6_2);
  EXPECT_TRUE(addr6_1 != addr6_3);

  EXPECT_TRUE(addr6_2 != empty);
  EXPECT_TRUE(addr6_2 != addr4_1);
  EXPECT_TRUE(addr6_2 != addr4_2);
  EXPECT_FALSE(addr6_2 != addr6_1);
  EXPECT_FALSE(addr6_2 != addr6_2);
  EXPECT_TRUE(addr6_2 != addr6_3);

  EXPECT_TRUE(addr6_3 != empty);
  EXPECT_TRUE(addr6_3 != addr4_1);
  EXPECT_TRUE(addr6_3 != addr4_2);
  EXPECT_TRUE(addr6_3 != addr6_1);
  EXPECT_TRUE(addr6_3 != addr6_2);
  EXPECT_FALSE(addr6_3 != addr6_3);
}

TEST(IPAddressTest, HostUInt32ToIPAddress) {
  uint32_t addr1 = 0;
  uint32_t addr2 = 0x7f000001;
  uint32_t addr3 = 0xffffffff;

  EXPECT_EQ("0.0.0.0", HostUInt32ToIPAddress(addr1).ToString());
  EXPECT_EQ("127.0.0.1", HostUInt32ToIPAddress(addr2).ToString());
  EXPECT_EQ("255.255.255.255", HostUInt32ToIPAddress(addr3).ToString());
}

TEST(IPAddressTest, IPAddressToHostUInt32) {
  IPAddress addr = StringToIPAddressOrDie("1.2.3.4");
  EXPECT_EQ(0x01020304, IPAddressToHostUInt32(addr));
}

TEST(IPAddressTest, UInt128ToIPAddress) {
  absl::uint128 addr1(0);
  absl::uint128 addr2(1);
  absl::uint128 addr3 = absl::MakeUint128(std::numeric_limits<uint64_t>::max(),
                                          std::numeric_limits<uint64_t>::max());

  EXPECT_EQ("::", UInt128ToIPAddress(addr1).ToString());
  EXPECT_EQ("::1", UInt128ToIPAddress(addr2).ToString());
  EXPECT_EQ("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff",
            UInt128ToIPAddress(addr3).ToString());
}

TEST(IPAddressTest, Constants) {
  EXPECT_EQ("0.0.0.0", IPAddress::Any4().ToString());
  EXPECT_EQ("::", IPAddress::Any6().ToString());
}

TEST(IPAddressTest, Logging) {
  const std::string kIPv4String = "1.2.3.4";
  const std::string kIPv6String = "2001:700:300:1800::f";
  IPAddress addr4, addr6;

  ASSERT_TRUE(StringToIPAddress(kIPv4String, &addr4));
  ASSERT_TRUE(StringToIPAddress(kIPv6String, &addr6));

  std::ostringstream out;
  out << addr4 << " " << addr6;
  EXPECT_EQ("1.2.3.4 2001:700:300:1800::f", out.str());
}

TEST(IPAddressTest, LoggingUninitialized) {
  std::ostringstream out;
  out << IPAddress();
  EXPECT_EQ("<uninitialized IPAddress>", out.str());
}

TEST(IPAddressTest, LoggingCorrupt) {
  IPAddress corrupt_ip;
  memset(&corrupt_ip, 0x55, sizeof(corrupt_ip));
  std::ostringstream out;
  out << corrupt_ip;
  EXPECT_EQ("<corrupt IPAddress with family=21845>", out.str());
}

TEST(IPAddressTest, IPv6LinkLocal) {
  const IPAddress fe80_1 = StringToIPAddressOrDie("fe80::1");
  const IPAddress fe80_2 = StringToIPAddressOrDie("fe80::2");
  const IPAddress fe80_1_if17(MakeScopedIP(fe80_1, 17));
  const IPAddress fe80_2_if17(MakeScopedIP(fe80_2, 17));
  const IPAddress fe80_2_if22(MakeScopedIP(fe80_2, 22));
  const IPAddress ff02_2 = StringToIPAddressOrDie("ff02::2");  // all-routers
  const IPAddress ff02_2_if17(MakeScopedIP(ff02_2, 17));

  // IPAddress::scope_id()
  EXPECT_EQ(0, fe80_1.scope_id());
  EXPECT_EQ(0, fe80_2.scope_id());
  EXPECT_EQ(17, fe80_1_if17.scope_id());
  EXPECT_EQ(17, fe80_2_if17.scope_id());
  EXPECT_EQ(22, fe80_2_if22.scope_id());
  EXPECT_EQ(0, ff02_2.scope_id());
  EXPECT_EQ(17, ff02_2_if17.scope_id());

  for (const auto& ip : {fe80_1, fe80_2, fe80_1_if17, fe80_2_if17, fe80_2_if22,
                         ff02_2, ff02_2_if17}) {
    EXPECT_EQ(AF_INET6, ip.address_family());
    EXPECT_EQ(128, IPAddressLength(ip));
  }

  // !=
  EXPECT_TRUE(fe80_1 != fe80_2);
  EXPECT_TRUE(fe80_1 != fe80_1_if17);
  EXPECT_TRUE(fe80_2 != fe80_2_if17);
  EXPECT_TRUE(fe80_2 != fe80_2_if22);
  EXPECT_TRUE(fe80_1_if17 != fe80_2_if17);
  EXPECT_TRUE(fe80_2_if17 != fe80_2_if22);

  // ==
  EXPECT_TRUE(fe80_1_if17 == fe80_1_if17);
  EXPECT_TRUE(fe80_2_if17 == fe80_2_if17);
  EXPECT_TRUE(fe80_2_if22 == fe80_2_if22);
  EXPECT_TRUE(fe80_1 == IPAddress(fe80_1_if17.ipv6_address()));
  EXPECT_TRUE(fe80_2 == IPAddress(fe80_2_if17.ipv6_address()));
  EXPECT_TRUE(fe80_2 == IPAddress(fe80_2_if22.ipv6_address()));

  // Check that we can't be super tricksy with the implementation to create
  // a collision.
  in6_addr addr6 = fe80_2_if17.ipv6_address();
  EXPECT_NE(fe80_2_if17, IPAddress(addr6));
  addr6.s6_addr32[1] = 17;
  EXPECT_NE(fe80_2_if17, IPAddress(addr6));
  addr6.s6_addr32[1] = googlesql_base::ghtonl(17);
  EXPECT_NE(fe80_2_if17, IPAddress(addr6));

  // Double-check that absence of a scope descriptor is not evidence of the
  // absence of a working IP string literal parser.
  EXPECT_EQ(ff02_2,
            StringToIPAddressWithOptionalScope(ff02_2.ToString()).value());

  // Appending a scope delimiter ('%') without a following zone_id does not
  // seem to comport with any of this text:
  //
  //     https://tools.ietf.org/html/rfc4007#section-11.2
  //     https://tools.ietf.org/html/rfc4007#section-11.6
  //     https://tools.ietf.org/html/rfc6874#section-2
  //
  // so check that it's treated as an error.
  EXPECT_FALSE(StringToIPAddressWithOptionalScope("fe80::%").ok());
  // Appending a default zone_id of "0" is, however, perfectly fine.
  EXPECT_EQ(StringToIPAddressOrDie("fe80::"),
            StringToIPAddressWithOptionalScope("fe80::%0").value());

  // For now verify that PackedString representation is identical to the
  // the un-scoped address implementation. How to meaningfully serialize and
  // de-serialize an interface index or name is left as an exercise for the
  // application.
  EXPECT_EQ(fe80_2.ToPackedString(), fe80_2_if17.ToPackedString());
  EXPECT_EQ(fe80_2.ToPackedString(), fe80_2_if22.ToPackedString());
}

TEST(IPAddressTest, IPAddressLength) {
  IPAddress ip;
  ASSERT_TRUE(StringToIPAddress("1.2.3.4", &ip));
  EXPECT_EQ(32, IPAddressLength(ip));
  ASSERT_TRUE(StringToIPAddress("2001:db8::1", &ip));
  EXPECT_EQ(128, IPAddressLength(ip));
}

TEST(IPAddressDeathTest, IPAddressLength) {
  IPAddress ip;
  int bitlength = 0;

  ScopedMockLogVerifier log(
      "IPAddressLength() of object with invalid address family");
  bitlength = IPAddressLength(ip);
  EXPECT_EQ(-1, bitlength);
}

TEST(IPAddressTest, IPAddressToUInt128) {
  IPAddress addr;
  ASSERT_TRUE(StringToIPAddress("2001:700:300:1803:b0ff::12", &addr));
  EXPECT_EQ(absl::MakeUint128(0x2001070003001803ULL, 0xb0ff000000000012ULL),
            IPAddressToUInt128(addr));
}

// Various death tests for IPAddress emergency behavior in production that
// should simply result in ABSL_CHECK failures in debug mode.

TEST(IPAddressDeathTest, EmergencyCoercion) {
  const std::string kIPv6Address = "2001:700:300:1803::1";
  IPAddress addr;
  in_addr addr4;

  ABSL_CHECK(StringToIPAddress(kIPv6Address, &addr));

  if (GOOGLESQL_DEBUG_MODE) {
    EXPECT_DEBUG_DEATH(addr4 = addr.ipv4_address(), "Check failed");
  } else {
    ScopedMockLogVerifier log("returning IPv4-coerced address");
    addr4 = addr.ipv4_address();
  }
}

TEST(IPAddressDeathTest, EmergencyCompatibility) {
  const std::string kIPv4Address = "129.240.2.40";
  IPAddress addr;
  in6_addr addr6;

  ABSL_CHECK(StringToIPAddress(kIPv4Address, &addr));

  if (GOOGLESQL_DEBUG_MODE) {
    EXPECT_DEBUG_DEATH(addr6 = addr.ipv6_address(), "Check failed");
  } else {
    ScopedMockLogVerifier log("returning IPv6 mapped address");
    addr6 = addr.ipv6_address();
    EXPECT_EQ("::ffff:129.240.2.40", IPAddress(addr6).ToString());
  }
}

// Invalid conversion in *OrDie() functions.
TEST(IPAddressDeathTest, InvalidStringConversion) {
  // Valid conversion.
  EXPECT_EQ(StringToIPAddressOrDie("1.2.3.4").ToString(), "1.2.3.4");
  EXPECT_EQ(StringToIPAddressOrDie("1.2.3.4").ToString(), "1.2.3.4");
  EXPECT_EQ(StringToIPAddressOrDie(absl::string_view("1.2.3.4")).ToString(),
            "1.2.3.4");
  EXPECT_EQ(StringToIPAddressOrDie("2001:700:300:1803::1").ToString(),
            "2001:700:300:1803::1");
  EXPECT_EQ(StringToIPAddressOrDie("2001:700:300:1803::1").ToString(),
            "2001:700:300:1803::1");
  EXPECT_EQ(StringToIPAddressOrDie(absl::string_view("2001:700:300:1803::1"))
                .ToString(),
            "2001:700:300:1803::1");
}

// Tests for IPRange.
TEST(IPRangeTest, BasicTest4) {
  IPAddress addr;
  const uint16_t kPrefixLength = 16;
  ASSERT_TRUE(StringToIPAddress("192.168.0.0", &addr));
  IPRange subnet(addr, kPrefixLength);
  EXPECT_EQ(addr, subnet.host());
  EXPECT_EQ(kPrefixLength, subnet.length());

  // Test copy construction.
  IPRange another_subnet = subnet;
  EXPECT_EQ(addr, another_subnet.host());
  EXPECT_EQ(kPrefixLength, another_subnet.length());

  // Test IPAddress constructor.
  EXPECT_EQ(addr, IPRange(addr).host());
  EXPECT_EQ(32, IPRange(addr).length());
}

TEST(IPRangeTest, BasicTest6) {
  IPAddress addr;
  const uint16_t kPrefixLength = 64;
  ASSERT_TRUE(StringToIPAddress("2001:700:300:1800::", &addr));
  IPRange subnet(addr, kPrefixLength);
  EXPECT_EQ(addr, subnet.host());
  EXPECT_EQ(kPrefixLength, subnet.length());

  // Test copy construction.
  IPRange another_subnet = subnet;
  EXPECT_EQ(addr, another_subnet.host());
  EXPECT_EQ(kPrefixLength, another_subnet.length());

  // Test IPAddress constructor.
  EXPECT_EQ(addr, IPRange(addr).host());
  EXPECT_EQ(128, IPRange(addr).length());
}

TEST(IPRangeTest, AnyRanges) {
  EXPECT_EQ("0.0.0.0/0", IPRange::Any4().ToString());
  EXPECT_EQ("::/0", IPRange::Any6().ToString());
}

TEST(IPRangeTest, ToAndFromString4) {
  const std::string kIPString = "192.168.0.0";
  const int kLength = 16;
  const std::string kSubnetString = kIPString + absl::StrFormat("/%u", kLength);
  const std::string kBogusSubnetString1 = "192.168.0.0/8";
  const std::string kBogusSubnetString2 = "192.256.0.0/16";
  const std::string kBogusSubnetString3 = "192.168.0.0/34";
  const std::string kBogusSubnetString4 = "0.0.0.0/-1";
  const std::string kBogusSubnetString5 = "0.0.0.0/+1";
  const std::string kBogusSubnetString6 = "0.0.0.0/";
  const std::string kBogusSubnetString7 = "192.168.0.0/16/16";
  const std::string kBogusSubnetString8 = "192.168.0.0/16 ";
  const std::string kBogusSubnetString9 = " 192.168.0.0/16";
  const std::string kBogusSubnetString10 = "192.168.0.0 /16";

  IPRange subnet;
  EXPECT_FALSE(StringToIPRange(kBogusSubnetString1, &subnet));
  EXPECT_FALSE(StringToIPRange(kBogusSubnetString2, &subnet));
  EXPECT_FALSE(StringToIPRange(kBogusSubnetString3, &subnet));
  EXPECT_FALSE(StringToIPRange(kBogusSubnetString4, &subnet));
  EXPECT_FALSE(StringToIPRange(kBogusSubnetString5, &subnet));
  EXPECT_FALSE(StringToIPRange(kBogusSubnetString6, &subnet));
  EXPECT_FALSE(StringToIPRange(kBogusSubnetString7, &subnet));
  EXPECT_FALSE(StringToIPRange(kBogusSubnetString8, &subnet));
  EXPECT_FALSE(StringToIPRange(kBogusSubnetString9, &subnet));
  EXPECT_FALSE(StringToIPRange(kBogusSubnetString10, &subnet));
  ASSERT_TRUE(StringToIPRange(kSubnetString, nullptr));
  ASSERT_TRUE(StringToIPRange(kSubnetString, &subnet));

  IPAddress addr4;
  ASSERT_TRUE(StringToIPAddress(kIPString, &addr4));
  EXPECT_EQ(addr4, subnet.host());
  EXPECT_EQ(kLength, subnet.length());

  EXPECT_EQ(kSubnetString, subnet.ToString());

  EXPECT_TRUE(StringToIPRangeAndTruncate(kBogusSubnetString1, &subnet));
  EXPECT_EQ("192.0.0.0/8", subnet.ToString());
  EXPECT_FALSE(StringToIPRangeAndTruncate(kBogusSubnetString2, &subnet));
  EXPECT_FALSE(StringToIPRangeAndTruncate(kBogusSubnetString3, &subnet));
  EXPECT_FALSE(StringToIPRangeAndTruncate(kBogusSubnetString4, &subnet));
  EXPECT_FALSE(StringToIPRangeAndTruncate(kBogusSubnetString5, &subnet));
  EXPECT_FALSE(StringToIPRangeAndTruncate(kBogusSubnetString6, &subnet));
  EXPECT_FALSE(StringToIPRangeAndTruncate(kBogusSubnetString7, &subnet));
  EXPECT_FALSE(StringToIPRangeAndTruncate(kBogusSubnetString8, &subnet));
  EXPECT_FALSE(StringToIPRangeAndTruncate(kBogusSubnetString9, &subnet));
  EXPECT_FALSE(StringToIPRangeAndTruncate(kBogusSubnetString10, &subnet));
}

TEST(IPRangeTest, DottedQuadNetmasks) {
  const std::string kIPString = "192.168.0.0";
  const std::string kDottedQuadNetmaskString = "255.255.0.0";
  const int kLength = 16;
  const std::string kSubnetString = kIPString + absl::StrFormat("/%u", kLength);
  const std::string kDottedQuadSubnetString =
      kIPString + "/" + kDottedQuadNetmaskString;

  const std::vector<std::string> kBogusDottedQuadStrings = {
    "192.168.0.0/128.255.0.0",
    "3ffe::1/255.255.0.0",
    "1.2.3.4/255",
    "1.2.3.4/255.",
    "1.2.3.4/255.255",
    "1.2.3.4/255.255.",
    "1.2.3.4/255.255.255",
    "1.2.3.4/255.255.255.",
    "1.2.3.4/255.255.255.256",
    "1.2.3.4/255.255.255.-255",
    "1.2.3.4/255.255.255.+255",
    "1.2.3.4/255.255.255.garbage",
  // On Darwin inet_pton ignores leading zeros so these would be valid.
#if !defined(__APPLE__)
    "1.2.3.4/0255.255.255.255",
    "1.2.3.4/255.255.255.000255",
#endif
  };

  // Check bogus strings.
  for (const std::string& bogus : kBogusDottedQuadStrings) {
    EXPECT_FALSE(StringToIPRangeAndTruncate(bogus, nullptr))
        << "Apparently '" << bogus << "' is actually valid?";
  }

  // Check valid strings.
  IPRange cidr;
  IPRange dotted_quad;
  ASSERT_TRUE(StringToIPRangeAndTruncate(kSubnetString, &cidr));
  ASSERT_TRUE(StringToIPRangeAndTruncate(kDottedQuadSubnetString,
                                         &dotted_quad));
  ASSERT_TRUE(cidr == dotted_quad);

  // Check some corner cases.
  EXPECT_TRUE(StringToIPRange("0.0.0.0/0.0.0.0", &cidr));
  EXPECT_EQ(0, cidr.length());
  EXPECT_EQ(IPAddress::Any4(), cidr.host());

  // If .expected_host_string is empty then .dotted_quad_string is
  // expected to FAIL StringToIPRangeAndTruncate().
  struct DottedQuadExpecations {
    std::string dotted_quad_string;
    std::string expected_host_string;
    int expected_length;
  };
  const std::vector<DottedQuadExpecations> dotted_quad_tests = {
      {"1.2.3.4/0.0.0.1", "", -1},
      {"1.2.3.4/1.0.0.0", "", -1},
      {"1.2.3.4/127.255.255.255", "", -1},
      {"1.2.3.4/254.255.255.255", "", -1},
      {"1.2.3.4/255.255.255.254", "1.2.3.4", 31},
      {"1.2.3.4/0.0.0.0", "0.0.0.0", 0},
  };

  for (const DottedQuadExpecations& entry : dotted_quad_tests) {
    IPRange range;
    IPAddress host;

    if (entry.expected_host_string.empty()) {
      // The dotted quad string should be rejected as invalid.
      ASSERT_FALSE(
          StringToIPRangeAndTruncate(entry.dotted_quad_string, &range));
      continue;
    }
    ASSERT_TRUE(StringToIPRangeAndTruncate(entry.dotted_quad_string, &range));
    ASSERT_TRUE(StringToIPAddress(entry.expected_host_string, &host));
    EXPECT_EQ(host, range.host())
        << entry.dotted_quad_string << " host equality expectation failed";
    EXPECT_EQ(entry.expected_length, range.length())
        << entry.dotted_quad_string << " length equality expectation failed";
  }
}

TEST(IPRangeTest, FromAddressString4) {
  const std::string kIPString = "192.168.0.0";
  IPAddress addr4;
  ASSERT_TRUE(StringToIPAddress(kIPString, &addr4));

  IPRange subnet;
  EXPECT_TRUE(StringToIPRange(kIPString, &subnet));
  EXPECT_EQ(addr4, subnet.host());
  EXPECT_EQ(32, subnet.length());

  EXPECT_TRUE(StringToIPRangeAndTruncate(kIPString, &subnet));
  EXPECT_EQ(addr4, subnet.host());
  EXPECT_EQ(32, subnet.length());
}

TEST(IPRangeTest, ToAndFromString6) {
  const std::string kIPString = "2001:700:300:1800::";
  const int kLength = 64;
  const std::string kSubnetString = kIPString + absl::StrFormat("/%u", kLength);
  const std::string kBogusSubnetString1 = "2001:700:300:1800::/48";
  const std::string kBogusSubnetString2 = "2001:700:300:180g::/64";
  const std::string kBogusSubnetString3 = "2001:700:300:1800::/129";
  const std::string kBogusSubnetString4 = "::/-1";
  const std::string kBogusSubnetString5 = "::/+1";
  const std::string kBogusSubnetString6 = "::/";
  const std::string kBogusSubnetString7 = "2001:700:300:1800::/64/64";
  const std::string kBogusSubnetString8 = "2001:700:300:1800::/64 ";
  const std::string kBogusSubnetString9 = " 2001:700:300:1800::/64";
  const std::string kBogusSubnetString10 = "2001:700:300:1800:: /64";

  IPRange subnet;
  EXPECT_FALSE(StringToIPRange(kBogusSubnetString1, &subnet));
  EXPECT_FALSE(StringToIPRange(kBogusSubnetString2, &subnet));
  EXPECT_FALSE(StringToIPRange(kBogusSubnetString3, &subnet));
  EXPECT_FALSE(StringToIPRange(kBogusSubnetString4, &subnet));
  EXPECT_FALSE(StringToIPRange(kBogusSubnetString5, &subnet));
  EXPECT_FALSE(StringToIPRange(kBogusSubnetString6, &subnet));
  EXPECT_FALSE(StringToIPRange(kBogusSubnetString7, &subnet));
  EXPECT_FALSE(StringToIPRange(kBogusSubnetString8, &subnet));
  EXPECT_FALSE(StringToIPRange(kBogusSubnetString9, &subnet));
  EXPECT_FALSE(StringToIPRange(kBogusSubnetString10, &subnet));
  ASSERT_TRUE(StringToIPRange(kSubnetString, nullptr));
  ASSERT_TRUE(StringToIPRange(kSubnetString, &subnet));

  IPAddress addr6;
  ASSERT_TRUE(StringToIPAddress(kIPString, &addr6));
  EXPECT_EQ(addr6, subnet.host());
  EXPECT_EQ(kLength, subnet.length());

  EXPECT_EQ(kSubnetString, subnet.ToString());

  EXPECT_TRUE(StringToIPRangeAndTruncate(kBogusSubnetString1, &subnet));
  EXPECT_EQ("2001:700:300::/48", subnet.ToString());
  EXPECT_FALSE(StringToIPRangeAndTruncate(kBogusSubnetString2, &subnet));
  EXPECT_FALSE(StringToIPRangeAndTruncate(kBogusSubnetString3, &subnet));
  EXPECT_FALSE(StringToIPRangeAndTruncate(kBogusSubnetString4, &subnet));
  EXPECT_FALSE(StringToIPRangeAndTruncate(kBogusSubnetString5, &subnet));
  EXPECT_FALSE(StringToIPRangeAndTruncate(kBogusSubnetString6, &subnet));
  EXPECT_FALSE(StringToIPRangeAndTruncate(kBogusSubnetString7, &subnet));
  EXPECT_FALSE(StringToIPRangeAndTruncate(kBogusSubnetString8, &subnet));
  EXPECT_FALSE(StringToIPRangeAndTruncate(kBogusSubnetString9, &subnet));
  EXPECT_FALSE(StringToIPRangeAndTruncate(kBogusSubnetString10, &subnet));
}

TEST(IPRangeTest, FromAddressString6) {
  const std::string kIPString = "2001:700:300:1800::";
  IPAddress addr6;
  ASSERT_TRUE(StringToIPAddress(kIPString, &addr6));

  IPRange subnet;
  EXPECT_TRUE(StringToIPRange(kIPString, &subnet));
  EXPECT_EQ(addr6, subnet.host());
  EXPECT_EQ(128, subnet.length());

  EXPECT_TRUE(StringToIPRangeAndTruncate(kIPString, &subnet));
  EXPECT_EQ(addr6, subnet.host());
  EXPECT_EQ(128, subnet.length());
}

TEST(IPRangeTest, Equality) {
  const std::string kIPv4String1 = "192.168.0.0/16";
  const std::string kIPv4String2 = "192.168.0.0/24";
  const std::string kIPv6String1 = "2001:700:300:1800::/64";
  const std::string kIPv6String2 = "2001:700:300:1800:0:0::/64";
  const std::string kIPv6String3 = "2001:700:300:dc0f::/64";

  IPRange subnet4_1, subnet4_2;
  IPRange subnet6_1, subnet6_2, subnet6_3;

  ASSERT_TRUE(StringToIPRange(kIPv4String1, &subnet4_1));
  ASSERT_TRUE(StringToIPRange(kIPv4String2, &subnet4_2));
  ASSERT_TRUE(StringToIPRange(kIPv6String1, &subnet6_1));
  ASSERT_TRUE(StringToIPRange(kIPv6String2, &subnet6_2));
  ASSERT_TRUE(StringToIPRange(kIPv6String3, &subnet6_3));

  // operator==
  EXPECT_TRUE(subnet4_1 == subnet4_1);
  EXPECT_FALSE(subnet4_1 == subnet4_2);
  EXPECT_FALSE(subnet4_1 == subnet6_1);
  EXPECT_FALSE(subnet4_1 == subnet6_2);
  EXPECT_FALSE(subnet4_1 == subnet6_3);

  EXPECT_FALSE(subnet4_2 == subnet4_1);
  EXPECT_TRUE(subnet4_2 == subnet4_2);
  EXPECT_FALSE(subnet4_2 == subnet6_1);
  EXPECT_FALSE(subnet4_2 == subnet6_2);
  EXPECT_FALSE(subnet4_2 == subnet6_3);

  EXPECT_FALSE(subnet6_1 == subnet4_1);
  EXPECT_FALSE(subnet6_1 == subnet4_2);
  EXPECT_TRUE(subnet6_1 == subnet6_1);
  EXPECT_TRUE(subnet6_1 == subnet6_2);
  EXPECT_FALSE(subnet6_1 == subnet6_3);

  EXPECT_FALSE(subnet6_2 == subnet4_1);
  EXPECT_FALSE(subnet6_2 == subnet4_2);
  EXPECT_TRUE(subnet6_2 == subnet6_1);
  EXPECT_TRUE(subnet6_2 == subnet6_2);
  EXPECT_FALSE(subnet6_2 == subnet6_3);

  EXPECT_FALSE(subnet6_3 == subnet4_1);
  EXPECT_FALSE(subnet6_3 == subnet4_2);
  EXPECT_FALSE(subnet6_3 == subnet6_1);
  EXPECT_FALSE(subnet6_3 == subnet6_2);
  EXPECT_TRUE(subnet6_3 == subnet6_3);

  // operator!= (same tests, just inverted)
  EXPECT_FALSE(subnet4_1 != subnet4_1);
  EXPECT_TRUE(subnet4_1 != subnet4_2);
  EXPECT_TRUE(subnet4_1 != subnet6_1);
  EXPECT_TRUE(subnet4_1 != subnet6_2);
  EXPECT_TRUE(subnet4_1 != subnet6_3);

  EXPECT_TRUE(subnet4_2 != subnet4_1);
  EXPECT_FALSE(subnet4_2 != subnet4_2);
  EXPECT_TRUE(subnet4_2 != subnet6_1);
  EXPECT_TRUE(subnet4_2 != subnet6_2);
  EXPECT_TRUE(subnet4_2 != subnet6_3);

  EXPECT_TRUE(subnet6_1 != subnet4_1);
  EXPECT_TRUE(subnet6_1 != subnet4_2);
  EXPECT_FALSE(subnet6_1 != subnet6_1);
  EXPECT_FALSE(subnet6_1 != subnet6_2);
  EXPECT_TRUE(subnet6_1 != subnet6_3);

  EXPECT_TRUE(subnet6_2 != subnet4_1);
  EXPECT_TRUE(subnet6_2 != subnet4_2);
  EXPECT_FALSE(subnet6_2 != subnet6_1);
  EXPECT_FALSE(subnet6_2 != subnet6_2);
  EXPECT_TRUE(subnet6_2 != subnet6_3);

  EXPECT_TRUE(subnet6_3 != subnet4_1);
  EXPECT_TRUE(subnet6_3 != subnet4_2);
  EXPECT_TRUE(subnet6_3 != subnet6_1);
  EXPECT_TRUE(subnet6_3 != subnet6_2);
  EXPECT_FALSE(subnet6_3 != subnet6_3);
}

TEST(IPRangeTest, LowerAndUpper4) {
  IPAddress expected, ip;
  IPRange range;

  ASSERT_TRUE(StringToIPAddress("1.2.3.4", &ip));

  // 1.2.3.4/0
  range = IPRange(ip, 0);
  ASSERT_TRUE(StringToIPAddress("0.0.0.0", &expected));
  EXPECT_EQ(expected, range.host());
  ASSERT_TRUE(StringToIPAddress("255.255.255.255", &expected));

  // 1.2.3.4/25
  range = IPRange(ip, 25);
  ASSERT_TRUE(StringToIPAddress("1.2.3.0", &expected));
  EXPECT_EQ(expected, range.host());
  ASSERT_TRUE(StringToIPAddress("1.2.3.127", &expected));

  // 1.2.3.4/31
  range = IPRange(ip, 31);
  EXPECT_EQ(ip, range.host());
  ASSERT_TRUE(StringToIPAddress("1.2.3.5", &expected));

  // 1.2.3.4/32
  range = IPRange(ip, 32);
  EXPECT_EQ(ip, range.host());
}

TEST(IPRangeTest, LowerAndUpper6) {
  IPAddress expected, ip;
  IPRange range;

  ASSERT_TRUE(StringToIPAddress("1:2:3:4:5:6:7:8", &ip));

  // 1:2:3:4:5:6:7:8/0
  range = IPRange(ip, 0);
  ASSERT_TRUE(StringToIPAddress("::", &expected));
  EXPECT_EQ(expected, range.host());
  ASSERT_TRUE(StringToIPAddress("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff",
                                &expected));

  // 1:2:3:4:5:6:7:8/113
  range = IPRange(ip, 113);
  ASSERT_TRUE(StringToIPAddress("1:2:3:4:5:6:7:0", &expected));
  EXPECT_EQ(expected, range.host());
  ASSERT_TRUE(StringToIPAddress("1:2:3:4:5:6:7:7fff", &expected));

  // 1:2:3:4:5:6:7:8/127
  range = IPRange(ip, 127);
  EXPECT_EQ(ip, range.host());
  ASSERT_TRUE(StringToIPAddress("1:2:3:4:5:6:7:9", &expected));

  // 1:2:3:4:5:6:7:8/128
  range = IPRange(ip, 128);
  EXPECT_EQ(ip, range.host());
}

TEST(IPRangeTest, IsWithinSubnet) {
  const IPRange subnet1 = StringToIPRangeOrDie("192.168.0.0/16");
  const IPRange subnet2 = StringToIPRangeOrDie("192.168.0.0/24");
  const IPRange subnet3 = StringToIPRangeOrDie("2001:700:300:1800::/64");
  const IPRange subnet4 = StringToIPRangeOrDie("::/0");

  const IPAddress addr1 = StringToIPAddressOrDie("192.168.1.5");
  const IPAddress addr2 = StringToIPAddressOrDie("2001:700:300:1800::1");
  const IPAddress addr3 = StringToIPAddressOrDie("2001:700:300:1801::1");

  EXPECT_TRUE(IsWithinSubnet(subnet1, addr1));
  EXPECT_FALSE(IsWithinSubnet(subnet2, addr1));
  EXPECT_FALSE(IsWithinSubnet(subnet3, addr1));
  EXPECT_FALSE(IsWithinSubnet(subnet4, addr1));

  EXPECT_FALSE(IsWithinSubnet(subnet1, addr2));
  EXPECT_FALSE(IsWithinSubnet(subnet2, addr2));
  EXPECT_TRUE(IsWithinSubnet(subnet3, addr2));
  EXPECT_TRUE(IsWithinSubnet(subnet4, addr2));

  EXPECT_FALSE(IsWithinSubnet(subnet1, addr3));
  EXPECT_FALSE(IsWithinSubnet(subnet2, addr3));
  EXPECT_FALSE(IsWithinSubnet(subnet3, addr3));
  EXPECT_TRUE(IsWithinSubnet(subnet4, addr3));

  EXPECT_FALSE(IsWithinSubnet(subnet1, IPAddress()));
  EXPECT_FALSE(IsWithinSubnet(IPRange(), addr1));
}

TEST(IPRangeTest, IsProperSubRange) {
  std::vector<std::string> kRangeString = {
      "192.168.0.0/15",  "192.169.0.0/16", "192.168.0.0/24",
      "192.168.0.80/28", "::/0",           "2001:700:300:1800::/64",
  };

  std::vector<IPRange> ranges;
  for (const std::string& range_str : kRangeString) {
    IPRange range;
    ASSERT_TRUE(StringToIPRange(range_str, &range));
    EXPECT_FALSE(IsProperSubRange(range, range));
    ranges.push_back(range);
  }

  EXPECT_TRUE(IsProperSubRange(ranges[0], ranges[1]));
  EXPECT_TRUE(IsProperSubRange(ranges[0], ranges[2]));
  EXPECT_TRUE(IsProperSubRange(ranges[0], ranges[3]));
  EXPECT_FALSE(IsProperSubRange(ranges[0], ranges[4]));
  EXPECT_FALSE(IsProperSubRange(ranges[0], ranges[5]));

  EXPECT_FALSE(IsProperSubRange(ranges[1], ranges[0]));
  EXPECT_FALSE(IsProperSubRange(ranges[1], ranges[2]));
  EXPECT_FALSE(IsProperSubRange(ranges[1], ranges[3]));
  EXPECT_FALSE(IsProperSubRange(ranges[1], ranges[4]));
  EXPECT_FALSE(IsProperSubRange(ranges[1], ranges[5]));

  EXPECT_FALSE(IsProperSubRange(ranges[2], ranges[0]));
  EXPECT_FALSE(IsProperSubRange(ranges[2], ranges[1]));
  EXPECT_TRUE(IsProperSubRange(ranges[2], ranges[3]));
  EXPECT_FALSE(IsProperSubRange(ranges[2], ranges[4]));
  EXPECT_FALSE(IsProperSubRange(ranges[2], ranges[5]));

  EXPECT_FALSE(IsProperSubRange(ranges[3], ranges[0]));
  EXPECT_FALSE(IsProperSubRange(ranges[3], ranges[1]));
  EXPECT_FALSE(IsProperSubRange(ranges[3], ranges[2]));
  EXPECT_FALSE(IsProperSubRange(ranges[3], ranges[4]));
  EXPECT_FALSE(IsProperSubRange(ranges[3], ranges[5]));

  EXPECT_FALSE(IsProperSubRange(ranges[4], ranges[0]));
  EXPECT_FALSE(IsProperSubRange(ranges[4], ranges[1]));
  EXPECT_FALSE(IsProperSubRange(ranges[4], ranges[2]));
  EXPECT_FALSE(IsProperSubRange(ranges[4], ranges[3]));
  EXPECT_TRUE(IsProperSubRange(ranges[4], ranges[5]));

  EXPECT_FALSE(IsProperSubRange(ranges[5], ranges[0]));
  EXPECT_FALSE(IsProperSubRange(ranges[5], ranges[1]));
  EXPECT_FALSE(IsProperSubRange(ranges[5], ranges[2]));
  EXPECT_FALSE(IsProperSubRange(ranges[5], ranges[3]));
  EXPECT_FALSE(IsProperSubRange(ranges[5], ranges[4]));

  for (const IPRange& r : ranges) {
    EXPECT_FALSE(IsProperSubRange(IPRange(), r));
    EXPECT_FALSE(IsProperSubRange(r, IPRange()));
  }
}

TEST(IPRangeTest, IPv6LinkLocal) {
  const IPAddress fe80_1 = StringToIPAddressOrDie("fe80::1");
  const IPAddress fe80_1_if17(MakeScopedIP(fe80_1, 17));

  const IPRange linklocal64(IPRange(fe80_1, 64));
  const IPRange linklocal64_if17(IPRange(fe80_1_if17, 64));

  EXPECT_NE(linklocal64, linklocal64_if17);

  // Truncation beyond the boundary of what qualifies a prefix as being
  // scope_id-applicable doesn't really make sense. In order to prevent the
  // creation of some kind of ::%eth0/0 IPRange, truncation beyond scope_id
  // qualification discards the scope_id.
  //
  // IPv6 unicast link-local prefix is fe80::/10 (but this is [presently]
  // indistinguishable from fe80::/9).
  EXPECT_EQ(17, IPRange(fe80_1_if17, 10).host().scope_id());
  EXPECT_EQ(0, IPRange(fe80_1_if17, 8).host().scope_id());
  // IPv6 multicast link-local prefix is ff02::/16 (but this is [presently]
  // indistinguishable from ff02::/15).
  const IPAddress ff02_2_if17(
      StringToIPAddressWithOptionalScope("ff02::2%17").value());
  EXPECT_EQ(17, IPRange(ff02_2_if17, 16).host().scope_id());
  EXPECT_EQ(0, IPRange(ff02_2_if17, 14).host().scope_id());

  // IsWithinSubnet follows the truncation logic above.
  EXPECT_FALSE(IsWithinSubnet(linklocal64, fe80_1_if17));
  EXPECT_FALSE(IsWithinSubnet(linklocal64_if17, fe80_1));
  EXPECT_TRUE(IsWithinSubnet(linklocal64_if17, fe80_1_if17));
  EXPECT_TRUE(IsWithinSubnet(IPRange(fe80_1_if17, 10), fe80_1_if17));
  EXPECT_FALSE(IsWithinSubnet(IPRange(fe80_1, 10), fe80_1_if17));
  EXPECT_TRUE(IsWithinSubnet(IPRange(fe80_1, 8), fe80_1_if17));
  EXPECT_TRUE(IsWithinSubnet(IPRange::Any6(), fe80_1_if17));

  // IsProperSubRange also follows the truncation logic above.
  EXPECT_FALSE(IsProperSubRange(linklocal64, IPRange(fe80_1_if17)));
  EXPECT_FALSE(IsProperSubRange(linklocal64_if17, IPRange(fe80_1)));
  EXPECT_TRUE(IsProperSubRange(linklocal64_if17, IPRange(fe80_1_if17)));
  EXPECT_TRUE(IsProperSubRange(IPRange(fe80_1_if17, 10), linklocal64_if17));
  EXPECT_FALSE(IsProperSubRange(IPRange(fe80_1, 10), linklocal64_if17));
  EXPECT_TRUE(IsProperSubRange(IPRange(fe80_1, 8), linklocal64_if17));
  EXPECT_TRUE(IsProperSubRange(IPRange::Any6(), linklocal64_if17));
}

TEST(IPRangeTest, UnsafeConstruct) {
  // Valid inputs.
  IPRange::UnsafeConstruct(IPAddress(), -1);
  IPRange::UnsafeConstruct(StringToIPAddressOrDie("192.0.2.0"), 24);
  IPRange::UnsafeConstruct(StringToIPAddressOrDie("2001:db8::"), 32);
}

TEST(IPRangeTest, LoggingUninitialized) {
  std::ostringstream out;
  out << IPRange();
  EXPECT_EQ("<uninitialized IPRange>", out.str());
}

}  // namespace
}  // namespace googlesql::internal
