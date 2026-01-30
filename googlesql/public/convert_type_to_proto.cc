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

#include "googlesql/public/convert_type_to_proto.h"

#include <ctype.h>

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "google/protobuf/descriptor.pb.h"
#include "googlesql/common/errors.h"
#include "googlesql/public/proto/wire_format_annotation.pb.h"
#include "googlesql/public/types/measure_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/value.pb.h"
#include "googlesql/base/case.h"
#include "absl/container/btree_set.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

static const char kGeneratedFieldNamePrefix[] = "_field_";

typedef ConvertTypeToProtoOptions::MessageOrEnumDescriptor
    MessageOrEnumDescriptor;

// TODO We haven't tuned what the generated proto looks like
// very much.  We can update that based on usage and desires of clients.
class TypeToProtoConverter {
 public:
  explicit TypeToProtoConverter(const ConvertTypeToProtoOptions& options)
      : options_(options) {}
  TypeToProtoConverter(const TypeToProtoConverter&) = delete;
  TypeToProtoConverter& operator=(const TypeToProtoConverter&) = delete;

  // Build the FileDescriptorProto in <file>.
  // If <table_type> is not DEFAULT_TABLE_TYPE, this makes a descriptor
  // to represent a table.
  // For DEFAULT_TABLE_TYPE, <type> must be a StructType or ArrayType.
  // This is meant to be called only once.
  absl::Status MakeFileDescriptorProto(const Type* type,
                                       TableType table_type,
                                       google::protobuf::FileDescriptorProto* file);

 private:
  const ConvertTypeToProtoOptions& options_;

  // Counter used to generate unique sub-message names.
  int unique_name_counter_ = 0;

  // The outermost DescriptorProto being constructed.
  google::protobuf::DescriptorProto* main_message_ = nullptr;

  // Used to collect the set of required .proto imports.
  std::set<std::string> import_filenames_;

  // Wrapper protos for structs and scalars.
  std::map<const Type*, google::protobuf::DescriptorProto*> constructed_wrappers_;
  // Struct protos.
  std::map<const Type*, google::protobuf::DescriptorProto*> constructed_structs_;
  // Array protos, keyed by element type.
  std::map<const Type*, google::protobuf::DescriptorProto*> constructed_arrays_;

  // Add a field to <in_message>.
  absl::Status MakeFieldDescriptor(const Type* field_type,
                                   const std::string& field_name,
                                   bool store_field_name_as_annotation,
                                   int field_number,
                                   google::protobuf::FieldDescriptorProto::Label label,
                                   google::protobuf::DescriptorProto* in_message);
  // Constructs a FieldDescriptorProto for the given field.
  absl::Status MakeFieldDescriptor(const Type* field_type,
                                   const std::string& field_name,
                                   bool store_field_name_as_annotation,
                                   int field_number,
                                   google::protobuf::FieldDescriptorProto::Label label,
                                   google::protobuf::FieldDescriptorProto* proto_field);

  // Make a proto to represent <struct_type> in <in_message>, which is
  // assumed to be an empty message.
  absl::Status MakeStructProto(const StructType* struct_type,
                               absl::string_view name,
                               google::protobuf::DescriptorProto* struct_proto);

  // Make a proto to represent <array_type> in <in_message>, which is
  // assumed to be an empty message.
  absl::Status MakeArrayProto(const ArrayType* array_type,
                              absl::string_view name,
                              google::protobuf::DescriptorProto* array_proto);

  // Get the DescriptorProto for a struct, re-using a cached one if possible.
  absl::Status GetDescriptorForStruct(
      const StructType* struct_type,
      const google::protobuf::DescriptorProto** descriptor_proto);

  // Get the wrapper DescriptorProto for storing a nullable object of
  // type <type>, re-using a cached one if possible.
  // Should not be used for ArrayTypes.
  absl::Status GetWrapperDescriptorForType(
      const Type* type,
      const google::protobuf::DescriptorProto** descriptor_proto);

