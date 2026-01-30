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

#include "googlesql/public/functions/net.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>

#include "googlesql/public/functions/util.h"
#include "absl/base/optimization.h"
#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/net/idn.h"
#include "googlesql/base/net/ipaddress.h"
#include "googlesql/base/net/public_suffix.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace functions {
namespace net {

bool FormatIP(int64_t in, std::string* out, absl::Status* error) {
  if (in < 0) {
    internal::UpdateError(
        error, "NET.FORMAT_IP() encountered a negative integer");
    return false;
  }
  if (in > std::numeric_limits<uint32_t>::max()) {
    internal::UpdateError(
        error, "NET.FORMAT_IP() encountered an invalid integer IP");
    return false;
  }
  *out = googlesql::internal::HostUInt32ToIPAddress(static_cast<uint32_t>(in))
             .ToString();
  return true;
}

bool IPv4FromInt64(int64_t in, std::string* out, absl::Status* error) {
  if (ABSL_PREDICT_TRUE(in >= -0x80000000LL && in <= 0xFFFFFFFFLL)) {
    uint32_t v = htonl(static_cast<uint32_t>(in));
    out->assign(reinterpret_cast<char*>(&v), sizeof(v));
    return true;
  }
  internal::UpdateError(
      error,
      absl::StrCat("NET.IPV4_FROM_INT64() encountered an invalid integer IP. "
                   "Expected range: [-0x80000000, 0xFFFFFFFF]; got ",
                   in));
  return false;
}

bool ParseIP(absl::string_view in, int64_t* out, absl::Status* error) {
  googlesql::internal::IPAddress addr;
  if (!googlesql::internal::StringToIPAddress(in, &addr)) {
    internal::UpdateError(
        error, "NET.PARSE_IP() encountered an unparseable IP-address");
    return false;
  }
  if (addr.address_family() != AF_INET) {
    internal::UpdateError(
        error, "NET.PARSE_IP() encountered a non-IPv4 address");
    return false;
  }
  *out = googlesql::internal::IPAddressToHostUInt32(addr);
  return true;
}

bool IPv4ToInt64(absl::string_view in, int64_t* out, absl::Status* error) {
  uint32_t v;
  if (ABSL_PREDICT_TRUE(in.size() == sizeof(v))) {
    memcpy(&v, in.data(), sizeof(v));
    *out = ntohl(v);
    return true;
  }
  internal::UpdateError(
      error, absl::StrCat("NET.IPV4_TO_INT64() encountered a non-IPv4 address. "
                          "Expected 4 bytes but got ",
                          in.size()));
  return false;
}

bool FormatPackedIP(absl::string_view in, std::string* out,
                    absl::Status* error) {
  googlesql::internal::IPAddress addr;
  if (!googlesql::internal::PackedStringToIPAddress(in, &addr)) {
    internal::UpdateError(error, "NET.FORMAT_PACKED_IP() encountered an "
                          "invalid packed IP-address. Packed addresses must be "
                          "of type bytes and have length 4 or 16.");
    return false;
  }
  *out = addr.ToString();
  return true;
}

bool IPToString(absl::string_view in, std::string* out, absl::Status* error) {
  googlesql::internal::IPAddress addr;
  if (ABSL_PREDICT_FALSE(
          !googlesql::internal::PackedStringToIPAddress(in, &addr))) {
    internal::UpdateError(
        error,
        absl::StrCat("NET.IP_TO_STRING() encountered an invalid IP address. "
                     "Expected 4 (for IPv4) or 16 (for IPv6) bytes but got ",
                     in.size()));
    return false;
  }
  *out = addr.ToString();
  return true;
}

bool ParsePackedIP(absl::string_view in, std::string* out,
                   absl::Status* error) {
  googlesql::internal::IPAddress addr;
  if (!googlesql::internal::StringToIPAddress(in, &addr)) {
    internal::UpdateError(
        error, "NET.PARSE_PACKED_IP() encountered an unparseable IP-address");
    return false;
  }
  *out = addr.ToPackedString();
  return true;
}

static bool InternalIPFromString(absl::string_view in, std::string* out) {
  if (ABSL_PREDICT_TRUE(in.size() < INET6_ADDRSTRLEN &&
                        memchr(in.data(), '\0', in.size()) == nullptr)) {
    // inet_pton needs a NULL-terminated string, so we have to make a copy.
    char in_str[INET6_ADDRSTRLEN];
    memcpy(in_str, in.data(), in.size());
    in_str[in.size()] = '\0';
    out->resize(sizeof(in_addr));
    if (ABSL_PREDICT_TRUE(inet_pton(AF_INET, in_str, &(*out)[0]) > 0)) {
      return true;
    }
    out->resize(sizeof(in6_addr));
    if (ABSL_PREDICT_TRUE(inet_pton(AF_INET6, in_str, &(*out)[0]) > 0)) {
      return true;
    }
  }
  return false;
}

bool IPFromString(absl::string_view in, std::string* out, absl::Status* error) {
  if (ABSL_PREDICT_FALSE(!InternalIPFromString(in, out))) {
    internal::UpdateError(
        error,
        absl::StrCat(
            "NET.IP_FROM_STRING() encountered an unparseable IP-address: ",
            in));
    return false;
  }
  return true;
}

absl::Status SafeIPFromString(absl::string_view in, std::string* out,
                              bool* is_null) {
  *is_null = !InternalIPFromString(in, out);
  return absl::OkStatus();
}

bool IPNetMask(int64_t output_length_bytes, int64_t prefix_length_bits,
               std::string* out, absl::Status* error) {
  if (ABSL_PREDICT_FALSE(output_length_bytes != sizeof(in_addr) &&
                         output_length_bytes != sizeof(in6_addr))) {
    internal::UpdateError(
        error, absl::StrCat("The first argument of NET.IP_NET_MASK() must "
                            "be either 4 (for IPv4) or 16 (for IPv6); got ",
                            output_length_bytes));
    return false;
  }
  if (ABSL_PREDICT_FALSE(prefix_length_bits < 0 ||
                         prefix_length_bits > output_length_bytes * 8)) {
    internal::UpdateError(
        error,
        absl::StrCat("The second argument of NET.IP_NET_MASK() must be between "
                     "0 and the first argument * 8; got ",
                     prefix_length_bits));
    return false;
  }
  out->assign(output_length_bytes, '\0');
  const int64_t num_bytes_with_all_ones = prefix_length_bits / 8;
  uint8_t* out_data = reinterpret_cast<uint8_t*>(&(*out)[0]);
  memset(out_data, '\xff', num_bytes_with_all_ones);
  const uint8_t remaining_bits = prefix_length_bits % 8;
  if (remaining_bits > 0) {
    out_data[num_bytes_with_all_ones] =
        static_cast<uint8_t>(0xff00u >> remaining_bits);
  }
  return true;
}

bool IPTrunc(absl::string_view in, int64_t prefix_length_bits, std::string* out,
             absl::Status* error) {
  if (ABSL_PREDICT_FALSE(in.size() != sizeof(in_addr) &&
                         in.size() != sizeof(in6_addr))) {
    internal::UpdateError(
        error,
        absl::StrCat("The first argument of NET.IP_TRUNC() must have length "
                     "4 (for IPv4) or 16 (for IPv6); got ",
                     in.size()));
    return false;
  }
  if (ABSL_PREDICT_FALSE(prefix_length_bits < 0 ||
                         prefix_length_bits > in.size() * 8)) {
    internal::UpdateError(
        error, absl::StrCat("The second argument of NET.IP_TRUNC() must be "
                            "between 0 and 8 * LENGTH(first argument); got ",
                            prefix_length_bits));
    return false;
  }
  out->assign(in.data(), in.size());
  const int64_t bytes_to_keep = prefix_length_bits / 8;
  uint8_t* out_data = reinterpret_cast<uint8_t*>(&(*out)[0]) + bytes_to_keep;
  memset(out_data, 0, in.size() - bytes_to_keep);
  const uint8_t remaining_bits = prefix_length_bits % 8;
  if (remaining_bits > 0) {
    *out_data = static_cast<uint8_t>(in[bytes_to_keep]) &
                static_cast<uint8_t>(0xff00u >> remaining_bits);
  }
  return true;
}

bool IPInNet(absl::string_view ip, absl::string_view net, bool* out,
             absl::Status* error) {
  googlesql::internal::IPRange ip_range;
  if (!googlesql::internal::StringToIPRangeAndTruncate(ip, &ip_range)) {
    internal::UpdateError(
        error, "NET.IP_IN_NET() encountered an unparseable IP-address");
    return false;
  }
  googlesql::internal::IPRange net_range;
  if (!googlesql::internal::StringToIPRangeAndTruncate(net, &net_range)) {
    internal::UpdateError(
        error, "NET.IP_IN_NET() encountered an unparseable net");
    return false;
  }
  *out = net_range == ip_range ||
         googlesql::internal::IsProperSubRange(net_range, ip_range);
  return true;
}

bool MakeNet(absl::string_view ip_string, int32_t subnet_size, std::string* out,
             absl::Status* error) {
  if (subnet_size < 0) {
    internal::UpdateError(
        error, "NET.MAKE_NET() encountered a negative length");
    return false;
  }
  googlesql::internal::IPRange range;
  if (!googlesql::internal::StringToIPRangeAndTruncate(ip_string, &range)) {
    internal::UpdateError(
        error, "NET.MAKE_NET() encountered an unparseable IP-address");
    return false;
  }
  if (subnet_size > range.length()) {
    internal::UpdateError(
        error, "NET.MAKE_NET() is trying to expand a subnet");
    return false;
  }
  *out = googlesql::internal::IPRange(range.host(), subnet_size).ToString();
  return true;
}

namespace {

enum HostCharMapEnum {
  kNetLocEnd = 0x40,  // end of network location (/?#\)
  kNormalize = 0x08,  // needs to be normalized before public suffix lookup
  kSlash = 0x02,      // \ or /
  kBracket = 0x01,    // [ or ]
};

static const char kHostCharMap[256] = {
  0,                                 // 00
  0,                                 // 01
  0,                                 // 02
  0,                                 // 03
  0,                                 // 04
  0,                                 // 05
  0,                                 // 06
  0,                                 // 07
  0,                                 // 08
  0,                                 // 09
  0,                                 // 0A
  0,                                 // 0B
  0,                                 // 0C
  0,                                 // 0D
  0,                                 // 0E
  0,                                 // 0F
  0,                                 // 10
  0,                                 // 11
  0,                                 // 12
  0,                                 // 13
  0,                                 // 14
  0,                                 // 15
  0,                                 // 16
  0,                                 // 17
  0,                                 // 18
  0,                                 // 19
  0,                                 // 1A
  0,                                 // 1B
  0,                                 // 1C
  0,                                 // 1D
  0,                                 // 1E
  0,                                 // 1F
  0,                                 // 20
  0,                                 // 21
  0,                                 // 22
  kNetLocEnd,                        // 23 '#'
  0,                                 // 24 '$'
  0,                                 // 25 '%'
  0,                                 // 26 '&'
  0,                                 // 27 '''
  0,                                 // 28 '('
  0,                                 // 29 ')'
  0,                                 // 2A '*'
  0,                                 // 2B '+'
  0,                                 // 2C ','
  0,                                 // 2D '-'
  0,                                 // 2E '.'
  kNetLocEnd | kSlash,               // 2F '/'
  0,                                 // 30 '0'
  0,                                 // 31 '1'
  0,                                 // 32 '2'
  0,                                 // 33 '3'
  0,                                 // 34 '4'
  0,                                 // 35 '5'
  0,                                 // 36 '6'
  0,                                 // 37 '7'
  0,                                 // 38 '8'
  0,                                 // 39 '9'
  0,                                 // 3A ':'
  0,                                 // 3B ';'
  0,                                 // 3C '<'
  0,                                 // 3D '='
  0,                                 // 3E '>'
  kNetLocEnd,                        // 3F '?'
  0,                                 // 40 '@'
  kNormalize,                        // 41 'A'
  kNormalize,                        // 42 'B'
  kNormalize,                        // 43 'C'
  kNormalize,                        // 44 'D'
  kNormalize,                        // 45 'E'
  kNormalize,                        // 46 'F'
  kNormalize,                        // 47 'G'
  kNormalize,                        // 48 'H'
  kNormalize,                        // 49 'I'
  kNormalize,                        // 4A 'J'
  kNormalize,                        // 4B 'K'
  kNormalize,                        // 4C 'L'
  kNormalize,                        // 4D 'M'
  kNormalize,                        // 4E 'N'
  kNormalize,                        // 4F 'O'
  kNormalize,                        // 50 'P'
  kNormalize,                        // 51 'Q'
  kNormalize,                        // 52 'R'
  kNormalize,                        // 53 'S'
  kNormalize,                        // 54 'T'
  kNormalize,                        // 55 'U'
  kNormalize,                        // 56 'V'
  kNormalize,                        // 57 'W'
  kNormalize,                        // 58 'X'
  kNormalize,                        // 59 'Y'
  kNormalize,                        // 5A 'Z'
  kBracket,                          // 5B '['
  kNetLocEnd | kSlash,               // 5C '\'
  kBracket,                          // 5D ']'
  0,                                 // 5E '^'
  0,                                 // 5F '_'
  0,                                 // 60 '`'
  0,                                 // 61 'a'
  0,                                 // 62 'b'
  0,                                 // 63 'c'
  0,                                 // 64 'd'
  0,                                 // 65 'e'
  0,                                 // 66 'f'
  0,                                 // 67 'g'
  0,                                 // 68 'h'
  0,                                 // 69 'i'
  0,                                 // 6A 'j'
  0,                                 // 6B 'k'
  0,                                 // 6C 'l'
  0,                                 // 6D 'm'
  0,                                 // 6E 'n'
  0,                                 // 6F 'o'
  0,                                 // 70 'p'
  0,                                 // 71 'q'
  0,                                 // 72 'r'
  0,                                 // 73 's'
  0,                                 // 74 't'
  0,                                 // 75 'u'
  0,                                 // 76 'v'
  0,                                 // 77 'w'
  0,                                 // 78 'x'
  0,                                 // 79 'y'
  0,                                 // 7A 'z'
  0,                                 // 7B '{'
  0,                                 // 7C '|'
  0,                                 // 7D '}'
  0,                                 // 7E '~'
  kNormalize,                        // 7F
  kNormalize,                        // 80
  kNormalize,                        // 81
  kNormalize,                        // 82
  kNormalize,                        // 83
  kNormalize,                        // 84
  kNormalize,                        // 85
  kNormalize,                        // 86
  kNormalize,                        // 87
  kNormalize,                        // 88
  kNormalize,                        // 89
  kNormalize,                        // 8A
  kNormalize,                        // 8B
  kNormalize,                        // 8C
  kNormalize,                        // 8D
  kNormalize,                        // 8E
  kNormalize,                        // 8F
  kNormalize,                        // 90
  kNormalize,                        // 91
  kNormalize,                        // 92
  kNormalize,                        // 93
  kNormalize,                        // 94
  kNormalize,                        // 95
  kNormalize,                        // 96
  kNormalize,                        // 97
  kNormalize,                        // 98
  kNormalize,                        // 99
  kNormalize,                        // 9A
  kNormalize,                        // 9B
  kNormalize,                        // 9C
  kNormalize,                        // 9D
  kNormalize,                        // 9E
  kNormalize,                        // 9F
  kNormalize,                        // A0
  kNormalize,                        // A1
  kNormalize,                        // A2
  kNormalize,                        // A3
  kNormalize,                        // A4
  kNormalize,                        // A5
  kNormalize,                        // A6
  kNormalize,                        // A7
  kNormalize,                        // A8
  kNormalize,                        // A9
  kNormalize,                        // AA
  kNormalize,                        // AB
  kNormalize,                        // AC
  kNormalize,                        // AD
  kNormalize,                        // AE
  kNormalize,                        // AF
  kNormalize,                        // B0
  kNormalize,                        // B1
  kNormalize,                        // B2
  kNormalize,                        // B3
  kNormalize,                        // B4
  kNormalize,                        // B5
  kNormalize,                        // B6
  kNormalize,                        // B7
  kNormalize,                        // B8
  kNormalize,                        // B9
  kNormalize,                        // BA
  kNormalize,                        // BB
  kNormalize,                        // BC
  kNormalize,                        // BD
  kNormalize,                        // BE
  kNormalize,                        // BF
  kNormalize,                        // C0
  kNormalize,                        // C1
  kNormalize,                        // C2
  kNormalize,                        // C3
  kNormalize,                        // C4
  kNormalize,                        // C5
  kNormalize,                        // C6
  kNormalize,                        // C7
  kNormalize,                        // C8
  kNormalize,                        // C9
  kNormalize,                        // CA
  kNormalize,                        // CB
  kNormalize,                        // CC
  kNormalize,                        // CD
  kNormalize,                        // CE
  kNormalize,                        // CF
  kNormalize,                        // D0
  kNormalize,                        // D1
  kNormalize,                        // D2
  kNormalize,                        // D3
  kNormalize,                        // D4
  kNormalize,                        // D5
  kNormalize,                        // D6
  kNormalize,                        // D7
  kNormalize,                        // D8
  kNormalize,                        // D9
  kNormalize,                        // DA
  kNormalize,                        // DB
  kNormalize,                        // DC
  kNormalize,                        // DD
  kNormalize,                        // DE
  kNormalize,                        // DF
  kNormalize,                        // E0
  kNormalize,                        // E1
  kNormalize,                        // E2
  kNormalize,                        // E3
  kNormalize,                        // E4
  kNormalize,                        // E5
  kNormalize,                        // E6
  kNormalize,                        // E7
  kNormalize,                        // E8
  kNormalize,                        // E9
  kNormalize,                        // EA
  kNormalize,                        // EB
  kNormalize,                        // EC
  kNormalize,                        // ED
  kNormalize,                        // EE
  kNormalize,                        // EF
  kNormalize,                        // F0
  kNormalize,                        // F1
  kNormalize,                        // F2
  kNormalize,                        // F3
  kNormalize,                        // F4
  kNormalize,                        // F5
  kNormalize,                        // F6
  kNormalize,                        // F7
  kNormalize,                        // F8
  kNormalize,                        // F9
  kNormalize,                        // FA
  kNormalize,                        // FB
  kNormalize,                        // FC
  kNormalize,                        // FD
  kNormalize,                        // FE
  kNormalize,                        // FF
};

// Implementation of RegDomain and PublicSuffix.
// func can be webutil_url::GetTopPrivateDomain or webutil_url::GetPublicSuffix.
absl::Status DomainSuffix(
    absl::string_view url,
    absl::FunctionRef<absl::string_view(absl::string_view)> SuffixFunc,
    absl::string_view* out, bool* is_null) {
  absl::string_view host;
  GOOGLESQL_RETURN_IF_ERROR(Host(url, &host, is_null));
  *out = absl::string_view();
  *is_null = true;
  std::string normalized_host_storage;
  absl::string_view normalized_host = host;
  for (const char c : host) {
    if (kHostCharMap[static_cast<uint8_t>(c)] & kNormalize) {
      if (!googlesql::internal::ToASCII(host, &normalized_host_storage)) {
        return absl::OkStatus();
      }
      normalized_host = normalized_host_storage;
      break;
    }
  }
  absl::string_view suffix = SuffixFunc(normalized_host);
  if (!suffix.empty()) {
    auto num_dots = std::count(suffix.begin(), suffix.end(), '.');
    // Remove the prefix before the (num_dots + 1)-th dot from the right.
    const char* host_begin = host.data();
    const char* p = host_begin + host.size();
    while (--p >= host_begin) {
      if (*p == '.' && --num_dots < 0) {
        break;
      }
    }
    ++p;
    *out = absl::string_view(p, host_begin - p + host.size());
    *is_null = out->empty();
  }
  return absl::OkStatus();
}

}  // namespace

absl::Status Host(absl::string_view url, absl::string_view* out,
                  bool* is_null) {
  if (url.empty()) {
    *out = {};
    *is_null = true;
    return absl::OkStatus();
  }
  url = absl::StripAsciiWhitespace(url);
  const char* begin = url.data();
  const char* end = begin + url.size();
  char colon_target = ':';
  if (begin + 1 < end &&
      (kHostCharMap[static_cast<uint8_t>(begin[0])] &
       kHostCharMap[static_cast<uint8_t>(begin[1])] & kSlash)) {
    // url starts with "//".
    begin += 2;
    colon_target = '/';  // Make it impossible to match in the next loop.
  }
  // Extract the substring before "#/?\\" and after the last '@' after the
  // scheme.
  for (const char* p = begin; p < end; ++p) {
    if (kHostCharMap[static_cast<uint8_t>(*p)] & kNetLocEnd) {
      end = p;
      break;
    }
    // If the first colon is followed by "//", then skip up to the "://";
    // otherwise treat the scheme as missing, ignoring schemes without "//"
    // (unsupported), such as "mailto:" and "javascript:".
    //
    // Note that "mailto:foo@bar.com" is an ambiguous case: it could mean a url
    // with username = "mailto", password = "foo", and host = "bar.com", or it
    // could mean one with scheme = "mailto" and recipient = "foo@bar.com".
    // This algorithm does not depend on the list of registered schemes, and
    // thus it takes the first interpretation.
    if (*p == colon_target) {
      if (p + 2 < end && (kHostCharMap[static_cast<uint8_t>(p[1])] &
                          kHostCharMap[static_cast<uint8_t>(p[2])] & kSlash)) {
        p += 2;
        begin = p + 1;
      }
      colon_target = '/';  // Prevent further matching.
    } else if (*p == '@') {
      begin = p + 1;
    }
  }

  // Truncate at the first ':' after the brackets enclosing IPv6.
  // If the first character is '[', then skip up to the second bracket
  // ('[' or ']') or end, whichever comes first.
  const char* p = begin;
  if (p < end && *p == '[') {
    while (++p < end) {
      if (kHostCharMap[static_cast<uint8_t>(*p)] & kBracket) {
        ++p;
        break;
      }
    }
  }
  p = reinterpret_cast<const char*>(memchr(p, ':', end - p));
  if (p != nullptr) {
    end = p;
  }

  // Skip leading dots.
  for (; begin < end && *begin == '.'; ++begin) {
  }
  // Remove trailing dots except the last one.
  if (end > begin) {
    const char* p = end - 1;
    if (*p == '.') {
      // Since leading dots have been skipped and there is still at least one
      // trailing dot, there must be some non-dots, and we don't have to
      // check p >= begin.
      do {
        --p;
      } while (*p == '.');
      end = p + 2;
    }
  }
  *out = absl::string_view(begin, end - begin);
  *is_null = out->empty();
  return absl::OkStatus();
}

absl::Status RegDomain(absl::string_view url, absl::string_view* out,
                       bool* is_null) {
  return DomainSuffix(url, &googlesql::internal::GetTopPrivateDomain, out,
                      is_null);
}

absl::Status PublicSuffix(absl::string_view url, absl::string_view* out,
                          bool* is_null) {
  return DomainSuffix(url, &googlesql::internal::GetPublicSuffix, out, is_null);
}

}  // namespace net
}  // namespace functions
}  // namespace googlesql
