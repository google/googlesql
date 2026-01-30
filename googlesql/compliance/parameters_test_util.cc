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

#include "googlesql/compliance/parameters_test_util.h"

#include <map>
#include <memory>
#include <string>

#include "googlesql/compliance/test_driver.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/reference_impl/reference_driver.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

absl::Status ParseTestFileParameters(absl::string_view param_string,
                                     ReferenceDriver* reference_driver,
                                     std::map<std::string, Value>* parameters) {
  parameters->clear();

  const std::string sql = absl::StrCat("SELECT ", param_string);

  ReferenceDriver::ExecuteStatementAuxOutput aux_output;
  GOOGLESQL_ASSIGN_OR_RETURN(Value value,
                   reference_driver->ExecuteStatementForReferenceDriver(
                       sql, /*parameters=*/{},
                       ReferenceDriver::ExecuteStatementOptions(), aux_output));
  GOOGLESQL_RET_CHECK(aux_output.is_deterministic_output.value_or(false));

  // Expect the result is in Array[Struct{...}].
  GOOGLESQL_RET_CHECK(value.is_valid() && !value.is_null() && value.type()->IsArray() &&
            value.num_elements() == 1)
      << "Generated values are not in format Array[...] when evaluating "
      << sql;

  const Value& value_struct = value.element(0);
  GOOGLESQL_RET_CHECK(value_struct.type()->IsStruct())
      << "Generated values are not in format Array[Struct{...}] when "
      << "evaluating " << sql;

  for (int i = 0; i < value_struct.num_fields(); i++) {
    // Create a copy of the value. Otherwise the value will be invalidated by
    // the next call of TestDriver::ExecuteStatement(...).
    parameters->emplace(value_struct.type()->AsStruct()->field(i).name,
                        Value(value_struct.field(i)));
  }
  return absl::OkStatus();
}

TestFileParameterParser::TestFileParameterParser()
    : reference_impl_(new ReferenceDriver) {}

TestFileParameterParser::~TestFileParameterParser() = default;

absl::Status TestFileParameterParser::Init(const TestDatabase& database) {
  initialized_ = true;
  LanguageOptions lang_options = reference_impl_->GetSupportedLanguageOptions();
  reference_impl_->SetLanguageOptions(lang_options);

  auto test_db_proto = std::make_unique<TestDatabaseProto>();
  GOOGLESQL_RETURN_IF_ERROR(SerializeTestDatabase(database, test_db_proto.get()));
  return reference_impl_->CreateDatabase(*test_db_proto);
}

absl::Status TestFileParameterParser::Parse(
    absl::string_view param_string, std::map<std::string, Value>* parameters) {
  if (!initialized_) {
    // Intiailize to a default empty database.
    GOOGLESQL_RETURN_IF_ERROR(Init(TestDatabase{}));
  }
  return ParseTestFileParameters(param_string, reference_impl_.get(),
                                 parameters);
}

}  // namespace googlesql
