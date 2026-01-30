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

// See (broken link) for details on these functions.

#ifndef GOOGLESQL_PUBLIC_FUNCTIONS_JSON_H_
#define GOOGLESQL_PUBLIC_FUNCTIONS_JSON_H_

#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/functions/json_internal.h"
#include "googlesql/public/json_value.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/value.h"
#include "googlesql/base/string_numbers.h"  
#include "absl/base/attributes.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace googlesql {
namespace functions {

namespace json_internal {

class ValidJSONPathIterator;

}  // namespace json_internal

// Utility class for Json extraction. Optimized for reuse of a constant
// json_path, whereas the above functions normalize json_path on every call.
class JsonPathEvaluator {
 public:
  ~JsonPathEvaluator();

  // Creates a new evaluator for a constant `json_path`. `json_path` does not
  // need to persist beyond the call to `Create()`.
  //
  // Parameters:
  // `sql_standard_mode`: The JSON Path is interpreted differently with this
  // flag. For full details please see (broken link)
  // 1) True: Only number subscripts are allowed. It allows the . child
  // operator as a quoted field name. The previous example could be rewritten
  // as $.a."b"
  // 2) False: String subscripts are allowed. For example: $.a["b"] is a valid
  // JSON path in this mode and equivalent to $.a.b
  // New callers to this API should be using `sql_standard_mode` = true.
  //
  // `enable_special_character_escaping_in_values`: If set to true, escapes
  // JSON values.
  //
  // `enable_special_character_escaping_in_keys`: If set to true, and
  // `enable_special_character_escaping_in_values` is set to true, escapes
  // JSON keys. This option was introduced as a bugfix for b/265948860.
  //
  // Escaping special characters is part of the behavior detailed in the
  // ISO/IEC TR 19075-6 report on SQL support for JavaScript Object Notation.
  //
  // This implementation follows the proto3 JSON spec ((broken link),
  // (broken link)), where special characters include the
  // following:
  //    * Quotation mark, '"'
  //    * Reverse solidus, '\'
  //    * Control characters (U+0000 through U+001F).
  //
  // Error cases:
  // * OUT_OF_RANGE - `json_path` is malformed.
  // * OUT_OF_RANGE - `json_path` uses a (currently) unsupported expression
  // type.
  static absl::StatusOr<std::unique_ptr<JsonPathEvaluator>> Create(
      absl::string_view json_path, bool sql_standard_mode,
      bool enable_special_character_escaping_in_values,
      bool enable_special_character_escaping_in_keys);

  // Extracts a value from `json` according to the JSONPath string json_path
  // provided in Create().
  // For example:
  //   json: {"a": {"b": [ { "c" : "foo" } ] } }
  //   json_path: $
  //   value -> {"a":{"b":[{"c":"foo"}]}}
  //
  //   json: {"a": {"b": [ { "c" : "foo" } ] } }
  //   json_path: $.a.b[0]
  //   value -> {"c":"foo"}
  //
  //   json: {"a": {"b": ["c" : "foo"]  } }
  //   json_path: $.a.b[0].c
  //   value -> "foo" (i.e. quoted string)
  //
  //  Since JsonPathEvaluator is only created on valid JSON paths this
  //  will always return OK.
  //
  // Null cases:
  // * `json` is malformed and fails to parse.
  // * json_path does not match anything.
  // * json_path uses an array index but the value is not an array, or the path
  //   uses a name but the value is not an object.
  //
  // When `issue_warning` is passed in, if there's a potential incorrect
  // behavior that does not cause an error, the callback will be triggered.
  absl::Status Extract(absl::string_view json, std::string* value,
                       bool* is_null,
                       std::optional<std::function<void(absl::Status)>>
                           issue_warning = std::nullopt) const;

  // Similar to the string version above, but for JSON types.
  // Returns std::nullopt to indicate that:
  // * json_path does not match anything.
  // * json_path uses an array index but the value is not an array, or the path
  //   uses a name but the value is not an object.
  std::optional<JSONValueConstRef> Extract(JSONValueConstRef input) const;

  // Similar to the above, but the 'json_path' provided in Create() must refer
  // to a scalar value in 'json'.
  // For example:
  //   json: {"a": {"b": ["c" : "foo"] } }
  //   json_path: $.a.b[0].c
  //   value -> foo (i.e. unquoted string)
  //
  //   json: {"a": 3.14159 }
  //   json_path: $.a
  //   value -> 3.14159 (i.e. unquoted string)
  //
  // Error cases are the same as in JsonExtract.
  // Null cases are the same as in JsonExtract, except for the addition of:
  // * json_path does not correspond to a scalar value in json.
  absl::Status ExtractScalar(absl::string_view json, std::string* value,
                             bool* is_null) const;

  // Similar to the string version above, but for JSON types.
  // Returns std::nullopt to indicate that:
  // * json_path does not match anything.
  // * json_path uses an array index but the value is not an array, or the path
  //   uses a name but the value is not an object.
  // * json_path does not correspond to a scalar value in json.
  std::optional<std::string> ExtractScalar(JSONValueConstRef input) const;

