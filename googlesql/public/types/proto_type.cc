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

#include "googlesql/public/types/proto_type.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "googlesql/base/logging.h"
#include "google/protobuf/util/field_comparator.h"
#include "google/protobuf/util/message_differencer.h"
#include "googlesql/common/errors.h"
#include "googlesql/common/float_margin.h"
#include "googlesql/public/functions/convert_proto.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/proto/type_annotation.pb.h"
#include "googlesql/public/proto/wire_format_annotation.pb.h"
#include "googlesql/public/strings.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/internal_proto_utils.h"
#include "googlesql/public/types/internal_utils.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/types/value_representations.h"
#include "googlesql/public/value.pb.h"
#include "googlesql/public/value_content.h"
#include "absl/algorithm/container.h"
#include "absl/base/attributes.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/inlined_vector.h"
#include "absl/hash/hash.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/cord.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "google/protobuf/dynamic_message.h"
#include "google/protobuf/message.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

ProtoType::ProtoType(const TypeFactory* factory,
                     const google::protobuf::Descriptor* descriptor,
                     const internal::CatalogName* catalog_name)
    : Type(factory, TYPE_PROTO),
      descriptor_(descriptor),
      catalog_name_(catalog_name) {
  ABSL_CHECK(descriptor_ != nullptr);
}

ProtoType::~ProtoType() = default;

bool ProtoType::IsSupportedType(const LanguageOptions& language_options) const {
  return language_options.SupportsProtoTypes();
}

bool ProtoType::EqualsForSameKind(const Type* that, bool equivalent) const {
  const ProtoType* other = that->AsProto();
  ABSL_DCHECK(other);
  return ProtoType::EqualsImpl(this, other, equivalent);
}

void ProtoType::DebugStringImpl(bool details, TypeOrStringVector* stack,
                                std::string* debug_string) const {
  if (catalog_name_ != nullptr) {
    absl::StrAppend(debug_string, *catalog_name_->path_string, ".");
  }

  absl::StrAppend(debug_string, "PROTO<", descriptor_->full_name());
  if (details) {
    absl::StrAppend(debug_string, ", file name: ", descriptor_->file()->name(),
                    ", <", descriptor_->DebugString(), ">");
  }
  absl::StrAppend(debug_string, ">");
}

bool ProtoType::SupportsOrdering(const LanguageOptions& language_options,
                                 std::string* type_description) const {
  if (type_description != nullptr) {
    *type_description =
        TypeKindToString(this->kind(), language_options.product_mode());
  }
  return false;
}

static bool HasFloatingPointFields(
    const google::protobuf::Descriptor* d,
    absl::flat_hash_set<const google::protobuf::Descriptor*>& visited) {
  bool is_new = visited.insert(d).second;
  if (!is_new) {
    return false;
  }
  for (int i = 0; i < d->field_count(); ++i) {
    const google::protobuf::FieldDescriptor* f = d->field(i);
    if (f->type() == google::protobuf::FieldDescriptor::TYPE_FLOAT ||
        f->type() == google::protobuf::FieldDescriptor::TYPE_DOUBLE) {
      return true;
    } else if (f->type() == google::protobuf::FieldDescriptor::TYPE_MESSAGE &&
               HasFloatingPointFields(f->message_type(), visited)) {
      return true;
    }
  }
  return false;
}

bool ProtoType::HasFloatingPointFields() const {
  absl::flat_hash_set<const google::protobuf::Descriptor*> visited;
  return googlesql::HasFloatingPointFields(descriptor(), visited);
}

const google::protobuf::Descriptor* ProtoType::descriptor() const {
  return descriptor_;
}

const google::protobuf::FieldDescriptor* ProtoType::map_key() const {
  return descriptor()->map_key();
}

const google::protobuf::FieldDescriptor* ProtoType::map_value() const {
  return descriptor()->map_value();
}

