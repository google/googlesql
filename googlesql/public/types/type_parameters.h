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

#ifndef GOOGLESQL_PUBLIC_TYPES_TYPE_PARAMETERS_H_
#define GOOGLESQL_PUBLIC_TYPES_TYPE_PARAMETERS_H_

#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/public/type_parameters.pb.h"
#include "googlesql/public/types/simple_value.h"
#include "googlesql/public/types/type.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"

namespace googlesql {

// Type parameters for extension types, which are represented
// by List<googlesql::Value> to allow maximum flexibility. Implementations can
// interpret the semantic of these type parameters.
class ExtendedTypeParameters {
 public:
  explicit ExtendedTypeParameters(std::vector<SimpleValue> parameters)
      : parameters_(std::move(parameters)) {}

  uint64_t num_parameters() const { return parameters_.size(); }
  const SimpleValue& parameter(int i) const { return parameters_[i]; }

  std::string DebugString() const;
  absl::Status Serialize(ExtendedTypeParametersProto* proto) const;
  static absl::StatusOr<ExtendedTypeParameters> Deserialize(
      const ExtendedTypeParametersProto& proto);
  bool Equals(const ExtendedTypeParameters& that) const;

 private:
  std::vector<SimpleValue> parameters_;
};

// Type parameters for erasable types (parameterized types), for example
// STRING(L) is an erasable string type with length limit. L is the type
// parameter of STRING(L) type.
class TypeParameters {
 public:
  // Constructs empty type parameters for types without parameters. Default
  // constructor must be public to be used in the ResolvedAST.
  TypeParameters();

  // Constructs type parameters for STRING(L) or BYTES(L) type.
  static absl::StatusOr<TypeParameters> MakeStringTypeParameters(
      const StringTypeParametersProto& string_type_parameters);

  // Constructs type parameters for NUMERIC(P[,S]) or BIGNUMERIC(P[,S]) type.
  static absl::StatusOr<TypeParameters> MakeNumericTypeParameters(
      const NumericTypeParametersProto& numeric_type_parameters);

  // Constructs type parameters for TIMESTAMP(P) type.
  static absl::StatusOr<TypeParameters> MakeTimestampTypeParameters(
      const TimestampTypeParametersProto& timestamp_type_parameters);

  // Constructs type parameters for extended type. <child_list> is optional;
  // if present, it stores sub-fields for extended type.
  static TypeParameters MakeExtendedTypeParameters(
      const ExtendedTypeParameters& extended_type_parameters,
      std::vector<TypeParameters> child_list = std::vector<TypeParameters>());

  // Constructs type parameters for STRUCT or ARRAY type.
  static TypeParameters MakeTypeParametersWithChildList(
      std::vector<TypeParameters> child_list);

  static absl::Status ValidateStringTypeParameters(
      const StringTypeParametersProto& string_type_parameters);

  static absl::Status ValidateNumericTypeParameters(
      const NumericTypeParametersProto& numeric_type_parameters);

  static absl::Status ValidateTimestampTypeParameters(
      const TimestampTypeParametersProto& timestamp_type_parameters);

  // Returns whether <type> matches this type parameter instance.
  // For example, StringTypeParameters only matches STRING and BYTES type. For
  // STRUCT/ARRAY/RANGE types, this function recursively checks if the subfields
  // match the corresponding type parameters. Note: a TypeParameters object with
  // one child can be applicable to ARRAY or RANGE types, or single-field STRUCT
  // type. An empty TypeParameters object matches any <type> and always returns
  // true.
  bool MatchType(const Type* type) const;

  // Returns true if type parameter is empty and has no children. Empty type
  // parameter is used as placeholder for type without parameters. E.g. in
  // STRUCT<INT64, STRING(10)>, the type parameter for INT64 is empty.
  bool IsEmpty() const {
    return std::holds_alternative<std::monostate>(type_parameters_holder_) &&
           child_list().empty();
  }
  bool IsStringTypeParameters() const {
    return std::holds_alternative<StringTypeParametersProto>(
        type_parameters_holder_);
  }
  bool IsTimestampTypeParameters() const {
    return std::holds_alternative<TimestampTypeParametersProto>(
        type_parameters_holder_);
  }
  bool IsNumericTypeParameters() const {
    return std::holds_alternative<NumericTypeParametersProto>(
        type_parameters_holder_);
  }
  bool IsExtendedTypeParameters() const {
    return std::holds_alternative<ExtendedTypeParameters>(
        type_parameters_holder_);
  }
  const StringTypeParametersProto& string_type_parameters() const {
    ABSL_CHECK(IsStringTypeParameters()) << "Not STRING type parameters";
    return std::get<StringTypeParametersProto>(type_parameters_holder_);
  }
  const TimestampTypeParametersProto& timestamp_type_parameters() const {
    ABSL_CHECK(IsTimestampTypeParameters()) << "Not TIMESTAMP type parameters";
    return std::get<TimestampTypeParametersProto>(type_parameters_holder_);
  }
  const NumericTypeParametersProto& numeric_type_parameters() const {
    ABSL_CHECK(IsNumericTypeParameters()) << "Not NUMERIC type parameters";
    return std::get<NumericTypeParametersProto>(type_parameters_holder_);
  }
  const ExtendedTypeParameters& extended_type_parameters() const {
    ABSL_CHECK(IsExtendedTypeParameters()) << "Not EXTENDED type parameters";
    return std::get<ExtendedTypeParameters>(type_parameters_holder_);
  }

