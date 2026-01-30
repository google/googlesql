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

#include "googlesql/public/types/type_parameters.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "google/protobuf/util/message_differencer.h"
#include "googlesql/public/functions/rounding_mode.pb.h"
#include "googlesql/public/simple_value.pb.h"
#include "googlesql/public/type_parameters.pb.h"
#include "googlesql/public/types/array_type.h"
#include "googlesql/public/types/range_type.h"
#include "googlesql/public/types/simple_value.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/substitute.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

namespace {

std::string StringTypeParametersDebugString(
    const StringTypeParametersProto& string_type_parameters) {
  if (string_type_parameters.is_max_length()) {
    return "(max_length=MAX)";
  }
  return absl::Substitute("(max_length=$0)",
                          string_type_parameters.max_length());
}

std::string NumericTypeParametersDebugString(
    const NumericTypeParametersProto& parameters) {
  std::string precision = parameters.is_max_precision()
                              ? "MAX"
                              : std::to_string(parameters.precision());
  int64_t scale = parameters.scale();
  return absl::Substitute("(precision=$0,scale=$1)", precision, scale);
}

std::string TimestampTypeParametersDebugString(
    const TimestampTypeParametersProto& parameters) {
  return absl::Substitute("(precision=$0)", parameters.precision());
}

std::string ChildTypeParametersDebugString(
    const std::vector<TypeParameters>& child_list) {
  return absl::StrCat(
      "[",
      absl::StrJoin(child_list, ",",
                    [](std::string* out, const TypeParameters& type_param) {
                      absl::StrAppend(out, type_param.DebugString());
                    }),
      "]");
}

}  // namespace

std::string TypeParameters::DebugString() const {
  if (IsStringTypeParameters()) {
    return StringTypeParametersDebugString(string_type_parameters());
  }
  if (IsNumericTypeParameters()) {
    return NumericTypeParametersDebugString(numeric_type_parameters());
  }
  if (IsTimestampTypeParameters()) {
    return TimestampTypeParametersDebugString(timestamp_type_parameters());
  }

  // Extended type may has child_list.
  std::string debug_string;
  if (IsExtendedTypeParameters()) {
    absl::StrAppend(&debug_string, extended_type_parameters().DebugString());
  }
  if (!child_list().empty()) {
    // STRUCT, ARRAY, RANGE type whose subfields has parameters.
    absl::StrAppend(&debug_string,
                    ChildTypeParametersDebugString(child_list()));
  }
  if (!debug_string.empty()) {
    return debug_string;
  }
  // Type without parameters, usually are subfields in a struct or array.
  return "null";
}

TypeParameters::TypeParameters(
    const StringTypeParametersProto& string_parameters)
    : type_parameters_holder_(string_parameters) {}
TypeParameters::TypeParameters(
    const NumericTypeParametersProto& numeric_parameters)
    : type_parameters_holder_(numeric_parameters) {}
TypeParameters::TypeParameters(
    const TimestampTypeParametersProto& timestamp_parameters)
    : type_parameters_holder_(timestamp_parameters) {}
TypeParameters::TypeParameters(
    const ExtendedTypeParameters& extended_parameters,
    std::vector<TypeParameters> child_list)
    : type_parameters_holder_(extended_parameters),
      child_list_(std::move(child_list)) {}
TypeParameters::TypeParameters(std::vector<TypeParameters> child_list)
    : child_list_(std::move(child_list)) {}
TypeParameters::TypeParameters() = default;

absl::StatusOr<TypeParameters> TypeParameters::MakeStringTypeParameters(
    const StringTypeParametersProto& string_type_parameters) {
  GOOGLESQL_RETURN_IF_ERROR(ValidateStringTypeParameters(string_type_parameters));
  return TypeParameters(string_type_parameters);
}
absl::StatusOr<TypeParameters> TypeParameters::MakeNumericTypeParameters(
    const NumericTypeParametersProto& numeric_type_parameters) {
  GOOGLESQL_RETURN_IF_ERROR(ValidateNumericTypeParameters(numeric_type_parameters));
  return TypeParameters(numeric_type_parameters);
}
absl::StatusOr<TypeParameters> TypeParameters::MakeTimestampTypeParameters(
    const TimestampTypeParametersProto& timestamp_type_parameters) {
  GOOGLESQL_RETURN_IF_ERROR(ValidateTimestampTypeParameters(timestamp_type_parameters));
  return TypeParameters(timestamp_type_parameters);
}

