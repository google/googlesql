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

#ifndef GOOGLESQL_PUBLIC_VALUE_H_
#define GOOGLESQL_PUBLIC_VALUE_H_

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iosfwd>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "googlesql/public/simple_token_list.h"
#include "googlesql/public/civil_time.h"
#include "googlesql/public/interval_value.h"
#include "googlesql/public/json_value.h"
#include "googlesql/public/numeric_value.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/timestamp_picos_value.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/extended_type.h"
#include "googlesql/public/types/map_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/value_equality_check_options.h"
#include "googlesql/public/types/value_representations.h"
#include "googlesql/public/uuid_value.h"
#include "googlesql/public/value.pb.h"
#include "googlesql/public/value_content.h"
#include "gtest/gtest_prod.h"
#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "absl/base/port.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/string_view.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "google/protobuf/dynamic_message.h"
#include "googlesql/base/map_view.h"

namespace googlesql {

// Represents a value in the GoogleSQL type system. Each valid value has a
// type. Values are copied by value. Struct and array values have refcounted
// contents so they can be copied cheaply. A value has type-specific methods
// that are applicable only to struct or array values.
//
// Array values carry a boolean indicating whether they preserve order or ignore
// order. The latter is used whenever the order of array elements is partially
// specified (e.g., after sorting with duplicate keys). A given query might
// produce arrays whose order is fully or partially specified depending on the
// input data. In the reference implementation, we track order preservation of
// arrays and compare arrays as multisets whenever their order is not fully
// specified. Tracking order preservation is not required otherwise; in the
// GoogleSQL data model, all arrays are totally ordered.
//
// Value's content is managed by corresponding Type through CopyValueContent and
// ClearValueContent functions. Value calls these functions during value copying
// and destruction. ValueContent class is used as a container to transfer
// content from Type to Value. A Value has a content if and only if this Value
// is non-null. Value's content can be up to 12 bytes in size. First 8 bytes are
// accessible to any type, while the remaining 4 can be used only by simple
// types.
//
// Caveat: since a Value contains a reference to a Type, referenced Type should
// outlive the Value. While simple built-in types are static, lifetime of
// parameterized types (e.g. proto, struct or enum) depends on a lifetime of the
// TypeFactory that created them and it's user's responsibility to make sure
// that a Value is released strictly before a Type that this Value references.
// Please check comments for googlesql::TypeFactory and
// googlesql::TypeFactoryOptions classes for the details on type's lifetime. The
// same way, user is responsible for making sure that DescriptorPool that
// provided proto descriptors for proto and enum types outlived Value belonging
// to these types.
//
// Thread safety
// -------------
// Value has the same thread-safety properties as many other types like
// string, vector<>, int, etc -- it is thread-compatible as defined by
// (broken link). In particular, if no thread may call a non-const
// method, then it is safe to concurrently call const methods. Copying a Value
// produces a new instance that can be used concurrently with the original in
// arbitrary ways.
//
class Value {
 public:
  // Constructs an invalid value. Needed for using values with STL. All methods
  // other than is_valid() will crash if called on invalid values.
#ifndef SWIG
  // SWIG has trouble with constexpr.
  constexpr
#endif
      Value();
  Value(const Value& that);
  const Value& operator=(const Value& that);
#ifndef SWIG
  Value(Value&& that) noexcept;             // NOLINT(build/c++11)
  Value& operator=(Value&& that) noexcept;  // NOLINT(build/c++11)
#endif
  ~Value();

  // Returns the type of the value.
  // Requires (crashes otherwise): is_valid()
  const Type* type() const;

  // Returns the type kind of the value. Same as type()->type_kind() but in some
  // cases can be a bit more efficient.
  // Requires (crashes otherwise): is_valid()
  TypeKind type_kind() const;

  // Returns the estimated size of the in-memory C++ representation of this
  // value.
  uint64_t physical_byte_size() const;

  // Returns true if the value is null.
  // Requires (crashes otherwise): is_valid()
  bool is_null() const;

  // Returns true if the value is an empty array.
  // Requires (crashes otherwise): is_valid()
  bool is_empty_array() const;

  // Returns true if the value is valid (invalid values are created by the
  // default constructor).
  bool is_valid() const;

  // Returns true if the Value is valid and non-null.
  bool has_content() const;

  // Accessors for accessing the data within atomic typed Values.
  // Requires (crashes otherwise): !is_null() && type_kind() == TYPE_INT32
  int32_t int32_value() const;
  // Requires (crashes otherwise): !is_null() && type_kind() == TYPE_INT64
  int64_t int64_value() const;
  // Requires (crashes otherwise): !is_null() && type_kind() == TYPE_UINT32
  uint32_t uint32_value() const;
  // Requires (crashes otherwise): !is_null() && type_kind() == TYPE_UINT64
  uint64_t uint64_value() const;
  // Requires (crashes otherwise): !is_null() && type_kind() == TYPE_BOOL
  bool bool_value() const;
  // Requires (crashes otherwise): !is_null() && type_kind() == TYPE_FLOAT
  float float_value() const;
  // Requires (crashes otherwise): !is_null() && type_kind() == TYPE_DOUBLE
  double double_value() const;
  // Requires (crashes otherwise): !is_null() && type_kind() == TYPE_STRING
  const std::string& string_value() const;
  // Requires (crashes otherwise): !is_null() && type_kind() == TYPE_BYTES
  const std::string& bytes_value() const;
  // Requires (crashes otherwise): !is_null() && type_kind() == TYPE_DATE
  int32_t date_value() const;
  // Requires (crashes otherwise): !is_null() && type_kind() == TYPE_ENUM
  int32_t enum_value() const;
  // Returns the name like "TESTENUM1" or number as string, like "7" if the name
  // is not known.
  // Requires (crashes otherwise): !is_null() && type_kind() == TYPE_ENUM
  std::string EnumDisplayName() const;
  // Returns the proto value as a Cord.
  // Requires (crashes otherwise): !is_null() && type_kind() == TYPE_PROTO
  const absl::Cord& proto_value() const;

  // Returns the name like "TESTENUM1", or a Status if the enum name cannot be
  // determined. The returned Status message is guaranteed to be usable in
  // user-facing error messages (E.g. not a GOOGLESQL_RET_CHECK stack trace), but exact
  // message is an implementation detail and may change over time.
  absl::StatusOr<absl::string_view> EnumName() const;

  // Returns date value as a absl::CivilDay.
  // Requires (crashes otherwise): !is_null() && type_kind() == TYPE_DATE
  absl::CivilDay ToCivilDay() const;

  // Returns timestamp value as absl::Time at nanoseconds precision.
  // Requires (crashes otherwise): !is_null() && type_kind() == TYPE_TIMESTAMP
  ABSL_DEPRECATED("Use ToUnixPicos()")
  absl::Time ToTime() const;

  // Returns timestamp value as Unix epoch microseconds.
  // Requires (crashes otherwise): !is_null() && type_kind() == TYPE_TIMESTAMP
  ABSL_DEPRECATED("Use ToUnixPicos()")
  int64_t ToUnixMicros() const;

  // Returns timestamp value as Unix epoch nanoseconds or an error if the value
  // does not fit into an int64.
  // Requires (crashes otherwise): !is_null() && type_kind() == TYPE_TIMESTAMP
  ABSL_DEPRECATED("Use ToUnixPicos()")
  absl::Status ToUnixNanos(int64_t* nanos) const;

  // Returns timestamp value as a TimestampPicosValue.
  // Requires (crashes otherwise): !is_null() && type_kind() == TYPE_TIMESTAMP
  const TimestampPicosValue& ToUnixPicos() const;

  // Returns time and datetime values at micros precision as bitwise encoded
  // int64, see public/civil_time.h for the encoding.
  // Requires (crashes otherwise): !is_null() && type_kind() == TYPE_TIME
  ABSL_DEPRECATED("Use ToUnixPicos()")
  int64_t ToPacked64TimeMicros() const;
  // Requires (crashes otherwise): !is_null() && type_kind() == TYPE_DATETIME
  ABSL_DEPRECATED("Use ToUnixPicos()")
  int64_t ToPacked64DatetimeMicros() const;

  // Requires (crashes otherwise): !is_null() && type_kind() == TYPE_TIME
  TimeValue time_value() const;
  // Requires (crashes otherwise): !is_null() && type_kind() == TYPE_DATETIME
  DatetimeValue datetime_value() const;
  // Requires (crashes otherwise): !is_null() && type_kind() == TYPE_INTERVAL
  const IntervalValue& interval_value() const;

