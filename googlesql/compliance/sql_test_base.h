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

// SQL End-To-End Testing Framework.
//
// SQLTestBase defines a SQL end-to-end testing framework. Features include:
//   * Known error handling
//   * Sponge report
//   * Per-file schema and data
//   * Parameters for file-based tests
//   * Names for code-based tests
//
// The infrastructure can be used for different types of testing, such as
// GoogleSQL compliance tesing.
//
// SQLTestBase implements a two-level workflow for file-based testing:
//   * File-level workflow
//     1. Prepare tables for SQL statements
//     2. Create all tables in one shot
//     3. Load protos
//     4. Process statements
//   * Statement-level workflow
//     1. Parse parameters
//     2. Check known errors that can be ignored
//     3. Execute a statement and check its result
//
// Each step of the workflow is implemented by a protected virtual member
// function. A subclass can enhance the workflow by overriding step functions.
//
// SQLTestBase uses a few member variables to control the progress of the
// workflow. A subclass needs to access those variables if it chooses to
// enhance the workflow. Thus they are declared protected as well.
//
// To use SQLTestBase, a subclass needs to:
//   1. Subclass SQLTestBase
//   2. Implement GetTestSuiteName()
//   3. Define TEST_F(...) for code-based tests
//   4. Define a TEST_P(...) that calls RunSQLTest(<filename>) for each *.test
//      file of test cases.
//   5. Define a cc_test and use "--known_error_files <file>,<file>,..." to
//      pass in known error files
//   6. Or use a googlesql_compliance_test BUILD rule, which accepts a
//      known_error_files argument.
//
// SQLTestBase is designed to be an abstract class. Because a TEST_F(...)
// cannot be defined on an abstract class, all tests using SQLTestBase must
// be defined in subclasses. This design enforces separation between the
// the infrastructure and the tests.
//
#ifndef GOOGLESQL_COMPLIANCE_SQL_TEST_BASE_H_
#define GOOGLESQL_COMPLIANCE_SQL_TEST_BASE_H_

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "googlesql/common/float_margin.h"
#include "googlesql/common/internal_value.h"
#include "googlesql/compliance/compliance_label.pb.h"
#include "googlesql/compliance/known_error.pb.h"
#include "googlesql/compliance/matchers.h"
#include "googlesql/compliance/test_driver.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/type.h"
#include "googlesql/public/value.h"
#include "googlesql/reference_impl/reference_driver.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/attributes.h"
#include "absl/cleanup/cleanup.h"
#include "absl/container/btree_set.h"
#include "absl/container/node_hash_set.h"
#include "absl/flags/declare.h"
#include "absl/functional/bind_front.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/source_location.h"
#include "absl/types/span.h"
#include "file_based_test_driver/file_based_test_driver.h"  
#include "file_based_test_driver/run_test_case_result.h"
#include "file_based_test_driver/test_case_options.h"
#include "re2/re2.h"

ABSL_DECLARE_FLAG(bool, googlesql_detect_falsly_required_features);
ABSL_DECLARE_FLAG(bool, googlesql_compliance_write_labels_to_file);
ABSL_DECLARE_FLAG(bool, googlesql_compliance_accept_all_test_output);

namespace googlesql {
class Stats;  // Defined in implementation file.
class FilebasedSQLTestFileOptions;
class FilebasedSQLTestCaseOptions;

std::string ScriptResultToString(const ScriptResult& result);

// Encapsulates the details of an individual SQL test case.
class SQLTestCase {
 public:
  using ParamsMap = std::map<std::string, Value>;

  SQLTestCase(absl::string_view name, absl::string_view sql,
              const ParamsMap& params)
      : name_(name), sql_(sql), params_(params) {}

  absl::string_view name() const { return name_; }
  absl::string_view sql() const { return sql_; }
  const ParamsMap& params() const { return params_; }

 private:
  std::string name_;
  std::string sql_;
  ParamsMap params_;
};

// Counts query result properties across multiple runs.
struct QueryResultStats {
  int verified_count = 0;
  int nonempty_result_count = 0;
  int ordered_result_count = 0;
  int verified_graph_count = 0;
  int verified_graph_nonempty_result_count = 0;
  int verified_measure_aggregation_count = 0;
  int verified_measure_aggregation_nonempty_result_count = 0;
  int created_joined_measure_tables_count = 0;
  int skipped_for_non_determinism_count = 0;
};

class SQLTestBase : public ::testing::TestWithParam<std::string> {
 public:
  using ComplianceTestCaseResult = std::variant<Value, ScriptResult>;

  // Returns a debug string.
  static std::string ToString(
      const absl::StatusOr<ComplianceTestCaseResult>& status);
  static std::string ToString(const std::map<std::string, Value>& parameters);

  // Returns the error matcher to match legal runtime errors.
  static MatcherCollection<absl::Status>* legal_runtime_errors();

  // Returns OK if the first column is the primary key column for all TestTables
  // in a TestDatabase (which is needed to support engines that require every
  // table to have a primary key). Specifically, the first column must meet the
  // following criteria:
  //   1. SupportsGrouping() is true for the type of the column
  //   2. Does not have NULL Values
  //   3. Does not have duplicated Values
  static absl::Status ValidateFirstColumnPrimaryKey(
      const TestDatabase& test_db, const LanguageOptions& language_options);