  // Get the DescriptorProto for a nullable array, re-using a cached one if
  // possible.
  absl::Status GetDescriptorForArray(
      const ArrayType* array_type,
      const google::protobuf::DescriptorProto** descriptor_proto);
};

absl::Status TypeToProtoConverter::MakeFieldDescriptor(
    const Type* field_type, const std::string& field_name,
    bool store_field_name_as_annotation, int field_number,
    google::protobuf::FieldDescriptorProto::Label label,
    google::protobuf::DescriptorProto* in_message) {
  GOOGLESQL_RET_CHECK_GE(field_number, 1);

  if (field_type->kind() == TYPE_ARRAY &&
      !options_.generate_nullable_array_wrappers &&
      !options_.generate_nullable_element_wrappers) {
    return MakeFieldDescriptor(
        field_type->AsArray()->element_type(), field_name,
        store_field_name_as_annotation, field_number,
        google::protobuf::FieldDescriptorProto::LABEL_REPEATED, in_message);
  }
  return MakeFieldDescriptor(field_type, field_name,
                             store_field_name_as_annotation, field_number,
                             label, in_message->add_field());
}

absl::Status TypeToProtoConverter::MakeFieldDescriptor(
    const Type* field_type, const std::string& field_name,
    bool store_field_name_as_annotation, int field_number,
    google::protobuf::FieldDescriptorProto::Label label,
    google::protobuf::FieldDescriptorProto* proto_field) {
  GOOGLESQL_RET_CHECK_GE(field_number, 1);
  if (store_field_name_as_annotation) {
    proto_field->set_name(
        absl::StrCat(kGeneratedFieldNamePrefix, field_number));
    proto_field->mutable_options()->SetExtension(googlesql::struct_field_name,
                                                 field_name);
  } else {
    GOOGLESQL_RET_CHECK(!field_name.empty());
    proto_field->set_name(field_name);
  }
  proto_field->set_number(field_number);
  proto_field->set_label(label);

  ABSL_DCHECK(!proto_field->has_type());
  switch (field_type->kind()) {
    case TYPE_INT32:
      proto_field->set_type(google::protobuf::FieldDescriptorProto::TYPE_INT32);
      break;
    case TYPE_INT64:
      proto_field->set_type(google::protobuf::FieldDescriptorProto::TYPE_INT64);
      break;
    case TYPE_UINT32:
      proto_field->set_type(google::protobuf::FieldDescriptorProto::TYPE_UINT32);
      break;
    case TYPE_UINT64:
      proto_field->set_type(google::protobuf::FieldDescriptorProto::TYPE_UINT64);
      break;
    case TYPE_BOOL:
      proto_field->set_type(google::protobuf::FieldDescriptorProto::TYPE_BOOL);
      break;
    case TYPE_FLOAT:
      proto_field->set_type(google::protobuf::FieldDescriptorProto::TYPE_FLOAT);
      break;
    case TYPE_DOUBLE:
      proto_field->set_type(google::protobuf::FieldDescriptorProto::TYPE_DOUBLE);
      break;
    case TYPE_STRING:
      proto_field->set_type(google::protobuf::FieldDescriptorProto::TYPE_STRING);
      break;
    case TYPE_BYTES:
      proto_field->set_type(google::protobuf::FieldDescriptorProto::TYPE_BYTES);
      break;
    case TYPE_DATE:
      proto_field->set_type(google::protobuf::FieldDescriptorProto::TYPE_INT32);
      proto_field->mutable_options()->SetExtension(
          googlesql::format, FieldFormat::DATE);
      break;
    case TYPE_TIMESTAMP:
      if (options_.timestamp_format_options.timestamp_format ==
          ConvertTypeToProtoOptions::TimestampFormat::kTimestampPicosProto) {
        proto_field->set_type(google::protobuf::FieldDescriptorProto::TYPE_BYTES);
        proto_field->mutable_options()->SetExtension(googlesql::format,
                                                     FieldFormat::TIMESTAMP);
      } else {
        proto_field->set_type(google::protobuf::FieldDescriptorProto::TYPE_INT64);
        proto_field->mutable_options()->SetExtension(
            googlesql::format, FieldFormat::TIMESTAMP_MICROS);
      }
      break;
    case TYPE_TIME:
      proto_field->set_type(google::protobuf::FieldDescriptorProto::TYPE_INT64);
      proto_field->mutable_options()->SetExtension(
          googlesql::format, FieldFormat::TIME_MICROS);
      break;
    case TYPE_DATETIME:
      proto_field->set_type(google::protobuf::FieldDescriptorProto::TYPE_INT64);
      proto_field->mutable_options()->SetExtension(
          googlesql::format, FieldFormat::DATETIME_MICROS);
      break;
    case TYPE_INTERVAL:
      proto_field->set_type(google::protobuf::FieldDescriptorProto::TYPE_BYTES);
      proto_field->mutable_options()->SetExtension(googlesql::format,
                                                   FieldFormat::INTERVAL);
      break;
    case TYPE_GEOGRAPHY:
      proto_field->set_type(google::protobuf::FieldDescriptorProto::TYPE_BYTES);
      proto_field->mutable_options()->SetExtension(
          googlesql::format, FieldFormat::ST_GEOGRAPHY_ENCODED);
      break;
    case TYPE_NUMERIC: {
      proto_field->set_type(google::protobuf::FieldDescriptorProto::TYPE_BYTES);
      proto_field->mutable_options()->SetExtension(
          googlesql::format, FieldFormat::NUMERIC);
      break;
    }
    case TYPE_BIGNUMERIC: {
      proto_field->set_type(google::protobuf::FieldDescriptorProto::TYPE_BYTES);
      proto_field->mutable_options()->SetExtension(
          googlesql::format, FieldFormat::BIGNUMERIC);
      break;
    }
    case TYPE_JSON: {
      proto_field->set_type(google::protobuf::FieldDescriptorProto::TYPE_STRING);
      proto_field->mutable_options()->SetExtension(
          googlesql::format, FieldFormat::JSON);
      break;
    }
    case TYPE_TOKENLIST: {
      proto_field->set_type(google::protobuf::FieldDescriptorProto::TYPE_BYTES);
      proto_field->mutable_options()->SetExtension(googlesql::format,
                                                   FieldFormat::TOKENLIST);
      break;
    }
    case TYPE_UUID: {
      proto_field->set_type(google::protobuf::FieldDescriptorProto::TYPE_BYTES);
      proto_field->mutable_options()->SetExtension(googlesql::format,
                                                   FieldFormat::UUID);
      break;
    }
    case TYPE_RANGE: {
      proto_field->set_type(google::protobuf::FieldDescriptorProto::TYPE_BYTES);
      const RangeType* range_type = field_type->AsRange();
      switch (range_type->element_type()->kind()) {
        case TYPE_DATE:
          proto_field->mutable_options()->SetExtension(
              googlesql::format, FieldFormat::RANGE_DATES_ENCODED);
          break;
        case TYPE_DATETIME:
          proto_field->mutable_options()->SetExtension(
              googlesql::format, FieldFormat::RANGE_DATETIMES_ENCODED);
          break;
        case TYPE_TIMESTAMP:
          proto_field->mutable_options()->SetExtension(
              googlesql::format, FieldFormat::RANGE_TIMESTAMPS_ENCODED);
          break;
        default:
          return absl::UnimplementedError(
              absl::StrCat("Conversion of ", range_type->DebugString(),
                           " is not supported."));
      }
      break;
    }
    case TYPE_ENUM: {
      const EnumType* enum_type = field_type->AsEnum();
      proto_field->set_type(google::protobuf::FieldDescriptorProto::TYPE_ENUM);
      proto_field->set_type_name(
          absl::StrCat(".", enum_type->enum_descriptor()->full_name()));
      import_filenames_.emplace(enum_type->enum_descriptor()->file()->name());

      if (options_.output_field_descriptor_map != nullptr) {
        (*options_.output_field_descriptor_map)[proto_field] =
            MessageOrEnumDescriptor(enum_type->enum_descriptor());
      }
      break;
    }
    case TYPE_PROTO: {
      const ProtoType* proto_type = field_type->AsProto();
      proto_field->set_type(google::protobuf::FieldDescriptorProto::TYPE_MESSAGE);
      proto_field->set_type_name(
          absl::StrCat(".", proto_type->descriptor()->full_name()));
      import_filenames_.emplace(proto_type->descriptor()->file()->name());

      if (options_.output_field_descriptor_map != nullptr) {
        (*options_.output_field_descriptor_map)[proto_field] =
            MessageOrEnumDescriptor(proto_type->descriptor());
      }
      // If we have any message-level annotations that would change the
      // interpreted type, add a field-level googlesql.is_raw_proto annotation
      // to suppress that.
      if (ProtoType::GetIsWrapperAnnotation(proto_type->descriptor()) ||
          ProtoType::GetIsStructAnnotation(proto_type->descriptor())) {
        proto_field->mutable_options()->SetExtension(googlesql::is_raw_proto,
                                                     true);
      }
      break;
    }
    case TYPE_STRUCT: {
      const google::protobuf::DescriptorProto* descriptor_proto;
      GOOGLESQL_RETURN_IF_ERROR(GetDescriptorForStruct(field_type->AsStruct(),
                                             &descriptor_proto));
      proto_field->set_type(google::protobuf::FieldDescriptorProto::TYPE_MESSAGE);
      proto_field->set_type_name(descriptor_proto->name());
      break;
    }
    case TYPE_ARRAY: {
      GOOGLESQL_RET_CHECK_EQ(label, google::protobuf::FieldDescriptorProto::LABEL_OPTIONAL)
          << "Arrays of repeateds not supported";
      const google::protobuf::DescriptorProto* descriptor_proto;
      if (options_.generate_nullable_array_wrappers) {
        // This field is not REPEATED. Repeatedness will be handled inside the
        // wrapper returned by GetDescriptorForArray.
        GOOGLESQL_RETURN_IF_ERROR(
            GetDescriptorForArray(field_type->AsArray(), &descriptor_proto));
      } else {
        // When not using a nullable array wrapper, simply generate an element
        // wrapper type. In this case the field will actually be repeated.
        GOOGLESQL_RETURN_IF_ERROR(GetWrapperDescriptorForType(
            field_type->AsArray()->element_type(), &descriptor_proto));
        proto_field->set_label(google::protobuf::FieldDescriptorProto::LABEL_REPEATED);
      }

      proto_field->set_type(google::protobuf::FieldDescriptorProto::TYPE_MESSAGE);
      proto_field->set_type_name(descriptor_proto->name());
      break;
    }
    case TYPE_MEASURE: {
      GOOGLESQL_RET_CHECK(!field_type->AsMeasure()->result_type()->IsMeasureType())
          << "Measures of measures are not supported";
      // We need the FieldDescriptorProto of the measure's result.
      GOOGLESQL_RETURN_IF_ERROR(MakeFieldDescriptor(
          field_type->AsMeasure()->result_type(), field_name,
          store_field_name_as_annotation, field_number, label, proto_field));
      proto_field->mutable_options()->SetExtension(googlesql::is_measure, true);
      break;
    }
    case TYPE_GRAPH_ELEMENT:
      // TODO: Implement this for GRAPH_ELEMENT.
      return absl::UnimplementedError(
          "GraphElementType type is not fully implemented");
    case TYPE_GRAPH_PATH:
      // TODO: Implement this for GRAPH_PATH.
      return absl::UnimplementedError(
          "GraphPathType type is not fully implemented");
    case TYPE_EXTENDED:
      // TODO: fix by moving this logic into Type class.
      return absl::UnimplementedError(
          "Extended types are not fully implemented");
    case TYPE_MAP:
      // TODO: Implement proto type conversion for Map.
      return absl::UnimplementedError(
          "Proto type conversion for Map is not yet implemented.");
    case TYPE_ROW:
      // TODO: Implement proto type conversion for Row.
      return absl::UnimplementedError(
          "Proto type conversion for ROW is not yet implemented.");
    case __TypeKind__switch_must_have_a_default__:
    case TYPE_UNKNOWN:
      break;  // Error generated below.
  }
  if (!proto_field->has_type()) {
    GOOGLESQL_RET_CHECK_FAIL() << "Invalid struct field type: "
                     << field_type->DebugString();
  }
  if (googlesql_base::ContainsKey(options_.field_format_map, field_type->kind())) {
    proto_field->mutable_options()->SetExtension(
        googlesql::format,
        googlesql_base::FindOrDie(options_.field_format_map, field_type->kind()));
  }
  return absl::OkStatus();
}

