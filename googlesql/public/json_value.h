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

#ifndef GOOGLESQL_PUBLIC_JSON_VALUE_H_
#define GOOGLESQL_PUBLIC_JSON_VALUE_H_

#include <stddef.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/compare.h"

namespace googlesql {

class JSONValueConstRef;
class JSONValueRef;

// Max array size. This is a soft limit only enforced on InsertArrayElement(s),
// as a user can accidentally enter a large value. This limit is not enforced on
// parsing as an accident is much less likely.
inline constexpr size_t kJSONMaxArraySize = 1000000;

// Options for parsing an input JSON-formatted string.
struct JSONParsingOptions {
  enum class WideNumberMode {
    // Numbers will be rounded to the nearest double value.
    kRound = 0,
    // Parsing will fail if there is a least one number value that does not
    // round-trip from string -> number -> string.
    kExact,
  };

  // This setting specifies how wide number should be handled.
  WideNumberMode wide_number_mode = WideNumberMode::kRound;
  // If 'max_nesting' is set to a non-negative number, parsing will fail if the
  // JSON document has more than 'max_nesting' levels of nesting. If it is set
  // to a negative number, the max nesting will be set to 0 instead (i.e. only
  // allowing scalar JSONs). JSON Arrays and Objects increase nesting levels.
  std::optional<int> max_nesting = std::nullopt;
  // If true, the sign on a signed zero is removed when converting numeric type
  // to string.
  // TODO : remove this option when all engines have
  // rolled out this new behavior.
  bool canonicalize_zero = false;
};

// Returns whether 'json_str' is a valid JSON string.
// This function is much faster than JSONValue::ParseJSONString for standard
// parsing mode and will be latter improved for legacy parsing mode as well.
//
// TODO: Find a better place for this function. It is currently
// placed in json_value.h because the implementation uses the
// JSONValueParserBase (but this can be moved too).
absl::Status IsValidJSON(
    absl::string_view json_str,
    const JSONParsingOptions& parsing_options = JSONParsingOptions());

// JSONValue stores a JSON document. Access to read and update the values and
// their members and elements is provided through JSONValueRef and
// JSONValueConstRef. JSONValue owns the document it represents.
class JSONValue final {
 public:
  // Constructs an empty JSON document.
  JSONValue();

  // Constructs JSON document containing the given value.
  explicit JSONValue(int64_t value);
  explicit JSONValue(uint64_t value);
  explicit JSONValue(double value);
  explicit JSONValue(bool value);
  explicit JSONValue(absl::string_view value);

  JSONValue(JSONValue&& value);
  ~JSONValue();

  JSONValue(const JSONValue&) = delete;
  JSONValue& operator=(const JSONValue&) = delete;

  JSONValue& operator=(JSONValue&& value);

  // Returns a read/write reference to the JSON value. The reference can be used
  // to access or update the value including object members and array elements.
  JSONValueRef GetRef();
  // Returns a read-only reference to the JSON value. The reference can be used
  // to read the value including object members and array elements.
  JSONValueConstRef GetConstRef() const;

  // Parses a given JSON document string and returns a JSON value.
  static absl::StatusOr<JSONValue> ParseJSONString(
      absl::string_view str,
      JSONParsingOptions parsing_options = JSONParsingOptions());

  // Decodes a binary representation of a JSON value produced by
  // JSONValueConstRef::SerializeAndAppendToProtoBytes(). Returns an error if
  // 'str' is not a valid binary representation.
  // Returns an error if the max nesting level of the JSON value exceeds
  // 'max_nesting_level'. If 'max_nesting_level' < 0, 0 will be used instead.
  static absl::StatusOr<JSONValue> DeserializeFromProtoBytes(
      absl::string_view str,
      std::optional<int> max_nesting_level = std::nullopt);

  // Returns a JSON value that is a deep copy of the given value.
  static JSONValue CopyFrom(JSONValueConstRef value);

  // Returns a JSON value with the contents "moved" from `value`. `value` is
  // left as a JSON 'null'. This operation is constant time complexity as it
  // uses move semantics.
  static JSONValue MoveFrom(JSONValueRef value);

