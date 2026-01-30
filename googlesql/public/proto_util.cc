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

#include "googlesql/public/proto_util.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/public/simple_token_list.h"
#include "googlesql/public/civil_time.h"
#include "googlesql/public/functions/arithmetics.h"
#include "googlesql/public/functions/date_time_util.h"
#include "googlesql/public/interval_value.h"
#include "googlesql/public/json_value.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/numeric_value.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/proto/type_annotation.pb.h"
#include "googlesql/public/timestamp_picos_value.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/timestamp_util.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "absl/base/attributes.h"
#include "absl/base/casts.h"
#include "absl/base/optimization.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/inlined_vector.h"
#include "absl/flags/flag.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "googlesql/base/source_location.h"
#include "absl/types/span.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/io/coded_stream.h"
#include "google/protobuf/io/zero_copy_stream_impl_lite.h"
#include "google/protobuf/wire_format_lite.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

// This flag is only for testing the non-optimized path when reading one proto
// field.
ABSL_FLAG(bool, googlesql_read_proto_field_optimized_path, true,
          "Use the specialized version of ReadProtoFields which looks for "
          "only one field.");

using google::protobuf::internal::WireFormatLite;

namespace googlesql {

#define RETURN_ERROR_IF_INVALID_DEFAULT_VALUE(valid, field)                 \
  while (ABSL_PREDICT_FALSE(!(valid)))                                      \
  return ::googlesql_base::InvalidArgumentErrorBuilder(                               \
             ::googlesql_base::SourceLocation::current())                             \
         << "Unable to decode default value for " << (field)->DebugString() \
         << "\n(value out of valid range)"

ProtoFieldDefaultOptions ProtoFieldDefaultOptions::FromFieldAndLanguage(
    const google::protobuf::FieldDescriptor* field,
    const LanguageOptions& language_options) {
  ProtoFieldDefaultOptions options;
  if (!field->has_presence() && language_options.LanguageFeatureEnabled(
                                    FEATURE_IGNORE_PROTO3_USE_DEFAULTS)) {
    options.ignore_use_default_annotations = true;
  }
  if (field->containing_type()->options().map_entry() &&
      language_options.LanguageFeatureEnabled(FEATURE_PROTO_MAPS)) {
    options.map_fields_always_nonnull = true;
  }
  return options;
}

absl::Status GetProtoFieldDefault(const ProtoFieldDefaultOptions& options,
                                  const google::protobuf::FieldDescriptor* field,
                                  const Type* type, Value* default_value) {
  if (options.ignore_format_annotations &&
      !(type->IsSimpleType() || type->IsArray() || field->is_required())) {
    // If we are ignoring format annotations, non-simple, non-array types do
    // not have default values.
    // If field is required, do not set default value.
    *default_value = Value::Null(type);
    return absl::OkStatus();
  }

  if (GOOGLESQL_DEBUG_MODE) {
    TypeKind field_kind;
    GOOGLESQL_RET_CHECK(ProtoType::FieldDescriptorToTypeKind(
                  options.ignore_format_annotations, field, &field_kind)
                  .ok());
    GOOGLESQL_RET_CHECK_EQ(type->kind(), field_kind);
  }

  if (field->is_required()) {
    // There is no default for a required field so don't return a valid value.
    *default_value = Value();
    return absl::OkStatus();
  }

  if (type->IsArray()) {
    // Missing repeated fields are treated as empty arrays.
    *default_value = Value::EmptyArray(type->AsArray());
    return absl::OkStatus();
  }

  if (type->IsMeasureType()) {
    // Measure types don't have a default value.
    GOOGLESQL_RET_CHECK_FAIL() << "Measure types don't have a default value";
  }

  const bool is_map_entry_with_special_handling =
      options.map_fields_always_nonnull &&
      field->containing_type()->options().map_entry();

  if (field->type() == google::protobuf::FieldDescriptor::TYPE_MESSAGE ||
      field->type() == google::protobuf::FieldDescriptor::TYPE_GROUP) {
    if (is_map_entry_with_special_handling) {
      // Map entry fields are considered to always be set, so we always use
      // defaults for them, even when they are messages!
      *default_value = Value::Proto(type->AsProto(), absl::Cord());
    } else {
      *default_value = Value::Null(type);
    }
    return absl::OkStatus();
  }

  // Map entry fields are considered to always be set, so we always use defaults
  // for them.
  const bool use_defaults = ProtoType::GetUseDefaultsExtension(field) ||
                            is_map_entry_with_special_handling;
  if (!use_defaults && !options.ignore_use_default_annotations) {
    *default_value = Value::Null(type);
    return absl::OkStatus();
  }

  int64_t datetime_value;
  if (type->kind() == TYPE_DATE || type->kind() == TYPE_TIMESTAMP ||
      type->kind() == TYPE_TIME || type->kind() == TYPE_DATETIME) {
    switch (field->type()) {
      case google::protobuf::FieldDescriptor::TYPE_INT32:
      case google::protobuf::FieldDescriptor::TYPE_SFIXED32:
      case google::protobuf::FieldDescriptor::TYPE_SINT32:
        datetime_value = field->default_value_int32();
        break;
      case google::protobuf::FieldDescriptor::TYPE_INT64:
      case google::protobuf::FieldDescriptor::TYPE_SFIXED64:
      case google::protobuf::FieldDescriptor::TYPE_SINT64:
        datetime_value = field->default_value_int64();
        break;
      case google::protobuf::FieldDescriptor::TYPE_UINT64:
        if ( type->kind() == TYPE_TIMESTAMP) {
          datetime_value =
              absl::bit_cast<int64_t>(field->default_value_uint64());
          break;
        }
        ABSL_FALLTHROUGH_INTENDED;
      default:
        return ::googlesql_base::InvalidArgumentErrorBuilder()
               << "Invalid date/time annotation on " << field->DebugString();
    }
  }
  switch (type->kind()) {
    case TYPE_INT32:
      *default_value = Value::Int32(field->default_value_int32());
      break;
    case TYPE_INT64:
      *default_value = Value::Int64(field->default_value_int64());
      break;
    case TYPE_UINT32:
      *default_value = Value::Uint32(field->default_value_uint32());
      break;
    case TYPE_UINT64:
      *default_value = Value::Uint64(field->default_value_uint64());
      break;
    case TYPE_BOOL:
      *default_value = Value::Bool(field->default_value_bool());
      break;
    case TYPE_FLOAT:
      *default_value = Value::Float(field->default_value_float());
      break;
    case TYPE_DOUBLE:
      *default_value = Value::Double(field->default_value_double());
      break;
    case TYPE_STRING:
      *default_value = Value::String(field->default_value_string());
      break;
    case TYPE_BYTES:
      *default_value = Value::Bytes(field->default_value_string());
      break;
    case TYPE_ENUM:
      *default_value =
          Value::Enum(type->AsEnum(), field->default_value_enum()->number());
      break;
    case TYPE_NUMERIC:
      *default_value = Value::Numeric(NumericValue());
      break;
    case TYPE_BIGNUMERIC:
      *default_value = Value::BigNumeric(BigNumericValue());
      break;
    case TYPE_DATE: {
      const FieldFormat::Format format = ProtoType::GetFormatAnnotation(field);

      int32_t decoded_date;
      bool is_null;
      absl::Status status = functions::DecodeFormattedDate(
          datetime_value, format, &decoded_date, &is_null);
      if (!status.ok()) {
        return ::googlesql_base::StatusBuilder(status.code())
               << "Unable to decode default value for " << field->DebugString()
               << ": " << status.message();
      }
      if (is_null) {
        *default_value = Value::NullDate();
      } else {
        RETURN_ERROR_IF_INVALID_DEFAULT_VALUE(
            functions::IsValidDate(decoded_date), field);
        *default_value = Value::Date(decoded_date);
      }
      break;
    }
    case TYPE_TIMESTAMP: {
      // For the new timestamp type, widen or truncate the value given the
      // field's annotation.
      const FieldFormat::Format format = ProtoType::GetFormatAnnotation(field);
      switch (format) {
        case FieldFormat::TIMESTAMP_SECONDS:
          RETURN_ERROR_IF_INVALID_DEFAULT_VALUE(
              (functions::IsValidTimestamp(datetime_value,
                                           functions::kSeconds)),
              field);
          datetime_value *= 1000000;
          break;
        case FieldFormat::TIMESTAMP_MILLIS:
          RETURN_ERROR_IF_INVALID_DEFAULT_VALUE(
              functions::IsValidTimestamp(datetime_value,
                                          functions::kMilliseconds),
              field);
          datetime_value *= 1000;
          break;
        case FieldFormat::TIMESTAMP_MICROS:
          RETURN_ERROR_IF_INVALID_DEFAULT_VALUE(
              functions::IsValidTimestamp(datetime_value,
                                          functions::kMicroseconds),
              field);
          break;
        case FieldFormat::TIMESTAMP_NANOS:
          RETURN_ERROR_IF_INVALID_DEFAULT_VALUE(
              functions::IsValidTimestamp(datetime_value,
                                          functions::kNanoseconds),
              field);
          datetime_value /= 1000;
          break;
        default:
          return ::googlesql_base::InvalidArgumentErrorBuilder()
                 << "Invalid field format for " << field->DebugString();
      }
      *default_value = Value::TimestampFromUnixMicros(datetime_value);
      break;
    }
    case TYPE_TIME: {
      TimeValue time = TimeValue::FromPacked64Micros(datetime_value);
      if (!time.IsValid()) {
        return ::googlesql_base::InvalidArgumentErrorBuilder()
               << "Unable to decode default value for " << field->DebugString();
      }
      *default_value = Value::Time(time);
      break;
    }
    case TYPE_DATETIME: {
      if (datetime_value == 0) {
        *default_value = Value::NullDatetime();
        break;
      }
      DatetimeValue datetime =
          DatetimeValue::FromPacked64Micros(datetime_value);
      if (!datetime.IsValid()) {
        return ::googlesql_base::InvalidArgumentErrorBuilder()
               << "Unable to decode default value for " << field->DebugString();
      }
      *default_value = Value::Datetime(datetime);
      break;
    }
    case TYPE_JSON: {
      *default_value = Value::Json(JSONValue());
      break;
    }
    case TYPE_INTERVAL: {
      *default_value = Value::Interval(IntervalValue());
      break;
    }
    case TYPE_RANGE: {
      const Type* range_element_type = type->AsRange()->element_type();
      *default_value = Value::Null(
          types::RangeTypeFromSimpleTypeKind(range_element_type->kind()));
      break;
    }
    case TYPE_TOKENLIST:
      *default_value = Value::TokenList(tokens::TokenList());
      break;
    default: {
      return ::googlesql_base::InvalidArgumentErrorBuilder()
             << "No default value for " << field->DebugString();
    }
  }
  return absl::OkStatus();
}

absl::Status GetProtoFieldTypeAndDefault(
    const ProtoFieldDefaultOptions& options,
    const google::protobuf::FieldDescriptor* field,
    absl::Span<const std::string> catalog_name_path, TypeFactory* type_factory,
    const Type** type, Value* default_value) {
  GOOGLESQL_RETURN_IF_ERROR(type_factory->GetProtoFieldType(
      options.ignore_format_annotations, field, catalog_name_path, type));
  if (default_value != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(GetProtoFieldDefault(options, field, *type, default_value));
  }

  ABSL_DCHECK(default_value == nullptr || !default_value->is_valid() ||
         default_value->type_kind() == (*type)->kind());

  if (GOOGLESQL_DEBUG_MODE) {
    // For testing, make sure the TypeKinds we get from
    // FieldDescriptorToTypeKind match the Types returned by this method.
    TypeKind computed_type_kind;
    GOOGLESQL_RETURN_IF_ERROR(ProtoType::FieldDescriptorToTypeKind(
        options.ignore_format_annotations, field, &computed_type_kind));
    GOOGLESQL_RET_CHECK_EQ((*type)->kind(), computed_type_kind)
        << (*type)->DebugString() << "\n"
        << field->DebugString();
  }

  return absl::OkStatus();
}

static absl::Status Int64ToAdjustedTimestampInt64(FieldFormat::Format format,
                                                  int64_t s,
                                                  int64_t* adjusted_s) {
  absl::Status status;
  switch (format) {
    case FieldFormat::TIMESTAMP_SECONDS:
      if (!functions::Multiply<int64_t>(s, int64_t{1000000}, adjusted_s,
                                        &status)) {
        return status;
      }
      break;
    case FieldFormat::TIMESTAMP_MILLIS:
      if (!functions::Multiply<int64_t>(s, int64_t{1000}, adjusted_s,
                                        &status)) {
        return status;
      }
      break;
    case FieldFormat::TIMESTAMP_MICROS:
      *adjusted_s = s;
      break;
    default:
      return ::googlesql_base::OutOfRangeErrorBuilder()
             << "Invalid timestamp field format: " << format;
  }
  return absl::OkStatus();
}

namespace {

// Convenient representation of the intermediate values produced by
// ReadWireValue.
using WireValueType = std::variant<
    // TYPE_INT32
    // TYPE_SINT32
    // TYPE_SFIXED32
    // TYPE_ENUM
    int32_t,
    // TYPE_INT64
    // TYPE_SINT64
    // TYPE_SFIXED64
    int64_t,
    // TYPE_UINT32
    // TYPE_FIXED32
    uint32_t,
    // TYPE_UINT64
    // TYPE_FIXED64
    uint64_t,
    // TYPE_BOOL
    bool,
    // TYPE_FLOAT
    float,
    // TYPE_DOUBLE
    double,
    // Note that the underlying type is cord to remove the need to copy when
    // these are turned into Value through Value::Proto.
    // TYPE_MESSAGE
    // TYPE_GROUP
    absl::Cord,
    // TYPE_STRING
    // TYPE_BYTES
    std::string>;

// Returns true if we should pretend the wire value is not present. This occurs
// for unknown enums in proto2.
bool WireValueShouldBeHidden(const ProtoFieldInfo& info,
                             WireValueType const& wire_value) {
  const google::protobuf::EnumDescriptor* enum_descriptor = info.descriptor->enum_type();
  if (!enum_descriptor) {
    return false;
  }

  const int32_t* const value = std::get_if<int32_t>(&wire_value);
  if (!value) {
    return false;
  }

  if (enum_descriptor->is_closed() &&
      !enum_descriptor->FindValueByNumber(*value)) {
    // Proto2 hides unknown enums, and returns false for has_.
    return true;
  }
  return false;
}

// Determines all wire values should be hidden, as if the whole vector did not
// exist. Shorthand for WireValueShouldBeHidden being true for all values in the
// vector.
template <typename Vector>
bool AllWireValuesShouldBeHidden(const ProtoFieldInfo& info,
                                 const Vector& values) {
  for (WireValueType const& wire_value : values) {
    if (!WireValueShouldBeHidden(info, wire_value)) {
      return false;
    }
  }
  return true;
}

}  // namespace

// Removes duplicate map keys from an array of map_values. If the values are not
// protobufs with the map_entry() option set, does nothing.
absl::Status RemoveDupsByKeyIfProtoMap(std::vector<Value>& values) {
  if (values.empty()) return absl::OkStatus();

  const ProtoType* const map_entry_type = values.front().type()->AsProto();

  if (map_entry_type == nullptr ||
      !map_entry_type->descriptor()->options().map_entry()) {
    return absl::OkStatus();
  }

  const google::protobuf::FieldDescriptor* const key_descriptor =
      map_entry_type->AsProto()->map_key();
  GOOGLESQL_RET_CHECK(key_descriptor != nullptr) << map_entry_type->DebugString();

  TypeKind key_type_kind;
  GOOGLESQL_RET_CHECK_OK(
      ProtoType::FieldDescriptorToTypeKind(key_descriptor, &key_type_kind))
      << "all proto map key types should be simple type kinds";
  const Type* key_type = types::TypeFromSimpleTypeKind(key_type_kind);
  GOOGLESQL_RET_CHECK(key_type != nullptr) << key_descriptor->DebugString();

  absl::flat_hash_set<Value> seen_keys;
  absl::Status status;

  // Erase any entries that have the same key as an earlier entry.
  // Since we reverse the map, this effectively keeps only the last element that
  // has a given key. This matches the proto2 wire format specification -- last
  // entry with a given key wins.
  std::reverse(values.begin(), values.end());
  values.erase(
      std::remove_if(
          values.begin(), values.end(),
          [&](const Value& value) {
            ABSL_DCHECK(value.type()->IsProto()) << value.DebugString(true);
            Value key;
            if (value.is_valid() && !value.is_null()) {
              status.Update(ReadProtoField(
                  key_descriptor, FieldFormat::DEFAULT_FORMAT, key_type,
                  /*default_value=*/Value(), value.proto_value(), &key));
            }
            if (!status.ok()) return false;
            return !seen_keys.insert(key).second;
          }),
      values.end());
  GOOGLESQL_RETURN_IF_ERROR(status);
  // Reverse it back to the original order.
  std::reverse(values.begin(), values.end());
  return absl::OkStatus();
}

// Populates 'value' with the value of the field at the front of 'in' (which is
// backed by 'bytes'), without considering any googlesql semantics.
static bool ReadWireValue(google::protobuf::FieldDescriptor::Type field_type,
                          uint32_t tag_and_type, const absl::Cord& bytes,
                          google::protobuf::io::CodedInputStream* in,
                          WireValueType* value) {
  int32_t i32;
  int64_t i64;
  uint32_t ui32;
  uint64_t ui64;
  bool b;
  float f;
  double d;

  const WireFormatLite::WireType wire_type =
      WireFormatLite::GetTagWireType(tag_and_type);
  switch (field_type) {
    case WireFormatLite::TYPE_INT32:
      if (wire_type == WireFormatLite::WIRETYPE_VARINT &&
          WireFormatLite::ReadPrimitive<int32_t, WireFormatLite::TYPE_INT32>(
              in, &i32)) {
        *value = i32;
        return true;
      }
      return false;
    case WireFormatLite::TYPE_INT64:
      if (wire_type == WireFormatLite::WIRETYPE_VARINT &&
          WireFormatLite::ReadPrimitive<int64_t, WireFormatLite::TYPE_INT64>(
              in, &i64)) {
        *value = i64;
        return true;
      }
      return false;

    case WireFormatLite::TYPE_UINT32:
      if (wire_type == WireFormatLite::WIRETYPE_VARINT &&
          WireFormatLite::ReadPrimitive<uint32_t, WireFormatLite::TYPE_UINT32>(
              in, &ui32)) {
        *value = ui32;
        return true;
      }
      return false;
    case WireFormatLite::TYPE_UINT64:
      if (wire_type == WireFormatLite::WIRETYPE_VARINT &&
          WireFormatLite::ReadPrimitive<uint64_t, WireFormatLite::TYPE_UINT64>(
              in, &ui64)) {
        *value = ui64;
        return true;
      }
      return false;

    case WireFormatLite::TYPE_SINT32:
      if (wire_type == WireFormatLite::WIRETYPE_VARINT &&
          WireFormatLite::ReadPrimitive<int32_t, WireFormatLite::TYPE_SINT32>(
              in, &i32)) {
        *value = i32;
        return true;
      }
      return false;
    case WireFormatLite::TYPE_SINT64:
      if (wire_type == WireFormatLite::WIRETYPE_VARINT &&
          WireFormatLite::ReadPrimitive<int64_t, WireFormatLite::TYPE_SINT64>(
              in, &i64)) {
        *value = i64;
        return true;
      }
      return false;

    case WireFormatLite::TYPE_FIXED32:
      if (wire_type == WireFormatLite::WIRETYPE_FIXED32 &&
          WireFormatLite::ReadPrimitive<uint32_t, WireFormatLite::TYPE_FIXED32>(
              in, &ui32)) {
        *value = ui32;
        return true;
      }
      return false;
    case WireFormatLite::TYPE_FIXED64:
      if (wire_type == WireFormatLite::WIRETYPE_FIXED64 &&
          WireFormatLite::ReadPrimitive<uint64_t, WireFormatLite::TYPE_FIXED64>(
              in, &ui64)) {
        *value = ui64;
        return true;
      }
      return false;

    case WireFormatLite::TYPE_SFIXED32:
      if (wire_type == WireFormatLite::WIRETYPE_FIXED32 &&
          WireFormatLite::ReadPrimitive<int32_t, WireFormatLite::TYPE_SFIXED32>(
              in, &i32)) {
        *value = i32;
        return true;
      }
      return false;
    case WireFormatLite::TYPE_SFIXED64:
      if (wire_type == WireFormatLite::WIRETYPE_FIXED64 &&
          WireFormatLite::ReadPrimitive<int64_t, WireFormatLite::TYPE_SFIXED64>(
              in, &i64)) {
        *value = i64;
        return true;
      }
      return false;

    case WireFormatLite::TYPE_BOOL:
      if (wire_type == WireFormatLite::WIRETYPE_VARINT &&
          WireFormatLite::ReadPrimitive<bool, WireFormatLite::TYPE_BOOL>(in,
                                                                         &b)) {
        *value = b;
        return true;
      }
      return false;

    case WireFormatLite::TYPE_ENUM:
      if (wire_type == WireFormatLite::WIRETYPE_VARINT &&
          WireFormatLite::ReadPrimitive<int32_t, WireFormatLite::TYPE_ENUM>(
              in, &i32)) {
        *value = i32;
        return true;
      }
      return false;

    case WireFormatLite::TYPE_FLOAT:
      if (wire_type == WireFormatLite::WIRETYPE_FIXED32 &&
          WireFormatLite::ReadPrimitive<float, WireFormatLite::TYPE_FLOAT>(
              in, &f)) {
        *value = f;
        return true;
      }
      return false;
    case WireFormatLite::TYPE_DOUBLE:
      if (wire_type == WireFormatLite::WIRETYPE_FIXED64 &&
          WireFormatLite::ReadPrimitive<double, WireFormatLite::TYPE_DOUBLE>(
              in, &d)) {
        *value = d;
        return true;
      }
      return false;
    case WireFormatLite::TYPE_STRING: {
      std::string s;
      if (WireFormatLite::ReadString(in, &s)) {
        *value = std::move(s);
        return true;
      }
      return false;
    }
    case WireFormatLite::TYPE_BYTES: {
      std::string s;
      if (WireFormatLite::ReadBytes(in, &s)) {
        *value = std::move(s);
        return true;
      }
      return false;
    }
    case WireFormatLite::TYPE_MESSAGE: {
      absl::Cord c;
      if (WireFormatLite::ReadBytes(in, &c)) {
        *value = std::move(c);
        return true;
      }
      return false;
    }
    case WireFormatLite::TYPE_GROUP: {
      const uint32_t start_position = in->CurrentPosition();
      if (!WireFormatLite::SkipField(in, tag_and_type)) return false;
      const int end_group_tag_size =
          google::protobuf::io::CodedOutputStream::VarintSize32(WireFormatLite::MakeTag(
              WireFormatLite::GetTagFieldNumber(tag_and_type),
              WireFormatLite::WIRETYPE_END_GROUP));
      const int group_size =
          in->CurrentPosition() - start_position - end_group_tag_size;
      *value = bytes.Subcord(start_position, group_size);
      return true;
    }
  }
}

// Ideally we would log the type and maybe FieldFormat here, but that would
// require passing a ProductMode which is overkill.
static std::string MakeReadValueErrorReason(
    const google::protobuf::FieldDescriptor* field_descriptor, FieldFormat::Format format,
    int64_t v) {
  std::string reason = absl::StrCat(
      "Corrupted protocol buffer: Failed to interpret value for field ",
      field_descriptor->full_name());
  if (format != FieldFormat::DEFAULT_FORMAT) {
    absl::StrAppend(&reason, " with field format ",
                    FieldFormat_Format_Name(format));
  }
  absl::StrAppend(&reason, ": ", v);
  return reason;
}

namespace {

struct VisitIntegerWireValueAsInt64 {
  absl::StatusOr<int64_t> operator()(int32_t i) const { return i; }
  absl::StatusOr<int64_t> operator()(int64_t i) const { return i; }
  absl::StatusOr<int64_t> operator()(uint32_t i) const { return i; }
  absl::StatusOr<int64_t> operator()(uint64_t i) const {
    return absl::bit_cast<int64_t>(i);
  }
  template <typename T>
  absl::StatusOr<int64_t> operator()(T) const {
    GOOGLESQL_RET_CHECK_FAIL() << "Unexpected type kind " << typeid(T).name()
                     << " in IntegerWireValueAsInt64()";
  }
};

// Same as 'value.ToInt64()', except uint64s are casted to int64, and bools and
// enums are not supported.
absl::StatusOr<int64_t> IntegerWireValueAsInt64(const WireValueType& value) {
  return std::visit(VisitIntegerWireValueAsInt64(), value);
}

}  // namespace

// Translate the proto field value 'wire_value' (obtained from ReadWireValue())
// to the corresponding googlesql Value. 'type' must correspond to
// 'field_descriptor' and 'format'.
static absl::StatusOr<Value> TranslateWireValue(
    const WireValueType& wire_value,
    const google::protobuf::FieldDescriptor* field_descriptor, FieldFormat::Format format,
    const Type* type) {
  GOOGLESQL_RET_CHECK(!type->IsArray());

  switch (type->kind()) {
    case TYPE_INT32: {
      const int32_t* const value = std::get_if<int32_t>(&wire_value);
      GOOGLESQL_RET_CHECK_NE(value, nullptr);
      return Value::Int32(*value);
    }
    case TYPE_INT64: {
      const int64_t* const value = std::get_if<int64_t>(&wire_value);
      GOOGLESQL_RET_CHECK_NE(value, nullptr);
      return Value::Int64(*value);
    }
    case TYPE_DATE: {
      GOOGLESQL_ASSIGN_OR_RETURN(const int64_t v, IntegerWireValueAsInt64(wire_value));
      int32_t decoded_date;
      bool is_null;
      const absl::Status status =
          functions::DecodeFormattedDate(v, format, &decoded_date, &is_null);
      if (ABSL_PREDICT_TRUE(status.ok())) {
        if (is_null) {
          return Value::NullDate();
        } else if (decoded_date >= types::kDateMin &&
                   decoded_date <= types::kDateMax) {
          return Value::Date(decoded_date);
        }
      }
      return googlesql_base::OutOfRangeErrorBuilder()
             << MakeReadValueErrorReason(field_descriptor, format, v);
    }
    case TYPE_TIMESTAMP: {
      GOOGLESQL_ASSIGN_OR_RETURN(const int64_t v, IntegerWireValueAsInt64(wire_value));
      int64_t adjusted_timestamp;
      if (ABSL_PREDICT_FALSE(
              !Int64ToAdjustedTimestampInt64(format, v, &adjusted_timestamp)
                   .ok()) ||
          ABSL_PREDICT_FALSE(adjusted_timestamp < types::kTimestampMin) ||
          ABSL_PREDICT_FALSE(adjusted_timestamp > types::kTimestampMax)) {
        // Adjustment for precision caused arithmetic overflow, or is out of
        // bounds.
        return googlesql_base::OutOfRangeErrorBuilder()
               << MakeReadValueErrorReason(field_descriptor, format, v);
      } else {
        return Value::TimestampFromUnixMicros(adjusted_timestamp);
      }
    }
    case TYPE_TIME: {
      GOOGLESQL_ASSIGN_OR_RETURN(const int64_t v, IntegerWireValueAsInt64(wire_value));
      TimeValue time = TimeValue::FromPacked64Micros(v);
      if (ABSL_PREDICT_TRUE(time.IsValid())) {
        return Value::Time(time);
      } else {
        return googlesql_base::OutOfRangeErrorBuilder()
               << MakeReadValueErrorReason(field_descriptor, format, v);
      }
    }
    case TYPE_DATETIME: {
      GOOGLESQL_ASSIGN_OR_RETURN(const int64_t v, IntegerWireValueAsInt64(wire_value));
      DatetimeValue datetime = DatetimeValue::FromPacked64Micros(v);
      if (ABSL_PREDICT_TRUE(datetime.IsValid())) {
        return Value::Datetime(datetime);
      } else {
        return googlesql_base::OutOfRangeErrorBuilder()
               << MakeReadValueErrorReason(field_descriptor, format, v);
      }
    }
    case TYPE_UINT32: {
      const uint32_t* const value = std::get_if<uint32_t>(&wire_value);
      GOOGLESQL_RET_CHECK_NE(value, nullptr);
      return Value::Uint32(*value);
    }
    case TYPE_UINT64: {
      const uint64_t* const value = std::get_if<uint64_t>(&wire_value);
      GOOGLESQL_RET_CHECK_NE(value, nullptr);
      return Value::Uint64(*value);
    }
    case TYPE_BOOL: {
      const bool* const value = std::get_if<bool>(&wire_value);
      GOOGLESQL_RET_CHECK_NE(value, nullptr);
      return Value::Bool(*value);
    }
    case TYPE_FLOAT: {
      const float* const value = std::get_if<float>(&wire_value);
      GOOGLESQL_RET_CHECK_NE(value, nullptr);
      return Value::Float(*value);
    }
    case TYPE_DOUBLE: {
      const double* const value = std::get_if<double>(&wire_value);
      GOOGLESQL_RET_CHECK_NE(value, nullptr);
      return Value::Double(*value);
    }
    case TYPE_ENUM: {
      const int32_t* const value = std::get_if<int32_t>(&wire_value);
      GOOGLESQL_RET_CHECK_NE(value, nullptr);
      Value enum_value = Value::Enum(type->AsEnum(), *value);
      if (ABSL_PREDICT_FALSE(!enum_value.is_valid())) {
        // TODO: Properly support open proto3 enums.
        return googlesql_base::OutOfRangeErrorBuilder()
               << MakeReadValueErrorReason(field_descriptor, format, *value);
      }
      return enum_value;
    }
    case TYPE_STRING: {
      const std::string* const value = std::get_if<std::string>(&wire_value);
      GOOGLESQL_RET_CHECK_NE(value, nullptr);
      return Value::String(*value);
    }
    case TYPE_BYTES: {
      const std::string* const value = std::get_if<std::string>(&wire_value);
      GOOGLESQL_RET_CHECK_NE(value, nullptr);
      return Value::Bytes(*value);
    }
    case TYPE_PROTO: {
      const absl::Cord* const value = std::get_if<absl::Cord>(&wire_value);
      GOOGLESQL_RET_CHECK_NE(value, nullptr);

      return Value::Proto(type->AsProto(), *value);
    }
    case TYPE_NUMERIC: {
      const std::string* const value = std::get_if<std::string>(&wire_value);
      GOOGLESQL_RET_CHECK_NE(value, nullptr);
      GOOGLESQL_ASSIGN_OR_RETURN(NumericValue numeric_value,
                       NumericValue::DeserializeFromProtoBytes(*value));
      return Value::Numeric(numeric_value);
    }
    case TYPE_ARRAY:
    case TYPE_STRUCT:
    default:
      GOOGLESQL_RET_CHECK_FAIL() << "Unexpected type kind: " << type->kind();
  }
}

inline bool IsPackedWireType(uint32_t tag) {
  return WireFormatLite::GetTagWireType(tag) ==
         WireFormatLite::WIRETYPE_LENGTH_DELIMITED;
}

using PackedValuesVector = absl::InlinedVector<WireValueType, 8>;

// Returns true on success.
static bool ReadPackedWireValues(int tag_number,
                                 google::protobuf::FieldDescriptor::Type field_type,
                                 google::protobuf::io::CodedInputStream* in,
                                 PackedValuesVector* values) {
  int length;
  if (!in->ReadVarintSizeAsInt(&length) || length <= 0) {
    return false;
  }
  // Only primitive numeric/enum values can be packed.
  absl::Cord unused_bytes;
  google::protobuf::io::CodedInputStream::Limit limit = in->PushLimit(length);
  uint32_t tag_and_type = WireFormatLite::MakeTag(
      tag_number, WireFormatLite::WireTypeForFieldType(
                      static_cast<WireFormatLite::FieldType>(
                          absl::implicit_cast<int>(field_type))));
  while (in->BytesUntilLimit() > 0) {
    WireValueType value;
    if (!ReadWireValue(field_type, tag_and_type, unused_bytes, in, &value)) {
      return false;
    }
    values->push_back(std::move(value));
  }
  in->PopLimit(limit);
  return true;
}

// Optimized version of ReadProtoFields where only one field is being fetched.
static absl::StatusOr<Value> ReadSingularProtoField(
    const ProtoFieldInfo& field_info, const absl::Cord& bytes) {
  const int field_info_tag = field_info.descriptor->number();

  // The elements we have seen for 'field_info'. Only used if
  // 'field_info.get_has_bit' is false.
  absl::InlinedVector<Value, 8> elements;
  const bool is_packable = field_info.descriptor->is_packable();
  uint32_t tag_and_type;
  google::protobuf::io::CordInputStream cord_stream(&bytes);
  google::protobuf::io::CodedInputStream in(&cord_stream);
  while (0 < (tag_and_type = in.ReadTag())) {
    const int tag_number = WireFormatLite::GetTagFieldNumber(tag_and_type);
    if (tag_number != field_info_tag) {
      if (ABSL_PREDICT_TRUE(WireFormatLite::SkipField(&in, tag_and_type))) {
        continue;
      }
      return ::googlesql_base::OutOfRangeErrorBuilder()
             << "Corrupted protocol buffer: "
             << "Failed to skip field with tag number " << tag_number << " in "
             << field_info.descriptor->containing_type()->full_name();
    }

    PackedValuesVector wire_values;
    // Protocol buffer parsers must be able to parse repeated fields that were
    // compiled as packed as if they were not packed, and vice versa.  Both
    // packed and non-packed field occurrences may appear within the same
    // message.
    if (is_packable && IsPackedWireType(tag_and_type)) {
      if (ABSL_PREDICT_FALSE(!ReadPackedWireValues(
              field_info.descriptor->number(), field_info.descriptor->type(),
              &in, &wire_values))) {
        return ::googlesql_base::OutOfRangeErrorBuilder()
               << "Corrupted protocol buffer: "
               << "Failed to read packed elements for field "
               << field_info.descriptor->full_name();
      }
    } else {
      WireValueType wire_value;
      if (ABSL_PREDICT_FALSE(!ReadWireValue(field_info.descriptor->type(),
                                            tag_and_type, bytes, &in,
                                            &wire_value))) {
        return googlesql_base::OutOfRangeErrorBuilder()
               << "Corrupted protocol buffer: Failed to read value for field "
               << field_info.descriptor->full_name();
      }
      wire_values.push_back(std::move(wire_value));
    }
    GOOGLESQL_RET_CHECK(!wire_values.empty());

    if (field_info.get_has_bit) {
      return Value::Bool(!AllWireValuesShouldBeHidden(field_info, wire_values));
    }
    for (const WireValueType& wire_value : wire_values) {
      if (WireValueShouldBeHidden(field_info, wire_value)) {
        continue;
      }
      const Type* element_type =
          field_info.type->IsArray()
              ? field_info.type->AsArray()->element_type()
              : field_info.type;
      GOOGLESQL_ASSIGN_OR_RETURN(Value element,
                       TranslateWireValue(wire_value, field_info.descriptor,
                                          field_info.format, element_type));
      elements.push_back(std::move(element));
    }
  }

  // Now that we have read all of the values we care about, use them to populate
  // the return value.
  if (field_info.get_has_bit) {
    return Value::Bool(false);
  }
  GOOGLESQL_RET_CHECK_EQ(field_info.type->IsArray(),
               field_info.descriptor->is_repeated());
  if (field_info.type->IsArray()) {
    std::vector<Value> values_not_inlined(
        std::make_move_iterator(elements.begin()),
        std::make_move_iterator(elements.end()));
    GOOGLESQL_RETURN_IF_ERROR(RemoveDupsByKeyIfProtoMap(values_not_inlined));
    return Value::MakeArray(field_info.type->AsArray(),
                            std::move(values_not_inlined));
  }
  if (elements.empty()) {
    if (ABSL_PREDICT_FALSE(field_info.descriptor->is_required())) {
      return googlesql_base::OutOfRangeErrorBuilder()
             << "Protocol buffer missing required field "
             << field_info.descriptor->full_name();
    } else {
      return field_info.default_value;
    }
  }
  if (elements.size() > 1 &&
      field_info.type->IsProto()) {
    // Merge multiple occurrences of embedded message
    absl::Cord merged_message;
    for (const auto& element : elements) {
      merged_message.Append(element.ToCord());
    }
    return Value::Proto(field_info.type->AsProto(), merged_message);
  }
  return std::move(elements.back());
}

namespace {

// Maps a tag number to all ProtoFieldInfos indexes with that tag number.
using FieldInfoMap = absl::flat_hash_map<int, std::vector<int>>;

// Maps a ProtoFieldInfo (by its index) to the corresponding Values we have
// seen for it, with errors for failure to convert wire values to the
// appropriate FieldFormats.
using ElementValueList = std::vector<std::vector<absl::StatusOr<Value>>>;

}  // namespace

absl::Status ReadProtoFields(
    absl::Span<const ProtoFieldInfo* const> field_infos,
    const absl::Cord& bytes, ProtoFieldValueList* field_value_list) {
  const bool use_optimization =
      field_infos.size() == 1 &&
      absl::GetFlag(FLAGS_googlesql_read_proto_field_optimized_path);

  if (use_optimization) {
    GOOGLESQL_ASSIGN_OR_RETURN(Value value,
                     ReadSingularProtoField(*field_infos[0], bytes));
    field_value_list->push_back(std::move(value));
    return absl::OkStatus();
  }

  field_value_list->resize(field_infos.size());

  FieldInfoMap field_info_map;
  for (int i = 0; i < field_infos.size(); ++i) {
    const ProtoFieldInfo* field_info = field_infos[i];
    field_info_map[field_info->descriptor->number()].push_back(i);
  }

  // If get_has_bit is true, this is either empty or contains a single
  // Value::Bool(true).
  ElementValueList element_value_list(field_infos.size());
  GOOGLESQL_RET_CHECK(!field_infos.empty());
  const google::protobuf::FieldDescriptor* some_field = field_infos[0]->descriptor;
    uint32_t tag_and_type;
    google::protobuf::io::CordInputStream cord_stream(&bytes);
    google::protobuf::io::CodedInputStream in(&cord_stream);
    while (0 < (tag_and_type = in.ReadTag())) {
      const int tag_number = WireFormatLite::GetTagFieldNumber(tag_and_type);
      const std::vector<int>* info_idxs =
          googlesql_base::FindOrNull(field_info_map, tag_number);
      if (info_idxs == nullptr) {
        if (ABSL_PREDICT_TRUE(WireFormatLite::SkipField(&in, tag_and_type))) {
          continue;
        }
        return ::googlesql_base::OutOfRangeErrorBuilder()
               << "Corrupted protocol buffer: "
               << "Failed to skip field with tag number " << tag_number
               << " in " << some_field->containing_type()->full_name();
      }
      // All of the field descriptors must come from the same
      // google::protobuf::Descriptor, so for a particular tag number, they are all the
      // same.
      GOOGLESQL_RET_CHECK(!info_idxs->empty());
      const google::protobuf::FieldDescriptor* descriptor =
          field_infos[(*info_idxs)[0]]->descriptor;

      PackedValuesVector wire_values;
      // Protocol buffer parsers must be able to parse repeated fields that were
      // compiled as packed as if they were not packed, and vice versa.  Both
      // packed and non-packed field occurrences may appear within the same
      // message.
      if (descriptor->is_packable() && IsPackedWireType(tag_and_type)) {
        if (ABSL_PREDICT_FALSE(!ReadPackedWireValues(
                descriptor->number(), descriptor->type(), &in, &wire_values))) {
          return ::googlesql_base::OutOfRangeErrorBuilder()
                 << "Corrupted protocol buffer: "
                 << "Failed to read packed elements for field "
                 << descriptor->full_name();
        }
      } else {
        WireValueType wire_value;
        if (ABSL_PREDICT_FALSE(!ReadWireValue(descriptor->type(), tag_and_type,
                                              bytes, &in, &wire_value))) {
          return googlesql_base::OutOfRangeErrorBuilder()
                 << "Corrupted protocol buffer: Failed to read value for field "
                 << descriptor->full_name();
        }
        wire_values.push_back(std::move(wire_value));
      }
      GOOGLESQL_RET_CHECK(!wire_values.empty());

      for (int i = 0; i < info_idxs->size(); ++i) {
        const int idx = (*info_idxs)[i];
        const ProtoFieldInfo* info = field_infos[idx];
        std::vector<absl::StatusOr<Value>>& elements = element_value_list[idx];

        if (info->get_has_bit) {
          if (elements.empty()) {
            if (!AllWireValuesShouldBeHidden(*info, wire_values)) {
              elements.push_back(Value::Bool(true));
            }
          }
        } else {
          const Type* element_type = info->type->IsArray()
                                         ? info->type->AsArray()->element_type()
                                         : info->type;
          for (const WireValueType& wire_value : wire_values) {
            if (!WireValueShouldBeHidden(*info, wire_value)) {
              elements.push_back(TranslateWireValue(
                  wire_value, descriptor, info->format, element_type));
            }
          }
        }
      }
    }

  // Now that we have read all of the values we care about, use them to populate
  // 'field_value_list'.
  for (int i = 0; i < field_infos.size(); ++i) {
    const ProtoFieldInfo* info = field_infos[i];
    std::vector<absl::StatusOr<Value>>& values = element_value_list[i];
    if (info->get_has_bit) {
      (*field_value_list)[i] = Value::Bool(!values.empty());
    } else {
      absl::StatusOr<Value> new_value;

      GOOGLESQL_RET_CHECK_EQ(info->type->IsArray(), info->descriptor->is_repeated());
      if (info->type->IsArray()) {
        std::vector<Value> element_values;
        element_values.reserve(values.size());
        bool success = true;
        for (absl::StatusOr<Value>& value : values) {
          if (ABSL_PREDICT_FALSE(!value.ok())) {
            success = false;
            new_value = value.status();
            break;
          }
          element_values.push_back(std::move(value).value());
        }

        GOOGLESQL_RETURN_IF_ERROR(RemoveDupsByKeyIfProtoMap(element_values));

        if (success) {
          new_value = Value::MakeArray(info->type->AsArray(),
                                       std::move(element_values));
        }
      } else if (values.empty()) {
        if (ABSL_PREDICT_FALSE(info->descriptor->is_required())) {
          new_value = absl::Status(
              absl::StatusCode::kOutOfRange,
              absl::StrCat("Protocol buffer missing required field ",
                           info->descriptor->full_name()));
        } else {
          new_value = info->default_value;
        }
      } else if (
          values.size() > 1 &&
          info->type->IsProto()) {
        // Merge multiple occurrences of embedded message
        absl::Cord merged_message;
        bool success = true;
        for (const absl::StatusOr<Value>& value : values) {
          if (!value.ok()) {
            success = false;
            new_value = value;
            break;
          }
          merged_message.Append(value->ToCord());
        }
        if (success) {
          new_value = Value::Proto(info->type->AsProto(), merged_message);
        }
      } else {
        new_value = std::move(values.back());
      }

      (*field_value_list)[i] = std::move(new_value);
    }
  }

  return absl::OkStatus();
}

absl::Status ReadProtoField(const google::protobuf::FieldDescriptor* field_descr,
                            FieldFormat::Format format, const Type* type,
                            const Value& default_value, bool get_has_bit,
                            const absl::Cord& bytes, Value* output_value) {
  ProtoFieldInfo info;
  info.descriptor = field_descr;
  info.format = format;
  info.type = type;
  info.default_value = default_value;
  info.get_has_bit = get_has_bit;

  ProtoFieldValueList field_value_list;
  GOOGLESQL_RETURN_IF_ERROR(ReadProtoFields({&info}, bytes, &field_value_list));
  GOOGLESQL_RET_CHECK_EQ(field_value_list.size(), 1);
  const absl::StatusOr<Value>& status_or_value = field_value_list[0];
  GOOGLESQL_RETURN_IF_ERROR(status_or_value.status());
  *output_value = status_or_value.value();
  return absl::OkStatus();
}

absl::Status ReadProtoField(const google::protobuf::FieldDescriptor* field_descr,
                            FieldFormat::Format format, const Type* type,
                            const Value& default_value, const absl::Cord& bytes,
                            Value* output_value) {
  return ReadProtoField(field_descr, format, type, default_value, false, bytes,
                        output_value);
}

absl::Status ProtoHasField(
    int32_t field_tag, const absl::Cord& bytes,
    bool* has_field) {
  *has_field = false;
    google::protobuf::io::CordInputStream cord_stream(&bytes);
    google::protobuf::io::CodedInputStream in(&cord_stream);
    uint32_t tag_and_type;
    while (0 < (tag_and_type = in.ReadTag())) {
      if (field_tag == WireFormatLite::GetTagFieldNumber(tag_and_type)) {
        // Stop once we find one instance of the field.
        // We have no special treatment for repeated fields here.
        *has_field = true;
        break;
      }
      if (!WireFormatLite::SkipField(&in, tag_and_type)) {
        return absl::Status(absl::StatusCode::kOutOfRange,
                            "Corrupted protocol buffer");
      }
    }
  return absl::OkStatus();
}

bool IsProtoMap(const Type* type) {
  if (type == nullptr) {
    return false;
  }
  if (!type->IsArray()) return false;
  const Type* element = type->AsArray()->element_type();
  if (!element->IsProto()) return false;
  return element->AsProto()->descriptor()->options().map_entry();
}

absl::Status ParseProtoMap(const Value& array_of_map_entry,
                           const Type* key_type, const Type* value_type,
                           std::vector<std::pair<Value, Value>>& output) {
  // `value_type` can be null pointer in cases such as when we are implementing
  // `PROTO_MAP_CONTAINS_KEY`. In that case, we don't need the value type and
  // plumbing it properly in the reference implementation is also non-trivail.
  GOOGLESQL_RET_CHECK(key_type != nullptr);
  if (!IsProtoMap(array_of_map_entry.type())) {
    return absl::InvalidArgumentError(
        absl::StrCat("Expected a proto map and got ",
                     array_of_map_entry.type()->DebugString()));
  }
  if (array_of_map_entry.is_null()) {
    return absl::OkStatus();
  }
  const ProtoType* entry_type =
      array_of_map_entry.type()->AsArray()->element_type()->AsProto();

  auto MakeInfo = [](const Type* type,
                     const google::protobuf::FieldDescriptor* descriptor) {
    ProtoFieldInfo info;
    info.default_value = Value::Null(type);
    info.type = type;
    info.descriptor = descriptor;
    info.format = FieldFormat::DEFAULT_FORMAT;
    info.get_has_bit = false;
    return info;
  };

  ProtoFieldInfo key_info;
  ProtoFieldInfo value_info;
  std::vector<ProtoFieldInfo*> info_pointers;
  info_pointers.reserve(2);
  key_info = MakeInfo(key_type, entry_type->map_key());
  info_pointers.push_back(&key_info);
  if (value_type != nullptr) {
    value_info = MakeInfo(value_type, entry_type->map_value());
    info_pointers.push_back(&value_info);
  }

  ProtoFieldValueList value_list;
  output.reserve(array_of_map_entry.elements().size());
  for (const Value& element : array_of_map_entry.elements()) {
    std::pair<Value, Value>& element_parsed = output.emplace_back();
    if (element.is_null()) {
      element_parsed.first = Value::Null(key_type);
      if (value_type != nullptr) {
        element_parsed.second = Value::Null(value_type);
      }
      continue;
    }
    GOOGLESQL_RETURN_IF_ERROR(
        ReadProtoFields(info_pointers, element.ToCord(), &value_list));
    int i = 0;
    GOOGLESQL_ASSIGN_OR_RETURN(element_parsed.first, value_list[i++]);
    if (value_type != nullptr) {
      GOOGLESQL_ASSIGN_OR_RETURN(element_parsed.second, value_list[i]);
    }
    value_list.clear();
  }
  return absl::OkStatus();
}

}  // namespace googlesql