// Return true if this name can be used as a proto field name.
// This just allows alpha-numeric plus underscores, and disallows empty names.
// We also disallow names that start with '_field_' to avoid
// collisions with the generated names we use for fields and nested types.
static bool IsValidFieldName(const absl::string_view name) {
  if (name.empty()) return false;
  if (!isalpha(name[0]) && name[0] != '_') return false;
  if (absl::StartsWith(name, kGeneratedFieldNamePrefix)) return false;
  for (const char c : name) {
    if (!isalnum(c) && c != '_') return false;
  }
  return true;
}

absl::Status TypeToProtoConverter::MakeStructProto(
    const StructType* struct_type, absl::string_view name,
    google::protobuf::DescriptorProto* struct_proto) {
  GOOGLESQL_RET_CHECK_EQ(struct_proto->field_size(), 0);

  struct_proto->set_name(name);
  struct_proto->mutable_options()->SetExtension(googlesql::is_struct, true);
  struct_proto->mutable_options()->SetExtension(googlesql::use_field_defaults,
                                                false);

  // Avoid generating protos with duplicate field names.
  absl::btree_set<std::string, googlesql_base::CaseLess> field_names;

  for (int i = 0; i < struct_type->num_fields(); ++i) {
    const StructType::StructField& struct_field = struct_type->field(i);
    int field_number = i + 1;
    // Note that [19000, 20000) are reserved tag numbers for protobuf and we
    // cannot use them.
    if (field_number >= google::protobuf::FieldDescriptor::kFirstReservedNumber) {
      field_number += (google::protobuf::FieldDescriptor::kLastReservedNumber + 1 -
                       google::protobuf::FieldDescriptor::kFirstReservedNumber);
    }

    GOOGLESQL_RETURN_IF_ERROR(MakeFieldDescriptor(
        struct_field.type, struct_field.name,
        // store_field_name_as_annotation if field is anonymous, duplicate,
        // or invalid as a proto field name.
        (!IsValidFieldName(struct_field.name) ||
         !googlesql_base::InsertIfNotPresent(&field_names, struct_field.name)),
        field_number /* field_number */,
        google::protobuf::FieldDescriptorProto::LABEL_OPTIONAL, struct_proto));
  }

  return absl::OkStatus();
}

