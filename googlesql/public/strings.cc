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

#include "googlesql/public/strings.h"

#include <ctype.h>

#include <iterator>
#include <string>
#include <vector>

#include "googlesql/common/errors.h"
#include "googlesql/common/utf_util.h"
#include "googlesql/parser/keywords.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "absl/algorithm/container.h"
#include "absl/base/no_destructor.h"
#include "absl/base/optimization.h"
#include "absl/container/flat_hash_set.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/case.h"
#include "unicode/utf.h"
#include "unicode/utf8.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

// x must be a valid hex.
static int HexDigitToInt(char x) {
  if (x > '9') {
    x += 9;
  }
  return x & 0xf;
}

// Returns true when following conditions are met:
// - <closing_str> is a suffix of <source>.
// - No other unescaped occurrence of <closing_str> inside <source> (apart from
//   being a suffix).
// Returns false otherwise. If <error> is non-NULL, returns an error message in
// <error>. If <error_offset> is non-NULL, returns the offset in <source> that
// corresponds to the location of the error.
static bool CheckForClosingString(absl::string_view source,
                                  absl::string_view closing_str,
                                  std::string* error, int* error_offset) {
  if (closing_str.empty()) return true;

  const char* p = source.data();
  const char* end = p + source.size();

  bool is_closed = false;
  while (p + closing_str.length() <= end) {
    if (*p != '\\') {
      const auto cur_pos = p - source.data();
      const bool is_closing =
          absl::StartsWith(absl::ClippedSubstr(source, cur_pos), closing_str);
      if (is_closing && p + closing_str.length() < end) {
        if (error) {
          *error =
              absl::StrCat("String cannot contain unescaped ", closing_str);
        }
        if (error_offset) *error_offset = p - source.data();
        return false;
      }
      is_closed = is_closing && (p + closing_str.length() == end);
    } else {
      p++;  // Read past the escaped character.
    }
    p++;
  }

  if (!is_closed) {
    if (error) {
      *error = absl::StrCat("String must end with ", closing_str);
    }
    if (error_offset) *error_offset = source.size();
    return false;
  }

  return true;
}

// Digit conversion.
static char hex_char[] = "0123456789abcdef";

#define IS_OCTAL_DIGIT(c) (((c) >= '0') && ((c) <= '7'))

// Writes a Unicode scalar value (a non-surrogate code point)
// to s (which must have enough capacity),
// and returns the number of bytes written.
static int AppendCodePoint(char* s, UChar32 cp) {
  int cp_len = 0;
  U8_APPEND_UNSAFE(s, cp_len, cp);
  return cp_len;
}

