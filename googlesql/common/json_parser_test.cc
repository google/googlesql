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

#include "googlesql/common/json_parser.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/compliance/depth_limit_detector_test_cases.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"

using googlesql::JSONParser;

namespace {
using ::absl_testing::StatusIs;

// Exclude '\0' in the end to catch bugs like b/153040983.
class StringNoTerminator {
 public:
  explicit StringNoTerminator(absl::string_view input)
      : str_no_terminator_(input.begin(), input.end()) {}
  absl::string_view data() const {
    return {str_no_terminator_.data(), str_no_terminator_.size()};
  }
  std::vector<char> str_no_terminator_;
};

// Constructs a pretty-printed js string from the parser calls.
class JSONToJSON : private StringNoTerminator, public JSONParser {
 private:
  std::string output_;
  int indent_;
  bool indent_next_;

 public:
  explicit JSONToJSON(absl::string_view js)
      : StringNoTerminator(js),
        JSONParser(data()),
        indent_(0),
        indent_next_(true) {}

  // The pretty-printed js that resulted from the call to Parse().
  const std::string& output() const { return output_; }
  void clear_output() { output_.clear(); }

  // Expose protected ContextAtCurrentPosition() for direct testing.
  std::string ContextAtCurrentPosition(
      int context_length = kDefaultContextLength) {
    return JSONParser::ContextAtCurrentPosition(context_length);
  }

 protected:
  bool BeginObject() override {
    Add("{\n");
    indent_++;
    return true;
  }
  bool EndObject() override {
    indent_--;
    Add("}");
    return true;
  }

  bool BeginMember(const std::string& key) override {
    Add(absl::StrFormat("\"%s\" : ", key));
    indent_next_ = false;
    return true;
  }
  bool EndMember(bool last) override {
    if (!last)
      output_.append(",\n");
    else
      output_.append("\n");
    return true;
  }

  bool BeginArray() override {
    Add("[\n");
    indent_++;
    return true;
  }
  bool EndArray() override {
    indent_--;
    Add("]");
    return true;
  }

  bool BeginArrayEntry() override { return true; }

  bool EndArrayEntry(bool last) override {
    if (!last)
      output_.append(",\n");
    else
      output_.append("\n");
    return true;
  }

  bool ParsedString(const std::string& str) override {
    Add(absl::StrFormat("\"%s\"", absl::CEscape(str)));
    return true;
  }
  bool ParsedNumber(absl::string_view str) override {
    Add(str);
    return true;
  }
  bool ParsedBool(bool val) override {
    Add(val ? "true" : "false");
    return true;
  }
  bool ParsedNull() override {
    Add("null");
    return true;
  }

  bool ReportFailure(absl::string_view error_message) override {
    return JSONParser::ReportFailure(error_message);
  }

 private:
  void Add(absl::string_view str) {
    if (indent_next_) PrintIndent();
    indent_next_ = true;
    output_.append(str.begin(), str.end());
  }

  void PrintIndent() {
    for (int i = 0; i < indent_; ++i) output_.append("  ");
  }
};

// Parse `input` and test that the pretty-printed result matches `expected`.
static void ParseAndCompare(absl::string_view input,
                            absl::string_view expected) {
  JSONToJSON j(input);
  GOOGLESQL_ASSERT_OK(j.Parse()) << "Parse failed: " << input;
  EXPECT_EQ(expected, j.output()) << "Input: " << input;
}

static void ParseAndExpectUnchanged(absl::string_view input) {
  return ParseAndCompare(input, input);
}

struct ParseTestCase {
  // Original text to parse.
  const char* orig;
  // Expected successful parse result or context at the point of failure.
  const char* expected;

  explicit ParseTestCase(const char* orig_chars, const char* expected_chars)
      : orig(orig_chars), expected(expected_chars) {}

