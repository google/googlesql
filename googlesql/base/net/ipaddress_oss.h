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

// Author: sesse@google.com (Steinar H. Gunderson)
//
// Various classes for storing Internet addresses:
//
//  * IPAddress:      An IPv4 or IPv6 address. Fundamentally represents a host
//                    (or more precisely, an network interface).
//                    Roughly analogous to a struct in_addr.
//  * IPRange:        A subnet address, ie. a range of IPv4 or IPv6 addresses
//                    (IPAddress, plus a prefix length). (Would have been named
//                    IPSubnet, but that was already taken several other
//                    places.)
//
// The IPAddress class explicitly does not handle mapped or compatible IPv4
// addresses specially. In particular, operator== will treat 1.2.3.4 (IPv4),
// ::1.2.3.4 (compatible IPv4 embedded in IPv6) and
// ::ffff:1.2.3.4 (mapped IPv4 embedded in IPv6) as all distinct.
//

#ifndef THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_NET_IPADDRESS_OSS_H_
#define THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_NET_IPADDRESS_OSS_H_

#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/config.h"
#include "absl/base/macros.h"
#include "absl/numeric/int128.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/endian.h"
#include "googlesql/base/logging.h"

#ifdef __APPLE__
#define s6_addr16 __u6_addr.__u6_addr16
#define s6_addr32 __u6_addr.__u6_addr32
#endif

namespace googlesql::internal {

// Forward declaration for IPAddress ostream operator, so that DCHECK
// macros know that there is an appropriate overload.
class IPAddress;
std::ostream& operator<<(std::ostream& stream, const IPAddress& address);
absl::StatusOr<IPAddress> MakeIPAddressWithScopeId(const in6_addr&, uint32_t);

class IPAddress {
 public:
  // Default constructor. Leaves the object in an empty state.
  // The empty state is analogous to a NULL pointer; the only operations
  // that are allowed on the object are:
  //
  //   * Assignment and copy construction.
  //   * Checking for the empty state (address_family() will return AF_UNSPEC,
  //     or equivalently, the helper function IsInitializedAddress() will
  //     return false).
  //   * Comparison (operator== and operator!=).
  //   * Logging (operator<<)
  //
  // In particular, no guarantees are made about the behavior of the
  // IPv4/IPv6 conversion accessors, of string conversions and
  // serialization, of IsAnyIPAddress() and friends.
  IPAddress() : address_family_(AF_UNSPEC) {}

  // Constructors from standard BSD socket address structures.
  constexpr explicit IPAddress(const in_addr& addr)
      : addr_(addr), address_family_(AF_INET) {}
  explicit IPAddress(const in6_addr& addr) : IPAddress(addr, 0U) {}

  // The address family; either AF_UNSPEC, AF_INET or AF_INET6.
  int address_family() const { return address_family_; }

  // The address as an in_addr structure; ABSL_CHECK-fails if address_family() is
  // not AF_INET (ie. the held address is not an IPv4 address).
  in_addr ipv4_address() const {
    ABSL_CHECK_EQ(AF_INET, address_family_);
    return addr_.addr4;
  }

  // The address as an in6_addr structure; ABSL_CHECK-fails if address_family() is
  // not AF_INET6 (ie. the held address is not an IPv6 address).
  in6_addr ipv6_address() const {
    ABSL_CHECK_EQ(AF_INET6, address_family_);
    if (ABSL_PREDICT_FALSE(HasCompactScopeId(addr_.addr6))) {
      return ipv6_address_slowpath();
    }
    return addr_.addr6;
  }

  // Convenient helpers that return true if IP address is v4 or v6 respectively.
  bool is_ipv4() const { return address_family_ == AF_INET; }
  bool is_ipv6() const { return address_family_ == AF_INET6; }

  // Returns the scope_id if this is an IPv6 link-local address with a
  // compactly stored scope_id; 0U otherwise. An IPv6 link-local address
  // may not have had a scope_id assigned; in this case 0U is also returned.
  uint32_t scope_id() const {
    if (is_ipv6() && HasCompactScopeId(addr_.addr6)) {
      return ntohl(addr_.addr6.s6_addr32[1]);
    }
    return 0U;
  }

