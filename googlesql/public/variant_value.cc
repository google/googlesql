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

#include "googlesql/public/variant_value.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "googlesql/public/json_value.h"
#include "googlesql/public/proto/type_annotation.pb.h"
#include "googlesql/public/proto_util.h"
#include "googlesql/public/strings.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/proto_type.h"
#include "googlesql/public/value.h"
#include "absl/algorithm/container.h"
#include "absl/base/no_destructor.h"
#include "absl/base/nullability.h"
#include "absl/container/flat_hash_set.h"
#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "googlesql/base/status_macros.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"
#include "google/protobuf/descriptor.h"

namespace googlesql {

// Defined here so that it can be used by the tag structs in the unnamed
// namespace.
namespace internal {
VariantValueAdapter MakeVariantValueAdapterForJSON(Value root_value,
                                                   JSONValueConstRef json_ref) {
  return VariantValueAdapter(std::move(root_value), json_ref);
}

}  // namespace internal

namespace {

using VariantValueAdapter = internal::VariantValueAdapter;
using JSONNode = internal::VariantValueAdapter::JSONNode;

// This function is different from Variant::VariantNull(). It returns the null
// payload itself, rather than the null payload wrapped in a Variant.
const Value& VariantNullPayload() {
  // A Variant null is represented by a JSON 'null' scalar value which is what
  // the default constructor of JSONValue produces.
  static const absl::NoDestructor<Value> kVariantNullPayload(
      Value::Json(JSONValue()));
  return *kVariantNullPayload;
}

// Converts SQL NULL to Variant Null. If the value is not SQL NULL, returns the
// value as-is.
Value SqlNullToVariantNull(Value value) {
  return value.is_null() ? VariantNullPayload() : value;
}

// Adapts a child Value into a VariantValueAdapter, unwrapping TYPE_VARIANT to
// its underlying view and converting SQL NULL to Variant Null.
absl::StatusOr<VariantValueAdapter> AdaptChildValue(Value child_val) {
  Value val = SqlNullToVariantNull(std::move(child_val));
  if (val.is_valid() && !val.is_null() && val.type()->IsVariant()) {
    return val.variant_value();
  }
  return VariantValueAdapter::Create(std::move(val));
}

// Tag structs for supported object types in the Variant View Framework:
// - ObjectTagStruct: Dispatches object operations (GetKeys, ForMembers,
//                    LookupKey) on SQL STRUCTs.
// - ObjectTagProto:  Dispatches object operations on PROTO values.
// - ObjectTagMap:    Dispatches object operations on MAP values with string
//                    keys.
// - ObjectTagJSON:   Dispatches object operations on JSON objects.
//
// These tags are grouped into ObjectTagVariant and dispatched via std::visit to
// provide a single source of truth for object discrimination and uniform
// access.
// Each of these tags implement the following functions:
//
//   Returns all the keys of the given Object.
//   absl::StatusOr<std::vector<std::string>> GetKeys() const;
//
//   Invokes `fn` on each key-value pair (member) of the given Object. Only the
//   top-level Object is unpacked; nested Objects are passed as
//   VariantValueAdapter instances without recursive unpacking.
//   absl::Status ForMembers(
//       absl::FunctionRef<absl::Status(absl::string_view key,
//                                      VariantValueAdapter value)> fn) const;
//
//   Returns true if the key exists in the object, false otherwise.
//   If out_value is not null, populates *out_value with the value associated
//   with the key.
//   absl::StatusOr<bool> LookupKey(
//       absl::string_view key,
//       std::optional<VariantValueAdapter>* out_value) const;
//
// Note that every tag has its own semantics when implementing these
// functions. Those are explained in the comments of each tag.

struct ObjectTagStruct {
  const Value& val;

  // Returns the keys of a STRUCT value.
  // Unnamed fields are included as empty string keys.
  // If there are duplicate keys, only the first one is returned.
  absl::StatusOr<std::vector<std::string>> GetKeys() const {
    GOOGLESQL_RET_CHECK(val.type()->IsStruct());
    std::vector<std::string> keys;
    const StructType* struct_type = val.type()->AsStruct();
    // We reserve space for all fields, but the number of keys actually returned
    // may be smaller if there are duplicate keys.
    keys.reserve(struct_type->num_fields());
    absl::flat_hash_set<absl::string_view> seen_keys;
    seen_keys.reserve(struct_type->num_fields());
    for (int i = 0; i < struct_type->num_fields(); ++i) {
      const std::string& name = struct_type->field(i).name;
      if (seen_keys.insert(name).second) {
        keys.push_back(name);
      }
    }
    return keys;
  }

