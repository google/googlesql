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

#include "googlesql/public/cast.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/common/determinism.h"
#include "googlesql/common/errors.h"
#include "googlesql/common/graph_element_utils.h"
#include "googlesql/common/internal_value.h"
#include "googlesql/common/thread_stack.h"
#include "googlesql/common/unicode_utils.h"
#include "googlesql/common/utf_util.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/civil_time.h"
#include "googlesql/public/coercer.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/functions/cast_date_time.h"
#include "googlesql/public/functions/convert.h"
#include "googlesql/public/functions/convert_proto.h"
#include "googlesql/public/functions/convert_string.h"
#include "googlesql/public/functions/convert_string_with_format.h"
#include "googlesql/public/functions/date_time_util.h"
#include "googlesql/public/functions/datetime.pb.h"
#include "googlesql/public/functions/range.h"
#include "googlesql/public/functions/string.h"
#include "googlesql/public/functions/to_json.h"
#include "googlesql/public/functions/unsupported_fields.pb.h"
#include "googlesql/public/input_argument_type.h"
#include "googlesql/public/interval_value.h"
#include "googlesql/public/json_value.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/numeric_value.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/pico_time.h"
#include "googlesql/public/proto_value_conversion.h"
#include "googlesql/public/signature_match_result.h"
#include "googlesql/public/strings.h"
#include "googlesql/public/timestamp_picos_value.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/graph_element_type.h"
#include "googlesql/public/types/graph_path_type.h"
#include "googlesql/public/types/proto_type.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/uuid_value.h"
#include "googlesql/public/value.h"
#include "googlesql/public/variant_value.h"
#include "absl/algorithm/container.h"
#include "absl/base/attributes.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "googlesql/base/status_macros.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "google/protobuf/dynamic_message.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_builder.h"