  // Requires (crashes otherwise): !is_null() && type_kind() == TYPE_NUMERIC
  const NumericValue& numeric_value() const;

  // Requires (crashes otherwise): !is_null() && type_kind() == TYPE_BIGNUMERIC
  const BigNumericValue& bignumeric_value() const;

  // Checks whether the value belongs to the JSON type, non-NULL and is in
  // validated representation.
  bool is_validated_json() const;
  // Returns true if the value belongs to the JSON type, non-null and is in
  // unparsed representation. See comments to is_validated_json() for more
  // details.
  bool is_unparsed_json() const;
  // Returns the JSON representation in the unparsed mode.
  // Requires (crashes otherwise): !is_null() && type_kind() == TYPE_JSON &&
  //   is_unparsed_json()
  const std::string& json_value_unparsed() const;
  // Returns the JSON value in the validated mode.
  // Requires (crashes otherwise): !is_null() && type_kind() == TYPE_JSON &&
  //   is_validated_json()
  JSONValueConstRef json_value() const;
  // Returns the string representing stored JSON value.
  // Requires (crashes otherwise): !is_null() && type_kind() == TYPE_JSON
  std::string json_string() const;

  // Requires (crashes otherwise): !is_null() && type_kind() == TYPE_TOKENLIST
  const tokens::TokenList& tokenlist_value() const;

  // RET_CHECKs if is_null() || type_kind() != TYPE_UUID,
  absl::StatusOr<UuidValue> uuid_value() const;

  // Returns the value content of extended type.
  // Caller must check `has_content()`, as an invalid value will be returned.
  // Requires (crashes otherwise): type_kind() == TYPE_EXTENDED
  ValueContent extended_value() const;

  // Generic accessor for numeric PODs.
  // Requires (crashes otherwise): !is_null() && T corresponds to type_kind():
  //   int32_t (TYPE_INT32), int64_t (TYPE_INT64), uint32_t (TYPE_UINT32),
  //   uint64_t (TYPE_UINT64), bool (TYPE_BOOL), float (TYPE_FLOAT), double
  //   (TYPE_DOUBLE), NumericValue (TYPE_NUMERIC), BigNumericValue
  //   (TYPE_BIGNUMERIC), IntervalValue (TYPE_INTERVAL)
  template <typename T>
  inline T Get() const;

  // Accessors that coerce the data to the requested C++ type.
  // Requires (crashes otherwise): !is_null() && type_kind() in (TYPE_BOOL,
  //   TYPE_INT32, TYPE_INT64, TYPE_UINT32, TYPE_DATE, TYPE_ENUM,
  //   TYPE_TIMESTAMP, TYPE_TIMESTAMP_PICOS)
  // Use of this method for TIMESTAMP values is DEPRECATED.
  int64_t ToInt64() const;
  // Requires (crashes otherwise): !is_null() && type_kind() in (TYPE_BOOL,
  //   TYPE_UINT32, TYPE_UINT64)
  uint64_t ToUint64() const;
  // Requires (crashes otherwise): !is_null() && type_kind() in (TYPE_BOOL,
  //   TYPE_DATE, TYPE_DOUBLE, TYPE_FLOAT, TYPE_INT32, TYPE_UINT32, TYPE_UINT64,
  //   TYPE_INT64, TYPE_NUMERIC, TYPE_BIGNUMERIC, TYPE_ENUM)
  // Use of this method for TIMESTAMP values is DEPRECATED.
  double ToDouble() const;
  // Requires (crashes otherwise): !is_null() && type_kind() in (TYPE_STRING,
  //   TYPE_BYTES, TYPE_PROTO)
  absl::Cord ToCord() const;
  // Requires (crashes otherwise): !is_null() && type_kind() in (TYPE_STRING,
  //   TYPE_BYTES, TYPE_PROTO)
  std::string ToString() const;

  // Convert this value to a dynamically allocated proto Message.
  //
  // If 'return_null_on_error' is false, this does a best-effort conversion of
  // the bytes to the Message type, and ignores errors during parsing, and does
  // not check that the constructed Message is valid. If 'return_null_on_error'
  // is true, returns null if there is a parse error (missing required fields
  // is still not considered an error).
  //
  // Caller owns the returned object, which cannot outlive <message_factory>.
  //
  // Note: If all you want to do is to convert a proto Value into a c++ proto
  // with a known type, consider using ToCord() and ParseFromCord() as follows:
  //   MyMessage pb;
  //   ...
  //   if (bool parse_ok = pb.ParseFromCord(value.ToCord()); !parse_ok) {
  //     /* Handle error */
  //   }
  // This simpler pattern avoids passing in a message factory and avoids the
  // ownership issues associated with the returned Message.
  //
  // Requires (crashes otherwise): !is_null() && type_kind() == TYPE_PROTO
  google::protobuf::Message* ToMessage(google::protobuf::DynamicMessageFactory* message_factory,
                             bool return_null_on_error = false) const;

  // Struct-specific methods.
  // Requires (crashes otherwise): !is_null() && type_kind() == TYPE_STRUCT
  int num_fields() const;
  // Requires (crashes otherwise): !is_null() && type_kind() == TYPE_STRUCT
  const Value& field(int i) const;
  // Requires (crashes otherwise): !is_null() && type_kind() == TYPE_STRUCT
  const std::vector<Value>& fields() const;
  // Returns the value of the first field with the given 'name'. If one doesn't
  // exist, returns an invalid value.
  // Does not find anonymous fields (those with empty names).
  // Requires (crashes otherwise): !is_null() && type_kind() == TYPE_STRUCT
  const Value& FindFieldByName(absl::string_view name) const;

  // Array and Map-specific methods.
  // Requires (crashes otherwise): !is_null() && type_kind() in (TYPE_ARRAY,
  //   TYPE_MAP)
  int num_elements() const;

  // Array-specific methods.
  // Requires (crashes otherwise): !is_null() && type_kind() == TYPE_ARRAY
  bool empty() const;
  // Requires (crashes otherwise): !is_null() && type_kind() == TYPE_ARRAY
  const Value& element(int i) const;
  // Requires (crashes otherwise): !is_null() && type_kind() == TYPE_ARRAY
  const std::vector<Value>& elements() const;

  // Map-specific methods.
  // Returns the entries of the map. Note that a stable order is not guaranteed.
  // Requires (crashes otherwise): !is_null() && type_kind() == TYPE_MAP
  googlesql_base::MapView<Value, Value> map_entries() const;

  // Range-specific methods.
  // Requires (crashes otherwise): !is_null() && type_kind() == TYPE_RANGE
  const Value& start() const;
  // Requires (crashes otherwise): !is_null() && type_kind() == TYPE_RANGE
  const Value& end() const;

  // GraphElement-specific methods.
  // Requires (crashes otherwise): !is_null() && type_kind() ==
  //   TYPE_GRAPH_ELEMENT
  bool IsNode() const;
  // Requires (crashes otherwise): !is_null() && type_kind() ==
  //   TYPE_GRAPH_ELEMENT
  bool IsEdge() const;

  // Returns an identifier uniquely identifies the graph element in a graph.
  // Requires (crashes otherwise): !is_null() && type_kind() ==
  //   TYPE_GRAPH_ELEMENT
  absl::string_view GetIdentifier() const;

  // Returns the definition name of the graph element.
  // Requires (crashes otherwise): !is_null() && type_kind() ==
  //   TYPE_GRAPH_ELEMENT
  absl::string_view GetDefinitionName() const;

  // Returns all the labels of the graph element, sorted by alphabet order
  // case-insensitively. The output preserves the original case.
  // Requires (crashes otherwise): !is_null() && type_kind() ==
  //   TYPE_GRAPH_ELEMENT
  absl::Span<const std::string> GetLabels() const;

  // Returns the source node identifier of the edge. Caller should check
  // IsEdge(), function will ABSL_DCHECK or return an invalid value for non-edges.
  // Requires (crashes otherwise): !is_null() && type_kind() ==
  //   TYPE_GRAPH_ELEMENT
  absl::string_view GetSourceNodeIdentifier() const;

  // Returns the destination node identifier of the edge. Caller should check
  // IsEdge(), function will ABSL_DCHECK or return an invalid value for non-edges.
  // Requires (crashes otherwise): !is_null() && type_kind() ==
  //   TYPE_GRAPH_ELEMENT
  absl::string_view GetDestNodeIdentifier() const;

  // Returns the key values of a graph node.
  // Requires (crashes otherwise): !is_null() && IsNode()
  absl::Span<const Value> GetGraphNodeKeys() const;

