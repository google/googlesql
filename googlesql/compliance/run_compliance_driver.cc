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

#include <unistd.h>

#include <iostream>
#include <map>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "googlesql/base/init_google.h"
#include "googlesql/base/logging.h"
#include "googlesql/base/fileutils.h"
#include "googlesql/base/helpers.h"
#include "googlesql/base/options.h"
#include "googlesql/common/options_utils.h"
#include "googlesql/compliance/test_driver.h"
#include "googlesql/compliance/test_driver.pb.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "absl/flags/flag.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/descriptor.h"

namespace googlesql {
struct QueryParameterFlagValue {
  std::map<std::string, googlesql::Value> parameters;
};

bool AbslParseFlag(absl::string_view text, QueryParameterFlagValue* flag,
                   std::string* err) {
  AnalyzerOptions analyzer_options;
  analyzer_options.set_language(
      googlesql::GetComplianceTestDriver()->GetSupportedLanguageOptions());

  auto catalog = std::make_unique<SimpleCatalog>("test");
  catalog->AddBuiltinFunctions(
      BuiltinFunctionOptions(analyzer_options.language()));

  return internal::ParseQueryParameterFlag(
      text, analyzer_options, catalog.get(), &flag->parameters, err);
}
std::string AbslUnparseFlag(const QueryParameterFlagValue& flag) {
  return internal::UnparseQueryParameterFlag(flag.parameters);
}
}  // namespace googlesql

ABSL_FLAG(std::string, sql_file, "",
          "Input file containing query; leave blank to read from stdin");

ABSL_FLAG(
    std::string, test_db, "",
    "File containing a TestDatabaseProto. Either binary or text format is "
    "accepted. If blank, the query will run against an empty TestDatabase.");

ABSL_FLAG(googlesql::QueryParameterFlagValue, parameters, {},
          googlesql::internal::kQueryParameterMapHelpstring);

namespace {
constexpr char kUsage[] = R"(Runs a query against a compliance test driver.

The query may be supplied in the following ways:
  1) As a direct commandline argument:
       run_compliance_test_driver 'SELECT 1'
  2) As a text file:
       run_compliance_test_driver /path/to/file
  3) Via stdin:
       echo 'SELECT 1' < $0
)";

std::string ReadSqlFromStdin() {
  if (isatty(fileno(stdin))) {
    std::cout << "Please type the sql you want to analyze." << std::endl;
    std::cout << "Press ctrl-D to terminate the query and run it." << std::endl;
  }
  std::string sql;
  for (std::string line; std::getline(std::cin, line);) {
    absl::StrAppend(&sql, line, "\n");
    break;
  }
  return sql;
}

std::string GetQuery(const std::vector<std::string>& args) {
  if (args.empty()) {
    if (!absl::GetFlag(FLAGS_sql_file).empty()) {
      std::string sql;
      GOOGLESQL_QCHECK_OK(googlesql_base::GetContents(absl::GetFlag(FLAGS_sql_file), &sql,
                                  ::googlesql_base::Defaults()));
      return sql;
    } else {
      return ReadSqlFromStdin();
    }
  } else {
    return absl::StrJoin(args, " ");
  }
}
}  // namespace

int main(int argc, char* argv[]) {
  std::vector<std::string> args;
  {
    std::vector<char*> remaining_args = absl::ParseCommandLine(argc, argv);
    args.assign(remaining_args.cbegin() + 1, remaining_args.cend());
  }
  std::string sql = GetQuery(args);
  googlesql::TestDriver* test_driver = googlesql::GetComplianceTestDriver();
  googlesql::TypeFactory type_factory;

  auto test_db_proto = std::make_unique<googlesql::TestDatabaseProto>();
  if (absl::GetFlag(FLAGS_test_db).empty()) {
    GOOGLESQL_QCHECK_OK(googlesql::SerializeTestDatabase(googlesql::TestDatabase{},
                                               test_db_proto.get()));
  } else {
    googlesql_base::ReadFileToProtoOrDie(absl::GetFlag(FLAGS_test_db),
                               test_db_proto.get());
  }
  absl::Status status = test_driver->CreateDatabase(*test_db_proto);
  googlesql::ProtoImporter proto_importer(/*runs_as_test=*/true);
  if (!status.ok()) {
    if (status.code() == absl::StatusCode::kUnimplemented &&
        status.message() ==
            "Test driver does not support creating database from proto") {
      // Fallback to the in-memory overload, until all external drivers are
      // migrated to the proto overload.
      ABSL_QCHECK(!test_driver->IsReferenceImplementation());
      absl::StatusOr<googlesql::TestDatabase> db =
          googlesql::DeserializeTestDatabase(*test_db_proto, &type_factory,
                                             proto_importer.importer()->pool());
      GOOGLESQL_QCHECK_OK(db.status());
      GOOGLESQL_QCHECK_OK(test_driver->CreateDatabase(*db));
    } else {
      GOOGLESQL_QCHECK_OK(status);
    }
  }

  googlesql::TypeFactory* driver_type_factory = test_driver->type_factory();
  if (driver_type_factory == nullptr) {
    driver_type_factory = &type_factory;
  }
  absl::StatusOr<googlesql::Value> value = test_driver->ExecuteStatement(
      sql, absl::GetFlag(FLAGS_parameters).parameters, driver_type_factory);
  if (!value.ok()) {
    std::cout << value.status().ToString() << std::endl;
    return 1;
  }
  std::cout << value->DebugString() << std::endl;
  return 0;
}