namespace googlesql {

#define MAX_LITERAL_DISPLAY_LENGTH 60

namespace {

functions::TimestampScale GetTimestampScale(
    const LanguageOptions& language_options) {
  if (language_options.LanguageFeatureEnabled(FEATURE_TIMESTAMP_NANOS)) {
    return functions::kNanoseconds;
  } else {
    return functions::kMicroseconds;
  }
}

void AddToCastMap(TypeKind from, TypeKind to, CastFunctionType type,
                  CastHashMap* map) {
  googlesql_base::InsertIfNotPresent(map, {from, to},
                          {type, Type::GetTypeCoercionCost(to, from)});
}

const CastHashMap* InitializeGoogleSQLCasts() {
  CastHashMap* map = new CastHashMap();

  const CastFunctionType IMPLICIT = CastFunctionType::IMPLICIT;
  const CastFunctionType EXPLICIT = CastFunctionType::EXPLICIT;
  const CastFunctionType EXPLICIT_OR_LITERAL =
      CastFunctionType::EXPLICIT_OR_LITERAL;
  const CastFunctionType EXPLICIT_OR_LITERAL_OR_PARAMETER =
      CastFunctionType::EXPLICIT_OR_LITERAL_OR_PARAMETER;

#define ADD_TO_MAP(from_type, to_type, cast_type) \
  AddToCastMap(TYPE_##from_type, TYPE_##to_type, cast_type, map);

  // Note that by convention, all type kinds are currently castable to
  // themselves as IMPLICIT.

  // clang-format off
  ADD_TO_MAP(BOOL,       BOOL,       IMPLICIT);
  ADD_TO_MAP(BOOL,       INT32,      EXPLICIT);
  ADD_TO_MAP(BOOL,       INT64,      EXPLICIT);
  ADD_TO_MAP(BOOL,       UINT32,     EXPLICIT);
  ADD_TO_MAP(BOOL,       UINT64,     EXPLICIT);
  ADD_TO_MAP(BOOL,       STRING,     EXPLICIT);
  ADD_TO_MAP(BOOL,       JSON,       EXPLICIT);

  ADD_TO_MAP(INT32,      BOOL,       EXPLICIT);
  ADD_TO_MAP(INT32,      INT32,      IMPLICIT);
  ADD_TO_MAP(INT32,      INT64,      IMPLICIT);
  ADD_TO_MAP(INT32,      UINT32,     EXPLICIT_OR_LITERAL);
  ADD_TO_MAP(INT32,      UINT64,     EXPLICIT_OR_LITERAL);
  ADD_TO_MAP(INT32,      FLOAT,      EXPLICIT_OR_LITERAL);
  ADD_TO_MAP(INT32,      DOUBLE,     IMPLICIT);
  ADD_TO_MAP(INT32,      STRING,     EXPLICIT);
  ADD_TO_MAP(INT32,      ENUM,       EXPLICIT_OR_LITERAL_OR_PARAMETER);
  ADD_TO_MAP(INT32,      NUMERIC,    IMPLICIT);
  ADD_TO_MAP(INT32,      BIGNUMERIC, IMPLICIT);
  ADD_TO_MAP(INT32,      JSON,       EXPLICIT);

  ADD_TO_MAP(INT64,      BOOL,       EXPLICIT);
  ADD_TO_MAP(INT64,      INT32,      EXPLICIT_OR_LITERAL);
  ADD_TO_MAP(INT64,      INT64,      IMPLICIT);
  ADD_TO_MAP(INT64,      UINT32,     EXPLICIT_OR_LITERAL);
  ADD_TO_MAP(INT64,      UINT64,     EXPLICIT_OR_LITERAL);
  ADD_TO_MAP(INT64,      FLOAT,      EXPLICIT_OR_LITERAL);
  ADD_TO_MAP(INT64,      DOUBLE,     IMPLICIT);
  ADD_TO_MAP(INT64,      STRING,     EXPLICIT);
  ADD_TO_MAP(INT64,      ENUM,       EXPLICIT_OR_LITERAL_OR_PARAMETER);
  ADD_TO_MAP(INT64,      NUMERIC,    IMPLICIT);
  ADD_TO_MAP(INT64,      BIGNUMERIC, IMPLICIT);
  ADD_TO_MAP(INT64,      JSON,       EXPLICIT);

  ADD_TO_MAP(UINT32,     BOOL,       EXPLICIT);
  ADD_TO_MAP(UINT32,     INT32,      EXPLICIT_OR_LITERAL);
  ADD_TO_MAP(UINT32,     INT64,      IMPLICIT);
  ADD_TO_MAP(UINT32,     UINT32,     IMPLICIT);
  ADD_TO_MAP(UINT32,     UINT64,     IMPLICIT);
  ADD_TO_MAP(UINT32,     FLOAT,      EXPLICIT_OR_LITERAL);
  ADD_TO_MAP(UINT32,     DOUBLE,     IMPLICIT);
  ADD_TO_MAP(UINT32,     STRING,     EXPLICIT);
  ADD_TO_MAP(UINT32,     ENUM,       EXPLICIT_OR_LITERAL);
  ADD_TO_MAP(UINT32,     NUMERIC,    IMPLICIT);
  ADD_TO_MAP(UINT32,     BIGNUMERIC, IMPLICIT);
  ADD_TO_MAP(UINT32,     JSON,       EXPLICIT);

  ADD_TO_MAP(UINT64,     BOOL,       EXPLICIT);
  ADD_TO_MAP(UINT64,     INT32,      EXPLICIT_OR_LITERAL);
  ADD_TO_MAP(UINT64,     INT64,      EXPLICIT_OR_LITERAL);
  ADD_TO_MAP(UINT64,     UINT32,     EXPLICIT_OR_LITERAL);
  ADD_TO_MAP(UINT64,     UINT64,     IMPLICIT);
  ADD_TO_MAP(UINT64,     FLOAT,      EXPLICIT_OR_LITERAL);
  ADD_TO_MAP(UINT64,     DOUBLE,     IMPLICIT);
  ADD_TO_MAP(UINT64,     STRING,     EXPLICIT);
  ADD_TO_MAP(UINT64,     ENUM,       EXPLICIT_OR_LITERAL);
  ADD_TO_MAP(UINT64,     NUMERIC,    IMPLICIT);
  ADD_TO_MAP(UINT64,     BIGNUMERIC, IMPLICIT);
  ADD_TO_MAP(UINT64,     JSON,       EXPLICIT);

  ADD_TO_MAP(NUMERIC,    INT32,      EXPLICIT);
  ADD_TO_MAP(NUMERIC,    INT64,      EXPLICIT);
  ADD_TO_MAP(NUMERIC,    UINT32,     EXPLICIT);
  ADD_TO_MAP(NUMERIC,    UINT64,     EXPLICIT);
  ADD_TO_MAP(NUMERIC,    FLOAT,      EXPLICIT);
  ADD_TO_MAP(NUMERIC,    DOUBLE,     IMPLICIT);
  ADD_TO_MAP(NUMERIC,    STRING,     EXPLICIT);
  ADD_TO_MAP(NUMERIC,    NUMERIC,    IMPLICIT);
  ADD_TO_MAP(NUMERIC,    BIGNUMERIC, IMPLICIT);
  ADD_TO_MAP(NUMERIC,    JSON,       EXPLICIT);

  ADD_TO_MAP(BIGNUMERIC, INT32,      EXPLICIT);
  ADD_TO_MAP(BIGNUMERIC, INT64,      EXPLICIT);
  ADD_TO_MAP(BIGNUMERIC, UINT32,     EXPLICIT);
  ADD_TO_MAP(BIGNUMERIC, UINT64,     EXPLICIT);
  ADD_TO_MAP(BIGNUMERIC, FLOAT,      EXPLICIT);
  ADD_TO_MAP(BIGNUMERIC, DOUBLE,     IMPLICIT);
  ADD_TO_MAP(BIGNUMERIC, STRING,     EXPLICIT);
  ADD_TO_MAP(BIGNUMERIC, NUMERIC,    EXPLICIT);
  ADD_TO_MAP(BIGNUMERIC, BIGNUMERIC, IMPLICIT);
  ADD_TO_MAP(BIGNUMERIC, JSON,       EXPLICIT);

  ADD_TO_MAP(FLOAT,      INT32,      EXPLICIT);
  ADD_TO_MAP(FLOAT,      INT64,      EXPLICIT);
  ADD_TO_MAP(FLOAT,      UINT32,     EXPLICIT);
  ADD_TO_MAP(FLOAT,      UINT64,     EXPLICIT);
  ADD_TO_MAP(FLOAT,      FLOAT,      IMPLICIT);
  ADD_TO_MAP(FLOAT,      DOUBLE,     IMPLICIT);
  ADD_TO_MAP(FLOAT,      STRING,     EXPLICIT);
  ADD_TO_MAP(FLOAT,      NUMERIC,    EXPLICIT);
  ADD_TO_MAP(FLOAT,      BIGNUMERIC, EXPLICIT);
  ADD_TO_MAP(FLOAT,      JSON,       EXPLICIT);

  ADD_TO_MAP(DOUBLE,     INT32,      EXPLICIT);
  ADD_TO_MAP(DOUBLE,     INT64,      EXPLICIT);
  ADD_TO_MAP(DOUBLE,     UINT32,     EXPLICIT);
  ADD_TO_MAP(DOUBLE,     UINT64,     EXPLICIT);
  ADD_TO_MAP(DOUBLE,     FLOAT,      EXPLICIT_OR_LITERAL);
  ADD_TO_MAP(DOUBLE,     DOUBLE,     IMPLICIT);
  ADD_TO_MAP(DOUBLE,     STRING,     EXPLICIT);
  ADD_TO_MAP(DOUBLE,     NUMERIC,    EXPLICIT_OR_LITERAL);
  ADD_TO_MAP(DOUBLE,     BIGNUMERIC, EXPLICIT_OR_LITERAL);
  ADD_TO_MAP(DOUBLE,     JSON,       EXPLICIT);

  ADD_TO_MAP(STRING,     INT32,      EXPLICIT);
  ADD_TO_MAP(STRING,     INT64,      EXPLICIT);
  ADD_TO_MAP(STRING,     UINT32,     EXPLICIT);
  ADD_TO_MAP(STRING,     UINT64,     EXPLICIT);
  ADD_TO_MAP(STRING,     FLOAT,      EXPLICIT);
  ADD_TO_MAP(STRING,     DOUBLE,     EXPLICIT);
  ADD_TO_MAP(STRING,     STRING,     IMPLICIT);
  ADD_TO_MAP(STRING,     BYTES,      EXPLICIT);
  ADD_TO_MAP(STRING,     DATE,       EXPLICIT_OR_LITERAL_OR_PARAMETER);
  ADD_TO_MAP(STRING,     TIMESTAMP,  EXPLICIT_OR_LITERAL_OR_PARAMETER);
  ADD_TO_MAP(STRING,     TIME,       EXPLICIT_OR_LITERAL_OR_PARAMETER);
  ADD_TO_MAP(STRING,     DATETIME,   EXPLICIT_OR_LITERAL_OR_PARAMETER);
  ADD_TO_MAP(STRING,     INTERVAL,   EXPLICIT);
  ADD_TO_MAP(STRING,     ENUM,       EXPLICIT_OR_LITERAL_OR_PARAMETER);
  ADD_TO_MAP(STRING,     PROTO,      EXPLICIT_OR_LITERAL_OR_PARAMETER);
  ADD_TO_MAP(STRING,     BOOL,       EXPLICIT);
  ADD_TO_MAP(STRING,     NUMERIC,    EXPLICIT);
  ADD_TO_MAP(STRING,     BIGNUMERIC, EXPLICIT);
  ADD_TO_MAP(STRING,     RANGE,      EXPLICIT);
  ADD_TO_MAP(STRING,     JSON,       EXPLICIT);

  ADD_TO_MAP(BYTES,      BYTES,      IMPLICIT);
  ADD_TO_MAP(BYTES,      STRING,     EXPLICIT);
  ADD_TO_MAP(BYTES,      PROTO,      EXPLICIT_OR_LITERAL_OR_PARAMETER);
  ADD_TO_MAP(BYTES,      TOKENLIST,  EXPLICIT);
  ADD_TO_MAP(BYTES,      JSON,       EXPLICIT);

  ADD_TO_MAP(DATE,       DATE,       IMPLICIT);
  ADD_TO_MAP(DATE,       DATETIME,   IMPLICIT);
  ADD_TO_MAP(DATE,       TIMESTAMP,  EXPLICIT);
  ADD_TO_MAP(DATE,       STRING,     EXPLICIT);
  ADD_TO_MAP(DATE,       JSON,       EXPLICIT);

  ADD_TO_MAP(TIMESTAMP,  DATE,       EXPLICIT);
  ADD_TO_MAP(TIMESTAMP,  DATETIME,   EXPLICIT);
  ADD_TO_MAP(TIMESTAMP,  TIME,       EXPLICIT);
  ADD_TO_MAP(TIMESTAMP,  TIMESTAMP,  IMPLICIT);
  ADD_TO_MAP(TIMESTAMP,  STRING,     EXPLICIT);
  ADD_TO_MAP(TIMESTAMP,  JSON,       EXPLICIT);

  // TODO: Add relevant tests for TIME and DATETIME.

  ADD_TO_MAP(TIME,       TIME,       IMPLICIT);
  ADD_TO_MAP(TIME,       STRING,     EXPLICIT);
  ADD_TO_MAP(TIME,       JSON,       EXPLICIT);

  ADD_TO_MAP(DATETIME,   DATE,       EXPLICIT);
  ADD_TO_MAP(DATETIME,   DATETIME,   IMPLICIT);
  ADD_TO_MAP(DATETIME,   STRING,     EXPLICIT);
  ADD_TO_MAP(DATETIME,   TIME,       EXPLICIT);
  ADD_TO_MAP(DATETIME,   TIMESTAMP,  EXPLICIT);
  ADD_TO_MAP(DATETIME,   JSON,       EXPLICIT);

  ADD_TO_MAP(INTERVAL,   INTERVAL,   IMPLICIT);
  ADD_TO_MAP(INTERVAL,   STRING,     EXPLICIT);
  ADD_TO_MAP(INTERVAL,   JSON,       EXPLICIT);

  ADD_TO_MAP(GEOGRAPHY,  GEOGRAPHY,  IMPLICIT);
  ADD_TO_MAP(GEOGRAPHY,  JSON,       EXPLICIT);

  ADD_TO_MAP(JSON,       JSON,       IMPLICIT);

  ADD_TO_MAP(VARIANT,    VARIANT,    IMPLICIT);

  ADD_TO_MAP(TOKENLIST,  TOKENLIST,  IMPLICIT);
  ADD_TO_MAP(TOKENLIST,  BYTES,      EXPLICIT);

  ADD_TO_MAP(ENUM,       STRING,     EXPLICIT);
  ADD_TO_MAP(ENUM,       INT32,      EXPLICIT);
  ADD_TO_MAP(ENUM,       INT64,      EXPLICIT);
  ADD_TO_MAP(ENUM,       UINT32,     EXPLICIT);
  ADD_TO_MAP(ENUM,       UINT64,     EXPLICIT);
  ADD_TO_MAP(ENUM,       JSON,       EXPLICIT);

  ADD_TO_MAP(PROTO,      STRING,     EXPLICIT);
  ADD_TO_MAP(PROTO,      BYTES,      EXPLICIT);
  ADD_TO_MAP(PROTO,      JSON,       EXPLICIT);

  ADD_TO_MAP(RANGE,      STRING,     EXPLICIT);
  ADD_TO_MAP(RANGE,      JSON,       EXPLICIT);

  // The non-simple types show up in this table as IMPLICIT, but coercions of
  // any kind should only be allowed if the types are Equivalent.
  // This must be checked by the caller, like in TypeCoercesTo.
  ADD_TO_MAP(ENUM,       ENUM,       IMPLICIT);
  ADD_TO_MAP(PROTO,      PROTO,      IMPLICIT);
  ADD_TO_MAP(ARRAY,      ARRAY,      IMPLICIT);
  ADD_TO_MAP(ARRAY,      JSON,       EXPLICIT);

  ADD_TO_MAP(STRUCT,     STRUCT,     IMPLICIT);
  ADD_TO_MAP(STRUCT,     JSON,       EXPLICIT);

  ADD_TO_MAP(RANGE,      RANGE,      IMPLICIT);
  ADD_TO_MAP(RANGE,      JSON,       EXPLICIT);

  ADD_TO_MAP(GRAPH_ELEMENT, GRAPH_ELEMENT, IMPLICIT);
  ADD_TO_MAP(GRAPH_ELEMENT, JSON,    EXPLICIT);

  ADD_TO_MAP(GRAPH_PATH, GRAPH_PATH, IMPLICIT);
  ADD_TO_MAP(GRAPH_PATH, JSON,       EXPLICIT);
  ADD_TO_MAP(MAP,        MAP,        IMPLICIT);

  ADD_TO_MAP(UUID,       UUID,       IMPLICIT);
  ADD_TO_MAP(UUID,       STRING,     EXPLICIT);
  ADD_TO_MAP(UUID,       BYTES,      EXPLICIT);
  ADD_TO_MAP(UUID,       JSON,       EXPLICIT);

  ADD_TO_MAP(STRING,     UUID,       EXPLICIT_OR_LITERAL_OR_PARAMETER);
  ADD_TO_MAP(BYTES,      UUID,       EXPLICIT);
  ADD_TO_MAP(COLUMN_LIST_SPEC, COLUMN_LIST_SPEC, IMPLICIT);

  // Add universal casts for VARIANT.
  for (int i = 1; i <= TypeKind_MAX; ++i) {
    if (!TypeKind_IsValid(i)) continue;
    TypeKind kind = static_cast<TypeKind>(i);
    if (kind == TYPE_VARIANT) continue;
    // Casts from TYPE_UNKNOWN, TYPE_MEASURE, and TYPE_ROW to VARIANT are not
    // supported.
    if (kind == TYPE_UNKNOWN || kind == TYPE_MEASURE || kind == TYPE_ROW) {
      continue;
    }

    AddToCastMap(kind, TYPE_VARIANT, IMPLICIT, map);
    if (kind != TYPE_EXTENDED && kind != TYPE_DECLARATIVE) {
      AddToCastMap(TYPE_VARIANT, kind, EXPLICIT, map);
    }
  }

  // clang-format on

  return map;
}

// Returns a single uint64 that represents an (input kind, output kind) pair.
// Useful for switching on a combination of two kinds.
constexpr uint64_t FCT(TypeKind input_kind, TypeKind output_kind) {
  return ((static_cast<uint64_t>(input_kind) << 32) + output_kind);
}

template <typename FromType, typename ToType>
absl::StatusOr<Value> NumericCast(const Value& value) {
  absl::Status status;
  FromType in = value.Get<FromType>();
  ToType out;
  functions::Convert<FromType, ToType>(in, &out, &status);
  if (status.ok()) {
    return Value::Make<ToType>(out);
  } else {
    return status;
  }
}

template <typename FromType, typename ToType>
absl::StatusOr<Value> NumericValueCast(const FromType& in) {
  absl::Status status;
  ToType out;
  functions::Convert<FromType, ToType>(in, &out, &status);
  if (status.ok()) {
    return Value::Make<ToType>(out);
  } else {
    return status;
  }
}

// Conversion function from a numeric Value to a string Value that
// handles NULL Values but otherwise just wraps the GoogleSQL function
// library function (which does not handle NULL values).
// The function is invoked like:
//   status = NumericToString<int32>(value)
//
// Crashes if the Value type does not correspond with <T>.
template <typename T>
absl::StatusOr<Value> NumericToString(const Value& v,
                                      bool canonicalize_zero = false) {
  if (v.is_null()) return Value::NullString();
  T value = v.Get<T>();
  std::string str;
  absl::Status error;
  if (googlesql::functions::NumericToString<T>(value, &str, &error,
                                               canonicalize_zero)) {
    return Value::String(str);
  } else {
    return error;
  }
}

// Conversion function from a numeric Value to a string Value that
// handles NULL Values but otherwise just wraps the GoogleSQL function
// library function (which does not handle NULL values).
// The function is invoked like:
//   status = StringToNumeric<int32>(value)
//
// Crashes if the Value <v> is not a string.
template <typename T>
absl::StatusOr<Value> StringToNumeric(const Value& v) {
  if (v.is_null()) return Value::MakeNull<T>();
  const std::string& value = v.string_value();
  T out;
  absl::Status error;
  if (googlesql::functions::StringToNumeric<T>(value, &out, &error)) {
    return Value::Make<T>(out);
  } else {
    return error;
  }
}

// Returns whether this cast is a map-entry cast (see DoMapEntryCast).
bool IsMapEntryCast(const Type* from, const Type* to) {
  return from->IsStruct() && from->AsStruct()->fields().size() == 2 &&
         to->IsProto() && to->AsProto()->descriptor()->options().map_entry();
}

// Casts a Value <v> to a JSON value.
absl::StatusOr<Value> CastToJSON(const Value& v,
                                 const LanguageOptions& language_options,
                                 bool canonicalize_zero) {
  if (v.is_null()) {
    return Value::NullJson();
  }
  GOOGLESQL_ASSIGN_OR_RETURN(JSONValue json_value,
                   googlesql::functions::ToJson(
                       v, /*stringify_wide_numbers=*/false, language_options,
                       canonicalize_zero,
                       /*unsupported_fields=*/
                       googlesql::functions::UnsupportedFieldsEnum::FAIL,
                       /*path_as_object=*/false,
                       /*reject_nan_infinity_floats=*/true));
  return Value::Json(std::move(json_value));
}

static std::string ValueOrUnbounded(std::optional<std::string> value) {
  return value ? *value : "UNBOUNDED";
}

// TODO: b/542423700 - Support casting composite types containing VARIANT (e.g.
// ARRAY<VARIANT>, STRUCT<VARIANT>) to JSON.
// Returns true if `from_type` is a non-VARIANT type whose structure and
// component types are supported for casting to JSON.
bool IsTypeSupportedForCastToJson(const Type* from_type) {
  switch (from_type->kind()) {
    case TYPE_JSON:
    case TYPE_BOOL:
    case TYPE_INT32:
    case TYPE_INT64:
    case TYPE_UINT32:
    case TYPE_UINT64:
    case TYPE_FLOAT:
    case TYPE_DOUBLE:
    case TYPE_NUMERIC:
    case TYPE_BIGNUMERIC:
    case TYPE_STRING:
    case TYPE_BYTES:
    case TYPE_DATE:
    case TYPE_TIME:
    case TYPE_DATETIME:
    case TYPE_TIMESTAMP:
    case TYPE_UUID:
    case TYPE_GEOGRAPHY:
    case TYPE_ARRAY:
    case TYPE_STRUCT:
    case TYPE_ENUM:
    case TYPE_INTERVAL:
    case TYPE_RANGE:
    case TYPE_GRAPH_ELEMENT:
    case TYPE_GRAPH_PATH:
    case TYPE_PROTO:
      return absl::c_all_of(from_type->ComponentTypes(),
                            IsTypeSupportedForCastToJson);
    default:
      return false;
  }
}

bool IsNonVariantTypeCastableToJson(const Type* from_type,
                                    const LanguageOptions& language_options) {
  if (from_type->IsJson()) {
    return true;
  }
  if (!language_options.LanguageFeatureEnabled(
          LanguageFeature::FEATURE_CAST_TO_JSON_TYPE)) {
    return false;
  }
  return IsTypeSupportedForCastToJson(from_type);
}

}  // namespace

bool IsTypeCastableToJson(const Type* from_type,
                          const LanguageOptions& language_options) {
  if (from_type->IsVariant()) {
    return language_options.LanguageFeatureEnabled(
        LanguageFeature::FEATURE_VARIANT_TYPE);
  }
  return IsNonVariantTypeCastableToJson(from_type, language_options);
}

bool IsTypeCastableToVariant(const Type* from_type,
                             const LanguageOptions& language_options) {
  // Implicit coercion to/from VARIANT always works since it is a SimpleType,
  // irrespective of whether FEATURE_VARIANT_TYPE is enabled.
  if (from_type->IsVariant()) {
    return true;
  }
  if (!language_options.LanguageFeatureEnabled(
          LanguageFeature::FEATURE_VARIANT_TYPE)) {
    return false;
  }
  switch (from_type->kind()) {
    case TYPE_UNKNOWN:
    case TYPE_MEASURE:
    case TYPE_ROW:
      return false;
    default:
      return absl::c_all_of(
          from_type->ComponentTypes(), [&](const Type* component_type) {
            return IsTypeCastableToVariant(component_type, language_options);
          });
  }
}

bool IsTypeCastableFromVariant(const Type* to_type,
                               const LanguageOptions& language_options) {
  // Implicit coercion to/from VARIANT always works since it is a SimpleType,
  // irrespective of whether FEATURE_VARIANT_TYPE is enabled.
  if (to_type->IsVariant()) {
    return true;
  }
  if (!language_options.LanguageFeatureEnabled(
          LanguageFeature::FEATURE_VARIANT_TYPE)) {
    return false;
  }
  switch (to_type->kind()) {
    case TYPE_UNKNOWN:
    case TYPE_MEASURE:
    case TYPE_ROW:
    case TYPE_EXTENDED:
    case TYPE_DECLARATIVE:
      // TODO: b/554949919 - Support casting from VARIANT to declarative and
      // extended types.
      // Casting from VARIANT to extended and declarative types is not supported
      // in GoogleSQL core: declarative types only coerce to/from their backing
      // types, and extended types require engine-specific catalog conversions
      // (matching JSON).
      return false;
    default:
      return absl::c_all_of(
          to_type->ComponentTypes(), [&](const Type* component_type) {
            return IsTypeCastableFromVariant(component_type, language_options);
          });
  }
}

namespace {

enum class JsonCastProperty { kEqualityPreserving, kOrderPreserving };

bool CheckJsonCastProperty(const Type* arg_type, JsonCastProperty property) {
  switch (arg_type->kind()) {
    // Types valid for both Equality and Ordering.
    case TYPE_JSON:
    case TYPE_BOOL:
    case TYPE_INT32:
    case TYPE_UINT32:
    case TYPE_INT64:
    case TYPE_UINT64:
    case TYPE_FLOAT:
    case TYPE_DOUBLE:
    case TYPE_NUMERIC:
    case TYPE_BIGNUMERIC:
    case TYPE_STRING:
    case TYPE_DATE:
    case TYPE_TIMESTAMP:
    case TYPE_DATETIME:
    case TYPE_TIME:
    case TYPE_UUID:
      return true;

    // Types valid only for Equality preserving.
    case TYPE_BYTES:
    case TYPE_ENUM:
      return property == JsonCastProperty::kEqualityPreserving;

    // Recursive types.
    case TYPE_ARRAY:
      return CheckJsonCastProperty(arg_type->AsArray()->element_type(),
                                   property);

    case TYPE_RANGE:
      if (property == JsonCastProperty::kOrderPreserving) {
        return false;
      }
      return CheckJsonCastProperty(arg_type->AsRange()->element_type(),
                                   property);

    default:
      return false;
  }
}

}  // namespace

bool IsJsonCastEqualityPreserving(const Type* arg_type) {
  return CheckJsonCastProperty(arg_type, JsonCastProperty::kEqualityPreserving);
}

bool IsJsonCastOrderPreserving(const Type* arg_type) {
  return CheckJsonCastProperty(arg_type, JsonCastProperty::kOrderPreserving);
}

// Returns a hash map with TypeKindPair as key, and CastFunctionProperty as
// value.  This identifies whether the (from, to) cast pairs in the key are
// allowed explicitly, implicitly, etc., and what the cost is for the cast.
// If a (from, to) pair is not in the map, then that cast is not allowed.
const CastHashMap& GetGoogleSQLCasts() {
  static const CastHashMap* cast_hash_map = InitializeGoogleSQLCasts();
  return *cast_hash_map;
}

const CastFunctionProperty* GetCastProperty(
    TypeKind from_type_kind, TypeKind to_type_kind,
    const LanguageOptions* language_options) {
  // Non-identity casts involving VARIANT require FEATURE_VARIANT_TYPE.
  if ((from_type_kind == TYPE_VARIANT || to_type_kind == TYPE_VARIANT) &&
      from_type_kind != to_type_kind && language_options != nullptr &&
      !language_options->LanguageFeatureEnabled(FEATURE_VARIANT_TYPE)) {
    return nullptr;
  }
  // TODO: check language options here for future JSON casts.
  return googlesql_base::FindOrNull(GetGoogleSQLCasts(),
                         TypeKindPair(from_type_kind, to_type_kind));
}

CastHashMap GetCastPropertyMap(const LanguageOptions* language_options) {
  if (language_options == nullptr) {
    return GetGoogleSQLCasts();
  }
  CastHashMap map;
  for (const auto& [type_kind_pair, property] : GetGoogleSQLCasts()) {
    if (GetCastProperty(type_kind_pair.first, type_kind_pair.second,
                        language_options) != nullptr) {
      map.insert({type_kind_pair, property});
    }
  }
  return map;
}

bool SupportsImplicitCoercion(CastFunctionType type) {
  return type == CastFunctionType::IMPLICIT;
}

bool SupportsLiteralCoercion(CastFunctionType type) {
  return type == CastFunctionType::IMPLICIT ||
         type == CastFunctionType::EXPLICIT_OR_LITERAL ||
         type == CastFunctionType::EXPLICIT_OR_LITERAL_OR_PARAMETER;
}

bool SupportsParameterCoercion(CastFunctionType type) {
  return type == CastFunctionType::IMPLICIT ||
         type == CastFunctionType::EXPLICIT_OR_LITERAL_OR_PARAMETER;
}

bool SupportsExplicitCast(CastFunctionType type) {
  return type == CastFunctionType::IMPLICIT ||
         type == CastFunctionType::EXPLICIT ||
         type == CastFunctionType::EXPLICIT_OR_LITERAL ||
         type == CastFunctionType::EXPLICIT_OR_LITERAL_OR_PARAMETER;
}

namespace {

class CastContext;

// Casts a VARIANT value view to `to_type`, dispatching by Variant category
// (Variant null, Primitive, Object, Array, or pass-through inner value).
absl::StatusOr<Value> CastFromVariant(const CastContext& context,
                                      const VariantValueView& view,
                                      const Type* to_type,
                                      bool* out_found_non_deterministic,
                                      int current_nesting_level = 0);

// CastContext is an abstract class containing basic set of properties and
// methods needed to execute a cast. Serves as a base class for classes
// responsible for execution of validated (CastValue) and plain
// (CastValueWithoutTypeValidation) casts.
class CastContext {
 public:
  // Deprecated. Use CastContext(default_timezone, current_timestamp,
  // language_options) instead.
  // Otherwise, if the cast is a cast from STRING to DATE, DATETIME or TIMESTAMP
  // with a format string, the cast will fail with error that current timestamp
  // is not set.
  ABSL_DEPRECATED(
      "Use CastContext(default_timezone, current_timestamp, language_options) "
      "instead")
  CastContext(absl::TimeZone default_timezone,
              const LanguageOptions& language_options,
              bool canonicalize_zero = false)
      : canonicalize_zero_(canonicalize_zero),
        default_timezone_(default_timezone),
        language_options_(language_options),
        current_timestamp_(std::nullopt) {}

