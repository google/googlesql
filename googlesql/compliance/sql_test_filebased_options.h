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

// The classes in this file handle interaction with
// file_based_test_driver::TestCaseOptions APIs on behalf of SQLTestBase.
// SQLTestBase gets to use a strongly typed API and delegates managing of
// option key registration, parsing, and validating to this component.

#ifndef GOOGLESQL_COMPLIANCE_SQL_TEST_FILEBASED_OPTIONS_H_
#define GOOGLESQL_COMPLIANCE_SQL_TEST_FILEBASED_OPTIONS_H_

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "googlesql/compliance/known_error.pb.h"
#include "googlesql/compliance/test_driver.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/type.h"
#include "googlesql/public/value.h"
#include "googlesql/reference_impl/reference_driver.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "file_based_test_driver/test_case_options.h"

namespace googlesql {

class FilebasedSQLTestFileOptions;

// Owns the options context for a single test case at a time.
class FilebasedSQLTestCaseOptions {
 public:
  FilebasedSQLTestCaseOptions(const FilebasedSQLTestCaseOptions&) = delete;
  FilebasedSQLTestCaseOptions& operator=(const FilebasedSQLTestCaseOptions&) =
      delete;

  FilebasedSQLTestCaseOptions(FilebasedSQLTestCaseOptions&&) = default;
  FilebasedSQLTestCaseOptions& operator=(FilebasedSQLTestCaseOptions&&) =
      default;

  // The SQL statement or script text with [options=...] and whitespace
  // stripped away.
  absl::string_view sql() const { return sql_; }

  // The name of the test. This is the file local name without the filename:
  // prefix.
  absl::string_view name() const { return name_; }

  // Query parameters used for the evaluation of this test case.
  const std::map<std::string, Value>& params() const { return params_; }

  // Features that must be enabled for the test to produce any of the expected
  // results.
  const std::set<LanguageFeature>& required_features() const {
    return required_features_;
  }
  // Features that must be disabled for the test to produce and of the expected
  // results.
  const std::set<LanguageFeature>& forbidden_features() {
    return forbidden_features_;
  }

  // Additional features that should be enabled by the reference driver to
  // execute a [prepare_database] statement.
  const std::set<LanguageFeature>& prepare_database_additional_features()
      const {
    return prepare_database_additional_features_;
  }

  // [labels=...] specified specificallyh on this test case.
  const std::vector<std::string>& local_labels() const { return local_labels_; }

  // Filenames for proto types added for this test case.
  const std::set<std::string>& proto_file_names() const {
    return new_proto_file_names_;
  }
  // Proto message types added for this test case.
  const std::set<std::string>& proto_message_names() const {
    return new_proto_message_names_;
  }
  // Proto enum types added for this test case.
  const std::set<std::string>& proto_enum_names() const {
    return new_proto_enum_names_;
  }

  PrimaryKeyMode primary_key_mode() const { return primary_key_mode_; }

  // True for test file segments marked [prepare_database]. These segments
  // aren't actually tests. Instead they set up schema objects such as tables or
  // functions.
  bool prepare_database() const { return prepare_database_; }

  bool reserve_match_recognize() const { return reserve_match_recognize_; }

  // Whether to reserve the keyword GRAPH_TABLE
  bool reserve_graph_table() const { return reserve_graph_table_; }

  // If true, the golden file will contain the computed labels in the printed
  // result.
  bool extract_labels() const { return extract_labels_; }

  // If true, the test will not attempt to detect falsely required features.
  bool skip_required_feature_integrity_check() const {
    return skip_required_feature_integrity_check_;
  }

  // If true, a copy of the test database is passed to the reference driver.
  bool use_test_database_copy() const { return use_test_database_copy_; }

  // If true, skip this test for compliance testing between standard and pipe
  // syntax.
  bool exclude_in_sql_builder_pipe_sql_equivalence_tests() const {
    return exclude_in_sql_builder_pipe_sql_equivalence_tests_;
  }

 private:
  // Encapsulation wise, these two classes are designed to work as one.
  friend class FilebasedSQLTestFileOptions;

