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

#ifndef GOOGLESQL_PUBLIC_PROCEDURE_H_
#define GOOGLESQL_PUBLIC_PROCEDURE_H_

#include <memory>
#include <string>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/proto/function.pb.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/type.h"
#include "googlesql/public/types/type_deserializer.h"
#include "absl/strings/str_join.h"

namespace googlesql {

// This class describes "callable" procedures available in a query engine.
//
// Procedures can only be invoked with CALL statement, e.g.:
//   CALL <procedure_name>(<argument_list>);
//
// Procedure has a FunctionSignature which defines arguments of the procedure.
// Note, FunctionSignature also defines return type which is ignored and not
// validated by GoogleSQL analyzer for Procedure since it can only be invoked
// by CALL statement.
class Procedure {
 public:
  Procedure(const std::vector<std::string>& name_path,
            const FunctionSignature& signature)
      : name_path_(name_path), signature_(signature) {
    GOOGLESQL_CHECK_OK(signature.IsValidForProcedure());
  }
  virtual ~Procedure() = default;

  Procedure(const Procedure&) = delete;
  Procedure& operator=(const Procedure&) = delete;

  // Returns the name of this Procedure.
  const std::string& Name() const { return name_path_.back(); }
  std::string FullName() const { return absl::StrJoin(name_path(), "."); }
  const std::vector<std::string>& name_path() const { return name_path_; }

  // Returns the argument signature of this procedure;
  const FunctionSignature& signature() const { return signature_; }

  static absl::StatusOr<std::unique_ptr<Procedure>> Deserialize(
      const ProcedureProto& proto, const TypeDeserializer& type_deserializer);

  absl::Status Serialize(FileDescriptorSetMap* file_descriptor_set_map,
                         ProcedureProto* proto) const;

  // Returns a relevant (customizable) user facing text (to be used in error
  // message) for procedure signature. For example:
  // "PROCEDURE_NAME(DATE, DATE, DATE_TIME_PART)"
  std::string GetSupportedSignatureUserFacingText(
      ProductMode product_mode) const;

 private:
  const std::vector<std::string> name_path_;
  const FunctionSignature signature_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_PROCEDURE_H_
