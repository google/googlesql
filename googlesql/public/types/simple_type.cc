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

#include "googlesql/public/types/simple_type.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "google/protobuf/timestamp.pb.h"
#include "googlesql/common/errors.h"
#include "googlesql/common/float_margin.h"
#include "googlesql/common/string_util.h"
#include "googlesql/public/civil_time.h"
#include "googlesql/public/functions/date_time_util.h"
#include "googlesql/public/interval_value.h"
#include "googlesql/public/json_value.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/numeric_value.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/pico_time.h"
#include "googlesql/public/strings.h"
#include "googlesql/public/timestamp_picos_value.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/type_parameters.pb.h"
#include "googlesql/public/types/internal_utils.h"
#include "googlesql/public/types/simple_value.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_modifiers.h"
#include "googlesql/public/types/type_parameters.h"
#include "googlesql/public/types/value_representations.h"
#include "googlesql/public/uuid_value.h"
#include "googlesql/public/value.pb.h"
#include "googlesql/public/value_content.h"
#include "absl/base/no_destructor.h"
#include "absl/hash/hash.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "googlesql/common/search/public/token_list_util.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"
#include "googlesql/base/time_proto_util.h"

namespace googlesql {

// The types below are used to represent the C++ type of a main value stored in
// ValueContent for a particular GoogleSQL type.
// Note: in addition to their main value content simple types TYPE_TIMESTAMP,
// TYPE_TIME and TYPE_DATETIME also have extended content stored in
// ValueContent::simple_type_extended_content_. The C++ type of this extended
// content is fixed (int32).
using DateValueContentType = int32_t;       // Used with TYPE_DATE.
using TimestampValueContentType = int64_t;  // Used with TYPE_TIMESTAMP.
using TimeValueContentType = int32_t;       // Used with TYPE_TIME.
using DateTimeValueContentType = int64_t;   // Used with TYPE_DATETIME.

namespace {

// Constants for NUMERIC / BIGNUMERIC type parameters.
constexpr int kNumericMaxPrecision = 29;
constexpr int kBigNumericMaxPrecision = 38;
constexpr int kNumericMaxScale = 9;
constexpr int kBigNumericMaxScale = 38;

// Specifies the type kind that a type name maps to, and when the type name is
// enabled. Even when the type name is enabled, the type kind might still be
// disabled (controlled by TypeKindInfo). The type name can be used only if both
// are enabled.
struct TypeNameInfo {
  TypeKind type_kind;
  // If false, this type name can be used in both the internal mode and the
  // external mode. If true, this type name can be used in the internal mode
  // only.
  bool internal_product_mode_only = false;
  // If present, then the feature controls whether the type name is enabled.
  // If absent, then the type name does not require any language feature.
  std::optional<LanguageFeature> alias_feature;
};

const std::map<absl::string_view, TypeNameInfo>& SimpleTypeNameInfoMap() {
  static auto result = new std::map<absl::string_view, TypeNameInfo>{
      {"int32", {TYPE_INT32, true}},
      {"uint32", {TYPE_UINT32, true}},
      {"int64", {TYPE_INT64}},
      {"uint64", {TYPE_UINT64, true}},
      {"bool", {TYPE_BOOL}},
      {"boolean", {TYPE_BOOL}},
      {"float", {TYPE_FLOAT, true}},
      {"float32", {TYPE_FLOAT}},
      {"float64", {TYPE_DOUBLE}},
      {"double", {TYPE_DOUBLE, true}},
      {"bytes", {TYPE_BYTES}},
      {"string", {TYPE_STRING}},
      {"date", {TYPE_DATE}},
      {"timestamp", {TYPE_TIMESTAMP}},
      {"time", {TYPE_TIME}},
      {"datetime", {TYPE_DATETIME}},
      {"interval", {TYPE_INTERVAL}},
      {"geography", {TYPE_GEOGRAPHY}},
      {"numeric", {TYPE_NUMERIC}},
      {"decimal", {TYPE_NUMERIC, false, FEATURE_DECIMAL_ALIAS}},
      {"bignumeric", {TYPE_BIGNUMERIC}},
      {"bigdecimal", {TYPE_BIGNUMERIC, false, FEATURE_DECIMAL_ALIAS}},
      {"json", {TYPE_JSON}},
      {"tokenlist", {TYPE_TOKENLIST}},
      {"uuid", {TYPE_UUID}},
  };
  return *result;
}

// Specifies when the type kind is enabled.
struct TypeKindInfo {
  // Whether this type kind is valid when stored in
  // `GetSimpleTypeKindInfo::kMap`.
  bool valid = false;

  // If true, this type kind can be used in both the internal mode and the
  // external mode. If false, this type kind can be used in the internal mode
  // only .
  bool internal_product_mode_only = false;
  // If present, then the feature controls whether the type kind is enabled.
  // If absent, then the type kind does not require any language feature.
  // Only one of `type_feature` or `disabling_type_feature` should be set.
  std::optional<LanguageFeature> type_feature;
  // If present, then the feature controls whether the type kind is enabled.
  // If absent, then the type kind does not require any language feature.
  // Only one of `type_feature` or `disabling_type_feature` should be set.
  std::optional<LanguageFeature> disabling_type_feature;

  // Builds a TypeKindInfo for both product modes.
  static TypeKindInfo Build() {
    return TypeKindInfo(/*internal_product_mode_only=*/false, std::nullopt,
                        std::nullopt);
  }

  // Builds a TypeKindInfo for `PRODUCT_INTERNAL`.
  static TypeKindInfo BuildInternalOnly() {
    return TypeKindInfo(/*internal_product_mode_only=*/true, std::nullopt,
                        std::nullopt);
  }

  // Builds a TypeKindInfo for both product modes, controlled by `type_feature`.
  static TypeKindInfo BuildWithTypeFeature(LanguageFeature type_feature) {
    return TypeKindInfo(/*internal_product_mode_only=*/false, type_feature,
                        std::nullopt);
  }

  // Builds a TypeKindInfo for both product modes, controlled by
  // `disabling_type_feature`.
  static TypeKindInfo BuildWithDisablingTypeFeature(
      LanguageFeature disabling_type_feature) {
    return TypeKindInfo(/*internal_product_mode_only=*/false, std::nullopt,
                        disabling_type_feature);
  }

  TypeKindInfo() = default;