TypeParameters TypeParameters::MakeExtendedTypeParameters(
    const ExtendedTypeParameters& extended_type_parameters,
    std::vector<TypeParameters> child_list) {
  return TypeParameters(extended_type_parameters, std::move(child_list));
}
TypeParameters TypeParameters::MakeTypeParametersWithChildList(
    std::vector<TypeParameters> child_list) {
  return TypeParameters(std::move(child_list));
}

absl::Status TypeParameters::ValidateStringTypeParameters(
    const StringTypeParametersProto& string_type_parameters) {
  if (string_type_parameters.has_is_max_length()) {
    GOOGLESQL_RET_CHECK(string_type_parameters.is_max_length())
        << "is_max_length should either be unset or true";
  } else {
    GOOGLESQL_RET_CHECK_GT(string_type_parameters.max_length(), 0)
        << "max_length must be larger than 0, actual max_length: "
        << string_type_parameters.max_length();
  }
  return absl::OkStatus();
}

absl::Status TypeParameters::ValidateTimestampTypeParameters(
    const TimestampTypeParametersProto& timestamp_type_parameters) {
  GOOGLESQL_RET_CHECK(timestamp_type_parameters.has_precision());
  int64_t precision = timestamp_type_parameters.precision();
  GOOGLESQL_RET_CHECK_GE(precision, 0);
  GOOGLESQL_RET_CHECK_LE(precision, 12);
  GOOGLESQL_RET_CHECK_EQ(precision % 3, 0);
  return absl::OkStatus();
}

absl::Status TypeParameters::ValidateNumericTypeParameters(
    const NumericTypeParametersProto& numeric_type_parameters) {
  int64_t precision = numeric_type_parameters.precision();
  int64_t scale = numeric_type_parameters.scale();
  if (numeric_type_parameters.has_is_max_precision()) {
    GOOGLESQL_RET_CHECK(numeric_type_parameters.is_max_precision())
        << "is_max_precision should either be unset or true";
  } else {
    GOOGLESQL_RET_CHECK(precision >= 1 && precision <= 76) << absl::Substitute(
        "precision must be within range [1, 76] or MAX, actual precision: $0",
        precision);
    GOOGLESQL_RET_CHECK_GE(precision, scale) << absl::Substitute(
        "precision must be equal or larger than scale, actual "
        "precision: $0, scale: $1",
        precision, scale);
  }
  GOOGLESQL_RET_CHECK(scale >= 0 && scale <= 38)
      << "scale must be within range [0, 38], actual scale: " << scale;
  return absl::OkStatus();
}

void TypeParameters::set_child_list(std::vector<TypeParameters> child_list) {
  ABSL_DCHECK(IsEmpty());
  child_list_ = std::move(child_list);
}

absl::Status TypeParameters::Serialize(TypeParametersProto* proto) const {
  proto->Clear();
  if (IsStringTypeParameters()) {
    *proto->mutable_string_type_parameters() = string_type_parameters();
    return absl::OkStatus();
  }
  if (IsNumericTypeParameters()) {
    *proto->mutable_numeric_type_parameters() = numeric_type_parameters();
    return absl::OkStatus();
  }
  if (IsTimestampTypeParameters()) {
    *proto->mutable_timestamp_type_parameters() = timestamp_type_parameters();
    return absl::OkStatus();
  }
  if (IsExtendedTypeParameters()) {
    GOOGLESQL_RETURN_IF_ERROR(extended_type_parameters().Serialize(
        proto->mutable_extended_type_parameters()));
  }
  // Serialize child_list.
  for (const TypeParameters& child : child_list_) {
    GOOGLESQL_RETURN_IF_ERROR(child.Serialize(proto->add_child_list()));
  }
  return absl::OkStatus();
}

absl::StatusOr<std::string> TypeParameters::SerializeAsString() const {
  TypeParametersProto proto;
  GOOGLESQL_RETURN_IF_ERROR(Serialize(&proto));
  return proto.SerializeAsString();
}