absl::Status TypeToProtoConverter::MakeArrayProto(
    const ArrayType* array_type, absl::string_view name,
    google::protobuf::DescriptorProto* array_proto) {
  GOOGLESQL_RET_CHECK_EQ(array_proto->field_size(), 0);

  // We make the wrapper proto storing the repeated field.
  // This wrapper can be optional to support a NULL array.
  array_proto->set_name(name);
  array_proto->mutable_options()->SetExtension(googlesql::is_wrapper, true);

  if (options_.generate_nullable_element_wrappers) {
    // We need to wrap the element with a proto to support NULL elements.
    const google::protobuf::DescriptorProto* element_proto;
    GOOGLESQL_RETURN_IF_ERROR(GetWrapperDescriptorForType(array_type->element_type(),
                                                &element_proto));

    google::protobuf::FieldDescriptorProto* proto_field = array_proto->add_field();
    proto_field->set_name("value");
    proto_field->set_number(1);
    proto_field->set_label(google::protobuf::FieldDescriptorProto::LABEL_REPEATED);

    proto_field->set_type(google::protobuf::FieldDescriptorProto::TYPE_MESSAGE);
    proto_field->set_type_name(element_proto->name());
  } else {
    // If nullable element wrappers are disabled, generate the repeated element
    // without a wrapper.
    GOOGLESQL_RETURN_IF_ERROR(MakeFieldDescriptor(
        array_type->element_type(), "value", false, 1,
        google::protobuf::FieldDescriptorProto::LABEL_REPEATED, array_proto));
  }

  return absl::OkStatus();
}

