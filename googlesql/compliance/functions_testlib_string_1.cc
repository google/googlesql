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

#include <string>
#include <utility>
#include <vector>

#include "googlesql/compliance/functions_testlib_common.h"
#include "googlesql/public/functions/normalize_mode.pb.h"
#include "googlesql/public/type.h"
#include "googlesql/public/value.h"
#include "googlesql/testing/test_function.h"
#include "googlesql/testing/test_value.h"
#include "googlesql/testing/using_test_value.cc"  // NOLINT
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/status.h"

namespace googlesql {
namespace {
constexpr absl::StatusCode OUT_OF_RANGE = absl::StatusCode::kOutOfRange;
}  // namespace

std::vector<FunctionTestCall> GetFunctionTestsOctetLength() {
  std::vector<FunctionTestCall> results = {
    // OCTET_LENGTH(string) -> int64
    {"octet_length", {NullString()}, NullInt64()},
    {"octet_length", {""}, 0ll},
    {"octet_length", {"e"}, 1ll},
    {"octet_length", {"abcde"}, 5ll},
    {"octet_length", {"абвгд"}, 10ll},
    {"octet_length", {"\0\0"}, 2ll},
    // OCTET_LENGTH(bytes) -> int64
    {"octet_length", {NullBytes()}, NullInt64()},
    {"octet_length", {Bytes("")}, 0ll},
    {"octet_length", {Bytes("e")}, 1ll},
    {"octet_length", {Bytes("abcde")}, 5ll},
    {"octet_length", {Bytes("абвгд")}, 10ll},
    {"octet_length", {Bytes("\0\0")}, 2ll},
  };
  return results;
}

std::vector<FunctionTestCall> GetFunctionTestsAscii() {
  std::vector<FunctionTestCall> results = {
    // ASCII(string) -> int64
    {"ascii", {NullString()}, NullInt64()},
    {"ascii", {""}, 0ll},
    {"ascii", {" A"}, 32ll},
    {"ascii", {"a"}, 97ll},
    {"ascii", {"abcd"}, 97ll},
    {"ascii", {"nЖЩФ"}, 110ll},
    {"ascii", {"\x41"}, 65ll},
    {"ascii", {"\?"}, 63ll},
    {"ascii", {"\t"}, 9ll},
    {"ascii", {"\uFFFF"}, NullInt64(), OUT_OF_RANGE},
    {"ascii", {"ЖЩФ"}, NullInt64(), OUT_OF_RANGE},

    // ASCII(bytes) -> int64
    {"ascii", {NullBytes()}, NullInt64()},
    {"ascii", {Bytes("")}, 0ll},
    {"ascii", {Bytes(" A")}, 32ll},
    {"ascii", {Bytes("a")}, 97ll},
    {"ascii", {Bytes("abcd")}, 97ll},
    {"ascii", {Bytes("nbca\0\1cde")}, 110ll},
    {"ascii", {Bytes("\x41")}, 65ll},
    {"ascii", {Bytes("\?")}, 63ll},
    {"ascii", {Bytes("\t")}, 9ll},
    {"ascii", {Bytes("\uFFFF")}, 239ll},
    {"ascii", {Bytes("ЖЩФ")}, 208ll},
  };
  return results;
}

std::vector<FunctionTestCall> GetFunctionTestsUnicode() {
  std::vector<FunctionTestCall> results = {
    // unicode(string) -> int64
    {"unicode", {NullString()}, NullInt64()},
    {"unicode", {""}, 0ll},
    {"unicode", {" A"}, 32ll},
    {"unicode", {"a"}, 97ll},
    {"unicode", {"abcd"}, 97ll},
    {"unicode", {"nЖЩФ"}, 110ll},
    {"unicode", {"\x41"}, 65ll},
    {"unicode", {"?"}, 63ll},
    {"unicode", {"\t"}, 9ll},
    {"unicode", {"\uE000"}, 0xE000ll},
    {"unicode", {"\uFFFF"}, 0xFFFFll},
    {"unicode", {"\U0010FFFE"}, 0x10FFFEll},
    {"unicode", {"\U0010FFFF"}, 0x10FFFFll},
    {"unicode", {"жщф"}, 1078ll},
  };
  { // construct a string
    std::string valid_codepoint_string;
    valid_codepoint_string.push_back(0x11);
    valid_codepoint_string.push_back('\xFF');
    valid_codepoint_string.push_back('\xFF');

    results.push_back({"unicode", {String(valid_codepoint_string)}, 17ll});
  }
  // Error cases.
  // The C++ compiler rejects Unicode literals in strings that aren't valid
  // codepoints, so we have to construct them "manually".
  {
    std::string invalid_codepoint_string;
    // The first character is an invalid codepoint.
    invalid_codepoint_string.push_back(0xD8);
    // invalid_codepoint_string.push_back('\xFF');

    results.push_back(
        {"unicode", {String(invalid_codepoint_string)}, NullInt64(),
         absl::OutOfRangeError("First char of input is not a structurally "
                               "valid UTF-8 character: '\\xd8'")});
  }
  {
    std::string invalid_codepoint_string;
    // The first character is an invalid codepoint.
    invalid_codepoint_string.push_back(0xDF);
    invalid_codepoint_string.push_back('\xFF');

    results.push_back(
        {"unicode", {String(invalid_codepoint_string)}, NullInt64(),
         absl::OutOfRangeError("First char of input is not a structurally "
                               "valid UTF-8 character: '\\xdf'")});
  }
  {
    std::string invalid_codepoint_string;
    // Invalid three-byte codepoint (above the valid range).
    invalid_codepoint_string.push_back(0xFF);
    invalid_codepoint_string.push_back('\xFF');
    invalid_codepoint_string.push_back('\xFF');
    invalid_codepoint_string.push_back('\xFF');

    results.push_back(
        {"unicode", {String(invalid_codepoint_string)}, NullInt64(),
         absl::OutOfRangeError("First char of input is not a structurally "
                               "valid UTF-8 character: '\\xff'")});
  }
  return results;
}

std::vector<FunctionTestCall> GetFunctionTestsChr() {
  std::vector<FunctionTestCall> results = {
    {"chr", {NullInt64()}, NullString()},
    {"chr", {32ll}, String(" ")},
    {"chr", {97ll}, String("a")},
    {"chr", {66ll}, String("B")},
    {"chr", {0ll}, String("\0")},
    {"chr", {1ll}, String("\1")},
    {"chr", {1078ll}, String("ж")},
    {"chr", {1076ll}, String("д")},
    {"chr", {0xD7FEll}, String("\uD7FE")},
    {"chr", {0xE000ll}, String("\uE000")},
    {"chr", {0x10FFFFll}, String("\U0010FFFF")},
    {"chr", {-1ll}, NullString(),
     absl::OutOfRangeError("Invalid codepoint -1")},
    {"chr", {-100ll}, NullString(),
     absl::OutOfRangeError("Invalid codepoint -100")},
    {"chr", {0xD800ll}, NullString(),
     absl::OutOfRangeError("Invalid codepoint 55296")},
    {"chr", {0x11FFFFll}, NullString(),
     absl::OutOfRangeError("Invalid codepoint 1179647")},
    {"chr", {0x1000000000000ll}, NullString(),
     absl::OutOfRangeError("Invalid codepoint 281474976710656")},
  };
  return results;
}

std::vector<FunctionTestCall> GetFunctionTestsSoundex() {
  std::vector<FunctionTestCall> results = {
      // soundex(string) -> string
      {"soundex", {NullString()}, NullString()},
      {"soundex", {""}, ""},
      // Invalid characters are skipped.
      {"soundex", {" "}, ""},
      {"soundex", {"  \t \n 78912!(@#&   "}, ""},
      {"soundex", {"A"}, "A000"},
      {"soundex", {"Aa"}, "A000"},
      {"soundex", {"AaAaa a123aA"}, "A000"},
      // Test that all alpha character maps to the correct value.
      {"soundex", {"AEIOUYHWaeiouyhw"}, "A000"},
      {"soundex", {"ABFPVbfpv"}, "A100"},
      {"soundex", {"ACGJKQSXZcgjkqsxz"}, "A200"},
      {"soundex", {"ADTdt"}, "A300"},
      {"soundex", {"ALl"}, "A400"},
      {"soundex", {"AMNmn"}, "A500"},
      {"soundex", {"ARr"}, "A600"},
      // H, W are considered vowels and are skipped.
      {"soundex", {"AAWIOUYHWB"}, "A100"},
      {"soundex", {"KhcABl"}, "K140"},
      // Second c is skipped because it has the same code as K.
      {"soundex", {"KcABl"}, "K140"},
      // c is skipped because the precedent non-skipped code is the same (s).
      {"soundex", {"Ashcraft"}, "A261"},
      {"soundex", {"Robert"}, "R163"},
      {"soundex", {"Rupert"}, "R163"},
      {"soundex", {"I LOVE YOU"}, "I410"},
      {"soundex", {"I LOVE YOU TOO"}, "I413"},
      {"soundex", {"ACDL"}, "A234"},
      {"soundex", {"3  ACDL"}, "A234"},
      {"soundex", {"ACDL56+56/-13"}, "A234"},
      {"soundex", {"AC#$!D193  L"}, "A234"},
      // Multi-byte UTF8 characters are skipped. The second l is skipped because
      // the previous valid character is already l.
      {"soundex", {"Aгдaгдlдl"}, "A400"}};

  // Test every character to make sure all non-alpha character is invalid.
  absl::flat_hash_set<char> re_special_chars({'[', ']', '(', ')', '{', '}', '.',
                                              '*', '+', '^', '$', '\\', '?',
                                              '|', '"'});
  for (int i = 0; i < 256; ++i) {
    char c = static_cast<char>(i);
    if (re_special_chars.contains(c)) {
      // TODO: Re-enable once special chars are differentiated in
      //     Generated tests names.
      continue;
    }
    std::string single_char = std::string(1, c);
    results.push_back(
        {"soundex",
         {single_char},
         absl::ascii_isalpha(c) ? absl::StrCat(single_char, "000") : ""});
  }

  return results;
}

std::vector<FunctionTestCall> GetFunctionTestsTranslate() {
  constexpr size_t kMaxOutputSize = (1 << 20);  // 1MB

  const std::string small_size_text(kMaxOutputSize / 2 + 1, 'a');
  const std::string exact_size_text(kMaxOutputSize, 'a');
  const std::string large_size_text(kMaxOutputSize + 1, 'a');

  std::vector<FunctionTestCall> results = {
      // translate(string, string, string) -> string
      {"translate", {NullString(), NullString(), NullString()}, NullString()},
      {"translate", {NullString(), NullString(), ""}, NullString()},
      {"translate", {NullString(), "", NullString()}, NullString()},
      {"translate", {NullString(), "", ""}, NullString()},
      {"translate", {"", NullString(), NullString()}, NullString()},
      {"translate", {"", NullString(), ""}, NullString()},
      {"translate", {"", "", NullString()}, NullString()},

      {"translate", {"", "", ""}, ""},
      {"translate", {"abcde", "", ""}, "abcde"},
      // Repeated characters in source characters are not allowed (even if they
      // map to the same target character).
      {"translate", {"abcde", "aba", "xyz"}, NullString(), OUT_OF_RANGE},
      {"translate", {"abcde", "aba", "xyx"}, NullString(), OUT_OF_RANGE},
      // Non valid UTF-8 characters are not allowed
      {"translate", {"abcde", "a\xC0", "bc"}, NullString(), OUT_OF_RANGE},
      {"translate", {"abcde", "ab", "b\xC0"}, NullString(), OUT_OF_RANGE},
      // Source characters without a corresponding target character are removed
      // from the input.
      {"translate", {"abcde", "ad", "x"}, "xbce"},
      {"translate", {"abcdebadbad", "ad", "x"}, "xbcebxbx"},
      // Extra characters in target characters are ignored.
      {"translate", {"abcde", "ab", "xyz123xy"}, "xycde"},
      // Characters are only modified once.
      {"translate", {"ababbce", "ab", "ba"}, "babaace"},
      {"translate", {"abcde", "abd", "xyz"}, "xycze"},
      {"translate", {"abcdebde", "abd", "xyz"}, "xyczeyze"},
      {"translate", {"abcde", "abd", "xдz"}, "xдcze"},
      {"translate", {"abcdebde", "abde", "xдz"}, "xдczдz"},
      {"translate", {"abcдd\nebde", "ab\ndeд", "12ф4\t456"}, "12c44ф\t24\t"},

      // Testing output size errors.
      {"translate", {small_size_text, "a", "a"}, small_size_text},
      // Input size is under limit, but TRANSLATE will make it over limit.
      // ¢ is a 2-byte character.
      {"translate",
       {small_size_text, "a", "¢"},
       NullString(),
       absl::OutOfRangeError(
           "Output of TRANSLATE exceeds max allowed output size of 1MB")},
      {"translate", {exact_size_text, "a", "a"}, exact_size_text},
      {"translate",
       {large_size_text, "", ""},
       NullString(),
       absl::OutOfRangeError(
           "Output of TRANSLATE exceeds max allowed output size of 1MB")},

      // Testing duplicate character
      {"translate",
       {"unused", "aa", "bc"},
       NullString(),
       absl::OutOfRangeError(
           "Duplicate character \"a\" in TRANSLATE source characters")},
      // Error even if the duplicate character doesn't have a corresponding
      // target character.
      {"translate",
       {"unused", "aa", "bc"},
       NullString(),
       absl::OutOfRangeError(
           "Duplicate character \"a\" in TRANSLATE source characters")},
      // Make sure characters are correctly escaped.
      {"translate",
       {"unused", "ab\nd\n", "vwxyz"},
       NullString(),
       absl::OutOfRangeError(
           "Duplicate character \"\\n\" in TRANSLATE source characters")},
      {"translate",
       {"unused", "ab'd'", "vwxyz"},
       NullString(),
       absl::OutOfRangeError(
           "Duplicate character \"'\" in TRANSLATE source characters")},
      {"translate",
       {"unused", "ab\"d\"", "vwxyz"},
       NullString(),
       absl::OutOfRangeError(
           "Duplicate character '\"' in TRANSLATE source characters")},

      // translate(bytes, bytes, bytes) -> bytes
      {"translate", {NullBytes(), NullBytes(), NullBytes()}, NullBytes()},
      {"translate", {NullBytes(), NullBytes(), Bytes("")}, NullBytes()},
      {"translate", {NullBytes(), Bytes(""), NullBytes()}, NullBytes()},
      {"translate", {NullBytes(), Bytes(""), Bytes("")}, NullBytes()},
      {"translate", {Bytes(""), NullBytes(), NullBytes()}, NullBytes()},
      {"translate", {Bytes(""), NullBytes(), Bytes("")}, NullBytes()},
      {"translate", {Bytes(""), Bytes(""), NullBytes()}, NullBytes()},

      {"translate", {Bytes(""), Bytes(""), Bytes("")}, Bytes("")},
      {"translate", {Bytes("abcde"), Bytes(""), Bytes("")}, Bytes("abcde")},
      // Repeated characters in source characters are not allowed (even if they
      // map to the same target character).
      {"translate",
       {Bytes("abcde"), Bytes("aba"), Bytes("xyz")},
       NullBytes(),
       OUT_OF_RANGE},
      {"translate",
       {Bytes("abcde"), Bytes("aba"), Bytes("xyx")},
       NullBytes(),
       OUT_OF_RANGE},
      // Source characters without a corresponding target character are removed
      // from the input.
      {"translate", {Bytes("abcde"), Bytes("ad"), Bytes("x")}, Bytes("xbce")},
      {"translate",
       {Bytes("abcdebadbad"), Bytes("ad"), Bytes("x")},
       Bytes("xbcebxbx")},
      // Extra characters in target characters are ignored.
      {"translate",
       {Bytes("abcde"), Bytes("ab"), Bytes("xyz123xy")},
       Bytes("xycde")},
      // Characters are only modified once.
      {"translate",
       {Bytes("ababbce"), Bytes("ab"), Bytes("ba")},
       Bytes("babaace")},
      {"translate",
       {Bytes("abcde"), Bytes("abd"), Bytes("xyz")},
       Bytes("xycze")},
      {"translate",
       {Bytes("abcdebde"), Bytes("abd"), Bytes("xyz")},
       Bytes("xyczeyze")},
      // д's UTF8 encoding is 0xD0 0xB4.
      {"translate",
       {Bytes("abmdgb"), Bytes("abd"), Bytes("xд")},
       Bytes("x\xD0m\xB4g\xD0")},
      {"translate",
       {Bytes("\x15\x01\xFFz"), Bytes("abc\x15\xFFxyz"), Bytes("\xAAxyz\xFA")},
       Bytes("z\x01\xFA")},

      // Testing output size errors.
      {"translate",
       {Bytes(small_size_text), Bytes("a"), Bytes("a")},
       Bytes(small_size_text)},
      {"translate",
       {Bytes(exact_size_text), Bytes("a"), Bytes("a")},
       Bytes(exact_size_text)},
      {"translate",
       {Bytes(large_size_text), Bytes(""), Bytes("")},
       NullBytes(),
       absl::OutOfRangeError(
           "Output of TRANSLATE exceeds max allowed output size of 1MB")},
      // Testing duplicate byte
      {"translate",
       {Bytes("unused"), Bytes("abcda"), Bytes("vwxyz")},
       NullBytes(),
       absl::OutOfRangeError("Duplicate byte 0x61 in TRANSLATE source bytes")},
      {"translate",
       {Bytes("unused"), Bytes("\x01\x01"), Bytes("yz")},
       NullBytes(),
       absl::OutOfRangeError("Duplicate byte 0x01 in TRANSLATE source bytes")},
  };

  return results;
}

std::vector<FunctionTestCall> GetFunctionTestsInitCap() {
  return {
      // initcap(string) -> string
      {"initcap", {NullString()}, NullString()},
      {"initcap", {""}, ""},
      {"initcap", {" "}, " "},
      {"initcap", {" a 1"}, " A 1"},
      {"initcap", {" a \tb"}, " A \tB"},
      {"initcap", {" a\tb\nc d"}, " A\tB\nC D"},
      {"initcap", {"aB-C ?DEF []h? j"}, "Ab-C ?Def []H? J"},
      {"initcap", {"ταινιών"}, "Ταινιών"},
      {"initcap", {"abcABC ж--щ фФ"}, "Abcabc Ж--Щ Фф"},
      {"initcap", {"\x61\xa7\x65\x71"}, NullString(), OUT_OF_RANGE},

      // initcap(string, string) -> string
      {"initcap", {NullString(), NullString()}, NullString()},
      {"initcap", {NullString(), ""}, NullString()},
      {"initcap", {"", NullString()}, NullString()},
      {"initcap", {"a \tb\n\tc d\ne", " \t"}, "A \tB\n\tC D\ne"},
      {"initcap", {"aBc -d", " -"}, "Abc -D"},
      {"initcap", {"a b", "ab"}, "a b"},
      {"initcap", {"cbac", "aabb"}, "CbaC"},
      {"initcap", {"Aa BC\nd", "ab\n"}, "Aa bc\nD"},
      {"initcap", {"aAbBc", "abAB"}, "aAbBC"},
      {"initcap", {"HERE-iS-a string", "a -"}, "Here-Is-a String"},
      {"initcap", {"τΑΑ τιν ιτιών", "τ"}, "τΑα τΙν ιτΙών"},
      {"initcap", {" 123 4a 5A b", " 1 234"}, " 123 4A 5a B"},
      {"initcap", {"ABCD", ""}, "Abcd"},
      {"initcap", {"\x61\xa7\x65\x71", " "}, NullString(), OUT_OF_RANGE},
      {"initcap", {"", "\x61\xa7\x65\x71"}, NullString(), OUT_OF_RANGE},
  };
}

// Defines the test cases for string normalization functions.
// The second argument represents the expected results under each
// normalize mode following exact same order as {NFC, NFKC, NFD, NFKD}.
static std::vector<NormalizeTestCase> GetNormalizeTestCases() {
  return {
    // normalize(string [, mode]) -> string
    {NullString(), {NullString(), NullString(), NullString(), NullString()}},
    {"", {"", "", "", ""}},
    {"abcABC", {"abcABC", "abcABC", "abcABC", "abcABC"}},
    {"abcABCжщфЖЩФ", {"abcABCжщфЖЩФ", "abcABCжщфЖЩФ", "abcABCжщфЖЩФ",
        "abcABCжщфЖЩФ"}},
    {"Ḋ", {"Ḋ", "Ḋ", "D\u0307", "D\u0307"}},
    {"D\u0307", {"Ḋ", "Ḋ", "D\u0307", "D\u0307"}},
    {"Google™", {"Google™", "GoogleTM", "Google™", "GoogleTM"}},
    {"龟", {"龟", "\u9F9F", "龟", "\u9F9F"}},
    {"10¹²³", {"10¹²³", "10123", "10¹²³", "10123"}},
    {"ẛ̣", {"ẛ̣", "ṩ", "\u017f\u0323\u0307", "s\u0323\u0307"}},
    {"Google™Ḋ龟10¹²³", {"Google™Ḋ龟10¹²³", "GoogleTMḊ\u9F9F10123",
        "Google™D\u0307龟10¹²³", "GoogleTMD\u0307\u9F9F10123"}},

    // normalize_and_casefold(string [, mode]) -> string
    {NullString(), {NullString(), NullString(), NullString(), NullString()},
        true /* is_casefold */},
    {"", {"", "", "", ""}, true},
    {"abcABC", {"abcabc", "abcabc", "abcabc", "abcabc"}, true},
    {"abcabcжщфЖЩФ", {"abcabcжщфжщф", "abcabcжщфжщф", "abcabcжщфжщф",
        "abcabcжщфжщф"}, true},
    {"Ḋ", {"ḋ", "ḋ", "d\u0307", "d\u0307"}, true},
    {"D\u0307", {"ḋ", "ḋ", "d\u0307", "d\u0307"}, true},
    {"Google™", {"google™", "googletm", "google™", "googletm"}, true},
    {"龟", {"龟", "\u9F9F", "龟", "\u9F9F"}, true},
    {"10¹²³", {"10¹²³", "10123", "10¹²³", "10123"}, true},
    {"ẛ̣", {"ṩ", "ṩ", "s\u0323\u0307", "s\u0323\u0307"}, true},
    {"Google™Ḋ龟10¹²³", {"google™ḋ龟10¹²³", "googletmḋ\u9F9F10123",
        "google™d\u0307龟10¹²³", "googletmd\u0307\u9F9F10123"}, true},
  };
}

std::vector<FunctionTestCall> GetFunctionTestsNormalize() {
  const googlesql::EnumType* enum_type = nullptr;
  GOOGLESQL_CHECK_OK(type_factory()->MakeEnumType(
      googlesql::functions::NormalizeMode_descriptor(), &enum_type));

  // For each NormalizeTestCase, constructs 5 FunctionTestCalls for each
  // normalize mode (default/NFC, NFC, NFKC, NFD, NFKD), respectively.
  std::vector<FunctionTestCall> ret;
  for (const NormalizeTestCase& test_case : GetNormalizeTestCases()) {
    ABSL_CHECK_EQ(functions::NormalizeMode_ARRAYSIZE, test_case.expected_nfs.size());
    const std::string function_name =
        test_case.is_casefold ? "normalize_and_casefold" : "normalize";
    ret.push_back(
        {function_name, {test_case.input}, test_case.expected_nfs[0]});
    for (int mode = 0; mode < functions::NormalizeMode_ARRAYSIZE; ++mode) {
      ret.push_back({function_name,
                     {test_case.input, Value::Enum(enum_type, mode)},
                     test_case.expected_nfs[mode]});
    }

    if (test_case.is_casefold) continue;
    // Adds more tests cases for NORMALIZE to verify the invariants between
    // normal forms.
    // 1. NFC(NFD(s)) = NFC(s)
    ret.push_back({"normalize",
                   {test_case.expected_nfs[functions::NormalizeMode::NFD],
                    Value::Enum(enum_type, functions::NormalizeMode::NFC)},
                   test_case.expected_nfs[functions::NormalizeMode::NFC]});
    // 2. NFD(NFC(s)) = NDF(s)
    ret.push_back({"normalize",
                   {test_case.expected_nfs[functions::NormalizeMode::NFC],
                    Value::Enum(enum_type, functions::NormalizeMode::NFD)},
                   test_case.expected_nfs[functions::NormalizeMode::NFD]});
    for (int mode = 0; mode < functions::NormalizeMode_ARRAYSIZE; ++mode) {
      // 3. NFKC(s) = NFKC(NFC(s)) = NFKC(NFD(s)) = NFKC(NFKC(s) = NFKC(NFKD(s))
      ret.push_back({"normalize",
                     {test_case.expected_nfs[mode],
                      Value::Enum(enum_type, functions::NormalizeMode::NFKC)},
                     test_case.expected_nfs[functions::NormalizeMode::NFKC]});
      // 4. NFKD(s) = NFKD(NFC(s)) = NFKD(NFD(s)) = NFKD(NFKC(s) = NFKD(NFKD(s))
      ret.push_back({"normalize",
                     {test_case.expected_nfs[mode],
                      Value::Enum(enum_type, functions::NormalizeMode::NFKD)},
                     test_case.expected_nfs[functions::NormalizeMode::NFKD]});
    }
  }
  return ret;
}

}  // namespace googlesql