  // Override this method to return the name of the test suite.
  // This makes SQLTestBase an abstract class.
  virtual const std::string GetTestSuiteName() = 0;

  // This function is used as an entry point for some codebased compliance
  // tests.  It should be avoided because it misses the unique_name_test checks.
  [[deprecated("Use RunSQL")]] absl::StatusOr<ComplianceTestCaseResult>
  ExecuteStatement(absl::string_view sql,
                   const std::map<std::string, Value>& parameters);

  // Use file-based test driver to run tests of a given file. Will be called
  // by TEST_P(...).
  //
  // Make it protected so it can be called by TEST_P(...) from a subclass.
  void RunSQLTests(absl::string_view filename);

  // Each test driver can call this method to specify known error entries
  // that are non file-based.
  absl::Status AddKnownErrorEntry(const KnownErrorEntry& known_error_entry);

  // Each engine should call this method to specify engine-specific known error
  // files. Will die if a file does not exist.
  void LoadKnownErrorFiles(absl::Span<const std::string> files);

  // By default, SetUp() calls CreateDatabase(TestDatabase{}) to clean up
  // existing database, so every TEST_F(...) or TEST_P(...) has a clean
  // environment to start with.  Can be overridden by subclasses if needed.
  void SetUp() override;

  // This function is used as a callback from
  // file_based_test_driver::RunTestCasesFromFiles
  //
  // When running in ValidateAgainstReference(), statement output will be
  // checked against the output found in the golden statements files.  Otherwise
  // statement output will be validated against the reference implementation.
  void RunTestFromFile(absl::string_view sql,
                       file_based_test_driver::RunTestCaseResult* test_result);

  // Known Error Mode
  //
  // A known error can be in four modes (ordered by severity):
  //   * ALLOW_UNIMPLEMENTED
  //   * ALLOW_ERROR
  //   * ALLOW_ERROR_OR_WRONG_ANSWER
  //   * CRASHES_DO_NOT_RUN
  //
  // The framework always tries to run statements in all modes except
  // CRASHES_DO_NOT_RUN. The outcome of a non-crashing known error statement
  // can be:
  //   * The statement passed the test:
  //     Record it in the to-be-removed-from-known-errors list.
  //     Still log it as a known error statement in stats. This will not change
  //     the compliance ratio. A developer still needs to manually remove the
  //     statement from known error files.
  //   * The statement failed the test, in the same mode:
  //     Log it as a known error statement in stats, thus the test will not
  //     fail.
  //   * The statement failed the test, in a more severe mode:
  //     Record it in to-be-added-to-known-errors list. Log it as a *FAILED*
  //     statement in stats and *FAIL* the test. This failure counts as a
  //     regression, and forces the developer to resolve the failure promptly.
  //   * The statement failed the test, in a less severe mode:
  //     Log it as a known error statement in stats. Record it in to-be-upgraded
  //     list.
  //
  // To run all tests (except CRASHES_DO_NOT_RUN) and compute a
  // compliance ratio that includes the tests that had known error entries but
  // passing, use the <flag to be named> flag.
  //
  // The following gMock matchers implement the logic of known error modes. All
  // tests must use these matchers to compare results.
  //
  // Sample syntax of the gMock matchers.
  //   EXPECT_THAT(RunSQL(sql), Returns(x));
  //   EXPECT_THAT(RunSQL(sql), ReturnsSuccess());
  ::testing::Matcher<const absl::StatusOr<ComplianceTestCaseResult>&> Returns(
      const Value& result, const absl::Status& status = absl::OkStatus(),
      FloatMargin float_margin = kExactFloatMargin) {
    return Returns(ComplianceTestCaseResult(result), status, float_margin);
  }
  ::testing::Matcher<const absl::StatusOr<ComplianceTestCaseResult>&> Returns(
      const ComplianceTestCaseResult& result,
      const absl::Status& status = absl::OkStatus(),
      FloatMargin float_margin = kExactFloatMargin);
  ::testing::Matcher<const absl::StatusOr<ComplianceTestCaseResult>&> Returns(
      const absl::StatusOr<ComplianceTestCaseResult>& result,
      FloatMargin float_margin = kExactFloatMargin);
  ::testing::Matcher<const absl::StatusOr<ComplianceTestCaseResult>&> Returns(
      absl::string_view result);
  ::testing::Matcher<const absl::StatusOr<ComplianceTestCaseResult>&> Returns(
      ::testing::Matcher<const absl::StatusOr<ComplianceTestCaseResult>&>
          matcher);
  // A googletest matcher that only checks if the result has OK status.
  ::testing::Matcher<const absl::StatusOr<ComplianceTestCaseResult>&>
  ReturnsSuccess();
  // A googletest matcher that only checks the result and records nothing.
  ::testing::Matcher<const absl::StatusOr<ComplianceTestCaseResult>&>
  ReturnsCheckOnly(const absl::StatusOr<ComplianceTestCaseResult>& result,
                   FloatMargin float_margin = kExactFloatMargin);