 private:
  TypeKindInfo(bool internal_product_mode_only,
               std::optional<LanguageFeature> type_feature,
               std::optional<LanguageFeature> disabling_type_feature)
      : valid(true), internal_product_mode_only(internal_product_mode_only) {
    ABSL_CHECK(!(type_feature.has_value() &&  // Crash OK
            disabling_type_feature.has_value()))
        << "Only one of type_feature or disabling_type_feature should be set.";
    this->type_feature = type_feature;
    this->disabling_type_feature = disabling_type_feature;
  }
};

const TypeKindInfo* GetSimpleTypeKindInfo(TypeKind kind) {
  using TypeKindInfoArray = std::array<TypeKindInfo, TypeKind_ARRAYSIZE>;
  static const absl::NoDestructor<TypeKindInfoArray> kMap([]() {
    TypeKindInfoArray result;
    result[TYPE_INT32] = TypeKindInfo::BuildInternalOnly();
    result[TYPE_UINT32] = TypeKindInfo::BuildInternalOnly();
    result[TYPE_INT64] = TypeKindInfo::Build();
    result[TYPE_UINT64] = TypeKindInfo::BuildInternalOnly();
    result[TYPE_BOOL] = TypeKindInfo::Build();
    result[TYPE_FLOAT] =
        TypeKindInfo::BuildWithDisablingTypeFeature(FEATURE_DISABLE_FLOAT32);
    result[TYPE_DOUBLE] = TypeKindInfo::Build();
    result[TYPE_BYTES] = TypeKindInfo::Build();
    result[TYPE_STRING] = TypeKindInfo::Build();
    result[TYPE_DATE] = TypeKindInfo::Build();
    result[TYPE_TIMESTAMP] = TypeKindInfo::Build();
    result[TYPE_TIME] = TypeKindInfo::BuildWithTypeFeature(FEATURE_CIVIL_TIME);
    result[TYPE_DATETIME] =
        TypeKindInfo::BuildWithTypeFeature(FEATURE_CIVIL_TIME);
    result[TYPE_INTERVAL] =
        TypeKindInfo::BuildWithTypeFeature(FEATURE_INTERVAL_TYPE);
    result[TYPE_GEOGRAPHY] =
        TypeKindInfo::BuildWithTypeFeature(FEATURE_GEOGRAPHY);
    result[TYPE_NUMERIC] =
        TypeKindInfo::BuildWithTypeFeature(FEATURE_NUMERIC_TYPE);
    result[TYPE_BIGNUMERIC] =
        TypeKindInfo::BuildWithTypeFeature(FEATURE_BIGNUMERIC_TYPE);
    result[TYPE_JSON] = TypeKindInfo::BuildWithTypeFeature(FEATURE_JSON_TYPE);
    result[TYPE_TOKENLIST] =
        TypeKindInfo::BuildWithTypeFeature(FEATURE_TOKENIZED_SEARCH);
    result[TYPE_UUID] = TypeKindInfo::BuildWithTypeFeature(FEATURE_UUID_TYPE);
    return result;
  }());
  const int kind_id = static_cast<int>(kind);
  if (kind_id < 0 || kind_id >= kMap->size()) {
    return nullptr;
  }
  const TypeKindInfo& info = (*kMap)[kind_id];
  return info.valid ? &info : nullptr;
}

// Joined result of TypeNameInfo and TypeKindInfo.
struct TypeInfo {
  TypeKind type_kind;
  bool internal_product_mode_only = false;
  std::optional<LanguageFeature> type_feature;
  std::optional<LanguageFeature> alias_feature;
  std::optional<LanguageFeature> disabling_type_feature;
};

// A map joining SimpleTypeNameInfoMap and SimpleTypeKindInfoMap.
// Caller takes ownership of the result.
std::map<absl::string_view, TypeInfo>* BuildSimpleTypeInfoMap() {
  auto* result = new std::map<absl::string_view, TypeInfo>;
  for (const auto& item : SimpleTypeNameInfoMap()) {
    const TypeNameInfo& type_name_info = item.second;
    TypeKind type_kind = type_name_info.type_kind;
    const TypeKindInfo* type_kind_info = GetSimpleTypeKindInfo(type_kind);
    ABSL_CHECK(type_kind_info != nullptr)
        << TypeKind_Name(type_kind) << " not found in SimpleTypeKindInfoMap()";
    result->emplace(
        item.first,
        TypeInfo{type_kind,
                 type_name_info.internal_product_mode_only ||
                     type_kind_info->internal_product_mode_only,
                 type_kind_info->type_feature, type_name_info.alias_feature,
                 type_kind_info->disabling_type_feature});
  }
  return result;
}

DateValueContentType GetDateValue(const ValueContent& value) {
  return value.GetAs<DateValueContentType>();
}

const NumericValue& GetNumericValue(const ValueContent& value) {
  return value.GetAs<internal::NumericRef*>()->value();
}

const BigNumericValue& GetBigNumericValue(const ValueContent& value) {
  return value.GetAs<internal::BigNumericRef*>()->value();
}

const std::string& GetStringValue(const ValueContent& value) {
  return value.GetAs<internal::StringRef*>()->value();
}

const std::string& GetBytesValue(const ValueContent& value) {
  return GetStringValue(value);
}

std::string GetJsonString(const ValueContent& value) {
  return value.GetAs<internal::JSONRef*>()->ToString();
}

// Return a `JSONValueConstRef` for the given ValueContent `value`.
// For unparsed JSON values, it will be parsed on the fly, and the return value
// is a const reference of the input `json_value`. Invalid unparsed
// JSON strings will return an error, the callers should check the status.
absl::StatusOr<JSONValueConstRef> GetJsonValue(const ValueContent& value,
                                               JSONValue* json_value) {
  auto json_ptr = value.GetAs<internal::JSONRef*>();
  if (json_ptr->document().has_value()) {
    return json_ptr->document().value();
  }
  const std::string* unparsed_string =
      value.GetAs<internal::JSONRef*>()->unparsed_string();
  GOOGLESQL_ASSIGN_OR_RETURN(auto parsed_json,
                   JSONValue::ParseJSONString(*unparsed_string));
  ABSL_DCHECK(json_value != nullptr);
  *json_value = JSONValue::MoveFrom(parsed_json.GetRef());
  return json_value->GetConstRef();
}

const IntervalValue& GetIntervalValue(const ValueContent& value) {
  return value.GetAs<internal::IntervalRef*>()->value();
}

const tokens::TokenList& GetTokenListValue(const ValueContent& value) {
  return value.GetAs<internal::TokenListRef*>()->value();
}

const UuidValue& GetUuidValue(const ValueContent& value) {
  return value.GetAs<internal::UuidRef*>()->value();
}

std::string AddTypePrefix(absl::string_view value, const Type* type,
                          ProductMode mode) {
  return absl::StrCat(type->TypeName(mode), " ", ToStringLiteral(value));
}

const TimestampPicosValue& GetTimestampPicosValue(const ValueContent& value) {
  return value.GetAs<internal::TimestampPicosRef*>()->value();
}

template <typename ContentT>
bool ContentEquals(const ValueContent& x, const ValueContent& y) {
  return x.GetAs<ContentT>() == y.GetAs<ContentT>();
}

template <typename ReferenceT>
bool ReferencedValueEquals(const ValueContent& x, const ValueContent& y) {
  return x.GetAs<ReferenceT*>()->value() == y.GetAs<ReferenceT*>()->value();
}

template <typename ContentT>
bool ContentLess(const ValueContent& x, const ValueContent& y) {
  return x.GetAs<ContentT>() < y.GetAs<ContentT>();
}

template <typename ReferenceT>
bool ReferencedValueLess(const ValueContent& x, const ValueContent& y) {
  return x.GetAs<ReferenceT*>()->value() < y.GetAs<ReferenceT*>()->value();
}

}  // namespace

SimpleType::SimpleType(const TypeFactoryBase* factory, TypeKind kind)
    : Type(factory, kind) {
  ABSL_CHECK(IsSimpleType(kind)) << kind;
}

SimpleType::~SimpleType() = default;

bool SimpleType::IsSupportedType(
    const LanguageOptions& language_options) const {
  const TypeKindInfo* info = GetSimpleTypeKindInfo(kind());
  if (info == nullptr) {
    return false;
  }
  if (language_options.product_mode() == PRODUCT_EXTERNAL &&
      info->internal_product_mode_only) {
    return false;
  }
  if (info->type_feature.has_value() &&
      !language_options.LanguageFeatureEnabled(info->type_feature.value())) {
    return false;
  } else if (info->disabling_type_feature.has_value() &&
             language_options.LanguageFeatureEnabled(
                 *info->disabling_type_feature)) {
    return false;
  }

  return true;
}

void SimpleType::DebugStringImpl(bool details, TypeOrStringVector* stack,
                                 std::string* debug_string) const {
  absl::StrAppend(debug_string, TypeKindToString(kind(), PRODUCT_INTERNAL));
}

absl::Status SimpleType::SerializeToProtoAndDistinctFileDescriptorsImpl(
    const BuildFileDescriptorSetMapOptions& options, TypeProto* type_proto,
    FileDescriptorSetMap* file_descriptor_set_map) const {
  type_proto->set_type_kind(kind());
  return absl::OkStatus();
}

std::string SimpleType::TypeName(ProductMode mode,
                                 bool use_external_float32) const {
  return TypeKindToString(kind(), mode, use_external_float32);
}

absl::StatusOr<std::string> SimpleType::TypeNameWithModifiers(
    const TypeModifiers& type_modifiers, ProductMode mode,
    bool use_external_float32) const {
  std::string result_type_name = TypeName(mode, use_external_float32);
  const TypeParameters& type_params = type_modifiers.type_parameters();
  // Prepares string for type parameters to append to type name.
  if (!type_params.IsEmpty()) {
    GOOGLESQL_RET_CHECK(type_params.child_list().empty() &&
              !type_params.IsExtendedTypeParameters())
        << "Input type parameter does not correspond to SimpleType";

    std::string type_param_name = "";
    if (type_params.IsNumericTypeParameters()) {
      if (type_params.numeric_type_parameters().has_is_max_precision()) {
        type_param_name = "(MAX, ";
      } else {
        type_param_name = absl::Substitute(
            "($0, ", type_params.numeric_type_parameters().precision());
      }
      absl::StrAppend(
          &type_param_name,
          absl::Substitute("$0)",
                           type_params.numeric_type_parameters().scale()));
    }
    if (type_params.IsStringTypeParameters()) {
      if (type_params.string_type_parameters().has_is_max_length()) {
        type_param_name = "(MAX)";
      } else {
        type_param_name = absl::Substitute(
            "($0)", type_params.string_type_parameters().max_length());
      }
    }
    if (type_params.IsTimestampTypeParameters()) {
      type_param_name = absl::Substitute(
          "($0)", type_params.timestamp_type_parameters().precision());
    }

    absl::StrAppend(&result_type_name, type_param_name);
  }

  // Prepares string for collation to append to type name.
  const Collation& collation = type_modifiers.collation();
  if (!collation.Empty()) {
    if (!collation.HasCompatibleStructure(this)) {
      return MakeSqlError() << "Input collation " << collation.DebugString()
                            << " is not compatible with type " << DebugString();
    }
    absl::StrAppend(&result_type_name, " COLLATE \'", collation.DebugString(),
                    "\'");
  }

  return result_type_name;
}

std::string SimpleType::CapitalizedName() const {
  switch (kind()) {
    case TYPE_INT32:
      return "Int32";
    case TYPE_INT64:
      return "Int64";
    case TYPE_UINT32:
      return "Uint32";
    case TYPE_UINT64:
      return "Uint64";
    case TYPE_BOOL:
      return "Bool";
    case TYPE_FLOAT:
      return "Float";
    case TYPE_DOUBLE:
      return "Double";
    case TYPE_STRING:
      return "String";
    case TYPE_BYTES:
      return "Bytes";
    case TYPE_DATE:
      return "Date";
    case TYPE_TIMESTAMP:
      return "Timestamp";
    case TYPE_TIME:
      return "Time";
    case TYPE_DATETIME:
      return "Datetime";
    case TYPE_INTERVAL:
      return "Interval";
    case TYPE_GEOGRAPHY:
      return "Geography";
    case TYPE_NUMERIC:
      return "Numeric";
    case TYPE_BIGNUMERIC:
      return "BigNumeric";
    case TYPE_JSON:
      return "Json";
    case TYPE_TOKENLIST:
      return "TokenList";
    case TYPE_UUID:
      return "Uuid";
    default:
      ABSL_LOG(FATAL) << "Unexpected simple type kind: " << kind();
  }
}

TypeKind SimpleType::GetTypeKindIfSimple(
    absl::string_view type_name, ProductMode mode,
    const LanguageOptions::LanguageFeatureSet* enabled_language_features) {
  static const std::map<absl::string_view, TypeInfo>* type_map =
      BuildSimpleTypeInfoMap();
  const TypeInfo* type_info =
      googlesql_base::FindOrNull(*type_map, absl::AsciiStrToLower(type_name));
  if (type_info == nullptr) {
    return TYPE_UNKNOWN;
  }
  if (mode == PRODUCT_EXTERNAL && type_info->internal_product_mode_only) {
    return TYPE_UNKNOWN;
  }
  if (enabled_language_features != nullptr) {
    if (type_info->type_feature.has_value() &&
        !enabled_language_features->contains(*type_info->type_feature)) {
      return TYPE_UNKNOWN;
    } else if (type_info->disabling_type_feature.has_value() &&
               enabled_language_features->contains(
                   *type_info->disabling_type_feature)) {
      return TYPE_UNKNOWN;
    }
    if (type_info->alias_feature.has_value() &&
        !enabled_language_features->contains(*type_info->alias_feature)) {
      return TYPE_UNKNOWN;
    }
  }
  return type_info->type_kind;
}

bool SimpleType::SupportsGroupingImpl(const LanguageOptions& language_options,
                                      const Type** no_grouping_type) const {
  const bool supports_grouping =
      !this->IsGeography() && !this->IsTokenList() &&
      !(this->IsJson() && !language_options.LanguageFeatureEnabled(
                              FEATURE_JSON_TYPE_COMPARISON)) &&
      !(this->IsFloatingPoint() && language_options.LanguageFeatureEnabled(
                                       FEATURE_DISALLOW_GROUP_BY_FLOAT));
  if (no_grouping_type != nullptr) {
    *no_grouping_type = supports_grouping ? nullptr : this;
  }
  return supports_grouping;
}

void SimpleType::CopyValueContent(TypeKind kind, const ValueContent& from,
                                  ValueContent* to) {
  switch (kind) {
    case TYPE_STRING:
    case TYPE_BYTES:
      from.GetAs<internal::StringRef*>()->Ref();
      break;
    case TYPE_GEOGRAPHY:
      from.GetAs<internal::GeographyRef*>()->Ref();
      break;
    case TYPE_NUMERIC:
      from.GetAs<internal::NumericRef*>()->Ref();
      break;
    case TYPE_BIGNUMERIC:
      from.GetAs<internal::BigNumericRef*>()->Ref();
      break;
    case TYPE_TIMESTAMP:
      from.GetAs<internal::TimestampPicosRef*>()->Ref();
      break;
    case TYPE_INTERVAL:
      from.GetAs<internal::IntervalRef*>()->Ref();
      break;
    case TYPE_JSON:
      from.GetAs<internal::JSONRef*>()->Ref();
      break;
    case TYPE_TOKENLIST:
      from.GetAs<internal::TokenListRef*>()->Ref();
      break;
    case TYPE_UUID:
      from.GetAs<internal::UuidRef*>()->Ref();
      break;
    default:
      break;
  }

  *to = from;
}

void SimpleType::CopyValueContent(const ValueContent& from,
                                  ValueContent* to) const {
  CopyValueContent(kind(), from, to);
}

void SimpleType::ClearValueContent(TypeKind kind, const ValueContent& value) {
  switch (kind) {
    case TYPE_STRING:
    case TYPE_BYTES:
      value.GetAs<internal::StringRef*>()->Unref();
      return;
    case TYPE_GEOGRAPHY:
      value.GetAs<internal::GeographyRef*>()->Unref();
      return;
    case TYPE_NUMERIC:
      value.GetAs<internal::NumericRef*>()->Unref();
      return;
    case TYPE_BIGNUMERIC:
      value.GetAs<internal::BigNumericRef*>()->Unref();
      return;
    case TYPE_TIMESTAMP:
      value.GetAs<internal::TimestampPicosRef*>()->Unref();
      return;
    case TYPE_INTERVAL:
      value.GetAs<internal::IntervalRef*>()->Unref();
      return;
    case TYPE_JSON:
      value.GetAs<internal::JSONRef*>()->Unref();
      return;
    case TYPE_TOKENLIST:
      value.GetAs<internal::TokenListRef*>()->Unref();
      return;
    case TYPE_UUID:
      value.GetAs<internal::UuidRef*>()->Unref();
      return;
    default:
      return;
  }
}

void SimpleType::ClearValueContent(const ValueContent& value) const {
  ClearValueContent(kind(), value);
}

uint64_t SimpleType::GetValueContentExternallyAllocatedByteSize(
    const ValueContent& value) const {
  switch (kind()) {
    case TYPE_STRING:
    case TYPE_BYTES:
      return value.GetAs<internal::StringRef*>()->physical_byte_size();
    case TYPE_GEOGRAPHY:
      return value.GetAs<internal::GeographyRef*>()->physical_byte_size();
    case TYPE_NUMERIC:
      return sizeof(internal::NumericRef);
    case TYPE_BIGNUMERIC:
      return sizeof(internal::BigNumericRef);
    case TYPE_TIMESTAMP:
      return sizeof(internal::TimestampPicosRef);
    case TYPE_JSON:
      return value.GetAs<internal::JSONRef*>()->physical_byte_size();
    case TYPE_TOKENLIST:
      return value.GetAs<internal::TokenListRef*>()->physical_byte_size();
    case TYPE_UUID:
      return sizeof(internal::UuidRef);
    default:
      return 0;
  }
}

absl::HashState SimpleType::HashTypeParameter(absl::HashState state) const {
  return state;  // Simple types don't have parameters.
}

namespace {

absl::HashState HashTokenList(const ValueContent& value,
                              absl::HashState state) {
  return absl::HashState::combine(std::move(state), GetTokenListValue(value));
}

}  // namespace

absl::HashState SimpleType::HashValueContent(const ValueContent& value,
                                             absl::HashState state) const {
  // These codes are picked arbitrarily.
  static constexpr uint64_t kFloatNanHashCode = 0x739EF9A0B2C15522ull;
  static constexpr uint64_t kDoubleNanHashCode = 0xA00397BC84F93AA7ull;
  static constexpr uint64_t kGeographyHashCode = 0x98389DC9632631AEull;

  switch (kind()) {
    case TYPE_INT32:
      return absl::HashState::combine(std::move(state), value.GetAs<int32_t>());
    case TYPE_INT64:
      return absl::HashState::combine(std::move(state), value.GetAs<int64_t>());
    case TYPE_UINT32:
      return absl::HashState::combine(std::move(state),
                                      value.GetAs<uint32_t>());
    case TYPE_UINT64:
      return absl::HashState::combine(std::move(state),
                                      value.GetAs<uint64_t>());
    case TYPE_BOOL:
      return absl::HashState::combine(std::move(state), value.GetAs<bool>());
    case TYPE_FLOAT: {
      float float_value = value.GetAs<float>();
      if (std::isnan(float_value)) {
        return absl::HashState::combine(std::move(state), kFloatNanHashCode);
      }
      return absl::HashState::combine(std::move(state), float_value);
    }
    case TYPE_DOUBLE: {
      double double_value = value.GetAs<double>();
      if (std::isnan(double_value)) {
        return absl::HashState::combine(std::move(state), kDoubleNanHashCode);
      }
      return absl::HashState::combine(std::move(state), double_value);
    }
    case TYPE_STRING:
    case TYPE_BYTES:
      return absl::HashState::combine(std::move(state), GetStringValue(value));
    case TYPE_DATE:
      return absl::HashState::combine(std::move(state), GetDateValue(value));
    case TYPE_TIMESTAMP:
      return absl::HashState::combine(std::move(state),
                                      GetTimestampPicosValue(value).HashCode());
    case TYPE_TIME:
      return absl::HashState::combine(std::move(state),
                                      value.GetAs<TimeValueContentType>(),
                                      value.simple_type_extended_content_);
    case TYPE_DATETIME:
      return absl::HashState::combine(std::move(state),
                                      value.GetAs<DateTimeValueContentType>(),
                                      value.simple_type_extended_content_);
    case TYPE_INTERVAL:
      return absl::HashState::combine(std::move(state),
                                      GetIntervalValue(value));
    case TYPE_NUMERIC:
      return absl::HashState::combine(std::move(state), GetNumericValue(value));
    case TYPE_BIGNUMERIC:
      return absl::HashState::combine(std::move(state),
                                      GetBigNumericValue(value));
    case TYPE_GEOGRAPHY:
      // We have no good hasher for geography (??)
      // so we just rely on a constant for hashing.
      return absl::HashState::combine(std::move(state), kGeographyHashCode);
    case TYPE_JSON: {
      JSONValue json_value;
      auto json_ref = GetJsonValue(value, &json_value);
      if (!json_ref.ok()) {
        // Invalid JSON values are hashed by their raw string.
        return absl::HashState::combine(std::move(state), GetJsonString(value));
      }
      return absl::HashState::combine(std::move(state), json_ref.value());
    }
    case TYPE_TOKENLIST:
      return HashTokenList(value, std::move(state));
    case TYPE_UUID:
      return absl::HashState::combine(std::move(state), GetUuidValue(value));
    default:
      ABSL_LOG(ERROR) << "Unexpected type kind: " << kind();
      return state;
  }
}

// Returns true if two INTERVAL values are an exact match (where 1 MONTH and
// 30 DAYS are *not* considered equal).
bool AllPartsIntervalMatch(const IntervalValue& x, const IntervalValue& y) {
  return x.get_months() == y.get_months() && x.get_days() == y.get_days() &&
         x.get_micros() == y.get_micros() &&
         x.get_nano_fractions() == y.get_nano_fractions();
}

bool JsonEquals(const ValueContent& x, const ValueContent& y) {
  JSONValue x_value;
  auto x_ptr = GetJsonValue(x, &x_value);
  JSONValue y_value;
  auto y_ptr = GetJsonValue(y, &y_value);

  // The most common case is that both values are valid JSON, so we check that
  // first.
  if (x_ptr.ok() && y_ptr.ok()) {
    // Valid JSON values are equal if they have the same JSON value using the
    // JSONValueConstRef::operator==().
    return x_ptr.value() == y_ptr.value();
  }
  if (!x_ptr.ok() && !y_ptr.ok()) {
    // Invalid JSON values are compared by their raw string.
    // This is the existing behavior and consistent with its hash function:
    // invalid JSON values are hashed by their raw string.
    return GetJsonString(x) == GetJsonString(y);
  }

  // Invalid JSON values are not equal to valid JSON values, and their hash
  // values are never the same.
  return false;
}

bool JsonLess(const ValueContent& x, const ValueContent& y) {
  JSONValue x_value;
  auto x_ptr = GetJsonValue(x, &x_value);

  JSONValue y_value;
  auto y_ptr = GetJsonValue(y, &y_value);

  if (!x_ptr.ok() || !y_ptr.ok()) {
    // Invalid JSON values are not less than each other.
    return false;
  }

  return x_ptr.value() < y_ptr.value();
}

bool SimpleType::ValueContentEquals(
    const ValueContent& x, const ValueContent& y,
    const ValueEqualityCheckOptions& options) const {
  switch (kind()) {
    case TYPE_INT32:
      return ContentEquals<int32_t>(x, y);
    case TYPE_INT64:
      return ContentEquals<int64_t>(x, y);
    case TYPE_UINT32:
      return ContentEquals<uint32_t>(x, y);
    case TYPE_UINT64:
      return ContentEquals<uint64_t>(x, y);
    case TYPE_BOOL:
      return ContentEquals<bool>(x, y);
    case TYPE_FLOAT:
      return options.float_margin.Equal(x.GetAs<float>(), y.GetAs<float>());
    case TYPE_DOUBLE:
      return options.float_margin.Equal(x.GetAs<double>(), y.GetAs<double>());
    case TYPE_STRING:
    case TYPE_BYTES:
      return ReferencedValueEquals<internal::StringRef>(x, y);
    case TYPE_DATE:
      return ContentEquals<DateValueContentType>(x, y);
    case TYPE_TIMESTAMP:
      return ReferencedValueEquals<internal::TimestampPicosRef>(x, y);
    case TYPE_TIME:
      return ContentEquals<TimeValueContentType>(x, y) &&
             x.simple_type_extended_content_ == y.simple_type_extended_content_;
    case TYPE_DATETIME:
      return ContentEquals<DateTimeValueContentType>(x, y) &&
             x.simple_type_extended_content_ == y.simple_type_extended_content_;
    case TYPE_INTERVAL:
      switch (options.interval_compare_mode) {
        case IntervalCompareMode::kAllPartsEqual:
          return AllPartsIntervalMatch(
              x.GetAs<internal::IntervalRef*>()->value(),
              y.GetAs<internal::IntervalRef*>()->value());
        case IntervalCompareMode::kSqlEquals:
          return ReferencedValueEquals<internal::IntervalRef>(x, y);
      }
    case TYPE_NUMERIC:
      return ReferencedValueEquals<internal::NumericRef>(x, y);
    case TYPE_BIGNUMERIC:
      return ReferencedValueEquals<internal::BigNumericRef>(x, y);
    case TYPE_JSON: {
      return JsonEquals(x, y);
    }
    case TYPE_TOKENLIST:
      return GetTokenListValue(x).EquivalentTo(GetTokenListValue(y));
    case TYPE_UUID:
      return ReferencedValueEquals<internal::UuidRef>(x, y);
    default:
      ABSL_LOG(FATAL) << "Unexpected simple type kind: " << kind();
  }
}

bool SimpleType::ValueContentLess(const ValueContent& x, const ValueContent& y,
                                  const Type* other_type) const {
  switch (kind()) {
    case TYPE_INT32:
      return ContentLess<int32_t>(x, y);
    case TYPE_INT64:
      return ContentLess<int64_t>(x, y);
    case TYPE_UINT32:
      return ContentLess<uint32_t>(x, y);
    case TYPE_UINT64:
      return ContentLess<uint64_t>(x, y);
    case TYPE_BOOL:
      return ContentLess<bool>(x, y);
    case TYPE_FLOAT:
      if (std::isnan(x.GetAs<float>()) && !std::isnan(y.GetAs<float>())) {
        return true;
      }
      if (std::isnan(y.GetAs<float>())) {
        return false;
      }
      return ContentLess<float>(x, y);
    case TYPE_DOUBLE:
      if (std::isnan(x.GetAs<double>()) && !std::isnan(y.GetAs<double>())) {
        return true;
      }
      if (std::isnan(y.GetAs<double>())) {
        return false;
      }
      return ContentLess<double>(x, y);
    case TYPE_STRING:
    case TYPE_BYTES:
      return ReferencedValueLess<internal::StringRef>(x, y);
    case TYPE_DATE:
      return ContentLess<DateValueContentType>(x, y);
    case TYPE_TIMESTAMP:
      return ReferencedValueLess<internal::TimestampPicosRef>(x, y);
    case TYPE_TIME:
      return ContentLess<TimeValueContentType>(x, y) ||
             (ContentEquals<TimeValueContentType>(x, y) &&
              x.simple_type_extended_content_ <
                  y.simple_type_extended_content_);
    case TYPE_DATETIME:
      return ContentLess<DateTimeValueContentType>(x, y) ||
             (ContentEquals<DateTimeValueContentType>(x, y) &&
              x.simple_type_extended_content_ <
                  y.simple_type_extended_content_);
    case TYPE_INTERVAL:
      return ReferencedValueLess<internal::IntervalRef>(x, y);
    case TYPE_NUMERIC:
      return ReferencedValueLess<internal::NumericRef>(x, y);
    case TYPE_BIGNUMERIC:
      return ReferencedValueLess<internal::BigNumericRef>(x, y);
    case TYPE_UUID:
      return ReferencedValueLess<internal::UuidRef>(x, y);
    case TYPE_JSON:
      return JsonLess(x, y);
    default:
      ABSL_LOG(ERROR) << "Cannot compare " << DebugString() << " to "
                  << DebugString();
      return false;
  }
}

std::string SimpleType::FormatTokenList(
    const ValueContent& value, const FormatValueContentOptions& options) const {
  return search::FormatTokenList(
      GetTokenListValue(value),
      options.mode == FormatValueContentOptions::Mode::kDebug,
      options.collapse_identical_tokens,
      options.format_token_attribute ? options.format_token_attribute
                                     : search::DefaultFormatAttribute);
}

std::vector<std::string> SimpleType::FormatTokenLines(
    const ValueContent& value, const FormatValueContentOptions& options) {
  return search::FormatTokenLines(
      GetTokenListValue(value), options.collapse_identical_tokens,
      options.format_token_attribute ? options.format_token_attribute
                                     : search::DefaultFormatAttribute);
}

std::string SimpleType::FormatValueContent(
    const ValueContent& value, const FormatValueContentOptions& options) const {
  switch (kind()) {
    case TYPE_BOOL:
      return (value.GetAs<bool>() ? "true" : "false");
    case TYPE_STRING:
      return ToStringLiteral(GetStringValue(value));
    case TYPE_BYTES:
      return ToBytesLiteral(GetBytesValue(value));
    case TYPE_DATE: {
      std::string s;
      // Failure cannot actually happen in this context since date_value()
      // is guaranteed to be valid.
      GOOGLESQL_CHECK_OK(functions::ConvertDateToString(GetDateValue(value), &s));
      return options.add_simple_type_prefix()
                 ? AddTypePrefix(s, this, options.product_mode)
                 : s;
    }
    case TYPE_TIMESTAMP: {
      std::string s;
      // Failure cannot actually happen in this context since the value
      // is guaranteed to be valid.
      GOOGLESQL_CHECK_OK(functions::ConvertTimestampToString(  // Crash OK
          GetTimestampPicosValue(value).ToPicoTime(), absl::UTCTimeZone(), &s));

      // TODO: Update the following code once timestamp_picos
      // literal syntax is decided.
      return options.add_simple_type_prefix()
                 ? AddTypePrefix(s, this, options.product_mode)
                 : s;
    }
    case TYPE_TIME: {
      std::string s = GetTimeValue(value).DebugString();
      return options.add_simple_type_prefix()
                 ? AddTypePrefix(s, this, options.product_mode)
                 : s;
    }
    case TYPE_DATETIME: {
      std::string s = GetDateTimeValue(value).DebugString();
      return options.add_simple_type_prefix()
                 ? AddTypePrefix(s, this, options.product_mode)
                 : s;
    }
    case TYPE_INT32:
      return options.as_literal()
                 ? absl::StrCat(value.GetAs<int32_t>())
                 : internal::GetCastExpressionString(
                       value.GetAs<int32_t>(), this, options.product_mode);
    case TYPE_UINT32:
      return options.as_literal()
                 ? absl::StrCat(value.GetAs<uint32_t>())
                 : internal::GetCastExpressionString(
                       value.GetAs<uint32_t>(), this, options.product_mode);
    case TYPE_INT64:
      return absl::StrCat(value.GetAs<int64_t>());
    case TYPE_UINT64:
      return options.as_literal()
                 ? absl::StrCat(value.GetAs<uint64_t>())
                 : internal::GetCastExpressionString(
                       value.GetAs<uint64_t>(), this, options.product_mode);
    case TYPE_FLOAT: {
      const float float_value = value.GetAs<float>();

      if (options.mode == FormatValueContentOptions::Mode::kDebug) {
        return RoundTripFloatToString(float_value);
      }

      // Floats and doubles like "inf" and "nan" need to be quoted.
      if (!std::isfinite(float_value)) {
        return internal::GetCastExpressionString(
            ToStringLiteral(RoundTripFloatToString(float_value)), this,
            options.product_mode, options.use_external_float32);
      } else {
        std::string s = RoundTripFloatToString(float_value);
        // Make sure that doubles always print with a . or an 'e' so they
        // don't look like integers.
        if (options.as_literal() &&
            s.find_first_not_of("-0123456789") == std::string::npos) {
          s.append(".0");
        }
        return options.as_literal() ? s
                                    : internal::GetCastExpressionString(
                                          s, this, options.product_mode,
                                          options.use_external_float32);
      }
    }
    case TYPE_DOUBLE: {
      const double double_value = value.GetAs<double>();

      if (options.mode == FormatValueContentOptions::Mode::kDebug) {
        // TODO I would like to change this so it returns "1.0" rather
        // than "1", like GetSQL(), but that affects a lot of client code.
        return RoundTripDoubleToString(double_value);
      }

      if (!std::isfinite(double_value)) {
        return internal::GetCastExpressionString(
            ToStringLiteral(RoundTripDoubleToString(double_value)), this,
            options.product_mode);
      } else {
        std::string s = RoundTripDoubleToString(double_value);
        // Make sure that doubles always print with a . or an 'e' so they
        // don't look like integers.
        if (s.find_first_not_of("-0123456789") == std::string::npos) {
          s.append(".0");
        }
        return s;
      }
    }
    case TYPE_NUMERIC: {
      std::string s = GetNumericValue(value).ToString();
      return options.add_simple_type_prefix()
                 ? absl::StrCat("NUMERIC ", ToStringLiteral(s))
                 : s;
    }
    case TYPE_BIGNUMERIC: {
      std::string s = GetBigNumericValue(value).ToString();
      return options.add_simple_type_prefix()
                 ? absl::StrCat("BIGNUMERIC ", ToStringLiteral(s))
                 : s;
    }
    case TYPE_INTERVAL: {
      std::string s = GetIntervalValue(value).ToString();
      return options.mode != FormatValueContentOptions::Mode::kDebug
                 ? absl::StrCat("INTERVAL ", ToStringLiteral(s),
                                " YEAR TO SECOND")
                 : s;
    }
    case TYPE_JSON: {
      std::string s = GetJsonString(value);
      return options.add_simple_type_prefix()
                 ? absl::StrCat("JSON ", ToStringLiteral(s))
                 : s;
    }
    case TYPE_UUID: {
      std::string s = GetUuidValue(value).ToString();
      return options.add_simple_type_prefix()
                 ? internal::GetCastExpressionString(
                       ToSingleQuotedStringLiteral(s), this,
                       options.product_mode)
                 : s;
    }
    case TYPE_TOKENLIST:
      return FormatTokenList(value, options);
    default:
      ABSL_LOG(ERROR) << "Unexpected type kind: " << kind();
      return "<Invalid simple type's value>";
  }
}

absl::Status SimpleType::SerializeValueContent(const ValueContent& value,
                                               ValueProto* value_proto) const {
  switch (kind()) {
    case TYPE_INT32:
      value_proto->set_int32_value(value.GetAs<int32_t>());
      break;
    case TYPE_INT64:
      value_proto->set_int64_value(value.GetAs<int64_t>());
      break;
    case TYPE_UINT32:
      value_proto->set_uint32_value(value.GetAs<uint32_t>());
      break;
    case TYPE_UINT64:
      value_proto->set_uint64_value(value.GetAs<uint64_t>());
      break;
    case TYPE_BOOL:
      value_proto->set_bool_value(value.GetAs<bool>());
      break;
    case TYPE_FLOAT:
      value_proto->set_float_value(value.GetAs<float>());
      break;
    case TYPE_DOUBLE:
      value_proto->set_double_value(value.GetAs<double>());
      break;
    case TYPE_NUMERIC:
      value_proto->set_numeric_value(
          GetNumericValue(value).SerializeAsProtoBytes());
      break;
    case TYPE_BIGNUMERIC:
      value_proto->set_bignumeric_value(
          GetBigNumericValue(value).SerializeAsProtoBytes());
      break;
    case TYPE_JSON:
      value_proto->set_json_value(GetJsonString(value));
      break;
    case TYPE_STRING:
      value_proto->set_string_value(GetStringValue(value));
      break;
    case TYPE_BYTES:
      value_proto->set_bytes_value(GetBytesValue(value));
      break;
    case TYPE_DATE:
      value_proto->set_date_value(GetDateValue(value));
      break;
    case TYPE_TIMESTAMP: {
      const PicoTime pico = GetTimestampPicosValue(value).ToPicoTime();
      if (pico.SubNanoseconds() > 0) {
        // Serialize to timestamp picos since we have picosecond precision.
        auto* timestamp_picos_proto =
            value_proto->mutable_timestamp_picos_value();
        auto [seconds, picos] = pico.SecondsAndPicoseconds();
        timestamp_picos_proto->set_seconds(seconds);
        timestamp_picos_proto->set_picos(picos);
      } else {
        // Serialize to timestamp for backwards compatibility.
        GOOGLESQL_RETURN_IF_ERROR(googlesql_base::EncodeGoogleApiProto(
            pico.ToAbslTime(), value_proto->mutable_timestamp_value()));
      }
      break;
    }
    case TYPE_DATETIME: {
      auto* datetime_proto = value_proto->mutable_datetime_value();
      datetime_proto->set_bit_field_datetime_seconds(
          GetDateTimeValue(value).Packed64DatetimeSeconds());
      datetime_proto->set_nanos(GetDateTimeValue(value).Nanoseconds());
      break;
    }
    case TYPE_TIME:
      value_proto->set_time_value(GetTimeValue(value).Packed64TimeNanos());
      break;
    case TYPE_INTERVAL:
      value_proto->set_interval_value(
          GetIntervalValue(value).SerializeAsBytes());
      break;
    case TYPE_TOKENLIST:
      value_proto->set_tokenlist_value(GetTokenListValue(value).GetBytes());
      break;
    case TYPE_UUID:
      value_proto->set_uuid_value(GetUuidValue(value).SerializeAsBytes());
      break;
    default:
      return absl::Status(absl::StatusCode::kInternal,
                          absl::StrCat("Unsupported type ", DebugString()));
  }
  return absl::OkStatus();
}

absl::Status SimpleType::DeserializeValueContent(const ValueProto& value_proto,
                                                 ValueContent* value) const {
  switch (kind()) {
    case TYPE_INT32:
      if (!value_proto.has_int32_value()) {
        return TypeMismatchError(value_proto);
      }
      value->set(value_proto.int32_value());
      break;
    case TYPE_INT64:
      if (!value_proto.has_int64_value()) {
        return TypeMismatchError(value_proto);
      }
      value->set(value_proto.int64_value());
      break;
    case TYPE_UINT32:
      if (!value_proto.has_uint32_value()) {
        return TypeMismatchError(value_proto);
      }
      value->set(value_proto.uint32_value());
      break;
    case TYPE_UINT64:
      if (!value_proto.has_uint64_value()) {
        return TypeMismatchError(value_proto);
      }
      value->set(value_proto.uint64_value());
      break;
    case TYPE_BOOL:
      if (!value_proto.has_bool_value()) {
        return TypeMismatchError(value_proto);
      }
      value->set(value_proto.bool_value());
      break;
    case TYPE_FLOAT:
      if (!value_proto.has_float_value()) {
        return TypeMismatchError(value_proto);
      }
      value->set(value_proto.float_value());
      break;
    case TYPE_DOUBLE:
      if (!value_proto.has_double_value()) {
        return TypeMismatchError(value_proto);
      }
      value->set(value_proto.double_value());
      break;
    case TYPE_NUMERIC: {
      if (!value_proto.has_numeric_value()) {
        return TypeMismatchError(value_proto);
      }
      GOOGLESQL_ASSIGN_OR_RETURN(
          NumericValue numeric_v,
          NumericValue::DeserializeFromProtoBytes(value_proto.numeric_value()));
      value->set(new internal::NumericRef(numeric_v));
      break;
    }
    case TYPE_BIGNUMERIC: {
      if (!value_proto.has_bignumeric_value()) {
        return TypeMismatchError(value_proto);
      }
      GOOGLESQL_ASSIGN_OR_RETURN(BigNumericValue bignumeric_v,
                       BigNumericValue::DeserializeFromProtoBytes(
                           value_proto.bignumeric_value()));
      value->set(new internal::BigNumericRef(bignumeric_v));
      break;
    }
    case TYPE_JSON: {
      if (!value_proto.has_json_value()) {
        return TypeMismatchError(value_proto);
      }
      value->set(new internal::JSONRef(std::move(value_proto.json_value())));
      break;
    }
    case TYPE_STRING:
      if (!value_proto.has_string_value()) {
        return TypeMismatchError(value_proto);
      }
      value->set(new internal::StringRef(
          std::string(value_proto.string_value())));
      break;
    case TYPE_BYTES:
      if (!value_proto.has_bytes_value()) {
        return TypeMismatchError(value_proto);
      }
      value->set(new internal::StringRef(
          std::string(value_proto.bytes_value())));
      break;
    case TYPE_DATE:
      if (!value_proto.has_date_value()) {
        return TypeMismatchError(value_proto);
      }
      if (!functions::IsValidDate(value_proto.date_value())) {
        return absl::Status(
            absl::StatusCode::kOutOfRange,
            absl::StrCat("Invalid value for DATE: ", value_proto.date_value()));
      }

      value->set(value_proto.date_value());
      break;
    case TYPE_TIMESTAMP: {
      if (value_proto.has_timestamp_value()) {
        auto time_or =
            googlesql_base::DecodeGoogleApiProto(value_proto.timestamp_value());
        if (!time_or.ok()) {
          return absl::Status(
              absl::StatusCode::kOutOfRange,
              absl::StrCat("Invalid value for TIMESTAMP",
                           value_proto.timestamp_value().DebugString()));
        }
        absl::Time t = time_or.value();
        GOOGLESQL_ASSIGN_OR_RETURN(PicoTime pico, PicoTime::Create(t));
        value->set(new internal::TimestampPicosRef(TimestampPicosValue(pico)));
      } else if (value_proto.has_timestamp_picos_value()) {
        GOOGLESQL_ASSIGN_OR_RETURN(
            PicoTime pico,
            PicoTime::Create(absl::FromUnixSeconds(
                                 value_proto.timestamp_picos_value().seconds()),
                             value_proto.timestamp_picos_value().picos()));
        value->set(new internal::TimestampPicosRef(TimestampPicosValue(pico)));
      } else {
        return TypeMismatchError(value_proto);
      }
      break;
    }
    case TYPE_DATETIME: {
      if (!value_proto.has_datetime_value()) {
        return TypeMismatchError(value_proto);
      }
      DatetimeValue wrapper = DatetimeValue::FromPacked64SecondsAndNanos(
          value_proto.datetime_value().bit_field_datetime_seconds(),
          value_proto.datetime_value().nanos());

      GOOGLESQL_RETURN_IF_ERROR(SetDateTimeValue(wrapper, value));
      break;
    }
    case TYPE_TIME: {
      if (!value_proto.has_time_value()) {
        return TypeMismatchError(value_proto);
      }
      TimeValue wrapper =
          TimeValue::FromPacked64Nanos(value_proto.time_value());

      GOOGLESQL_RETURN_IF_ERROR(SetTimeValue(wrapper, value));
      break;
    }
    case TYPE_INTERVAL: {
      if (!value_proto.has_interval_value()) {
        return TypeMismatchError(value_proto);
      }
      GOOGLESQL_ASSIGN_OR_RETURN(
          IntervalValue interval_v,
          IntervalValue::DeserializeFromBytes(value_proto.interval_value()));
      value->set(new internal::IntervalRef(interval_v));
      break;
    }
    case TYPE_TOKENLIST: {
      if (!value_proto.has_tokenlist_value()) {
        return TypeMismatchError(value_proto);
      }
      value->set(
          new internal::TokenListRef(tokens::TokenList::FromBytesUnvalidated(
              std::string(value_proto.tokenlist_value()))));
      break;
    }
    case TYPE_UUID: {
      if (!value_proto.has_uuid_value()) {
        return TypeMismatchError(value_proto);
      }
      GOOGLESQL_ASSIGN_OR_RETURN(UuidValue uuid_v, UuidValue::DeserializeFromBytes(
                                             value_proto.uuid_value()));
      value->set(new internal::UuidRef(uuid_v));
      break;
    }
    default:
      return absl::Status(absl::StatusCode::kInternal,
                          absl::StrCat("Unsupported type ", DebugString()));
  }

  return absl::OkStatus();
}

absl::Time SimpleType::GetTimestampValue(const ValueContent& value) {
  return absl::FromUnixSeconds(value.GetAs<TimestampValueContentType>()) +
         absl::Nanoseconds(value.simple_type_extended_content_);
}

absl::Status SimpleType::SetTimestampValue(absl::Time time,
                                           ValueContent* value) {
  if (!functions::IsValidTime(time)) {
    return absl::Status(
        absl::StatusCode::kOutOfRange,
        absl::StrCat("Invalid value for TIMESTAMP: ", absl::FormatTime(time)));
  }

  const int64_t timestamp_seconds = absl::ToUnixSeconds(time);
  const int32_t subsecond_nanos = static_cast<int32_t>(
      (time - absl::FromUnixSeconds(timestamp_seconds)) / absl::Nanoseconds(1));

  value->set(timestamp_seconds);
  value->simple_type_extended_content_ = subsecond_nanos;

  return absl::OkStatus();
}

TimeValue SimpleType::GetTimeValue(const ValueContent& value) {
  return TimeValue::FromPacked32SecondsAndNanos(
      value.GetAs<TimeValueContentType>(), value.simple_type_extended_content_);
}

absl::Status SimpleType::SetTimeValue(TimeValue time, ValueContent* value) {
  if (!time.IsValid()) {
    return absl::Status(absl::StatusCode::kOutOfRange,
                        "Invalid value for TIME");
  }

  const TimeValueContentType seconds = time.Packed32TimeSeconds();
  const int32_t nanoseconds = time.Nanoseconds();

  value->set(seconds);
  value->simple_type_extended_content_ = nanoseconds;

  return absl::OkStatus();
}

DatetimeValue SimpleType::GetDateTimeValue(const ValueContent& value) {
  return DatetimeValue::FromPacked64SecondsAndNanos(
      value.GetAs<DateTimeValueContentType>(),
      value.simple_type_extended_content_);
}

absl::Status SimpleType::SetDateTimeValue(DatetimeValue datetime,
                                          ValueContent* value) {
  if (!datetime.IsValid()) {
    return absl::Status(absl::StatusCode::kOutOfRange,
                        "Invalid value for DATETIME");
  }

  const DateTimeValueContentType seconds = datetime.Packed64DatetimeSeconds();
  const int32_t nanoseconds = datetime.Nanoseconds();

  value->set(seconds);
  value->simple_type_extended_content_ = nanoseconds;

  return absl::OkStatus();
}

absl::StatusOr<TypeParameters> SimpleType::ValidateAndResolveTypeParameters(
    const std::vector<TypeParameterValue>& type_parameter_values,
    ProductMode mode) const {
  if (IsString() || IsBytes()) {
    return ResolveStringBytesTypeParameters(type_parameter_values, mode);
  }
  if (IsNumericType() || IsBigNumericType()) {
    return ResolveNumericBignumericTypeParameters(type_parameter_values, mode);
  }
  if (IsTimestamp()) {
    return ResolveTimestampTypeParameters(type_parameter_values, mode);
  }
  return MakeSqlError() << ShortTypeName(mode)
                        << " does not support type parameters";
}

absl::StatusOr<TypeParameters> SimpleType::ResolveStringBytesTypeParameters(
    absl::Span<const TypeParameterValue> type_parameter_values,
    ProductMode mode) const {
  if (type_parameter_values.size() != 1) {
    return MakeSqlError() << ShortTypeName(mode)
                          << " type can only have one parameter. Found "
                          << type_parameter_values.size() << " parameters";
  }

  StringTypeParametersProto type_parameters_proto;
  const TypeParameterValue& param = type_parameter_values[0];
  if (!param.IsSpecialLiteral() && param.GetValue().has_int64_value()) {
    if (param.GetValue().int64_value() <= 0) {
      return MakeSqlError()
             << ShortTypeName(mode) << " length must be greater than 0";
    }
    type_parameters_proto.set_max_length(param.GetValue().int64_value());
    return TypeParameters::MakeStringTypeParameters(type_parameters_proto);
  }
  if (param.IsSpecialLiteral() &&
      param.GetSpecialLiteral() == TypeParameterValue::kMaxLiteral) {
    type_parameters_proto.set_is_max_length(true);
    return TypeParameters::MakeStringTypeParameters(type_parameters_proto);
  }
  return MakeSqlError()
         << ShortTypeName(mode)
         << " length parameter must be an integer or MAX keyword";
}

absl::StatusOr<TypeParameters>
SimpleType::ResolveNumericBignumericTypeParameters(
    absl::Span<const TypeParameterValue> type_parameter_values,
    ProductMode mode) const {
  if (type_parameter_values.size() > 2) {
    return MakeSqlError() << ShortTypeName(mode)
                          << " type can only have 1 or 2 parameters. Found "
                          << type_parameter_values.size() << " parameters";
  }

  // For both NUMERIC and BIGNUMERIC, scale must be an integer.
  if (type_parameter_values.size() == 2 &&
      !type_parameter_values[1].GetValue().has_int64_value()) {
    return MakeSqlError() << ShortTypeName(mode) << " scale must be an integer";
  }
  int64_t scale = type_parameter_values.size() == 2
                      ? type_parameter_values[1].GetValue().int64_value()
                      : 0;

  // Validate value range for scale.
  NumericTypeParametersProto type_parameters_proto;
  const int max_scale =
      IsNumericType() ? kNumericMaxScale : kBigNumericMaxScale;
  if (scale < 0 || scale > max_scale) {
    return MakeSqlError() << absl::Substitute(
               "In $0(P, S), S must be between 0 and $1", ShortTypeName(mode),
               max_scale);
  }
  type_parameters_proto.set_scale(scale);

  // For NUMERIC, precision can only be an integer.
  // For BIGNUMERIC, precision can be an integer or MAX literal.
  const TypeParameterValue& precision_param = type_parameter_values[0];
  if (!precision_param.IsSpecialLiteral() &&
      precision_param.GetValue().has_int64_value()) {
    const int max_precision =
        IsNumericType() ? kNumericMaxPrecision : kBigNumericMaxPrecision;
    int64_t precision = type_parameter_values[0].GetValue().int64_value();
    if (precision < std::max(int64_t{1}, scale) ||
        precision > max_precision + scale) {
      if (type_parameter_values.size() == 1) {
        return MakeSqlError()
               << absl::Substitute("In $0(P), P must be between 1 and $1",
                                   ShortTypeName(mode), max_precision);
      }
      return MakeSqlError()
             << absl::Substitute("In $0(P, $1), P must be between $2 and $3",
                                 ShortTypeName(mode), scale,
                                 scale == 0 ? 1 : scale, max_precision + scale);
    }
    type_parameters_proto.set_precision(precision);
    return TypeParameters::MakeNumericTypeParameters(type_parameters_proto);
  }
  if (precision_param.IsSpecialLiteral() &&
      precision_param.GetSpecialLiteral() == TypeParameterValue::kMaxLiteral &&
      IsBigNumericType()) {
    type_parameters_proto.set_is_max_precision(true);
    return TypeParameters::MakeNumericTypeParameters(type_parameters_proto);
  }

  // Error out for invalid precision parameter input.
  if (IsNumericType()) {
    return MakeSqlError() << ShortTypeName(mode)
                          << " precision must be an integer";
  }
  return MakeSqlError() << ShortTypeName(mode)
                        << " precision must be an integer or MAX keyword";
}

static absl::Status MakeTimestampPrecisionError(absl::string_view type_name) {
  return MakeSqlError() << type_name << " precision must be 0, 3, 6, 9, or 12";
}

absl::StatusOr<TypeParameters> SimpleType::ResolveTimestampTypeParameters(
    absl::Span<const TypeParameterValue> type_parameter_values,
    ProductMode mode) const {
  if (type_parameter_values.size() != 1) {
    return MakeSqlError() << ShortTypeName(mode)
                          << " type can only have one parameter. Found "
                          << type_parameter_values.size() << " parameters";
  }

  TimestampTypeParametersProto type_parameters_proto;
  const TypeParameterValue& param = type_parameter_values[0];
  if (!param.GetValue().has_int64_value()) {
    return MakeTimestampPrecisionError(ShortTypeName(mode));
  }
  int64_t value = param.GetValue().int64_value();
  if (value < 0 || value > 12 || value % 3 != 0) {
    return MakeTimestampPrecisionError(ShortTypeName(mode));
  }

  type_parameters_proto.set_precision(value);
  return TypeParameters::MakeTimestampTypeParameters(type_parameters_proto);
}

absl::Status SimpleType::ValidateResolvedTypeParameters(
    const TypeParameters& type_parameters, ProductMode mode) const {
  if (type_parameters.IsEmpty()) {
    return absl::OkStatus();
  }
  if (IsString() || IsBytes()) {
    GOOGLESQL_RET_CHECK(type_parameters.IsStringTypeParameters());
    return TypeParameters::ValidateStringTypeParameters(
        type_parameters.string_type_parameters());
  }
  if (IsNumericType() || IsBigNumericType()) {
    GOOGLESQL_RET_CHECK(type_parameters.IsNumericTypeParameters());
    return ValidateNumericTypeParameters(
        type_parameters.numeric_type_parameters(), mode);
  }
  if (IsTimestamp()) {
    GOOGLESQL_RET_CHECK(type_parameters.IsTimestampTypeParameters());
    return TypeParameters::ValidateTimestampTypeParameters(
        type_parameters.timestamp_type_parameters());
  }
  GOOGLESQL_RET_CHECK_FAIL() << ShortTypeName(mode)
                   << " does not support type parameters";
}

absl::Status SimpleType::ValidateNumericTypeParameters(
    const NumericTypeParametersProto& numeric_param, ProductMode mode) const {
  // Validate value range for scale.
  int max_scale = IsNumericType() ? kNumericMaxScale : kBigNumericMaxScale;
  int64_t scale = numeric_param.scale();
  GOOGLESQL_RET_CHECK(scale >= 0 && scale <= max_scale) << absl::Substitute(
      "In $0(P, S), S must be between 0 and $1, actual scale: $2",
      ShortTypeName(mode), max_scale, scale);

  // Validate value range for precision.
  if (numeric_param.has_is_max_precision()) {
    GOOGLESQL_RET_CHECK(IsBigNumericType());
    GOOGLESQL_RET_CHECK(numeric_param.is_max_precision())
        << "is_max_precision should either be unset or true";
  } else {
    int64_t precision = numeric_param.precision();
    int max_precision =
        IsNumericType() ? kNumericMaxPrecision : kBigNumericMaxPrecision;
    GOOGLESQL_RET_CHECK(precision >= std::max(int64_t{1}, scale) &&
              precision <= max_precision + scale)
        << absl::Substitute(
               "In $0(P, $1), P must be between $2 and $3, actual "
               "precision: $4",
               ShortTypeName(mode), scale, scale == 0 ? 1 : scale,
               max_precision + scale, precision);
  }
  return absl::OkStatus();
}

}  // namespace googlesql