absl::Status TypeToProtoConverter::GetDescriptorForStruct(
    const StructType* struct_type,
    const google::protobuf::DescriptorProto** descriptor_proto) {
  google::protobuf::DescriptorProto* struct_proto = nullptr;
  google::protobuf::DescriptorProto** cached_struct_proto = &struct_proto;
  if (options_.consolidate_constructed_types) {
    cached_struct_proto = &constructed_structs_[struct_type];
  }
  if (*cached_struct_proto == nullptr) {
    struct_proto = main_message_->add_nested_type();
    GOOGLESQL_RETURN_IF_ERROR(MakeStructProto(
        struct_type, absl::StrCat("_Struct", ++unique_name_counter_),
        struct_proto));
    *cached_struct_proto = struct_proto;
  }
  *descriptor_proto = *cached_struct_proto;
  return absl::OkStatus();
}

absl::Status TypeToProtoConverter::GetWrapperDescriptorForType(
    const Type* type,
    const google::protobuf::DescriptorProto** descriptor_proto) {
  // ArrayTypes are not handled here.  We build different looking protos for
  // them in GetDescriptorForArray.
  GOOGLESQL_RET_CHECK(!type->IsArray()) << type->DebugString();

  google::protobuf::DescriptorProto* wrapper_proto = nullptr;
  google::protobuf::DescriptorProto** cached_wrapper_proto = &wrapper_proto;
  if (options_.consolidate_constructed_types) {
    cached_wrapper_proto = &constructed_wrappers_[type];
  }
  if (*cached_wrapper_proto == nullptr) {
    wrapper_proto = main_message_->add_nested_type();

    wrapper_proto->set_name(
        absl::StrCat("_NullableObject", ++unique_name_counter_));
    wrapper_proto->mutable_options()->SetExtension(googlesql::is_wrapper, true);
    wrapper_proto->mutable_options()->SetExtension(
        googlesql::use_field_defaults, false);

    GOOGLESQL_RETURN_IF_ERROR(MakeFieldDescriptor(
        type,
        "value",
        false /* store_field_name_as_annotation */,
        1 /* field_number */,
        google::protobuf::FieldDescriptorProto::LABEL_OPTIONAL,
        wrapper_proto));
    *cached_wrapper_proto = wrapper_proto;
  }

  *descriptor_proto = *cached_wrapper_proto;
  return absl::OkStatus();
}