  // Run a "codebased" compliance or RQG test with optional parameters. 'sql'
  // can be either a statement or a script depending on the 'script_mode_' state
  // of SQLTestBase.
  // If 'permit_compile_failure' is set to false, an error from the Resolver
  // will cause the test to fail. This is used to enforce that compliance tests
  // are not used to test resolver code, for which analyzer tests are more
  // appropriate.
  absl::StatusOr<ComplianceTestCaseResult> RunSQL(
      absl::string_view sql, const SQLTestCase::ParamsMap& params = {},
      bool permit_compile_failure = true);

  // Run and validate a "codebased" compliance test with parameters and
  // feature requiments.
  void RunSQLOnFeaturesAndValidateResult(
      absl::string_view sql, const std::map<std::string, Value>& params,
      const std::set<LanguageFeature>& required_features,
      const std::set<LanguageFeature>& forbidden_features,
      const Value& expected_value, const absl::Status& expected_status,
      const FloatMargin& float_margin = googlesql::kDefaultFloatMargin);

  // Return the location string. Name is optional as it is defined only in
  // a *.test file for a statement.
  std::string Location(absl::string_view filename, int line_num,
                       absl::string_view name = "") const;

  // Get the current product mode of the test driver.
  ProductMode product_mode() const;

  // Returns true if the test driver wants FLOAT32 as the name for TYPE_FLOAT
  // in the external mode.
  // TODO: Remove once all engines are updated to use FLOAT32 in
  // the external mode.
  bool use_external_float32() const;

  //
  // MakeScopedLabel()
  //
  // This is used to set labels for all tests in the current scope. And can be
  // used with a string or vector of strings. Examples:
  //
  //   auto label1 = MakeScopedLabel("label_1");
  //   auto label2 = MakeScopedLabel(label_vector);
  //
  auto MakeScopedLabel(std::vector<std::string> labels) {
    AddCodeBasedLabels(labels);
    return absl::MakeCleanup(absl::bind_front(
        &SQLTestBase::RemoveCodeBasedLabels, this, std::move(labels)));
  }

  auto MakeScopedLabel(absl::string_view label) {
    return MakeScopedLabel(std::vector<std::string>{std::string(label)});
  }

  // Get the set of code-based labels. Make it protected so a subclass can
  // access it.
  absl::btree_set<std::string> GetCodeBasedLabels();

  // Internal state that controls the file-level workflow.
  enum FileWorkflow { CREATE_DATABASE, FIRST_STATEMENT, REST_STATEMENTS };
  FileWorkflow file_workflow() { return file_workflow_; }
  const TestDatabase& test_db() { return test_db_; }
  TestDatabase& mutable_test_db() { return test_db_; }

  const std::unique_ptr<file_based_test_driver::TestCaseOptions>& options();

  // Initializes file level internal state. Can be overridden by a subclass to
  // initialize an extended state space.
  virtual void InitFileState();

  // Internal state that controls the statement-level workflow.
  enum StatementWorkflow {
    // NORMAL means proceed to running the test against the engine.
    NORMAL,
    // CANCELLED means something went wrong in the test driver, like when
    // interperting the test options.
    CANCELLED,
    // KNOWN_CRASH means the test should not be run on the engine driver because
    // its skiplisted with "CRASHES_DO_NOT_RUN"
    KNOWN_CRASH,
    // SKIPPED means this isn't part of the subset of tests that we are
    // interested in running.
    SKIPPED,
    // FEATURE_MISMATCH means the test driver either does not set a required
    // LanguageFeature or does set a prohibited language feature.
    FEATURE_MISMATCH,
    // NOT_A_TEST means the file section being processed is database setup or
    // otherwise doesn't contain a test statement.
    NOT_A_TEST,
  };
  StatementWorkflow statement_workflow() { return statement_workflow_; }
  const std::string& sql() const { return sql_; }

  // <filename>:<statement_name>.
  const std::string& full_name() const { return full_name_; }

  // 'FILE-*-LINE-*-NAME-*'.
  const std::string& location() { return location_; }

  const std::map<std::string, Value>& parameters() const { return parameters_; }
  const file_based_test_driver::RunTestCaseResult* test_result() {
    return test_result_;
  }
  file_based_test_driver::RunTestCaseResult* mutable_test_result() {
    return test_result_;
  }

  // A subclass can read but not update the known_error_mode.
  KnownErrorMode known_error_mode() const { return known_error_mode_; }

  // Initializes statement level internal state. Can be overridden by a subclass
  // to initialize an extended state space.
  virtual void InitStatementState(
      absl::string_view sql,
      file_based_test_driver::RunTestCaseResult* test_result);

  // Step functions.
  //
  // Step functions operate on file- and statement-level internal states and
  // take no input arguments. State variables file_workflow_ and
  // statement_workflow_ control behaviors of step functions. Step functions
  // advance file_workflow_ and statement_workflow_. Step functions are
  // responsible to record stats and log errors.
  //
  // File-level workflow step functions.
  // Prepare default timezone, protos and enums.
  virtual void StepPrepareTimeZoneProtosEnums();