  // Returns the source key values of a graph edge.
  // Requires (crashes otherwise): !is_null() && IsEdge()
  absl::Span<const Value> GetGraphEdgeSourceKeys() const;

  // Returns the destination key values of a graph edge.
  // Requires (crashes otherwise): !is_null() && IsEdge()
  absl::Span<const Value> GetGraphEdgeDestKeys() const;

  // Returns source and destination key values of a graph edge concatenated
  // together.
  // Requires (crashes otherwise): !is_null() && IsEdge()
  std::vector<Value> GetGraphEdgeKeys() const;

  // Returns the definition name of the source node for a graph edge.
  // Requires (crashes otherwise): !is_null() && IsEdge()
  absl::string_view GetSourceNodeElementTableName() const;

  // Returns the definition name of the destination node for a graph edge.
  // Requires (crashes otherwise): !is_null() && IsEdge()
  absl::string_view GetDestNodeElementTableName() const;

  // Returns the names of all properties that exist on the names of the graph
  // element, sorted by alphabet order case-insensitively.
  // Requires (crashes otherwise): !is_null() && type_kind() ==
  //   TYPE_GRAPH_ELEMENT
  std::vector<std::string> property_names() const;

  // Returns all the property values of the graph element.
  // Requires (crashes otherwise): !is_null() && type_kind() ==
  //   TYPE_GRAPH_ELEMENT
  absl::Span<const Value> property_values() const;

  // Returns the value of the property in its union-ed graph element type with
  // the given 'name'. If one doesn't exist, returns an error status.
  // Note that Value is designed to be copied cheaply.
  // Requires (crashes otherwise): !is_null() && type_kind() ==
  //   TYPE_GRAPH_ELEMENT
  absl::StatusOr<Value> FindPropertyByName(const std::string& name) const;

  // Returns the value of the property with a valid value with the given 'name'.
  // If one doesn't exist, returns an error status.
  // Note that Value is designed to be copied cheaply.
  // Requires (crashes otherwise): !is_null() && type_kind() ==
  //   TYPE_GRAPH_ELEMENT
  absl::StatusOr<Value> FindValidPropertyValueByName(
      const std::string& name) const;

  // Returns the value of the static property with the given 'name'. If one
  // doesn't exist, returns an error status.
  // Requires (crashes otherwise): !is_null() && type_kind() ==
  //   TYPE_GRAPH_ELEMENT
  absl::StatusOr<Value> FindStaticPropertyByName(const std::string& name) const;

  // GraphPath-specific methods
  // Returns the number of graph elements in the path.
  // Requires (crashes otherwise): !is_null() && type_kind() == TYPE_GRAPH_PATH
  int num_graph_elements() const;

  // Returns all graph elements in the path.
  // Requires (crashes otherwise): !is_null() && type_kind() == TYPE_GRAPH_PATH
  absl::Span<const Value> graph_elements() const;

  // Returns the `i`th graph element of a path. If `i` is even, the type of the
  // returned type will be equal the path type's node_type(); otherwise will be
  // equal to the path type's edge_type().
  // Requires (crashes otherwise): !is_null() && type_kind() == TYPE_GRAPH_PATH
  const Value& graph_element(int i) const;

  // Returns true if 'this' equals 'that' or both are null. This is *not* SQL
  // equality which returns null when either value is null. Returns false if
  // 'this' and 'that' have different types. For floating point values, returns
  // 'true' if both values are NaN of the same type.
  // For protos, returns true if the protos have the equivalent descriptors
  // (using Type::Equivalent) and the values are equivalent according to
  // MessageDifferencer::Equals, using the descriptor from 'this'.
  bool Equals(const Value& that) const;

  // Returns the BOOL (possibly NULL) Value of the SQL expression (*this =
  // that). Handles corner cases involving NULLs and NaNs. Returns an invalid
  // Value if the value types are not directly comparable (without an implicit
  // or explicit cast). (For example, INT64 and UINT64 are directly comparable,
  // but INT64 and INT32 are not comparable without a widening cast.) To be
  // safe, it is best to only use this method with Types that appear as
  // arguments to a call to the $equals function in a resolved AST generated by
  // the resolver.
  Value SqlEquals(const Value& that) const;

  // When the types of 'this' and 'that' are compatible, this function returns
  // true when 'this' is smaller than 'that'. When the types of 'this' and
  // 'that' are not compatible, the behavior is undefined.
  //
  // For simple types, this uses '<' operator on native c++ types to compare the
  // values. A null value is less than any non-null value. This is
  // *not* SQL inequality. For floating point values, NaN sorts smaller than
  // any other value including negative infinity.
  // For struct types, this compares the fields of the STRUCT pairwise in
  // ordinal order, returns true as soon as LessThan returns true for a field.
  // For array types, this compares the arrays in lexicographical order.
  bool LessThan(const Value& that) const;

  // Returns the BOOL (possibly NULL) Value of the SQL expression (*this <
  // that). Handles corner cases involving NULLs, NaNs. Returns an invalid Value
  // if the value types are not directly comparable (without an implicit or
  // explicit cast). (For example, INT64 and UINT64 are directly comparable, but
  // INT64 and INT32 are not comparable without a widening cast.) To be safe, it
  // is best to only use this method with Types that appear as arguments to a
  // call to a comparison function ($equals, $less, $less_or_equals, $greater,
  // or $greater_or_equals) in a resolved AST generated by the resolver.
  Value SqlLessThan(const Value& that) const;

  // Returns the hash code of a value.
  //
  // Result is not guaranteed stable across different runs of a program. It is
  // not cryptographically secure. It should not be used for distributed
  // coordination, security or storage which requires a stable computation.
  //
  // For more background see https://abseil.io/docs/cpp/guides/hash.
  //
  size_t HashCode() const;

  template <typename H>
  friend H AbslHashValue(H h, const Value& v);

  // The following few methods return a string representation of the value.
  // Here is a basic summary of their behaviors.
  //
  //       value           true                 [1, 2, 3]
  // ------------------ ---------- -----------------------------------
  // ShortDebugString() true       [1, 2, 3]
  // DebugString(false)
  // ------------------ ---------- -----------------------------------
  // FullDebugString()  Bool(true) Array[Int64(1), Int64(2), Int64(3)]
  // DebugString(true)
  // ------------------ ---------- -----------------------------------
  // Format(true)       Bool(true) ARRAY<INT64>[1,
  //                                            2,
  //                                            3] *
  // ------------------ ---------- -----------------------------------
  // Format(false)      true       ARRAY<INT64>[1,
  //                                            2,
  //                                            3] *
  // ------------------ ---------- -----------------------------------
  // GetSQLLiteral()    true       [1, 2, 3]
  // ------------------ ---------- -----------------------------------
  //
  // * The array needs to be complex enough to trigger the wrapping.

  // Returns printable string for this Value.
  // Verbose DebugStrings include the type name.
  std::string ShortDebugString() const { return DebugString(false); }
  std::string FullDebugString() const { return DebugString(true); }
  std::string DebugString(bool verbose = false) const;

  // Returns a pretty-printed (e.g. multi-line wrapped) string for the value.
  // Suitable for printing in golden-tests and documentation.
  // 'print_top_level_type' causes the top-level value to print its type. Array
  // values always print their types.
  std::string Format(bool print_top_level_type = true) const;

  // Returns a SQL expression that produces this value.
  // This is not necessarily a literal since we don't have literal syntax
  // for all values.
  // This assumes that any types used in Value can be resolved using the name
  // returned from type->TypeName().  Returned type names are sensitive to
  // the SQL ProductMode (INTERNAL or EXTERNAL).
  //
  // Note: Arguably, GetSQL() and GetSQLLiteral() don't work quite right for
  // STRUCTs.  In particular, they do not preserve field names in the result
  // string.  For example, if you have a STRUCT value like
  // STRUCT<a INT64, b STRING>(1, 'a'), and call GetSQL(), the result will
  // be "(1, 'a')".  If we're only interested in the value itself and not the
  // original type (with named fields) then maybe that's ok.  Note that
  // GetSQLLiteral() is used in GoogleSQL's FORMAT() function implementation
  // (Format() in googlesql/public_functions/format.cc) so we cannot change
  // the output without breaking existing GoogleSQL function semantics.
  // Setting `use_external_float32` to true will return
  // FLOAT32 as the type name for TYPE_FLOAT, for PRODUCT_EXTERNAL mode.
  // TODO: Remove `use_external_float32` once all engines are
  // updated.
  std::string GetSQL(ProductMode mode, bool use_external_float32) const;
  std::string GetSQL(ProductMode mode) const {
    return GetSQL(mode, /*use_external_float32=*/false);
  }
  ABSL_DEPRECATED("Use signature taking ProductMode.")
  std::string GetSQL() const {
    return GetSQL(PRODUCT_EXTERNAL, /*use_external_float32=*/false);
  }