  explicit ParseTestCase(const char* orig_and_expected)
      : orig(orig_and_expected), expected(orig_and_expected) {}
};

// Try to parse `input` but expect failure.
static void ParseAndExpectFail(ParseTestCase test_case) {
  JSONToJSON j(test_case.orig);
  ASSERT_THAT(j.Parse(), StatusIs(absl::StatusCode::kOutOfRange))
      << "Input: " << test_case.orig;
  EXPECT_EQ(j.ContextAtCurrentPosition(), test_case.expected);
}

TEST(JSONParserTest, ParseIdempotency) {
  const char* str = "\"idempotency\"";
  JSONToJSON j(str);

  // The first parse should succeed.
  GOOGLESQL_ASSERT_OK(j.Parse());
  EXPECT_EQ(str, j.output()) << "Input: " << str;

  // The second parse should also succeed with the same output.
  j.clear_output();
  GOOGLESQL_ASSERT_OK(j.Parse());
  EXPECT_EQ(str, j.output()) << "Input: " << str;
}

TEST(JSONParserTest, ParseString) {
  const char* str = "\"neat\"";
  ParseAndExpectUnchanged(str);
}
ParseTestCase ParseStringComplicated_cases[] = {
    ParseTestCase{"\"\""},    // empty string
    ParseTestCase{"\"\\b\"",  // string with a \b only
                  "\"\\010\""},
    ParseTestCase{"\"hello\\nworld\""},  // \n
    ParseTestCase{"\"hello\\t\""},       // terminal \t
    ParseTestCase{"\"hello \\0\"",       // zero single octal escape sequence
                  "\"hello \\000\""},
    ParseTestCase{"\"hello \\1\"",  // non-zero single octal escape sequence
                  "\"hello \\001\""},
    ParseTestCase{"\"hello\\2world\"",  // 1 digit octal escape sequence
                  "\"hello\\002world\""},
    ParseTestCase{"\"hello \\01\"",  // 2 digit octal escape sequence
                  "\"hello \\001\""},
    ParseTestCase{"\"hello \\019\"",  // 2 digit octal escape sequence followed
                                      // by non-octal
                  "\"hello \\0019\""},
    ParseTestCase{"\"hello\\16world\"",  // 2 digit octal escape sequence
                  "\"hello\\016world\""},
    ParseTestCase{"\"hello\\251world\"",  // full octal escape sequence
                  "\"hello\\302\\251world\""},
    ParseTestCase{
        "\"hello \\2519\"",  // full octal escape sequence followed by non-octal
        "\"hello \\302\\2519\""},
    ParseTestCase{"\"hello \\2517\"",  // full octal escape sequence followed by
                                       // octal digit
                  "\"hello \\302\\2517\""},
    ParseTestCase{"\"hello\\xa9world\"",  // hex escape sequence
                  "\"hello\\302\\251world\""},
    ParseTestCase{"\"helloworld\\xa9\"",  // terminal hex escape sequence
                  "\"helloworld\\302\\251\""},
    ParseTestCase{
        "\"helloworld\\xA9\"",  // terminal hex escape sequence with upper case
        "\"helloworld\\302\\251\""},
    ParseTestCase{
        "\"helloworld\\xA9f\"",  // hex escape sequence followed by hex digit
        "\"helloworld\\302\\251f\""},
    ParseTestCase{
        "\"helloworld\\xA9g\"",  // hex escape sequence followed by non hex
        "\"helloworld\\302\\251g\""},
    ParseTestCase{
        "\"parecer\\u00e1 n\\u00famero\"",  // unicode spanish with accents
        "\"parecer\\303\\241 n\\303\\272mero\""},
    ParseTestCase{
        "\"parecer\\u00e1\\n n\\u00famero\"",  // unicode spanish with accents
                                               // followed by another escape
                                               // sequence
        "\"parecer\\303\\241\\n n\\303\\272mero\""},
    ParseTestCase{
        "\"\\u4e2d\\u65b0\\u7f5111\\u67087\\u65e5\\u7535\"",  // unicode chinese
        "\"\\344\\270\\255\\346\\226\\260\\347\\275\\22111"
        "\\346\\234\\2107\\346\\227\\245\\347\\224\\265\""},
    ParseTestCase{"'n\\e\\a\\t'", "\"nea\\t\""},
    ParseTestCase{"'neat'", "\"neat\""},
    ParseTestCase{"\"neat\\\\\""},
    ParseTestCase{"'ne\\\"at'", "\"ne\\\"at\""},
    ParseTestCase{"\"ne'at\"",
                  "\"ne\\\'at\""},  // ' is escaped unnecessarily by test code.
    ParseTestCase{"   \"neat\"", "\"neat\""},
    ParseTestCase{"\"neat\"   ", "\"neat\""},
    ParseTestCase{"\"ne\\\"at\""},
};

TEST(JSONParserTest, ParseStringComplicated) {
  for (int i = 0; i < ABSL_ARRAYSIZE(ParseStringComplicated_cases); ++i) {
    const char* orig = ParseStringComplicated_cases[i].orig;
    const char* expected = ParseStringComplicated_cases[i].expected;
    ParseAndCompare(orig, expected);
  }
}

ParseTestCase ParseStringFail_cases[] = {
    ParseTestCase{"\"\\"},      // escape nothing and don't terminate string
    ParseTestCase{"\"\\\""},    // escape nothing
    ParseTestCase{"\"\\x\""},   // empty hex encoding
    ParseTestCase{"\"\\xf\""},  // short hex encoding with high bit set
    ParseTestCase{"\"neat"},
    ParseTestCase{"neat\""},
    ParseTestCase{"ne\"at\""},
    ParseTestCase{"neat'"},
    ParseTestCase{"\"neat\\\\\\\""},  // three backslashes
    // incomplete UTF-8 BOM; shouldn't parse as whitespace or json
    ParseTestCase{"\xEF\xBB"},
    // invalid UTF-8 becomes U+FFFD, which is an "unexpected token"
    ParseTestCase{"\x80"},
    // UTF-8 BOM not at the beginning is an "unexpected token"
    ParseTestCase{"test\xEF\xBB\xBF"},
    // Long non-JSON string
    ParseTestCase{"abcdefghijklmnopqrstuvwxyz", "abcdefghij"},
};

TEST(JSONParserTest, ParseStringFail) {
  for (int i = 0; i < ABSL_ARRAYSIZE(ParseStringFail_cases); ++i) {
    ParseTestCase test_case = ParseStringFail_cases[i];
    ParseAndExpectFail(test_case);
  }
}

TEST(JSONParserTest, ParseStringFailOctalDigits) {
  // Parse special case to test the ABSL_DCHECK in ParseOctalDigits.
  // Resizing to special.size() because it strips out the null terminator.
  std::string special = "\"\\4";
  special.resize(special.size());
  JSONToJSON j(special);
  ASSERT_THAT(j.Parse(), StatusIs(absl::StatusCode::kOutOfRange))
      << "Input: " << special;
  EXPECT_EQ(j.ContextAtCurrentPosition(), special);
}

TEST(JSONParserTest, ParseUTF7FalsePositive) {
  const std::string str = "\"+2011+Abc\"";

  // Explicitly set the encoding so that it is not mistaken for UTF-7.
  JSONToJSON j(str);

  GOOGLESQL_ASSERT_OK(j.Parse());
  EXPECT_EQ(str, j.output());
}

TEST(JSONParserTest, ParseDouble) {
  const char* str = "5.734";
  ParseAndCompare(str, "5.734");
}

ParseTestCase ParseDoubleComplicated_cases[] = {
    ParseTestCase{"5"},
    ParseTestCase{"-5"},
    ParseTestCase{"-5.734"},
    ParseTestCase{"-5.734e2"},
    ParseTestCase{"-5.734e+2"},
    ParseTestCase{"5.734e-2"},
    ParseTestCase{"-5e2"},
    ParseTestCase{"-5E2"},
    ParseTestCase{"    -5e2", "-5e2"},
    ParseTestCase{"-5e2    ", "-5e2"},
};

TEST(JSONParserTest, ParseDoubleComplicated) {
  for (int i = 0; i < ABSL_ARRAYSIZE(ParseDoubleComplicated_cases); ++i) {
    const char* orig = ParseDoubleComplicated_cases[i].orig;
    const char* expected = ParseDoubleComplicated_cases[i].expected;
    ParseAndCompare(orig, expected);
  }
}

ParseTestCase ParseDoubleFail_cases[] = {
    ParseTestCase{"-5e2abc"},
    ParseTestCase{"-ab5e2"},
    ParseTestCase{"-5e"},
};

TEST(JSONParserTest, ParseDoubleFail) {
  for (int i = 0; i < ABSL_ARRAYSIZE(ParseDoubleFail_cases); ++i) {
    ParseTestCase test_case = ParseDoubleFail_cases[i];
    ParseAndExpectFail(test_case);
  }
}

TEST(JSONParserTest, ParseNumber) {
  JSONToJSON parser("5.734");
  absl::Status status = parser.Parse();
  GOOGLESQL_ASSERT_OK(status);
  EXPECT_EQ("5.734", parser.output());
}

ParseTestCase ParseNumberComplicated_cases[] = {
    ParseTestCase{"5"},
    ParseTestCase{"-5"},
    ParseTestCase{"-5.734"},
    ParseTestCase{"-5.734e2"},
    ParseTestCase{"-5.734e+2"},
    ParseTestCase{"5.734e-2"},
    ParseTestCase{"-5e2"},
    ParseTestCase{"-5E2"},
    ParseTestCase{"    -5e2", "-5e2"},
    ParseTestCase{"-5e2    ", "-5e2"},
    ParseTestCase{"999E9999"},  // Out of range for double.
};

TEST(JSONParserTest, ParseNumberComplicated) {
  for (auto test : ParseNumberComplicated_cases) {
    JSONToJSON parser(test.orig);
    absl::Status status = parser.Parse();
    GOOGLESQL_EXPECT_OK(status);
    EXPECT_EQ(test.expected, parser.output()) << "Input: " << test.orig;
  }
}

const ParseTestCase ParseNumberFail_cases[] = {
    // Invalid characters.
    ParseTestCase{"-5e2abc"},
    ParseTestCase{"-ab5e2"},
    // - not followed by digits.
    ParseTestCase{"-.003"},
    ParseTestCase{"-E30"},
    ParseTestCase{"-+"},
    ParseTestCase{"--"},
    ParseTestCase{"-"},
    // Leading 0 cannot be followed by digits.
    ParseTestCase{"00.0"},
    ParseTestCase{"01"},
    ParseTestCase{"-04"},
    // . not followed by digits.
    ParseTestCase{"1.e3"},
    ParseTestCase{"1.+"},
    ParseTestCase{"1.-"},
    ParseTestCase{"1.."},
    ParseTestCase{"1."},
    // E not followed by digits.
    ParseTestCase{"1.5E"},
    ParseTestCase{"1.5E."},
    ParseTestCase{"1.5eE"},
    // + not followed by digits.
    ParseTestCase{"5E+"},
    ParseTestCase{"5E+."},
    ParseTestCase{"5E+-"},
    ParseTestCase{"5E+e"},
    ParseTestCase{"5E++"},
    // - not followed by digits.
    ParseTestCase{"5E-"},
    ParseTestCase{"5E-."},
    ParseTestCase{"5E-+"},
    ParseTestCase{"5E-e"},
    ParseTestCase{"5E--"},
    // Must start with - or digit.
    ParseTestCase{".2"},
    ParseTestCase{"e20"},
    ParseTestCase{"+100"},
};

TEST(JSONParserTest, ParseNumberFail) {
  for (auto test : ParseNumberFail_cases) {
    ParseAndExpectFail(test);
  }
}

TEST(JSONParserTest, ParseBool) {
  ParseAndExpectUnchanged("true");
  ParseAndExpectUnchanged("false");
}

TEST(JSONParserTest, ParseBoolFail) {
  ParseAndExpectFail(ParseTestCase{"treu", "treu"});    // NOTYPO
  ParseAndExpectFail(ParseTestCase{"fasle", "fasle"});  // NOTYPO
  ParseAndExpectFail(ParseTestCase{"tr", "tr"});
  ParseAndExpectFail(ParseTestCase{"fa", "fa"});
}

TEST(JSONParserTest, ParseNull) { ParseAndExpectUnchanged("null"); }

TEST(JSONParserTest, ParseNullFail) {
  ParseAndExpectFail(ParseTestCase{"nul", "nul"});
}

TEST(JSONParserTest, ParseObject) {
  const char* str;
  const char* exp;
  str =
      "{\n"
      "  \"a\" : true,\n"
      "  \"b\" : false,\n"
      "}";
  exp =
      "{\n"
      "  \"a\" : true,\n"
      "  \"b\" : false\n"
      "}";
  ParseAndCompare(str, exp);
  str =
      "{\n"
      "  \"a\" : true,\n"
      "  \"b\" : false\n"
      "}";
  ParseAndExpectUnchanged(str);
}

ParseTestCase ParseObjectFail_cases[] = {
    ParseTestCase{"{\n"
                  "  \"a\""},  // no colon
    ParseTestCase{"{\n"
                  "  \"a\" : true\n"  // no comma
                  "  \"b\" : false,\n"
                  "}",
                  ": true\n  \"b\" : false"},
    ParseTestCase{"{\n"
                  "  \"a\" : true,\n"
                  "  \"b\" : false\n"
                  "]",  // ] instead of }
                  " : false\n]"},
    ParseTestCase{"{\n"
                  "  \"a\" : true,\n"
                  "  \"b\" : false\n"
                  "]",  // ] instead of }
                  " : false\n]"},
    ParseTestCase{"{\n"  // Array in object brackets.
                  "  \"a\",\n"
                  "  \"b\"\n"
                  "}"},
    ParseTestCase{"{\n"
                  "  \"a\" : true,,\n"  // Repeated commas
                  "  \"b\" : false\n"
                  "}",
                  "a\" : true,,\n  \"b\" : "},
    ParseTestCase{"{\n"
                  "  \"a\" : true,\n"
                  "  \"b\" : false"
                  "",  // missing final token.
                  "b\" : false"},
};

TEST(JSONParserTest, ParseObjectFail) {
  for (auto test : ParseObjectFail_cases) {
    ParseAndExpectFail(test);
  }
}

TEST(JSONParserTest, ParseArraySimple) {
  const char* str;
  str =
      "[\n"
      "  true,\n"
      "  false\n"
      "]";
  ParseAndExpectUnchanged(str);
}

TEST(JSONParserTest, ParseArrayTrailingComma) {
  std::unique_ptr<JSONToJSON> parser;
  const char *str, *exp;
  str =
      "[\n"
      "  true,\n"
      "  false,\n"
      "]";
  exp =
      "[\n"
      "  true,\n"
      "  false\n"
      "]";
  parser = std::make_unique<JSONToJSON>(str);
  GOOGLESQL_ASSERT_OK(parser->Parse());
  EXPECT_EQ(exp, parser->output());
}

TEST(JSONParserTest, ParseArrayRepeatedComma) {
  std::unique_ptr<JSONToJSON> parser;
  const char *str, *exp;
  str = "[true,,false]";
  exp =
      "[\n"
      "  true,\n"
      "  null,\n"
      "  false\n"
      "]";
  parser = std::make_unique<JSONToJSON>(str);
  GOOGLESQL_ASSERT_OK(parser->Parse());
  EXPECT_EQ(exp, parser->output());
}

TEST(JSONParserTest, ParseArrayLeadingCommas) {
  std::unique_ptr<JSONToJSON> parser;
  const char *str, *exp;
  str = "[,,null]";
  exp =
      "[\n"
      "  null,\n"
      "  null,\n"
      "  null\n"
      "]";
  parser = std::make_unique<JSONToJSON>(str);
  GOOGLESQL_ASSERT_OK(parser->Parse());
  EXPECT_EQ(exp, parser->output());
}

ParseTestCase ParseArrayFail_cases[] = {
    ParseTestCase{"[\n"
                  "  true\n"  // no comma
                  "  false,\n"
                  "]",
                  "  true\n  false,\n]"},
    ParseTestCase{"[\n"
                  "  true,\n"
                  "  false\n"
                  "}",  // } instead of ]
                  "\n  false\n}"},
    ParseTestCase{"[\n"  // Object in array brackets.
                  "  \"a\" : true,\n"
                  "  \"b\" : false\n"
                  "]",
                  "[\n  \"a\" : true,\n  \""},
};

TEST(JSONParserTest, ParseArrayFail) {
  for (auto test : ParseArrayFail_cases) {
    ParseAndExpectFail(test);
  }
}

TEST(JSONParserTest, UTF8) {
  const char* str = "\"\xE6\x95\x8F\xE6\x84\x9F\"";
  const char* exp = "\"\\346\\225\\217\\346\\204\\237\"";
  ParseAndCompare(str, exp);
}

TEST(JSONParserTest, ParseComplicated1) {
  const char* str =
      "{\n"
      "  \"glossary\" : {\n"
      "    \"title\" : \"example glossary\",\n"
      "    \"GlossDiv\" : {\n"
      "      \"title\" : \"S\",\n"
      "      \"GlossList\" : [\n"
      "        {\n"
      "          \"ID\" : \"SGML\",\n"
      "          \"SortAs\" : \"SGML\",\n"
      "          \"GlossTerm\" : \"Standard Generalized Markup Language\",\n"
      "          \"Acronym\" : \"SGML\",\n"
      "          \"Abbrev\" : \"ISO 8879:1986\",\n"
      "          \"GlossDef\" : \"A meta-markup language.\",\n"
      "          \"GlossSeeAlso\" : [\n"
      "            \"GML\",\n"
      "            \"XML\",\n"
      "            \"markup\"\n"
      "          ]\n"
      "        }\n"
      "      ]\n"
      "    }\n"
      "  }\n"
      "}";
  ParseAndExpectUnchanged(str);
}

TEST(JSONParserTest, ParseComplicated2) {
  const char* str =
      "{\n"
      "  \"a\" : [\n"
      "    [\n"
      "      5.200000,\n"
      "      false,\n"
      "      null\n"
      "    ],\n"
      "    [\n"
      "    ]\n"
      "  ],\n"
      "  \"b\" : {\n"
      "  },\n"
      "  \"c\" : false\n"
      "}";
  ParseAndExpectUnchanged(str);
}

// --- Optional extensions ---

// Test non-string keys in objects

TEST(JSONParserTest, KeyValueSimple) {
  const char* str =
      "{\n"
      "  str : \"foo\",\n"
      "  num : 5.000000,\n"
      "  bool : true,\n"
      "  bool : false,\n"
      "  obj : null\n,"
      "  5 : 2\n"
      "}";
  // Verify failure in non-extended mode
  JSONParser parser(str);
  EXPECT_THAT(parser.Parse(), StatusIs(absl::StatusCode::kOutOfRange))
      << "Input: " << str;
}

TEST(JSONParserTest, KeyValueComplicated) {
  const char* str =
      "{\n"
      "  f13s : 1.000000,\n"
      "  f__ : 2.000000,\n"
      "  $f_d : 3.000000\n"
      "}";
  // Verify failure in non-extended mode
  JSONParser parser(str);
  EXPECT_THAT(parser.Parse(), StatusIs(absl::StatusCode::kOutOfRange))
      << "Input: " << str;
}

TEST(JSONParserTest, KeyValueFail) {
  // Bad keys
  {
    const char* str = "{1f : 1}";
    JSONParser parser(str);
    EXPECT_THAT(parser.Parse(), StatusIs(absl::StatusCode::kOutOfRange))
        << "Input: " << str;
  }
  {
    const char* str = "{*f : 2}";
    JSONParser parser(str);
    EXPECT_THAT(parser.Parse(), StatusIs(absl::StatusCode::kOutOfRange))
        << "Input: " << str;
  }
}

// Functions

const char* FunctionSimple_cases[] = {
    "function ( ) { }",
    "function (arg) { }",                   // 1 arg
    "function (arg1, _arg$3, foo5__) { }",  // 3 args
    "function ( ) {return \"{\"}",          // Open brace in string
    "function ( ) {return \"}\"}",          // Close brace in string
    "function ( ) {{{}{{}}{}}}",            // Matching braces
};

TEST(JSONParserTest, FunctionSimple) {
  for (int i = 0; i < ABSL_ARRAYSIZE(FunctionSimple_cases); ++i) {
    // Verify functions not supported.
    JSONParser parser(FunctionSimple_cases[i]);
    EXPECT_THAT(parser.Parse(), StatusIs(absl::StatusCode::kOutOfRange))
        << "Input: " << FunctionSimple_cases[i];
  }
}

TEST(JSONParserTest, TestDeeplyNestedJSON) {
  googlesql::DepthLimitDetectorRuntimeControl control;
  control.max_probing_duration = absl::Seconds(1);

  for (auto const& test_case : googlesql::JSONDepthLimitDetectorTestCases()) {
    googlesql::DepthLimitDetectorTestResult result =
        googlesql::RunDepthLimitDetectorTestCase(
            test_case,
            [](absl::string_view json) {
              JSONParser parser(json);
              if (!parser.Parse().ok()) {
                return absl::ResourceExhaustedError("");
              }
              return absl::OkStatus();
            },
            control);
    EXPECT_EQ(result.depth_limit_detector_return_conditions[0].return_status,
              absl::OkStatus())
        << result;
    for (int i = 1; i < result.depth_limit_detector_return_conditions.size();
         ++i) {
      EXPECT_THAT(
          result.depth_limit_detector_return_conditions[i].return_status,
          StatusIs(absl::StatusCode::kResourceExhausted))
          << result.depth_limit_detector_return_conditions[i];
    }
  }
}

}  // namespace
