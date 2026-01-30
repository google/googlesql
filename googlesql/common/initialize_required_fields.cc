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

#include "googlesql/common/initialize_required_fields.h"

#include <set>
#include <string>
#include <utility>
#include <vector>

#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"

namespace googlesql {

bool InitializeMissingRequiredFields(google::protobuf::Message* message,
                                     std::set<std::string>* visited);

bool InitializeRequiredField(const google::protobuf::Reflection* reflection,
                             const google::protobuf::FieldDescriptor* field,
                             google::protobuf::Message* message,
                             std::set<std::string>* visited) {
  bool has_cycle = false;
  // First we recurse into subfields, if they exist...
  if (field->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE &&
      field->is_repeated()) {
    // Loop over repeated fields.
    for (int j = 0; j < reflection->FieldSize(*message, field); ++j) {
      has_cycle |= !InitializeMissingRequiredFields(
          reflection->MutableRepeatedMessage(message, field, j), visited);
    }
  } else if (field->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE &&
             reflection->HasField(*message, field)) {
    // Simply recurse into non-repeated fields.
    has_cycle |= !InitializeMissingRequiredFields(
        reflection->MutableMessage(message, field), visited);
  } else if (field->is_required() && !reflection->HasField(*message, field)) {
    // Now we actually handle the setting of unset fields.
    switch (field->cpp_type()) {
      case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
        reflection->SetInt32(message, field, field->default_value_int32());
        break;
      case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
        reflection->SetInt64(message, field, field->default_value_int64());
        break;
      case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
        reflection->SetUInt32(message, field, field->default_value_uint32());
        break;
      case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
        reflection->SetUInt64(message, field, field->default_value_uint64());
        break;
      case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
        reflection->SetDouble(message, field, field->default_value_double());
        break;
      case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
        reflection->SetFloat(message, field, field->default_value_float());
        break;
      case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
        reflection->SetBool(message, field, field->default_value_bool());
        break;
      case google::protobuf::FieldDescriptor::CPPTYPE_ENUM:
        reflection->SetEnum(message, field, field->default_value_enum());
        break;
      case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
        reflection->SetString(message, field,
                              std::string(field->default_value_string()));
        break;
      case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE:
        has_cycle |= !InitializeMissingRequiredFields(
            reflection->MutableMessage(message, field), visited);
        break;
    }
  }
  return has_cycle;
}

bool InitializeMissingRequiredFields(google::protobuf::Message* message,
                                     std::set<std::string>* visited) {
  const google::protobuf::Descriptor* descriptor = message->GetDescriptor();
  std::pair<std::set<std::string>::iterator, bool> it;
  bool has_cycle = false;

  if (visited != nullptr) {
    it = visited->emplace(descriptor->full_name());
    if (it.second == false) {
      // Skip this field because otherwise we end up in a cycle.
      return false;
    }
  }

  const google::protobuf::Reflection* reflection = message->GetReflection();
  for (int i = 0; i < descriptor->field_count(); ++i) {
    const google::protobuf::FieldDescriptor* field = descriptor->field(i);
    has_cycle |= InitializeRequiredField(reflection, field, message, visited);
  }
  std::vector<const google::protobuf::FieldDescriptor*> extensions;
  descriptor->file()->pool()->FindAllExtensions(descriptor, &extensions);
  for (int i = 0; i < extensions.size(); ++i) {
    const google::protobuf::FieldDescriptor* field = extensions[i];
    has_cycle |= InitializeRequiredField(reflection, field, message, visited);
  }
  if (visited != nullptr) {
    visited->erase(it.first);
  }
  return !has_cycle;
}

void InitializeRequiredFields(google::protobuf::Message* message) {
  InitializeMissingRequiredFields(message, nullptr);
}

}  // namespace googlesql