absl::Status ProtoType::SerializeToProtoAndDistinctFileDescriptorsImpl(
    const BuildFileDescriptorMapOptions& options, TypeProto* type_proto,
    FileDescriptorSetMap* file_descriptor_set_map) const {
  type_proto->set_type_kind(kind());
  ProtoTypeProto* proto_type_proto = type_proto->mutable_proto_type();
  proto_type_proto->set_proto_name(descriptor_->full_name());
  proto_type_proto->set_proto_file_name(descriptor_->file()->name());
  // Note that right now we are not supporting extensions to TypeProto, so
  // we do not need to look for all extensions of this proto.  Therefore the
  // FileDescriptorSet can be derived from the descriptor's FileDescriptor
  // dependencies.
  int set_index;
  GOOGLESQL_RETURN_IF_ERROR(internal::PopulateDistinctFileDescriptorSets(
      options, descriptor_->file(), file_descriptor_set_map, &set_index));
  if (set_index != 0) {
    proto_type_proto->set_file_descriptor_set_index(set_index);
  }

  if (catalog_name_ != nullptr) {
    absl::c_copy(catalog_name_->path,
                 google::protobuf::RepeatedFieldBackInserter(
                     proto_type_proto->mutable_catalog_name_path()));
  }

  return absl::OkStatus();
}

absl::Status ProtoType::GetFieldTypeByTagNumber(int number,
                                                TypeFactory* factory,
                                                bool use_obsolete_timestamp,
                                                const Type** type,
                                                std::string* name) const {
  const google::protobuf::FieldDescriptor* field_descr =
      descriptor_->FindFieldByNumber(number);
  if (field_descr == nullptr) {
    return MakeSqlError() << "Field number " << number
                          << " not found in descriptor "
                          << descriptor_->full_name();
  }
  if (name != nullptr) {
    *name = field_descr->name();
  }
  return factory->GetProtoFieldType(field_descr, use_obsolete_timestamp,
                                    CatalogNamePath(), type);
}

absl::Status ProtoType::GetFieldTypeByName(absl::string_view name,
                                           TypeFactory* factory,
                                           bool use_obsolete_timestamp,
                                           const Type** type,
                                           int* number) const {
  const google::protobuf::FieldDescriptor* field_descr =
      descriptor_->FindFieldByName(name);
  if (field_descr == nullptr) {
    return MakeSqlError()
           << "Field name " << name << " not found in descriptor "
           << descriptor_->full_name();
  }
  if (number != nullptr) {
    *number = field_descr->number();
  }
  return factory->GetProtoFieldType(field_descr, use_obsolete_timestamp,
                                    CatalogNamePath(), type);
}

std::string ProtoType::TypeName() const {
  std::string catalog_name_path;
  if (catalog_name_ != nullptr) {
    absl::StrAppend(&catalog_name_path, *catalog_name_->path_string, ".");
  }

  absl::StrAppend(&catalog_name_path,
                  ToIdentifierLiteral(descriptor_->full_name()));

  return catalog_name_path;
}

std::string ProtoType::ShortTypeName() const {
  std::string catalog_name_path;
  if (catalog_name_ != nullptr) {
    absl::StrAppend(&catalog_name_path, *catalog_name_->path_string, ".");
  }

  return absl::StrCat(catalog_name_path, descriptor_->full_name());
}

std::string ProtoType::TypeName(ProductMode mode_unused) const {
  return TypeName();
}

std::string ProtoType::CapitalizedName() const {
  ABSL_CHECK_EQ(kind(), TYPE_PROTO);  // Crash OK
  ABSL_CHECK(AsProto()->descriptor() != nullptr);
  return absl::StrCat("Proto<", AsProto()->descriptor()->full_name(), ">");
}

absl::Span<const std::string> ProtoType::CatalogNamePath() const {
  if (catalog_name_ == nullptr) {
    return {};
  } else {
    return catalog_name_->path;
  }
}