// ----------------------------------------------------------------------
// CUnescapeInternal()
//    Unescapes C escape sequences and is the reverse of CEscape().
//
//    If 'source' is valid, stores the unescaped string and its size in
//    'dest' and 'dest_len' respectively, and returns true. Otherwise
//    returns false and optionally stores the error description in
//    'error' and the error offset in 'error_offset'. If 'error' is
//    nonempty on return, 'error_offset' is in range [0, str.size()].
//    Set 'error' and 'error_offset' to NULL to disable error reporting.
//
//    'dest' must point to a buffer that is at least as big as 'source'.  The
//    unescaped string cannot grow bigger than the source string since no
//    unescaped sequence is longer than the corresponding escape sequence.
//    'source' and 'dest' must not be the same.
//
// Originally COPIED FROM strings/escaping.cc, with the following modifications:
// - Removed some code for unnecessary modes.
// - The string is required to be well-formed UTF-8.
// - Unicode surrogate code points are not allowed for
//   \u and \U escape sequences.
// - Extended the logic to support bytes literals, which have slightly different
//   escape support as compared to strings (\u is unsupported, and hex/octal
//   escapes are treated as a single byte rather than as a Unicode code point).
//
// If <closing_str> is non-empty, for <source> to be valid:
// - It must end with <closing_str>.
// - Should not contain any other unescaped occurrence of <closing_str>.
// ----------------------------------------------------------------------
static bool CUnescapeInternal(absl::string_view source,
                              absl::string_view closing_str,
                              bool is_raw_literal, bool is_bytes_literal,
                              char* dest, int* dest_len, std::string* error,
                              int* error_offset) {
  if (error_offset) *error_offset = 0;

  if (!CheckForClosingString(source, closing_str, error, error_offset)) {
    return false;
  }

  if (ABSL_PREDICT_FALSE(source.empty())) {
    *dest_len = 0;
    return true;
  }
  ABSL_CHECK(source.data() != dest) << "Source and destination cannot be the same";

  // Strip off the closing_str from the end before unescaping.
  source = source.substr(0, source.size() - closing_str.size());
  if (!is_bytes_literal) {
    auto span_length = SpanWellFormedUTF8(source);
    if (span_length < source.length()) {
      if (error) {
        *error = absl::StrCat(
            "Structurally invalid UTF8"  //
            " string: ",
            EscapeBytes(source));        //
      }
      if (error_offset) {
        *error_offset = span_length;
      }
      return false;
    }
  }

  char* d = dest;
  const char* p = source.data();
  const char* end = p + source.size();
  const char* last_byte = end - 1;

  while (p < end) {
    if (*p != '\\') {
      if (*p != '\r') {
        *d++ = *p++;
      } else {
        // All types of newlines in different platforms i.e. '\r', '\n', '\r\n'
        // are replaced with '\n'.
        *d++ = '\n';
        p++;
        if (p < end && *p == '\n') {
          p++;
        }
      }
    } else {
      if ((p + 1) > last_byte) {
        if (error) {
          *error = is_raw_literal
                       ? "Raw literals cannot end with odd number of \\"
                       : is_bytes_literal ? "Bytes literal cannot end with \\"
                                          : "String literal cannot end with \\";
        }
        if (error_offset) *error_offset = source.size();
        return false;
      } else {
        if (is_raw_literal) {
          // For raw literals, all escapes are valid and those characters ('\\'
          // and the escaped character) come through literally in the string.
          *d++ = *p++;
          *d++ = *p++;
          continue;
        } else {
          // Any error that occurs in the escape is accounted to the start of
          // the escape.
          if (error_offset) *error_offset = p - source.data();
          p++;  // Read past the escape character.
        }
      }

      switch (*p) {
        case 'a':  *d++ = '\a';  break;
        case 'b':  *d++ = '\b';  break;
        case 'f':  *d++ = '\f';  break;
        case 'n':  *d++ = '\n';  break;
        case 'r':  *d++ = '\r';  break;
        case 't':  *d++ = '\t';  break;
        case 'v':  *d++ = '\v';  break;
        case '\\': *d++ = '\\';  break;
        case '?':  *d++ = '\?';  break;  // \?  Who knew?
        case '\'': *d++ = '\'';  break;
        case '"':  *d++ = '\"';  break;
        case '`':  *d++ = '`';  break;

        case '0': case '1': case '2': case '3': {
          // Octal escape '\ddd': requires exactly 3 octal digits.  Note that
          // the highest valid escape sequence is '\377'.
          // For string literals, octal and hex escape sequences are interpreted
          // as unicode code points, and the related UTF8-encoded character is
          // added to the destination.  For bytes literals, octal and hex
          // escape sequences are interpreted as a single byte value.
          const char* octal_start = p;
          if (p + 2 >= end) {
            if (error) {
              *error =
                  "Illegal escape sequence: Octal escape must be followed by 3 "
                  "octal digits but saw: \\" +
                  std::string(octal_start, end - p);
            }
            // Error offset was set to the start of the escape above the switch.
            return false;
          }
          const char* octal_end = p + 2;
          UChar32 ch = 0;
          for (; p <= octal_end; ++p) {
            if (IS_OCTAL_DIGIT(*p)) {
              ch = ch * 8 + *p - '0';
            } else {
              if (error) {
                *error =
                    "Illegal escape sequence: Octal escape must be followed by "
                    "3 octal digits but saw: \\" +
                    std::string(octal_start, 3);
              }
              // Error offset was set to the start of the escape above the
              // switch.
              return false;
            }
          }
          p = octal_end;  // p points at last digit.
          if (is_bytes_literal) {
            *d = ch;
            d++;
          } else {
            d += AppendCodePoint(d, ch);
          }
          break;
        }
        case 'x': case 'X': {
          // Hex escape '\xhh': requires exactly 2 hex digits.
          // For string literals, octal and hex escape sequences are
          // interpreted as unicode code points, and the related UTF8-encoded
          // character is added to the destination.  For bytes literals, octal
          // and hex escape sequences are interpreted as a single byte value.
          const char* hex_start = p;
          if (p + 2 >= end) {
            if (error) {
              *error =
                  "Illegal escape sequence: Hex escape must be followed by 2 "
                  "hex digits but saw: \\" +
                  std::string(hex_start, end - p);
            }
            // Error offset was set to the start of the escape above the switch.
            return false;
          }
          UChar32 ch = 0;
          const char* hex_end = p + 2;
          for (++p; p <= hex_end; ++p) {
            if (absl::ascii_isxdigit(*p)) {
              ch = (ch << 4) + HexDigitToInt(*p);
            } else {
              if (error) {
                *error =
                    "Illegal escape sequence: Hex escape must be followed by 2 "
                    "hex digits but saw: \\" +
                    std::string(hex_start, 3);
              }
              // Error offset was set to the start of the escape above the
              // switch.
              return false;
            }
          }
          p = hex_end;  // p points at last digit.
          if (is_bytes_literal) {
            *d = ch;
            d++;
          } else {
            d += AppendCodePoint(d, ch);
          }
          break;
        }
        case 'u': {
          if (is_bytes_literal) {
            if (error) {
              *error =
                  std::string(
                      "Illegal escape sequence: Unicode escape sequence \\") +
                  *p + " cannot be used in bytes literals";
            }
            // Error offset was set to the start of the escape above the switch.
            return false;
          }
          // \uhhhh => Read 4 hex digits as a code point,
          //           then write it as UTF-8 bytes.
          UChar32 cp = 0;
          const char* hex_start = p;
          if (p + 4 >= end) {
            if (error) {
              *error =
                  "Illegal escape sequence: \\u must be followed by 4 hex "
                  "digits but saw: \\" +
                  std::string(hex_start, end - p);
            }
            // Error offset was set to the start of the escape above the switch.
            return false;
          }
          for (int i = 0; i < 4; ++i) {
            // Look one char ahead.
            if (absl::ascii_isxdigit(p[1])) {
              cp = (cp << 4) + HexDigitToInt(*++p);  // Advance p.
            } else {
              if (error) {
                *error =
                    "Illegal escape sequence: \\u must be followed by 4 "
                    "hex digits but saw: \\" +
                    std::string(hex_start, 5);
              }
              // Error offset was set to the start of the escape above the
              // switch.
              return false;
            }
          }
          if (U_IS_SURROGATE(cp)) {
            if (error) {
              *error = "Illegal escape sequence: Unicode value \\" +
                       std::string(hex_start, 5) + " is invalid";
            }
            // Error offset was set to the start of the escape above the switch.
            return false;
          }
          d += AppendCodePoint(d, cp);
          break;
        }
        case 'U': {
          if (is_bytes_literal) {
            if (error) {
              *error =
                  std::string(
                      "Illegal escape sequence: Unicode escape sequence \\") +
                  *p + " cannot be used in bytes literals";
            }
            return false;
          }
          // \Uhhhhhhhh => convert 8 hex digits to UTF-8.  Note that the
          // first two digits must be 00: The valid range is
          // '\U00000000' to '\U0010FFFF' (excluding surrogates).
          UChar32 cp = 0;
          const char* hex_start = p;
          if (p + 8 >= end) {
            if (error) {
              *error =
                  "Illegal escape sequence: \\U must be followed by 8 hex "
                  "digits but saw: \\" +
                  std::string(hex_start, end - p);
            }
            // Error offset was set to the start of the escape above the switch.
            return false;
          }
          for (int i = 0; i < 8; ++i) {
            // Look one char ahead.
            if (absl::ascii_isxdigit(p[1])) {
              cp = (cp << 4) + HexDigitToInt(*++p);
              if (cp > 0x10FFFF) {
                if (error) {
                  *error = "Illegal escape sequence: Value of \\" +
                           std::string(hex_start, 9) +
                           " exceeds Unicode limit (0x0010FFFF)";
                }
                // Error offset was set to the start of the escape above the
                // switch.
                return false;
              }
            } else {
              if (error) {
                *error =
                    "Illegal escape sequence: \\U must be followed by 8 "
                    "hex digits but saw: \\" +
                    std::string(hex_start, 9);
              }
              // Error offset was set to the start of the escape above the
              // switch.
              return false;
            }
          }
          if (U_IS_SURROGATE(cp)) {
            if (error) {
              *error = "Illegal escape sequence: Unicode value \\" +
                       std::string(hex_start, 9) + " is invalid";
            }
            // Error offset was set to the start of the escape above the switch.
            return false;
          }
          d += AppendCodePoint(d, cp);
          break;
        }

        case '\r':
        case '\n': {
          if (error) {
            *error = "Illegal escaped newline";
          }
          // Error offset was set to the start of the escape above the switch.
          return false;
        }

        default: {
          if (error) {
            *error = std::string("Illegal escape sequence: \\") + *p;
          }
          // Error offset was set to the start of the escape above the switch.
          return false;
        }
      }
      p++;                                 // read past letter we escaped
    }
  }
  *dest_len = d - dest;
  // Note - we are intentionally not checking for structurally valid UTF8 after
  // unescaping because given a structurally valid input, we will not generate
  // structurally invalid output.
  return true;
}