  // Extracts an array from `json` according to the JSONPath string json_path
  // provided in Create(). The value in `json` that json_path refers to should
  // be a JSON array. Then the output of the function will be in the form of an
  // ARRAY.
  // For example:
  //   json: ["foo","bar","baz"]
  //   json_path: $
  //   value -> ["\"foo\"", "\"bar\"", "\"baz\""] (ARRAY, quotes kept)
  //
  //   json: [1,2,3]
  //   value -> [1, 2, 3] (ARRAY, JSONPath is $ by default if not provided)
  //
  //   json: [1, null, "foo"]
  //   value -> [1, null, "\"foo\""]
  //
  //   json: {"a":[{"b":"foo","c":1},{"b":"bar","c":2}],"d":"baz"}
  //   json_path: $.a
  //   value -> ["{\"b\":\"foo\",\"c\":1}","{\"b\":\"bar\",\"c\":2}"]
  //       (ARRAY, objects in the form of strings)
  //
  // Error cases are the same as in Extract function.
  // Null cases are the same as in Extract, except for the addition of:
  // * json_path does not correspond to an array in json.
  //
  // When `issue_warning` is passed in, if there's a potential incorrect
  // behavior that does not cause an error, the callback will be triggered.
  absl::Status ExtractArray(absl::string_view json,
                            std::vector<std::string>* value, bool* is_null,
                            std::optional<std::function<void(absl::Status)>>
                                issue_warning = std::nullopt) const;

  // Similar to the string version above, but for JSON types.
  // Returns std::nullopt to indicate that:
  // * json_path does not match anything.
  // * json_path uses an array index but the value is not an array, or the path
  //   uses a name but the value is not an object.
  // * json_path does not correspond to an array value in json.
  std::optional<std::vector<JSONValueConstRef>> ExtractArray(
      JSONValueConstRef input) const;

  // Similar to ExtractArray(), except requires 'json' to be an array of scalar
  // value, and the strings in the array will be returned without quotes or
  // escaping. An std::nullopt in 'value' represents a SQL NULL.
  //
  // Example:
  //   json: ["foo","bar","baz"]
  //   json_path: $
  //   value -> ["foo", "bar", "baz"] (ARRAY, unquoted strings)
  //
  //   json: [1,2,3]
  //   value -> [1, 2, 3] (ARRAY, JSONPath is $ by default if not provided)
  //
  //   json: [1, null, "foo", "null"]
  //   value -> [1, NULL, "foo", "null"]
  //
  // Error cases are the same as in ExtractArray function.
  // Null cases are the same as in ExtractArray, except for the addition of:
  // * json_path does not correspond to an array of scalar objects in json.
  absl::Status ExtractStringArray(
      absl::string_view json, std::vector<std::optional<std::string>>* value,
      bool* is_null) const;

  // Similar to the string version above, but for JSON types.
  // Returns std::nullopt to indicate that:
  // * json_path does not match anything.
  // * json_path uses an array index but the value is not an array, or the path
  //   uses a name but the value is not an object.
  // * json_path does not correspond to an array of scalars in json.
  std::optional<std::vector<std::optional<std::string>>> ExtractStringArray(
      JSONValueConstRef input) const;

  ABSL_DEPRECATED("Set escaping in JsonPathEvaluator::Create() instead.")
  void enable_special_character_escaping() {
    enable_special_character_escaping_in_values_ = true;
  }

  // Sets the callback to be invoked when a string with special characters was
  // returned for JSON_EXTRACT, but special character escaping was turned off.
  // No callback will be made if this is set to an empty target.
  void set_escaping_needed_callback(
      std::function<void(absl::string_view, bool)> callback) {
    escaping_needed_callback_ = std::move(callback);
  }

