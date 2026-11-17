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

#include "googlesql/reference_impl/type_parameter_constraints.h"

#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/common/internal_value.h"
#include "googlesql/common/vector_value_util.h"
#include "googlesql/public/functions/string.h"
#include "googlesql/public/numeric_value.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/proto/vector_encoding_id.pb.h"
#include "googlesql/public/proto/vector_value.pb.h"
#include "googlesql/public/timestamp_picos_value.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/type_parameters.pb.h"
#include "googlesql/public/types/type_parameters.h"
#include "googlesql/public/types/vector_type_util.h"
#include "googlesql/public/types/vector_value_codec.h"
#include "googlesql/public/value.h"
#include "absl/numeric/int128.h"
#include "absl/status/status.h"
#include "googlesql/base/status_macros.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "googlesql/base/ret_check.h"

namespace googlesql {

constexpr int64_t kMaxStringLength = std::numeric_limits<int64_t>::max();
constexpr int64_t kMaxBytesLength = std::numeric_limits<int64_t>::max();

namespace {

absl::Status CheckMaxLength(int64_t max_length, int64_t length,
                            absl::string_view type_name) {
  if (max_length < length) {
    return absl::OutOfRangeError(absl::Substitute(
        "$0($1) has maximum length $1 but got a value with length $2",
        type_name, max_length, length));
  }

  return absl::OkStatus();
}

template <typename T>
absl::StatusOr<T> ApplyPrecisionScale(T value, int64_t precision,
                                      int64_t scale) {
  static_assert(std::is_same_v<T, NumericValue> ||
                std::is_same_v<T, BigNumericValue>);
  if (value.HasFractionalPart()) {
    GOOGLESQL_ASSIGN_OR_RETURN(value, value.Round(scale));
  }
  // The upper and lower bounds follow this format:
  //         <----precision---->
  //   [+/-] 9999999.99999999999
  //                 <--scale-->
  // It is not possible to represent the bounds of acceptable values using a
  // double, so create a string that represents the upper bound.
  std::string upper_bound_str;
  if (scale == 0) {
    upper_bound_str.resize(precision, '9');
  } else {
    upper_bound_str.resize(precision + 1, '9');
    upper_bound_str[precision - scale] = '.';
  }
  GOOGLESQL_ASSIGN_OR_RETURN(T upper_bound, T::FromString(upper_bound_str));
  GOOGLESQL_ASSIGN_OR_RETURN(T lower_bound,
                   T::FromString(absl::StrCat("-", upper_bound_str)));

  std::string type_name;
  if constexpr (std::is_same_v<T, NumericValue>) {
    if (scale == 0) {
      type_name = absl::Substitute("NUMERIC($0)", precision);
    } else {
      type_name = absl::Substitute("NUMERIC($0, $1)", precision, scale);
    }
  } else {
    if (scale == 0) {
      type_name = absl::Substitute("BIGNUMERIC($0)", precision);
    } else {
      type_name = absl::Substitute("BIGNUMERIC($0, $1)", precision, scale);
    }
  }
  if (!(lower_bound <= value && value <= upper_bound)) {
    return absl::OutOfRangeError(absl::Substitute(
        "$0 has precision $1 and scale $2 but got a value that is "
        "not in range of [$3, $4]",
        type_name, precision, scale, lower_bound.ToString(),
        upper_bound.ToString()));
  }
  return value;
}

static absl::StatusOr<TimestampPicosValue> ApplyPrecision(
    TimestampPicosValue value, int64_t precision) {
  absl::int128 unix_picos = value.ToUnixPicos();

  // Integer division (floor) to the right precision.
  int64_t picos_per_target_unit = 0;
  switch (precision) {
    case 0:
      picos_per_target_unit = 1'000'000'000'000;
      break;
    case 3:
      picos_per_target_unit = 1'000'000'000;
      break;
    case 6:
      picos_per_target_unit = 1'000'000;
      break;
    case 9:
      picos_per_target_unit = 1'000;
      break;
    case 12:
      picos_per_target_unit = 1;
      break;
    default:
      GOOGLESQL_RET_CHECK_FAIL() << "Unexpected precision: " << precision;
  }

  absl::int128 remainder = unix_picos % picos_per_target_unit;
  return TimestampPicosValue::FromUnixPicos(unix_picos - remainder);
}

static absl::Status ApplyVectorConstraints(const TypeParameters& type_params,
                                           const Value& value) {
  GOOGLESQL_RET_CHECK(type_params.IsVectorTypeParameters());
  const VectorTypeParametersProto* vector_params =
      type_params.vector_type_parameters();
  GOOGLESQL_RET_CHECK(vector_params != nullptr);

  GOOGLESQL_ASSIGN_OR_RETURN(VectorValueProto vector_value, ParseVectorValue(value));

  // The VECTOR value is self-describing, so the actual encoding is read from
  // the value rather than assumed from the type.
  const auto actual_encoding =
      static_cast<VectorEncodingId::Id>(vector_value.encoding());
  if (vector_params->has_encoding() &&
      vector_params->encoding() != actual_encoding) {
    return absl::OutOfRangeError(absl::Substitute(
        "VECTOR has encoding $0 but got a vector with encoding $1",
        VectorEncodingId_Id_Name(vector_params->encoding()),
        VectorEncodingId_Id_Name(actual_encoding)));
  }

  // The dims live in the payload, which only the encoding that wrote it can
  // interpret. FLOAT32 is the only encoding the reference implementation
  // understands, so any other one goes unchecked.
  if (vector_params->has_dims() &&
      actual_encoding == VectorEncodingId::FLOAT32) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::vector<float> elements,
                     DecodeFloat32VectorPayload(vector_value.data()));
    const int64_t actual_dims = elements.size();
    const int64_t expected_dims = vector_params->dims();
    if (actual_dims != expected_dims) {
      return absl::OutOfRangeError(absl::Substitute(
          "VECTOR($0) has dims $0 but got a vector with dims $1", expected_dims,
          actual_dims));
    }
  }
  return absl::OkStatus();
}

}  // namespace