// static
absl::Status ProtoType::GetTypeKindFromFieldDescriptor(
    const google::protobuf::FieldDescriptor* field, bool ignore_format_annotations,
    bool use_obsolete_timestamp, TypeKind* kind) {
  const google::protobuf::FieldDescriptor::Type field_type = field->type();
  if (!ignore_format_annotations) {
    GOOGLESQL_RETURN_IF_ERROR(ProtoType::ValidateTypeAnnotations(field));
  }
  const FieldFormat::Format format =
      ignore_format_annotations ? FieldFormat::DEFAULT_FORMAT
                                : ProtoType::GetFormatAnnotation(field);
  switch (field_type) {
    case google::protobuf::FieldDescriptor::TYPE_INT32:
    case google::protobuf::FieldDescriptor::TYPE_SFIXED32:
    case google::protobuf::FieldDescriptor::TYPE_SINT32: {
      switch (format) {
        case FieldFormat::DEFAULT_FORMAT:
          *kind = TYPE_INT32;
          break;
        case FieldFormat::DATE:
        case FieldFormat::DATE_DECIMAL:
          *kind = TYPE_DATE;
          break;
        default:
          // Should not reach this if ValidateTypeAnnotations() is working
          // properly.
          return MakeSqlError()
                 << "Proto " << field->containing_type()->full_name()
                 << " has invalid googlesql.format for INT32 field: "
                 << field->DebugString();
      }
      break;
    }
    case google::protobuf::FieldDescriptor::TYPE_INT64:
    case google::protobuf::FieldDescriptor::TYPE_SFIXED64:
    case google::protobuf::FieldDescriptor::TYPE_SINT64: {
      switch (format) {
        case FieldFormat::DEFAULT_FORMAT:
          *kind = TYPE_INT64;
          break;
        case FieldFormat::DATE:
        case FieldFormat::DATE_DECIMAL:
          *kind = TYPE_DATE;
          break;
        case FieldFormat::TIMESTAMP_SECONDS:
          if (use_obsolete_timestamp) {
          } else {
            *kind = TYPE_TIMESTAMP;
          }
          break;
        case FieldFormat::TIMESTAMP_MILLIS:
          if (use_obsolete_timestamp) {
          } else {
            *kind = TYPE_TIMESTAMP;
          }
          break;
        case FieldFormat::TIMESTAMP_MICROS:
          if (use_obsolete_timestamp) {
          } else {
            *kind = TYPE_TIMESTAMP;
          }
          break;
        case FieldFormat::TIMESTAMP_NANOS:
          *kind = TYPE_TIMESTAMP;
          break;
        case FieldFormat::TIME_MICROS:
          *kind = TYPE_TIME;
          break;
        case FieldFormat::DATETIME_MICROS:
          *kind = TYPE_DATETIME;
          break;
        default:
          // Should not reach this if ValidateTypeAnnotations() is working
          // properly.
          return MakeSqlError()
                 << "Proto " << field->containing_type()->full_name()
                 << " has invalid googlesql.format for INT64 field: "
                 << field->DebugString();
      }
      break;
    }
    case google::protobuf::FieldDescriptor::TYPE_FIXED32:
    case google::protobuf::FieldDescriptor::TYPE_UINT32:
      *kind = TYPE_UINT32;
      break;
    case google::protobuf::FieldDescriptor::TYPE_FIXED64:
      *kind = TYPE_UINT64;
      break;
    case google::protobuf::FieldDescriptor::TYPE_UINT64: {
      switch (format) {
        case FieldFormat::DEFAULT_FORMAT:
          *kind = TYPE_UINT64;
          break;
        case FieldFormat::TIMESTAMP_MICROS:
          if (use_obsolete_timestamp) {
          } else {
            *kind = TYPE_TIMESTAMP;
          }
          break;
        default:
          // Should not reach this if ValidateTypeAnnotations() is working
          // properly.
          return MakeSqlError()
                 << "Proto " << field->containing_type()->full_name()
                 << " has invalid googlesql.format for UINT64 field: "
                 << field->DebugString();
      }
      break;
    }
    case google::protobuf::FieldDescriptor::TYPE_BYTES: {
      switch (format) {
        case FieldFormat::DEFAULT_FORMAT:
          *kind = TYPE_BYTES;
          break;
        case FieldFormat::TIMESTAMP:
          *kind = TYPE_TIMESTAMP;
          break;
        case FieldFormat::ST_GEOGRAPHY_ENCODED:
          *kind = TYPE_GEOGRAPHY;
          break;
        case FieldFormat::NUMERIC:
          *kind = TYPE_NUMERIC;
          break;
        case FieldFormat::BIGNUMERIC:
          *kind = TYPE_BIGNUMERIC;
          break;
        case FieldFormat::INTERVAL:
          *kind = TYPE_INTERVAL;
          break;
        case FieldFormat::TOKENLIST:
          *kind = TYPE_TOKENLIST;
          break;
        case FieldFormat::UUID:
          *kind = TYPE_UUID;
          break;
        case FieldFormat::RANGE_DATES_ENCODED:
        case FieldFormat::RANGE_DATETIMES_ENCODED:
        case FieldFormat::RANGE_TIMESTAMPS_ENCODED:
          *kind = TYPE_RANGE;
          break;
        default:
          // Should not reach this if ValidateTypeAnnotations() is working
          // properly.
          return MakeSqlError()
                 << "Proto " << field->containing_type()->full_name()
                 << " has invalid googlesql.format for BYTES field: "
                 << field->DebugString();
      }
      break;
    }
    case google::protobuf::FieldDescriptor::TYPE_DOUBLE:
      *kind = TYPE_DOUBLE;
      break;
    case google::protobuf::FieldDescriptor::TYPE_FLOAT:
      *kind = TYPE_FLOAT;
      break;
    case google::protobuf::FieldDescriptor::TYPE_BOOL:
      *kind = TYPE_BOOL;
      break;
    case google::protobuf::FieldDescriptor::TYPE_ENUM:
      *kind = TYPE_ENUM;
      break;
    case google::protobuf::FieldDescriptor::TYPE_STRING: {
      switch (format) {
        case FieldFormat::DEFAULT_FORMAT:
          *kind = TYPE_STRING;
          break;
        case FieldFormat::JSON:
          *kind = TYPE_JSON;
          break;
        default:
          // Should not reach this if ValidateTypeAnnotations() is working
          // properly.
          return MakeSqlError()
                 << "Proto " << field->containing_type()->full_name()
                 << " has invalid googlesql.format for STRING field: "
                 << field->DebugString();
      }
      break;
    }
    case google::protobuf::FieldDescriptor::TYPE_MESSAGE:
      if (ignore_format_annotations) {
        *kind = TYPE_PROTO;
        break;
      }
      ABSL_FALLTHROUGH_INTENDED;
    case google::protobuf::FieldDescriptor::TYPE_GROUP:
      // NOTE: We are not unwrapping is_wrapper or is_struct here.
      // Use MakeUnwrappedTypeFromProto to get an unwrapped type.
      *kind = TYPE_PROTO;
      break;
    default:
      return MakeSqlError() << "Invalid protocol buffer Type: " << field_type;
  }
  return absl::OkStatus();
}