  // Returns a SQL expression that is compatible as a literal for this value.
  // This won't include CASTs except for non-finite floating point values, and
  // won't necessarily produce the exact same type when parsed on its own, but
  // it should be the closest SQL literal form for this value.  Returned type
  // names are sensitive to the SQL ProductMode (INTERNAL or EXTERNAL).
  // Setting `use_external_float32` to true will return
  // FLOAT32 as the type name for TYPE_FLOAT, for PRODUCT_EXTERNAL mode.
  // TODO: Remove `use_external_float32` once all engines are
  // updated.
  std::string GetSQLLiteral(ProductMode mode, bool use_external_float32) const;
  std::string GetSQLLiteral(ProductMode mode) const {
    return GetSQLLiteral(mode, /*use_external_float32=*/false);
  }
  ABSL_DEPRECATED("Use signature taking ProductMode.")
  std::string GetSQLLiteral() const {
    return GetSQLLiteral(PRODUCT_EXTERNAL, /*use_external_float32=*/false);
  }

  // We do not define < operator to prevent accidental use of values of mixed
  // types in STL set and map.
  bool operator==(const Value& that) const { return Equals(that); }
  bool operator!=(const Value& that) const { return !Equals(that); }

  // Factory methods to create atomic non-null values.
  static Value Int32(int32_t v);
  static Value Int64(int64_t v);
  static Value Uint32(uint32_t v);
  static Value Uint64(uint64_t v);
  static Value Bool(bool v);
  static Value Float(float v);
  static Value Double(double v);
  // Unfortunately using the function name String causes issues for
  // cs/googlesql::ValueConstructor constructor which takes string-like objects.
  // Therefore using the name StringValue is a pragmatic way around this issue.
  static Value StringValue(std::string v);
  static Value String(absl::string_view v);
  static Value String(const absl::Cord& v);
  // str may contain '\0' in the middle, without getting truncated.
  template <size_t N>
  static Value String(const char (&str)[N]);
  static Value Bytes(std::string v);
  static Value Bytes(absl::string_view v);
  static Value Bytes(const absl::Cord& v);
  // str may contain '\0' in the middle, without getting truncated.
  template <size_t N>
  static Value Bytes(const char (&str)[N]);
  // Create a date value. 'v' is the number of days since unix epoch 1970-1-1
  static Value Date(int32_t v);
  // Creates a timestamp value from absl::Time at nanoseconds precision.
  static Value Timestamp(absl::Time t);
  // Creates a timestamp value from TimestampPicos at picoseconds precision.
  static Value Timestamp(TimestampPicosValue t);

  // Creates a timestamp value from Unix micros.
  static Value TimestampFromUnixMicros(int64_t v);

  static Value Time(TimeValue time);
  static Value Datetime(DatetimeValue datetime);

  // Creates a Value from a bitwise encoded int64 at micros precision.
  // see public/civil_time.h for the encoding.
  static Value TimeFromPacked64Micros(int64_t v);
  static Value DatetimeFromPacked64Micros(int64_t v);

  static Value Interval(IntervalValue interval);

  static Value Numeric(NumericValue v);

  static Value BigNumeric(BigNumericValue v);

  // Creates a Value storing JSON document in as a plain string without doing
  // any validation.
  static Value UnvalidatedJsonString(std::string v);

  // Creates a Value that stores parsed JSON document using representation
  // optimized for member access operations.
  static Value Json(JSONValue value);

  // TODO: as of 2021Q4, the encoding is a work-in-progress
  // and subject to change.  Avoid storing on disk.
  static Value TokenList(tokens::TokenList value);

  // Creates a value of extended type with the given content.
  static Value Extended(const ExtendedType* type, const ValueContent& value);

  // Creates a Value storing UUID value.
  static Value Uuid(UuidValue v);

  // Generic factory for numeric PODs.
  // REQUIRES: T is one of int32, int64, uint32, uint64, bool, float, double,
  // string, NumericValue, BigNumericValue, IntervalValue, UuidValue,
  template <typename T>
  inline static Value Make(T value) {
    if constexpr (std::is_same_v<T, NumericValue>) {
      return Value::Numeric(value);
    } else if constexpr (std::is_same_v<T, BigNumericValue>) {
      return Value::BigNumeric(value);
    } else if constexpr (std::is_same_v<T, IntervalValue>) {
      return Value::Interval(value);
    } else if constexpr (std::is_same_v<T, bool>) {
      return Value::Bool(value);
    } else if constexpr (std::is_same_v<T, float>) {
      return Value::Float(value);
    } else if constexpr (std::is_same_v<T, double>) {
      return Value::Double(value);
    } else if constexpr (std::is_same_v<T, std::string>) {
      return Value::String(value);
    } else if constexpr (std::is_same_v<T, UuidValue>) {
      return Value::Uuid(value);
    } else {
      constexpr int kNumBits = sizeof(T) * CHAR_BIT;
      static_assert(kNumBits == 32 || kNumBits == 64,
                    "Unsupported numeric type.");
      if constexpr (std::is_signed_v<T>) {
        if constexpr (kNumBits == 32) {
          return Value::Int32(value);
        } else if constexpr (kNumBits == 64) {
          return Value::Int64(value);
        }
      } else if constexpr (std::is_unsigned_v<T>) {
        if constexpr (kNumBits == 32) {
          return Value::Uint32(value);
        } else if constexpr (kNumBits == 64) {
          return Value::Uint64(value);
        }
      } else {
        static_assert(std::is_void_v<T>, "Unsupported type.");
      }
    }
  }

  // Generic factory for null values.
  // REQUIRES: T is one of int32, int64, uint32, uint64, bool, float, double,
  // string.
  template <typename T>
  inline static Value MakeNull() {
    if constexpr (std::is_same_v<T, NumericValue>) {
      return Value::NullNumeric();
    } else if constexpr (std::is_same_v<T, BigNumericValue>) {
      return Value::NullBigNumeric();
    } else if constexpr (std::is_same_v<T, bool>) {
      return Value::NullBool();
    } else if constexpr (std::is_same_v<T, float>) {
      return Value::NullFloat();
    } else if constexpr (std::is_same_v<T, double>) {
      return Value::NullDouble();
    } else if constexpr (std::is_same_v<T, std::string>) {
      return Value::NullString();
    } else if constexpr (std::is_same_v<T, UuidValue>) {
      return Value::NullUuid();
    } else {
      constexpr int kNumBits = sizeof(T) * CHAR_BIT;
      if constexpr (std::is_signed_v<T>) {
        if constexpr (kNumBits == 32) {
          return Value::NullInt32();
        } else if constexpr (kNumBits == 64) {
          return Value::NullInt64();
        }
      } else if constexpr (std::is_unsigned_v<T>) {
        if constexpr (kNumBits == 32) {
          return Value::NullUint32();
        } else if constexpr (kNumBits == 64) {
          return Value::NullUint64();
        }
      } else {
        static_assert(std::is_void_v<T>, "Unsupported type.");
      }
    }
  }

  // Factory methods to create atomic null values.
  static Value NullInt32();
  static Value NullInt64();
  static Value NullUint32();
  static Value NullUint64();
  static Value NullBool();
  static Value NullFloat();
  static Value NullDouble();
  static Value NullBytes();
  static Value NullString();
  static Value NullDate();
  static Value NullTimestamp();
  static Value NullTime();
  static Value NullDatetime();
  static Value NullInterval();
  static Value NullGeography();
  static Value NullNumeric();
  static Value NullBigNumeric();
  static Value NullJson();
  static Value NullTokenList();
  static Value NullUuid();

  // Returns an empty but non-null Geography value.
  static Value EmptyGeography();

  // Creates an enum value of the specified 'enum_type'. Unless
  // `allow_unknown_enum_values` is set, 'value' must be a valid numeric value
  // declared in 'enum_type', otherwise created Value is invalid.
  // NOTE: Enum types could only be 4 bytes, so this will always return an
  // invalid value if <value> is out-of-range for int32.
  static Value Enum(const EnumType* type, int64_t value);
  static Value Enum(const EnumType* type, int64_t value,
                    bool allow_unknown_enum_values);
  // Creates an enum value of the specified 'type'. 'name' must be a valid name
  // declared in 'type', otherwise created Value is invalid. 'name' is case
  // sensitive.
  static Value Enum(const EnumType* type, absl::string_view name);
  // Creates a protocol buffer value.
  static Value Proto(const ProtoType* type, absl::Cord value);

