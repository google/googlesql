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

// Helpers for conversion from GoogleSQL values to JSON strings.

#ifndef GOOGLESQL_PUBLIC_FUNCTIONS_JSON_FORMAT_H_
#define GOOGLESQL_PUBLIC_FUNCTIONS_JSON_FORMAT_H_

#include <cstdint>
#include <string>

#include "googlesql/common/errors.h"
#include "googlesql/public/civil_time.h"
#include "googlesql/public/interval_value.h"
#include "googlesql/public/json_value.h"
#include "googlesql/public/numeric_value.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/value.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "googlesql/base/status_builder.h"

namespace googlesql {
namespace functions {

class JsonPrettyPrinter;

// The minimum value that can be expressed losslessly as a double precision
// floating point value.
constexpr int64_t kMinLosslessInt64ValueForJson = -9007199254740992;
// The maximum value that can be expressed losslessly as a double precision
// floating point value.
constexpr int64_t kMaxLosslessInt64ValueForJson = 9007199254740992;
// Default maximum size of strings returned from conversion to JSON, in bytes.
constexpr int32_t kDefaultMaxJsonStringSizeBytes = 1 << 20;

// Supports bool, int32, uint32, int64, uint64, float, double, NumericValue,
// and BigNumericValue
// Appends the representation of the value for JSON to 'output'.
void JsonFromNumericOrBool(bool value, std::string* output);
void JsonFromNumericOrBool(int32_t value, std::string* output);
void JsonFromNumericOrBool(uint32_t value, std::string* output);
void JsonFromNumericOrBool(int64_t value, std::string* output);
void JsonFromNumericOrBool(uint64_t value, std::string* output);
void JsonFromNumericOrBool(float value, std::string* output,
                           bool canonicalize_zero = false);
void JsonFromNumericOrBool(double value, std::string* output,
                           bool canonicalize_zero = false);
void JsonFromNumericOrBool(NumericValue value, std::string* output);
void JsonFromNumericOrBool(const BigNumericValue& value, std::string* output);

// Appends the given string in quotes to 'output'.
void JsonFromString(absl::string_view value, std::string* output);

// Base64-escapes (per RFC 4648) and quotes the given bytes value, appending it
// to 'output' if quote_output_string is true.
// Otherwise, returns the unquoted string.
void JsonFromBytes(absl::string_view value, std::string* output,
                   bool quote_output_string = true);

// Appends the given timestamp to 'output' as a quoted ISO 8601 date-time with
// a T separator and Z suffix if quote_output_string is true.
// Otherwise, returns the unquoted string.
absl::Status JsonFromTimestamp(absl::Time value, std::string* output,
                               bool quote_output_string = true);

absl::Status JsonFromTimestamp(TimestampPicosValue value, std::string* output,
                               bool quote_output_string = true);

// Appends the given datetime to 'output' as a quoted ISO 8601 date-time with
// a T separator and no timezone suffix if qua quotedt_string is true.
// Otherwise, returns the unquoted string.
absl::Status JsonFromDatetime(const DatetimeValue& value, std::string* output,
                              bool quote_output_string = true);

// Appends the given date to 'output' as a quoted ISO 8601 date with no timezone
// suffix if quote_output_string is true.
// Otherwise, returns the unquoted string.
absl::Status JsonFromDate(int32_t value, std::string* output,
                          bool quote_output_string = true);

// Appends the given time to 'output' as a quoted ISO 8601 time with no timezone
// suffix.
absl::Status JsonFromTime(const TimeValue& value, std::string* output,
                          bool quote_output_string = true);

// Appends the given interval to 'output' as a quoted ISO 8601 duration.
absl::Status JsonFromInterval(const IntervalValue& value, std::string* output);

// Appends the given JSON to 'output'.
void JsonFromJson(JSONValueConstRef value, JsonPrettyPrinter* pretty_printer,
                  std::string* output);

// Appends the JSON equivalent of the provided value to JSON. Returns an error
// and abandons appending to 'output' if it is greater than
// pretty_printer->max_json_output_size() in size.
// 'json_parsing_options' is used when the input is an unparsed JSON value.
absl::Status JsonFromValue(
    const Value& value, JsonPrettyPrinter* pretty_printer, std::string* output,
    const JSONParsingOptions& json_parsing_options = JSONParsingOptions());

// Pretty-printer for indenting and adding newlines to JSON.
class JsonPrettyPrinter {
 public:
  // If 'pretty_print' is false, then all of the functions below are no-ops with
  // respect to the provided 'output'. Otherwise the functions below control
  // indentation and can append newlines, separators, and spaces to an output
  // string.
  // 'product_mode' is used for type names in error messages.
  // The maximum output size of JSON strings converted from values defaults to
  // kDefaultMaxJsonStringSizeBytes.
  JsonPrettyPrinter(bool pretty_print, ProductMode product_mode)
      : pretty_print_(pretty_print),
        product_mode_(product_mode),
        max_json_output_size_(kDefaultMaxJsonStringSizeBytes),
        indent_(0) {}

  // Sets the maximum output size of JSON strings converted from values.
  void set_max_json_output_size_bytes(int64_t max_json_output_size) {
    max_json_output_size_ = max_json_output_size;
  }

  // Returns the maximum output size of JSON strings converted from values.
  int64_t max_json_output_size_bytes() const { return max_json_output_size_; }

  // Returns an error if the size of the given string exceeds
  // max_json_output_size_bytes().
  absl::Status CheckOutputSize(absl::string_view output) {
    if (output.size() > max_json_output_size_) {
      return MakeEvalError()
             << "Output of TO_JSON_STRING exceeds max allowed "
                "output size of "
      << max_json_output_size_ << " bytes";
    }
    return absl::OkStatus();
  }

  // Increases the indent by two spaces if pretty_print() is true.
  void IncreaseIndent() {
    if (pretty_print_) {
      indent_ += 2;
    }
  }

  // Decreases the indent by two spaces if pretty_print() is true.
  void DecreaseIndent() {
    if (pretty_print_) {
      indent_ -= 2;
    }
  }

  // Appends a newline to 'output' along with indent() spaces if pretty_print()
  // is true.
  void AppendNewlineAndIndent(std::string* output) const {
    if (pretty_print_) {
      output->push_back('\n');
      output->append(indent_, ' ');
    }
  }

  // Appends a space separator to 'output' if pretty_print() is true.
  void AppendSeparator(std::string* output) const {
    if (pretty_print_) {
      output->push_back(' ');
    }
  }

  // Returns the current indent.
  int indent() const { return indent_; }

  // Whether pretty-printing is enabled.
  bool pretty_print() const { return pretty_print_; }

  // Set whether to pretty-print. Resets the indentation level.
  void set_pretty_print(bool pretty_print) {
    pretty_print_ = pretty_print;
    indent_ = 0;
  }

  // The product mode for error messages.
  ProductMode product_mode() const { return product_mode_; }

 private:
  bool pretty_print_;
  const ProductMode product_mode_;
  int64_t max_json_output_size_;
  int indent_;
};

}  // namespace functions
}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_FUNCTIONS_JSON_FORMAT_H_