  // Prepare a test database for a *.test file.
  // Per-File Schema and Data
  //
  // Tables can be created in a *.test file by one of the two test case options:
  //
  //   [prepare_database]
  //   CREATE TABLE <table_name> AS SELECT ...
  //
  // The "SELECT ..." or the "CREATE TABLE ..." is not considered a test case.
  // The statement will be executed by the reference implementation engine, and
  // the result is used to create a table under name "table_name". A
  // [prepare_database] and its CREATE TABLE ... statement define a section, and
  // must be separated from others by "==". Multiple tables can be created by a
  // group of [prepare_database] sections, which must precede any tests in the
  // file.
  //
  // Use "UNION ALL" construct to create multiple rows. Define value types and
  // column names in the first row and subsequent rows will inherit the types
  // and column names. For example:
  //
  //   SELECT int32(1) as Column_1, int64(2) as Column_2 UNION ALL
  //     SELECT 4, 5 UNION ALL
  //     SELECT 7, 8
  //
  // [prepare_database] sections are not considered test cases. They do not
  // require names and any names, labels, global labels, or descriptions will be
  // ignored.
  virtual void StepPrepareDatabase();

  // Create the prepared test database for a *.test file.
  virtual void StepCreateDatabase();
  // Skips a test if the test requires some features that are not supported.
  virtual void StepSkipUnsupportedTest();
  // Check known errors for a statement.
  virtual void StepCheckKnownErrors();
  // Skips an empty test by setting "statement_workflow_" to SKIPPED.
  virtual void SkipEmptyTest();
  // Execute a statement and check its result.
  virtual void StepExecuteStatementCheckResult();

  // Checks whether to cancel the current statement. Updates statement workflow
  // accordingly. Make it protected so a subclass can access it. A statement can
  // be cancelled for multiple reasons. To name a few: the statement has no
  // name; cannot create tables for the statement; or cannot load protos for the
  // statement. A cancelled statement fails the test if it is not a known error.
  // A cancelled statement with an associated known-error entry should be in
  // mode ALLOW_ERROR. If it's known error mode was less severe mode, the
  // statement still fails the test.
  //
  // CheckCancellation() will do proper error handling and stats logging.
  // Always follow the convention that a step method calls a helper method to
  // do something that may fail. The helper method returns a status and the
  // step method checks the status to decide whether to cancel the current
  // statement. Sample usage:
  //
  // absl::Status DoSomethingHelper(...) {...}
  //
  // void StepDoSomething() {
  //   CheckCancellation(DoSomethingHelper(), "Short description of reason");
  //   if (CANCELLED == statement_workflow_) return;
  //
  //   CheckCancellation(DoSomethingElseHelper(), "Reason of something else");
  //   if (CANCELLED == statement_workflow_) return;
  //
  //   ...
  // }
  void CheckCancellation(const absl::Status& status, absl::string_view reason);

  // Enables negative testing so a cancelled statement won't really fail the
  // unittest.
  void EnableNegativeTesting() {
    fail_unittest_on_cancelled_statement_ = false;
  }

  // Name for code-based statements
  //
  // A name will be generated for each code-based statement in the format:
  //
  //   code:<name_prefix>[<result_type_name>]_<typed_parameters>
  //
  // Name prefix is set by the most recent SetNamePrefix(...). Optionally a
  // result type name can be attached to the name prefix. Use SetNamePrefix(...,
  // true) to turn on the option and use SetResultTypeName(...) to set the
  // result type name. In this way, a name prefix can have multiple result type
  // names that are set at later times.
  //
  // Typed parameters are a list of typed values in the format:
  //
  //   <type_name>_<value> for primitive types, or
  //   <type_name>_<signature_of_value> for composite types.
  //
  // Name is unique thus can be used in known-error entries for code-based
  // statements.

  //
  // Make it protected so a TEST_F(...) defined in a subclass can access it.
  // If need_result_type_name is true, the return type must be set with
  // SetResultTypeName and will be appended to the test name.
  void SetNamePrefix(
      absl::string_view name_prefix, bool need_result_type_name = false,
      googlesql_base::SourceLocation loc = googlesql_base::SourceLocation::current());

  // Sets result type name. Make it protected so a TEST_F(...) defined in a
  // subclass can access it.
  void SetResultTypeName(absl::string_view result_type_name);

  // Generates a name for a code-based statement.
  virtual std::string GenerateCodeBasedStatementName(
      absl::string_view sql,
      const std::map<std::string, Value>& parameters) const;

  // Make it protected so a subclass can access it, down cast it to an
  // engine-specific test driver, and invoke engine-specific features.
  //
  // Does not return NULL.
  TestDriver* driver() const { return test_driver_; }

  ReferenceDriver* reference_driver() const { return reference_driver_; }

  // Generates failure report. A subclass can override to add more information.
  virtual std::string GenerateFailureReport(const std::string& expected,
                                            const std::string& actual,
                                            const std::string& extra) const;

  // Returns the minimum float margin to use for comparing
  // two ComplianceTestCaseResult so that result can be considered equal.
  // If such a margin can be be found, `out_makes_equality_pass` will be set to
  // true. Otherwise, it will be set to false to indicate that the equality
  // still doesn't pass even with the last margin.
  // The last margin is returned in all cases.
  FloatMargin GetFloatEqualityMargin(
      absl::StatusOr<ComplianceTestCaseResult> actual,
      absl::StatusOr<ComplianceTestCaseResult> expected, int max_ulp_bits,
      bool& out_makes_equality_pass);