  // Invokes `fn` for each key-value pair (member) of a STRUCT value.
  // Unnamed fields are included with empty string keys.
  // If there are duplicate keys, only the first one is visited.
  // If a field value is SQL NULL, it is exposed as a Variant Null.
  absl::Status ForMembers(absl::FunctionRef<absl::Status(
                              absl::string_view key, VariantValueAdapter value)>
                              fn) const {
    GOOGLESQL_RET_CHECK(val.type()->IsStruct());
    const StructType* struct_type = val.type()->AsStruct();
    absl::flat_hash_set<absl::string_view> seen_keys;
    seen_keys.reserve(struct_type->num_fields());
    for (int i = 0; i < struct_type->num_fields(); ++i) {
      const std::string& name = struct_type->field(i).name;
      if (seen_keys.insert(name).second) {
        GOOGLESQL_ASSIGN_OR_RETURN(VariantValueAdapter member_val,
                         AdaptChildValue(val.field(i)));
        GOOGLESQL_RETURN_IF_ERROR(fn(name, std::move(member_val)));
      }
    }
    return absl::OkStatus();
  }

  // Looks up the member of a STRUCT value for the given key. Allows accessing
  // unnamed fields via empty string key. The names are matched case-sensitively
  // Returns the first match for duplicate keys.
  // If the value found against a key is SQL NULL, returns a Variant Null.
  absl::StatusOr<bool> LookupKey(
      absl::string_view key,
      std::optional<VariantValueAdapter>* /*absl_nullable*/ out_value) const {
    GOOGLESQL_RET_CHECK(val.type()->IsStruct());
    const StructType* struct_type = val.type()->AsStruct();
    for (int i = 0; i < struct_type->num_fields(); ++i) {
      if (struct_type->field(i).name == key) {
        if (out_value != nullptr) {
          GOOGLESQL_ASSIGN_OR_RETURN(*out_value, AdaptChildValue(val.field(i)));
        }
        return true;
      }
    }
    return false;
  }
};

struct ObjectTagProto {
  const Value& val;

  // Builds a ProtoFieldInfo for extracting `field` from a PROTO value.
  // Configures default value resolution using default options.
  // TODO: Support complex types (nested messages, enums) for
  // Proto.
  static absl::StatusOr<ProtoFieldInfo> MakeProtoFieldInfo(
      const google::protobuf::FieldDescriptor* field) {
    FieldFormat::Format format = ProtoType::GetFormatAnnotation(field);
    TypeKind kind;
    GOOGLESQL_RETURN_IF_ERROR(ProtoType::FieldDescriptorToTypeKindBase(field, &kind));

    const Type* gsql_type = nullptr;
    if (field->is_repeated()) {
      gsql_type = types::ArrayTypeFromSimpleTypeKind(kind);
    } else {
      gsql_type = types::TypeFromSimpleTypeKind(kind);
    }

    if (gsql_type == nullptr) {
      return absl::UnimplementedError(
          absl::StrCat("Cannot extract nested field '", field->name(),
                       "' of type '", field->type_name(), "' from Object"));
    }

    Value default_value;
    GOOGLESQL_RETURN_IF_ERROR(GetProtoFieldDefault(ProtoFieldDefaultOptions{}, field,
                                         gsql_type, &default_value));
    return ProtoFieldInfo{
        .descriptor = field,
        .format = format,
        .type = gsql_type,
        .get_has_bit = false,
        .default_value = std::move(default_value),
    };
  }

  // Returns the keys (field names) of a PROTO value.
  absl::StatusOr<std::vector<std::string>> GetKeys() const {
    GOOGLESQL_RET_CHECK(val.type()->IsProto());
    std::vector<std::string> keys;
    const google::protobuf::Descriptor* descriptor = val.type()->AsProto()->descriptor();
    keys.reserve(descriptor->field_count());
    for (int i = 0; i < descriptor->field_count(); ++i) {
      keys.push_back(std::string(descriptor->field(i)->name()));
    }
    return keys;
  }

