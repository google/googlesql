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

#ifndef GOOGLESQL_COMPLIANCE_PARAMETERS_TEST_UTIL_H_
#define GOOGLESQL_COMPLIANCE_PARAMETERS_TEST_UTIL_H_

#include <map>
#include <memory>
#include <string>

#include "googlesql/public/type.h"
#include "googlesql/public/value.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/status.h"

namespace googlesql {
class ReferenceDriver;
struct TestDatabase;

// Parses query parameters from a string in the format that appears in
// GoogleSQL compliance test files.
//
// Parameters can be specified within *.test files by the following test
// case options:
//
//   [parameters=<typed_value> as <param_name>, ...]
//   SELECT @<param_name> ...
//
// A typed value is specified by a SQL expression, such as CAST(2 AS INT32). The
// parameters can be used by the statement in the same section.
//
// Parse a parameters string, return a map of <param_name>:<typed_value>.
absl::Status ParseTestFileParameters(absl::string_view param_string,
                                     ReferenceDriver* reference_driver,
                                     std::map<std::string, Value>* parameters);

// A helper class which wraps ParseTestFileParameters and also owns the
// TypeFactory and a ReferenceDriver with an empty database. This lets
// external tools parse parameters without exposing the ReferenceDriver.
class TestFileParameterParser {
 public:
  TestFileParameterParser();
  ~TestFileParameterParser();

  // Initializes the parameter parser with a TestDatabase that may contain
  // proto and enum type information.
  absl::Status Init(const TestDatabase& database);

  // Parses a set of parameters from 'param_string' populating 'parameters'.
  absl::Status Parse(absl::string_view param_string,
                     std::map<std::string, Value>* parameters);

 private:
  std::unique_ptr<ReferenceDriver> reference_impl_;
  bool initialized_ = false;
};

}  // namespace googlesql

#endif  // GOOGLESQL_COMPLIANCE_PARAMETERS_TEST_UTIL_H_