// Same as CUnescapeInternal above, but outputs into a string.
static bool CUnescapeInternal(absl::string_view source,
                              absl::string_view closing_str,
                              bool is_raw_literal, bool is_bytes_literal,
                              std::string* dest, std::string* error,
                              int* error_offset) {
  dest->resize(source.size());
  int dest_size;
  if (!CUnescapeInternal(source, closing_str, is_raw_literal, is_bytes_literal,
                         const_cast<char*>(dest->data()), &dest_size, error,
                         error_offset)) {
    return false;
  }
  dest->resize(dest_size);
  return true;
}

// ----------------------------------------------------------------------
// CEscape()
// CHexEscape()
// Utf8SafeCEscape()
// Utf8SafeCHexEscape()
//    Escapes 'src' using C-style escape sequences.  This is useful for
//    preparing query flags.  The 'Hex' version uses hexadecimal rather than
//    octal sequences.  The 'Utf8Safe' version does not touch UTF-8 bytes.
//
//    Escaped chars: \n, \r, \t, ", ', \, and !ascii_isprint().
//
// COPIED FROM strings/escaping.cc, with unnecessary modes removed, and with
// the escape_quote_char feature added.
//
// If escape_quote_char is non-zero, only escape the quote character
// (from '"`) that matches escape_quote_char.
// This allows writing "ab'cd" or 'ab"cd' or `ab"cd` without extra escaping.
// ----------------------------------------------------------------------
static std::string CEscapeInternal(absl::string_view src, bool utf8_safe,
                                   char escape_quote_char) {
  std::string dest;
  bool last_hex_escape = false;  // true if last output char was \xNN.

  for (char sc : src) {
    unsigned char c = static_cast<unsigned char>(sc);
    bool is_hex_escape = false;
    switch (c) {
      case '\n': dest.append("\\" "n"); break;
      case '\r': dest.append("\\" "r"); break;
      case '\t': dest.append("\\" "t"); break;
      case '\\': dest.append("\\" "\\"); break;

      case '\'':
      case '\"':
      case '`':
        // Escape only quote chars that match escape_quote_char.
        if (escape_quote_char == 0 || c == escape_quote_char) {
          dest.push_back('\\');
        }
        dest.push_back(c);
        break;

      default:
        // Note that if we emit \xNN and the src character after that is a hex
        // digit then that digit must be escaped too to prevent it being
        // interpreted as part of the character code by C.
        if ((!utf8_safe || c < 0x80) &&
            (!absl::ascii_isprint(c) ||
             (last_hex_escape && absl::ascii_isxdigit(c)))) {
          dest.append("\\" "x");
          dest.push_back(hex_char[c / 16]);
          dest.push_back(hex_char[c % 16]);
          is_hex_escape = true;
        } else {
          dest.push_back(c);
          break;
        }
    }
    last_hex_escape = is_hex_escape;
  }

  return dest;
}