  // The address in string form, as returned by inet_ntop(). In particular,
  // this means that IPv6 addresses may be subject to zero compression
  // (e.g. "2001:700:300:1800::f" instead of "2001:700:300:1800:0:0:0:f").
  //
  std::string ToString() const;

  // Returns the same as ToString().
  // <buffer> must have room for at least INET6_ADDRSTRLEN bytes,
  // including the final NUL.
  // Returns the pointer to the terminating NUL.
  char* ToCharBuf(char* buffer) const;

  // Returns the address as a sequence of bytes in network-byte-order.
  // This is suitable for writing onto the wire or into a protocol buffer
  // as a string (proto1 syntax) or bytes (proto2 syntax).
  // IPv4 will be 4 bytes. IPv6 will be 16 bytes.
  // Can be parsed using PackedStringToIPAddress().
  std::string ToPackedString() const;

  // Static, constant IPAddresses for convenience.
  static IPAddress Any4();  // 0.0.0.0
  static IPAddress Any6();  // ::

  // IP addresses have no natural ordering, so only equality operators
  // are defined.
  bool operator==(const IPAddress& other) const {
    if (address_family_ != other.address_family_) {
      return false;
    }

    switch (address_family_) {
      case AF_INET:
        return addr_.addr4.s_addr == other.addr_.addr4.s_addr;
      case AF_INET6:
        return Equals6(other);
      default:
        // We've already verified that they've got the same address family, and
        // the only possibility at this point is AF_UNSPEC, which are all equal.
        return true;
    }
  }

  bool operator!=(const IPAddress& other) const { return !(*this == other); }

  IPAddress(const IPAddress&) = default;
  IPAddress(IPAddress&&) = default;
  IPAddress& operator=(const IPAddress&) = default;
  IPAddress& operator=(IPAddress&&) = default;

  friend absl::StatusOr<IPAddress> MakeIPAddressWithScopeId(const in6_addr&,
                                                            uint32_t);

 private:
  // Returns true if the given address is one that can make use of scope_ids;
  // false otherwise.
  //
  // Currently only IPv6 link-local unicast and link-local multicast addresses
  // fit this description. In the future, though, the IPv4 link-local unicast
  // range (169.254.0.0/16; RFC 3927) could be considered to use scope_ids.
  static bool MayUseScopeIds(const in6_addr& in6) {
    return (IN6_IS_ADDR_LINKLOCAL(&in6) || IN6_IS_ADDR_MC_LINKLOCAL(&in6));
  }

  // A much stricter test for whether in6 is a candidate for the kind of
  // scope_id compaction implemented here (cf. IPAddressMayUseScopeIds()).
  static bool MayUseCompactScopeIds(const in6_addr& in6) {
    return ((in6.s6_addr32[0] == htonl(0xfe800000U)) ||
            (in6.s6_addr32[0] == htonl(0xff020000U)));
  }

  // Test for whether in6 may safely, compactly store a scope_id.
  static bool MayStoreCompactScopeId(const in6_addr& in6) {
    return (MayUseCompactScopeIds(in6) && (in6.s6_addr32[1] == 0x0U));
  }

  // Test for whether in6 appears to have a compact scope_id stored.
  static bool HasCompactScopeId(const in6_addr& in6) {
    return (MayUseCompactScopeIds(in6) && (in6.s6_addr32[1] != 0x0U));
  }

  // Constructor that also supports an IPv6 link-local address with a scope_id.
  IPAddress(const in6_addr& addr, uint32_t scope_id)
      : addr_(addr), address_family_(AF_INET6) {
    if (ABSL_PREDICT_FALSE(MayUseScopeIds(addr_.addr6))) {
      if (MayUseCompactScopeIds(addr_.addr6)) {
        // May have been asked to explicitly overwrite one scope with another.
        addr_.addr6.s6_addr32[1] = htonl(scope_id);
      } else if (scope_id != 0) {
        ABSL_LOG(WARNING) << "Discarding scope_id; cannot be compactly stored.";
      }
    }
  }