absl::Status ApplyConstraints(const TypeParameters& type_params,
                              ProductMode mode, Value& value) {
  if (type_params.IsEmpty() || value.is_null()) {
    return absl::OkStatus();
  }
  GOOGLESQL_RETURN_IF_ERROR(
      value.type()->ValidateResolvedTypeParameters(type_params, mode));
  switch (value.type()->kind()) {
    case TYPE_STRING: {
      int64_t max_length =
          type_params.string_type_parameters().has_is_max_length()
              ? kMaxStringLength
              : type_params.string_type_parameters().max_length();

      // Get UTF string length.
      int64_t string_length;
      absl::Status length_error;
      if (!functions::LengthUtf8(value.string_value(), &string_length,
                                 &length_error)) {
        return length_error;
      }
      return CheckMaxLength(max_length, string_length, "STRING");
    }
    case TYPE_BYTES: {
      int64_t max_length =
          type_params.string_type_parameters().has_is_max_length()
              ? kMaxBytesLength
              : type_params.string_type_parameters().max_length();
      return CheckMaxLength(max_length, value.bytes_value().size(), "BYTES");
    }
    case TYPE_NUMERIC: {
      GOOGLESQL_ASSIGN_OR_RETURN(
          NumericValue updated_numeric_value,
          ApplyPrecisionScale(value.numeric_value(),
                              type_params.numeric_type_parameters().precision(),
                              type_params.numeric_type_parameters().scale()));
      value = Value::Numeric(updated_numeric_value);
      return absl::OkStatus();
    }
    case TYPE_BIGNUMERIC: {
      BigNumericValue updated_bignumeric_value;
      if (type_params.numeric_type_parameters().has_is_max_precision()) {
        if (!type_params.numeric_type_parameters().has_scale()) {
          return absl::OkStatus();
        }
        GOOGLESQL_ASSIGN_OR_RETURN(updated_bignumeric_value,
                         value.bignumeric_value().Round(
                             type_params.numeric_type_parameters().scale()));
      } else {
        GOOGLESQL_ASSIGN_OR_RETURN(updated_bignumeric_value,
                         ApplyPrecisionScale(
                             value.bignumeric_value(),
                             type_params.numeric_type_parameters().precision(),
                             type_params.numeric_type_parameters().scale()));
      }
      value = Value::BigNumeric(updated_bignumeric_value);
      return absl::OkStatus();
    }
    case TYPE_TIMESTAMP: {
      GOOGLESQL_ASSIGN_OR_RETURN(
          TimestampPicosValue updated_timestamp,
          ApplyPrecision(value.ToUnixPicos(),
                         type_params.timestamp_type_parameters().precision()));
      value = Value::Timestamp(updated_timestamp);
      return absl::OkStatus();
    }
    case TYPE_STRUCT: {
      std::vector<Value> new_fields;
      new_fields.reserve(value.num_fields());
      for (int i = 0; i < value.num_fields(); ++i) {
        Value new_field_value = value.field(i);
        GOOGLESQL_RETURN_IF_ERROR(
            ApplyConstraints(type_params.child(i), mode, new_field_value));
        new_fields.push_back(new_field_value);
      }
      value = Value::Struct(value.type()->AsStruct(), new_fields);
      return absl::OkStatus();
    }
    case TYPE_ARRAY: {
      std::vector<Value> new_elements;
      new_elements.reserve(value.num_elements());
      for (Value new_element_value : value.elements()) {
        GOOGLESQL_RETURN_IF_ERROR(
            ApplyConstraints(type_params.child(0), mode, new_element_value));
        new_elements.push_back(new_element_value);
      }
      // Preserve orderedness (or lack thereof) of the array.
      value = InternalValue::ArrayNotChecked(value.type()->AsArray(),
                                             InternalValue::GetOrderKind(value),
                                             std::move(new_elements));
      return absl::OkStatus();
    }
    case TYPE_RANGE: {
      Value new_start_value = value.start();
      GOOGLESQL_RETURN_IF_ERROR(
          ApplyConstraints(type_params.child(0), mode, new_start_value));
      Value new_end_value = value.end();
      GOOGLESQL_RETURN_IF_ERROR(
          ApplyConstraints(type_params.child(0), mode, new_end_value));
      GOOGLESQL_ASSIGN_OR_RETURN(value, Value::MakeRange(new_start_value, new_end_value));
      return absl::OkStatus();
    }
    case TYPE_MAP: {
      std::vector<std::pair<Value, Value>> new_map_entries;
      for (const auto& [map_key, map_value] : value.map_entries()) {
        Value constrained_map_key = map_key;
        GOOGLESQL_RETURN_IF_ERROR(
            ApplyConstraints(type_params.child(0), mode, constrained_map_key));
        Value constrained_map_value = map_value;
        GOOGLESQL_RETURN_IF_ERROR(ApplyConstraints(type_params.child(1), mode,
                                         constrained_map_value));
        new_map_entries.emplace_back(constrained_map_key,
                                     constrained_map_value);
      }
      GOOGLESQL_ASSIGN_OR_RETURN(value, Value::MakeMap(value.type()->AsMap(),
                                             std::move(new_map_entries)));
      return absl::OkStatus();
    }
    case TYPE_DECLARATIVE: {
      if (IsVectorType(value.type())) {
        return ApplyVectorConstraints(type_params, value);
      }
      GOOGLESQL_RET_CHECK_FAIL() << absl::Substitute(
          "Type parameters are not handled for type $0. (Got type "
          "parameters: $1)",
          value.type()->DebugString(), type_params.DebugString());
    }
    default:
      // If type parameters is non-empty, then the type should have been
      // explicitly handled above.
      GOOGLESQL_RET_CHECK_FAIL() << absl::Substitute(
          "Type parameters are not handled for $0. (Got type "
          "parameters: $1)",
          value.type()->DebugString(), type_params.DebugString());
  }
}

}  // namespace googlesql
