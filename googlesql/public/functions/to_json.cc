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

#include "googlesql/public/functions/to_json.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/common/errors.h"
#include "googlesql/common/thread_stack.h"
#include "googlesql/public/functions/json_format.h"
#include "googlesql/public/functions/unsupported_fields.pb.h"
#include "googlesql/public/interval_value.h"
#include "googlesql/public/json_value.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/graph_element_type.h"
#include "googlesql/public/types/proto_type.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/uuid_value.h"
#include "googlesql/public/value.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "google/protobuf/dynamic_message.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace functions {
namespace {

constexpr int64_t kInt64Min = std::numeric_limits<int64_t>::min();
constexpr int64_t kInt64Max = std::numeric_limits<int64_t>::max();
constexpr uint64_t kUint64Min = std::numeric_limits<uint64_t>::min();
constexpr uint64_t kUint64Max = std::numeric_limits<uint64_t>::max();

absl::Status GetToJsonStackOverflowStatus() {
  // Status object returned when the stack overflows. Used to avoid
  // RETURN_ERROR, which may end up calling GoogleOnceInit methods on
  // GenericErrorSpace, which in turn would require more stack while the
  // stack is already overflowed.
  static absl::Status* overflow =
      new absl::Status(absl::StatusCode::kResourceExhausted,
                       "Out of stack space due to deeply nested json object "
                       "in to_json function");
  return *overflow;
}

#define RETURN_ERROR_IF_OUT_OF_STACK_SPACE()                       \
  if (!::googlesql::ThreadHasEnoughStack()) {                      \
    return ::googlesql::functions::GetToJsonStackOverflowStatus(); \
  }

// Returns JSONValue constructed from NumericValue and BigNumericValue.
// If the value is int64 or uint64, use the corresponding value directly.
// If double, checks whether NumericValue/BigNumericValue can be
// converted to DOUBLE without precision loss and use the double value to
// construct the json if no loss. Otherwise returns the rounded value iff
// FEATURE_JSON_STRICT_NUMBER_PARSING is false.
template <typename T>
absl::StatusOr<JSONValue> ToJsonFromNumeric(
    const T& value, bool stringify_wide_number,
    const LanguageOptions& language_options, const absl::string_view type_name,
    bool canonicalize_zero,
    UnsupportedFieldsEnum::UnsupportedFields unsupported_fields) {
  if (!value.HasFractionalPart()) {
    // Check whether the value is int64
    if (value >= T(kInt64Min) && value <= T(kInt64Max)) {
      GOOGLESQL_ASSIGN_OR_RETURN(int64_t int64value, value.template To<int64_t>());
      return ToJson(Value::Int64(int64value), stringify_wide_number,
                    language_options, canonicalize_zero, unsupported_fields);
    }
    // Check whether the value is uint64
    if (value >= T(kUint64Min) && value <= T(kUint64Max)) {
      GOOGLESQL_ASSIGN_OR_RETURN(uint64_t uint64value, value.template To<uint64_t>());
      return ToJson(Value::Uint64(uint64value), stringify_wide_number,
                    language_options, canonicalize_zero, unsupported_fields);
    }
  }
  // Check whether the value can be converted to double without precision loss
  if (CheckNumberRoundtrip(value.ToString(), value.ToDouble()).ok()) {
    return JSONValue(value.ToDouble());
  }
  if (stringify_wide_number) {
    return JSONValue(value.ToString());
  }
  if (language_options.LanguageFeatureEnabled(
          FEATURE_JSON_STRICT_NUMBER_PARSING)) {
    return MakeEvalError() << "Failed to convert type " << type_name
                           << " to JSON";
  }
  return JSONValue(value.ToDouble());
}

// Returns the JSONValue from float and double.
// If the value is Infinity, -Infinity, or NaN, returns the json string
// representation. Otherwise returns json number type.
template <typename FloatType>
JSONValue ToJsonFromFloat(FloatType value, bool canonicalize_zero) {
  if (std::isnan(value)) {
    return JSONValue(std::string("NaN"));
  }
  if (std::isinf(value)) {
    return value > 0 ? JSONValue(std::string("Infinity"))
                     : JSONValue(std::string("-Infinity"));
  }
  ABSL_DCHECK(std::isfinite(value))
      << "Floating point number with unexpected properties" << value;
  if (canonicalize_zero && value == -0.0) {
    value = 0.0;
  }
  return JSONValue(value);
}

absl::StatusOr<JSONValue> ToJsonFromGraphElement(
    const Value& value, bool stringify_wide_numbers,
    const LanguageOptions& language_options, bool path_as_object,
    int current_nesting_level, bool canonicalize_zero,
    UnsupportedFieldsEnum::UnsupportedFields unsupported_fields);

absl::StatusOr<JSONValue> ToJsonFromGraphPath(
    const Value& value, bool stringify_wide_numbers,
    const LanguageOptions& language_options, bool path_as_object,
    int current_nesting_level, bool canonicalize_zero,
    UnsupportedFieldsEnum::UnsupportedFields unsupported_fields);

// Helper function for ToJson except that this function internally keeps
// tracking of <current_nesting_level> for STRUCT and ARRAY and checks stack
// space when <current_nesting_level> not less than
// kNestingLevelStackCheckThreshold. Returns StatusCode::kResourceExhausted when
// stack overflows.
absl::StatusOr<JSONValue> ToJsonHelper(
    const Value& value, bool stringify_wide_numbers,
    const LanguageOptions& language_options, bool path_as_object,
    int current_nesting_level, bool canonicalize_zero,
    UnsupportedFieldsEnum::UnsupportedFields unsupported_fields) {
  // Check the stack usage iff the <current_neesting_level> not less than
  // kNestingLevelStackCheckThreshold.
  if (current_nesting_level >= kNestingLevelStackCheckThreshold) {
    RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  }
  if (value.is_null()) {
    return JSONValue();
  }
  switch (value.type_kind()) {
    case TYPE_BOOL:
      return JSONValue(value.bool_value());
    case TYPE_INT32:
      return JSONValue(static_cast<int64_t>(value.int32_value()));
    case TYPE_UINT32:
      return JSONValue(static_cast<uint64_t>(value.uint32_value()));
    case TYPE_INT64: {
      const int64_t local = value.int64_value();
      if (stringify_wide_numbers && (local < kMinLosslessInt64ValueForJson ||
                                     local > kMaxLosslessInt64ValueForJson)) {
        return JSONValue(std::to_string(local));
      } else {
        return JSONValue(local);
      }
    }
    case TYPE_UINT64: {
      const uint64_t local = value.uint64_value();
      if (stringify_wide_numbers && local > kMaxLosslessInt64ValueForJson) {
        return JSONValue(std::to_string(local));
      } else {
        return JSONValue(local);
      }
    }
    case TYPE_FLOAT:
      return ToJsonFromFloat(value.float_value(), canonicalize_zero);
    case TYPE_DOUBLE:
      return ToJsonFromFloat(value.double_value(), canonicalize_zero);
    case TYPE_NUMERIC:
      return ToJsonFromNumeric(
          value.numeric_value(), stringify_wide_numbers, language_options,
          value.type()->ShortTypeName(language_options.product_mode()),
          canonicalize_zero, unsupported_fields);
    case TYPE_BIGNUMERIC:
      return ToJsonFromNumeric(
          value.bignumeric_value(), stringify_wide_numbers, language_options,
          value.type()->ShortTypeName(language_options.product_mode()),
          canonicalize_zero, unsupported_fields);
      break;
    case TYPE_STRING: {
      return JSONValue(value.string_value());
    }
    case TYPE_BYTES: {
      std::string tmp;
      JsonFromBytes(value.bytes_value(), &tmp, /*quote_output_string=*/false);
      return JSONValue(std::move(tmp));
    }
    case TYPE_TIMESTAMP: {
      std::string timestamp_string;
      GOOGLESQL_RETURN_IF_ERROR(JsonFromTimestamp(value.ToUnixPicos(), &timestamp_string,
                                        /*quote_output_string=*/false));
      return JSONValue(std::move(timestamp_string));
    }
    case TYPE_DATE: {
      std::string date_string;
      GOOGLESQL_RETURN_IF_ERROR(JsonFromDate(value.date_value(), &date_string,
                                   /*quote_output_string=*/false));
      return JSONValue(std::move(date_string));
    }
    case TYPE_DATETIME: {
      std::string datetime_string;
      GOOGLESQL_RETURN_IF_ERROR(JsonFromDatetime(value.datetime_value(), &datetime_string,
                                       /*quote_output_string=*/false));
      return JSONValue(std::move(datetime_string));
    }
    case TYPE_TIME: {
      std::string time_string;
      GOOGLESQL_RETURN_IF_ERROR(JsonFromTime(value.time_value(), &time_string,
                                   /*quote_output_string=*/false));
      return JSONValue(std::move(time_string));
    }
    case TYPE_INTERVAL:
      return JSONValue(value.interval_value().ToISO8601());
    case TYPE_UUID: {
      GOOGLESQL_ASSIGN_OR_RETURN(const UuidValue uuid_value, value.uuid_value());
      return JSONValue(uuid_value.ToString());
    }
    case TYPE_JSON: {
      if (value.is_validated_json()) {
        return JSONValue::CopyFrom(value.json_value());
      }
      auto input_json = JSONValue::ParseJSONString(
          value.json_value_unparsed(),
          JSONParsingOptions{
              .wide_number_mode =
                  (language_options.LanguageFeatureEnabled(
                       FEATURE_JSON_STRICT_NUMBER_PARSING)
                       ? JSONParsingOptions::WideNumberMode::kExact
                       : JSONParsingOptions::WideNumberMode::kRound)});
      if (!input_json.ok()) {
        return MakeEvalError() << input_json.status().message();
      }
      return input_json;
    }
    case TYPE_STRUCT: {
      JSONValue json_value;
      JSONValueRef json_value_ref = json_value.GetRef();
      json_value_ref.SetToEmptyObject();
      const StructType* struct_type = value.type()->AsStruct();
      int field_index = 0;
      for (const auto& field_value : value.fields()) {
        absl::string_view name = struct_type->field(field_index++).name;
        // If there is already a member existed, skip the further
        // processing as we only keep the first value of each member.
        if (json_value_ref.HasMember(name)) {
          continue;
        }
        GOOGLESQL_ASSIGN_OR_RETURN(
            JSONValue json_member_value,
            ToJsonHelper(field_value, stringify_wide_numbers, language_options,
                         path_as_object, current_nesting_level + 1,
                         canonicalize_zero, unsupported_fields));
        JSONValueRef member_value_ref = json_value_ref.GetMember(name);
        member_value_ref.Set(std::move(json_member_value));
      }
      return json_value;
    }
    case TYPE_ARRAY: {
      JSONValue json_value;
      JSONValueRef json_value_ref = json_value.GetRef();
      json_value_ref.SetToEmptyArray();
      if (!value.elements().empty()) {
        json_value_ref.GetArrayElement(value.num_elements() - 1);
        int element_index = 0;
        for (const auto& element_value : value.elements()) {
          auto json_element = ToJsonHelper(
              element_value, stringify_wide_numbers, language_options,
              path_as_object, current_nesting_level + 1, canonicalize_zero,
              UnsupportedFieldsEnum::FAIL);
          if (json_element.status().code() ==
              absl::StatusCode::kUnimplemented) {
            // The value type is not supported by TO_JSON, and we should
            // handle this entire array field as a whole, e.g. for IGNORE we
            // return just one `NULL`, instead of an `ARRAY(NULL, ...)`.
            if (unsupported_fields == UnsupportedFieldsEnum::FAIL) {
              return json_element.status();
            }
            if (unsupported_fields == UnsupportedFieldsEnum::IGNORE) {
              return JSONValue();
            }
            GOOGLESQL_RET_CHECK_EQ(unsupported_fields,
                         UnsupportedFieldsEnum::PLACEHOLDER);
            return JSONValue(
                absl::StrCat("Unsupported: array of ",
                             element_value.type()->ShortTypeName(
                                 language_options.product_mode())));
          }
          GOOGLESQL_RETURN_IF_ERROR(json_element.status());
          json_value_ref.GetArrayElement(element_index)
              .Set(std::move(json_element.value()));
          element_index++;
        }
      }
      return json_value;
    }
    case TYPE_ENUM: {
      if (absl::StatusOr<absl::string_view> name = value.EnumName();
          name.ok()) {
        return JSONValue(*name);
      } else {
        return JSONValue(static_cast<int64_t>(value.enum_value()));
      }
    }
    case TYPE_GRAPH_ELEMENT: {
      return ToJsonFromGraphElement(
          value, stringify_wide_numbers, language_options, path_as_object,
          current_nesting_level, canonicalize_zero, unsupported_fields);
    }
    case TYPE_GRAPH_PATH: {
      return ToJsonFromGraphPath(
          value, stringify_wide_numbers, language_options, path_as_object,
          current_nesting_level, canonicalize_zero, unsupported_fields);
    }
    case TYPE_RANGE: {
      JSONValue json_value;
      JSONValueRef json_value_ref = json_value.GetRef();
      json_value_ref.SetToEmptyObject();

      GOOGLESQL_ASSIGN_OR_RETURN(
          JSONValue json_member_value,
          ToJsonHelper(value.start(), stringify_wide_numbers, language_options,
                       path_as_object, current_nesting_level + 1,
                       canonicalize_zero, unsupported_fields));
      JSONValueRef member_value_ref = json_value_ref.GetMember("start");
      member_value_ref.Set(std::move(json_member_value));

      GOOGLESQL_ASSIGN_OR_RETURN(
          json_member_value,
          ToJsonHelper(value.end(), stringify_wide_numbers, language_options,
                       path_as_object, current_nesting_level + 1,
                       canonicalize_zero, unsupported_fields));
      member_value_ref = json_value_ref.GetMember("end");
      member_value_ref.Set(std::move(json_member_value));

      return json_value;
    }
    default: {
      if (unsupported_fields == UnsupportedFieldsEnum::FAIL) {
        return ::googlesql_base::UnimplementedErrorBuilder()
               << "Unsupported argument type "
               << value.type()->ShortTypeName(language_options.product_mode())
               << " for TO_JSON";
      }
      if (unsupported_fields == UnsupportedFieldsEnum::IGNORE) {
        return JSONValue();
      }
      GOOGLESQL_RET_CHECK_EQ(unsupported_fields, UnsupportedFieldsEnum::PLACEHOLDER);
      // The object of unsupported type will be replaced with a json string,
      // and no error will be triggered.
      return JSONValue(absl::StrCat(
          "Unsupported: ",
          value.type()->ShortTypeName(language_options.product_mode())));
    }
  }
}

absl::StatusOr<JSONValue> ToJsonFromGraphElement(
    const Value& value, bool stringify_wide_numbers,
    const LanguageOptions& language_options, bool path_as_object,
    int current_nesting_level, bool canonicalize_zero,
    UnsupportedFieldsEnum::UnsupportedFields unsupported_fields) {
  JSONValue json_value;
  JSONValueRef json_value_ref = json_value.GetRef();
  json_value_ref.SetToEmptyObject();
  if (language_options.LanguageFeatureEnabled(
          FEATURE_SQL_GRAPH_ELEMENT_DEFINITION_NAME_IN_JSON_RESULT)) {
    json_value_ref.GetMember("element_definition_name")
        .SetString(value.GetDefinitionName());
  }

  std::string tmp;
  JsonFromBytes(value.GetIdentifier(), &tmp, /*quote_output_string=*/false);
  json_value_ref.GetMember("identifier").SetString(tmp);

  JSONValueRef label_ref = json_value_ref.GetMember("labels");
  label_ref.SetToEmptyArray();
  absl::Span<const std::string> labels = value.GetLabels();
  for (int i = 0; i < labels.size(); ++i) {
    label_ref.GetArrayElement(i).SetString(labels[i]);
  }

  JSONValueRef properties_ref = json_value_ref.GetMember("properties");
  properties_ref.SetToEmptyObject();
  const GraphElementType* type = value.type()->AsGraphElement();
  GOOGLESQL_RET_CHECK_NE(type, nullptr);
  for (const std::string& property_name : value.property_names()) {
    GOOGLESQL_ASSIGN_OR_RETURN(const Value property_value,
                     value.FindValidPropertyValueByName(property_name));
    const PropertyType* property_type = type->FindPropertyType(property_name);
    // A static property must be defined in the graph element type.
    GOOGLESQL_RET_CHECK(
        (property_type != nullptr &&
         property_type->value_type->Equals(property_value.type()))
        // A dynamic property is json typed and graph element must be dynamic.
        || (type->is_dynamic() && property_value.type()->IsJson()));
    GOOGLESQL_ASSIGN_OR_RETURN(
        JSONValue v,
        ToJsonHelper(property_value, stringify_wide_numbers, language_options,
                     path_as_object, current_nesting_level + 1,
                     canonicalize_zero, unsupported_fields));
    properties_ref.GetMember(property_name).Set(std::move(v));
  }

  if (value.IsNode()) {
    json_value_ref.GetMember("kind").SetString("node");
    return json_value;
  }

  json_value_ref.GetMember("kind").SetString("edge");

  tmp.clear();
  JsonFromBytes(value.GetSourceNodeIdentifier(), &tmp,
                /*quote_output_string=*/false);
  json_value_ref.GetMember("source_node_identifier").SetString(tmp);

  tmp.clear();
  JsonFromBytes(value.GetDestNodeIdentifier(), &tmp,
                /*quote_output_string=*/false);
  json_value_ref.GetMember("destination_node_identifier").SetString(tmp);

  return json_value;
}

absl::StatusOr<JSONValue> ToJsonFromGraphPath(
    const Value& value, bool stringify_wide_numbers,
    const LanguageOptions& language_options, bool path_as_object,
    int current_nesting_level, bool canonicalize_zero,
    UnsupportedFieldsEnum::UnsupportedFields unsupported_fields) {
  bool to_json_object = path_as_object;

  if (!to_json_object) {
    JSONValue json_value;
    JSONValueRef json_value_ref = json_value.GetRef();
    json_value_ref.SetToEmptyArray();

    for (int i = 0; i < value.num_graph_elements(); ++i) {
      GOOGLESQL_RET_CHECK(value.graph_element(i).type()->IsGraphElement());
      GOOGLESQL_ASSIGN_OR_RETURN(JSONValue v,
                       ToJsonHelper(value.graph_element(i),
                                    stringify_wide_numbers, language_options,
                                    path_as_object, current_nesting_level + 1,
                                    canonicalize_zero, unsupported_fields));
      json_value_ref.GetArrayElement(i).Set(std::move(v));
    }
    return json_value;
  } else {
    JSONValue json_value;
    JSONValueRef json_value_ref = json_value.GetRef();
    json_value_ref.SetToEmptyObject();
    JSONValueRef elements_ref = json_value_ref.GetMember("elements");
    elements_ref.SetToEmptyArray();
    for (int i = 0; i < value.num_graph_elements(); ++i) {
      GOOGLESQL_ASSIGN_OR_RETURN(JSONValue v,
                       ToJsonHelper(value.graph_element(i),
                                    stringify_wide_numbers, language_options,
                                    path_as_object, current_nesting_level + 1,
                                    canonicalize_zero, unsupported_fields));
      elements_ref.GetArrayElement(i).Set(std::move(v));
    }

    return json_value;
  }
}

}  // namespace

absl::StatusOr<JSONValue> ToJson(
    const Value& value, bool stringify_wide_numbers,
    const LanguageOptions& language_options, bool canonicalize_zero,
    UnsupportedFieldsEnum::UnsupportedFields unsupported_fields,
    bool path_as_object) {
  return ToJsonHelper(value, stringify_wide_numbers, language_options,
                      path_as_object, /*current_nesting_level=*/0,
                      canonicalize_zero, unsupported_fields);
}

}  // namespace functions
}  // namespace googlesql