  bool Equals6(const IPAddress& other) const {
    ABSL_DCHECK_EQ(address_family_, AF_INET6);
#if defined(__x86_64__) || defined(__powerpc64__)
    // These 64-bit CPUs have efficient implementations of UNALIGNED_LOAD64().
    uint64_t a1 =
        GOOGLESQL_INTERNAL_UNALIGNED_LOAD64(&addr_.addr6.s6_addr32[0]);
    uint64_t a2 =
        GOOGLESQL_INTERNAL_UNALIGNED_LOAD64(&addr_.addr6.s6_addr32[2]);
    uint64_t b1 =
        GOOGLESQL_INTERNAL_UNALIGNED_LOAD64(&other.addr_.addr6.s6_addr32[0]);
    uint64_t b2 =
        GOOGLESQL_INTERNAL_UNALIGNED_LOAD64(&other.addr_.addr6.s6_addr32[2]);
    return ((a1 ^ b1) | (a2 ^ b2)) == 0;
#else
    return addr_.addr6.s6_addr32[0] == other.addr_.addr6.s6_addr32[0] &&
           addr_.addr6.s6_addr32[1] == other.addr_.addr6.s6_addr32[1] &&
           addr_.addr6.s6_addr32[2] == other.addr_.addr6.s6_addr32[2] &&
           addr_.addr6.s6_addr32[3] == other.addr_.addr6.s6_addr32[3];
#endif
  }

  in6_addr ipv6_address_slowpath() const;

  // In order to conserve space, a separate scope_id field has not been
  // added to this class. Instead, IPv6 link-local addresses have their
  // scope_id stored within in6_addr.s6_addr32[1]
  //
  // If IETF recommendations for fe80::/10 and/or ff02::/16 or standard usage
  // of these prefixes ever change to include use of addresses with a non-zero
  // second 32 bits, this compaction scheme MUST be revisited.
  union Addr {
    Addr() {}
    constexpr explicit Addr(const in_addr& a4) : addr4(a4) {}
    constexpr explicit Addr(const in6_addr& a6) : addr6(a6) {}
    in_addr addr4;
    in6_addr addr6;
  } addr_;

  // Not all platforms define sa_family_t, so use a uint16.
  // On Windows, Linux, sa_family_t is uint16.
  // On OSX, sa_family_t is uint8
  uint16_t address_family_;
};

namespace ipaddress_internal {

// Truncate any IPv4, IPv6, or empty IPAddress to the specified length.
// If *length_io exceeds the number of bits in the address family, then it
// will be overwritten with the correct value.  Normal addresses will
// ABSL_CHECK-fail if the length is negative, but empty addresses ignore the
// length and write -1.
//
IPAddress TruncateIPAndLength(const IPAddress& addr, int* length_io);

// A templated Formatter for use with the strings::Join API to print
// collections of IPAddresses, SocketAddresses, or IPRanges (or anything
// with a suitable ToString() method).  See also //strings/join.h.
// TODO: Replace with calls to something better in //strings:join, once
// something better is available.
template <typename T>
struct ToStringJoinFormatter {
  void operator()(std::string* out, const T& t) const {
    out->append(t.ToString());
  }
};

}  // namespace ipaddress_internal

// Forward declaration. See definition below.
int IPAddressLength(const IPAddress& ip);

class IPRange {
 private:
  // IPRange is a tuple of (host, length).
  // Using inheritance for the host allows the compiler to pack
  // length into the unused bytes of IPAddress.
  //
  // The data is stored in a separate struct so that overload resolution
  // will remain the same as before.
  struct Data : public IPAddress {
    int16_t length;

    Data() : length(-1) {}
    Data(const Data& d) = default;
    Data& operator=(const Data&) = default;

    template <typename T>
    explicit Data(const T& host) : IPAddress(host) {}

    template <typename T>
    Data(const T& host, int16_t length_arg)
        : IPAddress(host), length(length_arg) {}

    bool operator==(const Data& other) const {
      // Compare length first, since it is cheaper.
      return length == other.length && IPAddress::operator==(other);
    }
  };