absl::Status ProtoType::FieldDescriptorToTypeKind(
    bool ignore_annotations, const google::protobuf::FieldDescriptor* field,
    TypeKind* kind) {
  if (field->is_repeated()) {
    *kind = TYPE_ARRAY;
  } else if (field->options().GetExtension(googlesql::is_measure)) {
    *kind = TYPE_MEASURE;
  } else {
    GOOGLESQL_RETURN_IF_ERROR(
        FieldDescriptorToTypeKindBase(ignore_annotations, field, kind));
  }
  return absl::OkStatus();
}

absl::Status ProtoType::FieldDescriptorToTypeKind(
    const google::protobuf::FieldDescriptor* field, bool use_obsolete_timestamp,
    TypeKind* kind) {
  if (field->is_repeated()) {
    *kind = TYPE_ARRAY;
  } else if (field->options().GetExtension(googlesql::is_measure)) {
    *kind = TYPE_MEASURE;
  } else {
    GOOGLESQL_RETURN_IF_ERROR(
        FieldDescriptorToTypeKindBase(field, use_obsolete_timestamp, kind));
  }
  return absl::OkStatus();
}

const google::protobuf::FieldDescriptor* ProtoType::FindFieldByNameIgnoreCase(
    const google::protobuf::Descriptor* descriptor, absl::string_view name) {
  // Try the fast way first.
  const google::protobuf::FieldDescriptor* found = descriptor->FindFieldByName(name);
  if (found != nullptr) return found;

  // We don't bother looking for multiple names that match.
  // Protos with duplicate names (case insensitively) do not compile in
  // c++ or java, so we don't worry about them.
  for (int i = 0; i < descriptor->field_count(); ++i) {
    const google::protobuf::FieldDescriptor* field = descriptor->field(i);
    if (googlesql_base::CaseEqual(field->name(), name)) {
      return field;
    }
  }
  return nullptr;
}

bool ProtoType::HasFormatAnnotation(
    const google::protobuf::FieldDescriptor* field) {
  return GetFormatAnnotationImpl(field) != FieldFormat::DEFAULT_FORMAT;
}