 protected:
  SQLTestBase();

  ABSL_DEPRECATED(
      "DO NOT USE THIS. USED ONLY FOR AN EXTERNAL LEGACY TEST"
      "WHICH IS USING FAKE IN-MEMORY DESCRIPTORS")
  virtual bool UseLeakyDescriptors() const { return false; };

  // Does not take ownership of the pointers. 'reference_driver' must be NULL if
  // and only if 'test_driver' is NULL.
  //
  // If 'test_driver' is NULL, the destructor is the only method that can be
  // called on the resulting object. This is a hack to facilitate creating the
  // code-based compliance test data structures through googletest but then
  // skipping the actual tests (which presumably are being run in another BUILD
  // target).
  SQLTestBase(TestDriver* test_driver, ReferenceDriver* reference_driver);
  SQLTestBase(const SQLTestBase&) = delete;
  SQLTestBase& operator=(const SQLTestBase&) = delete;
  ~SQLTestBase() override;

  //////
  // The TESTONLY_ functions below are used to give the unit test limited
  // access to the internal state of this class. Needing access to the internal
  // state is already a symptom of poorly factored code, but the cost to
  // clean up the testing framework appears quite high. The TESTONLY_ accessors
  // protect encapsulation better than making the entire unit test harness a
  // friend.
  //
  // These functions should only be used by SqlTestBaseTest and any other code
  // accessing them risks breakage when these functions go away.
  //////

  // Get the compliance test labels proto computed for the most recently
  // compiled test case.
  const ComplianceTestCaseLabels& TESTONLY_ComplianceTestCaseLabels();

  // Allows unit test to set `test_file_options_` so that it can exercise
  // filebased test code paths from a non-file driven unit test.
  void TESTONLY_SetTestFileOptions(
      std::unique_ptr<FilebasedSQLTestFileOptions> test_file_options);

  void ClearParameters() { parameters_.clear(); }

  virtual absl::Status CreateDatabase(const TestDatabase& test_db);

  // Accessor for the type factory used for populating test tables.
  TypeFactory* table_type_factory();

  // Accessor for the type factory used for statement execution.
  // This should be deleted and each driver should own and encapsulate its own
  // type factory.
  // Communication with SQLTestBase should be done based on the
  // TestDatabaseProto.
  TypeFactory* execute_statement_type_factory() const {
    TypeFactory* driver_type_factory = driver()->type_factory();
    if (driver_type_factory != nullptr) {
      return driver_type_factory;
    }
    return execute_statement_type_factory_.get();
  }

  // Resets the execute statement type factory to free types created during
  // statement execution.
  void ResetExecuteStatementTypeFactory();

  ReferenceDriver::ExecuteStatementOptions GetExecuteStatementOptions() const;

  // Accessor for language options of the driver.
  LanguageOptions driver_language_options();

  // Returns true if 'driver()' supports 'feature'. ABSL_CHECK fails if 'driver()' is
  // the reference implementation and 'feature' is not enabled.
  bool DriverSupportsFeature(LanguageFeature feature);

  bool IsFileBasedStatement() const;

  bool script_mode() const { return script_mode_; }
  void set_script_mode(bool script_mode) { script_mode_ = script_mode; }

  // Accessors are public.
  void set_statement_workflow(StatementWorkflow statement_workflow) {
    statement_workflow_ = statement_workflow;
  }

  // Tests can register a test case an inspector callback that peeks at each
  // test case before it runs. The inspector callback is static so that
  // it applies to all TestCases even when the test makes multiple instances of
  // SQLTestBase.
  using TestCaseInspectorFn =
      std::function<absl::Status(const SQLTestCase& sql_test_case)>;
  static void RegisterTestCaseInspector(TestCaseInspectorFn* inspector) {
    test_case_inspector_ = inspector;
  }

  // Apply a registered inspector to the test case.
  absl::Status InspectTestCase() {
    if (test_case_inspector_ != nullptr) {
      return (*test_case_inspector_)(
          SQLTestCase(full_name(), sql(), parameters()));
    }
    return absl::OkStatus();
  }

  ReferenceDriver* test_setup_driver() { return test_setup_driver_.get(); }

  // Check 'feature' to ensure that it is required for evaluating the test case.
  // This is useful for codebased tests that have an expected result instead of
  // an expected golden file output. When 'require_inclusive' is false we are
  // checking if the feature is falsely prohibited. When checking for falsely
  // prohibited features we make sure adding that feature causes the test to
  // fail.
  bool IsFeatureFalselyRequired(
      LanguageFeature feature, bool require_inclusive, absl::string_view sql,
      const std::map<std::string, Value>& param_map,
      const std::set<LanguageFeature>& required_features,
      const absl::Status& initial_run_status,
      const absl::StatusOr<ComplianceTestCaseResult>& expected_result,
      const FloatMargin& expected_float_margin);

  // Overrides the default formatting options for the test result value content.
  // Since ToString is static, so must be the formatting options.
  static void SetFormatValueContentOptions(
      InternalValue::FormatValueContentOptions options);

 private:
  // Accesses ValidateFirstColumnPrimaryKey
  friend class CodebasedTestsEnvironment;

