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

#ifndef GOOGLESQL_PUBLIC_TYPES_PROTO_TYPE_H_
#define GOOGLESQL_PUBLIC_TYPES_PROTO_TYPE_H_

#include <cstddef>
#include <cstdint>
#include <string>

#include "googlesql/base/logging.h"
#include "google/protobuf/descriptor.pb.h"
#include "googlesql/common/errors.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/proto/type_annotation.pb.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_modifiers.h"
#include "googlesql/public/types/type_parameters.h"
#include "absl/hash/hash.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "google/protobuf/descriptor.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
class LanguageOptions;
class TypeFactory;
class ValueContent;
class ValueProto;
}  // namespace googlesql

namespace googlesql {

namespace internal {
struct CatalogName;
}

// A proto type.
// The type may contain a catalog path if it was explicitly provided during
// construction, so it can be reproduced by SQLBuilder while rebuilding SQL and
// error messages and SQLBuilder. Two types with the same descriptors but
// different catalog names are not equal, but equivalent.
class ProtoType : public Type {
 public:
#ifndef SWIG
  ProtoType(const ProtoType&) = delete;
  ProtoType& operator=(const ProtoType&) = delete;
#endif  // SWIG

  const ProtoType* AsProto() const override { return this; }

  const google::protobuf::Descriptor* descriptor() const;

  // Returns the value of descriptor()->map_key().
  const google::protobuf::FieldDescriptor* map_key() const;

  // Returns the value of descriptor()->map_value().
  const google::protobuf::FieldDescriptor* map_value() const;

  // Helper function to determine equality or equivalence for proto types.
  static bool EqualsImpl(const ProtoType* type1, const ProtoType* type2,
                         bool equivalent);

  bool SupportsOrdering(const LanguageOptions& language_options,
                        std::string* type_description) const override;
  bool SupportsEquality() const override { return false; }

  bool UsingFeatureV12CivilTimeType() const override { return false; }

  // TODO: The current implementation of TypeName/ShortTypeName
  // should be re-examined for Proto and Enum Types.  Currently, the
  // TypeName is the back-ticked descriptor full_name, while the ShortTypeName
  // is just the descriptor full_name (without back-ticks).  The back-ticks
  // are not necessary for TypeName() to be reparseable, so should be removed.
  std::string TypeName(ProductMode mode_unused) const override;
  std::string TypeName(ProductMode mode_unused,
                       bool use_external_float32_unused) const override {
    return TypeName();
  }
  // ProtoType does not support type parameters or collation, which is why
  // TypeName(mode) is used.
  absl::StatusOr<std::string> TypeNameWithModifiers(
      const TypeModifiers& type_modifiers, ProductMode mode) const override {
    const TypeParameters& type_params = type_modifiers.type_parameters();
    const Collation& collation = type_modifiers.collation();
    GOOGLESQL_RET_CHECK(type_params.IsEmpty());
    GOOGLESQL_RET_CHECK(collation.Empty());
    return TypeName(mode);
  }
  absl::StatusOr<std::string> TypeNameWithModifiers(
      const TypeModifiers& type_modifiers, ProductMode mode,
      bool use_external_float32_unused) const override {
    return TypeNameWithModifiers(type_modifiers, mode);
  }

  std::string ShortTypeName(ProductMode mode_unused) const override {
    return ShortTypeName();
  }

  std::string ShortTypeName(ProductMode mode_unused,
                            bool use_external_float32_unused) const override {
    return ShortTypeName();
  }

  std::string CapitalizedName() const override;

  std::string ShortTypeName() const;
  std::string TypeName() const;  // Proto-specific version does not need mode.

  // Nested catalog names, that were passed to the constructor.
  absl::Span<const std::string> CatalogNamePath() const;

