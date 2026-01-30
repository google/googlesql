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

#ifndef GOOGLESQL_TESTING_TEST_VALUE_H_
#define GOOGLESQL_TESTING_TEST_VALUE_H_

#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "googlesql/common/float_margin.h"
#include "googlesql/common/internal_value.h"
#include "googlesql/compliance/test_driver.h"
#include "googlesql/public/json_value.h"
#include "googlesql/public/numeric_value.h"
#include "googlesql/public/timestamp_picos_value.h"
#include "googlesql/public/type.h"
#include "googlesql/public/types/graph_element_type.h"
#include "googlesql/public/types/value_equality_check_options.h"
#include "googlesql/public/value.h"
#include "gmock/gmock.h"
#include "googlesql/base/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "google/protobuf/descriptor.h"
#include "googlesql/base/status.h"

namespace googlesql {

// Supports implicit conversion of C++ types to Values.
class ValueConstructor {
 public:
  // Very roundabout way of saying 'int32' in a way that's portable.
  template <typename Int32T,
            typename std::enable_if<std::is_integral<Int32T>::value &&
                                    std::is_signed<Int32T>::value &&
                                    sizeof(Int32T) == 4>::type* = nullptr>
  ValueConstructor(Int32T v)  // NOLINT(google-explicit-constructor)
      : v_(Value::Int32(v)) {}

  // Very roundabout way of saying 'uint32' in a way that's portable.
  template <typename UInt32T,
            typename std::enable_if<std::is_integral<UInt32T>::value &&
                                    std::is_unsigned<UInt32T>::value &&
                                    sizeof(UInt32T) == 4>::type* = nullptr>
  ValueConstructor(UInt32T v)  // NOLINT(google-explicit-constructor)
      : v_(Value::Uint32(v)) {}

  // Very roundabout way of saying 'int64' in a way that's portable.
  template <typename Int64T,
            typename std::enable_if<std::is_integral<Int64T>::value &&
                                    std::is_signed<Int64T>::value &&
                                    sizeof(Int64T) == 8>::type* = nullptr>
  ValueConstructor(Int64T v)  // NOLINT(google-explicit-constructor)
      : v_(Value::Int64(v)) {}

  // Very roundabout way of saying 'uint64' in a way that's portable.
  template <typename UInt64T,
            typename std::enable_if<std::is_integral<UInt64T>::value &&
                                    std::is_unsigned<UInt64T>::value &&
                                    sizeof(UInt64T) == 8>::type* = nullptr>
  ValueConstructor(UInt64T v)  // NOLINT(google-explicit-constructor)
      : v_(Value::Uint64(v)) {}

  ValueConstructor(float v)  // NOLINT(google-explicit-constructor)
      : v_(Value::Float(v)) {}
  ValueConstructor(double v)  // NOLINT(google-explicit-constructor)
      : v_(Value::Double(v)) {}
  ValueConstructor(bool v)  // NOLINT(google-explicit-constructor)
      : v_(Value::Bool(v)) {}
  ValueConstructor(NumericValue v)  // NOLINT(google-explicit-constructor)
      : v_(Value::Numeric(v)) {}
  ValueConstructor(BigNumericValue v)  // NOLINT(google-explicit-constructor)
      : v_(Value::BigNumeric(v)) {}

  // Forward all other types to Value::String.
  // This is necessary for handling const char* and const char (str&)[N]
  // correctly, where str might contain '\0' in the middle.
  // If we define both ValueConstructor(const char*) and
  // ValueConstructor(const char (str&)[N]), then ValueConstructor("") is
  // ambiguous; if we define ValueConstructor(const char (str&)[N]) only,
  // then ValueConstructor(bool) will be called for const char*.
  template <
      typename T,
      typename std::enable_if<!std::is_integral<T>::value>::type* = nullptr>
  ValueConstructor(  // NOLINT(google-explicit-constructor)
      const T& string_type_value)
      : v_(Value::String(string_type_value)) {}
  ValueConstructor(const Value& v)  // NOLINT(google-explicit-constructor)
      : v_(v) {}

  const Value& get() const { return v_; }
  const Type* type() const { return v_.type(); }

  static std::vector<Value> ToValues(absl::Span<const ValueConstructor> slice) {
    std::vector<Value> values;
    for (const auto& v : slice) {
      values.push_back(v.get());
    }
    return values;
  }

  static std::vector<ValueConstructor> FromValues(
      absl::Span<const Value> slice) {
    std::vector<ValueConstructor> ret;
    for (const auto& v : slice) {
      ret.emplace_back(v);
    }
    return ret;
  }