  // Invokes `fn` for each key-value pair (member) of a PROTO value by decoding
  // all fields in a single pass over the serialized wire bytes.
  // TODO: b/554973153 - Support complex types (nested messages, enums) for
  // Proto.
  absl::Status ForMembers(absl::FunctionRef<absl::Status(
                              absl::string_view key, VariantValueAdapter value)>
                              fn) const {
    GOOGLESQL_RET_CHECK(val.type()->IsProto());
    const google::protobuf::Descriptor* descriptor = val.type()->AsProto()->descriptor();
    const int num_fields = descriptor->field_count();
    if (num_fields == 0) {
      return absl::OkStatus();
    }

    // Build ProtoFieldInfo metadata for all fields and collect pointers for
    // ReadProtoFields.
    std::vector<ProtoFieldInfo> field_infos;
    field_infos.reserve(num_fields);
    for (int i = 0; i < num_fields; ++i) {
      GOOGLESQL_ASSIGN_OR_RETURN(ProtoFieldInfo info,
                       MakeProtoFieldInfo(descriptor->field(i)));
      field_infos.push_back(std::move(info));
    }
    std::vector<const ProtoFieldInfo*> field_info_ptrs;
    field_info_ptrs.reserve(num_fields);
    for (const ProtoFieldInfo& info : field_infos) {
      field_info_ptrs.push_back(&info);
    }

    ProtoFieldValueList field_values;
    GOOGLESQL_RETURN_IF_ERROR(
        ReadProtoFields(field_info_ptrs, val.proto_value(), &field_values));
    GOOGLESQL_RET_CHECK_EQ(field_values.size(), num_fields);

    for (int i = 0; i < num_fields; ++i) {
      GOOGLESQL_ASSIGN_OR_RETURN(Value output_value, std::move(field_values[i]));
      GOOGLESQL_ASSIGN_OR_RETURN(VariantValueAdapter member_val,
                       AdaptChildValue(std::move(output_value)));
      GOOGLESQL_RETURN_IF_ERROR(fn(descriptor->field(i)->name(), std::move(member_val)));
    }
    return absl::OkStatus();
  }

  // Looks up the member of a PROTO value for the given key (field name). If
  // the field is not set on the wire, uses the default value according to
  // default options. If the resulting value is SQL NULL, exposes it as Variant
  // Null.
  // TODO: Support complex types (nested messages, enums) for
  // Proto.
  absl::StatusOr<bool> LookupKey(
      absl::string_view key,
      std::optional<VariantValueAdapter>* /*absl_nullable*/ out_value) const {
    GOOGLESQL_RET_CHECK(val.type()->IsProto());
    const google::protobuf::Descriptor* descriptor = val.type()->AsProto()->descriptor();
    const google::protobuf::FieldDescriptor* field = descriptor->FindFieldByName(key);
    if (field != nullptr) {
      if (out_value != nullptr) {
        GOOGLESQL_ASSIGN_OR_RETURN(ProtoFieldInfo info, MakeProtoFieldInfo(field));
        Value output_value;
        GOOGLESQL_RETURN_IF_ERROR(ReadProtoField(info.descriptor, info.format, info.type,
                                       info.default_value, val.proto_value(),
                                       &output_value));
        GOOGLESQL_ASSIGN_OR_RETURN(*out_value, AdaptChildValue(std::move(output_value)));
      }
      return true;
    }
    return false;
  }
};

struct ObjectTagMap {
  const Value& val;

  // Returns the keys of a MAP value. SQL NULL keys are not returned.
  absl::StatusOr<std::vector<std::string>> GetKeys() const {
    GOOGLESQL_RET_CHECK(val.type()->IsMap() &&
              val.type()->AsMap()->key_type()->IsString());
    std::vector<std::string> keys;
    keys.reserve(val.map_entries().size());
    for (const auto& entry : val.map_entries()) {
      if (!entry.first.is_null()) {
        keys.push_back(entry.first.string_value());
      }
    }
    return keys;
  }