absl::Status TypeToProtoConverter::GetDescriptorForArray(
    const ArrayType* array_type,
    const google::protobuf::DescriptorProto** descriptor_proto) {
  google::protobuf::DescriptorProto* array_proto = nullptr;
  google::protobuf::DescriptorProto** cached_array_proto = &array_proto;
  if (options_.consolidate_constructed_types) {
  // Drive the cache off element_type since we'll get slightly better
  // re-use that way.
    cached_array_proto = &constructed_arrays_[array_type->element_type()];
  }
  if (*cached_array_proto == nullptr) {
    array_proto = main_message_->add_nested_type();
    GOOGLESQL_RETURN_IF_ERROR(MakeArrayProto(
        array_type, absl::StrCat("_NullableArray", ++unique_name_counter_),
        array_proto));
    *cached_array_proto = array_proto;
  }
  *descriptor_proto = *cached_array_proto;
  return absl::OkStatus();
}

absl::Status TypeToProtoConverter::MakeFileDescriptorProto(
    const Type* type, TableType table_type, google::protobuf::FileDescriptorProto* file) {
  file->Clear();
  // The output_field_descriptor_map makes the assumption that pointers from
  // repeated message type fields are stable, e.g. that it is valid to obtain a
  // pointer to an element of a repeated field, add additional elements, and
  // then continue to interact with the original pointer. This is an effect of
  // the RepeatedPtrField implementation, since it maintains a vector of element
  // pointers, but is not explicitly documented anywhere.
  if (options_.output_field_descriptor_map != nullptr) {
    options_.output_field_descriptor_map->clear();
  }

  if (table_type == SQL_TABLE) {
    if (!type->IsStruct()) {
      return MakeSqlError()
             << "SQL table row type must be a struct, but got type "
             << type->DebugString();
    }
    absl::btree_set<std::string, googlesql_base::CaseLess> field_names;
    for (const StructType::StructField& field : type->AsStruct()->fields()) {
      if (!options_.sql_table_options.allow_anonymous_field_name &&
          field.name.empty()) {
        return MakeSqlError()
               << "Cannot make SQL table from struct with anonymous columns: "
               << type->DebugString();
      }
      if (!options_.sql_table_options.allow_duplicate_field_names &&
          !googlesql_base::InsertIfNotPresent(&field_names, field.name)) {
        return MakeSqlError()
               << "Cannot make SQL table from struct with duplicate "
                  "column names: "
               << type->DebugString();
      }
    }
  }

  import_filenames_.insert(
      "googlesql/public/proto/type_annotation.proto");

  if (table_type == DEFAULT_TABLE_TYPE || type->IsStruct()) {
    main_message_ = file->add_message_type();
    if (type->IsStruct()) {
      GOOGLESQL_RETURN_IF_ERROR(MakeStructProto(type->AsStruct(), options_.message_name,
                                      main_message_));
    } else if (type->IsArray()) {
      GOOGLESQL_RETURN_IF_ERROR(MakeArrayProto(type->AsArray(), options_.message_name,
                                     main_message_));
    } else {
      GOOGLESQL_RET_CHECK_FAIL() << "MakeFileDescriptorProto called on type "
                       << type->DebugString();
    }
  } else {
    GOOGLESQL_RET_CHECK_EQ(table_type, VALUE_TABLE);

    // We have a scalar type, so we'll need to make a wrapper message to
    // store it.
    main_message_ = file->add_message_type();
    main_message_->set_name(options_.message_name);
    main_message_->mutable_options()->SetExtension(googlesql::is_wrapper, true);

    GOOGLESQL_RETURN_IF_ERROR(MakeFieldDescriptor(
        type,
        "_value" /* field_name */,
        false /* store_field_name_as_annotation */,
        1 /* field_number */,
        google::protobuf::FieldDescriptorProto::LABEL_OPTIONAL,
        main_message_));
  }

  if (table_type != DEFAULT_TABLE_TYPE) {
    // Always set the googlesql.table_type extension on the top-level message
    // for value tables.
    // For proto value tables, this means adding it onto the existing message.
    main_message_->mutable_options()->SetExtension(googlesql::table_type,
                                                   table_type);
  }

  if (options_.add_import_statements) {
    for (const std::string& import_filename : import_filenames_) {
      file->add_dependency(import_filename);
    }
  }

  return absl::OkStatus();
}