FieldFormat::Format ProtoType::GetFormatAnnotationImpl(
    const google::protobuf::FieldDescriptor* field) {
  // Read the format encoding, or if it doesn't exist, the type encoding.
  if (field->options().HasExtension(googlesql::format)) {
    return field->options().GetExtension(googlesql::format);
  } else if (field->options().HasExtension(googlesql::type)) {
    return field->options().GetExtension(googlesql::type);
  } else {
    return FieldFormat::DEFAULT_FORMAT;
  }
}

// Forward declaration, see below for documentation.
static bool HasSubfieldWithFormatHelper(
    const google::protobuf::FieldDescriptor* field, FieldFormat::Format format,
    absl::flat_hash_set<const google::protobuf::Descriptor*>* visited_descriptors);

// This is a helper for HasSubfieldWithFormat() that keeps track the
// visited descriptors to avoid crashing in recursive protos messages.
static bool HasSubfieldWithFormatHelper(
    const google::protobuf::Descriptor* descriptor, FieldFormat::Format format,
    absl::flat_hash_set<const google::protobuf::Descriptor*>* visited_descriptors) {
  if (!visited_descriptors->insert(descriptor).second) {
    return false;
  }
  for (int i = 0; i < descriptor->field_count(); ++i) {
    if (HasSubfieldWithFormatHelper(descriptor->field(i), format,
                                    visited_descriptors))
      return true;
  }
  return false;
}

// This is a helper for HasSubfieldWithFormat() that keeps track the
// visited descriptors to avoid crashing in recursive protos messages.
static bool HasSubfieldWithFormatHelper(
    const google::protobuf::FieldDescriptor* field, FieldFormat::Format format,
    absl::flat_hash_set<const google::protobuf::Descriptor*>* visited_descriptors) {
  if (ProtoType::GetFormatAnnotation(field) == format) {
    return true;
  }
  if (field->message_type() != nullptr &&
      HasSubfieldWithFormatHelper(field->message_type(), format,
                                  visited_descriptors)) {
    return true;
  }
  return false;
}

bool ProtoType::HasSubfieldWithFormat(const google::protobuf::Descriptor* descriptor,
                                      FieldFormat::Format format) {
  absl::flat_hash_set<const google::protobuf::Descriptor*> visited_descriptors;
  return HasSubfieldWithFormatHelper(descriptor, format, &visited_descriptors);
}

bool ProtoType::HasSubfieldWithFormat(const google::protobuf::FieldDescriptor* field,
                                      FieldFormat::Format format) {
  absl::flat_hash_set<const google::protobuf::Descriptor*> visited_descriptors;
  return HasSubfieldWithFormatHelper(field, format, &visited_descriptors);
}

FieldFormat::Format ProtoType::GetFormatAnnotation(
    const google::protobuf::FieldDescriptor* field) {
  // Read the format (or deprecated type) encoding.
  const FieldFormat::Format format = GetFormatAnnotationImpl(field);

  const DeprecatedEncoding::Encoding encoding =
      field->options().GetExtension(googlesql::encoding);
  // If we also have a (valid) deprecated encoding annotation, merge that over
  // top of the type encoding.  Ignore any invalid encoding annotation.
  // This exists for backward compatibility with existing .proto files only.
  if (encoding == DeprecatedEncoding::DATE_DECIMAL &&
      format == FieldFormat::DATE) {
    return FieldFormat::DATE_DECIMAL;
  }
  return format;
}

bool ProtoType::GetUseDefaultsExtension(
    const google::protobuf::FieldDescriptor* field) {
  if (field->options().HasExtension(googlesql::use_defaults)) {
    // If the field has a use_defaults extension, use that.
    return field->options().GetExtension(googlesql::use_defaults);
  } else {
    // Otherwise, use its message's use_field_defaults extension
    // (which defaults to true if not explicitly set).
    const google::protobuf::Descriptor* parent = field->containing_type();
    return parent->options().GetExtension(googlesql::use_field_defaults);
  }
}

bool ProtoType::GetIsWrapperAnnotation(const google::protobuf::Descriptor* message) {
  return message->options().GetExtension(googlesql::is_wrapper);
}

bool ProtoType::GetIsStructAnnotation(const google::protobuf::Descriptor* message) {
  return message->options().GetExtension(googlesql::is_struct);
}