  CastContext(absl::TimeZone default_timezone,
              std::optional<absl::Time> current_timestamp,
              const LanguageOptions& language_options,
              bool canonicalize_zero = false)
      : canonicalize_zero_(canonicalize_zero),
        default_timezone_(default_timezone),
        language_options_(language_options),
        current_timestamp_(current_timestamp) {
    if (current_timestamp_.has_value()) {
      // Extracting the DATE from the current timestamp should never fail since
      // it will be in the supported range 0001-01-01 to 9999-12-31.
      int32_t current_date;
      GOOGLESQL_CHECK_OK(functions::ExtractFromTimestamp(
          functions::DATE, current_timestamp_.value(), default_timezone_,
          &current_date));
      current_date_ = current_date;
    }
  }

  virtual ~CastContext() {}

  CastContext(const CastContext&) = delete;
  CastContext& operator=(const CastContext&) = delete;

  absl::StatusOr<Value> CastValue(
      const Value& from_value, const Type* to_type,
      const std::optional<std::string>& format = std::nullopt,
      bool* out_found_non_deterministic = nullptr) const;

  const LanguageOptions& language_options() const { return language_options_; }
  bool canonicalize_zero() const { return canonicalize_zero_; }

 protected:
  const absl::TimeZone& default_timezone() const { return default_timezone_; }
  std::optional<absl::Time> current_timestamp() const {
    return current_timestamp_;
  }
  std::optional<int32_t> current_date() const { return current_date_; }

  // If true, the sign on a signed zero is removed when converting numeric type
  // to string.
  // TODO : remove this flag when all engines have
  // rolled out this new behavior.
  const bool canonicalize_zero_ = false;

 private:
  // Executes a cast which involves extended types: source and/or destination
  // type is extended.
  virtual absl::StatusOr<Value> CastWithExtendedType(
      const Value& from_value, const Type* to_type) const = 0;

  // Checks that coercion is valid using Coercer.
  virtual absl::Status ValidateLiteralValueCoercion(
      const Value& from_value, const Type* to_type) const = 0;