absl::StatusOr<TypeParameters> TypeParameters::Deserialize(
    const TypeParametersProto& proto) {
  if (proto.has_string_type_parameters()) {
    return TypeParameters::MakeStringTypeParameters(
        proto.string_type_parameters());
  }
  if (proto.has_numeric_type_parameters()) {
    return TypeParameters::MakeNumericTypeParameters(
        proto.numeric_type_parameters());
  }
  if (proto.has_timestamp_type_parameters()) {
    return TypeParameters::MakeTimestampTypeParameters(
        proto.timestamp_type_parameters());
  }
  // STRUCT, ARRAY, RANGE, or ExtendedType can have empty child_list if
  // sub-fields don't have any type parameters.
  std::vector<TypeParameters> child_list;
  child_list.reserve(proto.child_list_size());
  for (const TypeParametersProto& child_proto : proto.child_list()) {
    GOOGLESQL_ASSIGN_OR_RETURN(TypeParameters type_parameters, Deserialize(child_proto));
    child_list.push_back(std::move(type_parameters));
  }
  if (proto.has_extended_type_parameters()) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        ExtendedTypeParameters extended_type_parameters,
        ExtendedTypeParameters::Deserialize(proto.extended_type_parameters()));
    return TypeParameters::MakeExtendedTypeParameters(extended_type_parameters,
                                                      child_list);
  }
  return TypeParameters::MakeTypeParametersWithChildList(child_list);
}

bool TypeParameters::Equals(const TypeParameters& that) const {
  if (IsStringTypeParameters()) {
    return that.IsStringTypeParameters() &&
           google::protobuf::util::MessageDifferencer::Equals(
               string_type_parameters(), that.string_type_parameters());
  }
  if (IsNumericTypeParameters()) {
    return that.IsNumericTypeParameters() &&
           google::protobuf::util::MessageDifferencer::Equals(
               numeric_type_parameters(), that.numeric_type_parameters());
  }
  if (IsExtendedTypeParameters()) {
    if (!that.IsExtendedTypeParameters() ||
        !extended_type_parameters().Equals(that.extended_type_parameters())) {
      return false;
    }
  }
  if (num_children() != that.num_children()) {
    return false;
  }
  for (int i = 0; i < num_children(); ++i) {
    if (!child(i).Equals(that.child(i))) {
      return false;
    }
  }
  return true;
}

bool TypeParameters::MatchType(const Type* type) const {
  // Empty TypeParameters object matches all types.
  if (IsEmpty()) {
    return true;
  }
  if (IsStringTypeParameters()) {
    return type->IsString() || type->IsBytes();
  }
  if (IsNumericTypeParameters()) {
    return type->IsNumericType() || type->IsBigNumericType();
  }
  if (IsTimestampTypeParameters()) {
    return type->IsTimestamp();
  }
  if (IsExtendedTypeParameters()) {
    // TODO: When integrating with extended type, we can call a virtual
    // function on the 'type' object that indicates whether the specific
    // extended parameters (and child_list) together match the type.
    return type->IsExtendedType();
  }
  std::vector<const Type*> component_types = type->ComponentTypes();
  if (child_list().size() != component_types.size()) {
    return false;
  }
  for (int i = 0; i < child_list().size(); ++i) {
    if (!child_list()[i].MatchType(component_types[i])) {
      return false;
    }
  }
  return true;
}

std::string ExtendedTypeParameters::DebugString() const {
  return absl::StrCat(
      "(",
      absl::StrJoin(parameters_, ",",
                    [](std::string* out, const SimpleValue& value) {
                      absl::StrAppend(out, value.DebugString());
                    }),
      ")");
}

absl::Status ExtendedTypeParameters::Serialize(
    ExtendedTypeParametersProto* proto) const {
  proto->Clear();
  for (const SimpleValue& parameter : parameters_) {
    GOOGLESQL_RETURN_IF_ERROR(parameter.Serialize(proto->add_parameters()));
  }
  return absl::OkStatus();
}

absl::StatusOr<ExtendedTypeParameters> ExtendedTypeParameters::Deserialize(
    const ExtendedTypeParametersProto& proto) {
  std::vector<SimpleValue> parameters;
  parameters.reserve(proto.parameters_size());
  for (const SimpleValueProto& parameter_proto : proto.parameters()) {
    GOOGLESQL_ASSIGN_OR_RETURN(parameters.emplace_back(),
                     SimpleValue::Deserialize(parameter_proto));
  }
  return ExtendedTypeParameters(parameters);
}

bool ExtendedTypeParameters::Equals(const ExtendedTypeParameters& that) const {
  if (num_parameters() != that.num_parameters()) {
    return false;
  }
  for (int i = 0; i < num_parameters(); ++i) {
    if (!parameters_[i].Equals(that.parameter(i))) {
      return false;
    }
  }
  return true;
}

}  // namespace googlesql