bool ProtoType::HasStructFieldName(const google::protobuf::FieldDescriptor* field) {
  return field->options().HasExtension(googlesql::struct_field_name);
}

const std::string& ProtoType::GetStructFieldName(
    const google::protobuf::FieldDescriptor* field) {
  return field->options().GetExtension(googlesql::struct_field_name);
}

namespace {

// Helper function that finds a field or a named extension with the given name.
// Possible return values are:
//  HAS_FIELD if the field exists;
//  HAS_NO_FIELD if neither exists.
Type::HasFieldResult HasProtoFieldOrNamedExtension(
    const google::protobuf::Descriptor* descriptor, absl::string_view name,
    int* field_id) {
  const google::protobuf::FieldDescriptor* field =
      ProtoType::FindFieldByNameIgnoreCase(descriptor, name);
  if (field != nullptr) {
    *field_id = field->number();
    return Type::HAS_FIELD;
  }

  return Type::HAS_NO_FIELD;
}

// Returns a reference to a container that stores a value of the proto type.
internal::ProtoRep* GetValueRef(const ValueContent& value) {
  return value.GetAs<internal::ProtoRep*>();
}

absl::Cord GetCordValue(const ValueContent& value) {
  return GetValueRef(value)->value();
}

}  // namespace

Type::HasFieldResult ProtoType::HasFieldImpl(const std::string& name,
                                             int* field_id,
                                             bool include_pseudo_fields) const {
  Type::HasFieldResult result = HAS_NO_FIELD;
  constexpr int kNotFound = -1;
  int found_idx = kNotFound;

  if (include_pseudo_fields) {
    // Consider virtual fields in addition to physical fields, which means
    // there may be ambiguity between a built-in field and a virtual field.
    result = HasProtoFieldOrNamedExtension(descriptor_, name, &found_idx);
    if (absl::StartsWithIgnoreCase(name, "has_") &&
        HasProtoFieldOrNamedExtension(descriptor_, name.substr(4),
                                      &found_idx) != HAS_NO_FIELD) {
      result =
          (result != HAS_NO_FIELD) ? HAS_AMBIGUOUS_FIELD : HAS_PSEUDO_FIELD;
    }
  } else {
    // Look for physical field only, so the result is always unambiguous.
    const google::protobuf::FieldDescriptor* field =
        ProtoType::FindFieldByNameIgnoreCase(descriptor_, name);
    if (field != nullptr) {
      found_idx = field->number();
      result = Type::HAS_FIELD;
    }
  }

  if (field_id != nullptr && found_idx != kNotFound) {
    *field_id = found_idx;
  }
  return result;
}

bool ProtoType::HasAnyFields() const { return descriptor_->field_count() != 0; }

bool ProtoType::EqualsImpl(const ProtoType* const type1,
                           const ProtoType* const type2, bool equivalent) {
  const internal::CatalogName* catalog_name1 = type1->catalog_name_;
  const internal::CatalogName* catalog_name2 = type2->catalog_name_;
  const bool catalogs_are_empty =
      catalog_name1 == nullptr && catalog_name2 == nullptr;
  const bool catalogs_are_equal =
      catalog_name1 != nullptr && catalog_name2 != nullptr &&
      *catalog_name1->path_string == *catalog_name2->path_string;

  if (type1->descriptor() == type2->descriptor() &&
      (catalogs_are_empty || catalogs_are_equal)) {
    return true;
  }
  if (equivalent &&
      type1->descriptor()->full_name() == type2->descriptor()->full_name()) {
    return true;
  }
  return false;
}

void ProtoType::CopyValueContent(const ValueContent& from,
                                 ValueContent* to) const {
  GetValueRef(from)->Ref();
  *to = from;
}

void ProtoType::ClearValueContent(const ValueContent& value) const {
  GetValueRef(value)->Unref();
}

uint64_t ProtoType::GetValueContentExternallyAllocatedByteSize(
    const ValueContent& value) const {
  return GetValueRef(value)->physical_byte_size();
}

absl::HashState ProtoType::HashTypeParameter(absl::HashState state) const {
  // Proto types are equivalent if they have the same full name, so hash it.
  return absl::HashState::combine(std::move(state), descriptor()->full_name());
}

absl::HashState ProtoType::HashValueContent(const ValueContent& value,
                                            absl::HashState state) const {
  // No efficient way to compute a hash on protobufs, so just let equals
  // sort it out.
  return absl::HashState::combine(std::move(state), 0);
}

