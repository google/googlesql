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

#ifndef GOOGLESQL_PUBLIC_VARIANT_VALUE_H_
#define GOOGLESQL_PUBLIC_VARIANT_VALUE_H_

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "googlesql/public/json_value.h"
#include "googlesql/public/type.h"
#include "googlesql/public/value.h"
#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace googlesql {

namespace internal {

// VariantValueAdapter adapts a googlesql::Value to the Variant data model
// (Simple View), classifying supported non-null values into one of three
// mutually exclusive categories (Primitive, Object, or Array) and providing
// uniform inspection and traversal across heterogeneous SQL types.
//
// Categories and Type Mapping:
// 1. Primitive: Scalar types that are not objects or arrays. Includes numeric
//    types, string, boolean and JSON 'null'. JSON primitive values are also
//    exposed as Primitives.
// 2. Object: Unordered collections of key-value pairs with string keys and
//    Variant values. The following backing types are exposed as Objects:
//    * STRUCT: Field names act as keys. Unnamed fields use the empty string as
//      their key. When duplicate field names exist, only the first field is
//      exposed.
//    * PROTO: Field names from the proto descriptor act as keys.
//    * MAP<STRING, V>: Non-null keys and their mapped values are exposed.
//      Entries with SQL NULL keys are omitted.
//    * JSON object: Exposed as key-value pairs of its JSON members.
// 3. Array: Ordered list of Variant values. SQL arrays of any type and JSON
//    arrays are exposed as Arrays.
//
// Note that the above views are not applicable to all types that can be stored
// in a Variant. For example, RANGE or MAP with non-string keys is not exposed
// as any of the above views.
//
// INVARIANT: At most one of IsPrimitive(), IsObject(), and IsArray() is true
// for any VariantValueAdapter.
//
// SQL NULL vs. Variant null:
// There are two kinds of nulls within the Variant model.
// 1. SQL NULL: The absence of a value (e.g., untyped NULL or typed SQL NULL
//    like Value::NullInt64()). Checked via is_null(). IsPrimitive(),
//    IsObject(), IsArray(), and IsVariantNull() all return false.
// 2. Variant null: It is a primitive value (similar to JSON 'null') and it's a
//    valid value. Checked via IsVariantNull(). IsPrimitive() returns true,
//    while IsObject() and   IsArray() return false. A Variant null is produced
//    either from a JSON 'null' value or when extracting a SQL NULL child from a
//    STRUCT field, ARRAY element, MAP value, or PROTO field.
//
// VariantValueAdapter is defined in googlesql::internal and exposed publicly
// via the googlesql::VariantValueView type alias.
//
// Internal Representation:
//   The underlying state is stored in rep_ as std::variant<Value, JSONNode>
//     * JSONNode is used for all valid, non-null JSON values. It bundles the
//       reference-counted root TYPE_JSON Value (root_value, kept to manage
//       document lifetime) with a JSONValueConstRef (ref) pointing to the
//       specific JSON node represented by this adapter.
//     * Value is used for all non-JSON types, SQL NULLs, invalid Values, and
//       unparsed JSON strings that failed to parse.
//   Unparsed TYPE_JSON strings are parsed once during VariantValueAdapter
//   construction. On success, the parsed document is wrapped in a validated
//   TYPE_JSON Value and stored as a JSONNode. On failure, the unparsed Value is
//   stored in the Value arm of rep_. IsPrimitive(), IsObject() and IsArray()
//   all return false. Traversing into JSON objects or arrays (GetKeyValue,
//   GetMembers, ForMembers, GetElement, GetElements) constructs child adapters
//   with a JSONNode that copies root_value (incrementing its reference count)
//   and sets ref to the child JSONValueConstRef. This avoids deep-copying JSON
//   subtrees while keeping child adapters valid independently of the parent
//   adapter's lifetime.
class VariantValueAdapter {
 public:
  // Projects `value` into the Variant Simple View.
  //
  // Callers holding a non-null TYPE_VARIANT should use value.variant_value()
  // instead. Passing a valid, non-null TYPE_VARIANT returns an error.
  static absl::StatusOr<VariantValueAdapter> Create(Value value);

  // Returns whether the Variant is valid. It would be invalid if
  // it was assigned an invalid value.
  bool is_valid() const;
  // Returns true if the Variant is SQL NULL. A SQL Variant means the underlying
  // value is an untyped NULL or a typed NULL.
  bool is_null() const;

  // Returns whether the Variant represents a primitive value. Returns false if
  // the Variant is invalid or SQL NULL.
  bool IsPrimitive() const;
  // Returns whether the Variant represents an object. Returns false if the
  // Variant is invalid or SQL NULL.
  bool IsObject() const;
  // Returns whether the Variant represents an array. Returns false if the
  // Variant is invalid or SQL NULL.
  bool IsArray() const;

  // Variant nulls are distinct from SQL NULL Variants. These can be created by
  // storing a JSON null inside a Variant, or accessing a SQL NULL element /
  // field inside an Array or Object. Returns false if the Variant is invalid or
  // SQL NULL.
  bool IsVariantNull() const;

  // If the Variant represents the underlying value as a primitive, returns the
  // primitive value. Returns an error if the Variant is not a primitive.
  absl::StatusOr<Value> GetPrimitiveValue() const;

  // If the Variant represents the underlying value as an object,
  // returns the keys of the object.
  // Requires IsObject() to be true. Otherwise, returns an error.
  // TODO: DO NOT USE. This function would be removed because
  // it provides no benefit over GetMembers().
  absl::StatusOr<std::vector<std::string>> GetKeys() const;