  // Creates a struct of the specified 'type' and given 'values'. The size of
  // 'values' must agree with the number of fields in 'type', and the
  // types of those values must match the corresponding struct fields, otherwise
  // returns an error. 'type' must outlive the returned object.
  static absl::StatusOr<Value> MakeStruct(const StructType* type,
                                          absl::Span<const Value> values);

#ifndef SWIG
  static absl::StatusOr<Value> MakeStruct(const StructType* type,
                                          std::vector<Value>&& values);

  // This overload is required to disambiguate vector&& and absl::Span.
  static absl::StatusOr<Value> MakeStruct(const StructType* type,
                                          std::initializer_list<Value> values);

  // Creates a struct of the specified 'type' and given 'values'. The size of
  // the 'values' vector must agree with the number of fields in 'type', and the
  // types of those values must match the corresponding struct fields.
  // This precondition is tested only during debug mode, and will result in
  // undefined behavior at a later time if it is violated.
  // This is an optimization, but it is dangerous and should only be used
  // when a demonstrated performance concern requires it. 'type' must outlive
  // the returned object.
  static absl::StatusOr<Value> MakeStructFromValidatedInputs(
      const StructType* type, std::vector<Value>&& values);

  // Creates a struct of the specified 'type' and given 'values'. The size of
  // the 'values' vector must agree with the number of fields in 'type', and the
  // types of those values must match the corresponding struct fields, otherwise
  // this will crash with a ABSL_CHECK failure.
  ABSL_DEPRECATED("Please use MakeStruct() instead.")
  static Value Struct(const StructType* type, absl::Span<const Value> values) {
    absl::StatusOr<Value> value = MakeStruct(type, values);
    GOOGLESQL_CHECK_OK(value);
    return std::move(value).value();
  }

  // Creates a struct of the specified 'type' by moving 'values'. The size of
  // the 'values' vector must agree with the number of fields in 'type', and the
  // types of those values must match the corresponding struct fields. However,
  // this is only ABSL_CHECK'd in debug mode.
  ABSL_DEPRECATED("Please use MakeStructFromValidatedInputs() instead.")
  static Value UnsafeStruct(const StructType* type,
                            std::vector<Value>&& values) {
    absl::StatusOr<Value> value =
        MakeStructFromValidatedInputs(type, std::move(values));
    GOOGLESQL_CHECK_OK(value);
    return std::move(value).value();
  }
#endif

  // Creates an empty array of the given 'array_type'.
  static Value EmptyArray(const ArrayType* array_type);

  // Creates an array of the given 'array_type' initialized with 'values'.
  // The type of each value must be the same as array_type->element_type(),
  // otherwise returns an error. 'array_type' must outlive the returned object.
  static absl::StatusOr<Value> MakeArray(const ArrayType* array_type,
                                         absl::Span<const Value> values);

#ifndef SWIG
  static absl::StatusOr<Value> MakeArray(const ArrayType* array_type,
                                         std::vector<Value>&& values);

  // This overload is required to disambiguate vector&& and absl::Span.
  static absl::StatusOr<Value> MakeArray(const ArrayType* array_type,
                                         std::initializer_list<Value> values);

  // Creates an array of the specified 'array_type' and given 'values'.
  // The type of each value must be the same as array_type->element_type().
  // This precondition is tested only during debug mode, and will result in
  // undefined behavior at a later time if it is violated.
  // This is an optimization, but it is dangerous and should only be used
  // when a demonstrated performance concern requires it.
  // 'array_type' must outlive the returned object.
  static absl::StatusOr<Value> MakeArrayFromValidatedInputs(
      const ArrayType* array_type, std::vector<Value>&& values);

  // Creates an array of the given 'array_type' initialized with 'values'.
  // The type of each value must be the same as array_type->element_type().
  // otherwise this will crash with a ABSL_CHECK failure.
  // 'array_type' must outlive the returned object.
  ABSL_DEPRECATED("Please use MakeArray() instead.")
  static Value Array(const ArrayType* array_type,
                     absl::Span<const Value> values) {
    absl::StatusOr<Value> value = MakeArray(array_type, values);
    GOOGLESQL_CHECK_OK(value);
    return std::move(value).value();
  }

  // Creates an array of the given 'array_type' initialized with 'values'.
  // The type of each value must be the same as array_type->element_type(),
  // however, this is only ABSL_CHECK'd in debug mode.
  // 'array_type' must outlive the returned object.
  ABSL_DEPRECATED("Please use MakeArrayFromValidatedInputs() instead.")
  static Value UnsafeArray(const ArrayType* array_type,
                           std::vector<Value>&& values) {
    absl::StatusOr<Value> value =
        MakeArrayFromValidatedInputs(array_type, std::move(values));
    GOOGLESQL_CHECK_OK(value);
    return std::move(value).value();
  }
#endif

  static Value UnboundedStartDate();
  static Value UnboundedEndDate();
  static Value UnboundedStartDatetime();
  static Value UnboundedEndDatetime();
  static Value UnboundedStartTimestamp();
  static Value UnboundedEndTimestamp();

  // Creates a Range given 'start' and 'end' values. 'start and 'end' must have
  // the same type, and this type has to be a valid range element type. The
  // range element type is inferred from the values. If 'start' or 'end' are
  // null values, then the range is unbounded on that end respectively.
  static absl::StatusOr<Value> MakeRange(const Value& start, const Value& end);

  // Creates a Range given 'start' and 'end' values with the 'range_type'.
  // Range preconditions are only tested during debug mode, and will result in
  // undefined behavior at a later time if they are violated.
  // This is an optimization, but it is dangerous and should only be used
  // when a demonstrated performance concern requires it. 'range_type' must
  // outlive the returned object.
  static absl::StatusOr<Value> MakeRangeFromValidatedInputs(
      const RangeType* range_type, const Value& start, const Value& end);

  typedef std::pair<std::string, Value> Property;

  // Contains labels and properties of a graph element.
  // - `static_labels` must not be empty.
  // - `static_properties` must be a subset of the declared static properties in
  //    the containing GraphElementType.
  // - `dynamic_labels` is empty if there is no dynamic labels.
  // - `dynamic_properties` is nullopt if `graph_element_type` is static.
  //   Otherwise, it must be a valid JSON value.
  struct GraphElementLabelsAndProperties {
    std::vector<std::string> static_labels;
    std::vector<Value::Property> static_properties;
    std::vector<std::string> dynamic_labels;
    std::optional<JSONValueConstRef> dynamic_properties = std::nullopt;
  };

  // Contains keys and definition names of a graph element.
  struct GraphNodeKeysAndElementTableNames {
    absl::string_view identifier;
    // While `identifier` is derived from keys, `keys` are stored as a vector
    // to preserve individual key types and values for use in different
    // functions.
    std::vector<Value> keys;
    absl::string_view element_table_name;
  };

  struct GraphEdgeKeysAndElementTableNames {
    absl::string_view identifier;
    absl::string_view element_table_name;
    GraphNodeKeysAndElementTableNames source;
    GraphNodeKeysAndElementTableNames dest;
  };

  ABSL_DEPRECATED("Use other MakeGraphNode overload instead.")
  static absl::StatusOr<Value> MakeGraphNode(
      const GraphElementType* graph_element_type, absl::string_view identifier,
      absl::Span<const Property> properties,
      absl::Span<const std::string> labels, absl::string_view definition_name) {
    return MakeGraphNode(
        graph_element_type, identifier,
        GraphElementLabelsAndProperties{
            .static_labels = {labels.begin(), labels.end()},
            .static_properties = {properties.begin(), properties.end()}},
        definition_name);
  }

  // TODO b/475590397: currently used by Spandex side, and will be removed
  // after Spandex side move to the new interface.
  ABSL_DEPRECATED("Use other MakeGraphNode overload instead.")
  static absl::StatusOr<Value> MakeGraphNode(
      const GraphElementType* graph_element_type, absl::string_view identifier,
      const GraphElementLabelsAndProperties& labels_and_properties,
      absl::string_view element_table_name);

  static absl::StatusOr<Value> MakeGraphNode(
      const GraphElementType* graph_element_type,
      const GraphElementLabelsAndProperties& labels_and_properties,
      const GraphNodeKeysAndElementTableNames&
          node_keys_and_element_table_names);

