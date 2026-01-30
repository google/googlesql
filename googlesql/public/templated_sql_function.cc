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

#include "googlesql/public/templated_sql_function.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/proto/function.pb.h"
#include "googlesql/proto/internal_error_location.pb.h"
#include "googlesql/public/error_location.pb.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/parse_resume_location.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_deserializer.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

// static
const char TemplatedSQLFunction::kTemplatedSQLFunctionGroup[] =
    "Templated_SQL_Function";

TemplatedSQLFunction::TemplatedSQLFunction(
    const std::vector<std::string>& function_name_path,
    const FunctionSignature& signature,
    const std::vector<std::string>& argument_names,
    const ParseResumeLocation& parse_resume_location, Mode mode,
    const FunctionOptions& options)
    : Function(function_name_path, kTemplatedSQLFunctionGroup, mode,
               {signature}, options),
      argument_names_(argument_names),
      parse_resume_location_(parse_resume_location) {
  GOOGLESQL_CHECK_OK(signature.IsValidForFunction());
}

// static
absl::Status TemplatedSQLFunction::Deserialize(
    const FunctionProto& proto,
    const std::vector<const google::protobuf::DescriptorPool*>& pools,
    TypeFactory* factory, std::unique_ptr<Function>* result) {
  std::vector<std::string> name_path;
  for (const std::string& name : proto.name_path()) {
    name_path.push_back(name);
  }

  // Note that GoogleSQL does not yet support overloaded templated functions.
  // So we check that there is exactly one signature and retrieve it.
  GOOGLESQL_RET_CHECK_EQ(1, proto.signature_size()) << proto.DebugString();
  std::unique_ptr<FunctionSignature> function_signature;
  GOOGLESQL_ASSIGN_OR_RETURN(function_signature,
                   FunctionSignature::Deserialize(
                       proto.signature(0), TypeDeserializer(factory, pools)));

  std::vector<std::string> argument_names;
  for (const std::string& name : proto.templated_sql_function_argument_name()) {
    argument_names.push_back(name);
  }

  std::unique_ptr<FunctionOptions> function_options;
  GOOGLESQL_RETURN_IF_ERROR(
      FunctionOptions::Deserialize(proto.options(), &function_options));

  GOOGLESQL_RET_CHECK(proto.has_parse_resume_location()) << proto.DebugString();
  *result = std::make_unique<TemplatedSQLFunction>(
      name_path, *function_signature, argument_names,
      ParseResumeLocation::FromProto(proto.parse_resume_location()),
      proto.mode(), *function_options);
  return absl::OkStatus();
}

absl::Status TemplatedSQLFunction::Serialize(
    FileDescriptorSetMap* file_descriptor_set_map, FunctionProto* proto,
    bool omit_signatures) const {
  GOOGLESQL_RETURN_IF_ERROR(
      Function::Serialize(file_descriptor_set_map, proto, omit_signatures));
  parse_resume_location_.Serialize(proto->mutable_parse_resume_location());
  for (const std::string& name : argument_names_) {
    proto->add_templated_sql_function_argument_name(name);
  }
  return absl::OkStatus();
}

TemplatedSQLFunctionCall::TemplatedSQLFunctionCall(
    std::unique_ptr<const ResolvedExpr> expr,
    std::vector<std::unique_ptr<const ResolvedComputedColumn>>
        aggregate_expr_list)
    : expr_(expr.release()),
      aggregate_expression_list_(std::move(aggregate_expr_list)) {}

const std::vector<std::unique_ptr<const ResolvedComputedColumn>>&
TemplatedSQLFunctionCall::aggregate_expression_list() const {
  return aggregate_expression_list_;
}

inline void ResolvedComputedColumnFormatter(
    std::string* out,
    const std::unique_ptr<const ResolvedComputedColumn>& computed_column) {
  absl::StrAppend(out, computed_column->DebugString());
}

std::string TemplatedSQLFunctionCall::DebugString() const {
  return absl::StrCat("TemplatedSQLFunctionCall expr: ",
                      (expr_ == nullptr ? "nullptr" : expr_->DebugString()),
                      "\naggregate_expression_list:\n",
                      absl::StrJoin(aggregate_expression_list_, "\n",
                                    ResolvedComputedColumnFormatter));
}

namespace {
static bool module_initialization_complete = []() {
  Function::RegisterDeserializer(
      TemplatedSQLFunction::kTemplatedSQLFunctionGroup,
      TemplatedSQLFunction::Deserialize);
  return true;
} ();
}  // namespace

}  // namespace googlesql
