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

#include "googlesql/testing/type_util.h"

#include <string>
#include <vector>

#include "google/protobuf/duration.pb.h"
#include "google/protobuf/timestamp.pb.h"
#include "google/protobuf/wrappers.pb.h"
#include "google/type/date.pb.h"
#include "google/type/latlng.pb.h"
#include "google/type/timeofday.pb.h"
#include "google/protobuf/descriptor.pb.h"
#include "googlesql/compliance/test_util.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/testdata/test_proto3.pb.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "googlesql/base/check.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/descriptor.h"

namespace googlesql {
namespace testing {

bool HasFloatingPointNumber(const googlesql::Type* type) {
  if (type->IsArray()) {
    return HasFloatingPointNumber(type->AsArray()->element_type());
  } else if (type->IsStruct()) {
    for (int i = 0; i < type->AsStruct()->num_fields(); i++) {
      if (HasFloatingPointNumber(type->AsStruct()->field(i).type)) {
        return true;
      }
    }
    return false;
  } else if (type->kind() == googlesql::TYPE_DOUBLE ||
             type->kind() == googlesql::TYPE_FLOAT) {
    return true;
  } else {
    return false;
  }
}

std::vector<const Type*> GoogleSqlComplexTestTypes(
    googlesql::TypeFactory* type_factory,
    const google::protobuf::DescriptorPool* descriptor_pool) {
  for (const std::string& filepath : GoogleSqlTestProtoFilepaths()) {
    descriptor_pool->FindFileByName(filepath);
  }

  std::vector<const Type*> output;

  // Builtin enums & protos should only use the generated pool.
  for (const auto& [name, enum_descriptor] : GetBuiltinEnumDescriptors()) {
    GOOGLESQL_CHECK_OK(
        type_factory->MakeEnumType(enum_descriptor, &output.emplace_back()));
  }
  for (const auto& [name, proto_descriptor] : GetBuiltinProtoDescriptors()) {
    GOOGLESQL_CHECK_OK(
        type_factory->MakeProtoType(proto_descriptor, &output.emplace_back()));
  }

  for (const std::string& proto_name : GoogleSqlRandomTestProtoNames()) {
    // Protos from builtins are already loaded from the generated pool.
    if (GetBuiltinProtoDescriptors().contains(proto_name)) {
      continue;
    }
    const google::protobuf::Descriptor* descriptor =
        descriptor_pool->FindMessageTypeByName(proto_name);
    ABSL_CHECK(descriptor != nullptr)
        << "Cannot find Proto Message Type: " << proto_name
        << ", available files: "
        << absl::StrJoin(GoogleSqlTestProtoFilepaths(), ",");

    const Type* proto_type;
    GOOGLESQL_CHECK_OK(type_factory->MakeProtoType(descriptor, &proto_type));
    output.push_back(proto_type);
  }

  for (const std::string& enum_name : GoogleSqlTestEnumNames()) {
    // Enums from builtins are already loaded from the generated pool.
    if (GetBuiltinEnumDescriptors().contains(enum_name)) {
      continue;
    }
    const google::protobuf::EnumDescriptor* descriptor =
        descriptor_pool->FindEnumTypeByName(enum_name);
    ABSL_CHECK(descriptor != nullptr)
        << "Cannot find Enum Type: " << enum_name << ", available files: "
        << absl::StrJoin(GoogleSqlTestProtoFilepaths(), ",");
    const Type* enum_type;
    GOOGLESQL_CHECK_OK(type_factory->MakeEnumType(descriptor, &enum_type));
    output.push_back(enum_type);
  }

  const Type* struct_int64_type;
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType(
      {{"int64_val", googlesql::types::Int64Type()}}, &struct_int64_type));
  output.push_back(struct_int64_type);

  return output;
}

std::vector<std::string> GoogleSqlTestProtoFilepaths() {
  // `rounding_mode`, `array_find_mode`, `array_zip_mode`, `bitwise_agg_mode`
  // fixes `Enum not found` error in RQG / RSG: b/293474126.
  return {"googlesql/public/functions/rounding_mode.proto",
          "googlesql/public/functions/array_find_mode.proto",
          "googlesql/public/functions/array_zip_mode.proto",
          "googlesql/public/functions/bitwise_agg_mode.proto",
          "googlesql/testdata/test_schema.proto",
          "googlesql/testdata/test_proto3.proto",
          "google/protobuf/duration.proto",
          "google/protobuf/timestamp.proto",
          "google/protobuf/wrappers.proto",
          "google/type/latlng.proto",
          "google/type/timeofday.proto",
          "google/type/date.proto"};
}

std::vector<std::string> GoogleSqlTestProtoNames() {
  return {"googlesql_test.KitchenSinkPB",
          "googlesql_test.MessageWithMapField",
          "googlesql_test.MessageWithMapField.StringInt32MapEntry",
          "googlesql_test.CivilTimeTypesSinkPB",
          "googlesql_test.RecursiveMessage",
          "googlesql_test.RecursivePB",
          "googlesql_test.Proto3KitchenSink",
          "googlesql_test.Proto3KitchenSink.Nested",
          "googlesql_test.Proto3MessageWithNulls",
          "googlesql_test.EmptyMessage",
          "googlesql_test.Proto3TestExtraPB",
          "google.protobuf.Timestamp",
          "google.protobuf.Duration",
          "google.type.Date",
          "google.type.TimeOfDay",
          "google.type.LatLng",
          "google.protobuf.DoubleValue",
          "google.protobuf.FloatValue",
          "google.protobuf.Int64Value",
          "google.protobuf.UInt64Value",
          "google.protobuf.Int32Value",
          "google.protobuf.UInt32Value",
          "google.protobuf.BoolValue",
          "google.protobuf.StringValue",
          "google.protobuf.BytesValue"};
}

std::vector<std::string> GoogleSqlRandomTestProtoNames() {
  return {
      "googlesql_test.KitchenSinkPB",  // Formatted one per line.
      // TODO: b/281436434 - Add OneofProto back once the bug is fixed.
      // "googlesql_test.OneofProto",
      "google.protobuf.Timestamp",
      "google.protobuf.Duration",
      "google.type.Date",
      "google.type.TimeOfDay",
      "google.type.LatLng",
      "google.protobuf.DoubleValue",
      "google.protobuf.FloatValue",
      "google.protobuf.Int64Value",
      "google.protobuf.UInt64Value",
      "google.protobuf.Int32Value",
      "google.protobuf.UInt32Value",
      "google.protobuf.BoolValue",
      "google.protobuf.StringValue",
      "google.protobuf.BytesValue",
  };
}

std::vector<std::string> GoogleSqlTestEnumNames() {
  return {"googlesql_test.TestEnum", "googlesql_test.AnotherTestEnum",
          "googlesql.functions.RoundingMode",
          "googlesql.functions.ArrayFindEnums.ArrayFindMode",
          "googlesql.functions.ArrayZipEnums.ArrayZipMode"};
}

}  // namespace testing
}  // namespace googlesql