absl::Status UnescapeString(absl::string_view str, std::string* out,
                            std::string* error_string, int* error_offset) {
  if (!CUnescapeInternal(str, "" /* closing_str */, false /* is_raw_literal */,
                         false /* is_bytes_literal */, out, error_string,
                         error_offset)) {
    return MakeSqlError()
           << "Invalid escaped string: '"
           << str << "'"
           << (error_string == nullptr ? ""
                                       : absl::StrCat(", ", *error_string));
  }
  return absl::OkStatus();
}

absl::Status UnescapeBytes(absl::string_view str, std::string* out,
                           std::string* error_string, int* error_offset) {
  if (!CUnescapeInternal(str, "" /* closing_str */, false /* is_raw_literal */,
                         true /* is_bytes_literal */, out, error_string,
                         error_offset)) {
    return MakeSqlError() << "Invalid escaped bytes: '" << EscapeBytes(str)
                          << "'"
                          << (error_string == nullptr
                                  ? ""
                                  : absl::StrCat(", ", *error_string));
  }
  return absl::OkStatus();
}

std::string EscapeString(absl::string_view str) {
  return CEscapeInternal(str, true /* utf8_safe */, 0 /* escape_quote_char */);
}

std::string EscapeBytes(absl::string_view str, bool escape_all_bytes,
                        char escape_quote_char) {
  std::string escaped_bytes;
  for (int i = 0; i < str.size(); ++i) {
    unsigned char c = static_cast<unsigned char>(str[i]);
    if (escape_all_bytes || !absl::ascii_isprint(c)) {
      escaped_bytes += "\\x";
      escaped_bytes += absl::BytesToHexString(str.substr(i, 1));
    } else {
      switch (c) {
        // Note that we only handle printable escape characters here.  All
        // unprintable (\n, \r, \t, etc.) are hex escaped above.
        case '\\':
          escaped_bytes += "\\\\";
          break;
        case '\'':
        case '"':
        case '`':
          // Escape only quote chars that match escape_quote_char.
          if (escape_quote_char == 0 || c == escape_quote_char) {
            escaped_bytes += '\\';
          }
          escaped_bytes += c;
          break;
        default:
          escaped_bytes += c;
          break;
      }
    }
  }
  return escaped_bytes;
}

static bool MayBeTripleQuotedString(const absl::string_view str) {
  return (str.size() >= 6 &&
          ((absl::StartsWith(str, "\"\"\"") && absl::EndsWith(str, "\"\"\"")) ||
           (absl::StartsWith(str, "'''") && absl::EndsWith(str, "'''"))));
}

static bool MayBeStringLiteral(const absl::string_view str) {
  return (str.size() >= 2 && str[0] == str[str.size() - 1] &&
          (str[0] == '\'' || str[0] == '"'));
}

static bool MayBeBytesLiteral(const absl::string_view str) {
  return (str.size() >= 3 && (str[0] == 'b' || str[0] == 'B') &&
          str[1] == str[str.size() - 1] && (str[1] == '\'' || str[1] == '"'));
}