  // If the Variant represents the underlying value as an object,
  // returns all key-value pairs (members) of the object. Only the top-level
  // object is unpacked; nested objects are returned as VariantValueAdapter
  // instances without recursive unpacking.
  // Requires IsObject() to be true. Otherwise, returns an error.
  // TODO: Support complex proto fields (nested messages, enums).
  absl::StatusOr<std::vector<std::pair<std::string, VariantValueAdapter>>>
  GetMembers() const;

  // If the Variant represents the underlying value as an object, invokes `fn`
  // for each key-value pair (member) of the object without materializing all
  // members at once. Only the top-level object is unpacked; nested objects are
  // passed as VariantValueAdapter instances without recursive unpacking. Stops
  // iteration and returns the error if `fn` returns a non-OK status.
  // Requires IsObject() to be true. Otherwise, returns an error.
  // TODO: b/554973153 - Support complex proto fields (nested messages, enums).
  absl::Status ForMembers(absl::FunctionRef<absl::Status(
                              absl::string_view key, VariantValueAdapter value)>
                              fn) const;

  // If the Variant represents the underlying value as an object,
  // returns whether the key exists in the object.
  // If the Variant is not an object, returns false.
  bool HasKey(absl::string_view key) const;

  // If the Variant represents the underlying value as an object,
  // returns the value associated with the key.
  // Returns std::nullopt if the key is not found.
  // Requires IsObject() is true otherwise returns an error.
  // TODO: Support complex proto fields (nested messages, enums).
  absl::StatusOr<std::optional<VariantValueAdapter>> GetKeyValue(
      absl::string_view key) const;

  // If the Variant represents the underlying value as an object,
  // returns the value associated with the key.
  // Returns an error if the key is not found.
  // Requires IsObject() is true otherwise returns an error.
  // TODO: Support complex proto fields (nested messages, enums).
  absl::StatusOr<VariantValueAdapter> GetKeyValueIfExists(
      absl::string_view key) const;

  // If the Variant represents the underlying value as an array,
  // returns the size of the array.
  // Requires IsArray() to be true. Otherwise, returns an error.
  absl::StatusOr<int64_t> GetArraySize() const;

  // If the Variant represents the underlying value as an array,
  // returns all elements of the array. Only the top-level elements are
  // unpacked;
  // nested arrays or objects are returned as VariantValueAdapter instances
  // without recursive unpacking.
  // Requires IsArray() to be true. Otherwise, returns an error.
  absl::StatusOr<std::vector<VariantValueAdapter>> GetElements() const;

  // If the Variant represents the underlying value as an array,
  // returns the element at the given index.
  // Returns std::nullopt if the index is out of bounds.
  // Requires IsArray() to be true. Otherwise, returns an error.
  absl::StatusOr<std::optional<VariantValueAdapter>> GetElement(
      int64_t index) const;

  // If the Variant represents the underlying value as an array,
  // returns the element at the given index.
  // Returns an error if the index is out of bounds.
  // Requires IsArray() to be true. Otherwise, returns an error.
  absl::StatusOr<VariantValueAdapter> GetElementIfExists(int64_t index) const;

  // Returns a debug string representation of the Variant value projected
  // through the Simple View.
  // For Primitive view, the underlying value's DebugString is returned.
  // For Array view, the elements are recursively converted to
  // their DebugString representations.
  // For Object view, the values are recursively converted to
  // their DebugString representations. The key-value pairs are
  // lexicographically sorted by key.
  // SQL NULL Variants are represented as "NULL" while Variant Nulls are
  // represented as "null".
  std::string DebugString() const;

  // Materializes this view as a GoogleSQL Value of type TYPE_VARIANT:
  // - Returns Value::NullVariant() (SQL NULL of type VARIANT) if is_null().
  // - Returns Value::VariantNull() (Variant null of type VARIANT) if
  //   IsVariantNull().
  // - Otherwise wraps the represented value in Value::Variant(...)
  //   (materializing a TYPE_JSON Value if this view represents a JSON node).
  // Requires is_valid() to be true and the underlying type to be supported in
  // VARIANT. Otherwise, returns an error.
  absl::StatusOr<Value> ToVariantValue() const;

  // Struct stored in rep_ when this adapter represents a valid non-null JSON
  // value. The underlying root JSON document is stored in root_value, and ref
  // points to the JSON sub-node that this adapter represents.
  struct JSONNode {
    Value root_value;
    JSONValueConstRef ref;
  };

 private:
  // If this adapter represents a valid non-null JSON value, JSONNode is
  // populated. Otherwise (non-JSON types, SQL NULLs (including JSON-typed SQL
  // NULLs), invalid Values, and unparsed JSON strings that failed parsing),
  // Value is populated. See "Internal Representation" above for more details.
  std::variant<Value, JSONNode> rep_;

  // The Tag structs use this constructor to initialize rep_ directly with a
  // JSONNode.
  friend VariantValueAdapter MakeVariantValueAdapterForJSON(
      Value root_value, JSONValueConstRef json_ref);
  explicit VariantValueAdapter(Value value);
  explicit VariantValueAdapter(Value root_value, JSONValueConstRef json_ref)
      : rep_(JSONNode{std::move(root_value), json_ref}) {}
};

}  // namespace internal

// VariantValueView is an alias for internal::VariantValueAdapter. This alias
// is intended to be used in public APIs such that internal details of the
// VariantValueAdapter are hidden.
using VariantValueView = internal::VariantValueAdapter;

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_VARIANT_VALUE_H_