 private:
  struct Impl;

  std::unique_ptr<Impl> impl_;

  friend class JSONValueConstRef;
  friend class JSONValueRef;
};

// JSONValueConstRef is a read-only reference to a JSON document stored by
// JSONValue. The JSONValue instance referenced should outlive the
// JSONValueConstRef instance.
class JSONValueConstRef {
 public:
  JSONValueConstRef() = delete;
  JSONValueConstRef(const JSONValueConstRef& pointer) = default;
  JSONValueConstRef(JSONValueConstRef&& pointer) = default;

  JSONValueConstRef& operator=(const JSONValueConstRef&) = default;
  JSONValueConstRef& operator=(JSONValueConstRef&&) = default;

  bool IsNumber() const;
  bool IsString() const;
  bool IsBoolean() const;
  bool IsNull() const;
  bool IsObject() const;
  bool IsArray() const;

  bool IsInt64() const;
  bool IsUInt64() const;
  bool IsDouble() const;

  // Returns a JSON number value as int64.
  // Requires IsInt64() to be true. Otherwise, the call results in ABSL_LOG(FATAL).
  int64_t GetInt64() const;
  // Returns a JSON number value as uint64.
  // Requires IsUInt64() to be true. Otherwise, the call results in ABSL_LOG(FATAL).
  uint64_t GetUInt64() const;
  // Returns a JSON number value as double.
  // Requires IsDouble() to be true. Otherwise, the call results in ABSL_LOG(FATAL).
  double GetDouble() const;
  // Returns a JSON string value.
  // Requires IsString() to be true. Otherwise, the call results in ABSL_LOG(FATAL).
  std::string GetString() const;
  // Returns a JSON string value.
  // Requires IsString() to be true. Otherwise, the call results in ABSL_LOG(FATAL).
  // This function returns a const ref, avoiding a copy compared to the function
  // above. The underlying JSONValue must outlive the returned reference.
  const std::string& GetStringRef() const;
  // Returns a JSON boolean value.
  // Requires IsBoolean() to be true. Otherwise, the call results in ABSL_LOG(FATAL).
  bool GetBoolean() const;

  // If the JSON value being referenced is an object, returns the number of
  // elements.
  //
  // Requires IsObject() to be true. Otherwise, the call results in ABSL_LOG(FATAL).
  size_t GetObjectSize() const;
  // If the JSON value being referenced is an object, returns whether the 'key'
  // references an existing member. If the JSON value is not an object, returns
  // false.
  //
  // Note: If the intent is to check for a key existence before accessing it,
  // use GetMemberIfExists() to avoid an additional map lookup.
  bool HasMember(absl::string_view key) const;
  // If the JSON value being referenced is an object, returns the member
  // corresponding to the given 'key'. If such 'key' does not exist, then
  // the call results in ABSL_LOG(FATAL).
  //
  // Requires IsObject() to be true. Otherwise, the call results in ABSL_LOG(FATAL).
  JSONValueConstRef GetMember(absl::string_view key) const;
  // If the JSON value being referenced is an object, returns the member
  // corresponding to the given 'key' if it exists. If such 'key' does not
  // exist or if the JSON value is not an object, then returns std::nullopt.
  std::optional<JSONValueConstRef> GetMemberIfExists(
      absl::string_view key) const;
  // If the JSON value being referenced is an object, returns all the key/value
  // pairs corresponding to members of the object.
  //
  // Requires IsObject() to be true. Otherwise, the call results in ABSL_LOG(FATAL).
  std::vector<std::pair<absl::string_view, JSONValueConstRef>> GetMembers()
      const;