 private:
  const Value v_;
  // Intentionally copyable.
};

namespace internal {
template <class T>
inline absl::StatusOr<Value> GetStatusOrValue(T& arg);

template <>
inline absl::StatusOr<Value> GetStatusOrValue(
    const absl::StatusOr<Value>& arg) {
  return arg;
}

template <>
inline absl::StatusOr<Value> GetStatusOrValue(const Value& arg) {
  return arg;
}

template <>
inline absl::StatusOr<Value> GetStatusOrValue(
    const absl::StatusOr<std::variant<Value, MultiStmtResult>>& arg) {
  if (!arg.ok()) {
    return arg.status();
  }
  ABSL_CHECK(std::holds_alternative<Value>(arg.value()));
  return absl::StatusOr<Value>(std::get<Value>(arg.value()));
}
}  // namespace internal

// Matches StatusOr<Value> or Value against 'expected_value' respecting array
// orderedness.
//
// Accepts either Value or ComplianceTestCaseResult as the argument. If a
// ComplianceTestCaseResult is supplied, it must hold a Value, not a
// ScriptResult.
MATCHER_P(EqualsValue, expected_value, "") {
  absl::StatusOr<Value> result = internal::GetStatusOrValue(arg);
  if (!result.ok()) {
    return false;
  }
  const Value& value = result.value();
  std::string reason;
  if (!InternalValue::Equals(
          expected_value, value,
          ValueEqualityCheckOptions{
              .interval_compare_mode = IntervalCompareMode::kAllPartsEqual,
              .float_margin = kExactFloatMargin,
              .reason = &reason})) {
    *result_listener << reason;
    return false;
  }
  return true;
}

// Matches StatusOr<Value> or Value against 'expected_value' respecting array
// orderedness and using the default floating point error margin.
MATCHER_P(AlmostEqualsValue, expected_value, "") {
  absl::StatusOr<Value> result = arg;
  if (!result.ok()) {
    return false;
  }
  const Value& value = result.value();
  std::string reason;
  if (!InternalValue::Equals(
          expected_value, value,
          ValueEqualityCheckOptions{.float_margin = kDefaultFloatMargin,
                                    .reason = &reason})) {
    *result_listener << reason;
    return false;
  }
  return true;
}

// Matches StatusOr<Value> against 'error_substring'.
MATCHER_P(ReturnsNoValue, error_substring, "") {
  absl::StatusOr<Value> result = internal::GetStatusOrValue(arg);
  if (result.ok()) {
    return false;
  }
  if (!absl::StrContains(result.status().ToString(), error_substring)) {
    return false;
  }
  return true;
}

namespace test_values {

// Declare the constants below in a namespace so they can be included via "using
// test_value::kPreservesOrder" etc.
typedef InternalValue::OrderPreservationKind OrderPreservationKind;
static const OrderPreservationKind kPreservesOrder =
    InternalValue::kPreservesOrder;
static const OrderPreservationKind kIgnoresOrder = InternalValue::kIgnoresOrder;

// Default static type factory. It accumulates types throughout the lifetime of
// a test.
TypeFactory* static_type_factory();

// The following functions create new types using the static factory and leak
// memory. These functions generally take an optional type factory, and if
// the type factory is not provided then the default static type factory is
// used (see: static_type_factory()). Be careful if the static type factory
// is used, since the static type factory will become dependent on the type
// factory that owns any argument Types and therefore the other type factory
// cannot be safely destructed before this static type factory.

// Creates a struct. 'names' contains the field names and 'values' contains
// the values. The vectors must be non-empty and agree on the size. The type
// of the struct is computed from 'names' and types of 'values'. If type_factory
// is not provided the function will use the default static type factory (see:
// static_type_factory()).
Value Struct(absl::Span<const std::string> names,
             absl::Span<const ValueConstructor> values,
             TypeFactory* type_factory = nullptr);
// Creates a struct of anonymous type from (field name, value) pairs. If
// type_factory is not provided the function will use the default static type
// factory (see: static_type_factory()).
Value Struct(absl::Span<const std::pair<std::string, Value>> pairs,
             TypeFactory* type_factory = nullptr);

// Creates an array of 'values'. Array type is derived from the values.
// All values must agree on the type. If type_factory is not provided the
// function will use the default static type factory (see:
// static_type_factory()).
// NOTE: If providing a type factory explicitly, the order_kind must also be
// provided to avoid pointer coercion to bool.
Value Array(absl::Span<const ValueConstructor> values,
            OrderPreservationKind order_kind = kPreservesOrder,
            TypeFactory* type_factory = nullptr);

// Creates an array of struct 'values'. Array type is derived from 'names' and
// the values in 'structs'. All vectors of values must agree on the type. If
// type_factory is not provided the function will use the default static type
// factory (see: static_type_factory()).
// NOTE: If providing a type factory explicitly, the order_kind must also be
// provided to avoid pointer coercion to bool.
Value StructArray(absl::Span<const std::string> names,
                  absl::Span<const std::vector<ValueConstructor>> structs,
                  OrderPreservationKind order_kind = kPreservesOrder,
                  TypeFactory* type_factory = nullptr);

// Creates a range with values 'start' and 'end'.
Value Range(ValueConstructor start, ValueConstructor end);

// Creates a static graph node with identifier, definition name, static
// properties and static labels for graph identified by graph_reference.
absl::StatusOr<Value> GraphNode(
    absl::Span<const std::string> graph_reference, absl::string_view identifier,
    absl::Span<const std::pair<std::string, Value>> static_properties,
    absl::Span<const std::string> static_labels,
    absl::string_view element_table_name, TypeFactory* type_factory = nullptr);

// Creates a static graph edge with identifier, definition name, static
// properties, static labels, source and destination node identifiers for graph
// identified by graph_reference.
absl::StatusOr<Value> GraphEdge(
    absl::Span<const std::string> graph_reference, absl::string_view identifier,
    absl::Span<const std::pair<std::string, Value>> static_properties,
    absl::Span<const std::string> static_labels,
    absl::string_view element_table_name,
    absl::string_view source_node_identifier,
    absl::string_view dest_node_identifier,
    TypeFactory* type_factory = nullptr);

// Creates a dynamic graph node with identifier, definition name, static
// properties, dynamic properties, static labels, and dynamic labels for graph
// identified by graph_reference.
Value DynamicGraphNode(
    absl::Span<const std::string> graph_reference, absl::string_view identifier,
    absl::Span<const std::pair<std::string, Value>> static_properties,
    JSONValueConstRef dynamic_properties,
    absl::Span<const std::string> static_labels,
    absl::Span<const std::string> dynamic_labels,
    absl::string_view element_table_name, TypeFactory* type_factory = nullptr);

// Creates a dynamic graph edge with identifier, definition name, static
// properties, dynamic properties, static labels, and dynamic labels, source and
// destination node identifiers for graph identified by graph_reference.
Value DynamicGraphEdge(
    absl::Span<const std::string> graph_reference, absl::string_view identifier,
    absl::Span<const std::pair<std::string, Value>> static_properties,
    JSONValueConstRef dynamic_properties,
    absl::Span<const std::string> static_labels,
    absl::Span<const std::string> dynamic_labels,
    absl::string_view element_table_name,
    absl::string_view source_node_identifier,
    absl::string_view dest_node_identifier,
    TypeFactory* type_factory = nullptr);

// Creates a map with key/value pairs from 'elements'.
Value Map(
    absl::Span<const std::pair<ValueConstructor, ValueConstructor>> elements,
    TypeFactory* type_factory = nullptr);

// If type_factory is not provided the function will use the default static type
// factory (see: static_type_factory())
const ArrayType* MakeArrayType(const Type* element_type,
                               TypeFactory* type_factory = nullptr);

// TODO: jmorcos - These functions are dangerous if used in the compliance flows
// due to the type factory differences.
// If type_factory is not provided the function will use the default static type
// factory (see: static_type_factory())
const StructType* MakeStructType(
    absl::Span<const StructType::StructField> fields,
    TypeFactory* type_factory = nullptr);

// If type_factory is not provided the function will use the default static type
// factory (see: static_type_factory())
const ProtoType* MakeProtoType(const google::protobuf::Descriptor* descriptor,
                               TypeFactory* type_factory = nullptr);

// If type_factory is not provided the function will use the default static type
// factory (see: static_type_factory())
const EnumType* MakeEnumType(const google::protobuf::EnumDescriptor* descriptor,
                             TypeFactory* type_factory = nullptr);

// If type_factory is not provided the function will use the default static type
// factory (see: static_type_factory())
const RangeType* MakeRangeType(const Type* element_type,
                               TypeFactory* type_factory = nullptr);

// Creates a static graph element type.
// If type_factory is not provided the function will use the default static type
// factory (see: static_type_factory())
const GraphElementType* MakeGraphElementType(
    absl::Span<const std::string> graph_reference,
    GraphElementType::ElementKind element_kind,
    absl::Span<const GraphElementType::PropertyType> property_types,
    TypeFactory* type_factory = nullptr);

// Creates a dynamic graph element type.
// If type_factory is not provided the function will use the default static type
// factory (see: static_type_factory())
const GraphElementType* MakeDynamicGraphElementType(
    absl::Span<const std::string> graph_reference,
    GraphElementType::ElementKind element_kind,
    absl::Span<const GraphElementType::PropertyType> property_types,
    TypeFactory* type_factory = nullptr);

// If type_factory is not provided the function will use the default static type
// factory (see: static_type_factory())
const GraphPathType* MakeGraphPathType(const GraphElementType* node_type,
                                       const GraphElementType* edge_type,
                                       TypeFactory* type_factory = nullptr);

// If type_factory is not provided the function will use the default static type
// factory (see: static_type_factory())
absl::StatusOr<const Type*> MakeMapType(const Type* key_type,
                                        const Type* value_type,
                                        TypeFactory* type_factory = nullptr);

// Matches x against y respecting array orderedness and using the default
// floating point error margin. If the reason parameter is nullptr then no
// mismatch diagnostic string will be populated.
bool AlmostEqualsValue(const Value& x, const Value& y, std::string* reason);

}  // namespace test_values

}  // namespace googlesql

#endif  // GOOGLESQL_TESTING_TEST_VALUE_H_