static bool MayBeRawStringLiteral(const absl::string_view str) {
  return (str.size() >= 3 && (str[0] == 'r' || str[0] == 'R') &&
          str[1] == str[str.size() - 1] && (str[1] == '\'' || str[1] == '"'));
}

static bool MayBeRawBytesLiteral(const absl::string_view str) {
  return (str.size() >= 4 &&
          (strncasecmp(str.data(), "rb", sizeof("rb") - 1) == 0 ||
           strncasecmp(str.data(), "br", sizeof("br") - 1) == 0) &&
          (str[2] == str[str.size() - 1]) && (str[2] == '\'' || str[2] == '"'));
}

absl::Status ParseStringLiteral(absl::string_view str, std::string* out,
                                std::string* error_string, int* error_offset) {
  if (error_offset) *error_offset = 0;
  ABSL_CHECK_NE(str.data(), out->data())
      << "Source and destination cannot be the same";

  const bool is_string_literal = MayBeStringLiteral(str);
  const bool is_raw_string_literal = MayBeRawStringLiteral(str);
  if (!is_string_literal && !is_raw_string_literal) {
    const std::string error =      //
        "Invalid string literal";
    if (error_string) *error_string = error;
    return MakeSqlError() << error;
  }

  absl::string_view copy_str = str;
  if (is_raw_string_literal) {
    // Strip off the prefix 'r' from the raw string content before parsing.
    copy_str = absl::ClippedSubstr(copy_str, 1);
  }

  const bool is_triple_quoted = MayBeTripleQuotedString(copy_str);
  // Starts after the opening quotes {""", '''} or {", '}.
  const int quotes_length = is_triple_quoted ? 3 : 1;
  const absl::string_view quotes = copy_str.substr(0, quotes_length);
  copy_str = absl::ClippedSubstr(copy_str, quotes_length);
  std::string local_error_string;
  if (!CUnescapeInternal(copy_str,
                         quotes /* closing_str */, is_raw_string_literal,
                         false /* is_bytes_literal */, out, &local_error_string,
                         error_offset)) {
    // Correct the error offset for what we stripped off from the start.
    if (error_offset) *error_offset += copy_str.data() - str.data();
    if (error_string) *error_string = local_error_string;
    return MakeSqlError()                 //
           << "Invalid string literal: "
           << local_error_string;         //
  }
  return absl::OkStatus();
}

absl::Status ParseBytesLiteral(absl::string_view str, std::string* out,
                               std::string* error_string, int* error_offset) {
  if (error_offset) *error_offset = 0;
  ABSL_CHECK_NE(str.data(), out->data())
      << "Source and destination cannot be the same";

  const bool is_bytes_literal = MayBeBytesLiteral(str);
  const bool is_raw_bytes_literal = MayBeRawBytesLiteral(str);
  if (!is_bytes_literal && !is_raw_bytes_literal) {
    const std::string error = "Invalid bytes literal";
    if (error_string) *error_string = error;
    return MakeSqlError() << error;
  }

  absl::string_view copy_str = str;
  if (is_raw_bytes_literal) {
    // Strip off the prefix {"rb", "br"} from the raw bytes content before
    copy_str = absl::ClippedSubstr(copy_str, 2);
  } else {
    ABSL_DCHECK(is_bytes_literal);
    // Strip off the prefix 'b' from the bytes content before parsing.
    copy_str = absl::ClippedSubstr(copy_str, 1);
  }

  const bool is_triple_quoted = MayBeTripleQuotedString(copy_str);
  // Starts after the opening quotes {""", '''} or {", '}.
  const int quotes_length = is_triple_quoted ? 3 : 1;
  const absl::string_view quotes = copy_str.substr(0, quotes_length);
  // Includes the closing quotes.
  copy_str = absl::ClippedSubstr(copy_str, quotes_length);
  std::string local_error_string;
  if (!CUnescapeInternal(copy_str,
                         quotes /* closing_str */, is_raw_bytes_literal,
                         true /* is_bytes_literal */, out, &local_error_string,
                         error_offset)) {
    // Correct the error offset for what we stripped off from the start.
    if (error_offset) *error_offset += copy_str.data() - str.data();
    if (error_string) *error_string = local_error_string;
    return MakeSqlError() << "Invalid bytes literal: " << local_error_string;
  }
  return absl::OkStatus();
}

std::string ToStringLiteral(absl::string_view str) {
  absl::string_view quote =
      (str.find('"') != str.npos && str.find('\'') == str.npos) ? "'" : "\"";
  return absl::StrCat(
      quote, CEscapeInternal(str, true /* utf8_safe */, quote[0]), quote);
}

std::string ToSingleQuotedStringLiteral(absl::string_view str) {
  return absl::StrCat("'", CEscapeInternal(str, true /* utf8_safe */, '\''),
                      "'");
}

std::string ToDoubleQuotedStringLiteral(absl::string_view str) {
  return absl::StrCat("\"", CEscapeInternal(str, true /* utf8_safe */, '"'),
                      "\"");
}