  // Creates a graph edge. An edge has an identifier, a source node identifier,
  // a destination node identifier, a definition name, and a list of properties.
  // - <identifier>, <source/dest_node_identifier> should not be empty;
  // - <properties> must be a subset of what is declared in <graph_element_type>
  // with matching types. otherwise returns an error.
  ABSL_DEPRECATED("Use other MakeGraphEdge overload instead.")
  static absl::StatusOr<Value> MakeGraphEdge(
      const GraphElementType* graph_element_type, absl::string_view identifier,
      absl::Span<const Property> properties,
      absl::Span<const std::string> labels, absl::string_view definition_name,
      absl::string_view source_node_identifier,
      absl::string_view dest_node_identifier) {
    return MakeGraphEdge(
        graph_element_type, identifier,
        GraphElementLabelsAndProperties{
            .static_labels = {labels.begin(), labels.end()},
            .static_properties = {properties.begin(), properties.end()}},
        definition_name, source_node_identifier, dest_node_identifier);
  }
  static absl::StatusOr<Value> MakeGraphEdge(
      const GraphElementType* graph_element_type,
      const GraphElementLabelsAndProperties& labels_and_properties,
      const GraphEdgeKeysAndElementTableNames&
          edge_keys_and_element_table_names);

  // TODO: b/475590397 - currently used by Spandex side, and will be removed
  // after Spandex side move to the new interface.
  ABSL_DEPRECATED("Use other MakeGraphEdge overload instead.")
  static absl::StatusOr<Value> MakeGraphEdge(
      const GraphElementType* graph_element_type, absl::string_view identifier,
      const GraphElementLabelsAndProperties& labels_and_properties,
      absl::string_view element_table_name,
      absl::string_view source_node_identifier,
      absl::string_view dest_node_identifier);

  // Create a graph path. A path is a list of interleaved nodes and edges given
  // in <graph_elements>. The length of <graph_elements> must be odd (starts
  // and ends with a graph node). None of the values in <graph_elements> can be
  // null: if they are, an error is returned.
  static absl::StatusOr<Value> MakeGraphPath(
      const GraphPathType* graph_path_type, std::vector<Value> graph_elements);

#ifndef SWIG  // TODO: Investigate SWIG compatibility for MAP.
  // Creates a map of the given 'map_type' initialized with 'map_entries' as the
  // key/value pairs. The type of each key and value must match the key and
  // value types in map_type, otherwise returns an error (in debug mode).
  // 'map_type' must outlive the returned object. A map_entries containing
  // multiple equivalent keys will result in an OutOfRange error.
  //
  // REQUIRES: map_type.type_kind() == MAP_TYPE
  // REQUIRES: map_entries key and value types match map_type
  // REQUIRES: map_entries does not contain any entries with equivalent keys.
  static absl::StatusOr<Value> MakeMap(
      const Type* map_type,
      absl::Span<const std::pair<const Value, const Value>> map_entries);
  static absl::StatusOr<Value> MakeMap(
      const Type* map_type, std::vector<std::pair<Value, Value>>&& map_entries);
  static absl::StatusOr<Value> MakeMap(
      const Type* map_type,
      std::initializer_list<std::pair<Value, Value>> map_entries);

  // Creates a map of the given `map_type` copying the key/value pairs
  // in `source_map`. The type of each key and value must match the key and
  // value types in map_type, otherwise returns an error (in debug mode).
  // `map_type` must outlive the returned object.
  //
  // REQUIRES: map_type.type_kind() == MAP_TYPE
  // REQUIRES: source_map key and value types match map_type
  static absl::StatusOr<Value> MakeMap(const Type* map_type,
                                       googlesql_base::MapView<Value, Value> source_map);
#endif

  // Creates a null of the given 'type'.
  static Value Null(const Type* type);
  // Creates an invalid value.
  static Value Invalid() { return Value(); }

  // Parse an integer literal into a Value.
  // The returned Value will be INT64, unless UINT64 is necessary to store
  // the value.  Hex literals are supported.  Negative non-hex literals are
  // supported.  Leading and trailing spaces are ignored.
  ABSL_MUST_USE_RESULT static bool ParseInteger(absl::string_view input,
                                                Value* value);

  // Serializes the Value into ValueProto protocol buffer.
  absl::Status Serialize(ValueProto* value_proto) const;

  // Deserializes a ValueProto into Value. Since ValueProto does not know its
  // full type, the type information is passed as an additional parameter.
  static absl::StatusOr<Value> Deserialize(const ValueProto& value_proto,
                                           const Type* type);

 private:
  // For access to StringRef and TypedList.
  FRIEND_TEST(ValueTest, PhysicalByteSize);
  // For access to GetContent() and MakeArrayInternal
  FRIEND_TEST(TypeTest, FormatValueContentArraySQLLiteralMode);
  FRIEND_TEST(TypeTest, FormatValueContentArraySQLExpressionMode);
  FRIEND_TEST(TypeTest, FormatValueContentArrayDebugMode);
  FRIEND_TEST(TypeTest, FormatValueContentStructSQLLiteralMode);
  FRIEND_TEST(TypeTest, FormatValueContentStructSQLExpressionMode);
  FRIEND_TEST(TypeTest, FormatValueContentStructDebugMode);
  FRIEND_TEST(TypeTest, FormatValueContentStructWithAnonymousFieldsDebugMode);
  FRIEND_TEST(MapTest, FormatValueContentSQLLiteralMode);
  FRIEND_TEST(MapTest, FormatValueContentSQLExpressionMode);
  FRIEND_TEST(MapTestFormatValueContentDebugMode, FormatValueContentDebugMode);
  FRIEND_TEST(MapTest, FormatValueContentDebugModeEmptyMap);
  // For access to Metadata
  FRIEND_TEST(ValueTest, MetadataChecks);

  template <bool as_literal, bool maybe_add_simple_type_prefix>
  std::string GetSQLInternal(ProductMode mode, bool use_external_float32) const;

  template <typename H>
  H HashValueInternal(H h) const;

  friend class InternalValue;  // Defined in googlesql/common/internal_value.h.
  friend struct InternalComparer;  // Defined in value.cc.
  friend struct InternalHasher;    // Defined in value.cc
  class TypedList;                 // Defined in value_inl.h
  class TypedMap;                  // Defined in value_inl.h
  class TypedMeasure;              // Defined in value_inl.h

  class GraphElementValue;  // Defined in value_inl.h
  class GraphPathValue;     // Defined in value_inl.h

  // REQUIRES: !is_null(), type_kind() == TYPE_GRAPH_ELEMENT
  const GraphElementValue* graph_element_value() const;

  // REQUIRES: !is_null(), type_kind() == TYPE_GRAPH_PATH
  const GraphPathValue* graph_path_value() const;

  // Specifies whether an array value preserves or ignores order (public array
  // values always preserve order). The enum values are designed to be used with
  // boolean AND and OR.
  typedef bool OrderPreservationKind;

  static constexpr OrderPreservationKind kPreservesOrder = true;
  static constexpr OrderPreservationKind kIgnoresOrder = false;

  static constexpr int kInvalidTypeKind =
      __TypeKind__switch_must_have_a_default__;

  // Constructs an empty (where content contains zeros) or NULL value of the
  // given 'type'. Argument order_kind is currently used only for arrays.
  // kPreservesOrder should be set for all other types, except for Map which
  // always sets kIgnoresOrder regardless of the supplied order_kind.
  Value(const Type* type, bool is_null, OrderPreservationKind order_kind);

  // Constructs a typed NULL of the given 'type'.
  explicit Value(const Type* type)
      : Value(type, /*is_null=*/true,
              type->kind() == TYPE_MAP ? kIgnoresOrder : kPreservesOrder) {}
#ifndef SWIG
  // SWIG has trouble with constexpr.
  constexpr
#endif
      explicit Value(TypeKind kind)
      : metadata_(kind, /*is_null=*/true,
                  kind == TYPE_MAP ? kIgnoresOrder : kPreservesOrder,
                  /*value_extended_content=*/0) {
  }

  // Constructors for non-null atomic values.
  explicit Value(int32_t value);
  explicit Value(int64_t value);
  explicit Value(uint32_t value);
  explicit Value(uint64_t value);
  explicit Value(bool value);
  explicit Value(float value);
  explicit Value(double value);
  // REQUIRES: type_kind is date or timestamp_{seconds|millis|micros}
  Value(TypeKind type_kind, int64_t value);
  // REQUIRES: type_kind is string or bytes
  Value(TypeKind type_kind, std::string value);