  // Get the GoogleSQL Type of the requested field of the proto, identified by
  // either tag number or name.  A new Type may be created so a type factory
  // is required.  If the field name or number is not found, then
  // absl::StatusCode::kInvalidArgument is returned.  The last argument can be
  // used to output the corresponding name/number as appropriate.
  absl::Status GetFieldTypeByTagNumber(int number, TypeFactory* factory,
                                       const Type** type,
                                       std::string* name = nullptr) const {
    return GetFieldTypeByTagNumber(
        number, factory, /*use_obsolete_timestamp=*/false, type, name);
  }
  absl::Status GetFieldTypeByName(absl::string_view name, TypeFactory* factory,
                                  const Type** type,
                                  int* number = nullptr) const {
    return GetFieldTypeByName(name, factory,
                              /*use_obsolete_timestamp=*/false, type, number);
  }

  // DEPRECATED: Callers should remove their dependencies on obsolete types and
  // move to the methods above.
  ABSL_DEPRECATED("Use overload without 'use_obsolete_timestamp' argument.")
  absl::Status GetFieldTypeByTagNumber(int number, TypeFactory* factory,
                                       bool use_obsolete_timestamp,
                                       const Type** type,
                                       std::string* name = nullptr) const;
  ABSL_DEPRECATED("Use overload without 'use_obsolete_timestamp' argument.")
  absl::Status GetFieldTypeByName(absl::string_view name, TypeFactory* factory,
                                  bool use_obsolete_timestamp,
                                  const Type** type,
                                  int* number = nullptr) const;

  // Get the GoogleSQL TypeKind of the requested proto field. If
  // <ignore_annotations> is false, then format annotations on the field are
  // respected when determining the TypeKind. If <ignore_annotations> is true,
  // then format annotations are ignored and the default TypeKind for the proto
  // field type is returned.
  //
  // This always ignores (does not unwrap) is_struct and is_wrapper annotations.
  static absl::Status FieldDescriptorToTypeKind(
      bool ignore_annotations, const google::protobuf::FieldDescriptor* field,
      TypeKind* kind);

  // Get the GoogleSQL TypeKind of the requested proto field.
  // This is the same as the above signature with ignore_annotations = false.
  // This is the TypeKind for the field type visible in GoogleSQL, matching
  // the Type returned by GetProtoFieldType (except for array types).
  static absl::Status FieldDescriptorToTypeKind(
      const google::protobuf::FieldDescriptor* field, TypeKind* kind) {
    return FieldDescriptorToTypeKind(
        /*ignore_annotations=*/false, field, kind);
  }
  // DEPRECATED: Callers should remove their dependencies on obsolete types and
  // move to the method above.
  ABSL_DEPRECATED("Use overload without 'use_obsolete_timestamp' argument.")
  static absl::Status FieldDescriptorToTypeKind(
      const google::protobuf::FieldDescriptor* field, bool use_obsolete_timestamp,
      TypeKind* kind);

  // This is the same as FieldDescriptorToTypeKind except it ignores
  // repeatedness of the proto field and never returns TYPE_ARRAY.
  static absl::Status FieldDescriptorToTypeKindBase(
      bool ignore_annotations, const google::protobuf::FieldDescriptor* field,
      TypeKind* kind) {
    return GetTypeKindFromFieldDescriptor(field, ignore_annotations,
                                          /*use_obsolete_timestamp=*/false,
                                          kind);
  }
  // This is the same as the above signature with ignore_annotations = false.
  static absl::Status FieldDescriptorToTypeKindBase(
      const google::protobuf::FieldDescriptor* field, TypeKind* kind) {
    return FieldDescriptorToTypeKindBase(/*ignore_annotations=*/false, field,
                                         kind);
  }
  // DEPRECATED: Callers should remove their dependencies on obsolete types and
  // move to the method above.
  ABSL_DEPRECATED("Use overload without 'use_obsolete_timestamp' argument.")
  static absl::Status FieldDescriptorToTypeKindBase(
      const google::protobuf::FieldDescriptor* field, bool use_obsolete_timestamp,
      TypeKind* kind) {
    return GetTypeKindFromFieldDescriptor(field,
                                          /*ignore_format_annotations=*/false,
                                          use_obsolete_timestamp, kind);
  }

  // Case insensitive version of google::protobuf::Descriptor::FindFieldByName.
  // Returns NULL if the name is not found.
  static const google::protobuf::FieldDescriptor* FindFieldByNameIgnoreCase(
      const google::protobuf::Descriptor* descriptor, absl::string_view name);

