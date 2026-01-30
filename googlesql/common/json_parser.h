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

//
// A JSON (JavaScript Object Notation) parser.
// It is a SAX parser as opposed to a DOM parser, so clients must subclass
// it and implement handlers.
//
// The JSON grammar can be found at
//   http://www.json.org/draft-crockford-jsonorg-json-04.txt

#ifndef GOOGLESQL_COMMON_JSON_PARSER_H__
#define GOOGLESQL_COMMON_JSON_PARSER_H__

#include <string>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace googlesql {

// All methods in this class return false upon failure and expect any
// failure to be unrecoverable.
class JSONParser {
 public:
  // The object underlying `json` must outlive this object.
  // `json` is assumed to be valid utf8.
  explicit JSONParser(absl::string_view json);
  JSONParser(const JSONParser&) = delete;
  JSONParser& operator=(const JSONParser&) = delete;
  virtual ~JSONParser();

  // Parse the entire json string.  The virtual functions below will
  // be called as the parser comes across the respective entities.
  absl::Status Parse();

 protected:
  // Clients implement the following virtual functions.  Since
  // the client may expect only a subset of the JS grammar, the
  // client can return false to any of these calls and Parse() will
  // return false immediately.

  // By default these return true for ease of subclassing
  // and to obtain a syntax checker by default.

  // Called when the parser sees a '{' at the beginning of a value.
  virtual bool BeginObject();
  // Called when the parser sees a '}' at the end of a value.
  virtual bool EndObject();

  // Objects consist of a series of key/value pairs or members.
  // Called with the key for the current member.
  virtual bool BeginMember(const std::string& key);
  // Called after the value for the member has been parsed. 'last'
  // will be true if this was the last member listed in the object.
  virtual bool EndMember(bool last);

  // Called when the parser sees a '[' at the beginning of a value.
  virtual bool BeginArray();
  // Called when the parser sees a ']' at the end of a value.
  virtual bool EndArray();

  // Called before parsing each element in the array.
  virtual bool BeginArrayEntry();
  // Called after parsing each element in the array.  'last' will be
  // true if this was the last element in the array.
  virtual bool EndArrayEntry(bool last);

  // The parser just parsed a string with this value.
  virtual bool ParsedString(const std::string& str);
  // The parser just parsed a number with this value.
  virtual bool ParsedNumber(absl::string_view str);
  // The parser just parsed a boolean with this value.
  virtual bool ParsedBool(bool val);
  // The parser just parsed a null value.
  virtual bool ParsedNull();

  // Report the type and position of a failure.
  // Returns false for convenience.
  virtual bool ReportFailure(absl::string_view error_message);
  virtual bool ReportFailure(absl::string_view error_message,
                             absl::StatusCode error_code);

  // Returns a substring with context at the current reader position.
  // Useful for error messages.
  std::string ContextAtCurrentPosition(int context_length) const;

  // The default context length to use for ContextAtCurrentPosition().
  static constexpr int kDefaultContextLength = 10;

 private:
  enum TokenType {
    BEGIN_STRING,         // " or '
    BEGIN_NUMBER,         // - or digit
    BEGIN_TRUE,           // true
    BEGIN_FALSE,          // false
    BEGIN_NULL,           // null
    BEGIN_OBJECT,         // {
    END_OBJECT,           // }
    BEGIN_ARRAY,          // [
    END_ARRAY,            // ]
    VALUE_SEPARATOR,      // ,
    BEGIN_KEY,            // letter, _, $ or digit.  Must begin with non-digit
    UNKNOWN
  };

  // Handles any type
  absl::Status ParseValue();

  // Expects p_ to point to the beginning of a string.
  bool ParseString();

  // Expects p_ to point to the beginning of a string.  Upon success,
  // *str will be the string-unescaped string.
  bool ParseStringHelper(std::string* str);

  // Expects p_ to point to a json number.
  bool ParseNumber();

  // If str is not null then it will contain the string of the number on return.
  bool ParseNumberTextHelper(absl::string_view* str);

  // Expects p_ to point to a BEGIN_OBJECT character.  For each member
  // in the object, recursively parses the value.
  bool ParseObject();

  // Expects p_ to point to a BEGIN_ARRAY character.  For each element
  // in the array, recursively parses the value.
  bool ParseArray();

  // Expects p_ to point to an unquoted literal
  bool ParseTrue();
  bool ParseFalse();
  bool ParseNull();

  // Advance p_ past all whitespace or until the end of the string.
  void SkipWhitespace();

  // Advance one UTF-8 character
  void AdvanceOneCodepoint();

  // Advance a byte, an optimization used for advancing past
  // single-byte (ascii) tokens.
  void AdvanceOneByte();

  // Return the type of the next token.  Advance p_ to that token.
  TokenType GetNextTokenType();          // Value context

  // These two functions parse escape sequences in strings.  Collectively they
  // parse \uhhhh, \xhh, and \ooo where h is a hex number and o is an octal
  // number. It returns false when there's a parsing error, either the size is
  // not the expected size or a character is not a hex digit.  When it returns
  // str will contain what has been successfully parsed so far.
  bool ParseHexDigits(int size, std::string* str);
  void ParseOctalDigits(int size, std::string* str);

  // Returns true if c contains an Octal digit, false otherwise.
  static bool IsOctalDigit(char c);

  // The status of the parser. If a parsing failure occurs, this should be
  // updated to an error status and message describing the error.
  absl::Status status_ = absl::OkStatus();

  // The json string encoded in UTF8.
  absl::string_view json_;

  // A pointer into json_ to keep track of the current parsing location.
  absl::string_view p_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_COMMON_JSON_PARSER_H__
