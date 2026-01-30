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

#include "googlesql/public/procedure.h"

#include <memory>
#include <string>
#include <vector>

#include "googlesql/proto/function.pb.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_deserializer.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

// static
absl::StatusOr<std::unique_ptr<Procedure>> Procedure::Deserialize(
    const ProcedureProto& proto, const TypeDeserializer& type_deserializer) {
  std::vector<std::string> name_path;
  for (const std::string& name : proto.name_path()) {
    name_path.push_back(name);
  }

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<FunctionSignature> signature,
      FunctionSignature::Deserialize(proto.signature(), type_deserializer));
  return std::make_unique<Procedure>(name_path, *signature);
}

absl::Status Procedure::Serialize(FileDescriptorSetMap* file_descriptor_set_map,
                                  ProcedureProto* proto) const {
  for (const std::string& name : name_path()) {
    proto->add_name_path(name);
  }

  GOOGLESQL_RETURN_IF_ERROR(signature_.Serialize(file_descriptor_set_map,
                                       proto->mutable_signature()));

  return absl::OkStatus();
}

std::string Procedure::GetSupportedSignatureUserFacingText(
    ProductMode product_mode) const {
  std::vector<std::string> argument_texts;
  for (const FunctionArgumentType& argument : signature_.arguments()) {
    argument_texts.push_back(argument.UserFacingNameWithCardinality(
        product_mode, FunctionArgumentType::NamePrintingStyle::kIfNamedOnly,
        /*print_template_details=*/true));
  }
  return absl::StrCat(FullName(), "(", absl::StrJoin(argument_texts, ", "),
                      ")");
}

}  // namespace googlesql