  // Invokes `fn` for each key-value pair (member) of a MAP value. SQL NULL
  // keys are omitted, and SQL NULL values are exposed as Variant Null.
  absl::Status ForMembers(absl::FunctionRef<absl::Status(
                              absl::string_view key, VariantValueAdapter value)>
                              fn) const {
    GOOGLESQL_RET_CHECK(val.type()->IsMap() &&
              val.type()->AsMap()->key_type()->IsString());
    for (const auto& [key, value] : val.map_entries()) {
      if (!key.is_null()) {
        GOOGLESQL_ASSIGN_OR_RETURN(VariantValueAdapter member_val,
                         AdaptChildValue(value));
        GOOGLESQL_RETURN_IF_ERROR(fn(key.string_value(), std::move(member_val)));
      }
    }
    return absl::OkStatus();
  }

  // Returns the member of a MAP value for the given key. SQL NULL values are
  // returned as Variant Null.
  absl::StatusOr<bool> LookupKey(
      absl::string_view key,
      std::optional<VariantValueAdapter>* /*absl_nullable*/ out_value) const {
    GOOGLESQL_RET_CHECK(val.type()->IsMap() &&
              val.type()->AsMap()->key_type()->IsString());
    auto map = val.map_entries();
    auto it = map.find(Value::String(key));
    if (it != map.end()) {
      if (out_value != nullptr) {
        GOOGLESQL_ASSIGN_OR_RETURN(*out_value, AdaptChildValue(it->second));
      }
      return true;
    }
    return false;
  }
};

struct ObjectTagJSON {
  const JSONNode& json_node;

  // Returns the keys of a JSON value.
  absl::StatusOr<std::vector<std::string>> GetKeys() const {
    std::vector<std::string> keys;
    keys.reserve(json_node.ref.GetObjectSize());
    for (const auto& [key, value] : json_node.ref.GetMembers()) {
      keys.push_back(std::string(key));
    }
    return keys;
  }

  // Invokes `fn` for each key-value pair (member) of a JSON value.
  absl::Status ForMembers(absl::FunctionRef<absl::Status(
                              absl::string_view key, VariantValueAdapter value)>
                              fn) const {
    for (const auto& [key, member_val] : json_node.ref.GetMembers()) {
      GOOGLESQL_RETURN_IF_ERROR(fn(key, internal::MakeVariantValueAdapterForJSON(
                                  json_node.root_value, member_val)));
    }
    return absl::OkStatus();
  }

  // Returns the member of a JSON value for the given key.
  absl::StatusOr<bool> LookupKey(
      absl::string_view key,
      std::optional<VariantValueAdapter>* /*absl_nullable*/ out_value) const {
    std::optional<JSONValueConstRef> member =
        json_node.ref.GetMemberIfExists(key);
    if (member.has_value()) {
      if (out_value != nullptr) {
        *out_value = internal::MakeVariantValueAdapterForJSON(
            json_node.root_value, member.value());
      }
      return true;
    }
    return false;
  }
};

// Tag structs for supported array types in the Variant View Framework:
// - ArrayTagSql:  Dispatches array operations on SQL ARRAYs.
// - ArrayTagJSON: Dispatches array operations on JSON arrays.
//
// These tags are grouped into ArrayTagVariant and dispatched via std::visit to
// provide a single source of truth for array discrimination and uniform
// access.
// Each of these tags implement the following functions:
//
//   Returns the size of the given Array.
//   absl::StatusOr<int64_t> GetSize() const;
//
//   Returns all elements of the given Array. Only the top-level elements are
//   unpacked; nested Arrays or Objects are returned as VariantValueAdapter
//   instances without recursive unpacking.
//   absl::StatusOr<std::vector<VariantValueAdapter>> GetElements() const;
//
//   Returns the element at the given index in the Array.
//   Returns std::nullopt if the index is out of bounds.
//   absl::StatusOr<std::optional<VariantValueAdapter>> GetElement(
//       int64_t index) const;

struct ArrayTagSql {
  const Value& val;

  int64_t GetSize() const { return val.num_elements(); }