  const absl::TimeZone default_timezone_;
  const LanguageOptions& language_options_;
  const std::optional<absl::Time> current_timestamp_;
  std::optional<int32_t> current_date_;
};

// Tries to perform a STRUCT->PROTO cast if to_type is a map_entry. See
// (broken link). <from_value> is a two-field struct where
// the fields represent the key and value of the requested map_entry proto
// in <to_type>.
absl::StatusOr<Value> DoMapEntryCast(const CastContext& context,
                                     const Value& from_value,
                                     const Type* to_type,
                                     bool* out_found_non_deterministic) {
  GOOGLESQL_RET_CHECK(from_value.is_valid());
  GOOGLESQL_RET_CHECK(IsMapEntryCast(from_value.type(), to_type));

  if (from_value.is_null()) {
    return MakeEvalError() << "Cannot cast a NULL struct to a proto map entry";
  }

  const ProtoType* to_proto_type = to_type->AsProto();
  TypeFactory type_factory;
  const Type* key_type;
  const Type* value_type;

  GOOGLESQL_RETURN_IF_ERROR(to_proto_type->GetFieldTypeByTagNumber(
      to_proto_type->map_key()->number(), &type_factory, &key_type));
  GOOGLESQL_RETURN_IF_ERROR(to_proto_type->GetFieldTypeByTagNumber(
      to_proto_type->map_value()->number(), &type_factory, &value_type));

  GOOGLESQL_ASSIGN_OR_RETURN(Value key, context.CastValue(from_value.field(0), key_type,
                                                /*format=*/std::nullopt,
                                                out_found_non_deterministic));
  GOOGLESQL_ASSIGN_OR_RETURN(
      Value value,
      context.CastValue(from_value.field(1), value_type,
                        /*format=*/std::nullopt, out_found_non_deterministic));

  google::protobuf::Arena arena;
  google::protobuf::DynamicMessageFactory factory;
  google::protobuf::Message* message =
      factory.GetPrototype(to_proto_type->descriptor())->New(&arena);

  bool use_wire_format_annotations = true;
  GOOGLESQL_RETURN_IF_ERROR(MergeValueToProtoField(key, to_proto_type->map_key(),
                                         use_wire_format_annotations, &factory,
                                         message));
  GOOGLESQL_RETURN_IF_ERROR(MergeValueToProtoField(value, to_proto_type->map_value(),
                                         use_wire_format_annotations, &factory,
                                         message));

  absl::Cord bytes;
  GOOGLESQL_RET_CHECK(message->SerializeToString(&bytes));
  return Value::Proto(to_proto_type, bytes);
}

absl::StatusOr<Value> NumericToStringWithFormat(const Value& v,
                                                absl::string_view format,
                                                ProductMode product_mode,
                                                bool canonicalize_zero) {
  if (v.is_null()) {
    return Value::NullString();
  }

  GOOGLESQL_ASSIGN_OR_RETURN(const std::string str,
                   googlesql::functions::NumericalToStringWithFormat(
                       v, format, product_mode, canonicalize_zero));
  return Value::String(str);
}

// Validates the value-aware implicit cast between two graph element types, and
// looks up the static properties from the `from_value` according to those of
// the `to_type` and returns them.
static absl::Status ValidateGraphElementValueImplicitCastAndAddStaticProperties(
    const Value& from_value, const GraphElementType* to_type,
    const LanguageOptions& language_options,
    std::vector<Value::Property>& result_static_properties,
    absl::flat_hash_set<absl::string_view>& static_property_names) {
  result_static_properties.clear();
  static_property_names.clear();

  const GraphElementType* from_graph_type = from_value.type()->AsGraphElement();
  // The coercer currently ensures that they're both node / edge, and from
  // the same graph reference. Checking here for completeness.
  if (to_type->IsEdge() != from_graph_type->IsEdge()) {
    return MakeSqlError()
           << "Cannot cast graph element type between node and edge type: "
           << from_graph_type->ShortTypeName(language_options.product_mode())
           << " to " << to_type->ShortTypeName(language_options.product_mode());
  }
  if (!absl::c_equal(to_type->graph_reference(),
                     from_graph_type->graph_reference(),
                     googlesql_base::CaseEqual)) {
    return MakeSqlError()
           << "Cannot cast between graph element types with different "
              "graph references: "
           << from_graph_type->ShortTypeName(language_options.product_mode())
           << " to " << to_type->ShortTypeName(language_options.product_mode());
  }

  if (!language_options.LanguageFeatureEnabled(
          FEATURE_SQL_GRAPH_DYNAMIC_ELEMENT_TYPE)) {
    GOOGLESQL_RET_CHECK(!from_graph_type->is_dynamic() && !to_type->is_dynamic())
        << "Dynamic graph element type is not supported";
  }
  if (from_graph_type->is_dynamic() && !to_type->is_dynamic()) {
    return MakeSqlError()
           << "Cannot cast dynamic graph element to static graph element";
  }

  // The same static property name must have the same value type.
  // Dynamic property with conflicting name in the from type shadows static
  // property with the same name in the to type.
  for (const PropertyType& property_type : to_type->property_types()) {
    static_property_names.insert(property_type.name);
    if (absl::StatusOr<Value> from_property_val =
            from_value.FindStaticPropertyByName(property_type.name);
        from_property_val.ok()) {
      GOOGLESQL_RET_CHECK(
          from_property_val.value().type()->Equals(property_type.value_type))
          << "In the same graph, property of the same name must have the "
             "same value type. Current property type: "
          << from_property_val.value().type()->ShortTypeName(
                 language_options.product_mode())
          << "; other property type: "
          << property_type.value_type->ShortTypeName(
                 language_options.product_mode());
      result_static_properties.push_back(
          {property_type.name, std::move(from_property_val.value())});
    }
  }
  // Coercion should be checked by the caller of `CastValue`.
  GOOGLESQL_RET_CHECK(from_graph_type->CoercibleTo(to_type));
  return absl::OkStatus();
}

absl::StatusOr<Value> CastContext::CastValue(
    const Value& from_value, const Type* to_type,
    const std::optional<std::string>& format,
    bool* out_found_non_deterministic) const {
  GOOGLESQL_RET_CHECK(from_value.is_valid());
  // Use a shorter name inside the body of this method.
  const Value& v = from_value;

  if (v.type()->Equals(to_type)) {
    // Coercion from a value to the exact same type always works.
    return v;
  }

  if (to_type->IsVariant()) {
    if (!IsTypeCastableToVariant(v.type(), language_options())) {
      return MakeSqlError()
             << "Unsupported cast from "
             << v.type()->ShortTypeName(language_options().product_mode())
             << " to "
             << to_type->ShortTypeName(language_options().product_mode());
    }
    return Value::Variant(v);
  }

  if (v.type()->IsVariant()) {
    if (!IsTypeCastableFromVariant(to_type, language_options())) {
      return MakeSqlError()
             << "Unsupported cast from "
             << v.type()->ShortTypeName(language_options().product_mode())
             << " to "
             << to_type->ShortTypeName(language_options().product_mode());
    }
    if (format.has_value()) {
      return MakeSqlError()
             << "FORMAT is not allowed for cast from "
             << v.type()->ShortTypeName(language_options().product_mode())
             << " to "
             << to_type->ShortTypeName(language_options().product_mode());
    }
    // A SQL NULL VARIANT casts to a SQL NULL value of the target type.
    if (v.is_null()) {
      return Value::Null(to_type);
    }
    GOOGLESQL_ASSIGN_OR_RETURN(VariantValueView view, v.variant_value());
    bool variant_non_deterministic = false;
    GOOGLESQL_ASSIGN_OR_RETURN(Value casted_val,
                     CastFromVariant(*this, view, to_type,
                                     out_found_non_deterministic != nullptr
                                         ? &variant_non_deterministic
                                         : nullptr));
    if (variant_non_deterministic && out_found_non_deterministic != nullptr) {
      *out_found_non_deterministic = true;
    }
    return casted_val;
  }

  if (from_value.type()->IsExtendedType() || to_type->IsExtendedType()) {
    return CastWithExtendedType(from_value, to_type);
  }

  // Special case: STRUCT are not generally castable to PROTO, but there is an
  // exception for two-field structs whose fields are castable to the fields
  // of a map_entry protocol buffer (see (broken link)).
  if (language_options().LanguageFeatureEnabled(
          LanguageFeature::FEATURE_PROTO_MAPS) &&
      IsMapEntryCast(from_value.type(), to_type)) {
    return DoMapEntryCast(*this, from_value, to_type,
                          out_found_non_deterministic);
  }

  // Check to see if the type kinds are castable.
  const CastFunctionProperty* property =
      GetCastProperty(v.type_kind(), to_type->kind(), &language_options());
  if (property == nullptr) {
    return MakeSqlError()
           << "Unsupported cast from "
           << v.type()->ShortTypeName(language_options().product_mode())
           << " to "
           << to_type->ShortTypeName(language_options().product_mode());
  }

  //  NULL handling for Values occurs here.
  if (v.is_null()) {
    if (!v.type()->IsSimpleType() && v.type_kind() == to_type->kind()) {
      // This is a cast of a complex type to a complex type with the same
      // kind.  Type kind checks are not enough to verify that the cast
      // between types is valid (i.e., array to array or struct to struct),
      // so perform a literal coercion check to see if the complex types
      // are compatible and therefore a NULL value can cast from one to
      // the other.
      GOOGLESQL_RETURN_IF_ERROR(ValidateLiteralValueCoercion(v, to_type));
    }
    // We have already validated that this is a valid cast for NULL values,
    // so just return a NULL value of <to_type>.
    return Value::Null(to_type);
  }

  // TODO: Consider breaking this up, as the switch is extremely
  // large.
  switch (FCT(v.type()->kind(), to_type->kind())) {
    // Numeric casts. Identity casts are handled above.
    case FCT(TYPE_INT32, TYPE_INT64):
      return NumericCast<int32_t, int64_t>(v);
    case FCT(TYPE_INT32, TYPE_UINT32):
      return NumericCast<int32_t, uint32_t>(v);
    case FCT(TYPE_INT32, TYPE_UINT64):
      return NumericCast<int32_t, uint64_t>(v);
    case FCT(TYPE_INT32, TYPE_BOOL):
      return NumericCast<int32_t, bool>(v);
    case FCT(TYPE_INT32, TYPE_FLOAT):
      return NumericCast<int32_t, float>(v);
    case FCT(TYPE_INT32, TYPE_DOUBLE):
      return NumericCast<int32_t, double>(v);
    case FCT(TYPE_INT32, TYPE_STRING):
      if (format.has_value()) {
        return NumericToStringWithFormat(v, format.value(),
                                         language_options().product_mode(),
                                         canonicalize_zero_);
      } else {
        return NumericToString<int32_t>(v, canonicalize_zero_);
      }
    case FCT(TYPE_INT32, TYPE_NUMERIC):
      return NumericCast<int32_t, NumericValue>(v);
    case FCT(TYPE_INT32, TYPE_BIGNUMERIC):
      return NumericCast<int32_t, BigNumericValue>(v);

    case FCT(TYPE_UINT32, TYPE_INT32):
      return NumericCast<uint32_t, int32_t>(v);
    case FCT(TYPE_UINT32, TYPE_INT64):
      return NumericCast<uint32_t, int64_t>(v);
    case FCT(TYPE_UINT32, TYPE_UINT64):
      return NumericCast<uint32_t, uint64_t>(v);
    case FCT(TYPE_UINT32, TYPE_BOOL):
      return NumericCast<uint32_t, bool>(v);
    case FCT(TYPE_UINT32, TYPE_FLOAT):
      return NumericCast<uint32_t, float>(v);
    case FCT(TYPE_UINT32, TYPE_DOUBLE):
      return NumericCast<uint32_t, double>(v);
    case FCT(TYPE_UINT32, TYPE_STRING):
      if (format.has_value()) {
        return NumericToStringWithFormat(v, format.value(),
                                         language_options().product_mode(),
                                         canonicalize_zero_);
      } else {
        return NumericToString<uint32_t>(v, canonicalize_zero_);
      }
    case FCT(TYPE_UINT32, TYPE_NUMERIC):
      return NumericCast<uint32_t, NumericValue>(v);
    case FCT(TYPE_UINT32, TYPE_BIGNUMERIC):
      return NumericCast<uint32_t, BigNumericValue>(v);

    case FCT(TYPE_INT64, TYPE_INT32):
      return NumericCast<int64_t, int32_t>(v);
    case FCT(TYPE_INT64, TYPE_UINT32):
      return NumericCast<int64_t, uint32_t>(v);
    case FCT(TYPE_INT64, TYPE_UINT64):
      return NumericCast<int64_t, uint64_t>(v);
    case FCT(TYPE_INT64, TYPE_BOOL):
      return NumericCast<int64_t, bool>(v);
    case FCT(TYPE_INT64, TYPE_FLOAT):
      return NumericCast<int64_t, float>(v);
    case FCT(TYPE_INT64, TYPE_DOUBLE):
      return NumericCast<int64_t, double>(v);
    case FCT(TYPE_INT64, TYPE_STRING):
      if (format.has_value()) {
        return NumericToStringWithFormat(v, format.value(),
                                         language_options().product_mode(),
                                         canonicalize_zero_);
      } else {
        return NumericToString<int64_t>(v, canonicalize_zero_);
      }
    case FCT(TYPE_INT64, TYPE_NUMERIC):
      return NumericCast<int64_t, NumericValue>(v);
    case FCT(TYPE_INT64, TYPE_BIGNUMERIC):
      return NumericCast<int64_t, BigNumericValue>(v);

    case FCT(TYPE_UINT64, TYPE_INT32):
      return NumericCast<uint64_t, int32_t>(v);
    case FCT(TYPE_UINT64, TYPE_INT64):
      return NumericCast<uint64_t, int64_t>(v);
    case FCT(TYPE_UINT64, TYPE_UINT32):
      return NumericCast<uint64_t, uint32_t>(v);
    case FCT(TYPE_UINT64, TYPE_BOOL):
      return NumericCast<uint64_t, bool>(v);
    case FCT(TYPE_UINT64, TYPE_FLOAT):
      return NumericCast<uint64_t, float>(v);
    case FCT(TYPE_UINT64, TYPE_DOUBLE):
      return NumericCast<uint64_t, double>(v);
    case FCT(TYPE_UINT64, TYPE_STRING):
      if (format.has_value()) {
        return NumericToStringWithFormat(v, format.value(),
                                         language_options().product_mode(),
                                         canonicalize_zero_);
      } else {
        return NumericToString<uint64_t>(v, canonicalize_zero_);
      }
    case FCT(TYPE_UINT64, TYPE_NUMERIC):
      return NumericCast<uint64_t, NumericValue>(v);
    case FCT(TYPE_UINT64, TYPE_BIGNUMERIC):
      return NumericCast<uint64_t, BigNumericValue>(v);

    case FCT(TYPE_BOOL, TYPE_INT32):
      return NumericCast<bool, int32_t>(v);
    case FCT(TYPE_BOOL, TYPE_INT64):
      return NumericCast<bool, int64_t>(v);
    case FCT(TYPE_BOOL, TYPE_UINT32):
      return NumericCast<bool, uint32_t>(v);
    case FCT(TYPE_BOOL, TYPE_UINT64):
      return NumericCast<bool, uint64_t>(v);
    case FCT(TYPE_BOOL, TYPE_STRING):
      return NumericToString<bool>(v, canonicalize_zero_);

    case FCT(TYPE_FLOAT, TYPE_INT32):
      return NumericCast<float, int32_t>(v);
    case FCT(TYPE_FLOAT, TYPE_INT64):
      return NumericCast<float, int64_t>(v);
    case FCT(TYPE_FLOAT, TYPE_UINT32):
      return NumericCast<float, uint32_t>(v);
    case FCT(TYPE_FLOAT, TYPE_UINT64):
      return NumericCast<float, uint64_t>(v);
    case FCT(TYPE_FLOAT, TYPE_DOUBLE):
      return NumericCast<float, double>(v);
    case FCT(TYPE_FLOAT, TYPE_STRING):
      if (format.has_value()) {
        return NumericToStringWithFormat(v, format.value(),
                                         language_options().product_mode(),
                                         canonicalize_zero_);
      } else {
        return NumericToString<float>(v, canonicalize_zero_);
      }
    case FCT(TYPE_FLOAT, TYPE_NUMERIC):
      return NumericCast<float, NumericValue>(v);
    case FCT(TYPE_FLOAT, TYPE_BIGNUMERIC):
      return NumericCast<float, BigNumericValue>(v);

    case FCT(TYPE_DOUBLE, TYPE_INT32):
      return NumericCast<double, int32_t>(v);
    case FCT(TYPE_DOUBLE, TYPE_INT64):
      return NumericCast<double, int64_t>(v);
    case FCT(TYPE_DOUBLE, TYPE_UINT32):
      return NumericCast<double, uint32_t>(v);
    case FCT(TYPE_DOUBLE, TYPE_UINT64):
      return NumericCast<double, uint64_t>(v);
    case FCT(TYPE_DOUBLE, TYPE_FLOAT):
      return NumericCast<double, float>(v);
    case FCT(TYPE_DOUBLE, TYPE_STRING):
      if (format.has_value()) {
        return NumericToStringWithFormat(v, format.value(),
                                         language_options().product_mode(),
                                         canonicalize_zero_);
      } else {
        return NumericToString<double>(v, canonicalize_zero_);
      }
    case FCT(TYPE_DOUBLE, TYPE_NUMERIC):
      return NumericCast<double, NumericValue>(v);
    case FCT(TYPE_DOUBLE, TYPE_BIGNUMERIC):
      return NumericCast<double, BigNumericValue>(v);

    case FCT(TYPE_INT32, TYPE_ENUM):
    case FCT(TYPE_INT64, TYPE_ENUM):
    case FCT(TYPE_UINT32, TYPE_ENUM): {
      const Value to_value = Value::Enum(to_type->AsEnum(), v.ToInt64());
      if (!to_value.is_valid()) {
        return MakeEvalError()
               << "Out of range cast of integer " << v.ToInt64()
               << " to enum type "
               << to_type->ShortTypeName(language_options().product_mode());
      }
      return to_value;
    }
    case FCT(TYPE_UINT64, TYPE_ENUM): {
      // Static cast may turn out-of-bound uint64's to negative int64's which
      // will yield invalid enums.
      const Value to_value = Value::Enum(
          to_type->AsEnum(), static_cast<int64_t>(v.uint64_value()));
      if (!to_value.is_valid()) {
        return MakeEvalError()
               << "Out of range cast of integer " << v.uint64_value()
               << " to enum type "
               << to_type->ShortTypeName(language_options().product_mode());
      }
      return to_value;
    }

    case FCT(TYPE_STRING, TYPE_BOOL):
      return StringToNumeric<bool>(v);
    case FCT(TYPE_STRING, TYPE_INT32):
      return StringToNumeric<int32_t>(v);
    case FCT(TYPE_STRING, TYPE_INT64):
      return StringToNumeric<int64_t>(v);
    case FCT(TYPE_STRING, TYPE_UINT32):
      return StringToNumeric<uint32_t>(v);
    case FCT(TYPE_STRING, TYPE_UINT64):
      return StringToNumeric<uint64_t>(v);
    case FCT(TYPE_STRING, TYPE_FLOAT):
      return StringToNumeric<float>(v);
    case FCT(TYPE_STRING, TYPE_DOUBLE):
      return StringToNumeric<double>(v);
    case FCT(TYPE_STRING, TYPE_NUMERIC):
      return StringToNumeric<NumericValue>(v);
    case FCT(TYPE_STRING, TYPE_BIGNUMERIC):
      return StringToNumeric<BigNumericValue>(v);

    case FCT(TYPE_STRING, TYPE_ENUM): {
      const Value to_value = Value::Enum(to_type->AsEnum(), v.string_value());
      if (!to_value.is_valid()) {
        return MakeEvalError()
               << "Out of range cast of string '" << v.string_value()
               << "' to enum type "
               << to_type->ShortTypeName(language_options().product_mode());
      }
      return to_value;
    }

    case FCT(TYPE_STRING, TYPE_DATE): {
      int32_t date;
      if (format.has_value()) {
        if (!current_date().has_value()) {
          return MakeEvalError() << "current timestamp is not set";
        }

        GOOGLESQL_RETURN_IF_ERROR(functions::CastStringToDate(
            format.value(), v.string_value(), current_date().value(), &date));
      } else {
        GOOGLESQL_RETURN_IF_ERROR(
            functions::ConvertStringToDate(v.string_value(), &date));
      }
      return Value::Date(date);
    }
    case FCT(TYPE_STRING, TYPE_TIMESTAMP): {
      // TODO: These should be using the non-deprecated signature
      // that includes an argument to indicate if a timezone is allowed in
      // the string or not.  If not allowed and there is a timezone then
      // an error should be provided.
      functions::TimestampScale scale;
      if (language_options().LanguageFeatureEnabled(FEATURE_TIMESTAMP_PICOS)) {
        scale = functions::kPicoseconds;
      } else if (language_options().LanguageFeatureEnabled(
                     FEATURE_TIMESTAMP_NANOS)) {
        scale = functions::kNanoseconds;
      } else {
        scale = functions::kMicroseconds;
      }

      // All GoogleSQL Timestamps are internally stored with picosecond
      // precision (with 0 padding if needed). We use scale when parsing the
      // timestamp string to enforce the precision limit set by language feature
      // flags.
      PicoTime pico_time;
      if (format.has_value()) {
        if (!current_timestamp().has_value()) {
          return MakeEvalError() << "current timestamp is not set";
        }

        GOOGLESQL_ASSIGN_OR_RETURN(
            pico_time, functions::CastStringToTimestamp(
                           format.value(), v.string_value(), default_timezone(),
                           current_timestamp().value()));
      } else {
        GOOGLESQL_RETURN_IF_ERROR(functions::ConvertStringToTimestamp(
            v.string_value(), default_timezone(), scale,
            /*allow_tz_in_str=*/true, &pico_time));
      }
      return Value::Timestamp(TimestampPicosValue(pico_time));
    }
    case FCT(TYPE_TIMESTAMP, TYPE_STRING): {
      std::string timestamp;
      if (format.has_value()) {
        GOOGLESQL_ASSIGN_OR_RETURN(timestamp,
                         functions::CastFormatTimestampToString(
                             format.value(), v.ToUnixPicos().ToPicoTime(),
                             default_timezone()));
      } else {
        GOOGLESQL_RETURN_IF_ERROR(functions::ConvertTimestampToString(
            v.ToUnixPicos().ToPicoTime(), default_timezone(), &timestamp));
      }
      return Value::String(timestamp);
    }
    case FCT(TYPE_DATE, TYPE_TIMESTAMP): {
      int64_t timestamp;
      GOOGLESQL_RETURN_IF_ERROR(functions::ConvertDateToTimestamp(
          v.date_value(), functions::kMicroseconds, default_timezone(),
          &timestamp));
      return Value::TimestampFromUnixMicros(timestamp);
    }
    case FCT(TYPE_TIMESTAMP, TYPE_DATE): {
      GOOGLESQL_ASSIGN_OR_RETURN(int64_t date64,
                       ExtractFromTimestamp(functions::DateTimestampPart::DATE,
                                            v.ToUnixPicos().ToPicoTime(),
                                            default_timezone()));
      int32_t date32 = static_cast<int32_t>(date64);
      GOOGLESQL_RET_CHECK_EQ(date32, date64);
      return Value::Date(date32);
    }
    case FCT(TYPE_STRING, TYPE_BYTES):
      if (format.has_value()) {
        std::string output;
        GOOGLESQL_RETURN_IF_ERROR(functions::StringToBytes(v.string_value(),
                                                 format.value(), &output));
        return Value::Bytes(output);
      }

      return Value::Bytes(v.string_value());

    case FCT(TYPE_STRING, TYPE_PROTO): {
      if (to_type->AsProto()->descriptor() == nullptr) {
        // TODO: Cannot currently get here, since a ProtoType
        // requires a non-nullptr descriptor.  This may change when we
        // implement  opaque protos.  Additionally, opaque protos may affect
        // the ability to successfully parse or serialize the proto (note
        // also that a fully-defined proto might have a descendant field
        // that is an opaque proto).
        return MakeEvalError()
               << "Invalid cast from string to opaque proto type "
               << to_type->ShortTypeName(language_options().product_mode());
      }
      google::protobuf::DynamicMessageFactory msg_factory;
      std::unique_ptr<google::protobuf::Message> message(
          msg_factory.GetPrototype(to_type->AsProto()->descriptor())->New());
      absl::Status error;
      functions::StringToProto(v.string_value(), message.get(), &error);
      GOOGLESQL_RETURN_IF_ERROR(error);
      // TODO: SerializeToCord returns false if not all required
      // fields are present.  If we want to allow missing required fields
      // We could use SerializePartialToCord().
      absl::Cord cord_value;
      bool is_valid = message->SerializeToString(&cord_value);
      if (!is_valid) {
        // TODO: This does not seem reachable given that we just
        // successfully parsed the string to a valid message.
        std::string output_string(ToStringLiteral(v.string_value()));
        output_string =
            PrettyTruncateUTF8(output_string, MAX_LITERAL_DISPLAY_LENGTH);
        return MakeEvalError()
               << "Invalid cast to type "
               << to_type->ShortTypeName(language_options().product_mode())
               << " from string: " << output_string;
      }
      return Value::Proto(to_type->AsProto(), std::move(cord_value));
    }

    case FCT(TYPE_BYTES, TYPE_STRING): {
      if (format.has_value()) {
        std::string output;
        GOOGLESQL_RETURN_IF_ERROR(
            functions::BytesToString(v.bytes_value(), format.value(), &output));
        return Value::String(output);
      }

      const std::string& utf8 = v.bytes_value();
      // No escaping is needed since the bytes value is already unescaped.
      if (!IsWellFormedUTF8(utf8)) {
        return MakeEvalError() << "Invalid cast of bytes to UTF8 string";
      }
      return Value::String(utf8);
    }

    case FCT(TYPE_BYTES, TYPE_PROTO):
      // Opaque proto support does not affect this implementation, which does
      // no validation.
      return Value::Proto(to_type->AsProto(), absl::Cord(v.bytes_value()));
    case FCT(TYPE_BYTES, TYPE_TOKENLIST): {
      auto tokenlist = tokens::TokenList::FromBytesUnvalidated(v.bytes_value());
      if (!tokenlist.IsValid()) {
        return MakeEvalError() << "Invalid tokenlist encoding";
      }
      return Value::TokenList(std::move(tokenlist));
    }
    case FCT(TYPE_DATE, TYPE_STRING): {
      std::string date;
      if (format.has_value()) {
        GOOGLESQL_RETURN_IF_ERROR(functions::CastFormatDateToString(
            format.value(), v.date_value(), &date));
      } else {
        GOOGLESQL_RETURN_IF_ERROR(functions::ConvertDateToString(v.date_value(), &date));
      }
      return Value::String(date);
    }

    case FCT(TYPE_ENUM, TYPE_STRING):
      return Value::String(v.EnumDisplayName());

    case FCT(TYPE_ENUM, TYPE_INT32):
      return Value::Int32(v.enum_value());
    case FCT(TYPE_ENUM, TYPE_INT64):
      return NumericValueCast<int32_t, int64_t>(v.enum_value());
    case FCT(TYPE_ENUM, TYPE_UINT32):
      return NumericValueCast<int32_t, uint32_t>(v.enum_value());
    case FCT(TYPE_ENUM, TYPE_UINT64):
      return NumericValueCast<int32_t, uint64_t>(v.enum_value());

    case FCT(TYPE_ENUM, TYPE_ENUM): {
      if (!v.type()->Equivalent(to_type)) {
        return MakeSqlError()
               << "Invalid enum cast from "
               << v.type()->ShortTypeName(language_options().product_mode())
               << " to "
               << to_type->ShortTypeName(language_options().product_mode());
      }
      const Value to_value = Value::Enum(to_type->AsEnum(), v.enum_value());
      if (!to_value.is_valid()) {
        return MakeEvalError()
               << "Out of range enum value " << v.ToInt64()
               << " when converting enum type "
               << to_type->ShortTypeName(language_options().product_mode())
               << " to a different definition of the same enum";
      }
      return to_value;
    }

    case FCT(TYPE_STRING, TYPE_TIME): {
      TimeValue time;
      if (format.has_value()) {
        GOOGLESQL_RETURN_IF_ERROR(functions::CastStringToTime(
            format.value(), v.string_value(),
            GetTimestampScale(language_options()), &time));
      } else {
        GOOGLESQL_RETURN_IF_ERROR(functions::ConvertStringToTime(
            v.string_value(), GetTimestampScale(language_options()), &time));
      }
      return Value::Time(time);
    }
    case FCT(TYPE_TIME, TYPE_STRING): {
      std::string result;
      if (format.has_value()) {
        GOOGLESQL_RETURN_IF_ERROR(functions::CastFormatTimeToString(
            format.value(), v.time_value(), &result));
      } else {
        GOOGLESQL_RETURN_IF_ERROR(functions::ConvertTimeToString(
            v.time_value(), GetTimestampScale(language_options()), &result));
      }
      return Value::String(result);
    }
    case FCT(TYPE_TIMESTAMP, TYPE_TIME): {
      TimeValue time;
      GOOGLESQL_RETURN_IF_ERROR(functions::ConvertTimestampToTime(
          v.ToTime(), default_timezone(), &time));
      return Value::Time(time);
    }

    case FCT(TYPE_STRING, TYPE_DATETIME): {
      DatetimeValue datetime;
      if (format.has_value()) {
        if (!current_date().has_value()) {
          return MakeEvalError() << "current timestamp is not set";
        }

        GOOGLESQL_RETURN_IF_ERROR(functions::CastStringToDatetime(
            format.value(), v.string_value(),
            GetTimestampScale(language_options()), current_date().value(),
            &datetime));
      } else {
        GOOGLESQL_RETURN_IF_ERROR(functions::ConvertStringToDatetime(
            v.string_value(), GetTimestampScale(language_options()),
            &datetime));
      }
      return Value::Datetime(datetime);
    }
    case FCT(TYPE_DATETIME, TYPE_STRING): {
      std::string result;
      if (format.has_value()) {
        GOOGLESQL_RETURN_IF_ERROR(functions::CastFormatDatetimeToString(
            format.value(), v.datetime_value(), &result));
      } else {
        GOOGLESQL_RETURN_IF_ERROR(functions::ConvertDatetimeToString(
            v.datetime_value(), GetTimestampScale(language_options()),
            &result));
      }
      return Value::String(result);
    }
    case FCT(TYPE_DATETIME, TYPE_TIMESTAMP): {
      absl::Time time;
      GOOGLESQL_RETURN_IF_ERROR(functions::ConvertDatetimeToTimestamp(
          v.datetime_value(), default_timezone(), &time));
      return Value::Timestamp(time);
    }
    case FCT(TYPE_TIMESTAMP, TYPE_DATETIME): {
      DatetimeValue datetime;
      GOOGLESQL_RETURN_IF_ERROR(functions::ConvertTimestampToDatetime(
          v.ToTime(), default_timezone(), &datetime));
      return Value::Datetime(datetime);
    }
    case FCT(TYPE_DATETIME, TYPE_DATE): {
      int32_t date;
      GOOGLESQL_RETURN_IF_ERROR(functions::ExtractFromDatetime(
          functions::DATE, v.datetime_value(), &date));
      return Value::Date(date);
    }
    case FCT(TYPE_DATE, TYPE_DATETIME): {
      DatetimeValue datetime;
      GOOGLESQL_QCHECK_OK(
          functions::ConstructDatetime(v.date_value(), TimeValue(), &datetime));
      return Value::Datetime(datetime);
    }
    case FCT(TYPE_DATETIME, TYPE_TIME): {
      TimeValue time;
      GOOGLESQL_RETURN_IF_ERROR(
          functions::ExtractTimeFromDatetime(v.datetime_value(), &time));
      return Value::Time(time);
    }

    case FCT(TYPE_INTERVAL, TYPE_STRING): {
      return Value::String(v.interval_value().ToString());
    }
    case FCT(TYPE_STRING, TYPE_INTERVAL): {
      bool allow_nanos =
          language_options().LanguageFeatureEnabled(FEATURE_TIMESTAMP_NANOS) ||
          !language_options().LanguageFeatureEnabled(
              FEATURE_ENFORCE_MICROS_MODE_IN_INTERVAL_TYPE);
      GOOGLESQL_ASSIGN_OR_RETURN(IntervalValue interval,
                       IntervalValue::Parse(v.string_value(), allow_nanos));
      return Value::Interval(interval);
    }

    case FCT(TYPE_TOKENLIST, TYPE_BYTES):
      return Value::Bytes(v.tokenlist_value().GetBytes());

    case FCT(TYPE_STRUCT, TYPE_STRUCT): {
      const StructType* v_type = v.type()->AsStruct();
      std::vector<Value> casted_field_values(v_type->num_fields());
      if (v_type->num_fields() != to_type->AsStruct()->num_fields()) {
        return MakeSqlError()
               << "Unsupported cast from "
               << v.type()->ShortTypeName(language_options().product_mode())
               << " to "
               << to_type->ShortTypeName(language_options().product_mode());
      }
      for (int i = 0; i < v_type->num_fields(); ++i) {
        absl::StatusOr<Value> field_cast_result =
            CastValue(v.field(i), to_type->AsStruct()->field(i).type,
                      /*format=*/std::nullopt, out_found_non_deterministic);
        if (!field_cast_result.ok()) {
          const auto& field = to_type->AsStruct()->field(i);
          return googlesql_base::StatusBuilder(field_cast_result.status()).SetPrepend()
                 << "Error casting field "
                 << (field.name.empty() ? absl::StrCat("at index ", i)
                                        : absl::StrCat("'", field.name, "'"))
                 << " in STRUCT: ";
        }
        casted_field_values[i] = std::move(field_cast_result.value());
      }

      return Value::Struct(to_type->AsStruct(), casted_field_values);
    }

    case FCT(TYPE_PROTO, TYPE_STRING): {
      if (v.type()->AsProto()->descriptor() == nullptr) {
        // TODO: Cannot currently get here.  The implementation of
        // opaque protos may affect this.
        return MakeEvalError()
               << "Invalid cast from opaque proto type "
               << to_type->ShortTypeName(language_options().product_mode())
               << " to string";
      }
      google::protobuf::DynamicMessageFactory msg_factory;
      std::unique_ptr<google::protobuf::Message> message(
          msg_factory.GetPrototype(v.type()->AsProto()->descriptor())->New());
      bool is_valid = message->ParsePartialFromString(v.ToCord());
      if (!is_valid) {
        std::string display_bytes =
            PrettyTruncateUTF8(ToBytesLiteral(std::string(v.ToCord())),
                               MAX_LITERAL_DISPLAY_LENGTH);
        return MakeEvalError()
               << "Invalid cast to string from type "
               << v.type()->ShortTypeName(language_options().product_mode())
               << ": " << display_bytes;
      }
      absl::Status error;
      absl::Cord printed_msg;
      functions::ProtoToString(message.get(), &printed_msg, &error);
      GOOGLESQL_RETURN_IF_ERROR(error);
      return Value::String(std::string(printed_msg));
    }

    case FCT(TYPE_PROTO, TYPE_BYTES):
      // Opaque proto support does not affect this implementation, which does
      // no validation.
      return Value::Bytes(v.ToCord());

    case FCT(TYPE_PROTO, TYPE_PROTO):
      if (!v.type()->Equivalent(to_type)) {
        return MakeSqlError()
               << "Invalid proto cast from "
               << v.type()->ShortTypeName(language_options().product_mode())
               << " to "
               << to_type->ShortTypeName(language_options().product_mode());
      }
      // We don't currently do any validity checking on the serialized bytes.
      return Value::Proto(to_type->AsProto(), v.ToCord());

    case FCT(TYPE_ARRAY, TYPE_ARRAY): {
      GOOGLESQL_RETURN_IF_ERROR(ValidateLiteralValueCoercion(v, to_type));

      const Type* to_element_type = to_type->AsArray()->element_type();
      std::vector<Value> casted_elements(v.num_elements());
      for (int i = 0; i < v.num_elements(); ++i) {
        if (v.element(i).is_null() && !to_element_type->IsVariant()) {
          casted_elements[i] = Value::Null(to_element_type);
        } else {
          absl::StatusOr<Value> element_cast_result =
              CastValue(v.element(i), to_element_type,
                        /*format=*/std::nullopt, out_found_non_deterministic);
          if (!element_cast_result.ok()) {
            return googlesql_base::StatusBuilder(element_cast_result.status())
                       .SetPrepend()
                   << "Error casting element at index " << i << " in ARRAY: ";
          }
          casted_elements[i] = std::move(element_cast_result.value());
        }
      }
      return InternalValue::ArrayChecked(to_type->AsArray(),
                                         InternalValue::order_kind(v),
                                         std::move(casted_elements));
    }

    case FCT(TYPE_NUMERIC, TYPE_INT32):
      return NumericCast<NumericValue, int32_t>(v);
    case FCT(TYPE_NUMERIC, TYPE_INT64):
      return NumericCast<NumericValue, int64_t>(v);
    case FCT(TYPE_NUMERIC, TYPE_UINT32):
      return NumericCast<NumericValue, uint32_t>(v);
    case FCT(TYPE_NUMERIC, TYPE_UINT64):
      return NumericCast<NumericValue, uint64_t>(v);
    case FCT(TYPE_NUMERIC, TYPE_FLOAT):
      return NumericCast<NumericValue, float>(v);
    case FCT(TYPE_NUMERIC, TYPE_DOUBLE):
      return NumericCast<NumericValue, double>(v);
    case FCT(TYPE_NUMERIC, TYPE_BIGNUMERIC):
      return NumericCast<NumericValue, BigNumericValue>(v);
    case FCT(TYPE_NUMERIC, TYPE_STRING):
      if (format.has_value()) {
        return NumericToStringWithFormat(v, format.value(),
                                         language_options().product_mode(),
                                         canonicalize_zero_);
      } else {
        return NumericToString<NumericValue>(v, canonicalize_zero_);
      }
    case FCT(TYPE_BIGNUMERIC, TYPE_INT32):
      return NumericCast<BigNumericValue, int32_t>(v);
    case FCT(TYPE_BIGNUMERIC, TYPE_INT64):
      return NumericCast<BigNumericValue, int64_t>(v);
    case FCT(TYPE_BIGNUMERIC, TYPE_UINT32):
      return NumericCast<BigNumericValue, uint32_t>(v);
    case FCT(TYPE_BIGNUMERIC, TYPE_UINT64):
      return NumericCast<BigNumericValue, uint64_t>(v);
    case FCT(TYPE_BIGNUMERIC, TYPE_FLOAT):
      return NumericCast<BigNumericValue, float>(v);
    case FCT(TYPE_BIGNUMERIC, TYPE_DOUBLE):
      return NumericCast<BigNumericValue, double>(v);
    case FCT(TYPE_BIGNUMERIC, TYPE_NUMERIC):
      return NumericCast<BigNumericValue, NumericValue>(v);
    case FCT(TYPE_BIGNUMERIC, TYPE_STRING):
      if (format.has_value()) {
        return NumericToStringWithFormat(v, format.value(),
                                         language_options().product_mode(),
                                         canonicalize_zero_);
      } else {
        return NumericToString<BigNumericValue>(v, canonicalize_zero_);
      }
    case FCT(TYPE_STRING, TYPE_RANGE): {
      if (v.is_null()) {
        return Value::Null(to_type);
      }
      GOOGLESQL_ASSIGN_OR_RETURN(const auto boundaries,
                       ParseRangeBoundaries(v.string_value()));
      const Type* element_type = to_type->AsRange()->element_type();
      Value start = Value::Null(element_type);
      if (boundaries.start.has_value()) {
        GOOGLESQL_ASSIGN_OR_RETURN(start,
                         CastValue(Value::String(boundaries.start.value()),
                                   element_type, format));
      }
      Value end = Value::Null(element_type);
      if (boundaries.end.has_value()) {
        GOOGLESQL_ASSIGN_OR_RETURN(end, CastValue(Value::String(boundaries.end.value()),
                                        element_type, format));
      }
      return Value::MakeRange(start, end);
    }
    case FCT(TYPE_RANGE, TYPE_STRING): {
      if (v.is_null()) {
        return Value::NullString();
      }
      std::optional<std::string> start = std::nullopt;
      if (!v.start().is_null()) {
        GOOGLESQL_ASSIGN_OR_RETURN(Value start_str,
                         CastValue(v.start(), to_type, format));
        start = start_str.string_value();
      }
      std::optional<std::string> end = std::nullopt;
      if (!v.end().is_null()) {
        GOOGLESQL_ASSIGN_OR_RETURN(Value end_str, CastValue(v.end(), to_type, format));
        end = end_str.string_value();
      }
      return Value::String(absl::StrFormat("[%s, %s)", ValueOrUnbounded(start),
                                           ValueOrUnbounded(end)));
    }
    case FCT(TYPE_GRAPH_ELEMENT, TYPE_GRAPH_ELEMENT): {
      // Casts of graph elements are only generated when resolving graph
      // function calls to satisfy the type signature: there is no behavior
      // difference. Here we only change the property set and keep the element
      // identifiers.
      const auto* to_graph_type = to_type->AsGraphElement();

      // See below for coercion rules between graph element types.
      // --------------------------------------------------------------
      // |           |         |  RESULT TYPE PROPERTIES    |         |
      // | FROM TYPE | TO TYPE |    STATIC   |    DYNAMIC   | RESULT  |
      // --------------------------------------------------------------
      // | Static    | Static  | TO TYPE     | nullopt      | ALLOWED |
      // | Static    | Dynamic | TO TYPE     | {}           | ALLOWED |
      // | Dynamic   | Static  | N/A         | N/A          | ERROR   |
      // | Dynamic   | Dynamic | TO TYPE     | FROM TYPE    | ALLOWED |
      // --------------------------------------------------------------
      std::vector<Value::Property> to_type_static_properties;
      absl::flat_hash_set<absl::string_view> static_property_names;
      GOOGLESQL_RETURN_IF_ERROR(
          ValidateGraphElementValueImplicitCastAndAddStaticProperties(
              v, to_graph_type, language_options(), to_type_static_properties,
              static_property_names));

      Value::GraphElementLabelsAndProperties labels_and_properties = {
          .static_labels = {v.GetLabels().begin(), v.GetLabels().end()},
          .static_properties = to_type_static_properties};

      // If casting from static to static graph element type, we do not need to
      // compute the dynamic properties.
      const auto* from_graph_type = v.type()->AsGraphElement();
      if (!from_graph_type->is_dynamic() && !to_graph_type->is_dynamic()) {
        if (v.IsEdge()) {
          Value::GraphEdgeKeysAndElementTableNames
              edge_keys_and_element_table_names = {
                  .identifier = v.GetIdentifier(),
                  .element_table_name = v.GetDefinitionName(),
                  .source = {.identifier = v.GetSourceNodeIdentifier(),
                             .keys = {v.GetGraphEdgeSourceKeys().begin(),
                                      v.GetGraphEdgeSourceKeys().end()},
                             .element_table_name =
                                 v.GetSourceNodeElementTableName()},
                  .dest = {
                      .identifier = v.GetDestNodeIdentifier(),
                      .keys = {v.GetGraphEdgeDestKeys().begin(),
                               v.GetGraphEdgeDestKeys().end()},
                      .element_table_name = v.GetDestNodeElementTableName()}};
          return Value::MakeGraphEdge(to_graph_type, labels_and_properties,
                                      edge_keys_and_element_table_names
          );
        } else {
          Value::GraphNodeKeysAndElementTableNames
              node_keys_and_element_table_name = {
                  .identifier = v.GetIdentifier(),
                  .keys = {v.GetGraphNodeKeys().begin(),
                           v.GetGraphNodeKeys().end()},
                  .element_table_name = v.GetDefinitionName()};
          return Value::MakeGraphNode(to_graph_type, labels_and_properties,
                                      node_keys_and_element_table_name
          );
        }
      }
      // Dynamic properties of the casted type are computed from the from type's
      // dynamic properties.
      // - If the from type is static, the casted type uses an empty dynamic
      //   properties JSON value.
      // - If the from type is dynamic, dynamic properties with conflicting
      //   names compared with static properties in the to type are dropped.
      std::vector<Value::Property> from_type_dynamic_properties;
      if (from_graph_type->is_dynamic()) {
        for (const std::string& property_name : v.property_names()) {
          if (!static_property_names.contains(property_name)) {
            GOOGLESQL_ASSIGN_OR_RETURN(const Value from_type_property_val,
                             v.FindValidPropertyValueByName(property_name));
            from_type_dynamic_properties.push_back(
                {property_name, from_type_property_val});
          }
        }
      }
      GOOGLESQL_ASSIGN_OR_RETURN(
          const JSONValue json_value,
          MakePropertiesJsonValue(absl::MakeSpan(from_type_dynamic_properties),
                                  language_options()));
      labels_and_properties.dynamic_properties = json_value.GetConstRef();
      if (v.IsEdge()) {
        Value::GraphEdgeKeysAndElementTableNames
            edge_keys_and_element_table_names = {
                .identifier = v.GetIdentifier(),
                .element_table_name = v.GetDefinitionName(),
                .source = {.identifier = v.GetSourceNodeIdentifier(),
                           .keys = {v.GetGraphEdgeSourceKeys().begin(),
                                    v.GetGraphEdgeSourceKeys().end()},
                           .element_table_name =
                               v.GetSourceNodeElementTableName()},
                .dest = {
                    .identifier = v.GetDestNodeIdentifier(),
                    .keys = {v.GetGraphEdgeDestKeys().begin(),
                             v.GetGraphEdgeDestKeys().end()},
                    .element_table_name = v.GetDestNodeElementTableName()}};
        return Value::MakeGraphEdge(to_graph_type, labels_and_properties,
                                    edge_keys_and_element_table_names
        );
      } else {
        Value::GraphNodeKeysAndElementTableNames
            node_keys_and_element_table_name = {
                .identifier = v.GetIdentifier(),
                .keys = {v.GetGraphNodeKeys().begin(),
                         v.GetGraphNodeKeys().end()},
                .element_table_name = v.GetDefinitionName()};
        return Value::MakeGraphNode(to_graph_type, labels_and_properties,
                                    node_keys_and_element_table_name
        );
      }
    }
    case FCT(TYPE_GRAPH_PATH, TYPE_GRAPH_PATH): {
      const GraphPathType* to_path_type = to_type->AsGraphPath();
      std::vector<Value> components;
      components.reserve(v.num_graph_elements());
      for (int i = 0; i < v.num_graph_elements(); ++i) {
        GOOGLESQL_ASSIGN_OR_RETURN(Value component,
                         CastValue(v.graph_element(i),
                                   i % 2 == 0 ? to_path_type->node_type()
                                              : to_path_type->edge_type(),
                                   format, out_found_non_deterministic));
        components.push_back(std::move(component));
      }
      return Value::MakeGraphPath(to_path_type, std::move(components));
    }
    case FCT(TYPE_UUID, TYPE_STRING): {
      GOOGLESQL_ASSIGN_OR_RETURN(UuidValue uuid, v.uuid_value());
      return Value::String(uuid.ToString());
    }
    case FCT(TYPE_STRING, TYPE_UUID): {
      GOOGLESQL_ASSIGN_OR_RETURN(UuidValue uuid, UuidValue::FromString(v.string_value()));
      return Value::Uuid(uuid);
    }
    case FCT(TYPE_UUID, TYPE_BYTES): {
      GOOGLESQL_ASSIGN_OR_RETURN(UuidValue uuid, v.uuid_value());
      std::string uuid_bytes;
      uuid.SerializeAndAppendToBytes(&uuid_bytes);
      return Value::Bytes(uuid_bytes);
    }
    case FCT(TYPE_BYTES, TYPE_UUID): {
      if (v.bytes_value().size() != sizeof(UuidValue)) {
        return MakeEvalError() << "Invalid bytes value size for UUID, expected "
                               << sizeof(UuidValue) << " bytes, but got "
                               << v.bytes_value().size() << " bytes.";
      }
      GOOGLESQL_ASSIGN_OR_RETURN(UuidValue uuid,
                       UuidValue::DeserializeFromBytes(v.bytes_value()));
      return Value::Uuid(uuid);
    }
    case FCT(TYPE_MAP, TYPE_MAP): {
      GOOGLESQL_RETURN_IF_ERROR(ValidateLiteralValueCoercion(v, to_type));

      const Type* to_key_type = to_type->AsMap()->key_type();
      const Type* to_value_type = to_type->AsMap()->value_type();
      std::vector<std::pair<Value, Value>> casted_entries;
      for (auto& [k, v] : v.map_entries()) {
        Value casted_key;
        if (k.is_null()) {
          casted_key = Value::Null(to_key_type);
        } else {
          absl::StatusOr<Value> key_cast_result =
              CastValue(k, to_key_type, /*format=*/std::nullopt,
                        out_found_non_deterministic);
          if (!key_cast_result.ok()) {
            return googlesql_base::StatusBuilder(key_cast_result.status()).SetPrepend()
                   << "Error casting key " << k.ShortDebugString()
                   << " in MAP: ";
          }
          casted_key = std::move(key_cast_result.value());
        }

        Value casted_value;
        if (v.is_null()) {
          casted_value = Value::Null(to_value_type);
        } else {
          absl::StatusOr<Value> value_cast_result =
              CastValue(v, to_value_type, /*format=*/std::nullopt,
                        out_found_non_deterministic);
          if (!value_cast_result.ok()) {
            return googlesql_base::StatusBuilder(value_cast_result.status()).SetPrepend()
                   << "Error casting value for key " << k.ShortDebugString()
                   << " in MAP: ";
          }
          casted_value = std::move(value_cast_result.value());
        }
        casted_entries.push_back(std::make_pair(casted_key, casted_value));
      }
      return Value::MakeMap(to_type, std::move(casted_entries));
    }
    case FCT(TYPE_BOOL, TYPE_JSON):
    case FCT(TYPE_INT32, TYPE_JSON):
    case FCT(TYPE_UINT32, TYPE_JSON):
    case FCT(TYPE_INT64, TYPE_JSON):
    case FCT(TYPE_UINT64, TYPE_JSON):
    case FCT(TYPE_FLOAT, TYPE_JSON):
    case FCT(TYPE_DOUBLE, TYPE_JSON):
    case FCT(TYPE_NUMERIC, TYPE_JSON):
    case FCT(TYPE_BIGNUMERIC, TYPE_JSON):
    case FCT(TYPE_STRING, TYPE_JSON):
    case FCT(TYPE_DATE, TYPE_JSON):
    case FCT(TYPE_TIMESTAMP, TYPE_JSON):
    case FCT(TYPE_DATETIME, TYPE_JSON):
    case FCT(TYPE_TIME, TYPE_JSON):
    case FCT(TYPE_UUID, TYPE_JSON):
    case FCT(TYPE_GEOGRAPHY, TYPE_JSON):
    case FCT(TYPE_ARRAY, TYPE_JSON):
    case FCT(TYPE_STRUCT, TYPE_JSON):
    case FCT(TYPE_BYTES, TYPE_JSON):
    case FCT(TYPE_ENUM, TYPE_JSON):
    case FCT(TYPE_INTERVAL, TYPE_JSON):
    case FCT(TYPE_RANGE, TYPE_JSON):
    case FCT(TYPE_GRAPH_ELEMENT, TYPE_JSON):
    case FCT(TYPE_GRAPH_PATH, TYPE_JSON):
    case FCT(TYPE_PROTO, TYPE_JSON): {
      return CastToJSON(v, language_options(), canonicalize_zero_);
    }
    default:
      return ::googlesql_base::UnimplementedErrorBuilder()
             << "Unimplemented cast from "
             << v.type()->ShortTypeName(language_options().product_mode())
             << " to "
             << to_type->ShortTypeName(language_options().product_mode());
  }
}

bool ContainsExtendedType(const Type* type) {
  if (type->IsExtendedType()) {
    return true;
  }
  return absl::c_any_of(type->ComponentTypes(), ContainsExtendedType);
}

// Casts an extracted or pass-through `inner_val` from a VARIANT to `to_type`.
// Validates runtime coercibility for non-simple types and converts
// kInvalidArgument errors from CastContext::CastValue into kOutOfRange
// evaluation errors for SAFE_CAST.
absl::StatusOr<Value> CastInnerValueFromVariant(
    const CastContext& context, const Value& inner_val, const Type* to_type,
    bool* out_found_non_deterministic) {
  // Valid JSON values are always handled by the VariantNull, Primitive,
  // Object, or Array branches in CastFromVariant. A JSON value only reaches
  // this fallback when it holds a malformed unparsed JSON string that failed to
  // parse during VariantValueAdapter construction. Since VariantValueAdapter
  // does not store the error status in rep_ (to keep the adapter lightweight),
  // we re-run ParseJSONString here on the error path to retrieve the detailed
  // syntax error message, matching the pattern in variant_value.cc.
  if (inner_val.type()->IsJson() && inner_val.is_unparsed_json()) {
    absl::Status status =
        JSONValue::ParseJSONString(inner_val.json_value_unparsed()).status();
    GOOGLESQL_RET_CHECK(!status.ok());
    return MakeEvalError() << status.message();
  }

  // Validate coercibility for non-simple inner types at runtime since the
  // analyzer only sees TYPE_VARIANT at compile time.
  // TODO: b/554949919 - Support casting from a VARIANT containing an
  // ExtendedType when a Catalog or extended conversion evaluator is available.
  if (ContainsExtendedType(inner_val.type())) {
    return MakeEvalError() << "Unsupported cast from "
                           << inner_val.type()->ShortTypeName(
                                  context.language_options().product_mode())
                           << " to "
                           << to_type->ShortTypeName(
                                  context.language_options().product_mode());
  }
  if (!inner_val.type()->IsSimpleType() && !to_type->IsSimpleType() &&
      !inner_val.type()->Equals(to_type)) {
    TypeFactory type_factory;
    Coercer coercer(&type_factory, &context.language_options(),
                    /*catalog=*/nullptr);
    SignatureMatchResult match_result;
    ExtendedCompositeCastEvaluator extended_conversion_evaluator =
        ExtendedCompositeCastEvaluator::Invalid();
    GOOGLESQL_ASSIGN_OR_RETURN(
        bool coerces,
        coercer.CoercesTo(InputArgumentType(inner_val.type()), to_type,
                          /*is_explicit=*/true, &match_result,
                          &extended_conversion_evaluator));
    if (!coerces) {
      return MakeEvalError() << "Unsupported cast from "
                             << inner_val.type()->ShortTypeName(
                                    context.language_options().product_mode())
                             << " to "
                             << to_type->ShortTypeName(
                                    context.language_options().product_mode());
    }
  }

  absl::StatusOr<Value> result = context.CastValue(
      inner_val, to_type, /*format=*/std::nullopt, out_found_non_deterministic);

  // For non-variant types, incompatible casts (e.g. INT64 -> DATE) fail at
  // analysis time, and CastValue() returns MakeSqlError() (kInvalidArgument)
  // for unsupported cast types. SAFE_CAST is designed to handle runtime
  // data evaluation errors (kOutOfRange), not unsupported cast signatures.
  // For VARIANT, analysis succeeds for all target types, so dynamic type
  // incompatibility is only encountered at runtime. We convert kInvalidArgument
  // to MakeEvalError() here so SAFE_CAST treats it as a runtime evaluation
  // error and returns NULL instead of failing the query.
  if (!result.ok()) {
    if (absl::IsInvalidArgument(result.status())) {
      return MakeEvalError() << result.status().message();
    }
    return result;
  }

  // CastValue does not set `out_found_non_deterministic` for float/proto/etc.
  // casts because callers can detect potential nondeterminism statically from
  // the input Type. Since a VARIANT's static type is TYPE_VARIANT, we must
  // check the inner value's dynamic type against the target type at runtime.
  if (out_found_non_deterministic != nullptr && !*out_found_non_deterministic &&
      (CastHasPotentialNondeterminism(inner_val.type(), to_type) ||
       HasNonDeterministicArrayCastToJson(inner_val, to_type))) {
    *out_found_non_deterministic = true;
  }
  return result;
}

// Casts a VARIANT Object to a STRUCT matching target fields case-insensitively
// (with an unnamed field matching the "" key).
// Missing fields evaluate to NULL and unasked keys are ignored.
// Returns a runtime error if `to_type` contains duplicate field names
// (case-insensitively) or multiple unnamed fields, or if a target field
// matches multiple source Object keys that differ only by case. Matched fields
// are cast in target STRUCT field order so error precedence does not depend on
// the Object's internal iteration order.
absl::StatusOr<Value> CastVariantObjectToStruct(
    const CastContext& context, const VariantValueView& view,
    const Type* to_type, bool* out_found_non_deterministic,
    int current_nesting_level) {
  const StructType* to_struct = to_type->AsStruct();
  const int num_fields = to_struct->num_fields();

  std::vector<Value> casted_fields;
  casted_fields.reserve(num_fields);
  absl::flat_hash_map<std::string, int> lower_field_to_index;
  lower_field_to_index.reserve(num_fields);
  for (int i = 0; i < num_fields; ++i) {
    const StructType::StructField& field = to_struct->field(i);
    auto [unused_it, inserted] = lower_field_to_index.emplace(
        googlesql::GetNormalizedAndCasefoldedString(field.name), i);
    if (!inserted) {
      if (field.name.empty()) {
        return MakeEvalError()
               << "Cannot cast VARIANT Object to STRUCT: target struct "
                  "contains multiple anonymous fields";
      }
      return MakeEvalError()
             << "Cannot cast VARIANT Object to STRUCT: target struct "
                "contains duplicate field name '"
             << field.name << "'";
    }
    casted_fields.push_back(Value::Null(field.type));
  }

  struct MatchedMember {
    std::string key;
    VariantValueView value;
  };
  std::vector<std::optional<MatchedMember>> matched_members(num_fields);
  GOOGLESQL_RETURN_IF_ERROR(view.ForMembers(
      [&](absl::string_view key, VariantValueView child_view) -> absl::Status {
        auto it = lower_field_to_index.find(
            googlesql::GetNormalizedAndCasefoldedString(key));
        if (it == lower_field_to_index.end()) {
          return absl::OkStatus();
        }
        const int field_index = it->second;
        if (matched_members[field_index].has_value()) {
          return MakeEvalError()
                 << "Cannot cast VARIANT Object to STRUCT: target struct "
                    "field '"
                 << to_struct->field(field_index).name
                 << "' matches multiple VARIANT Object keys "
                    "case-insensitively ('"
                 << matched_members[field_index]->key << "' and '" << key
                 << "')";
        }
        matched_members[field_index] = MatchedMember{
            .key = std::string(key),
            .value = std::move(child_view),
        };
        return absl::OkStatus();
      }));

  for (int i = 0; i < num_fields; ++i) {
    if (!matched_members[i].has_value()) {
      continue;
    }
    const StructType::StructField& field = to_struct->field(i);
    GOOGLESQL_ASSIGN_OR_RETURN(
        casted_fields[i],
        CastFromVariant(context, matched_members[i]->value, field.type,
                        out_found_non_deterministic, current_nesting_level + 1),
        _.SetPrepend() << "Error casting field "
                       << (field.name.empty()
                               ? absl::StrCat("at index ", i)
                               : absl::StrCat("'", field.name, "'"))
                       << " in STRUCT: ");
  }

  return Value::MakeStruct(to_struct, std::move(casted_fields));
}

// Casts a VARIANT Object `view` to a MAP type `to_type` (REQUIRES:
// `to_type->IsMap()`), recursively casting each member value.
absl::StatusOr<Value> CastVariantObjectToMap(const CastContext& context,
                                             const VariantValueView& view,
                                             const Type* to_type,
                                             bool* out_found_non_deterministic,
                                             int current_nesting_level) {
  const MapType* to_map = to_type->AsMap();
  // Variant Objects always have STRING keys, so the target MAP key type must
  // be STRING.
  if (!to_map->key_type()->IsString()) {
    return MakeEvalError() << "Cannot cast VARIANT Object to "
                           << to_type->ShortTypeName(
                                  context.language_options().product_mode())
                           << ": target MAP key type must be STRING";
  }
  std::vector<std::pair<Value, Value>> casted_entries;
  GOOGLESQL_RETURN_IF_ERROR(
      view.ForMembers([&](absl::string_view key,
                          const VariantValueView& child_view) -> absl::Status {
        GOOGLESQL_ASSIGN_OR_RETURN(
            Value casted_val,
            CastFromVariant(context, child_view, to_map->value_type(),
                            out_found_non_deterministic,
                            current_nesting_level + 1),
            _.SetPrepend() << "Error casting value for key '" << key
                           << "' in MAP: ");
        casted_entries.emplace_back(Value::String(key), std::move(casted_val));
        return absl::OkStatus();
      }));
  return Value::MakeMap(to_type, std::move(casted_entries));
}

absl::StatusOr<JSONValue> CastVariantToJson(const CastContext& context,
                                            const VariantValueView& view,
                                            bool* out_found_non_deterministic,
                                            int current_nesting_level);

// Converts a non-null primitive or pass-through `Value` inside a VARIANT
// directly to `JSONValue` without wrapping in an intermediate `Value::Json`.
absl::StatusOr<JSONValue> CastInnerValueToJson(
    const CastContext& context, const Value& inner_val,
    bool* out_found_non_deterministic) {
  if (!IsTypeSupportedForCastToJson(inner_val.type())) {
    return MakeEvalError() << "Unsupported cast from "
                           << inner_val.type()->ShortTypeName(
                                  context.language_options().product_mode())
                           << " to JSON";
  }
  absl::StatusOr<JSONValue> json_val = googlesql::functions::ToJson(
      inner_val, /*stringify_wide_numbers=*/false, context.language_options(),
      context.canonicalize_zero(),
      /*unsupported_fields=*/googlesql::functions::UnsupportedFieldsEnum::FAIL,
      /*path_as_object=*/false, /*reject_nan_infinity_floats=*/true);
  if (!json_val.ok()) {
    if (absl::IsInvalidArgument(json_val.status())) {
      return MakeEvalError() << json_val.status().message();
    }
    return json_val.status();
  }
  if (out_found_non_deterministic != nullptr && !*out_found_non_deterministic &&
      (HasFloatingPoint(inner_val.type()) ||
       HasNonDeterministicArrayCastToJson(inner_val, types::JsonType()))) {
    *out_found_non_deterministic = true;
  }
  return json_val;
}

// Returns true if `view` wraps an unordered SQL ARRAY whose converted JSON
// elements in `json_arr_ref` are not all equal, making the JSON array's element
// order non-deterministic.
bool IsUnorderedMultiValueSqlArray(const VariantValueView& view,
                                   JSONValueConstRef json_arr_ref) {
  if (json_arr_ref.GetArraySize() <= 1) {
    return false;
  }
  const Type* unprojected_type = view.GetUnprojectedType();
  if (unprojected_type == nullptr || !unprojected_type->IsArray()) {
    return false;
  }
  absl::StatusOr<Value> array_val = view.GetUnprojectedValue();
  if (!array_val.ok() || !array_val->is_valid() ||
      InternalValue::order_kind(*array_val) == InternalValue::kPreservesOrder) {
    return false;
  }
  JSONValueConstRef first_elem = json_arr_ref.GetArrayElement(0);
  for (size_t i = 1; i < json_arr_ref.GetArraySize(); ++i) {
    if (json_arr_ref.GetArrayElement(i) != first_elem) {
      return true;
    }
  }
  return false;
}

absl::StatusOr<JSONValue> CastVariantObjectToJson(
    const CastContext& context, const VariantValueView& view,
    bool* out_found_non_deterministic, int current_nesting_level) {
  if (current_nesting_level >= functions::kNestingLevelStackCheckThreshold) {
    GOOGLESQL_RETURN_IF_NOT_ENOUGH_STACK(
        "Out of stack space due to deeply nested variant object in cast "
        "function");
  }
  JSONValue json_obj;
  JSONValueRef ref = json_obj.GetRef();
  ref.SetToEmptyObject();
  GOOGLESQL_RETURN_IF_ERROR(
      view.ForMembers([&](absl::string_view key,
                          const VariantValueView& child_view) -> absl::Status {
        GOOGLESQL_ASSIGN_OR_RETURN(
            JSONValue child_json,
            CastVariantToJson(context, child_view, out_found_non_deterministic,
                              current_nesting_level + 1));
        ref.GetMember(key).Set(std::move(child_json));
        return absl::OkStatus();
      }));
  return json_obj;
}

absl::StatusOr<JSONValue> CastVariantArrayToJson(
    const CastContext& context, const VariantValueView& view,
    bool* out_found_non_deterministic, int current_nesting_level) {
  if (current_nesting_level >= functions::kNestingLevelStackCheckThreshold) {
    GOOGLESQL_RETURN_IF_NOT_ENOUGH_STACK(
        "Out of stack space due to deeply nested variant array in cast "
        "function");
  }
  GOOGLESQL_ASSIGN_OR_RETURN(std::vector<VariantValueView> elements, view.GetElements());
  JSONValue json_arr;
  JSONValueRef ref = json_arr.GetRef();
  ref.SetToEmptyArray();
  if (!elements.empty()) {
    // Pre-size the JSON array to avoid repeated resizes while populating it.
    ref.GetArrayElement(elements.size() - 1);
    for (size_t i = 0; i < elements.size(); ++i) {
      GOOGLESQL_ASSIGN_OR_RETURN(
          JSONValue elem_json,
          CastVariantToJson(context, elements[i], out_found_non_deterministic,
                            current_nesting_level + 1));
      ref.GetArrayElement(i).Set(std::move(elem_json));
    }
  }
  if (out_found_non_deterministic != nullptr && !*out_found_non_deterministic &&
      IsUnorderedMultiValueSqlArray(view, json_arr.GetConstRef())) {
    *out_found_non_deterministic = true;
  }
  return json_arr;
}

// Builds `JSONValue` bottom-up so nested Variant Objects and Arrays move child
// `JSONValue`s directly instead of wrapping and deep-copying intermediate
// `Value`s at every nesting level.
absl::StatusOr<JSONValue> CastVariantToJson(const CastContext& context,
                                            const VariantValueView& view,
                                            bool* out_found_non_deterministic,
                                            int current_nesting_level) {
  GOOGLESQL_RET_CHECK(!view.is_null());
  if (view.IsVariantNull()) {
    return JSONValue();
  }
  // If `view` is already backed by a JSON node or unparsed JSON string,
  // `GetUnprojectedValue()` either copies the JSON subtree directly without
  // re-traversing or returns the parse error status.
  if (const Type* unprojected_type = view.GetUnprojectedType();
      unprojected_type != nullptr && unprojected_type->IsJson()) {
    GOOGLESQL_ASSIGN_OR_RETURN(Value json_val, view.GetUnprojectedValue());
    return JSONValue::CopyFrom(json_val.json_value());
  }
  if (view.IsPrimitive()) {
    GOOGLESQL_ASSIGN_OR_RETURN(Value primitive_val, view.GetPrimitiveValue());
    return CastInnerValueToJson(context, primitive_val,
                                out_found_non_deterministic);
  }
  // TODO: b/554949919 - Revisit how a VARIANT holding a PROTO casts to JSON
  // once the VARIANT data model is finalized.
  if (view.IsObject()) {
    return CastVariantObjectToJson(context, view, out_found_non_deterministic,
                                   current_nesting_level);
  }
  if (view.IsArray()) {
    return CastVariantArrayToJson(context, view, out_found_non_deterministic,
                                  current_nesting_level);
  }
  // Pass-through for inner values that are not exposed as Primitives, Objects,
  // or Arrays (for example, non-string-keyed MAP<K, V> or GRAPH_ELEMENT).
  GOOGLESQL_ASSIGN_OR_RETURN(Value inner_val, view.GetUnprojectedValue());
  return CastInnerValueToJson(context, inner_val, out_found_non_deterministic);
}

// Casts a VARIANT Object `view` to `to_type` (such as MAP<STRING, V> or a
// pass-through target type when `view` wraps a stored PROTO value).
absl::StatusOr<Value> CastVariantObject(const CastContext& context,
                                        const VariantValueView& view,
                                        const Type* to_type,
                                        bool* out_found_non_deterministic,
                                        int current_nesting_level) {
  if (to_type->IsStruct()) {
    // TODO: b/554949919 - Revisit once the VARIANT CAST-out rules are
    // finalized: check whether the target STRUCT type matches the unprojected
    // inner STRUCT value directly (e.g., preserving duplicate field names)
    // before falling back to VARIANT Object matching rules.
    return CastVariantObjectToStruct(context, view, to_type,
                                     out_found_non_deterministic,
                                     current_nesting_level);
  }
  if (to_type->IsMap()) {
    return CastVariantObjectToMap(context, view, to_type,
                                  out_found_non_deterministic,
                                  current_nesting_level);
  }
  // A stored PROTO value is exposed as a Variant Object. Casts to STRUCT and
  // MAP<STRING, V> are handled above (and JSON is handled in CastFromVariant)
  // via Object member semantics. For remaining target types (PROTO, STRING,
  // BYTES), delegate to CastValue on the underlying proto.
  if (const Type* unprojected_type = view.GetUnprojectedType();
      unprojected_type != nullptr && unprojected_type->IsProto()) {
    // TODO: b/554949919 - Revisit whether casting a VARIANT holding a PROTO to
    // non-Object target types (such as BYTES or STRING) should fall back to the
    // unprojected PROTO value or be disallowed once the VARIANT spec is
    // finalized.
    GOOGLESQL_ASSIGN_OR_RETURN(Value proto_val, view.GetUnprojectedValue());
    return CastInnerValueFromVariant(context, proto_val, to_type,
                                     out_found_non_deterministic);
  }
  // TODO: b/573401247 - Support casting general VARIANT Objects (JSON, STRUCT,
  // MAP) to PROTO according to spec rules.
  return MakeEvalError() << "Cannot cast VARIANT Object to "
                         << to_type->ShortTypeName(
                                context.language_options().product_mode());
}

// Casts a VARIANT Array `view` to `to_type`, recursively casting each element
// when `to_type->IsArray()` and preserving the underlying array's order kind.
absl::StatusOr<Value> CastVariantArray(const CastContext& context,
                                       const VariantValueView& view,
                                       const Type* to_type,
                                       bool* out_found_non_deterministic,
                                       int current_nesting_level) {
  if (to_type->IsArray()) {
    const Type* to_elem_type = to_type->AsArray()->element_type();
    GOOGLESQL_ASSIGN_OR_RETURN(std::vector<VariantValueView> elements,
                     view.GetElements());
    std::vector<Value> casted_elements;
    casted_elements.reserve(elements.size());
    for (int i = 0; i < static_cast<int>(elements.size()); ++i) {
      GOOGLESQL_ASSIGN_OR_RETURN(Value casted_elem,
                       CastFromVariant(context, elements[i], to_elem_type,
                                       out_found_non_deterministic,
                                       current_nesting_level + 1),
                       _.SetPrepend() << "Error casting element at index " << i
                                      << " in ARRAY: ");
      casted_elements.push_back(std::move(casted_elem));
    }
    // Preserve the underlying array's order-preservation kind, matching
    // ARRAY-to-ARRAY casts.
    InternalValue::OrderPreservationKind order_kind =
        InternalValue::kPreservesOrder;
    if (const Type* unprojected_type = view.GetUnprojectedType();
        unprojected_type != nullptr && unprojected_type->IsArray()) {
      GOOGLESQL_ASSIGN_OR_RETURN(Value array_val, view.GetUnprojectedValue());
      order_kind = InternalValue::order_kind(array_val);
    }
    return InternalValue::MakeArray(to_type->AsArray(), order_kind,
                                    std::move(casted_elements));
  }
  return MakeEvalError() << "Cannot cast VARIANT Array to "
                         << to_type->ShortTypeName(
                                context.language_options().product_mode());
}

absl::StatusOr<Value> CastFromVariant(const CastContext& context,
                                      const VariantValueView& view,
                                      const Type* to_type,
                                      bool* out_found_non_deterministic,
                                      int current_nesting_level) {
  if (current_nesting_level >= functions::kNestingLevelStackCheckThreshold) {
    GOOGLESQL_RETURN_IF_NOT_ENOUGH_STACK(
        "Out of stack space due to deeply nested variant object in cast "
        "function");
  }
  GOOGLESQL_RET_CHECK(!view.is_null());
  if (to_type->IsVariant()) {
    return view.ToVariantValue();
  }
  if (to_type->IsJson()) {
    if (const Type* unprojected_type = view.GetUnprojectedType();
        unprojected_type != nullptr && unprojected_type->IsJson()) {
      return view.GetUnprojectedValue();
    }
    GOOGLESQL_ASSIGN_OR_RETURN(
        JSONValue json_val,
        CastVariantToJson(context, view, out_found_non_deterministic,
                          current_nesting_level));
    return Value::Json(std::move(json_val));
  }
  if (view.IsVariantNull()) {
    return Value::Null(to_type);
  }
  if (view.IsPrimitive()) {
    GOOGLESQL_ASSIGN_OR_RETURN(Value primitive_val, view.GetPrimitiveValue());
    return CastInnerValueFromVariant(context, primitive_val, to_type,
                                     out_found_non_deterministic);
  }
  if (view.IsObject()) {
    return CastVariantObject(context, view, to_type,
                             out_found_non_deterministic,
                             current_nesting_level);
  }
  if (view.IsArray()) {
    return CastVariantArray(context, view, to_type, out_found_non_deterministic,
                            current_nesting_level);
  }
  // Pass-through for inner values that are not exposed as Primitives, Objects,
  // or Arrays (for example, non-string-keyed MAP<K, V>, GRAPH_ELEMENT, or
  // malformed unparsed JSON).
  GOOGLESQL_ASSIGN_OR_RETURN(Value inner_val, view.GetUnprojectedValue());
  return CastInnerValueFromVariant(context, inner_val, to_type,
                                   out_found_non_deterministic);
}

// CastContextWithValidation implements a validated cast. Used by CastValue.
class CastContextWithValidation : public CastContext {
 public:
  CastContextWithValidation(absl::TimeZone default_timezone,
                            std::optional<absl::Time> current_timestamp,
                            const LanguageOptions& language_options,
                            Catalog* catalog, bool canonicalize_zero)
      : CastContext(default_timezone, current_timestamp, language_options,
                    canonicalize_zero),
        catalog_(catalog) {}