  // If the JSON value being referenced is an array, returns the number of
  // elements.
  //
  // Requires IsArray() to be true. Otherwise, the call results in ABSL_LOG(FATAL).
  size_t GetArraySize() const;
  // If the JSON value being referenced is an array, returns the element at
  // 'index'. If such element does not exist (index >= GetArraySize()), then
  // the returned value is an invalid object.
  //
  // Requires IsArray() to be true. Otherwise, the call results in ABSL_LOG(FATAL).
  JSONValueConstRef GetArrayElement(size_t index) const;
  // If the JSON value being referenced is an array, returns all the elements.
  //
  // Requires IsArray() to be true. Otherwise, the call results in ABSL_LOG(FATAL).
  std::vector<JSONValueConstRef> GetArrayElements() const;

  // Serializes the JSON value into a compact string representation.
  // Note: For a human-friendly representation, please use Format().
  std::string ToString() const;

  // Serializes the JSON value into a multiline, human-friendly formatted string
  // representation.
  std::string Format() const;

  // Encodes the JSON value into a binary representation using UBJSON format and
  // append to 'output'. The binary representation can be decoded using
  // JSONValue::DeserializeFromProtoBytes().
  void SerializeAndAppendToProtoBytes(std::string* output) const;

  // Returns the number of bytes used to store the JSON value.
  uint64_t SpaceUsed() const;

  // Returns true iff the nesting level of JSON value larger than `max_nesting`.
  // JSON Scalar value has level 0. Example JSON '{"a":1}', '[1,2]' has level 1.
  // Space complexity: O(min(depth(JSON), max_nesting)).
  bool NestingLevelExceedsMax(int64_t max_nesting) const;

  // Returns hash code for the value.
  size_t HashCode() const;

  // Returns true if the JSON value referenced by 'this' equals the one
  // referenced by 'that'. This is only used in testing and is *not* the SQL
  // equality.
  bool NormalizedEquals(JSONValueConstRef that) const;

  // Overload the comparison operators for `JSONValueConstRef`.
  //
  // JSON values with different types are ordered as follows,
  // Null < String < Number < Boolean < Array < Object.
  //
  // JSON values of the same type are ordered as follows,
  //
  // Null:
  //   There is only one null value, and they are equal.
  // String:
  //   Compare the string values lexicographically.
  // Number:
  //   Compare the number values mathematically.
  // Boolean:
  //   False is less than True.
  // Arrays:
  //   JSON arrays are compared element by element from the beginning.
  //   The first pair of elements that are not semantically equal determines
  //   the order based on the comparison of those two elements (using these
  //   JSON comparison rules recursively). If one array's elements form a
  //   prefix of the other's, the longer array is considered greater. Empty
  //   JSON array is considered less than non-empty JSON array.
  // Objects:
  //   Compare the members of the two JSON objects, sorted by string type keys
  //   lexicographically.
  //   When the keys are different, the object with the smaller key is
  //   considered less. If the keys are the same, the smaller JSON value with
  //   the key is considered less. If the key and value are the same, the
  //   smaller JSON with the fewer members is considered less.
  friend bool operator==(JSONValueConstRef lhs, JSONValueConstRef rhs);
  friend bool operator!=(JSONValueConstRef lhs, JSONValueConstRef rhs);
  friend bool operator<(JSONValueConstRef lhs, JSONValueConstRef rhs);
  friend bool operator>(JSONValueConstRef lhs, JSONValueConstRef rhs);
  friend bool operator<=(JSONValueConstRef lhs, JSONValueConstRef rhs);
  friend bool operator>=(JSONValueConstRef lhs, JSONValueConstRef rhs);

  // Overload of the hash function for `JSONValueConstRef`.
  template <typename H>
  friend H AbslHashValue(H h, const JSONValueConstRef& v) {
    return H::combine(std::move(h), v.HashCode());
  }

 protected:
  explicit JSONValueConstRef(const JSONValue::Impl* value_pointer);

 private:
  const JSONValue::Impl* impl_;

  // Returns the three-way comparison result of the JSON values referenced.
  // In c++20, this function can be replaced by the `<=>` operator.
  friend absl::partial_ordering spaceship_operator(JSONValueConstRef lhs,
                                                   JSONValueConstRef rhs);