std::string ToBytesLiteral(absl::string_view str) {
  absl::string_view quote =
      (str.find('"') != str.npos && str.find('\'') == str.npos) ? "'" : "\"";
  return absl::StrCat("b", quote,
                      EscapeBytes(str, false /* escape_all_bytes */, quote[0]),
                      quote);
}

std::string ToSingleQuotedBytesLiteral(absl::string_view str) {
  return absl::StrCat(
      "b'", EscapeBytes(str, false /* escape_all_bytes */, '\''), "'");
}

std::string ToDoubleQuotedBytesLiteral(absl::string_view str) {
  return absl::StrCat(
      "b\"", EscapeBytes(str, false /* escape_all_bytes */, '"'), "\"");
}

// Return true if <str> is guaranteed to be a valid identifier without quoting.
// If <allow_reserved_keywords> is false, returns false if <str> is a reserved
// or conditionally reserved keyword.
bool IsValidUnquotedIdentifier(absl::string_view str,
                               const LanguageOptions& language_options,
                               bool allow_reserved_keywords) {
  if (str.empty()) return false;
  if (!isalpha(str[0]) && str[0] != '_') return false;
  for (char c : str) {
    if (!isalnum(c) && c != '_') {
      return false;
    }
  }
  // Reserved keywords cannot be used as identifiers without quoting.
  return allow_reserved_keywords || !language_options.IsReservedKeyword(str);
}

static absl::Status ParseIdentifierImpl(absl::string_view str,
                                        const LanguageOptions& language_options,
                                        std::string* out,
                                        std::string* error_string,
                                        int* error_offset,
                                        bool allow_reserved_keywords) {
  if (error_offset) *error_offset = 0;
  // The closing quote is validated by CUnescapeInternal() below.
  const bool is_quoted = !str.empty() && str[0] == '`';

  if (is_quoted) {
    const int quotes_length = 1;  // Starts after the opening quote '`'.
    const absl::string_view quotes = str.substr(0, quotes_length);
    // Includes the closing quotes.
    absl::string_view copy_str = absl::ClippedSubstr(str, quotes_length);
    std::string local_error_string;
    if (!CUnescapeInternal(copy_str, quotes /* closing_str */,
                           false /* is_raw_literal */,
                           false /* is_bytes_literal */, out,
                           &local_error_string, error_offset)) {
      // Correct the error offset for what we stripped off from the start.
      if (error_offset) *error_offset += copy_str.data() - str.data();
      if (error_string) *error_string = local_error_string;
      return MakeSqlError() << "Invalid identifier: " << local_error_string;
    }
    if (str.size() == 2) {
      // Empty identifier. CUnescapeInternal() verified that it contained a
      // closing quote.
      const std::string error = "Invalid empty identifier";
      if (error_string) *error_string = error;
      return MakeSqlError() << error;
    }
  } else {
    if (!IsValidUnquotedIdentifier(str, language_options,
                                   allow_reserved_keywords)) {
      const std::string error = "Invalid identifier";
      if (error_string) *error_string = error;
      return MakeSqlError() << error;
    }
    out->assign(str.data(), str.size());
  }

  return absl::OkStatus();
}

absl::Status ParseIdentifier(absl::string_view str,
                             const LanguageOptions& language_options,
                             std::string* out, std::string* error_string,
                             int* error_offset) {
  return ParseIdentifierImpl(str, language_options, out, error_string,
                             error_offset, false /* allow_reserved_keywords */);
}

absl::Status ParseGeneralizedIdentifier(absl::string_view str, std::string* out,
                                        std::string* error_string,
                                        int* error_offset) {
  // LanguageOptions do not matter since reserved keywords are allowed
  LanguageOptions language_options;

  return ParseIdentifierImpl(str, language_options, out, error_string,
                             error_offset, true /* allow_reserved_keywords */);
}

std::string ToAlwaysQuotedIdentifierLiteral(absl::string_view str) {
  return absl::StrCat("`", CEscapeInternal(str, true /* utf8_safe */, '`'),
                      "`");
}

std::string ToIdentifierLiteral(absl::string_view str,
                                bool quote_reserved_keywords) {
  static const absl::NoDestructor<const LanguageOptions> kLanguageOptions([] {
    LanguageOptions language_options;
    language_options.EnableAllReservableKeywords();
    return language_options;
  }());

  return IsValidUnquotedIdentifier(str, *kLanguageOptions,
                                   !quote_reserved_keywords) &&
                 !parser::NonReservedIdentifierMustBeBackquoted(str)
             ? std::string(str)
             : ToAlwaysQuotedIdentifierLiteral(str);
}

std::string ToIdentifierLiteral(IdString str, bool quote_reserved_keywords) {
  return ToIdentifierLiteral(str.ToStringView(), quote_reserved_keywords);
}

