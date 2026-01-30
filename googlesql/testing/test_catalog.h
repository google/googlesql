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

#ifndef GOOGLESQL_TESTING_TEST_CATALOG_H_
#define GOOGLESQL_TESTING_TEST_CATALOG_H_

// TestCatalog specializes SimpleCatalog (from googlesql/public) by adding
// support for error injection.
//
// ../testdata/sample_catalog.h has a TestCatalog loaded with a specific schema
// shared by several of our tests.

#include <string>
#include <vector>

#include "googlesql/public/catalog.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/type.h"
#include "absl/container/node_hash_map.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/status.h"

namespace googlesql {

class TestCatalog : public SimpleCatalog {
 public:
  explicit TestCatalog(absl::string_view name) : SimpleCatalog(name) {}
  TestCatalog(const TestCatalog&) = delete;
  TestCatalog& operator=(const TestCatalog&) = delete;
  ~TestCatalog() override;

  absl::Status GetTable(const std::string& name, const Table** table,
                        const FindOptions& options = FindOptions()) override;

  absl::Status GetFunction(const std::string& name, const Function** function,
                           const FindOptions& options = FindOptions()) override;

  absl::Status GetType(const std::string& name, const Type** type,
                       const FindOptions& options = FindOptions()) override;

  absl::Status GetCatalog(const std::string& name, Catalog** catalog,
                          const FindOptions& options = FindOptions()) override;

  // Add an error status that will be returned when this name is looked up.
  void AddError(absl::string_view name, const absl::Status& error);

 private:
  // These errors will be returned for any matching lookup, regardless of type.
  absl::node_hash_map<std::string, absl::Status> errors_;

  // Return an error if one exists in <errors_>.
  absl::Status GetErrorForName(absl::string_view name) const;
};

// This is just a Function whose constructor implicitly sets the Function
// group_ to 'TestFunction'.
class TestFunction : public Function {
 public:
  TestFunction(absl::string_view function_name, Function::Mode mode,
               const std::vector<FunctionSignature>& function_signatures);
  TestFunction(const TestFunction&) = delete;
  TestFunction& operator=(const TestFunction&) = delete;
  ~TestFunction() override;
};

}  // namespace googlesql

#endif  // GOOGLESQL_TESTING_TEST_CATALOG_H_
