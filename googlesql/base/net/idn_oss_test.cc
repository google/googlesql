//
// Copyright 2020 Google LLC
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

#include "googlesql/base/net/idn_oss.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/string_view.h"

namespace {

static void ToAsciiAndBack(absl::string_view utf8, absl::string_view ascii) {
  std::string host;

  EXPECT_TRUE(googlesql::internal::ToASCII(utf8, &host));
  EXPECT_EQ(ascii, host);
}

TEST(ToASCII, Empty) {
  std::string host;
  EXPECT_FALSE(googlesql::internal::ToASCII("", &host));
}

TEST(ToASCII, Basic) {
  ToAsciiAndBack("\u5341.com", "xn--kkr.com");
}

TEST(ToASCII, AlreadyASCII) {
  // Hostnames that are already ASCII should not be altered.
  ToAsciiAndBack("google.com", "google.com");

  // Punycode will be unchanged by ToASCII, but obviously will be changed by
  // ToUTF8.
  std::string host;
  EXPECT_TRUE(googlesql::internal::ToASCII("xn--nza.com", &host));
  EXPECT_EQ("xn--nza.com", host);
}

TEST(ToASCII, ReservedLDHLabel) {
  // https://tools.ietf.org/html/rfc5890#section-2.3.1 specifies that all
  // labels with hyphens in the 3rd and 4th position are "reserved LDH labels".
  // Unknown, reserved labels should be rejected by IDNA aware conversion
  // functions.
  // Fun fact: Bandaid accidentally used names of this form: b/32469561.
  std::string host;
  EXPECT_FALSE(googlesql::internal::ToASCII("aa--foo.com", &host));
}

TEST(ToASCII, UpperCaseDotCom) {
  std::string host;
  EXPECT_TRUE(googlesql::internal::ToASCII("\u5341.COM", &host));
  EXPECT_EQ("xn--kkr.com", host);
}

TEST(ToASCII, TrailingDot) {
  std::string host;
  EXPECT_TRUE(googlesql::internal::ToASCII("foo.com.", &host));
  EXPECT_EQ("foo.com.", host);
}

TEST(ToASCII, LeadingDot) {
  std::string host;
  EXPECT_FALSE(googlesql::internal::ToASCII(".foo.com", &host));
}

TEST(ToASCII, AdjacentDots) {
  std::string host;
  EXPECT_FALSE(googlesql::internal::ToASCII("foo..com", &host));
}

TEST(ToASCII, EmptyPunycode) {
  std::string host;
  EXPECT_FALSE(googlesql::internal::ToASCII("xn--", &host));
}

TEST(ToASCII, NonUtf8) {
  std::string host;
  EXPECT_FALSE(googlesql::internal::ToASCII("\xA2\xCC.com", &host));
}

TEST(ToASCII, LongUtf8) {
  std::string utf8;

  // This causes U_INDEX_OUTOFBOUNDS_ERROR inside ICU.
  for (int i = 0; i < 300; ++i) {
    utf8.append("\u00E9");
  }

  std::string ascii;
  EXPECT_FALSE(googlesql::internal::ToASCII(utf8, &ascii));
}

TEST(ToASCII, Underscore) {
  // We want underscore to be valid, since the browsers support it.
  ToAsciiAndBack("_\u5341.com", "xn--_-vu8a.com");
}

TEST(ToASCII, Control) {
  // Control character \x02
  ToAsciiAndBack("\u00E9\x02.com", "xn--\x02-9fa.com");
}

TEST(ToASCII, UnassignedInUnicode32) {
  // U+03F7: upper-case letter that was unassigned in Unicode 3.2.
  // Make sure it is supported in the new version of Unicode/IDNA.
  std::string host;
  EXPECT_TRUE(googlesql::internal::ToASCII("\u03F7.com", &host));
  EXPECT_EQ("xn--nza.com", host);
}

TEST(ToASCII, Unassigned) {
  // Unassigned character (U+80000) is invalid.
  std::string host;
  EXPECT_FALSE(googlesql::internal::ToASCII("\U00080000.com", &host));
}

TEST(ToASCII, Eszett) {
  // Eszett (U+00DF) becomes ss.
  std::string host;
  EXPECT_TRUE(googlesql::internal::ToASCII("gro\u00DFe-feier.de", &host));
  EXPECT_EQ("grosse-feier.de", host);
}

TEST(ToASCII, EncodedEszett) {
  // Encoded Eszett is left as is.
  std::string eszett = "xn--groe-feier-73a.de";
  std::string host;
  EXPECT_TRUE(googlesql::internal::ToASCII(eszett, &host));
  EXPECT_EQ(eszett, host);
}

TEST(ToASCII, LongHostName) {
  // 736-byte UTF-8 name <-> 253-byte ASCII name
  std::string u_label;
  for (int i = 0; i < 56; ++i) {
    u_label += "\U00010428";
  }
  std::string utf8_name;
  for (int i = 0; i < 3; ++i) {
    utf8_name += u_label + ".";
  }
  for (int i = 0; i < 61; ++i) {
    utf8_name += "b";
  }
  EXPECT_EQ(736, utf8_name.size());
  std::string a_label =
      "xn--hj8caaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  EXPECT_EQ(63, a_label.size());  // longest possible label
  std::string ascii_name;
  for (int i = 0; i < 3; ++i) {
    ascii_name += a_label + ".";
  }
  for (int i = 0; i < 61; ++i) {
    ascii_name += "b";
  }
  EXPECT_EQ(253, ascii_name.size());  // longest possible name
  ToAsciiAndBack(utf8_name, ascii_name);

  std::string host;

  // 737-byte UTF-8 name -> 254-byte ASCII name -> failure
  utf8_name += "b";
  EXPECT_EQ(737, utf8_name.size());
  EXPECT_FALSE(googlesql::internal::ToASCII(utf8_name, &host));

  // 64-byte label -> failure
  std::string long_label(64, 'a');
  EXPECT_FALSE(googlesql::internal::ToASCII(long_label, &host));
}

}  // anonymous namespace.