std::string IdentifierPathToString(absl::Span<const std::string> path,
                                   bool quote_reserved_keywords) {
  std::string result;
  for (const std::string& identifier : path) {
    if (result.empty()) {
      result += ToIdentifierLiteral(identifier,
                                    /*quote_reserved_keywords=*/true);
    } else {
      result += "." + ToIdentifierLiteral(identifier, quote_reserved_keywords);
    }
  }
  return result;
}

std::string IdentifierPathToString(absl::Span<const IdString> path,
                                   bool quote_reserved_keywords) {
  std::string result;
  for (const IdString identifier : path) {
    if (result.empty()) {
      result += ToIdentifierLiteral(identifier,
                                    /*quote_reserved_keywords=*/true);
    } else {
      result += "." + ToIdentifierLiteral(identifier, quote_reserved_keywords);
    }
  }
  return result;
}

// Helper method for advancing a char pointer to the next unescaped backquote.
// The pointer will point to the backquote if one is found; if no backquote is
// found, this will return false and leave `p` unmodified. Both `p` and `end`
// are expected to be pointers to the same underlying buffer, with `p` preceding
// `end`.
static bool AdvanceToNextBackquote(const char* end, const char** pos) {
  const char* p = *pos;
  while (p < end && *p != '`') {
    // Skip escaped backquotes.
    const char* next_byte = p + 1;
    if (*p == '\\' && next_byte < end &&
        // Ensure escaped backslashes are also skipped.
        (*next_byte == '`' || *next_byte == '\\')) {
      ++p;
    }
    // Advance to the next character.
    ++p;
  }
  // If the pointer is at the end, or if the provided pointer was already beyond
  // `end`, no backquote was found.
  if (p >= end) {
    return false;
  }
  *pos = p;
  return true;
}

// Attempt to parse the provided segment as an identifier and add to the output
// if valid. If the identifier is invalid, an error will be returned.  The
// parsing also handles unquoted identifiers that are supported by the lexer's
// DOT_IDENTIFIER mode. This means path components comprised of numbers/reserved
// keywords are allowed without backquoting as long as they are not the first
// component (e.g. "abc.123" and "abc.select" are both valid, "123.abc" and
// "select.abc" are not).
//
// Note: If `segment` is not backquoted and is not the first element of `out`,
// it will be escaped/wrapped in backquotes before parsing.
static absl::Status AddIdentifierPathSegment(
    absl::string_view segment, const LanguageOptions& language_options,
    std::vector<std::string>* out) {
  GOOGLESQL_RET_CHECK(!segment.empty());
  std::string out_string;
  if (segment[0] == '`') {
    GOOGLESQL_RETURN_IF_ERROR(ParseIdentifier(segment, language_options, &out_string));
  } else {
    // DOT_IDENTIFIER mode only applies to path components after the first. If
    // the input vector is empty, use ParseIdentifier to catch cases where the
    // first path value is "123", "select", etc.
    if (out->empty() && segment[0] != '/') {
      GOOGLESQL_RETURN_IF_ERROR(ParseIdentifier(segment, language_options, &out_string));
    } else {
      // Unescaped path components beginning with digits/containing reserved
      // keywords or slash paths are valid but need to be quoted to ensure they
      // are correctly parsed.
      const std::string quoted_segment = absl::StrCat(
          "`", CEscapeInternal(segment, true /* utf8_safe */, '`'), "`");
      GOOGLESQL_RETURN_IF_ERROR(
          ParseIdentifier(quoted_segment, language_options, &out_string));
    }
  }
  out->push_back(out_string);
  return absl::OkStatus();
}

static absl::StatusOr<bool> IsSlashPath(
    absl::string_view str, const LanguageOptions& language_options) {
  if (str.empty() || str[0] != '/') return false;
  if (!language_options.LanguageFeatureEnabled(FEATURE_ALLOW_SLASH_PATHS)) {
    return MakeSqlError() << "Path starts with an invalid character '/'";
  }
  return true;
}

static bool IsSlashPathSpecialCharacter(char character) {
  return character == '/' || character == '-' || character == ':';
}

