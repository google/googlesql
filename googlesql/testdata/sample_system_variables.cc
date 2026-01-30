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

#include "googlesql/testdata/sample_system_variables.h"

#include <string>
#include <vector>

#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/type.h"
#include "googlesql/public/types/proto_type.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "googlesql/base/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_join.h"

namespace googlesql {

static absl::Status AddSystemVariable(const std::vector<std::string>& name_path,
                                      const Type* type,
                                      AnalyzerOptions* options) {
  if (!type->IsSupportedType(options->language())) {
    // Skip system variable whose type is not supported by the language.  This
    // is to prevent tests which use a reduced feature set (which don't test
    // system variables) from being blocked by failure to set them.
    GOOGLESQL_VLOG(1) << "Skipping system variable " << absl::StrJoin(name_path, ".")
            << " due to unsupported type: " << type->DebugString();
    return absl::OkStatus();
  }
  GOOGLESQL_VLOG(1) << "Adding system variable " << absl::StrJoin(name_path, ".")
          << " of type: " << type->DebugString();
  return options->AddSystemVariable(name_path, type);
}

void SetupSampleSystemVariables(TypeFactory* type_factory,
                                AnalyzerOptions* options) {
  const Type* bool_type = type_factory->get_bool();
  const Type* int32_type = type_factory->get_int32();
  const Type* int64_type = type_factory->get_int64();
  const Type* uint64_type = type_factory->get_uint64();
  const Type* string_type = type_factory->get_string();
  const Type* timestamp_type = type_factory->get_timestamp();
  const Type* int64_array_type;
  GOOGLESQL_CHECK_OK(type_factory->MakeArrayType(int64_type, &int64_array_type));

  const StructType* struct_type;
  const StructType* nested_struct_type;
  const StructType* foo_struct_type;
  const StructType* with_struct_type;
  const ProtoType* proto_type;
  const ProtoType* proto_map_element_type;
  const Type* array_of_map_element_type;
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType({{"a", int32_type}, {"b", string_type}},
                                        &struct_type));
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType({{"c", int32_type}, {"d", struct_type}},
                                        &nested_struct_type));
  GOOGLESQL_CHECK_OK(
      type_factory->MakeStructType({{"bar", int32_type}}, &foo_struct_type));
  GOOGLESQL_CHECK_OK(
      type_factory->MakeStructType({{"with", int32_type}}, &with_struct_type));
  GOOGLESQL_CHECK_OK(type_factory->MakeProtoType(
      googlesql_test::KitchenSinkPB::descriptor(), &proto_type));
  GOOGLESQL_CHECK_OK(type_factory->MakeProtoType(
      googlesql_test::MessageWithMapField::descriptor()
          ->FindFieldByName("string_int32_map")
          ->message_type(),
      &proto_map_element_type));
  GOOGLESQL_CHECK_OK(type_factory->MakeArrayType(proto_map_element_type,
                                       &array_of_map_element_type));

  GOOGLESQL_CHECK_OK(AddSystemVariable({"bool_system_variable"}, bool_type, options));
  GOOGLESQL_CHECK_OK(AddSystemVariable({"int32_system_variable"}, int32_type, options));
  GOOGLESQL_CHECK_OK(AddSystemVariable({"int64_array_system_variable"}, int64_array_type,
                             options));
  GOOGLESQL_CHECK_OK(AddSystemVariable({"int64_system_variable"}, int64_type, options));
  GOOGLESQL_CHECK_OK(AddSystemVariable({"uint64_system_variable"}, uint64_type, options));
  GOOGLESQL_CHECK_OK(AddSystemVariable({"struct_system_variable"}, nested_struct_type,
                             options));
  GOOGLESQL_CHECK_OK(AddSystemVariable({"sysvar_foo"}, foo_struct_type, options));
  GOOGLESQL_CHECK_OK(AddSystemVariable({"sysvar_foo", "bar"}, int64_type, options));
  GOOGLESQL_CHECK_OK(
      AddSystemVariable({"sysvar_foo", "bar", "baz"}, int64_type, options));
  GOOGLESQL_CHECK_OK(AddSystemVariable({"proto_system_variable"}, proto_type, options));
  GOOGLESQL_CHECK_OK(AddSystemVariable({"error", "message"}, string_type, options));
  GOOGLESQL_CHECK_OK(AddSystemVariable({"sysvar.with.dots"}, string_type, options));
  GOOGLESQL_CHECK_OK(AddSystemVariable({"namespace.with.dots", "sysvar"}, string_type,
                             options));
  GOOGLESQL_CHECK_OK(AddSystemVariable({"map_system_variable"}, array_of_map_element_type,
                             options));
  GOOGLESQL_CHECK_OK(AddSystemVariable({"sysvar.part1.part2"}, string_type, options));
  GOOGLESQL_CHECK_OK(
      AddSystemVariable({"sysvar", "part1", "part2"}, string_type, options));
  GOOGLESQL_CHECK_OK(AddSystemVariable({"timestamp_system_variable"}, timestamp_type,
                             options));
  // System var names that are reserved keywords are supported.
  GOOGLESQL_CHECK_OK(AddSystemVariable({"from"}, nested_struct_type, options));
  GOOGLESQL_CHECK_OK(AddSystemVariable({"where", "d"}, nested_struct_type, options));
  GOOGLESQL_CHECK_OK(AddSystemVariable({"where"}, nested_struct_type, options));
  GOOGLESQL_CHECK_OK(AddSystemVariable({"union", "d.with"}, with_struct_type, options));
  GOOGLESQL_CHECK_OK(
      AddSystemVariable({"group", "with_dots"}, nested_struct_type, options));
  GOOGLESQL_CHECK_OK(AddSystemVariable({"having"}, with_struct_type, options));
  GOOGLESQL_CHECK_OK(
      AddSystemVariable({"order.with", "dots"}, nested_struct_type, options));
  GOOGLESQL_CHECK_OK(AddSystemVariable({"order.w", "dots"}, nested_struct_type, options));
  // Not reserved, just used to compare to the reserved keyword case
  GOOGLESQL_CHECK_OK(AddSystemVariable({"fromx"}, nested_struct_type, options));
  GOOGLESQL_CHECK_OK(AddSystemVariable({"wherex", "d"}, nested_struct_type, options));
  GOOGLESQL_CHECK_OK(AddSystemVariable({"wherex"}, nested_struct_type, options));
  GOOGLESQL_CHECK_OK(AddSystemVariable({"unionx", "d.with"}, with_struct_type, options));
  GOOGLESQL_CHECK_OK(
      AddSystemVariable({"groupx", "with_dots"}, nested_struct_type, options));
  GOOGLESQL_CHECK_OK(AddSystemVariable({"havingx"}, with_struct_type, options));
  GOOGLESQL_CHECK_OK(
      AddSystemVariable({"orderx.w", "dots"}, nested_struct_type, options));
}
}  // namespace googlesql