 private:
  JsonPathEvaluator(std::unique_ptr<json_internal::ValidJSONPathIterator> itr,
                    bool enable_special_character_escaping_in_values,
                    bool enable_special_character_escaping_in_keys);
  const std::unique_ptr<json_internal::ValidJSONPathIterator> path_iterator_;
  bool enable_special_character_escaping_in_values_ = false;
  bool enable_special_character_escaping_in_keys_ = false;
  std::function<void(absl::string_view, bool)> escaping_needed_callback_;
};

// Converts a JSONPath token (unquoted and unescaped) into a SQL standard
// JSONPath token (used by JSON_QUERY and JSON_VALUE).
// Examples:
// foo is converted to foo
// a.b is converted to "a.b"
// te"st' is converted to "te\"st'"
std::string ConvertJSONPathTokenToSqlStandardMode(
    absl::string_view json_path_token);

// Converts a non SQL standard JSONPath (JSONPaths used by
// JSON_EXTRACT for example) into a SQL standard JSONPath (used by JSON_QUERY
// for example).
// Examples:
// $['a.b'] is converted to $."a.b"
// $['an "array" field'][3] to $."an \"array\" field".3
//
// See (broken link) for more info.
absl::StatusOr<std::string> ConvertJSONPathToSqlStandardMode(
    absl::string_view json_path);

// Merges JSONPaths into a SQL standard JSONPath. Each JSONPath input can be in
// either SQL standard mode.
absl::StatusOr<std::string> MergeJSONPathsIntoSqlStandardMode(
    absl::Span<const std::string> json_paths);

// Converts 'input' into a INT64.
// Returns an error if:
// - 'input' does not contain a number.
// - 'input' is not within the INT64 value domain (meaning the number has a
//   fractional part or is not within the INT64 range).
absl::StatusOr<int64_t> ConvertJsonToInt64(JSONValueConstRef input);

// Converts 'input' into a INT32.
// Returns an error if:
// - 'input' does not contain a number.
// - 'input' is not within the INT32 value domain (meaning the number has a
//   fractional part or is not within the INT32 range).
absl::StatusOr<int32_t> ConvertJsonToInt32(JSONValueConstRef input);

// Converts 'input' into a UINT64.
// Returns an error if:
// - 'input' does not contain a number.
// - 'input' is not within the UINT64 value domain (meaning the number has a
//   fractional part, is negative or is not within the UINT64 range).
absl::StatusOr<uint64_t> ConvertJsonToUint64(JSONValueConstRef input);

// Converts 'input' into a UINT32.
// Returns an error if:
// - 'input' does not contain a number.
// - 'input' is not within the UINT32 value domain (meaning the number has a
//   fractional part, is negative or is not within the UINT32 range).
absl::StatusOr<uint32_t> ConvertJsonToUint32(JSONValueConstRef input);

// Converts 'input' into a BOOL.
// Returns an error if:
// - 'input' does not contain a boolean.
absl::StatusOr<bool> ConvertJsonToBool(JSONValueConstRef input);

// Converts 'input' into a STRING.
// Returns an error if:
// - 'input' does not contain a string.
absl::StatusOr<std::string> ConvertJsonToString(JSONValueConstRef input);

// Mode to determine how to handle numbers that cannot be round-tripped.
enum class WideNumberMode { kRound, kExact };

// Converts 'input' into a Double.
// 'mode': defines what happens with a number that cannot be converted to double
// without loss of precision:
// - 'exact': function fails if result cannot be round-tripped through double.
// - 'round': the numeric value stored in JSON will be rounded to DOUBLE.
absl::StatusOr<double> ConvertJsonToDouble(JSONValueConstRef input,
                                           WideNumberMode mode,
                                           ProductMode product_mode);

// Converts 'input' into a Float.
// 'mode': defines what happens with a number that cannot be converted to float
// without loss of precision:
// - 'exact': function fails if result cannot be round-tripped through float.
// - 'round': the numeric value stored in JSON will be rounded to FLOAT.
absl::StatusOr<float> ConvertJsonToFloat(JSONValueConstRef input,
                                         WideNumberMode mode,
                                         ProductMode product_mode);

// Converts 'input' into a ARRAY<INT64>.
// Returns an error if:
// - 'input' is not a JSON array.
// - `ConvertJsonToInt64` fails for any element of 'input'.
absl::StatusOr<std::vector<int64_t>> ConvertJsonToInt64Array(
    JSONValueConstRef input);

// Converts 'input' into a ARRAY<INT32>.
// Returns an error if:
// - 'input' is not a JSON array.
// - `ConvertJsonToInt32` fails for any element of 'input'.
absl::StatusOr<std::vector<int32_t>> ConvertJsonToInt32Array(
    JSONValueConstRef input);

// Converts 'input' into a ARRAY<UINT64>.
// Returns an error if:
// - 'input' is not a JSON array.
// - `ConvertJsonToUint64` fails for any element of 'input'.
absl::StatusOr<std::vector<uint64_t>> ConvertJsonToUint64Array(
    JSONValueConstRef input);

// Converts 'input' into a ARRAY<UINT32>.
// Returns an error if:
// - 'input' is not a JSON array.
// - `ConvertJsonToUint32` fails for any element of 'input'.
absl::StatusOr<std::vector<uint32_t>> ConvertJsonToUint32Array(
    JSONValueConstRef input);

// Converts 'input' into a ARRAY<BOOL>.
// Returns an error if:
// - 'input' is not a JSON array.
// - `ConvertJsonToBool` fails for any element of 'input'.
absl::StatusOr<std::vector<bool>> ConvertJsonToBoolArray(
    JSONValueConstRef input);

// Converts 'input' into a ARRAY<STRING>.
// Returns an error if:
// - 'input' is not a JSON array.
// - `ConvertJsonToString` fails for any element of 'input'.
absl::StatusOr<std::vector<std::string>> ConvertJsonToStringArray(
    JSONValueConstRef input);

// Converts 'input' into a ARRAY<DOUBLE>.
// Returns an error if:
// - 'input' is not a JSON array.
// - `ConvertJsonToDouble` fails for any element of 'input'.
absl::StatusOr<std::vector<double>> ConvertJsonToDoubleArray(
    JSONValueConstRef input, WideNumberMode mode, ProductMode product_mode);

// Converts 'input' into a ARRAY<FLOAT>.
// Returns an error if:
// - 'input' is not a JSON array.
// - `ConvertJsonToFloat` fails for any element of 'input'.
absl::StatusOr<std::vector<float>> ConvertJsonToFloatArray(
    JSONValueConstRef input, WideNumberMode mode, ProductMode product_mode);

// Returns the type of the outermost JSON value as a text string.
absl::StatusOr<std::string> GetJsonType(JSONValueConstRef input);

// Converts a json 'input' into a Boolean.
// Upon success the function returns the converted value, else returns nullopt.
// Returns non-ok status if there's an internal error during execution. For more
// details on the conversion rules
// see (broken link).
absl::StatusOr<std::optional<bool>> LaxConvertJsonToBool(
    JSONValueConstRef input);

// Similar to the above function except converts json 'input' into INT64.
// Floating point numbers are rounded when converted to INT64.
absl::StatusOr<std::optional<int64_t>> LaxConvertJsonToInt64(
    JSONValueConstRef input);

// Similar to the above function except converts json 'input' into INT32.
// Floating point numbers are rounded when converted to INT32.
// Integers outside INT32 range yield nullopt.
absl::StatusOr<std::optional<int32_t>> LaxConvertJsonToInt32(
    JSONValueConstRef input);

// Similar to the above function except converts json 'input' into UINT64.
// Floating point numbers are rounded when converted to UINT64.
// Integers outside UINT64 range yield nullopt.
absl::StatusOr<std::optional<uint64_t>> LaxConvertJsonToUint64(
    JSONValueConstRef input);

// Similar to the above function except converts json 'input' into UINT32.
// Floating point numbers are rounded when converted to UINT32.
// Integers outside UINT32 range yield nullopt.
absl::StatusOr<std::optional<uint32_t>> LaxConvertJsonToUint32(
    JSONValueConstRef input);

// Similar to the above function except converts json 'input' into DOUBLE.
absl::StatusOr<std::optional<double>> LaxConvertJsonToFloat64(
    JSONValueConstRef input);

// Similar to the above function except converts json 'input' into FLOAT.
absl::StatusOr<std::optional<float>> LaxConvertJsonToFloat32(
    JSONValueConstRef input);

// Similar to the above function except converts json 'input' into STRING.
absl::StatusOr<std::optional<std::string>> LaxConvertJsonToString(
    JSONValueConstRef input);

// Converts a JSON 'input' into a ARRAY<BOOL>.
// Upon success the function returns the converted value. If the value is not
// an array, returns nullopt. If an element of the input array fails lax
// conversion, that element will be set to nullopt in the resulting vector.
// Returns non-ok status if there's an internal error during execution.
// For more details on the conversion rules
// see (broken link).
absl::StatusOr<std::optional<std::vector<std::optional<bool>>>>
LaxConvertJsonToBoolArray(JSONValueConstRef input);

// Similar to the above function except converts JSON 'input' into INT64 array.
// Floating point numbers are rounded when converted to INT64.
absl::StatusOr<std::optional<std::vector<std::optional<int64_t>>>>
LaxConvertJsonToInt64Array(JSONValueConstRef input);

// Similar to the above function except converts JSON 'input' into INT32 array.
// Floating point numbers are rounded when converted to INT32.
absl::StatusOr<std::optional<std::vector<std::optional<int32_t>>>>
LaxConvertJsonToInt32Array(JSONValueConstRef input);

// Similar to the above function except converts JSON 'input' into UINT64 array.
// Floating point numbers are rounded when converted to UINT64.
absl::StatusOr<std::optional<std::vector<std::optional<uint64_t>>>>
LaxConvertJsonToUint64Array(JSONValueConstRef input);

// Similar to the above function except converts JSON 'input' into UINT32 array.
// Floating point numbers are rounded when converted to UINT32.
absl::StatusOr<std::optional<std::vector<std::optional<uint32_t>>>>
LaxConvertJsonToUint32Array(JSONValueConstRef input);

// Similar to the above function except converts JSON 'input' into FLOAT32
// array.
absl::StatusOr<std::optional<std::vector<std::optional<double>>>>
LaxConvertJsonToFloat64Array(JSONValueConstRef input);

// Similar to the above function except converts JSON 'input' into FLOAT32
// array.
absl::StatusOr<std::optional<std::vector<std::optional<float>>>>
LaxConvertJsonToFloat32Array(JSONValueConstRef input);

// Similar to the above function except converts JSON 'input' into STRING array.
absl::StatusOr<std::optional<std::vector<std::optional<std::string>>>>
LaxConvertJsonToStringArray(JSONValueConstRef input);

// Converts a variadic number of arguments into a JSON array of these arguments.
// If 'canonicalize_zero' is true, the sign on a signed zero is removed when
// converting a numeric type to JSON.
// TODO : remove canonicalize_zero flag when all
// engines have rolled out this new behavior.
absl::StatusOr<JSONValue> JsonArray(absl::Span<const Value> args,
                                    const LanguageOptions& language_options,
                                    bool canonicalize_zero);

// Builder of JSON objects (used by JSON_OBJECT function implementations).
// Duplicate keys are ignored (first value is kept).
class JsonObjectBuilder {
 public:
  // If 'canonicalize_zero' is true, the sign on a signed zero is removed when
  // converting a numeric type to JSON.
  //
  // TODO : remove canonicalize_zero flag when all
  // engines have rolled out this new behavior.
  JsonObjectBuilder(const LanguageOptions options, bool canonicalize_zero)
      : options_(options), canonicalize_zero_(canonicalize_zero) {
    Reset();
  }
  ~JsonObjectBuilder() = default;