 private:
  absl::StatusOr<Value> CastWithExtendedType(
      const Value& from_value, const Type* to_type) const override {
    if (catalog_ == nullptr) {
      return googlesql_base::FailedPreconditionErrorBuilder()
             << "Attempt to cast a Value of extened type without providing a "
                "Catalog";
    }

    Catalog::FindConversionOptions options(
        /*is_explicit=*/true, Catalog::ConversionSourceExpressionKind::kLiteral,
        language_options().product_mode());
    Conversion conversion = Conversion::Invalid();
    GOOGLESQL_RETURN_IF_ERROR(catalog_->FindConversion(from_value.type(), to_type,
                                             options, &conversion));
    return conversion.evaluator().Eval(from_value);
  }

  absl::Status ValidateLiteralValueCoercion(
      const Value& from_value, const Type* to_type) const override {
    SignatureMatchResult result;
    TypeFactory type_factory;
    Coercer coercer(&type_factory, &language_options(), catalog_);
    if (!coercer.CoercesTo(InputArgumentType(from_value), to_type,
                           /*is_explicit=*/true, &result)) {
      return MakeSqlError()
             << "Unsupported cast from "
             << from_value.type()->ShortTypeName(
                    language_options().product_mode())
             << " to "
             << to_type->ShortTypeName(language_options().product_mode());
    }

    return absl::OkStatus();
  }