  // SQLTestEnvironment needs to access stats_ for recording global stats.
  friend class SQLTestEnvironment;

  // Accesses RegisterTestCaseInspector
  friend class UniqueNameTestEnvironment;

  // Accesses sql_, parameters_ and full_name_ for generating a report.
  friend class RQGBasedSqlBuilderTest;

  // Accesses sql_, parameters_ and full_name_ for generating a report.
  friend class RQGBasedPipeSqlBuilderTest;

  // For being able to access the test case options for compliance testing
  // between standard and pipe syntax against the reference implementation.
  friend class FileBasedPipeSqlBuilderTest;

  // KnownErrorFilter needs to use stats_ to record failed statements. It also
  // needs to read sql_, location_, full_name_, and known_error_mode() for
  // statements.
  template <typename ResultType>
  friend class KnownErrorFilter;

  static TestCaseInspectorFn* test_case_inspector_;

  // Shared resource of Stats over all tests in a test case.
  static std::unique_ptr<Stats> stats_;

  // An error matcher to match legal runtime errors.
  static std::unique_ptr<MatcherCollection<absl::Status>> legal_runtime_errors_;

  std::unique_ptr<FilebasedSQLTestFileOptions> test_file_options_;
  std::unique_ptr<FilebasedSQLTestCaseOptions> test_case_options_;

  FileWorkflow file_workflow_;

  StatementWorkflow statement_workflow_ = NORMAL;
  std::string sql_;        // The SQL string
  std::string full_name_;  // <filename>:<statement_name>
  std::string
      location_;  // FILE-<filename>-LINE-<line_num>-NAME-<statement_name>

  // A special ReferenceDriver that is only used to set up the tests. For
  // example, it is used to construct the tables specified by [prepare_database]
  // sections. It must outlive all of the other drivers/TypeFactories, which may
  // depend on this ReferenceDriver's TypeFactory.
  std::unique_ptr<ReferenceDriver> test_setup_driver_;

  // If true, queries are executed as scripts, rather than standalone
  // statements.
  bool script_mode_ = false;

  // NULL if this object does not own 'test_driver_'.
  std::unique_ptr<TestDriver> test_driver_owner_;

  // The TestDriver being tested, set in the constructor. If it is NULL, then
  // the only method call supported by this object is the destructor.
  TestDriver* test_driver_;

  // NULL if this object does not own 'reference_driver_'. If it is non-NULL, it
  // may have different options than 'test_setup_driver_'.
  std::unique_ptr<ReferenceDriver> reference_driver_owner_;

  // ReferenceDriver for comparison against 'test_driver_', set in the
  // constructor.
  ReferenceDriver* reference_driver_;

  // TypeFactory that is used to execute statements.
  std::unique_ptr<TypeFactory> execute_statement_type_factory_;

  // Whether a cancelled statement fails the unittest. For negative testing
  // only.
  bool fail_unittest_on_cancelled_statement_ = true;

  std::map<std::string, Value> parameters_;
  file_based_test_driver::RunTestCaseResult* test_result_ = nullptr;

  // Labels
  //
  // Each statement has a mandatory "name", an optional "labels" set, and an
  // optional "description". A *.test file has an optional "global_labels" set,
  // which has to be defined only once in the first non-create-table section.
  // The "global_labels" apply to all the statements in the file.
  //
  // Name and labels are used in known error entries. Name is referred by
  // <filename>:<name> in known error files.

  // Current set of effective labels.
  absl::btree_set<std::string> effective_labels_;

  // Compliance report labels collected from pre-rewritten resolved AST.
  // See (broken link):engine_compliance_reportcard
  absl::btree_set<std::string> compliance_labels_;

  // Boolean mark to indicate whether or not reference driver catalog is
  // properly initialized in StepCreateDatabase function.
  bool is_catalog_initialized_ = false;

  // Known Errors
  //
  // Contains known errors labels and statements. Statements are referred by
  // <filename>:<statement_name>.
  // The entries are split into non-regex labels and regexes for faster
  // matching.
  absl::node_hash_set<std::string> known_error_labels_;
  absl::node_hash_set<std::string> known_error_regex_strings_;
  std::vector<std::unique_ptr<RE2>> known_error_regexes_;

  // Maps a label to its known error mode and the set of reasons as defined in
  // corresponding files.
  struct LabelInfo {
    KnownErrorMode mode;
    std::set<std::string> reason;
  };
  std::map<std::string, LabelInfo> label_info_map_;

  // Known Error mode for the current statement.
  KnownErrorMode known_error_mode_ = KnownErrorMode::NONE;

  // Set of labels in known_error files that affect current statement.
  absl::btree_set<std::string> by_set_;

  // The container for proto files, proto names, enum names, and tables that
  // are used to create a test database.
  TestDatabase test_db_;

  // The container for CREATE CONSTANT statements that were executed during
  // a [prepare_database] step.
  std::vector<std::string> constant_stmt_cache_;
  // The container for CREATE FUNCTION statements that were executed during
  // a [prepare_database] step.
  std::vector<std::string> udf_stmt_cache_;
  // The container for CREATE VIEW statements that were executed during
  // a [prepare_database] step.
  std::vector<std::string> view_stmt_cache_;