  // Get the googlesql Format from a FieldDescriptor.
  // Note that if a deprecated Encoding annotation exists and is valid,
  // this merges it over top of the Format annotation and acts as if the
  // Format was written in the current non-deprecated syntax.
  static bool HasFormatAnnotation(const google::protobuf::FieldDescriptor* field);
  static FieldFormat::Format GetFormatAnnotation(
      const google::protobuf::FieldDescriptor* field);

  // Returns true if <field>/<descriptor> or any of its descendent fields have
  // <format> as a GoogleSQL annotation as returned by
  // GetFormatAnnotation(). Otherwise, returns false.
  static bool HasSubfieldWithFormat(const google::protobuf::Descriptor* descriptor,
                                    FieldFormat::Format format);
  static bool HasSubfieldWithFormat(const google::protobuf::FieldDescriptor* field,
                                    FieldFormat::Format format);

  // Returns true if default value for <field> should be used.
  // Returns false if SQL NULL should be used instead.
  // This is based on the googlesql.use_defaults annotation on the field and
  // the googlesql.use_field_defaults annotation on the containing message.
  static bool GetUseDefaultsExtension(const google::protobuf::FieldDescriptor* field);

  // Returns true if <message> is annotated with googlesql.is_wrapper=true.
  static bool GetIsWrapperAnnotation(const google::protobuf::Descriptor* message);

  // Returns true if <message> is annotated with googlesql.is_struct=true.
  static bool GetIsStructAnnotation(const google::protobuf::Descriptor* message);

  // Get the struct field name from a FieldDescriptor.
  static bool HasStructFieldName(const google::protobuf::FieldDescriptor* field);
  static const std::string& GetStructFieldName(
      const google::protobuf::FieldDescriptor* field);

  // Check if structure has some fields.
  bool HasAnyFields() const override;

  // Validate TypeAnnotations for a file, proto, or field.  Protos not
  // in <validated_descriptor_set> are added to the set and validated.
  // <validated_descriptor_set> may be a pointer to any set type that contains
  // 'const google::protobuf::Descriptor*' (absl::flat_hash_set<const google::protobuf::Descriptor*>
  // is recommended), or it may simply be nullptr.  Proto validation includes
  // recursively validating proto types of fields if <validated_descriptor_set>
  // is not NULL.
  template <typename SetPtrType>
  static absl::Status ValidateTypeAnnotations(
      const google::protobuf::FileDescriptor* file_descriptor,
      SetPtrType validated_descriptor_set);

  template <typename SetPtrType>
  static absl::Status ValidateTypeAnnotations(
      const google::protobuf::Descriptor* descriptor,
      SetPtrType validated_descriptor_set);
  static absl::Status ValidateTypeAnnotations(
      const google::protobuf::Descriptor* descriptor) {
    return ValidateTypeAnnotations(descriptor,
                                   /*validated_descriptor_set=*/nullptr);
  }

  template <typename SetPtrType>
  static absl::Status ValidateTypeAnnotations(
      const google::protobuf::FieldDescriptor* field,
      SetPtrType validated_descriptor_set);
  static absl::Status ValidateTypeAnnotations(
      const google::protobuf::FieldDescriptor* field) {
    return ValidateTypeAnnotations(field, /*validated_descriptor_set=*/nullptr);
  }

  bool IsSupportedType(const LanguageOptions& language_options) const override;

 protected:
  int64_t GetEstimatedOwnedMemoryBytesSize() const override {
    return sizeof(*this);
  }

  bool HasFloatingPointFields() const override;

 private:
  // Returns true iff <validated_descriptor_set> is not null and already
  // contains <descriptor>.  Otherwise returns false and, if
  // <validated_descriptor_set> is non-null, inserts <descriptor> into it.
  template <typename SetPtrType>
  static bool IsAlreadyValidated(SetPtrType validated_descriptor_set,
                                 const google::protobuf::Descriptor* descriptor) {
    return validated_descriptor_set != nullptr &&
           !validated_descriptor_set->insert(descriptor).second;
  }