  // Returns type parameters for subfields for composite types, e.g. ARRAY,
  // STRUCT, ... etc.
  // For example, for ARRAY and RANGE:
  //   If the element or its subfield has type parameters, then
  //   child_list.size() is 1, and child_list(0) is the element type parameters.
  //   Otherwise child_list is empty.
  // For STRUCT:
  //   If the i-th field has type parameters then child_list(i) is the field
  //   type parameters.
  //   If the i-th field has no type parameters, then child_list[i] contains an
  //   empty TypeParameter. The size of the child_list is equal to either the
  //   number of fields or 0.
  //   If none of the fields and none of their subfields has type parameters,
  //   then child_list is empty.
  // Other composite types operate similarly.
  // For other types, child_list is empty.
  const std::vector<TypeParameters>& child_list() const { return child_list_; }
  const TypeParameters& child(int i) const { return child_list_[i]; }
  uint64_t num_children() const { return child_list_.size(); }

  // Sets the child_list of a TypeParameters object to <child_list>, creating an
  // Array, Struct, or Range TypeParameters object. This function should only be
  // used with an empty TypeParameters object.
  void set_child_list(std::vector<TypeParameters> child_list);

  absl::Status Serialize(TypeParametersProto* proto) const;
  absl::StatusOr<std::string> SerializeAsString() const;
  static absl::StatusOr<TypeParameters> Deserialize(
      const TypeParametersProto& proto);
  std::string DebugString() const;
  bool Equals(const TypeParameters& that) const;

 private:
  explicit TypeParameters(const StringTypeParametersProto& string_parameters);
  explicit TypeParameters(const NumericTypeParametersProto& numeric_parameters);
  explicit TypeParameters(
      const TimestampTypeParametersProto& timestamp_parameters);
  TypeParameters(const ExtendedTypeParameters& extended_parameters,
                 std::vector<TypeParameters> child_list);
  explicit TypeParameters(std::vector<TypeParameters> child_list);

  // Default value is the 1st type (std::monostate), meaning the type parameter
  // is empty.
  std::variant<std::monostate, StringTypeParametersProto,
               NumericTypeParametersProto, TimestampTypeParametersProto,
               ExtendedTypeParameters>
      type_parameters_holder_;
  // Stores type parameters for subfields for ARRAY, STRUCT, or RANGE types
  std::vector<TypeParameters> child_list_;
};

// This class serves as a wrapper around SimpleValues to allow for special
// literals to be represented as type parameters (eg. MAX).
//
// When a type parameter is an integer/string literal, googlesql::SimpleValue
// can store it directly.
//
// When a type parameter is a special literal, e.g. in STRING(MAX), the literal
// is stored as a TypeParametersLiteral enum.
class TypeParameterValue {
 public:
  enum TypeParametersLiteral { kNonSpecialLiteral = 0, kMaxLiteral = 1 };

  explicit TypeParameterValue(SimpleValue value_in)
      : value_(value_in), literal_(kNonSpecialLiteral) {}
  explicit TypeParameterValue(TypeParametersLiteral literal_in)
      : literal_(literal_in) {
    ABSL_CHECK(IsSpecialLiteral()) << "TypeParameterValue cannot be constructed "
                                 "with a non-special literal";
  }

  SimpleValue GetValue() const { return value_; }
  TypeParametersLiteral GetSpecialLiteral() const { return literal_; }
  bool IsSpecialLiteral() const { return literal_ != kNonSpecialLiteral; }

 private:
  SimpleValue value_;
  TypeParametersLiteral literal_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_TYPES_TYPE_PARAMETERS_H_
