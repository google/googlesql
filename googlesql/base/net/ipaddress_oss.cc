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
// Definition of classes IPAddress and IPRange.

#include "googlesql/base/net/ipaddress_oss.h"

#include <arpa/inet.h>
#include <net/if.h>
#include <netdb.h>

#include <iterator>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "absl/numeric/int128.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "googlesql/base/endian.h"
#include "googlesql/base/logging.h"

namespace googlesql::internal {

// Sanity check: be sure INET_ADDRSTRLEN fits into INET6_ADDRSTRLEN.
// ToCharBuf() (below) depends on this.
static_assert(INET_ADDRSTRLEN <= INET6_ADDRSTRLEN, "ipv6_larger_than_ipv4");

namespace {

const int kMaxNetmaskIPv4 = 32;
const int kMaxNetmaskIPv6 = 128;

IPAddress MakeIPAddressWithOptionalScopeId(absl::uint128 ip6uint,
                                           uint32_t scope_id) {
  const IPAddress ip6(UInt128ToIPAddress(ip6uint));
  const auto rval = MakeIPAddressWithScopeId(ip6.ipv6_address(), scope_id);
  return rval.ok() ? rval.value() : ip6;
}

}  // namespace

IPAddress IPAddress::Any4() {
  return HostUInt32ToIPAddress(INADDR_ANY);
}

IPAddress IPAddress::Any6() {
  return IPAddress(in6addr_any);
}

in6_addr IPAddress::ipv6_address_slowpath() const {
  ABSL_CHECK_EQ(AF_INET6, address_family_);
  if (ABSL_PREDICT_FALSE(HasCompactScopeId(addr_.addr6))) {
    in6_addr copy = addr_.addr6;
    copy.s6_addr32[1] = 0;  // clear the scope_id (interface index)
    return copy;
  }
  return addr_.addr6;
}

IPAddress HostUInt32ToIPAddress(uint32_t address) {
  in_addr addr;
  addr.s_addr = htonl(address);
  return IPAddress(addr);
}

IPAddress UInt128ToIPAddress(const absl::uint128 bigint) {
  in6_addr addr6;
  addr6.s6_addr32[0] = googlesql_base::ghtonl(
      static_cast<uint32_t>(absl::Uint128High64(bigint) >> 32));
  addr6.s6_addr32[1] = googlesql_base::ghtonl(
      static_cast<uint32_t>(absl::Uint128High64(bigint) & 0xFFFFFFFFULL));
  addr6.s6_addr32[2] = googlesql_base::ghtonl(
      static_cast<uint32_t>(absl::Uint128Low64(bigint) >> 32));
  addr6.s6_addr32[3] = googlesql_base::ghtonl(
      static_cast<uint32_t>(absl::Uint128Low64(bigint) & 0xFFFFFFFFULL));
  return IPAddress(addr6);
}

namespace {

void AppendIPv4ToString(const uint8_t* octets, std::string* out) {
  absl::StrAppendFormat(out, "%d.%d.%d.%d", octets[0], octets[1], octets[2],
                        octets[3]);
}

// Returns start of the longest sequence of zero words of length at least 2.
// Returns -1 if no such sequence exists. Returns first such sequence in the
// event of multiple such sequences.
int FindLongestZeroWordSequence(const uint16_t* addr) {
  int cnt = 0;
  int best_len = 1;
  int best_start = -1;
  for (int i = 0; i < 8; ++i) {
    if (addr[i] == 0) {
      ++cnt;
      if (cnt > best_len) {
        best_len = cnt;
        best_start = i + 1 - cnt;
      }
    } else {
      cnt = 0;
    }
  }
  return best_start;
}

void AppendIPv6ToString(const in6_addr& addr, std::string* out) {
  if (addr.s6_addr32[0] == 0 && addr.s6_addr32[1] == 0) {
    // If lower half of address is zero, it starts with :: and it may be
    // embedded IPv4 address.
    out->push_back(':');
    // Check for IPv6 embedded IPv4 address.
    if (addr.s6_addr16[4] == 0 &&
        (addr.s6_addr16[5] == 0xffff ||
         (addr.s6_addr16[5] == 0 && addr.s6_addr16[6] != 0))) {
      if (addr.s6_addr16[5] != 0) {
        absl::StrAppend(out, ":ffff");
      }
      out->push_back(':');
      AppendIPv4ToString(&addr.s6_addr[12], out);
      return;
    }
    int i = 4;
    // Skip remaining zero words.
    while (i < 8 && addr.s6_addr16[i] == 0) {
      ++i;
    }
    if (i < 8) {
      for (; i < 8; ++i) {
        absl::StrAppend(out, ":",
                        absl::Hex(googlesql_base::gntohs(addr.s6_addr16[i])));
      }
    } else {
      out->push_back(':');
    }
  } else {
    const int start = FindLongestZeroWordSequence(addr.s6_addr16);
    for (int i = 0; i < 8; ++i) {
      if (i == start) {
        // At least two words are guaranteed to be zero.
        i += 2;
        while (i < 8 && addr.s6_addr16[i] == 0) {
          ++i;
        }
        out->push_back(':');
        if (i == 8) {
          out->push_back(':');
          break;
        }
      }
      if (i) {
        out->push_back(':');
      }
      absl::StrAppend(out,
                      absl::Hex(googlesql_base::gntohs(addr.s6_addr16[i])));
    }
  }
}

}  // namespace

std::string IPAddress::ToString() const {
  std::string out;
  out.reserve(INET6_ADDRSTRLEN + 1);
  switch (address_family_) {
    case AF_INET:
      AppendIPv4ToString(reinterpret_cast<const uint8_t*>(&addr_.addr4.s_addr),
                         &out);
      break;
    case AF_INET6:
      if (ABSL_PREDICT_FALSE(HasCompactScopeId(addr_.addr6))) {
        AppendIPv6ToString(ipv6_address(), &out);
      } else {
        AppendIPv6ToString(addr_.addr6, &out);
      }
      break;
    case AF_UNSPEC:
      ABSL_LOG(ERROR) << "Calling ToCharBuf() on an empty IPAddress";
      return "";
      break;
    default:
      ABSL_LOG(FATAL) << "Unknown address family " << address_family_;
  }
  return out;
}

std::string IPAddress::ToPackedString() const {
  switch (address_family_) {
    case AF_INET:
      return std::string(reinterpret_cast<const char*>(&addr_.addr4),
                         sizeof(addr_.addr4));
    case AF_INET6:
      if (ABSL_PREDICT_FALSE(HasCompactScopeId(addr_.addr6))) {
        // Calling ToPackedString() on an IPv6 link-local address is somewhat
        // suspect. When later de-serialized, even on the same machine, there
        // is no inherent guarantee that a given interface index remains valid.
        // For now, output them the same way as their un-scoped cousins --
        // what to do with the interface index and/or name is likely to be an
        // application-dependent matter.
        GOOGLESQL_VLOG(2) << "ToPackedString() dropping scope ID";
        const auto addr6 = ipv6_address();
        return std::string(reinterpret_cast<const char*>(&addr6),
                           sizeof(addr6));
      } else {
        return std::string(reinterpret_cast<const char*>(&addr_.addr6),
                           sizeof(addr_.addr6));
      }
    case AF_UNSPEC:
      ABSL_LOG(ERROR) << "Calling ToPackedString() on an empty IPAddress";
      return "";
    default:
      ABSL_LOG(FATAL) << "Unknown address family " << address_family_;
  }
}

absl::StatusOr<IPAddress> MakeIPAddressWithScopeId(const in6_addr& addr,
                                                   uint32_t scope_id) {
  if (scope_id == 0) return IPAddress(addr);

  if (!IPAddress::MayUseScopeIds(addr)) {
    return absl::InvalidArgumentError("address does not use scope_ids");
  } else if (!IPAddress::MayUseCompactScopeIds(addr)) {
    return absl::InvalidArgumentError("address cannot use compact scope_ids");
  } else if (!IPAddress::MayStoreCompactScopeId(addr)) {
    return absl::InvalidArgumentError("address cannot safely compact scope_id");
  }

  return IPAddress(addr, scope_id);
}

bool StringToIPAddress(const char* str, IPAddress* out) {
  // Try to parse the string as an IPv4 address first. (glibc does not
  // yet recognize IPv6 addresses when given an address family of
  // AF_INET, but at some point it will, and at that point the other way
  // around would break.)
  in_addr addr4;
  if (str && inet_pton(AF_INET, str, &addr4) > 0) {
    if (out) {
      *out = IPAddress(addr4);
    }
    return true;
  }

  in6_addr addr6;
  if (str && inet_pton(AF_INET6, str, &addr6) > 0) {
    if (out) {
      *out = IPAddress(addr6);
    }
    return true;
  }

  return false;
}

bool StringToIPAddress(const absl::string_view str, IPAddress* out) {
  // We spend a lot of time in this routine, so make a zero-terminated
  // copy of the piece on the stack if it's short, rather than constructing
  // a temporary string.
  if (str.size() <= INET6_ADDRSTRLEN) {
    char buf[INET6_ADDRSTRLEN + 1];
    buf[str.size()] = '\0';
    memcpy(buf, str.data(), str.size());
    return StringToIPAddress(buf, out);
  } else {
    return StringToIPAddress(std::string(str).c_str(), out);
  }
}

namespace {

// Maps error values from getaddrinfo(3) to canonical Status codes.
absl::Status InternalGetaddrinfoErrorToStatus(int rval, int copied_errno) {
  if (rval == 0) return absl::OkStatus();

  const char* error_str = gai_strerror(rval);
  // Note that getaddrinfo is only guaranteed to set errno when the return value
  // is EAI_SYSTEM.  Otherwise, errno is unreliable.
  //
  // Plausible error values are from
  // https://tools.ietf.org/html/rfc3493#section-6.1
  switch (rval) {
    case EAI_AGAIN:
      return absl::UnavailableError(absl::StrCat("EAI_AGAIN: ", error_str));
    case EAI_BADFLAGS:
      return absl::InvalidArgumentError(
          absl::StrCat("EAI_BADFLAGS: ", error_str));
    case EAI_FAIL:
      return absl::NotFoundError(absl::StrCat("EAI_FAIL: ", error_str));
    case EAI_FAMILY:
      return absl::InvalidArgumentError(
          absl::StrCat("EAI_FAMILY: ", error_str));
    case EAI_MEMORY:
      return absl::ResourceExhaustedError(
          absl::StrCat("EAI_MEMORY: ", error_str));
    case EAI_NONAME:
      return absl::NotFoundError(absl::StrCat("EAI_NONAME: ", error_str));
    case EAI_SERVICE:
      return absl::InvalidArgumentError(
          absl::StrCat("EAI_SERVICE: ", error_str));
    case EAI_SOCKTYPE:
      return absl::InvalidArgumentError(
          absl::StrCat("EAI_SOCKTYPE: ", error_str));
    default:
      return absl::UnknownError(
          absl::StrCat("getaddrinfo returned ", rval, " (", error_str, ")"));
  }
}

}  // namespace

absl::StatusOr<IPAddress> StringToIPAddressWithOptionalScope(
    const absl::string_view str) {
  const auto scope_delimiter = str.rfind('%');
  if (scope_delimiter == absl::string_view::npos) {
    IPAddress ip{};
    if (StringToIPAddress(str, &ip)) {
      return ip;
    } else {
      return absl::InvalidArgumentError("bad IP string literal");
    }
  }

  // Addresses with a scope delimiter ('%') but without a following zone_id
  // does not seem to comport with any of this text:
  //
  //     https://tools.ietf.org/html/rfc4007#section-11.2
  //     https://tools.ietf.org/html/rfc4007#section-11.6
  //     https://tools.ietf.org/html/rfc6874#section-2
  //
  // However, it seems at least one getaddrinfo() implementation accepts this
  // syntax. Until further review of text and use cases comes to a different
  // conclusion, check for this case and return an error.
  if (str.substr(scope_delimiter).size() == 1) {  // EndsWith('%')
    return absl::InvalidArgumentError("missing zone_id");
  }

  const std::string str_null_terminated(str);

  // Trust getaddrinfo()'s ability to parse scope_ids and interface names.
  struct addrinfo hints {};
  hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
  hints.ai_family = AF_INET6;
  // Hint that getaddrinfo() need not return a linked list of answers.
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_protocol = IPPROTO_UDP;

  struct addrinfo* res{nullptr};
  const int rval =
      getaddrinfo(str_null_terminated.c_str(), nullptr, &hints, &res);
  std::unique_ptr<struct addrinfo, decltype(&freeaddrinfo)> cleanup(
      res, freeaddrinfo);
  if (rval != 0) {
    return InternalGetaddrinfoErrorToStatus(rval, errno);
  }
  if (res == nullptr || res->ai_addr == nullptr ||
      res->ai_addrlen < sizeof(struct sockaddr_in6)) {
    return absl::InternalError("getaddrinfo returned nonsensical response");
  }
  const auto* sin6 = reinterpret_cast<sockaddr_in6*>(res->ai_addr);
  return MakeIPAddressWithScopeId(sin6->sin6_addr, sin6->sin6_scope_id);
}

bool PackedStringToIPAddress(absl::string_view str, IPAddress* out) {
  if (str.length() == sizeof(in_addr)) {
    if (out) {
      in_addr addr;
      memcpy(&addr, str.data(), sizeof(addr));
      *out = IPAddress(addr);
    }
    return true;
  } else if (str.length() == sizeof(in6_addr)) {
    if (out) {
      in6_addr addr;
      memcpy(&addr, str.data(), sizeof(addr));
      *out = IPAddress(addr);
    }
    return true;
  }

  return false;
}

namespace {

bool InternalStringToNetmaskLength(absl::string_view str,
                                   int host_address_family, int32_t* out) {
  ABSL_DCHECK(out);

  // Explicitly check that the first and last characters are digits, because
  // SimpleAtoi will accept whitespace, +, -, etc.
  if (str.empty() || !absl::ascii_isdigit(*str.begin()) ||
      !absl::ascii_isdigit(*str.rbegin())) {
    return false;
  }

  // Check for a decimal number.
  if (absl::SimpleAtoi(str, out)) {
    ABSL_DCHECK_GE(*out, 0);
    const int max_length =
        host_address_family == AF_INET6 ? kMaxNetmaskIPv6 : kMaxNetmaskIPv4;
    return *out <= max_length;
  }

  // Check for a netmask in dotted quad form, e.g. "255.255.0.0".
  in_addr mask;
  if (host_address_family == AF_INET &&
      inet_pton(AF_INET, std::string(str).c_str(), &mask) > 0) {
    if (mask.s_addr == 0) {
      *out = 0;
    } else {
      // Now we check to make sure we have a sane netmask.
      // The inverted mask in native byte order (+1) will have to be a
      // power of two, if it's valid.
      uint32_t inv_mask = (~googlesql_base::gntohl(mask.s_addr)) + 1;
      // Power of two iff x & (x - 1) == 0.
      if ((inv_mask & (inv_mask - 1)) != 0) {
        return false;
      }
      *out = 32 - __builtin_ffs(googlesql_base::gntohl(mask.s_addr)) + 1;
    }
    return true;
  }

  return false;
}

//
// The "meat" of StringToIPRange{,AndTruncate}. Does no checking of correct
// prefix length, nor any automatic truncation.
//
bool InternalStringToIPRange(absl::string_view str,
                             std::pair<IPAddress, int>* out) {
  ABSL_DCHECK(out);

  // Try to parse everything before the slash as an IP address.
  // If there is no slash, then substr(0, npos) yields the full string.
  const size_t slash_pos = str.find('/');
  if (!StringToIPAddress(str.substr(0, slash_pos), &out->first)) {
    return false;
  }

  // Try to parse everything after the slash as a prefix length.
  if (slash_pos != absl::string_view::npos) {
    return InternalStringToNetmaskLength(
        absl::ClippedSubstr(str, slash_pos + 1), out->first.address_family(),
        &out->second);
  }

  // There was no slash, so the range covers a single address.
  out->second = IPAddressLength(out->first);
  return true;
}

}  // namespace

bool StringToIPRange(absl::string_view str, IPRange* out) {
  std::pair<IPAddress, int> parsed;
  if (!InternalStringToIPRange(str, &parsed)) {
    return false;
  }
  const IPRange result(parsed.first, parsed.second);
  if (result.host() != parsed.first) {
    // Some bits were truncated.
    return false;
  }
  if (out) {
    *out = result;
  }
  return true;
}

bool StringToIPRangeAndTruncate(absl::string_view str, IPRange* out) {
  std::pair<IPAddress, int> parsed;
  if (!InternalStringToIPRange(str, &parsed)) {
    return false;
  }
  if (out) {
    *out = IPRange(parsed.first, parsed.second);
  }
  return true;
}

namespace ipaddress_internal {
IPAddress TruncateIPAndLength(const IPAddress& addr, int* length_io) {
  const int length = *length_io;
  switch (addr.address_family()) {
    case AF_INET: {
      if (length >= kMaxNetmaskIPv4) {
        *length_io = kMaxNetmaskIPv4;
        return addr;
      } else if (length > 0) {
        uint32_t ip4 = IPAddressToHostUInt32(addr);
        ip4 &= ~0U << (32 - length);
        return HostUInt32ToIPAddress(ip4);
      } else if (length == 0) {
        return IPAddress::Any4();
      }
      break;
    }
    case AF_INET6: {
      if (length >= kMaxNetmaskIPv6) {
        *length_io = kMaxNetmaskIPv6;
        return addr;
      } else if (length > 0) {
        absl::uint128 ip6 = IPAddressToUInt128(addr);
        ip6 &= ~absl::uint128(0) << (128 - length);
        return MakeIPAddressWithOptionalScopeId(ip6, addr.scope_id());
      } else if (length == 0) {
        return IPAddress::Any6();
      }
      break;
    }
    case AF_UNSPEC:
      *length_io = -1;
      return addr;
  }
  ABSL_LOG(ERROR) << "Invalid truncation: " << addr << "/" << length;
  *length_io = -1;
  return IPAddress();
}

}  // namespace ipaddress_internal

}  // namespace googlesql::internal