  // Does not take ownership of <factory> or <descriptor>.  The <descriptor>
  // must outlive the type.
  ProtoType(const TypeFactory* factory, const google::protobuf::Descriptor* descriptor,
            const internal::CatalogName* catalog_name);
  ~ProtoType() override;

  bool SupportsGroupingImpl(const LanguageOptions& language_options,
                            const Type** no_grouping_type) const override {
    if (no_grouping_type != nullptr) {
      *no_grouping_type = this;
    }
    return false;
  }

  bool SupportsPartitioningImpl(
      const LanguageOptions& language_options,
      const Type** no_partitioning_type) const override {
    if (no_partitioning_type != nullptr) {
      *no_partitioning_type = this;
    }
    return false;
  }

  // Internal version of GetFormatAnnotation that just merges <type> and
  // <format>, without merging <encoding>.
  static FieldFormat::Format GetFormatAnnotationImpl(
      const google::protobuf::FieldDescriptor* field);

  // Get the GoogleSQL TypeKind of the requested proto field. If
  // <ignore_format_annotations> is true, then format annotations are ignored
  // and the default TypeKind for the proto field type is returned.
  ABSL_DEPRECATED("Use overload without 'use_obsolete_timestamp' argument.")
  static absl::Status GetTypeKindFromFieldDescriptor(
      const google::protobuf::FieldDescriptor* field, bool ignore_format_annotations,
      bool use_obsolete_timestamp, TypeKind* kind);

  absl::Status SerializeToProtoAndDistinctFileDescriptorsImpl(
      const BuildFileDescriptorMapOptions& options, TypeProto* type_proto,
      FileDescriptorSetMap* file_descriptor_set_map) const override;

  bool EqualsForSameKind(const Type* that, bool equivalent) const override;

  void DebugStringImpl(bool details, TypeOrStringVector* stack,
                       std::string* debug_string) const override;

  HasFieldResult HasFieldImpl(const std::string& name, int* field_id,
                              bool include_pseudo_fields) const override;

  void CopyValueContent(const ValueContent& from,
                        ValueContent* to) const override;
  void ClearValueContent(const ValueContent& value) const override;
  uint64_t GetValueContentExternallyAllocatedByteSize(
      const ValueContent& value) const override;
  absl::HashState HashTypeParameter(absl::HashState state) const override;
  absl::HashState HashValueContent(const ValueContent& value,
                                   absl::HashState state) const override;
  bool ValueContentEquals(
      const ValueContent& x, const ValueContent& y,
      const ValueEqualityCheckOptions& options) const override;
  bool ValueContentLess(const ValueContent& x, const ValueContent& y,
                        const Type* other_type) const override;
  std::string FormatValueContent(
      const ValueContent& value,
      const FormatValueContentOptions& options) const override;
  absl::Status SerializeValueContent(const ValueContent& value,
                                     ValueProto* value_proto) const override;
  absl::Status DeserializeValueContent(const ValueProto& value_proto,
                                       ValueContent* value) const override;

  const google::protobuf::Descriptor* descriptor_;       // Not owned.
  const internal::CatalogName* catalog_name_;  // Optional.

  friend class TypeFactory;