bool ProtoType::ValueContentEquals(
    const ValueContent& x, const ValueContent& y,
    const ValueEqualityCheckOptions& options) const {
  const absl::Cord& x_value = x.GetAs<internal::ProtoRep*>()->value();
  const absl::Cord& y_value = y.GetAs<internal::ProtoRep*>()->value();

  // Shortcut fast case. When byte buffers are equal do not parse the protos.
  if (x_value == y_value) return true;

  // We use the descriptor from x.  The implementation of Type equality
  // currently means the descriptors must be identical.  If we relax that,
  // it is possible this comparison would be asymmetric, but only in
  // unusual cases where a message field is unknown on one side but not
  // the other, and doesn't compare identically as bytes.
  google::protobuf::DynamicMessageFactory factory;
  const google::protobuf::Message* prototype = factory.GetPrototype(descriptor());

  std::unique_ptr<google::protobuf::Message> x_msg = absl::WrapUnique(prototype->New());
  std::unique_ptr<google::protobuf::Message> y_msg = absl::WrapUnique(prototype->New());
  if (!x_msg->ParsePartialFromCord(x_value) ||
      !y_msg->ParsePartialFromCord(y_value)) {
    if (options.reason != nullptr) {
      absl::StrAppend(
          options.reason,
          "Failed to parse a proto message that resides in compared value");
    }
    return false;
  }
  google::protobuf::util::MessageDifferencer differencer;
  google::protobuf::util::DefaultFieldComparator comparator;
  comparator.set_treat_nan_as_equal(true);
  if (!options.float_margin.IsExactEquality()) {
    comparator.set_float_comparison(
        google::protobuf::util::DefaultFieldComparator::APPROXIMATE);
  }
  differencer.set_field_comparator(&comparator);
  std::string differencer_reason;
  if (options.reason != nullptr) {
    differencer.ReportDifferencesToString(&differencer_reason);
  }
  const bool result = differencer.Compare(*x_msg, *y_msg);
  if (!differencer_reason.empty()) {
    absl::StrAppend(options.reason, differencer_reason);
    // The newline will be added already.
    ABSL_DCHECK_EQ(differencer_reason[differencer_reason.size() - 1], '\n')
        << differencer_reason;
  }
  return result;
}

bool ProtoType::ValueContentLess(const ValueContent& x, const ValueContent& y,
                                 const Type* other_type) const {
  ABSL_LOG(ERROR) << "Cannot compare " << DebugString() << " to "
              << other_type->DebugString();
  return false;
}

std::string ProtoType::FormatValueContent(
    const ValueContent& value, const FormatValueContentOptions& options) const {
  if (!options.as_literal()) {
    return internal::GetCastExpressionString(
        ToBytesLiteral(std::string(GetCordValue(value))), this,
        options.product_mode, options.use_external_float32);
  }

  google::protobuf::DynamicMessageFactory message_factory;
  std::unique_ptr<google::protobuf::Message> message(
      message_factory.GetPrototype(descriptor())->New());
  const bool success = message->ParsePartialFromCord(GetCordValue(value));

  if (options.mode == FormatValueContentOptions::Mode::kDebug) {
    if (!success) {
      return "{<unparseable>}";
    }

    return absl::StrCat(
        "{",
        options.verbose
            ?
        message->DebugString()
        : message->ShortDebugString(),
        "}");
  }

  absl::Status status;
  absl::Cord out;
  if (functions::ProtoToString(message.get(), &out, &status)) {
    return ToStringLiteral(std::string(out));
  }

  // This branch is not expected, but try to return something.
  return ToStringLiteral(message->ShortDebugString());
}

absl::Status ProtoType::SerializeValueContent(const ValueContent& value,
                                              ValueProto* value_proto) const {
  value_proto->set_proto_value(GetCordValue(value));
  return absl::OkStatus();
}

absl::Status ProtoType::DeserializeValueContent(const ValueProto& value_proto,
                                                ValueContent* value) const {
  if (!value_proto.has_proto_value()) {
    return TypeMismatchError(value_proto);
  }
  value->set(new internal::ProtoRep(this, value_proto.proto_value()));
  return absl::OkStatus();
}

}  // namespace googlesql