  // Code-based label set. Use a vector since labels might be added multiple
  // times.
  std::vector<std::string> code_based_labels_;

  // Variables and functions for code-based names.
  std::string name_prefix_;
  bool name_prefix_need_result_type_name_ = false;
  std::string result_type_name_;

  // Return true if we are checking the "golden" expected output against the
  // reference implementation. In this mode, we should run all statements, in
  // all relevant modes, and test all outputs (against expected outputs from
  // code or from files).
  //
  // When this is false, we are testing an engine (or a specific configuration
  // of the reference implementation) and comparing its output to the reference
  // implementation's output. We will run each statement using
  // the engine's options (from TestDriver::GetSupportedLanguageOptions) and
  // compare against the reference output with the same options.
  //
  // TODO: There are many blocks of code in the cc file that look like:
  // if (IsVerifyingGoldens()) {
  //   ...  // Do something
  // } else {
  //   ...  // Do something else
  // }
  // Consider creating another interface with two implementations of each
  // method: one that implements the behavior for testing the reference
  // implementation, and one that implements the behavior for testing a real
  // engine.
  bool IsVerifyingGoldens() const;

  // Return a set of effective labels, include <filename>:<statement_name>,
  // labels, and global_labels.
  absl::btree_set<std::string> EffectiveLabels(
      absl::string_view full_name, absl::Span<const std::string> labels,
      absl::Span<const std::string> global_labels) const;

  // Log a set of strings as a single line with a prefix.
  template <typename ContainerType>
  void LogStrings(const ContainerType& strings,
                  const std::string& prefix) const;

  // Log label_to_reason_map_ map in a single line.
  void LogReason() const;

  // Log label_known_error_mode_map_ map in a single line.
  void LogMode() const;

  // Will not clear known_error_ and label_to_reason_map_ before loading. This
  // is to support multiple known error files per engine.
  absl::Status LoadKnownErrorFile(absl::string_view filename);

  // Check if any labels in effective_labels is a known error. Returns the
  // maximum mode, where 0 means not a known error and non-zero means
  // it is a known error. Returns the subset in by_set.
  KnownErrorMode IsKnownError(
      const absl::btree_set<std::string>& effective_labels,
      absl::btree_set<std::string>* by_set) const;

  // Wraps Stats::RecordKnownErrorStatement(). Ignores test result of a
  // known error statement. This is required so the golden tool will not pick up
  // a possibly wrong test result generated by a known error statement.
  void RecordKnownErrorStatement(KnownErrorMode mode);

  // Similar to RecordKnownErrorStatement(). A no-op if `check_only is true.
  void RecordKnownErrorStatement(KnownErrorMode mode, bool check_only) {
    if (!check_only) {
      RecordKnownErrorStatement(mode);
    }
  }

  // Similar to RecordKnownErrorStatement(). Uses 'known_error_mode_' by
  // default.
  void RecordKnownErrorStatement() {
    RecordKnownErrorStatement(known_error_mode_);
  }

  // Prepares protos and enums in the test drivers.
  absl::Status LoadProtosAndEnums();

  // Sets default time zone in the reference driver. This is needed since time
  // zone may affect results of statements that generate parameters or table
  // contents.
  absl::Status SetDefaultTimeZone(const std::string& default_time_zone);

  // Create the prepared database. This includes the protos and enums, as well
  // as all the tables.
  virtual absl::Status CreateDatabase();

  // Helper to add constants from CREATE CONSTANT statements to the reference
  // and test drivers.
  //
  // If `cache_stmts` is true, cache the statements in `constant_stmt_cache_`
  // if both the reference and test drivers are able to successfully execute
  // them.
  absl::Status AddConstants(absl::Span<const std::string> create_constant_stmts,
                            bool cache_stmts);

  // Helper to add views from CREATE VIEW statements to the reference and
  // test drivers.
  //
  // If `cache_stmts` is true, cache the statements in `view_stmt_cache_`
  // if both the reference and test drivers are able to successfully execute
  // them.
  absl::Status AddViews(absl::Span<const std::string> create_view_stmts,
                        bool cache_stmts = false);

  // Helper to add functions from CREATE FUNCTION statements to the reference
  // and test drivers.
  //
  // If `cache_stmts` is true, cache the statements in `udf_stmt_cache_`
  // if both the reference and test drivers are able to successfully execute
  // them.
  absl::Status AddFunctions(absl::Span<const std::string> create_function_stmts,
                            bool cache_stmts = false);

  // Apply the current CREATE TABLE FUNCTION statement and add the function to
  // the Database.
  void AddTVF();

  // Add and remove labels to the code-based label set. Duplicated labels are
  // allowed. Labels will be added in the specified order, and be removed in
  // the reverse order. When removing labels, validates the to-be removed
  // labels are the same with those added.
  void AddCodeBasedLabels(std::vector<std::string> labels);
  void RemoveCodeBasedLabels(std::vector<std::string> labels);

  // If true, database preparation statements are allowed to fail when run
  // against the reference implementation. This is used to test handling of
  // broken [prepare_database] statements and for test setup that may fail
  // under certain modes like external.
  bool AllowsFailedReferenceSetup() const;