absl::Status ConvertStructToProto(
    const StructType* struct_type,
    google::protobuf::FileDescriptorProto* file,
    const ConvertTypeToProtoOptions& options) {
  TypeToProtoConverter converter(options);
  return converter.MakeFileDescriptorProto(struct_type, DEFAULT_TABLE_TYPE,
                                           file);
}

absl::Status ConvertArrayToProto(
    const ArrayType* array_type,
    google::protobuf::FileDescriptorProto* file,
    const ConvertTypeToProtoOptions& options) {
  TypeToProtoConverter converter(options);
  return converter.MakeFileDescriptorProto(array_type, DEFAULT_TABLE_TYPE,
                                           file);
}

absl::Status ConvertTableToProto(
    absl::Span<const std::pair<std::string, const Type*>> columns,
    bool is_value_table, google::protobuf::FileDescriptorProto* file,
    const ConvertTypeToProtoOptions& options) {
  TypeFactory type_factory;
  const Type* row_type;
  if (is_value_table) {
    GOOGLESQL_RET_CHECK_EQ(columns.size(), 1)
        << "Value table must have exactly one column";
    row_type = columns[0].second;
  } else {
    std::vector<StructType::StructField> fields;
    fields.reserve(columns.size());
    for (const auto& column : columns) {
      fields.push_back(StructType::StructField(column.first, column.second));
    }
    GOOGLESQL_RETURN_IF_ERROR(type_factory.MakeStructType(fields, &row_type));
  }
  return ConvertTableToProto(row_type, is_value_table, file, options);
}