  friend class JSONValue;
};

// JSONValueRef is a read/write reference to a JSON document stored by
// JSONValue. The JSONValue instance referenced should outlive the
// JSONValueRef instance.
class JSONValueRef : public JSONValueConstRef {
 public:
  JSONValueRef() = delete;
  JSONValueRef(const JSONValueRef& pointer) = default;
  JSONValueRef(JSONValueRef&& pointer) = default;

  JSONValueRef& operator=(const JSONValueRef&) = default;
  JSONValueRef& operator=(JSONValueRef&&) = default;

  // Sets the JSON value to null.
  void SetNull();
  // Sets the JSON value to the given int64 value.
  void SetInt64(int64_t value);
  // Sets the JSON value to the given uin64 value.
  void SetUInt64(uint64_t value);
  // Sets the JSON value to the given double value.
  void SetDouble(double value);
  // Sets the JSON value to the given string value.
  void SetString(absl::string_view value);
  // Sets the JSON value to the given boolean value.
  void SetBoolean(bool value);
  // Sets the JSON value to the given json value.
  void Set(JSONValue json_value);
  // Sets the JSON value to an empty object.
  void SetToEmptyObject();
  // Sets the JSON value to an empty array.
  void SetToEmptyArray();

  // If the JSON value being referenced is an object, returns the member
  // corresponding to the given 'key'. If such 'key' does not exist, creates
  // a null value corresponding to the 'key' and returns it.
  //
  // Requires IsObject() or IsNull() to be true. Otherwise, the call results in
  // ABSL_LOG(FATAL).
  JSONValueRef GetMember(absl::string_view key);
  // If the JSON value being referenced is an object, returns the member
  // corresponding to the given 'key' if it exists. If such 'key' does not
  // exist or if the JSON value is not an object, then returns std::nullopt.
  std::optional<JSONValueRef> GetMemberIfExists(absl::string_view key);
  // If the JSON value being referenced is an object, returns all the key/value
  // pairs corresponding to members of the object.
  //
  // Requires IsObject() or IsNull() to be true. Otherwise, the call results in
  // ABSL_LOG(FATAL).
  std::vector<std::pair<absl::string_view, JSONValueRef>> GetMembers();
  // If the JSON value being referenced is an object, removes the key/value
  // pair corresponding to 'key' if it exists. Return true if the member exists,
  // false otherwise.
  //
  // Returns an error if the JSON value is not an object.
  absl::StatusOr<bool> RemoveMember(absl::string_view key);

  // If the JSON value being referenced is an array, returns the element at
  // 'index'. If the element does not exist, resizes the array with null
  // elements and returns a reference to the newly created null element.
  //
  // Requires IsArray() or IsNull() to be true. Otherwise, the call results in
  // ABSL_LOG(FATAL).
  JSONValueRef GetArrayElement(size_t index);
  // If the JSON value being referenced is an array, returns all the elements.
  //
  // Requires IsArray() or IsNull() to be true. Otherwise, the call results in
  // ABSL_LOG(FATAL).
  std::vector<JSONValueRef> GetArrayElements();
  // If the JSON value being referenced is an array, inserts 'json_value' at
  // 'index'. If 'index' is greater than the size of the array, expands the
  // array with JSON nulls then inserts the value at the end of the array.
  //
  // Returns an error if the JSON value is not an array or if the insertion
  // would result in a oversized JSON array (max size: kJSONMaxArraySize).
  absl::Status InsertArrayElement(JSONValue json_value, size_t index);
  // If the JSON value being referenced is an array, inserts 'json_values' at
  // 'index'. If 'index' is greater than the size of the array, expands the
  // array with JSON nulls then inserts the values at the end of the array. The
  // values are inserted in the same order as their order in the vector.
  //
  // Returns an error if the JSON value is not an array or if the insertion
  // would result in a oversized JSON array (max size: kJSONMaxArraySize).
  absl::Status InsertArrayElements(std::vector<JSONValue> json_values,
                                   size_t index);
  // If the JSON value being referenced is an array, adds 'json_value' at the
  // end of the array.
  //
  // Returns an error if the JSON value is not an array or if the insertion
  // would result in a oversized JSON array (max size: kJSONMaxArraySize).
  absl::Status AppendArrayElement(JSONValue json_value);
  // If the JSON value being referenced is an array, adds 'json_value' at the
  // end of the array. The values are appended in the same order as their order
  // in the vector.
  //
  // Returns an error if the JSON value is not an array or if the insertion
  // would result in a oversized JSON array (max size: kJSONMaxArraySize).
  absl::Status AppendArrayElements(std::vector<JSONValue> json_values);
  // If the JSON value being referenced is an array, removes the element at
  // 'index' and returns true. If 'index' is not a valid value, does nothing and
  // returns false.
  //
  // Returns an error if the JSON value is not an array.
  absl::StatusOr<bool> RemoveArrayElement(int64_t index);