  mutable Catalog* catalog_;
};

// CastContextWithoutValidation implements an unvalidated cast. Used by the
// CastValueWithoutTypeValidation.
class CastContextWithoutValidation : public CastContext {
 public:
  CastContextWithoutValidation(
      absl::TimeZone default_timezone,
      std::optional<absl::Time> current_timestamp,
      const LanguageOptions& language_options,
      const ExtendedCompositeCastEvaluator* extended_cast_evaluator,
      bool canonicalize_zero)
      : CastContext(default_timezone, current_timestamp, language_options,
                    canonicalize_zero),
        extended_cast_evaluator_(extended_cast_evaluator) {}

  absl::StatusOr<Value> CastWithExtendedType(
      const Value& from_value, const Type* to_type) const override {
    if (extended_cast_evaluator_ == nullptr) {
      return googlesql_base::FailedPreconditionErrorBuilder()
             << "Attempt to cast a Value of extened type without providing an "
                "extended conversion function";
    }

    return extended_cast_evaluator_->Eval(from_value, to_type);
  }

  absl::Status ValidateLiteralValueCoercion(
      const Value& from_value, const Type* to_type) const override {
    return absl::OkStatus();
  }

 private:
  const ExtendedCompositeCastEvaluator* extended_cast_evaluator_;
};

}  // namespace

absl::StatusOr<Value> CastValue(const Value& from_value,
                                absl::TimeZone default_timezone,
                                const LanguageOptions& language_options,
                                const Type* to_type, Catalog* catalog,
                                bool canonicalize_zero) {
  return CastValue(from_value, default_timezone, language_options, to_type,
                   /*format=*/std::nullopt, catalog, canonicalize_zero);
}

absl::StatusOr<Value> CastValue(const Value& from_value,
                                absl::TimeZone default_timezone,
                                const LanguageOptions& language_options,
                                const Type* to_type,
                                const std::optional<std::string>& format,
                                Catalog* catalog, bool canonicalize_zero) {
  return CastContextWithValidation(default_timezone,
                                   /*current_timestamp=*/std::nullopt,
                                   language_options, catalog, canonicalize_zero)
      .CastValue(from_value, to_type, format);
}

namespace internal {

absl::StatusOr<Value> CastValueWithoutTypeValidation(
    const Value& from_value, absl::TimeZone default_timezone,
    std::optional<absl::Time> current_timestamp,
    const LanguageOptions& language_options, const Type* to_type,
    const std::optional<std::string>& format,
    const std::optional<std::string>& time_zone,
    const ExtendedCompositeCastEvaluator* extended_conversion_evaluator,
    bool canonicalize_zero, bool* out_found_non_deterministic) {
  absl::TimeZone timezone = default_timezone;
  if (time_zone.has_value()) {
    GOOGLESQL_RETURN_IF_ERROR(functions::MakeTimeZone(time_zone.value(), &timezone));
  }
  bool found_non_deterministic = false;
  GOOGLESQL_ASSIGN_OR_RETURN(Value result,
                   CastContextWithoutValidation(
                       timezone, current_timestamp, language_options,
                       extended_conversion_evaluator, canonicalize_zero)
                       .CastValue(from_value, to_type, format,
                                  out_found_non_deterministic != nullptr
                                      ? &found_non_deterministic
                                      : nullptr));
  if (found_non_deterministic && out_found_non_deterministic != nullptr) {
    *out_found_non_deterministic = true;
  }
  return result;
}

const CastHashMap& GetGoogleSQLCasts() {
  static const CastHashMap* cast_hash_map = InitializeGoogleSQLCasts();
  return *cast_hash_map;
}


}  // namespace internal

absl::StatusOr<ConversionEvaluator> ConversionEvaluator::Create(
    const Type* from_type, const Type* to_type, const Function* function) {
  GOOGLESQL_RET_CHECK(from_type);
  GOOGLESQL_RET_CHECK(to_type);
  GOOGLESQL_RET_CHECK(function);

  return ConversionEvaluator(from_type, to_type, function);
}

FunctionSignature ConversionEvaluator::GetFunctionSignature(
    const Type* from_type, const Type* to_type) {
  return FunctionSignature({to_type, /*num_occurrences=*/1},
                           {{from_type, /*num_occurrences=*/1}},
                           /*context_ptr=*/nullptr);
}

absl::StatusOr<Value> ConversionEvaluator::Eval(const Value& from_value) const {
  if (!is_valid()) {
    return googlesql_base::FailedPreconditionErrorBuilder()
           << "Attempt to cast a value using invalid conversion";
  }

  if (!from_type_->Equals(from_value.type())) {
    return googlesql_base::InvalidArgumentErrorBuilder()
           << "Type of casted value doesn't match the source type of "
              "conversion";
  }

  if (from_type_->Equals(to_type_)) {
    return from_value;
  }

  GOOGLESQL_ASSIGN_OR_RETURN(auto evaluator, function_->GetFunctionEvaluatorFactory()(
                                       function_signature()));
  absl::StatusOr<Value> result = evaluator({from_value});
  ABSL_DCHECK(!result.ok() || to_type_->Equals(result.value().type()))
      << "Conversion evaluator from type " << from_type_->DebugString()
      << " to type " << to_type_->DebugString() << " returned a value of type "
      << result.value().type()->DebugString();

  return result;
}

absl::StatusOr<Conversion> Conversion::Create(
    const Type* from_type, const Type* to_type, const Function* function,
    const CastFunctionProperty& property) {
  GOOGLESQL_ASSIGN_OR_RETURN(ConversionEvaluator evaluator,
                   ConversionEvaluator::Create(from_type, to_type, function));
  return Create(evaluator, property);
}

absl::StatusOr<Conversion> Conversion::Create(
    const ConversionEvaluator& evaluator,
    const CastFunctionProperty& property) {
  GOOGLESQL_RET_CHECK(evaluator.is_valid());
  return Conversion(evaluator, property);
}

bool Conversion::IsMatch(const Catalog::FindConversionOptions& options) const {
  if (!is_valid()) {
    return false;
  }

  // Conversion can be: 1) explicit 2) implicit 3) implicit for literals and
  // explicit for other expressions 4) implicit for literals & parameters and
  // explicit for other expressions. If conversion is implicit, it also always
  // can be applied explicitly.

  if (options.is_explicit()) {
    return true;  // All types of conversions can be applied explicitly.
  }

  // We are looking for implicit conversion below and need to check whether it
  // can be applied to all kinds of expression (unconditional) or only to some.

  if (property().is_implicit()) {
    return true;  // Conversion is unconditionally implicit.
  }

  switch (options.source_kind()) {
    case Catalog::ConversionSourceExpressionKind::kLiteral:
      return property().type == CastFunctionType::EXPLICIT_OR_LITERAL ||
             property().type ==
                 CastFunctionType::EXPLICIT_OR_LITERAL_OR_PARAMETER;
    case Catalog::ConversionSourceExpressionKind::kParameter:
      return property().type ==
             CastFunctionType::EXPLICIT_OR_LITERAL_OR_PARAMETER;
    default:
      return false;
  }
}

absl::StatusOr<Value> ExtendedCompositeCastEvaluator::Eval(
    const Value& from_value, const Type* to_type) const {
  for (const ConversionEvaluator& evaluator : evaluators_) {
    if (evaluator.from_type()->Equals(from_value.type()) &&
        evaluator.to_type()->Equals(to_type)) {
      return evaluator.Eval(from_value);
    }
  }

  return googlesql_base::InvalidArgumentErrorBuilder()
         << "Conversion from type " << from_value.type()->DebugString()
         << " to type " << to_type->DebugString()
         << " is not found in ExtendedCompositeCastEvaluator";
}

}  // namespace googlesql