absl::Status ConvertTableToProto(
    const Type* row_type,
    bool is_value_table,
    google::protobuf::FileDescriptorProto* file,
    const ConvertTypeToProtoOptions& options) {
  // Shortcut for proto value tables.  We don't need to generate a new proto.
  // We just copy the existing proto's Descriptor and add on the
  // googlesql.type_table annotation.
  if (is_value_table && row_type->IsProto()) {
    row_type->AsProto()->descriptor()->file()->CopyTo(file);
    return AddValueTableAnnotationForProto(
        row_type->AsProto()->descriptor()->full_name(), file);
  }

  TypeToProtoConverter converter(options);
  return converter.MakeFileDescriptorProto(
      row_type, is_value_table ? VALUE_TABLE : SQL_TABLE, file);
}

// Recursive implementation of FindDescriptorProto.  Handles nested message
// definitions.  <message_names> is the full_name with the package stripped
// off, split into parts.
static google::protobuf::DescriptorProto* FindDescriptorProtoImpl(
    const std::vector<std::string>& message_names, int message_names_at,
    google::protobuf::DescriptorProto* current_message) {
  if (current_message->name() != message_names[message_names_at]) {
    return nullptr;
  }
  if (message_names_at + 1 == message_names.size()) {
    return current_message;
  }
  for (int i = 0; i < current_message->nested_type_size(); ++i) {
    google::protobuf::DescriptorProto* found =
        FindDescriptorProtoImpl(message_names, message_names_at + 1,
                                current_message->mutable_nested_type(i));
    if (found != nullptr) {
      return found;
    }
  }
  return nullptr;
}

// Find a proto message descriptor inside <file> with name <full_name>.
// Return NULL if not found.
static google::protobuf::DescriptorProto* FindDescriptorProto(
    google::protobuf::FileDescriptorProto* file, absl::string_view full_name) {
  std::vector<std::string> message_names;
  if (!file->package().empty()) {
    if (!absl::StartsWith(full_name, absl::StrCat(file->package(), "."))) {
      return nullptr;
    }
    message_names =
        absl::StrSplit(full_name.substr(file->package().size() + 1), '.');
  } else {
    message_names = absl::StrSplit(full_name, '.');
  }

  for (int i = 0; i < file->message_type_size(); ++i) {
    google::protobuf::DescriptorProto* found =
        FindDescriptorProtoImpl(message_names, 0,
                                file->mutable_message_type(i));
    if (found != nullptr) {
      return found;
    }
  }
  return nullptr;
}

absl::Status AddValueTableAnnotationForProto(
    absl::string_view message_full_name, google::protobuf::FileDescriptorProto* file) {
  google::protobuf::DescriptorProto* message = FindDescriptorProto(file,
                                                         message_full_name);
  if (message == nullptr) {
    return MakeSqlError() << "Message " << message_full_name
                          << " not found in FileDescriptorProto";
  }
  message->mutable_options()->SetExtension(googlesql::table_type, VALUE_TABLE);
  return absl::OkStatus();
}

}  // namespace googlesql