 public:
  // Default constructor. Leaves the object in an empty state.
  // The empty state is analogous to a NULL pointer; the only operations
  // that are allowed on the object are:
  //
  //   * Assignment and copy construction.
  //   * Checking for the empty state (IsInitializedRange() will return false).
  //   * Comparison (operator== and operator!=).
  //   * Logging (operator<<)
  //
  // In particular, no guarantees are made about the behavior of the
  // of string conversions and serialization, or any other accessors.
  IPRange() {}

  // Constructs an IPRange from an address and a length. Properly zeroes out
  // bits and adjusts length as required, but ABSL_CHECK-fails on negative lengths
  // (since that is inherently nonsensical). Typical examples:
  //
  //   129.240.2.3/10 => 129.192.0.0/10
  //   2001:700:300:1800::/48 => 2001:700:300::/48
  //
  //   127.0.0.1/33 => 127.0.0.1/32
  //   ::1/129 => ::1/128
  //
  //   IPAddress()/* => empty IPRange()
  //
  //   127.0.0.1/-1 => undefined (currently ABSL_CHECK-fail)
  //   ::1/-1 => undefined (currently ABSL_CHECK-fail)
  //
  IPRange(const IPAddress& host, int length)
      : data_(ipaddress_internal::TruncateIPAndLength(host, &length)) {
    data_.length = static_cast<int16_t>(length);
  }

  // Unsafe constructor from a host and prefix length.
  //
  // This is the fastest way to construct an IPRange, but the caller must
  // ensure that all inputs are strictly validated:
  //   - IPv4 host must have length 0..32
  //   - IPv6 host must have length 0..128
  //   - The host must be cleanly truncated, i.e. there must not be any bits
  //     set beyond the prefix length.
  //   - Uninitialized IPAddress() must have length -1
  //
  // For performance reasons, these constraints are only checked in debug mode.
  // Any violations will result in undefined behavior.  Callers who cannot
  // guarantee correctness should use IPRange(host, length) instead.
  static IPRange UnsafeConstruct(const IPAddress& host, int length) {
    return IPRange(host, length, /* dummy = */ 0);
  }

  // Construct an IPRange from just an IPAddress, applying the
  // address-family-specific maximum netmask length.
  explicit IPRange(const IPAddress& host)
      : data_(host, IPAddressLength(host)) {}

  // The individual parts of the subnet.
  IPAddress host() const { return data_; }
  int length() const { return data_.length; }

  // Subnets have no natural ordering, so only equality operators
  // are defined.
  bool operator==(const IPRange& other) const { return data_ == other.data_; }
  bool operator!=(const IPRange& other) const {
    return !(data_ == other.data_);
  }

  // A string representation of the subnet, in "host/length" format.
  // Examples would be "127.0.0.0/8" or "2001:700:300:1800::/64".
  std::string ToString() const {
    return absl::StrCat(data_.ToString(), "/", length());
  }

  // Convenience ranges, representing every IPv4 or IPv6 address.
  static IPRange Any4() {
    return IPRange::UnsafeConstruct(IPAddress::Any4(), 0);  // 0.0.0.0/0
  }
  static IPRange Any6() {
    return IPRange::UnsafeConstruct(IPAddress::Any6(), 0);  // ::/0
  }

  IPRange(const IPRange&) = default;
  IPRange(IPRange&&) = default;
  IPRange& operator=(const IPRange&) = default;
  IPRange& operator=(IPRange&&) = default;

 private:
  // Internal implementation of UnsafeConstruct().
  IPRange(const IPAddress& host, int length, int dummy)
      : data_(host, static_cast<int16_t>(length)) {
    ABSL_DCHECK_EQ(this->host(),
              ipaddress_internal::TruncateIPAndLength(host, &length))
        << "Host has bits set beyond the prefix length.";
    ABSL_DCHECK_EQ(this->length(), length)
        << "Length is inconsistent with address family.";
  }