absl::Status ParseIdentifierPath(absl::string_view str,
                                 const LanguageOptions& language_options,
                                 std::vector<std::string>* out) {
  if (str.empty()) return MakeSqlError() << "Path strings cannot be empty";

  // Fail fast if the path starts or ends with '.'.
  if (str[0] == '.') {
    return MakeSqlError() << "Path strings cannot begin with `.`";
  }
  if (str[str.size() - 1] == '.') {
    return MakeSqlError() << "Path strings cannot end with `.`";
  }

  const char* p = str.data();
  const char* segment_start = p;
  const char* const end = str.data() + str.size();
  std::vector<std::string> temp_out;
  // If the paths starts with '/' and FEATURE_ALLOW_SLASH_PATHS is
  // enabled, then the first segment of the path can contain slash, dash, and
  // colon.
  GOOGLESQL_ASSIGN_OR_RETURN(bool allow_slash_path_segment,
                   IsSlashPath(str, language_options));
  // A path cannot end in '/'. '-', or ':'.
  if (allow_slash_path_segment &&
      IsSlashPathSpecialCharacter(str[str.size() - 1])) {
    return MakeSqlError() << "Path string cannot end with `"
                          << str[str.size() - 1] << "`";
  }
  while (p < end) {
    // Check for consecutive '.'s (e.g. 'table..name').
    if (*p == '.') {
      return MakeSqlError()     //
             << "Path string "
             << "contains an empty path component";  //
    }

    // Find the next '.'. The main logic applied here is to skip dots within
    // backquoted sections - validation is handled when parsing the value.
    const char* previous = end;
    while (p < end && *p != '.') {
      if (allow_slash_path_segment && IsSlashPathSpecialCharacter(*p)) {
        // Do not allow these characters to appear next to each other.
        if (previous != end && IsSlashPathSpecialCharacter(*previous)) {
          return MakeSqlError() << "Path contains an invalid character '" << *p
                                << " after character '" << *previous << "'";
        }
      } else {
        // Path identifiers can only be alphanumeric or '_'. Backquotes indicate
        // the beginning of an escape sequence and are therefore allowed.
        if (!isalnum(*p) && *p != '_' && *p != '`') {
          return MakeSqlError()
                 << "Path contains an invalid character '" << *p << "'";
        }
        // Skip over dots within backquoted sections (e.g. 'table.`name.dot`).
        if (*p == '`') {
          // Fail if the unescaped backquote is not the first piece of the
          // component (e.g. abc.`def` is allowed,  abc.d`ef` is not).
          if (p != segment_start) {
            return MakeSqlError()
                   << "Path contains an invalid character '`' "
                   << "not at the start of a dotted path segment";
          }
          // Skip the opening backquote.
          ++p;
          // Note that this will end on the closing backquote.
          if (!AdvanceToNextBackquote(end, &p)) {
            return MakeSqlError() << "Path contains an unmatched `";
          }
        }
      }
      // Advance to the next character.
      previous = p;
      ++p;
    }

    // Make sure that '/', '-', ':' do not precede the dot e.g.
    // '/span/global/.Table' is invalid.
    if (p != end && *p == '.' && previous != end &&
        IsSlashPathSpecialCharacter(*previous)) {
      return MakeSqlError()
             << "Path contains an invalid character '" << *previous << "'";
    }

    // Extract the segment and attempt to parse as an identifier.
    absl::string_view segment(segment_start, std::distance(segment_start, p));
    GOOGLESQL_RETURN_IF_ERROR(
        AddIdentifierPathSegment(segment, language_options, &temp_out));

    // Advance past the dot and reset the start position.
    ++p;
    segment_start = p;
    allow_slash_path_segment = false;
  }

  // The slashed path should return error if a segment has a reserved keyword
  // for example /CREATE/abc.
  const bool is_quoted = !str.empty() && str[0] == '`';
  if (language_options.LanguageFeatureEnabled(FEATURE_ALLOW_SLASH_PATHS) &&
      !temp_out.empty() && !is_quoted) {
    absl::string_view first_seg = temp_out[0];
    std::vector<std::string> slash_path = absl::StrSplit(first_seg, '/');
    if (slash_path.size() > 1 &&
        absl::c_any_of(slash_path, &IsReservedKeyword)) {
      return MakeSqlError() << "Slashed path contains a reserved keyword'";
    }
  }

  *out = temp_out;
  return absl::OkStatus();
}

bool IsInternalAlias(absl::string_view alias) {
  return !alias.empty() && alias[0] == '$';
}

bool IsInternalAlias(IdString alias) {
  return !alias.empty() && alias[0] == '$';
}

bool IsKeyword(absl::string_view str) {
  return parser::GetKeywordInfo(str) != nullptr;
}

bool IsReservedKeyword(absl::string_view str) {
  // For backward compatibility reasons, this must return false for
  // conditionally reserved keywords.
  const parser::KeywordInfo* keyword_info = parser::GetKeywordInfo(str);
  return keyword_info != nullptr && keyword_info->IsAlwaysReserved();
}

// Make a hash_set of all upper-cased reserved keywords.
// For backward compatibility reasons, this set includes unconditionally
// reserved keywords only.
static absl::flat_hash_set<std::string> MakeReservedKeywordsUpperSet(
    bool include_conditionally_reserved_keywords) {
  absl::flat_hash_set<std::string> result;
  for (const parser::KeywordInfo& keyword_info : parser::GetAllKeywords()) {
    if (include_conditionally_reserved_keywords
            ? keyword_info.CanBeReserved()
            : keyword_info.IsAlwaysReserved()) {
      googlesql_base::InsertOrDie(&result, absl::AsciiStrToUpper(keyword_info.keyword()));
    }
  }
  return result;
}

const absl::flat_hash_set<std::string>& GetReservedKeywords() {
  static const absl::flat_hash_set<std::string>& keywords =
      *new auto(MakeReservedKeywordsUpperSet(
          /*include_conditionally_reserved_keywords=*/false));
  return keywords;
}

}  // namespace googlesql