  // Returns all elements in the Array.
  // If an element is SQL NULL, it is exposed as a Variant Null.
  absl::StatusOr<std::vector<VariantValueAdapter>> GetElements() const {
    GOOGLESQL_RET_CHECK(val.type()->IsArray());
    std::vector<VariantValueAdapter> elements;
    elements.reserve(val.num_elements());
    for (int i = 0; i < val.num_elements(); ++i) {
      GOOGLESQL_ASSIGN_OR_RETURN(VariantValueAdapter element,
                       AdaptChildValue(val.element(i)));
      elements.push_back(std::move(element));
    }
    return elements;
  }

  // Returns the element at the given index in the Array.
  // If the value found at the index is SQL NULL, returns a Variant Null.
  absl::StatusOr<std::optional<VariantValueAdapter>> GetElement(
      int64_t index) const {
    if (index < 0 || index >= val.num_elements()) {
      return std::nullopt;
    }
    GOOGLESQL_ASSIGN_OR_RETURN(VariantValueAdapter element,
                     AdaptChildValue(val.element(static_cast<int>(index))));
    return element;
  }
};

struct ArrayTagJSON {
  const JSONNode& json_node;

  // Returns the size of the JSON array.
  int64_t GetSize() const {
    return static_cast<int64_t>(json_node.ref.GetArraySize());
  }

  // Returns all elements in the JSON array.
  absl::StatusOr<std::vector<VariantValueAdapter>> GetElements() const {
    std::vector<VariantValueAdapter> elements;
    elements.reserve(json_node.ref.GetArraySize());
    for (size_t i = 0; i < json_node.ref.GetArraySize(); ++i) {
      elements.emplace_back(internal::MakeVariantValueAdapterForJSON(
          json_node.root_value, json_node.ref.GetArrayElement(i)));
    }
    return elements;
  }