  std::string sql_;
  std::string name_;
  std::map<std::string, Value> params_;
  std::set<LanguageFeature> required_features_;
  std::set<LanguageFeature> forbidden_features_;
  std::set<LanguageFeature> prepare_database_additional_features_;
  std::vector<std::string> local_labels_;
  std::set<std::string> new_proto_file_names_;
  std::set<std::string> new_proto_message_names_;
  std::set<std::string> new_proto_enum_names_;
  PrimaryKeyMode primary_key_mode_ = PrimaryKeyMode::DEFAULT;
  bool extract_labels_ = false;
  bool prepare_database_ = false;
  bool reserve_match_recognize_ = false;
  bool reserve_graph_table_ = false;
  bool skip_required_feature_integrity_check_ = false;
  bool use_test_database_copy_ = false;
  bool exclude_in_sql_builder_pipe_sql_equivalence_tests_ = false;

  FilebasedSQLTestCaseOptions();
};

// Owns the options context for a single test file.
class FilebasedSQLTestFileOptions {
 public:
  FilebasedSQLTestFileOptions(const FilebasedSQLTestFileOptions&) = delete;
  FilebasedSQLTestFileOptions& operator=(const FilebasedSQLTestFileOptions&) =
      delete;

  FilebasedSQLTestFileOptions(FilebasedSQLTestFileOptions&&) = default;
  FilebasedSQLTestFileOptions& operator=(FilebasedSQLTestFileOptions&&) =
      default;

  // Create a new test file options handler. The 'reference_driver' is used
  // to evaluate SQL expressions to generate parameter values. This should be
  // the same reference driver used to evaluate schema objects. There are a tiny
  // number of compliance tests for which this matters, as some engines
  // depend on no-op cast removal that can be broken if parameter types and
  // table column types are merely equivalent rather than equal. Using the same
  // test driver ensures they are equal.
  explicit FilebasedSQLTestFileOptions(ReferenceDriver* reference_driver);

  // Called once per test case. This must be called in order from the first
  // test case in the file to the last test case. Some options are only allowed
  // on the first case in a file. Others, such as [default options=...] or
  // [proto_type_names=...] apply to subsequent cases but not previous ones.
  absl::StatusOr<std::unique_ptr<FilebasedSQLTestCaseOptions>> ProcessTestCase(
      absl::string_view test_case, std::string* failure_reason);

  // Labels that apply to all test cases in the file that are set once per file.
  const std::vector<std::string>& global_labels() const {
    return global_labels_;
  }

  // A default timezone that is set once per file.
  absl::string_view default_timezone() const { return default_timezone_; }

 private:
  // Encapsulation wise, these two classes are designed to work as one.
  friend class FilebasedSQLTestCaseOptions;

  // For accessing 'options_'. This breaks encapulation, but provides backward
  // compatibility for Spanner's test driver.
  // TODO: Migrate Spanner to FilebasedSQLTestFileOptions and remove.
  friend class SQLTestBase;

  // A reference driver is used to compute parameter values.
  ReferenceDriver* reference_driver_;

  std::unique_ptr<file_based_test_driver::TestCaseOptions> options_;

  // Used to ensure uniques name within a *.test file.
  absl::flat_hash_set<std::string> names_;
  int64_t statement_count_ = 0;

  std::string global_labels_string_;
  std::vector<std::string> global_labels_;
  std::string default_timezone_ = "";

  // Keep track of all proto types added so far within the file.
  absl::flat_hash_set<std::string> all_proto_file_names_;
  absl::flat_hash_set<std::string> all_proto_message_names_;
  absl::flat_hash_set<std::string> all_proto_enum_names_;

  absl::Status ExtractName(bool validate_name, std::string* name);

  // Processes test case options [load_proto_files], [load_proto_names], and
  // [load_enum_names], which load protos and enums in a *.test file. Each
  // takes a list of comma-delimited proto files, proto names, or enum names.
  // A group of these test case options need to collocate with either a
  // [prepare_database] or the first statement.
  absl::Status ExtractProtoAndEnumTypes(
      std::set<std::string>& new_proto_file_names,
      std::set<std::string>& new_proto_message_names,
      std::set<std::string>& new_proto_enum_names);

  absl::Status ExtractGlobalLabels();

  absl::Status ExtractDefaultTimezone(std::string* default_time_zone);
};

}  // namespace googlesql

#endif  // GOOGLESQL_COMPLIANCE_SQL_TEST_FILEBASED_OPTIONS_H_