  // This friend class is a thief used to access HasFloatingPointFields from the
  // reference implementation.
  friend class ProtoTypeHasFloatingPointChecker;
};

// Implementation of templated methods of ProtoType.
template <typename SetPtrType>
absl::Status ProtoType::ValidateTypeAnnotations(
    const google::protobuf::FileDescriptor* file_descriptor,
    SetPtrType validated_descriptor_set) {
  // Check all messages.
  for (int idx = 0; idx < file_descriptor->message_type_count(); ++idx) {
    const google::protobuf::Descriptor* message_type =
        file_descriptor->message_type(idx);
    GOOGLESQL_RETURN_IF_ERROR(
        ValidateTypeAnnotations(message_type, validated_descriptor_set));
  }

  // Check all extensions.
  for (int idx = 0; idx < file_descriptor->extension_count(); ++idx) {
    const google::protobuf::FieldDescriptor* extension =
        file_descriptor->extension(idx);
    GOOGLESQL_RETURN_IF_ERROR(
        ValidateTypeAnnotations(extension, validated_descriptor_set));
  }

  return absl::OkStatus();
}

template <typename SetPtrType>
absl::Status ProtoType::ValidateTypeAnnotations(
    const google::protobuf::Descriptor* descriptor,
    SetPtrType validated_descriptor_set) {
  if (IsAlreadyValidated(validated_descriptor_set, descriptor)) {
    // Already validated this proto, return OK.
    return absl::OkStatus();
  }

  // Check googlesql.is_wrapper.
  if (GetIsWrapperAnnotation(descriptor)) {
    if (descriptor->field_count() != 1) {
      return MakeSqlError()
             << "Proto " << descriptor->full_name()
             << " has googlesql.is_wrapper = true but does not have exactly"
             << " one field:\n"
             << descriptor->DebugString();
    }
    // We cannot have both is_wrapper and is_struct.  Beyond that, there is
    // no checking to do for is_struct.
    if (GetIsStructAnnotation(descriptor)) {
      return MakeSqlError()
             << "Proto " << descriptor->full_name()
             << " has both googlesql.is_wrapper = true and"
                " googlesql.is_struct = true:\n"
             << descriptor->DebugString();
    }
  }

  // Check all fields.
  for (int idx = 0; idx < descriptor->field_count(); ++idx) {
    const google::protobuf::FieldDescriptor* field = descriptor->field(idx);
    GOOGLESQL_RETURN_IF_ERROR(ValidateTypeAnnotations(field, validated_descriptor_set));
  }

  return absl::OkStatus();
}

template <typename SetPtrType>
absl::Status ProtoType::ValidateTypeAnnotations(
    const google::protobuf::FieldDescriptor* field,
    SetPtrType validated_descriptor_set) {
  const google::protobuf::FieldDescriptor::Type field_type = field->type();

  // Check googlesql.format and the deprecated googlesql.type version.
  // While validating, we check HasExtension explicitly because we want to
  // make sure the extension is not explicitly written as DEFAULT_FORMAT.
  if (field->options().HasExtension(googlesql::format) ||
      field->options().HasExtension(googlesql::type)) {
    const FieldFormat::Format field_format = GetFormatAnnotationImpl(field);
    // NOTE: This should match ProtoUtil::CheckIsSupportedFieldFormat in
    // reference_impl/proto_util.cc.
    switch (field_type) {
      case google::protobuf::FieldDescriptor::TYPE_INT32:
      case google::protobuf::FieldDescriptor::TYPE_SFIXED32:
      case google::protobuf::FieldDescriptor::TYPE_SINT32:
        // DATE and DATE_DECIMAL are valid for int32.
        if (field_format != FieldFormat::DATE &&
            field_format != FieldFormat::DATE_DECIMAL) {
          return MakeSqlError()
                 << "Proto " << field->containing_type()->full_name()
                 << " has invalid googlesql.format for INT32 field: "
                 << field->DebugString();
        }
        break;
      case google::protobuf::FieldDescriptor::TYPE_INT64:
      case google::protobuf::FieldDescriptor::TYPE_SFIXED64:
      case google::protobuf::FieldDescriptor::TYPE_SINT64:
        {
          // DATE, DATE_DECIMAL, DATETIME, TIME and TIMESTAMP_* are valid for
          // int64.
          if (field_format != FieldFormat::DATE &&
              field_format != FieldFormat::DATE_DECIMAL &&
              field_format != FieldFormat::TIMESTAMP_SECONDS &&
              field_format != FieldFormat::TIMESTAMP_MILLIS &&
              field_format != FieldFormat::TIMESTAMP_MICROS &&
              field_format != FieldFormat::TIMESTAMP_NANOS &&
              field_format != FieldFormat::TIME_MICROS &&
              field_format != FieldFormat::DATETIME_MICROS) {
            return MakeSqlError()
                   << "Proto " << field->containing_type()->full_name()
                   << " has invalid googlesql.format for INT64 field: "
                   << field->DebugString();
          }
        }
        break;
      case google::protobuf::FieldDescriptor::TYPE_UINT64:
        {
          if (field_format != FieldFormat::TIMESTAMP_MICROS) {
            return MakeSqlError()
                   << "Proto " << field->containing_type()->full_name()
                   << " has invalid googlesql.format for UINT64 field: "
                   << field->DebugString();
          }
        }
        break;
      case google::protobuf::FieldDescriptor::TYPE_BYTES:
        {
        if (field_format != FieldFormat::ST_GEOGRAPHY_ENCODED &&
            field_format != FieldFormat::NUMERIC &&
            field_format != FieldFormat::BIGNUMERIC &&
            field_format != FieldFormat::TIMESTAMP_PICOS &&
            field_format != FieldFormat::TOKENLIST &&
            field_format != FieldFormat::RANGE_DATES_ENCODED &&
            field_format != FieldFormat::RANGE_DATETIMES_ENCODED &&
            field_format != FieldFormat::RANGE_TIMESTAMPS_ENCODED &&
            field_format != FieldFormat::INTERVAL &&
            field_format != FieldFormat::UUID &&
            field_format != FieldFormat::TIMESTAMP) {
          return MakeSqlError()
                 << "Proto " << field->containing_type()->full_name()
                 << " has invalid googlesql.format for BYTES field: "
                 << field->DebugString();
        }
      } break;
      case google::protobuf::FieldDescriptor::TYPE_STRING:
        {
        if (field_format != FieldFormat::JSON) {
          return MakeSqlError()
                 << "Proto " << field->containing_type()->full_name()
                 << " has invalid googlesql.format for STRING field: "
                 << field->DebugString();
        }
        }
        break;
      default:
        return MakeSqlError()
               << "Proto " << field->containing_type()->full_name()
               << " has invalid googlesql.format for field: "
               << field->DebugString();
        break;
    }
  }

  // Check googlesql.encoding (which is deprecated).
  if (field->options().HasExtension(googlesql::encoding)) {
    const DeprecatedEncoding::Encoding encoding_annotation =
        field->options().GetExtension(googlesql::encoding);
    const FieldFormat::Format format_annotation =
        GetFormatAnnotationImpl(field);
    switch (encoding_annotation) {
      case DeprecatedEncoding::DATE_DECIMAL:
        // This is allowed only on DATE fields.
        if (format_annotation != FieldFormat::DATE) {
          return MakeSqlError()
                 << "Proto " << field->containing_type()->full_name()
                 << " has googlesql.encoding that can only be applied"
                    " along with googlesql.format=DATE for field: "
                 << field->DebugString();
        }
        break;

      default:
        return MakeSqlError()
               << "Proto " << field->containing_type()->full_name()
               << " has invalid googlesql.encoding for field: "
               << field->DebugString();
    }
  }

  // Check googlesql.use_defaults.  Explicitly setting use_defaults to
  // true for repeated fields is an error.
  if (field->options().HasExtension(googlesql::use_defaults) &&
      field->options().GetExtension(googlesql::use_defaults) &&
      field->is_repeated()) {
    return MakeSqlError()
           << "Proto " << field->containing_type()->full_name()
           << " has invalid googlesql.use_defaults for repeated field: "
           << field->DebugString();
  }

  // Recurse if relevant.
  if (validated_descriptor_set != nullptr &&
      (field_type == google::protobuf::FieldDescriptor::TYPE_GROUP ||
       field_type == google::protobuf::FieldDescriptor::TYPE_MESSAGE)) {
    return ValidateTypeAnnotations(field->message_type(),
                                   validated_descriptor_set);
  }

  return absl::OkStatus();
}

// Template override for std::nullptr_t. This is needed when the caller simply
// passes in 'nullptr' for 'validated_descriptor_set', which should be supported
// even though it is not a set type that can work with the generic version.
template <>
inline bool ProtoType::IsAlreadyValidated(
    std::nullptr_t validated_descriptor_set,
    const google::protobuf::Descriptor* descriptor) {
  return false;
}

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_TYPES_PROTO_TYPE_H_