  // Returns the element at the given index in the JSON array.
  absl::StatusOr<std::optional<VariantValueAdapter>> GetElement(
      int64_t index) const {
    if (index < 0 || index >= json_node.ref.GetArraySize()) {
      return std::nullopt;
    }
    return internal::MakeVariantValueAdapterForJSON(
        json_node.root_value,
        json_node.ref.GetArrayElement(static_cast<size_t>(index)));
  }
};

using ArrayTagVariant = std::variant<ArrayTagSql, ArrayTagJSON>;
using ObjectTagVariant =
    std::variant<ObjectTagStruct, ObjectTagProto, ObjectTagMap, ObjectTagJSON>;

absl::StatusOr<ObjectTagVariant> GetObjectTag(
    const std::variant<Value, JSONNode>& adapter_rep) {
  if (const JSONNode* json_node = std::get_if<JSONNode>(&adapter_rep);
      json_node != nullptr) {
    if (json_node->ref.IsObject()) {
      return ObjectTagJSON{*json_node};
    }
  }

  if (const Value* val = std::get_if<Value>(&adapter_rep); val != nullptr) {
    GOOGLESQL_RET_CHECK(val->is_valid());
    if (!val->is_null()) {
      // Valid non-null JSON values are always stored in the JSONNode arm of
      // adapter_rep. A non-null JSON value in the Value arm is an unparsed JSON
      // string that failed to parse in the VariantValueAdapter constructor.
      // Re-run ParseJSONString on this error path to return the parse error.
      if (val->type()->IsJson()) {
        return JSONValue::ParseJSONString(val->json_value_unparsed()).status();
      }
      if (val->type()->IsStruct()) {
        return ObjectTagStruct{*val};
      }
      if (val->type()->IsProto()) {
        return ObjectTagProto{*val};
      }
      if (val->type()->IsMap()) {
        if (val->type()->AsMap()->key_type()->IsString()) {
          return ObjectTagMap{*val};
        }
      }
    }
  }

  return absl::InvalidArgumentError("Not an object");
}

absl::StatusOr<ArrayTagVariant> GetArrayTag(
    const std::variant<Value, JSONNode>& adapter_rep) {
  if (const JSONNode* json_node = std::get_if<JSONNode>(&adapter_rep);
      json_node != nullptr) {
    if (json_node->ref.IsArray()) {
      return ArrayTagJSON{*json_node};
    }
  }

  if (const Value* val = std::get_if<Value>(&adapter_rep); val != nullptr) {
    GOOGLESQL_RET_CHECK(val->is_valid());
    if (!val->is_null()) {
      // Valid non-null JSON values are always stored in the JSONNode arm of
      // adapter_rep. A non-null JSON value in the Value arm is an unparsed JSON
      // string that failed to parse in the VariantValueAdapter constructor.
      // Re-run ParseJSONString on this error path to return the parse error.
      if (val->type()->IsJson()) {
        return JSONValue::ParseJSONString(val->json_value_unparsed()).status();
      }
      if (val->type()->IsArray()) {
        return ArrayTagSql{*val};
      }
    }
  }

  return absl::InvalidArgumentError("Not an array");
}

}  // namespace

namespace internal {

// --- VariantValueAdapter ---

absl::StatusOr<VariantValueAdapter> VariantValueAdapter::Create(Value value) {
  GOOGLESQL_RET_CHECK(!value.is_valid() || value.is_null() || !value.type()->IsVariant())
      << "Cannot construct VariantValueAdapter from a valid, non-null "
         "TYPE_VARIANT Value. Use value.variant_value() instead";
  return VariantValueAdapter(std::move(value));
}

VariantValueAdapter::VariantValueAdapter(Value value) {
  if (value.is_valid() && !value.is_null() && value.type()->IsJson()) {
    if (value.is_validated_json()) {
      JSONValueConstRef json_ref = value.json_value();
      rep_ = JSONNode{std::move(value), json_ref};
      return;
    }

    absl::StatusOr<JSONValue> parsed_json =
        JSONValue::ParseJSONString(value.json_value_unparsed());
    if (parsed_json.ok()) {
      Value root_node = Value::Json(std::move(*parsed_json));
      JSONValueConstRef json_ref = root_node.json_value();
      rep_ = JSONNode{std::move(root_node), json_ref};
      return;
    }
    // Note that if the JSON string could not be parsed, it is stored in
    // the Value arm of rep_.
  }

  rep_ = std::move(value);
}

bool VariantValueAdapter::is_valid() const {
  if (const Value* val = std::get_if<Value>(&rep_); val != nullptr) {
    return val->is_valid();
  }
  return true;
}

bool VariantValueAdapter::is_null() const {
  if (const Value* val = std::get_if<Value>(&rep_); val != nullptr) {
    return val->is_valid() && val->is_null();
  }
  return false;
}

bool VariantValueAdapter::IsPrimitive() const {
  if (const JSONNode* json_node = std::get_if<JSONNode>(&rep_);
      json_node != nullptr) {
    return json_node->ref.IsNumber() || json_node->ref.IsString() ||
           json_node->ref.IsBoolean() || json_node->ref.IsNull();
  }
  if (const Value* val = std::get_if<Value>(&rep_); val != nullptr) {
    return val->is_valid() && !val->is_null() && !val->type()->IsJson() &&
           ((val->type()->IsSimpleType() && !val->type()->IsVariant()) ||
            val->type()->IsEnum());
  }
  return false;
}

bool VariantValueAdapter::IsObject() const {
  if (const JSONNode* json_node = std::get_if<JSONNode>(&rep_);
      json_node != nullptr) {
    return json_node->ref.IsObject();
  }
  if (const Value* val = std::get_if<Value>(&rep_); val != nullptr) {
    if (!val->is_valid() || val->is_null()) return false;
    if (val->type()->IsMap()) {
      return val->type()->AsMap()->key_type()->IsString();
    }
    return val->type()->IsStruct() || val->type()->IsProto();
  }
  return false;
}

bool VariantValueAdapter::IsArray() const {
  if (const JSONNode* json_node = std::get_if<JSONNode>(&rep_);
      json_node != nullptr) {
    return json_node->ref.IsArray();
  }
  if (const Value* val = std::get_if<Value>(&rep_); val != nullptr) {
    return val->is_valid() && !val->is_null() && val->type()->IsArray();
  }
  return false;
}

bool VariantValueAdapter::IsVariantNull() const {
  if (const JSONNode* json_node = std::get_if<JSONNode>(&rep_);
      json_node != nullptr) {
    return json_node->ref.IsNull();
  }
  return false;
}

absl::StatusOr<Value> VariantValueAdapter::GetPrimitiveValue() const {
  if (const JSONNode* json_node = std::get_if<JSONNode>(&rep_);
      json_node != nullptr) {
    const JSONValueConstRef& json_ref = json_node->ref;
    if (json_ref.IsNull()) return VariantNullPayload();
    if (json_ref.IsBoolean()) return Value::Bool(json_ref.GetBoolean());
    if (json_ref.IsInt64()) return Value::Int64(json_ref.GetInt64());
    if (json_ref.IsUInt64()) return Value::Uint64(json_ref.GetUInt64());
    if (json_ref.IsDouble()) return Value::Double(json_ref.GetDouble());
    if (json_ref.IsString()) {
      return Value::String(json_ref.GetStringRef());
    }
  }
  if (const Value* val = std::get_if<Value>(&rep_); val != nullptr) {
    if (val->is_valid() && !val->is_null() && !val->type()->IsJson() &&
        ((val->type()->IsSimpleType() && !val->type()->IsVariant()) ||
         val->type()->IsEnum())) {
      return *val;
    }
  }

  return absl::InvalidArgumentError("Value is not a primitive");
}

const Type* VariantValueAdapter::GetUnprojectedType() const {
  if (std::holds_alternative<JSONNode>(rep_)) {
    return types::JsonType();
  }
  const Value* val = std::get_if<Value>(&rep_);
  if (val != nullptr && val->is_valid()) {
    return val->type();
  }
  return nullptr;
}

absl::StatusOr<Value> VariantValueAdapter::GetUnprojectedValue() const {
  GOOGLESQL_RET_CHECK(is_valid());
  if (const JSONNode* json_node = std::get_if<JSONNode>(&rep_);
      json_node != nullptr) {
    return Value::Json(JSONValue::CopyFrom(json_node->ref));
  }
  const Value* val = std::get_if<Value>(&rep_);
  GOOGLESQL_RET_CHECK(val != nullptr);
  if (!val->is_null() && val->type()->IsJson()) {
    GOOGLESQL_ASSIGN_OR_RETURN(JSONValue parsed_json,
                     JSONValue::ParseJSONString(val->json_value_unparsed()));
    return Value::Json(std::move(parsed_json));
  }
  return *val;
}

absl::StatusOr<std::vector<std::string>> VariantValueAdapter::GetKeys() const {
  GOOGLESQL_ASSIGN_OR_RETURN(ObjectTagVariant tag, GetObjectTag(rep_));
  return std::visit([](const auto& tag_val) { return tag_val.GetKeys(); }, tag);
}

absl::StatusOr<std::vector<std::pair<std::string, VariantValueAdapter>>>
VariantValueAdapter::GetMembers() const {
  std::vector<std::pair<std::string, VariantValueAdapter>> members;
  GOOGLESQL_RETURN_IF_ERROR(
      ForMembers([&members](absl::string_view key, VariantValueAdapter value) {
        members.emplace_back(std::string(key), std::move(value));
        return absl::OkStatus();
      }));
  return members;
}

absl::Status VariantValueAdapter::ForMembers(
    absl::FunctionRef<absl::Status(absl::string_view key,
                                   VariantValueAdapter value)>
        fn) const {
  GOOGLESQL_ASSIGN_OR_RETURN(ObjectTagVariant tag, GetObjectTag(rep_));
  return std::visit(
      [fn](const auto& tag_val) { return tag_val.ForMembers(fn); }, tag);
}

bool VariantValueAdapter::HasKey(absl::string_view key) const {
  absl::StatusOr<ObjectTagVariant> tag = GetObjectTag(rep_);
  if (!tag.ok()) return false;
  return std::visit(
      [key](const auto& tag_val) {
        absl::StatusOr<bool> exists_ok = tag_val.LookupKey(key, nullptr);
        return exists_ok.ok() && *exists_ok;
      },
      *tag);
}

absl::StatusOr<std::optional<VariantValueAdapter>>
VariantValueAdapter::GetKeyValue(absl::string_view key) const {
  GOOGLESQL_ASSIGN_OR_RETURN(ObjectTagVariant tag, GetObjectTag(rep_));
  return std::visit(
      [key](const auto& tag_val)
          -> absl::StatusOr<std::optional<VariantValueAdapter>> {
        std::optional<VariantValueAdapter> value;
        GOOGLESQL_ASSIGN_OR_RETURN(bool exists, tag_val.LookupKey(key, &value));
        return exists ? value : std::nullopt;
      },
      tag);
}

absl::StatusOr<VariantValueAdapter> VariantValueAdapter::GetKeyValueIfExists(
    absl::string_view key) const {
  GOOGLESQL_ASSIGN_OR_RETURN(std::optional<VariantValueAdapter> val, GetKeyValue(key));
  if (!val.has_value()) {
    return absl::NotFoundError(absl::StrCat("Key not found: ", key));
  }
  return *std::move(val);
}

absl::StatusOr<int64_t> VariantValueAdapter::GetArraySize() const {
  GOOGLESQL_RET_CHECK(is_valid());
  GOOGLESQL_ASSIGN_OR_RETURN(ArrayTagVariant tag, GetArrayTag(rep_));
  return std::visit([](const auto& tag_val) { return tag_val.GetSize(); }, tag);
}

absl::StatusOr<std::vector<VariantValueAdapter>>
VariantValueAdapter::GetElements() const {
  GOOGLESQL_RET_CHECK(is_valid());
  GOOGLESQL_ASSIGN_OR_RETURN(ArrayTagVariant tag, GetArrayTag(rep_));
  return std::visit([](const auto& tag_val) { return tag_val.GetElements(); },
                    tag);
}

absl::StatusOr<std::optional<VariantValueAdapter>>
VariantValueAdapter::GetElement(int64_t index) const {
  GOOGLESQL_RET_CHECK(is_valid());
  GOOGLESQL_ASSIGN_OR_RETURN(ArrayTagVariant tag, GetArrayTag(rep_));
  return std::visit(
      [index](const auto& tag_val) { return tag_val.GetElement(index); }, tag);
}

absl::StatusOr<VariantValueAdapter> VariantValueAdapter::GetElementIfExists(
    int64_t index) const {
  GOOGLESQL_ASSIGN_OR_RETURN(std::optional<VariantValueAdapter> val, GetElement(index));
  if (!val.has_value()) {
    return absl::OutOfRangeError(absl::StrCat("Index out of bounds: ", index));
  }
  return *val;
}

std::string VariantValueAdapter::DebugString() const {
  if (!is_valid() || is_null()) {
    if (const Value* val = std::get_if<Value>(&rep_); val != nullptr) {
      return val->DebugString();
    }
    // Should never reach here.
    return "ERROR";
  }

  if (absl::StatusOr<Value> primitive_val = GetPrimitiveValue();
      primitive_val.ok()) {
    return primitive_val->DebugString();
  }
  if (absl::StatusOr<std::vector<std::pair<std::string, VariantValueAdapter>>>
          members = GetMembers();
      members.ok()) {
    absl::c_sort(*members, [](const auto& a, const auto& b) {
      return a.first < b.first;
    });
    return absl::StrCat("{",
                        absl::StrJoin(*members, ", ",
                                      [](std::string* out, const auto& entry) {
                                        absl::StrAppend(
                                            out, ToStringLiteral(entry.first),
                                            ": ", entry.second.DebugString());
                                      }),
                        "}");
  }
  if (absl::StatusOr<std::vector<VariantValueAdapter>> elements = GetElements();
      elements.ok()) {
    return absl::StrCat("[",
                        absl::StrJoin(*elements, ", ",
                                      [](std::string* out, const auto& elem) {
                                        absl::StrAppend(out,
                                                        elem.DebugString());
                                      }),
                        "]");
  }

  if (const Value* val = std::get_if<Value>(&rep_); val != nullptr) {
    return val->DebugString();
  }
  // Should never reach here.
  return "ERROR";
}

absl::StatusOr<Value> VariantValueAdapter::ToVariantValue() const {
  GOOGLESQL_RET_CHECK(is_valid());
  if (IsVariantNull()) {
    return Value::VariantNull();
  }
  if (const JSONNode* json_node = std::get_if<JSONNode>(&rep_);
      json_node != nullptr) {
    return Value::Variant(Value::Json(JSONValue::CopyFrom(json_node->ref)));
  }
  if (const Value* val = std::get_if<Value>(&rep_); val != nullptr) {
    return Value::Variant(*val);
  }
  GOOGLESQL_RET_CHECK_FAIL() << "Unreachable VariantValueAdapter state";
}

}  // namespace internal

}  // namespace googlesql
