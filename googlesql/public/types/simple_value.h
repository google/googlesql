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

#ifndef GOOGLESQL_PUBLIC_TYPES_SIMPLE_VALUE_H_
#define GOOGLESQL_PUBLIC_TYPES_SIMPLE_VALUE_H_

#include <cstdint>
#include <string>
#include <utility>

#include "googlesql/public/simple_value.pb.h"
#include "googlesql/public/types/value_representations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace googlesql {

// Represents an immutable simple value. SimpleValue is a light weight version
// of value class. It can be used in the type library to avoid circular
// dependency of directly using value class in the type library.
//
// SimpleValue supports cheap copying and assignment. More general scalar
// types or vector types (e.g. vector<string>) could be added when needed.
class SimpleValue {
 public:
  enum ValueType {
    TYPE_INVALID = 0,
    TYPE_INT64,
    TYPE_STRING,
    TYPE_BOOL,
    TYPE_DOUBLE,
    TYPE_BYTES,
  };
  // Factory methods to create atomic values.
  static SimpleValue Int64(int64_t v);
  static SimpleValue String(std::string v);
  static SimpleValue Bool(bool v);
  static SimpleValue Double(double v);
  static SimpleValue Bytes(std::string v);

  // Constructs an invalid value.  Needed for using values with STL.  All
  // methods that fetch value will crash if called on invalid values.
  SimpleValue() : type_(TYPE_INVALID) {}

  SimpleValue(const SimpleValue& that) { CopyFrom(that); }
  SimpleValue(SimpleValue&&);
  ~SimpleValue() { Clear(); }
  SimpleValue& operator=(const SimpleValue& that);
  SimpleValue& operator=(SimpleValue&& that);

  bool operator==(const SimpleValue& that) const { return Equals(that); }
  bool operator!=(const SimpleValue& that) const { return !Equals(that); }

  // Returns true if the value is valid (invalid values are created by the
  // default constructor).
  bool IsValid() const { return type_ != TYPE_INVALID; }
  bool has_int64_value() const { return type_ == TYPE_INT64; }
  bool has_string_value() const { return type_ == TYPE_STRING; }
  bool has_bool_value() const { return type_ == TYPE_BOOL; }
  bool has_double_value() const { return type_ == TYPE_DOUBLE; }
  bool has_bytes_value() const { return type_ == TYPE_BYTES; }

  // Crashes if type is not TYPE_INT64.
  int64_t int64_value() const;
  // Crashes if type is not TYPE_STRING.
  const std::string& string_value() const;
  // Crashes if type is not TYPE_BOOL.
  bool bool_value() const;
  // Crashes if type is not TYPE_DOUBLE.
  double double_value() const;
  // Crashes if type is not TYPE_BYTES.
  const std::string& bytes_value() const;

  // Returns true if this instance equals <that>.
  bool Equals(const SimpleValue& that) const;

  // Serializes this instance to proto.
  absl::Status Serialize(SimpleValueProto* proto) const;

  // Deserializes and returns an instance from a proto.
  static absl::StatusOr<SimpleValue> Deserialize(const SimpleValueProto& proto);

  std::string DebugString() const;

 private:
  friend class SimpleValueTest;
  friend class AnnotationMap;
  // Hide constructors below.  Users should always use factory method to create
  // an instance.
  SimpleValue(ValueType type, int64_t value)
      : type_(type), int64_value_(value) {}
  SimpleValue(ValueType type, std::string value)
      : type_(type), string_ptr_(new internal::StringRef(std::move(value))) {}
  SimpleValue(ValueType type, bool value) : type_(type), bool_value_(value) {}
  SimpleValue(ValueType type, double value)
      : type_(type), double_value_(value) {}

  // Clears the contents and makes it invalid.
  void Clear();

  // Copies the contents of this instance from <that>. Crashes if this pointer
  // equals to <that>.
  void CopyFrom(const SimpleValue& that);

  // Returns estimated size of memory owned by this SimpleValue.
  int64_t GetEstimatedOwnedMemoryBytesSize() const;

  ValueType type_;
  union {
    int64_t int64_value_ = 0;  // Assigned for TYPE_INT64.
    bool bool_value_;        // Assigned for TYPE_BOOL.
    double double_value_;     // Assigned for TYPE_DOUBLE.
    // Assigned for TYPE_STRING and TYPE_BYTES.  An instance of SimpleValue may
    // share ownership of the pointer with other instances with references being
    // counted.
    internal::StringRef* string_ptr_;
  };
};

}  // namespace googlesql
#endif  // GOOGLESQL_PUBLIC_TYPES_SIMPLE_VALUE_H_