  enum class RemoveEmptyOptions {
    // No-op.
    kNone = 0,
    // Remove only empty OBJECTs.
    kObject = 1,
    // Remove only empty ARRAYs.
    kArray = 2,
    // Remove empty OBJECTs and ARRAYs.
    kObjectAndArray = 3
  };

  // If the JSON value being referenced is an OBJECT, cleanup value by
  // removing JSON 'null' and optionally remove empty child containers based
  // on `remove_empty_options`. Only cleans up top level children and is
  // non-recursive.
  //
  // Returns an error if the JSON value is not an OBJECT.
  absl::Status CleanupJsonObject(RemoveEmptyOptions remove_empty_options);

  // If the JSON value being referenced is an ARRAY, cleanup value by
  // removing JSON 'null' and optionally remove empty child containers based
  // on `remove_empty_options`. Only cleans up top level children and is
  // non-recursive.
  //
  // Returns an error if the JSON value is not an ARRAY.
  absl::Status CleanupJsonArray(RemoveEmptyOptions remove_empty_options);

 private:
  explicit JSONValueRef(JSONValue::Impl* impl);

  JSONValue::Impl* impl_;

  friend class JSONValue;
};

// Returns absl::OkStatus() if the number in string 'lhs' is numerically
// equivalent to the number in the string created by serializing a JSON document
// containing 'val' to a string. Returns an error status otherwise.
//
// Restrictions:
//   - Input string ('lhs') can be at most 1500 characters in length.
//   - Numbers represented by 'lhs' and 'val' can have at most 1074 significant
//   fractional decimal digits, and at most 1500 significant digits.
//
// To understand the restrictions above, we must consider the following:
//
//  1) The 64-bit IEEE 754 double only guarantees roundtripping from text ->
//  double -> text for numbers that have 15 significant digits at most. Values
//  with more significant digits than 15 may or may not round-trip.
//
//  2) The conversion of double -> text -> double will produce the exact same
//  double value if at least 17 significant digits are used in serialization of
//  the double value to text (e.g. "10.000000000000001" -> double and
//  "10.0000000000000019" -> double return the same double value).
//
// The implication of the above two facts might suggest that numbers with more
// than 17 significant digits should be rejected for round-tripping. However,
// there are 2 categories of numbers that can exceed 17 significant digits but
// still round-trip. The first category consists of very large whole numbers
// with a continuous sequence of trailing zeros before the decimal point
// (e.g. 1e+200). The second category consists of very small purely fractional
// numbers with a continuous sequence of leading zeros after the decimal point
// (e.g. 1e-200).
//
// To ensure that the numbers in these additional categories are correctly
// considered for round-tripping, we allow for a maximum of 1074 significant
// fractional digits and a maximum total string length of 1500 characters.
//
// For ASCII decimal input strings, this allows for a maximum of 424 significant
// whole decimal digits, which exceeds the number of significant whole digits
// for the maximum value of double (~1.7976931348623157E+308), while still
// capturing the maximum number of fractional digits for a double (1074).
// Scientific notation input strings can potentially express more than 424
// significant decimal digits, while still being constrained to 1074 significant
// fractional decimal digits.
absl::Status CheckNumberRoundtrip(absl::string_view lhs, double val);

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_JSON_VALUE_H_