  // Constructs a timestamp value.
  explicit Value(absl::Time t);

  // Constructs a timestamp_picos value.
  // TODO: Remove type_kind argument after TIMESTAMP_PICOS is no
  //                    longer used.
  ABSL_DEPRECATED("Used only to migrate from TIMESTAMP_PICOS to TIMESTAMP.")
  explicit Value(const TimestampPicosValue& t, TypeKind type_kind);

  // Constructs a TIME value.
  explicit Value(TimeValue time);

  // Constructs a DATETIME value.
  explicit Value(DatetimeValue datetime);

  // Constructs an INTERVAL value.
  explicit Value(const IntervalValue& interval);

  explicit Value(const NumericValue& numeric);

  explicit Value(const BigNumericValue& bignumeric);

  // Takes ownership of 'json_ptr' without increasing its ref count.
  explicit Value(internal::JSONRef* json_ptr);

  // Constructs a TOKENLIST value.
  explicit Value(tokens::TokenList tokenlist);

  explicit Value(const UuidValue& uuid);

  // Constructs an enum.
  Value(const EnumType* enum_type, int64_t value,
        bool allow_unknown_enum_values);
  Value(const EnumType* enum_type, absl::string_view name);

  // Constructs a proto.
  Value(const ProtoType* proto_type, absl::Cord value);

  // Constructs a value of extended type.
  Value(const ExtendedType* extended_type, const ValueContent& value);

  // Clears the contents of the value and makes it invalid. Must be called
  // exactly once prior to destruction or assignment.
  void Clear();

  // Copies the contents of a value from another value.
  void CopyFrom(const Value& that);

  // If an array has order_kind()=kIgnoresOrder, the array represents a
  // an unordered vector (aka multiset). This bit is used internally
  // by test code; public arrays are always ordered.
  bool order_kind() const;

  // Fills the DeepOrderKindSpec `spec` for a given Value `v`. `spec` is a
  // recursive tree structure which represents the array orderedness of
  // positions within `v`, for use in value comparisons.
  static void FillDeepOrderKindSpec(const Value& v, DeepOrderKindSpec* spec);

  static bool EqualsInternal(const Value& x, const Value& y, bool allow_bags,
                             const ValueEqualityCheckOptions& options);

  // Creates an array of the given 'array_type' initialized by moving from
  // 'values'.  The type of each value must be the same as
  // array_type->element_type(). This property is validated if
  // 'already_validated' is false or we are in debug mode.
  static absl::StatusOr<Value> MakeArrayInternal(
      bool already_validated, const ArrayType* array_type,
      OrderPreservationKind order_kind, std::vector<Value> values);

  // Creates a struct of the given 'struct_type' initialized by moving from
  // 'values'. Each value must have the proper type. This property is validated
  // if 'already_validated' is false or we are in debug mode.
  static absl::StatusOr<Value> MakeStructInternal(bool already_validated,
                                                  const StructType* struct_type,
                                                  std::vector<Value> values);

  // Creates a Range given 'start' and 'end' values. Each value must have the
  // proper type, and the 'start' value must be smaller than the 'end' value.
  // This property is validated if 'is_validated' is false or in debug mode.
  // 'range_type' is only provided when calling MakeRangeFromValidatedInputs.
  static absl::StatusOr<Value> MakeRangeInternal(
      bool is_validated, const Value& start, const Value& end,
      const RangeType* range_type = nullptr);

  // Builds a graph element (node or edge). See MakeGraphNode/Edge for more
  // explanation.
  // If duplicate property names are present:
  // - static property names take precedence over dynamic ones;
  // - among dynamic properties, the first name is returned (compared
  //   case-insensitively).
  static absl::StatusOr<Value> MakeGraphElement(
      const GraphElementType* graph_element_type,
      const GraphElementLabelsAndProperties& labels_and_properties,
      const GraphNodeKeysAndElementTableNames& node_data,
      const GraphEdgeKeysAndElementTableNames& edge_data
  );

#ifndef SWIG  // TODO: Investigate SWIG compatibility for MAP.

  // Creates a map of the given 'map_type' initialized with the entries in
  // 'map_entries'.
  // A map_entries containing duplicate keys will result in out of range error.
  // A map_entries containing values with types that do not match the provided
  //   type will result in system error, but only in debug mode.
  static absl::StatusOr<Value> MakeMapInternal(
      const Type* type,
      const std::vector<std::pair<Value, Value>>& map_entries);

  // Same as above, but creates the key/value pairs from a googlesql_base::MapView.
  static absl::StatusOr<Value> MakeMapInternal(
      const Type* type, googlesql_base::MapView<Value, Value> map_entries);

#endif

  // Returns a pretty-printed (e.g. wrapped) string for the value
  // indented a number of spaces according to the 'indent' parameter.
  // 'force_type' causes the top-level value to print its type. By
  // default, only Array values print their types.
  std::string FormatInternal(Type::FormatValueContentOptions options) const;

  // Type cannot create a list of Values because it cannot depend on
  // "value" package. Thus for Array/Struct/Range types that need list of
  // values, we will create them from Value directly.
  // Similarly for GraphElementType and GraphPath.
  // TODO: This can be avoided when we create virtual value list
  // interface which can be defined outside of "value", but Value provides its
  // implementation which it feeds to above types.
  bool DoesTypeUseValueList() const {
    return metadata_.type_kind() == TYPE_ARRAY ||
           metadata_.type_kind() == TYPE_STRUCT ||
           metadata_.type_kind() == TYPE_GRAPH_ELEMENT ||
           metadata_.type_kind() == TYPE_GRAPH_PATH ||
           metadata_.type_kind() == TYPE_RANGE;
  }

  // Type cannot create a list of Values because it cannot depend on
  // "value" package. Thus for Map type that needs a list of values, we
  // will create them from Value directly.
  bool DoesTypeUseValueMap() const { return metadata_.type_kind() == TYPE_MAP; }

  bool DoesTypeUseValueMeasure() const {
    return metadata_.type_kind() == TYPE_MEASURE;
  }

  // Gets Value's content. Requires: has_content() == true.
  ValueContent GetContent() const;

  // Sets the content of the Value and makes the Value non-null. Requires:
  // is_valid() == true.
  void SetContent(const ValueContent& content);

  // Nanoseconds for TYPE_TIMESTAMP, TYPE_TIME and TYPE_DATETIME types
  int32_t subsecond_nanos() const {
    ABSL_DCHECK(metadata_.can_store_value_extended_content());
    ABSL_DCHECK(metadata_.type_kind() == TypeKind::TYPE_TIMESTAMP ||
           metadata_.type_kind() == TypeKind::TYPE_TIME ||
           metadata_.type_kind() == TypeKind::TYPE_DATETIME);
    return metadata_.value_extended_content();
  }

  // Store a pointer to a Type and value flags in metadata. Requires: type must
  // not be a simple built-in type.
  void SetMetadataForNonSimpleType(const Type* type, bool is_null = false,
                                   bool preserves_order = kPreservesOrder);

  // Metadata is 8 bytes class which stores the following fields:
  //  1. 2 bit flags:
  //    1.1. is_null: specifies whether value is NULL
  //    1.2. preserves_order: used by GoogleSQL internally for array testing
  //  2. union (either one or another) of:
  //    2.1. type (Type*): pointer to a Value's Type OR
  //    2.2. struct with fields:
  //    2.2.1. kind (16 bits TypeKind): kind of a built-in type
  //    2.2.2. value_extended_content (int32): 4 bytes that simple built-in
  //     types can use to store arbitrary Value related information in addition
  //     to main 64-bit Value's part. This field is currently used to store
  //     nanoseconds for TYPE_TIMESTAMP, TYPE_TIME, TYPE_DATETIME types and
  //     value for ENUM type (pointer to enum is stored in 64-bit part).
  //
  // As can be seen, Metadata can store either TypeKind or a pointer to a Type
  // directly. In the first case, it also can store 32-bit Value's part called
  // value_extended_content. Currently we store TypeKind for all simple built-in
  // types and store a pointer for all other types.
  class Metadata {
   public:
    // Returns true if instance is valid: was initialized and references a valid
    // Type.
    bool is_valid() const;

    // Returns a kind of Value's type.
    TypeKind type_kind() const;

    // Returns a pointer to Value's Type. Requires is_valid(). If TypeKind is
    // stored in the Metadata, Type pointer is obtained from static TypeFactory.
    const Type* type() const;