  // Not copyable or movable
  JsonObjectBuilder(const JsonObjectBuilder&) = delete;
  JsonObjectBuilder& operator=(const JsonObjectBuilder&) = delete;

  // Resets the builder into the initial state (empty JSON object, empty
  // encountered keys list).
  void Reset();

  // Add the key/value pair to the JSON object.
  // Duplicate keys are ignored (first inserted value is kept). This is
  // consistent with our JSON specs.
  // See discussion in (broken link).
  //
  // Returns whether the pair was inserted.
  // When an error is returned, it is the caller's responsibility to Reset() the
  // builder.
  absl::StatusOr<bool> Add(absl::string_view key, const Value& value);

  // Returns the JSON object. Resets the internal state. The instance can be
  // used again.
  ABSL_MUST_USE_RESULT JSONValue Build();

 private:
  const LanguageOptions options_;
  const bool canonicalize_zero_;

  absl::flat_hash_set<absl::string_view> keys_set_;
  JSONValue result_;
};

// Converts a list of key/values pairs into a JSON object using 'builder'.
// 'builder' can be re-used for subsequent calls to JsonObject().
// Duplicate keys are discarded (first value is kept).
// Returns an error if 'keys' and 'values' don't have the same length.
absl::StatusOr<JSONValue> JsonObject(absl::Span<const absl::string_view> keys,
                                     absl::Span<const Value*> values,
                                     JsonObjectBuilder& builder);

// Removes the object member or the array element pointed to by `path_iterator`
// and returns true. If `path_iterator` is an nonexistent path, the function
// does nothing and returns false.
//
// Returns an error if the `path_iterator` is '$'.
absl::StatusOr<bool> JsonRemove(
    JSONValueRef input, json_internal::StrictJSONPathIterator& path_iterator);

// Insert `value` into `input` at location pointed to by `path_iterator`.
// `path_iterator` must point to an array index. If the array index is larger
// than the size of the array, the array is expanded and filled with JSON nulls,
// and `value` is inserted at the correct `index`.
//
// If the final array pointed to by `path_iterator` is a JSON null, then an
// array is created, expanded with JSON nulls, and `value` is inserted at the
// correct index. The array is created even if no element is inserted. Example:
//
// JsonInsertArrayElement(JSON 'null', Iter('$[2]'), [], ...,
//                        /*insert_each_element=*/true)
// -> JSON '[null, null, null]'
//
// If `path_iterator` doesn`t point to an array index, or if the path doesn`t
// exist, the function does nothing.
//
// If `insert_each_element` is true and `value` is an array, then each element
// of the array is inserted in the same order as their position in the `value`
// array. If false, then `value` is converted to a JSON array and inserted.
//
// Examples:
// - JsonInsertArrayElement(JSON '[1, "foo"]', Iter('$[1]'), [10, 20], ...,
//                          true)
//   -> JSON '[1, 10, 20, "foo"]'
// - JsonInsertArrayElement(JSON '[1, "foo"]', Iter('$[1]'), [10, 20], ...,
//                          false)
//   -> JSON '[1, [10, 20], "foo"]'
//
// Returns an error if the conversion of `value` to a JSON value fails.
//
// `input` is not modified if the insertion fails for any reason.
//
// If `canonicalize_zero` is true, the sign on a signed zero is removed when
// converting a numeric type to JSON.
// TODO : remove canonicalize_zero flag when all
// engines have rolled out this new behavior.
absl::Status JsonInsertArrayElement(
    JSONValueRef input, json_internal::StrictJSONPathIterator& path_iterator,
    const Value& value, const LanguageOptions& language_options,
    bool canonicalize_zero, bool insert_each_element = true);

// Appends `value` into the array in `input` pointed to by `path_iterator`.
// `path_iterator` must point to an array or JSON null.
//
// If `path_iterator` points to a JSON null, an array is created and `value` is
// added as first element(s). The array is created even if no element is
// appended. Example:
//
// JsonAppendArrayElement(JSON 'null', Iter('$'), [], ...,
//                        /*append_each_element=*/true)
// -> JSON '[]'
//
// If `path_iterator` doesn't point to an array or JSON null, or if the path
// doesn't exist, the function does nothing.
//
// If `append_each_element` is true and `value` is an array, then each element
// of the array is appended at in the same order as their position in the
// `value` array. If false, then `value` is converted to a JSON array and
// appended. Examples:
// - JsonAppendArrayElement(JSON '[1, "foo"]', Iter('$'), [10, 20], ..., true)
//   -> JSON '[1, "foo", 10, 20]'
// - JsonAppendArrayElement(JSON '[1, "foo"]', Iter('$'), [10, 20], ..., false)
//   -> JSON '[1, "foo", [10, 20]]'
//
// Returns an error if the conversion of `value` to a JSON value fails.
//
// `input` is not modified if the insertion fails for any reason.
//
// If `canonicalize_zero` is true, the sign on a signed zero is removed when
// converting a numeric type to JSON.
// TODO : remove canonicalize_zero flag when all
// engines have rolled out this new behavior.
absl::Status JsonAppendArrayElement(
    JSONValueRef input, json_internal::StrictJSONPathIterator& path_iterator,
    const Value& value, const LanguageOptions& language_options,
    bool canonicalize_zero, bool append_each_element = true);

// Replaces data in `input` pointed to by `path_iterator` with `value`. If
// `create_if_missing` is set to true and the path does not exist or points
// to a JSON 'null' in the `input` it is recursively created. If
// `create_if_missing` is set to false, set operations for non-existent paths
// are ignored.
//
// If the set operation is invalid, the operation is ignored, and the function
// does nothing. An operation is invalid if there is a type mismatch between
// tokens in path and `input`. For example:
// JsonSet(JSON '{"a": [1]}', "$.a.b", 2, ...)
// The expected type of subpath "$.a" is an object but JSON token at subpath is
// an array.
//
// In all the below examples the value of `create_if_missing` is set to true.
// Example 1: JsonSet(JSON '{"a": {}}', "$.a.b.c", 2, ...)
// Result: JSON '{"a": {"b": {"c": 2}}}'
// Reasoning: Suffix ".b.c" doesn't exist so it is created.
//
// Example 2:
// JsonSet(JSON '{"a": []}', "$.a[2].b", 2, ...)
// Result: JSON '{"a": [null, null, {"b": 2}]}'
// Reasoning: Suffix "[2].b" doesn't exist so it is created. Array is expanded
// and filled with nulls.
//
// Example 3:
// JsonSet(JSON '{"a": null}', "$.a.b", 2, ...)
// Result: JSON '{"a":{"b":2}}'
// Reasoning: Prefix "$.a" points to JSON 'null'. Recursively creates suffix
// ".b".
//
// Example 4:
// JsonSet(JSON '{"a": null}', "$.a[2]", 3, ...)
// Result: JSON '{"a": [null, null, 3]}'
// Reasoning: Prefix "$.a" points to a JSON 'null'. Recursively creates suffix
// "[2]".
//
// See (broken link) for additional examples.
//
// Returns an error if conversion of `value` to a JSON value fails.
//
// `input` is not modified if the mutation fails for any reason.
//
// If `canonicalize_zero` is true, the sign on a signed zero is removed when
// converting a numeric type to JSON.
// TODO : remove canonicalize_zero flag when all
// engines have rolled out this new behavior.
absl::Status JsonSet(JSONValueRef input,
                     json_internal::StrictJSONPathIterator& path_iterator,
                     const Value& value, bool create_if_missing,
                     const LanguageOptions& language_options,
                     bool canonicalize_zero);

// Cleans up `input` by removing JSON 'null' and optionally empty containers
// from the JSON subtree pointed to by `path_iterator`.  If `path_iterator`
// points to a nonexistent path does nothing.
//
// Parameters:
// - `include_arrays`: If set to true, removes JSON 'null' from both ARRAYs.
//    and OBJECTS. Else, only removes JSON 'null' from OBJECTS.
// - `remove_empty`: If set to true, removes empty containers, else ignores.
//
// Parameter value combinations and behaviors:
// - Parameter Values: `include_arrays` = false, `remove_empty` = false
//   Behavior: Removes nulls from OBJECTs. Empty containers remain.
//
// - Parameter Values: `include_arrays` = true, `remove_empty` = false
//   Behavior: Removes nulls from OBJECTs and ARRAYs. Empty containers remain.
//
// - Parameter Values: `include_arrays` = false, `remove_empty` = true
//   Behavior: Removes nulls from OBJECTs. Recursively remove empty OBJECTS if
//   the parent is OBJECT. If the parent is an ARRAY the empty object remains.
//
// - Parameter Values: `include_arrays` = true, `remove_empty` = true
//   Behavior: Removes nulls from OBJECTs and ARRAYs.  Recursively removes empty
//   OBJECTS and ARRAYs.
//
// Examples
// JSON json_doc = JSON '{"a":null, "b":1, "c":[null, null], "d":{"e":null}}'
//
// Example 1:
// JsonStripNulls(json_doc, "$", false, false);
// Result: JSON '{"b":1, "c":[null, null], "d":{}}'
// Reasoning: Removes all {key,value} pairs that have JSON 'null' value from
// OBJECTS. ARRAYs and empty containers are ignored.
//
// Example 2:
// JsonStripNulls(json_doc, "$", true, false);
// Result: JSON '{"b":1, "c":[], "d":{}}'
// Reasoning: Removes all {key,value} pairs that have JSON 'null' value from
// OBJECTS and ARRAYs. Empty containers are ignored.
//
// Example 3:
// JsonStripNulls(json_doc, "$", false, true);
// Result: JSON '{"b":1, "c":[null, null]}'
// Reasoning: Removes all {key,value} pairs that have JSON 'null' value from
// OBJECTS and removes empty OBJECT structures. ARRAYS are ignored.
//
// Example 4:
// JsonStripNulls(json_doc, "$", true, true);
// Result: JSON '{"b":1}'
// Reasoning: Removes all {key,value} pairs that have JSON 'null' values from
// both OBJECTS and ARRAYS and removes all empty containers.
//
// JSON json_doc = JSON '[null, {"a":null}, [null]]'
//
// Example 5:
// JsonStripNulls(json_doc, "$", false, true);
// Result: JSON '[null, null, [null]]'
// Reasoning: Removes '{"a":null}' and replaces with NULL. Empty OBJECT is not
// removed and instead replaced by NULL because parent is ARRAY.
//
// Example 6:
// JsonStripNulls(json_doc, "$", true, true);
// Result: JSON 'null'
// Reasoning: After NULLs are removed only empty containers remain and removed.
//
// See (broken link) for additional examples.
//
// Returns non-ok status if there's an internal error during execution.
absl::Status JsonStripNulls(
    JSONValueRef input, json_internal::StrictJSONPathIterator& path_iterator,
    bool include_arrays, bool remove_empty);

// Extracts values from `input` matched by JSONPath `path_iterator`. This
// version of JSON_QUERY implementation extracts data using lax semantics. Lax
// semantics implicitly adapts matching JSONPath to the structure of the
// `input`.
//
// If the `path_iterator`.GetJsonPathOptions().recursive is set to true: A path
// token expects an object(.member) and the json_doc element is an array, each
// element in the array is recursively unwrapped until a non-array type is
// encountered prior to matching member.
// If set to false: Only a single level array is unwrapped prior to matching
// member.
//
// Matched results are wrapped in a JSON array. If `path_iterator` matches no
// data an empty JSON array is returned.
//
// Example 1:
// JsonQueryLax(JSON '[{"a":1}, [{"a":[2], {"b":3}, 4]]', '$.a')
// `path_iterator`.GetJsonPathOptions().recursive = true
// Result: JSON '[1, [2]]'
// Reasoning: Because `recursive` is set to true, we recursively unwrap before
// matching token `.a`.
//
// Example 2:
// JsonQueryLax(JSON '[{"a":1}, [{"a":[2], {"b":3}, 4]]', '$.a')
// `path_iterator`.GetJsonPathOptions().recursive = false
// Result: JSON '[1]'
// Reasoning: Because `recursive` is set to false, token `.a` does not match
// json_doc element '[{"a":[2], {"b":3}, 4]]'.
//
// Example 3:
// JsonQueryLax(JSON '[[{"a":[]}]]', '$.a')
// Result: JSON '[[]]'
// `path_iterator`.GetJsonPathOptions().recursive = true
// Reasoning: Because `recursive` is set to true, we recursively unwrap before
// matching token `.a`.
//
// Example 4:
// JsonQueryLax(JSON '[[{"a":[]}]]', '$.a')
// `path_iterator`.GetJsonPathOptions().recursive = false
// Result: JSON '[]'
// Reasoning: Because `recursive` is set to false, no data is matched.
//
// Example 5:
// JSON_QUERY_LAX(JSON '1', '$[0]')
// `path_iterator`.GetJsonPathOptions().recursive = {false or true}
// Result: JSON '[1]'
// Reasoning: Path '[0]' forces autowrap of json doc element '1' -> '[1]' before
// matching the token '[0]. `recursive` has no effect on autowrap of arrays.
//
// Example 6:
// JSON_QUERY_LAX(JSON '1', '$[1]')
// `path_iterator`.GetJsonPathOptions().recursive = {false or true}
// Result: JSON '[]' (No matching results)
// Reasoning: Path '[1]' forces autowrap of json doc element '1' -> '[1]' before
// matching the token '[1]'. However, index 1 is larger than the array size.
// `recursive` has no effect on autowrap of arrays.
//
// Invariant: path_iterator->JsonPathOptions->lax must be 'true', otherwise the
// function will return an error. This is because non-lax JSON_QUERY does not
// follow StrictPath semantics.
absl::StatusOr<JSONValue> JsonQueryLax(
    JSONValueConstRef input,
    json_internal::StrictJSONPathIterator& path_iterator);

// Returns true if a given JSON document `target` is contained within a JSON
// document `input`. Otherwise, returns false.
//
// Example 1:
// JsonContains(JSON '{"a":1,"b":2,"c":null}', JSON '{"c":null,"a":1.00}');
// Result: True
// Reasoning: All keys and values are contained. The ordering of keys and
// format of numeric values are not important. Also, JSON null is contained with
// JSON null.
//
// Example 2:
// JsonContains(JSON '{"a":1,"b":2,"c":3}', JSON '{"c":3.0,"d":1}');
// Result: False
// Reasoning: Only partial keys and values are matched.
//
// Example 3:
// JsonContains(JSON '[1,2,[3,4,[5]],[6,7]]', JSON '[2,[6],[[5]],2]');
// Result: True
// Reasoning: Duplicated values in arrays are not important. The nested levels
// of these array elements are.
//
// Example 4:
// JsonContains(JSON '[1,2,[3,4,[5]],[6,7]]', JSON '2.0');
// Result: True
// Reasoning: Top level json array can contains a scalar value directly.
//
// Example 5:
// JsonContains(JSON '[{"a":1},[{"a":[2]}, 3]]', JSON '[[{"a":2}]]')
// Result: False
// Reasoning: Because the scalar value 2 is not matched with json array type.
// Switching the tareget as JSON '[[{"a":[2]}]]' will return true.
//
// Example 6:
// JsonContains(JSON '[{"a":1}, {"a":2,"b":3},4]]', JSON '[{"a":1,"b":3}]')
// Result: False
// Reasoning: Because the json object in the array needs to be matched together.
// Switching the tareget as JSON '[{"a":1},{"b":3}]' will return true.
//
// See (broken link) for full details.
bool JsonContains(JSONValueConstRef input, JSONValueConstRef target);

// Options for JsonKeys functions.
//
struct JSONKeysOptions {
  json_internal::JsonPathOptions path_options;
  int64_t max_depth = std::numeric_limits<int64_t>::max();
};

// Returns all keys in a given JSON `input` based on `options`.
// Keys are deduplicated and sorted alphabetically.
//
// Parameter: `options.path_options`
//
// If `path_options.kLax` is enabled, descends through the json_doc in a 'lax'
// interpretation which includes objects and an auto unwrap of a single level of
// arrays.
//
// If `path_options.kLaxRecursive` is enabled, descends through the json_doc in
// a 'lax recursive' interpretation which auto unwraps arrays until a non-array
// type is encountered.
//
// Parameter: `options.max_depth`
// A limit to the max depth descended in the `input` json_doc. Each object
// descent increments the current depth by 1. If the depth limit is reached, the
// descension of the current branch ends and the current path key is added to
// the result. If no limit is provided, there is no depth enforcement.
//
// Example 1:
// JsonKeys(JSON '{"a":{"b":1}}',
//   {.path_options = AnyOf(kNone, kLax, kLaxRecursive}))
// Result: ["a", "a.b"]
//
// Example 2:
// JsonKeys(JSON '{"a":[{"b":1}, {"c":2}]}',
//   {.path_options = AnyOf(kLax, kLaxRecursive})
// Result: ["a", "a.b", "a.c"]
//
// Example 2a:
// JsonKeys(JSON '{"a":[{"b":1}, {"c":2}]}', {.path_options = kNone})
// Result: ["a"]
// Reasoning: Because `lax` is false, there is no descention through arrays.
//
// Example 3:
// JsonKeys(JSON '{"a":[[{"b":1}]]}', {.path_options = kLaxRecursive})
// Result: ["a", "a.b"]
// Reasoning: Because `kLaxRecursive` is enabled, auto unwraps nested arrays.
//
// Example 3a:
// JsonKeys(JSON '{"a":[[{"b":1}]]}', .path_options = AnyOf(kNone, kLax})
// Result: ["a"]
// Reasoning: Because `kLaxRecursive` is not enabled, nested arrays are NOT
// unwrapped.
//
// Example 4:
// JsonKeys(JSON '{"a":{"b":1}}',
//   AnyOf({.path_options = kNone, .max_depth = 1},
//         {.path_options = kLax, .max_depth = 1},
//         {.path_options = kLaxRecursive, .max_depth = 1}))
// Result: ["a"]
// Reasoning: Because `max_depth` = 1 only top level keys are returned and "a.b"
// is not included in the result.
//
// Example 5:
// JsonKeys(JSON '{"a":[{"b":1}]}',
//   AnyOf({.path_options = kLax, .max_depth = 2},
//         {.path_options = kLaxRecursive, .max_depth = 2}))
// Result: ["a", "a.b"]
// Reasoning: Only encountered objects increment depth, therefore "a.b" is
// included in the result.
//
// Invalid inputs:
// If `options.max_depth` <= 0, returns an error.
//
absl::StatusOr<std::vector<std::string>> JsonKeys(
    JSONValueConstRef input, const JSONKeysOptions& options);

// Flattens a JSON `input` into a vector of JSON values.
//
// The function traverses the JSON structure and extracts all nested array
// elements, stopping recursion only when encountering a non-array type.
//
// Example 1:
// JsonFlatten(JSON '[1, 2, 3]')
// Result: [JSON '1', JSON '2', JSON '3']
//
// Example 2:
// JsonFlatten(JSON '[[[1, 2], 3], 4]')
// Result: [JSON '1', JSON '2', JSON '3', JSON '4']
//
// Example 3:
// JsonFlatten(JSON '[[1, 2], {"foo": [3, 4]}]')
// Result: [JSON '1', JSON '2', JSON '{"foo": [3, 4]}']
//
// Example 4:
// JsonFlatten([JSON '[[[1, 2], 3], 4], {"a": 10}', ["foo", true]'])
// Result: [JSON '1', JSON '2', JSON '3', JSON '4', JSON '{"a": 10}',
//          JSON '"foo"', JSON 'true']
//
std::vector<JSONValueConstRef> JsonFlatten(JSONValueConstRef input);

}  // namespace functions
}  // namespace googlesql
#endif  // GOOGLESQL_PUBLIC_FUNCTIONS_JSON_H_