  Data data_;
};

// Convert a host byte order uint32 into an IPv4 IPAddress.
//
// This is the less-evil cousin of UInt32ToIPAddress.  It can be used with
// protobufs, mathematical/bitwise operations, or any other case where the
// address is represented as an ordinary number.
//
// Example usage:
//   HostUInt32ToIPAddress(0x01020304).ToString();  // Yields "1.2.3.4"
//
IPAddress HostUInt32ToIPAddress(uint32_t address);

// Convert an IPv4 IPAddress to a uint32 in host byte order.
// This is the inverse of HostUInt32ToIPAddress().
//
// Example usage:
//   const IPAddress addr(...); // 1.2.3.4
//   IPAddressToHostUInt32(addr);  // Yields 0x01020304
//
// Will ABSL_CHECK-fail if addr does not contain an IPv4 address.
inline uint32_t IPAddressToHostUInt32(const IPAddress& addr) {
  return ntohl(addr.ipv4_address().s_addr);
}

// Convert a uint128 in host byte order to an IPv6 IPAddress
// (e.g., uint128(0, 1) will become "::1").
// Not a constructor, to make it easier to grep for the ugliness later.
IPAddress UInt128ToIPAddress(absl::uint128 bigint);

// Convert an IPv6 IPAddress to a uint128 in host byte order
// (e.g., "::1" will become uint128(0, 1)).
// Will ABSL_CHECK-fail if addr does not contain an IPv6 address,
// so use with care, and only in low-level code.
inline absl::uint128 IPAddressToUInt128(const IPAddress& addr) {
  struct in6_addr addr6 = addr.ipv6_address();
  return absl::MakeUint128(
      static_cast<uint64_t>(ntohl(addr6.s6_addr32[0])) << 32 |
          static_cast<uint64_t>(ntohl(addr6.s6_addr32[1])),
      static_cast<uint64_t>(ntohl(addr6.s6_addr32[2])) << 32 |
          static_cast<uint64_t>(ntohl(addr6.s6_addr32[3])));
}

// Parse an IPv4 or IPv6 address in textual form to an IPAddress.
// Not a constructor since it can fail (in which case it returns false,
// and the contents of "out" is undefined). If only validation is required,
// "out" can be set to nullptr.
//
// The input argument can be in whatever form inet_pton(AF_INET, ...) or
// inet_pton(AF_INET6, ...) accepts (ie. typically something like "127.0.0.1"
// or "2001:700:300:1800::f").
//
// Note that in particular, this function does not do DNS lookup.
//
ABSL_MUST_USE_RESULT bool StringToIPAddress(const absl::string_view str,
                                            IPAddress* out);

// Parse an IPv4 or IPv6 address in textual form to an IPAddress.
//
// This difference between this function and others is that this function
// additionally understands IPv6 addresses with scope identifiers (either
// numerical interface indexes or interface names) and can return properly
// scoped IP addresses (see MakeIPAddressWithScopeId() above).
absl::StatusOr<IPAddress> StringToIPAddressWithOptionalScope(
    absl::string_view str);

// StringToIPAddress conversion methods that ABSL_CHECK()-fail on invalid input.
// Not a good idea to use on user-provided input.
inline IPAddress StringToIPAddressOrDie(absl::string_view str) {
  IPAddress ip;
  ABSL_CHECK(StringToIPAddress(str, &ip)) << "Invalid IP " << str;
  return ip;
}
// Parse a "binary" or packed string containing an IPv4 or IPv6 address in
// non-textual, network-byte-order form to an IPAddress.  Not a constructor
// since it can fail (in which case it returns false, and the contents of
// "out" is undefined). If only validation is required, "out" can be set to
// nullptr.
ABSL_MUST_USE_RESULT bool PackedStringToIPAddress(absl::string_view str,
                                                  IPAddress* out);
// Binary packed string conversion methods that ABSL_CHECK()-fail on invalid input.
inline IPAddress PackedStringToIPAddressOrDie(absl::string_view str) {
  IPAddress ip;
  ABSL_CHECK(PackedStringToIPAddress(str, &ip))
      << "Invalid packed IP address of length " << str.length();
  return ip;
}

// For debugging/logging. Note that as a special case, you can log an
// uninitialized IP address, although you cannot use ToString() on it.
inline std::ostream& operator<<(std::ostream& stream,
                                const IPAddress& address) {
  switch (address.address_family()) {
    case AF_INET:
    case AF_INET6:
      return stream << address.ToString();
    case AF_UNSPEC:
      return stream << "<uninitialized IPAddress>";
    default:
      return stream << "<corrupt IPAddress with family="
                    << address.address_family() << ">";
  }
}

// Return the family-dependent length (in bits) of an IP address given an
// IPAddress object.  A debug-fatal error is logged if the address family
// is not of the Internet variety, i.e. not one of set(AF_INET, AF_INET6);
// the caller is responsible for verifying IsInitializedAddress(ip).
inline int IPAddressLength(const IPAddress& ip) {
  switch (ip.address_family()) {
    case AF_INET:
      return 32;
    case AF_INET6:
      return 128;
    default:
      ABSL_LOG(ERROR) << "IPAddressLength() of object with invalid address family: "
                 << ip.address_family();
      return -1;
  }
}

//
// Parse an IPv4 or IPv6 subnet mask in textual form into an IPRange.
// Not a constructor since it can fail (in which case it returns false,
// and the contents of "out" is undefined). If only validation is required,
// "out" can be set to nullptr.
//
// Note that an improperly zeroed out mask (say, 192.168.0.0/8) will be
// rejected as invalid by this function. If you instead want the excess bits to
// be zeroed out silently, see StringToIPRangeAndTruncate(), below.
//
// The format accepted is the same that is output by IPRange::ToString().
// Any IP addresses without a "/netmask" will be given an implicit
// CIDR netmask length equal to the number of bits in the address
// family (e.g. /32 or /128).  Additionally, IPv4 ranges may have a netmask
// specifier in the older dotted quad format, e.g. "/255.255.0.0".
//
ABSL_MUST_USE_RESULT bool StringToIPRange(absl::string_view str, IPRange* out);

// StringToIPRange conversion methods that ABSL_CHECK()-fail on invalid input.
// Not a good idea to use on user-provided input.
inline IPRange StringToIPRangeOrDie(absl::string_view str) {
  IPRange ipr;
  ABSL_CHECK(StringToIPRange(str, &ipr)) << "Invalid IP range " << str;
  return ipr;
}

//
// The same as StringToIPRange and StringToIPRangeOrDie, but truncating instead
// of returning an error in the event of an improperly zeroed out mask (ie.,
// 192.168.0.0/8 will automatically be changed to 192.0.0.0/8).
//
ABSL_MUST_USE_RESULT bool StringToIPRangeAndTruncate(absl::string_view str,
                                                     IPRange* out);
inline IPRange StringToIPRangeAndTruncateOrDie(absl::string_view str) {
  IPRange ipr;
  ABSL_CHECK(StringToIPRangeAndTruncate(str, &ipr)) << "Invalid IP range " << str;
  return ipr;
}
// For debugging/logging.
inline std::ostream& operator<<(std::ostream& stream, const IPRange& range) {
  if (range.host().address_family() == AF_UNSPEC) {
    return stream << "<uninitialized IPRange>";
  } else {
    return stream << range.ToString();
  }
}

// Checks whether the given IP address "needle" is within the IP range
// "haystack".  Note that an IPv4 address is never considered to be within an
// IPv6 range, and vice versa.
inline bool IsWithinSubnet(const IPRange& haystack, const IPAddress& needle) {
  return haystack.host().address_family() == needle.address_family() &&
         haystack == IPRange(needle, haystack.length());
}

// Checks whether the given IP range "needle" is properly contained within
// the IP range "haystack", i.e. whether "needle" is a more specific of
// "haystack".  Note that an IPv4 range is never considered to be contained
// within an IPv6 range, and vice versa.
inline bool IsProperSubRange(const IPRange& haystack, const IPRange& needle) {
  return haystack.length() < needle.length() &&
         IsWithinSubnet(haystack, needle.host());
}

static_assert(sizeof(IPAddress) == 20, "IPAddress should be 20 bytes");
static_assert(sizeof(IPRange) == 20, "IPRange should be 20 bytes");

}  // namespace googlesql::internal

#endif  // THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_NET_IPADDRESS_OSS_H_