    // Returns true, if instance stores pointer to a Type and false if type's
    // kind.
    bool has_type_pointer() const;

    // Returns true, if instance has space for 32-bit value extended content. It
    // can be the case only if we store TypeKind and thus has_type_pointer() ==
    // false.
    bool can_store_value_extended_content() const;

    // True, if Value is null.
    bool is_null() const;

    // This bit is used internally by test code to represent unordered arrays;
    // public arrays are always ordered.
    bool preserves_order() const;

    // 32-bit value that can be used by simple built-in types. Requires
    // can_store_value_extended_content() == true.
    int32_t value_extended_content() const;

    Metadata(const Type* type, bool is_null, bool preserves_order);

    constexpr Metadata(TypeKind kind, bool is_null, bool preserves_order,
                       int32_t value_extended_content);

    // Metadata for non-null Value with preserves_order = kPreservesOrder and
    // value_extended_content = 0.
#ifndef SWIG
    // SWIG has trouble with constexpr.
    constexpr
#endif
        explicit Metadata(TypeKind kind)
        : Metadata(kind, /*is_null=*/false, kPreservesOrder,
                   /*value_extended_content=*/0) {
    }

    // Metadata for non-null Value with preserves_order = kPreservesOrder.
    constexpr Metadata(TypeKind kind, int32_t value_extended_content)
        : Metadata(kind, /*is_null=*/false, kPreservesOrder,
                   value_extended_content) {}

    Metadata(const Metadata& that) = default;
    Metadata& operator=(const Metadata& that) = default;

    static constexpr Metadata Invalid() {
      return Metadata(static_cast<TypeKind>(kInvalidTypeKind));
    }

   private:
    class Content;

    Content* content();
    const Content* content() const;

    // We use int64 instead of Content here, so we don't need to expose
    // Content definition into value.h header file.
    int64_t data_{0};
  };

  // 64-bit field, which stores Value's type, value's flags and 32-bit part of
  // the value that is available for simple types only.
  Metadata metadata_ = Metadata::Invalid();

  // 64-bit part of the value.
  union {
    int64_t int64_value_ = 0;  // also seconds|millis|micros since 1970-1-1.
    int32_t int32_value_;      // also date
    uint64_t uint64_value_;
    uint32_t uint32_value_;
    bool bool_value_;
    float float_value_;
    double double_value_;
    int64_t timestamp_seconds_;   // Same as google.protobuf.Timestamp.seconds.
    int32_t bit_field_32_value_;  // Whole-second part of TimeValue.
    int64_t bit_field_64_value_;  // Whole-second part of DatetimeValue.
    int32_t enum_value_;          // Used for TYPE_ENUM.
    internal::StringRef*
        string_ptr_;  // Reffed. Used for TYPE_STRING and TYPE_BYTES.
    internal::ValueContentOrderedListRef*
        container_ptr_;  // Reffed. Used for arrays, structs, and RANGE.
    internal::ProtoRep* proto_ptr_;          // Reffed. Used for protos.
    internal::GeographyRef* geography_ptr_;  // Owned. Used for geographies.
    internal::NumericRef*
        numeric_ptr_;  // Owned. Used for values of TYPE_NUMERIC.
    internal::BigNumericRef*
        bignumeric_ptr_;  // Owned. Used for values of TYPE_BIGNUMERIC.
    internal::TimestampPicosRef*
        timestamp_picos_ptr_;      // Owned. Used for values of TYPE_TIMESTAMP.
    internal::JSONRef* json_ptr_;  // Owned. Used for values of TYPE_JSON.
    internal::IntervalRef*
        interval_ptr_;  // Owned. Used for values of TYPE_INTERVAL.
    internal::TokenListRef*
        tokenlist_ptr_;  // Owned. Used for values of TYPE_TOKENLIST.
    internal::ValueContentMapRef*
        map_ptr_;                  // Owned. Used for values of TYPE_MAP.
    internal::UuidRef* uuid_ptr_;  // Owned. Used for values of TYPE_UUID.
    internal::ValueContentMeasureRef*
        measure_ptr_;  // Owned. Used for values of TYPE_MEASURE.
  };
  // Intentionally copyable.
};

#ifndef SWIG
static_assert(sizeof(Value) == sizeof(int64_t) * 2, "Value size mismatch");
#endif

// Allow Value to be logged.
std::ostream& operator<<(std::ostream& out, const Value& value);

namespace values {

// Constructors below wrap the respective static methods in Value class. See
// comments there for details.

// Constructors for non-null atomic values.
Value Int64(int64_t v);
Value Int32(int32_t v);
Value Uint64(uint64_t v);
Value Uint32(uint32_t v);
Value Bool(bool v);
Value Float(float v);
Value Double(double v);
Value String(absl::string_view v);
Value String(const absl::Cord& v);
// str may contain '\0' in the middle, without getting truncated.
template <size_t N>
Value String(const char (&str)[N]);
Value Bytes(absl::string_view v);
Value Bytes(const absl::Cord& v);
// str may contain '\0' in the middle, without getting truncated.
template <size_t N>
Value Bytes(const char (&str)[N]);
Value Date(int32_t v);
Value Date(absl::CivilDay d);
Value Timestamp(absl::Time t);
Value Timestamp(TimestampPicosValue t);
Value TimestampFromUnixMicros(int64_t v);

Value Time(TimeValue time);
Value TimeFromPacked64Micros(int64_t v);
Value Datetime(DatetimeValue datetime);
Value DatetimeFromPacked64Micros(int64_t v);
Value Interval(IntervalValue interval);
Value Numeric(NumericValue v);
Value Numeric(int64_t v);
Value BigNumeric(BigNumericValue v);
Value BigNumeric(int64_t v);
Value Enum(const EnumType* enum_type, int32_t value,
           bool allow_unnamed_values = true);
Value Enum(const EnumType* enum_type, absl::string_view name);
Value Struct(const StructType* type, absl::Span<const Value> values);
#ifndef SWIG
Value UnsafeStruct(const StructType* type, std::vector<Value>&& values);
#endif
Value Uuid(UuidValue v);
Value Proto(const ProtoType* proto_type, absl::Cord value);
Value Proto(const ProtoType* proto_type, const google::protobuf::Message& msg);
Value EmptyArray(const ArrayType* type);
Value Array(const ArrayType* type, absl::Span<const Value> values);
#ifndef SWIG
Value UnsafeArray(const ArrayType* type, std::vector<Value>&& values);
#endif
Value True();
Value False();

// Constructors for null values.
Value NullInt64();
Value NullInt32();
Value NullUint64();
Value NullUint32();
Value NullBool();
Value NullFloat();
Value NullDouble();
Value NullString();
Value NullBytes();
Value NullDate();
Value NullTimestamp();
Value NullTime();
Value NullDatetime();
Value NullInterval();
Value NullNumeric();
Value NullBigNumeric();
Value NullUuid();
Value Null(const Type* type);

// Constructor for an invalid value.
Value Invalid();

// Array constructors.
Value Int64Array(absl::Span<const int64_t> values);
Value Int32Array(absl::Span<const int32_t> values);
Value Uint64Array(absl::Span<const uint64_t> values);
Value Uint32Array(absl::Span<const uint32_t> values);
Value BoolArray(const std::vector<bool>& values);
Value FloatArray(absl::Span<const float> values);
Value DoubleArray(absl::Span<const double> values);
Value StringArray(absl::Span<const std::string> values);
// Does not take ownership of Cord* values.
Value StringArray(absl::Span<const absl::Cord* const> values);
Value BytesArray(absl::Span<const std::string> values);
// Does not take ownership of Cord* values.
Value BytesArray(absl::Span<const absl::Cord* const> values);
Value NumericArray(absl::Span<const NumericValue> values);
Value BigNumericArray(absl::Span<const BigNumericValue> values);
Value JsonArray(absl::Span<const JSONValue> values);
// 'values' are JSON values (e.g. '{"a": 10}') and not literal strings values.
Value UnvalidatedJsonStringArray(absl::Span<const std::string> values);
Value TimestampArray(absl::Span<const absl::Time> values);
Value TimestampArray(absl::Span<const TimestampPicosValue> values);
Value DateArray(absl::Span<const absl::CivilDay> values);

}  // namespace values
}  // namespace googlesql

// Include the implementations of the inline methods. Out of line for
// clarity. Is not intended to be read by users.
#include "googlesql/public/value_inl.h"  

#endif  // GOOGLESQL_PUBLIC_VALUE_H_