  // Turns a string into an RE2 safe string. Used to generate names for
  // code-based statements. Names must be RE2 safe as known error list
  // processing uses RE2 regex to match names.
  std::string SafeString(absl::string_view str) const;

  // Generates a signature of a bytes or string. A signature is the left 8
  // characters of the string, followed by the fingerprint of the string,
  // followed by the right 8 characters of the string.
  std::string SignatureOfString(absl::string_view str) const;

  // Generates a signature of a array, proto, or struct value to be that of
  // its debug string.
  std::string SignatureOfCompositeValue(const Value& value) const;

  // Converts a value to a safe string.
  std::string ValueToSafeString(const Value& value) const;

  // Returns the name prefix. Append result type name to it when necessary.
  // The prefix is in the format: code:<name_prefix>[<result_type_name>].
  std::string GetNamePrefix() const;

  class TestResults {
   public:
    explicit TestResults(absl::StatusOr<ComplianceTestCaseResult> driver_output,
                         std::optional<bool> is_deterministic);

    absl::StatusOr<ComplianceTestCaseResult> driver_output() const {
      return driver_output_;
    }

    // Provide a helper to access the status to make the class compatible with
    // KnownErrorMatcher.
    absl::Status status() const { return driver_output_.status(); }

    // Returns the result as the string that is printed in the .test files.
    absl::string_view ToString() const;

   private:
    // This helps compliance tests provide better failure messages.
    template <typename Sink>
    friend void AbslStringify(Sink& sink, const TestResults& result) {
      sink.Append(result.ToString());
      sink.Append("\n");
    }

    // We need the result status to honor the known error filters.
    const absl::StatusOr<ComplianceTestCaseResult> driver_output_;

    std::optional<bool> is_deterministic_ = std::nullopt;

    // If ToString has been called, we cache its output here to avoid
    // recomputation.
    mutable std::string cached_to_string_ = "";
  };

  // Similar to the Returns matchers in the public API, but takes a TestResults
  // so that it can handle the reference impls non-determinism signal.
  ::testing::Matcher<const TestResults&> ToStringIs(absl::string_view result);

  // Implement the ToString interface for TestResult because it's expected by
  // the matcher templates.
  static std::string ToString(const TestResults& result);

  // Executes a test case, either as a standalone statement, or as a script,
  // depending on <script_mode_>.
  TestResults ExecuteTestCase();

  // Executes a statement against the reference driver, creating a copy of the
  // test database when `[use_database_copy]` is enabled.
  absl::StatusOr<ComplianceTestCaseResult> ExecuteStatementReferenceDriver(
      ReferenceDriver::ExecuteStatementAuxOutput& aux_output);

  // NOTE: This implementation is specific to testing the reference
  // implementation.
  // TODO: This should be pulled out to a separate subclass
  // specific to the reference implementation.
  TestResults RunTestWithFeaturesEnabled(
      const std::set<LanguageFeature>& features_set);

  // For each required feature, re-runs each iteration of the test with that
  // feature removed.  Then compares the output with the feature removed to the
  // same run with the feature included.  If the result is unchanged, fails the
  // test since this means the required feature was not actually required.
  // NOTE: This implementation is specific to testing the reference
  // implementation.
  // TODO: This should be pulled out to a separate subclass
  // specific to the reference implementation.
  void RunAndCompareTestWithoutEachRequiredFeatures(
      const std::set<LanguageFeature>& required_features,
      TestResults& test_result);

  // True if running the query with the required features except
  // `feature_to_check` returns a result other than `test_result`.
  bool IsFeatureRequired(LanguageFeature feature_to_check,
                         TestResults& test_result);

  // Parses the expected results and compares them against `test_result`
  void ParseAndCompareExpectedResults(TestResults& test_result);

  // Adds tables with measure columns to `test_db` if the test case requires the
  // FEATURE_ENABLE_MEASURES feature.
  void MaybeAddMeasureTables(
      const FilebasedSQLTestCaseOptions& test_case_options,
      TestDatabase& test_db);

  static InternalValue::FormatValueContentOptions*
      format_value_content_options_;
};

}  // namespace googlesql

// Be explicit so it won't break the build in "-c opt" mode.
namespace testing {
namespace internal {

// Teaches googletest to print StatusOr<Value>.
template <>
class UniversalPrinter<absl::StatusOr<::googlesql::Value>> {
 public:
  static void Print(const absl::StatusOr<::googlesql::Value>& status_or,
                    ::std::ostream* os) {
    *os << ::googlesql::SQLTestBase::ToString(status_or);
  }
};

// Teaches googletest to print StatusOr<ComplianceTestCaseResult>.
// This is important to make RQG test failures print a human consumable diff
// between expected and actual results.
template <>
class UniversalPrinter<
    absl::StatusOr<::googlesql::SQLTestBase::ComplianceTestCaseResult>> {
 public:
  static void Print(
      const absl::StatusOr<::googlesql::SQLTestBase::ComplianceTestCaseResult>&
          status_or,
      ::std::ostream* os) {
    *os << ::googlesql::SQLTestBase::ToString(status_or);
  }
};

}  // namespace internal
}  // namespace testing

#endif  // GOOGLESQL_COMPLIANCE_SQL_TEST_BASE_H_
