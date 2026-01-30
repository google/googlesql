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

// Test driver interface for running compliance tests.
// See compliance_test_base.h for instructions on how to run compliance tests
// against a particular engine.

#ifndef GOOGLESQL_COMPLIANCE_TEST_DRIVER_H_
#define GOOGLESQL_COMPLIANCE_TEST_DRIVER_H_

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/base/path.h"
#include "googlesql/common/measure_analysis_utils.h"
#include "googlesql/common/testing/testing_proto_util.h"
#include "googlesql/common/type_visitors.h"
#include "googlesql/compliance/test_driver.pb.h"
#include "googlesql/public/functions/date_time_util.h"  
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/type.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/value.h"
#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "absl/flags/declare.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "googlesql/base/file_util.h"
#include "google/protobuf/compiler/importer.h"
#include "google/protobuf/io/zero_copy_stream.h"
#include "google/protobuf/io/zero_copy_stream_impl_lite.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_builder.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

// Specifies any assumptions that a test makes about the primary keys of
// non-value test tables. Engines can skip tests for unsupported primary key
// modes by overriding TestDriver::SkipTestsWithPrimaryKeyMode() accordingly.
//
// It seems likely that this will never be relevant outside of DML statements,
// because that is the only place where primary keys affect GoogleSQL specified
// behavior.
enum class PrimaryKeyMode {
  // The test requires the engine to either act as if the first column of a
  // table is its primary key, or the table does not have any primary key. The
  // reference implementation is free to make a different choice. Tests with
  // this mode must be written to pass regardless of what choice the engine
  // makes.
  DEFAULT = 0,

  // The test requires the engine to act as if the first column of a table is
  // its primary key.
  FIRST_COLUMN_IS_PRIMARY_KEY = 1,

  // The test requires the engine to act as if the tables do not have primary
  // keys.
  NO_PRIMARY_KEY = 2,

  // User code that switches on this enum must have a default case so
  // builds won't break if new enums get added.
  __PrimaryKeyMode__switch_must_have_a_default__ = -1
};

// Returns the string form of 'mode'.
inline std::string PrimaryKeyModeName(PrimaryKeyMode mode) {
  switch (mode) {
    case PrimaryKeyMode::DEFAULT:
      return "DEFAULT";
    case PrimaryKeyMode::FIRST_COLUMN_IS_PRIMARY_KEY:
      return "FIRST_COLUMN_IS_PRIMARY_KEY";
    case PrimaryKeyMode::NO_PRIMARY_KEY:
      return "NO_PRIMARY_KEY";
    default:
      ABSL_LOG(FATAL) << "Unknown PrimaryKeyMode: " << static_cast<int>(mode);
  }
}

// Defines options for creating or populating random data for a table. Make it
// a 'class' so member variables can only be accessed by methods. This allows
// detection of invalid range settings (e.g., min > max). Copyable so that one
// TestTableOptions object can be used to configure multiple TestTable objects.
class TestTableOptions {
 public:
  // Default value for expected table size.
  static constexpr int kDefaultExpectedTableSize = 100;

  // Default value for nullable probability.
  static constexpr double kDefaultNullableProbability = 0.1;

  TestTableOptions() = default;
  TestTableOptions(const TestTableOptions&) = default;
  TestTableOptions& operator=(const TestTableOptions&) = default;

  // Sets expected table size range.
  void set_expected_table_size_range(int min, int max) {
    ABSL_CHECK_GE(max, min);
    ABSL_CHECK_GE(min, 0);
    expected_table_size_min_ = min;
    expected_table_size_max_ = max;
  }

  // Accessors to member variables.
  int expected_table_size_min() const { return expected_table_size_min_; }
  int expected_table_size_max() const { return expected_table_size_max_; }

  void set_is_value_table(const bool is_value_table) {
    is_value_table_ = is_value_table;
  }
  bool is_value_table() const { return is_value_table_; }

  void set_nullable_probability(double probability) {
    nullable_probability_ = probability;
  }
  double nullable_probability() const { return nullable_probability_; }

  std::set<LanguageFeature> required_features() const {
    return required_features_;
  }

  std::set<LanguageFeature>* mutable_required_features() {
    return &required_features_;
  }

  const std::string& userid_column() const { return userid_column_; }
  void set_userid_column(absl::string_view userid_column) {
    userid_column_ = userid_column;
  }

  const std::vector<const AnnotationMap*>& column_annotations() const {
    return column_annotations_;
  }

  void set_column_annotations(
      std::vector<const AnnotationMap*> column_annotations) {
    column_annotations_ = std::move(column_annotations);
  }

 private:
  // LINT.IfChange
  // Defines expected table size after populating it with random data. The
  // table will have a random size in [min, max].
  int expected_table_size_min_ = kDefaultExpectedTableSize;
  int expected_table_size_max_ = kDefaultExpectedTableSize;

  // Indicates whether the table is a value table.
  bool is_value_table_ = false;

  // Indicates the probability of NULL Values in each column of a table.
  double nullable_probability_ = kDefaultNullableProbability;

  // A test table is only created if the test driver supports all of its
  // required features, and even then it is only visible in tests that list all
  // of its required features in their [required_features] sections.
  std::set<LanguageFeature> required_features_;

  // Table metadata identifying the User ID column for
  // (broken link). Corresponds to
  // googlesql::Table::GetUserIdColumn().
  //
  // An empty string means no user id column is set for this table (the default
  // case).
  std::string userid_column_;

  // Annotations for each column of the table. <column_annotations_> is either
  // empty or has the same number of elements as the number of the columns in
  // the table. May have nullptr to indicate the corresponding table column
  // doesn't have annotation.
  std::vector<const AnnotationMap*> column_annotations_;
};

// This describes a table that should be present in the created database.
// The table name is in the key of the 'tables' map in TestDatabase.
//
// Engines that require primary keys can assume that the first column is a valid
// primary key. That is, its type supports grouping and it contains no NULL or
// duplicate values.
struct TestTable {
  // The contents of the table, mapped into a Value object. Tables are
  // represented here as ARRAYs of STRUCTs where the STRUCT field names give
  // the column names. Measure column values are not included in this
  // representation.
  Value table_as_value;
  TestTableOptions options;

  // Compliance testing for measures is difficult, because it is currently
  // impossible to define measures via SQL or use DDL to add measure columns to
  // a table.
  //
  // We work around this limitation by adding predefined measure columns to
  // tables in the test database.
  //
  // The fields below are used to configure the addition of measure columns to
  // the table.
  // TODO: b/350555383 - Remove these workaround once we can define measures via
  // SQL or use DDL to add measure columns to a table.

  // Measure column definitions to add to the table.
  std::vector<MeasureColumnDef> measure_column_defs;
  // Row identity column indices for the table with measure columns.
  std::vector<int> row_identity_columns;
};

// This describes the tables that should be present in the created database,
// and other options necessary for the database.
//
// A test driver implementation needs to make available the protos and enums
// specified in 'proto_names' and 'enum_names'. The protos and enums should be
// defined in a proto file listed in 'proto_files', or that is directly or
// indirectly referenced by a proto file listed in 'proto_files'.
//
struct TestDatabase {
  // Clears everything.
  void clear() {
    proto_files.clear();
    proto_names.clear();
    enum_names.clear();
    tables.clear();
    tvfs.clear();
    property_graph_defs.clear();
    measure_function_defs.clear();
  }

  // Returns true if empty.
  bool empty() const {
    return proto_files.empty() && proto_names.empty() && enum_names.empty() &&
           tables.empty() && tvfs.empty() && property_graph_defs.empty() &&
           measure_function_defs.empty();
  }
  // LINT.IfChange
  // File paths (*.proto) relative to the build workspace
  std::set<std::string> proto_files;
  bool runs_as_test = true;       // When true, looks for files in test_srcdir.
  std::set<std::string> proto_names;        // Set of proto type names.
  std::set<std::string> enum_names;         // Set of enum type names.
  std::map<std::string, TestTable> tables;  // Keyed on table name.
  std::map<std::string, std::string> tvfs;  // Keyed on TVF name.
  std::map<std::string, std::string>
      property_graph_defs;  // Keyed on graph name.

  // Holds CREATE FUNCTION statements to create UDFs and UDAs that are
  // referenced by measure expressions
  std::vector<std::string> measure_function_defs;
};

// The result of executing a single statement. It can be a statement in a
// script, or a sub-statement in a multi-statement query.
struct StatementResult {
  // For a statement in a script, this is the name of the procedure the
  // statement belongs to. Empty if the statement is not part of a procedure or
  // this is a sub-statement in a multi-statement query.
  //
  // If the statement belongs to a procedure, line/column numbers, below are
  // relative to the procedure body, rather than the overall script.
  std::string procedure_name;

  // Line number of the start of the statement, 1-based.
  // 0 if the line number cannot be determined.
  //
  // NOTE: Currently, line and column are only populated for statements in a
  // script, but this may be extended to multi-statement queries in the future.
  int line = 0;

  // Column number of the start of the statement, 1-based.
  // 0 if the column number cannot be determined.
  int column = 0;

  // The result of the statement. See TestDriver::ExecuteStatement() for a
  // description on what types of values are expected for different statement
  // types.
  absl::StatusOr<Value> result;
};

// Represents multiple statement results. It can be used for both script and
// multi-statement queries.
struct MultiStmtResult {
  // A list of statements that were executed, along with their results.
  //
  // For a script, statements are evaluated sequentially and side-effects are
  // applied. Even if the script fails, `statement_results` is still
  // populated with the list of statements that ran up to and including the
  // failure. A failed `StatementResult` is still possible, even if the script
  // succeeds, if the error was caught by an exception handler in the script.
  //
  // For a multi-statement query, each statement can fail or succeed
  // independently. The sub-statements are evaluated as-if in parallel and
  // are against a single database snapshot.
  std::vector<StatementResult> statement_results;
};

using ScriptResult ABSL_DEPRECATED("Inline me!") = MultiStmtResult;

// Serialize TestDatabase to a proto. This is to allow building test drivers in
// other languages (in particular, java).
absl::Status SerializeTestDatabase(const TestDatabase& database,
                                   TestDatabaseProto* proto);

// Deserializes a TestDatabaseProto to a TestDatabase. This is used for running
// a compliance driver for a standalone repro, by allowing the database to be
// specified as a proto.
absl::StatusOr<TestDatabase> DeserializeTestDatabase(
    const TestDatabaseProto& proto, TypeFactory* type_factory,
    const ::google::protobuf::DescriptorPool* descriptor_pool);

static const char default_default_time_zone[] = "America/Los_Angeles";

class TestDriver {
 public:
  virtual ~TestDriver() = default;

  // Returns the set of LanguageOptions supported by this engine.
  // LanguageOptions change the expected output of some queries.
  // e.g. Features added in version X should give errors in earlier versions.
  virtual LanguageOptions GetSupportedLanguageOptions() = 0;

  // Engines that wish to support tests with non-default primary key modes
  // must override this method. Examples:
  //
  // - An engine that requires every table to have a primary key should skip
  //   tests with PrimaryKeyMode::NO_PRIMARY_KEY.
  //
  // - An engine that does not support primary keys should skip tests with
  //   PrimaryKeyMode::FIRST_COLUMN_IS_PRIMARY_KEY;
  //
  // - An engine that supports tables with and without primary keys should have
  //   two test drivers. One test driver can store all test tables with primary
  //   keys and skip tests with PrimaryKeyMode::NO_PRIMARY_KEY, and the second
  //   test driver can store all test tables without primary keys and only run
  //   tests with PrimaryKeyMode::NO_PRIMARY_KEY.
  virtual absl::StatusOr<bool> SkipTestsWithPrimaryKeyMode(
      PrimaryKeyMode primary_key_mode) {
    return false;
  }

  // Supplies a TestDatabase. Must be called prior to ExecuteStatement().
  ABSL_DEPRECATED(
      "Use the overload which takes a proto instead, to ensure all"
      "types and proto/enum descriptors are internalized.")
  virtual absl::Status CreateDatabase(const TestDatabase& test_db) {
    GOOGLESQL_RET_CHECK_FAIL() << "Implement the overload which takes a proto instead";
  }
  virtual absl::Status CreateDatabase(const TestDatabaseProto& test_db_proto) {
    return absl::UnimplementedError(
        "Test driver does not support creating database from proto");
  }

  // Supplies several "temporary" SQL constants that the driver should add to
  // the catalog. This will be called after CreateDatabase but before
  // ExecuteStatement.
  virtual absl::Status AddSqlConstants(
      absl::Span<const std::string> create_constant_stmts) {
    return absl::UnimplementedError(
        "Test driver does not support SQL Constants.");
  }

  // Supplies several "temporary" Sql UDF definitions that the driver should add
  // to the catalog. This will be called after CreateDatabase but before
  // ExecuteStatement.
  virtual absl::Status AddSqlUdfs(
      absl::Span<const std::string> create_function_stmts) {
    return absl::UnimplementedError("Test driver does not support SQL UDFs.");
  }

  // Supplies several "temporary" view definitions that the driver should add
  // to the catalog. This will be called after CreateDatabase but before
  // ExecuteStatement.
  virtual absl::Status AddViews(
      absl::Span<const std::string> create_view_stmts) {
    return absl::UnimplementedError("Test driver does not support SQL Views.");
  }

  // Supplies several property graph definitions that the driver should add to
  // the catalog. This will be called after CreateDatabase but before
  // ExecuteStatement.
  virtual absl::Status AddPropertyGraphs(
      absl::Span<const std::string> create_property_graph_stmts) {
    return absl::UnimplementedError(
        "Test driver does not support property graphs.");
  }

  // Executes a statement using the given 'parameters' and returns the result.
  // Implementations should use 'type_factory' to instantiate all types that are
  // used in the returned result. The return value is only valid as long as
  // 'type_factory' and this driver are valid.
  //
  // For a ResolvedQueryStmt, the return type is an array. If the
  // 'is_value_table' field is true, the element type is the corresponding value
  // type, and each element of the array represents a return value. Otherwise,
  // the element type is a struct, and each element of the array represents a
  // returned row.
  //
  // For a DML statement, the returned value is a struct with two fields: an
  // int64 representing the number of rows/values (depending on whether the
  // table is a value table) modified by the statement, and an array
  // representing the full contents of the table after applying the
  // statement. In this framework, DML statements do not have side effects.
  // When processing a DML statement, the driver should make a full copy of the
  // target table, then modify and return the copy.
  //
  // There are helpers that may be useful for producing DML output statement
  // types in type_helpers.h.
  //
  // Returns an error if the input `sql` is a generalized statement
  // resolved to a `ResolvedGeneralizedQueryStmt`, or a `ResolvedMultiStmt`
  // after rewrite.
  virtual absl::StatusOr<Value> ExecuteStatement(
      const std::string& sql, const std::map<std::string, Value>& parameters,
      TypeFactory* type_factory) = 0;

  // Similar to ExecuteStatement() but allows executing multi-stmts, which can
  // return multiple results. Specifically,
  //
  // - If the input `sql` is a generalized statement resolved to a
  //   `ResolvedGeneralizedQueryStmt`, or a `ResolvedMultiStmt` after rewrite,
  //   the output will be a list of results, where each result is either a
  //   single googlesql `Value` containing the rows of the table, or an error
  //   status.
  //
  // - If the input `sql` is a single statement and the execution succeeds, the
  //   output will be a list of size one containing the result. See the comment
  //   on `ExecuteStatement` for more details about the return value of a single
  //   statement.
  //
  // - If the input `sql` is a single statement and the execution fails, the
  //   output is the error status.
  //
  // The compliance test framework always calls this method to execute
  // statements.
  //
  // Engines that do not support generalized statements only need to implement
  // `ExecuteStatement`. The default implementation of this method calls
  // `ExecuteStatement` and wraps the result.
  //
  // Engines that support generalized statements should override this method.
  // `ExecuteStatement` should return an error if the input sql produces
  // multiple results.
  virtual absl::StatusOr<MultiStmtResult> ExecuteGeneralizedStatement(
      const std::string& sql, const std::map<std::string, Value>& parameters,
      TypeFactory* type_factory) {
    GOOGLESQL_ASSIGN_OR_RETURN(Value result,
                     ExecuteStatement(sql, parameters, type_factory));
    return MultiStmtResult{{StatementResult{.result = result}}};
  }

  virtual absl::StatusOr<std::vector<Value>> RepeatExecuteStatement(
      const std::string& sql, const std::map<std::string, Value>& parameters,
      TypeFactory* type_factory, uint64_t times) {
    std::vector<Value> result(times);
    for (int i = 0; i < times; ++i) {
      GOOGLESQL_ASSIGN_OR_RETURN(result[i],
                       ExecuteStatement(sql, parameters, type_factory));
    }
    return result;
  }

  // Similar to ExecuteStatement(), but executes 'sql' as a script, rather than
  // an individual statement.
  virtual absl::StatusOr<ScriptResult> ExecuteScript(
      const std::string& sql, const std::map<std::string, Value>& parameters,
      TypeFactory* type_factory) {
    return absl::UnimplementedError("Scripts are not supported");
  }

  // This method is only intended to be overridden by the
  // reference implementation.  Other engines should not override.
  virtual bool IsReferenceImplementation() const { return false; }

  // This method is here only to pass the TypeFactory to the ExecuteStatement()
  // method. Once all external drivers are migrated to encapsulate their own
  // TypeFactory, this method can be removed and ExecuteStatement() will
  // transition to not take a TypeFactory.
  virtual TypeFactory* type_factory() { return nullptr; }

  // Sets a query evaluation timeout or returns an error if that timeout is not
  // supported by this engine. This timeout does not apply to non-query
  // GoogleSQL statements.
  virtual absl::Status SetStatementEvaluationTimeout(absl::Duration timeout) {
    return ::googlesql_base::UnimplementedErrorBuilder()
           << "This test driver doesn't support statement evaluation timeouts.";
  }

  static absl::TimeZone GetDefaultDefaultTimeZone() {
    absl::TimeZone time_zone;
    ABSL_CHECK(absl::LoadTimeZone(default_default_time_zone, &time_zone));
    return time_zone;
  }

  // Returns the default time zone set for the driver.  Derived classes
  // must override this appropriately.  TODO: Once spandex
  // overrides this, make it a pure virtual function.
  virtual const absl::TimeZone GetDefaultTimeZone() const {
    return absl::UTCTimeZone();
  }

  // Sets the default time zone for the driver.  TODO: Once
  // Spandex overrides this, make it a pure virtual function.
  virtual absl::Status SetDefaultTimeZone(
      const std::string& default_time_zone) {
    return absl::OkStatus();
  }

  // Returns the context information in this driver. The message will be printed
  // out when the query result is not equal to the one from the reference
  // engine.
  // TODO This method is currently used to print out the
  // materialized views information because the views are generated in the test
  // engine now. A better solution is to move the view creation logic to
  // reference engine and print out the debug information in one place.
  virtual std::string DebugContext() { return "";}

  // Classes for use with the proto2 Importer, which can be used to import the
  // proto files included in a test database. The source tree is responsible for
  // reading files, while the error collector handles reporting errors from the
  // import process.
  class ProtoSourceTree : public google::protobuf::compiler::SourceTree {
   public:
    explicit ProtoSourceTree(absl::string_view base_dir)
        : base_dir_(base_dir) {}
    google::protobuf::io::ZeroCopyInputStream* Open(absl::string_view filename) override {
      std::string contents;
      if (internal::GetContents(googlesql_base::JoinPath(base_dir_, filename), &contents)
              .ok()) {
        contents_.push_back(contents);
        return new google::protobuf::io::ArrayInputStream(
            contents_.back().data(),
            static_cast<int>(contents_.back().size()));
      }
      return nullptr;
    }

   private:
    const std::string base_dir_;
    std::vector<std::string> contents_;  // Backing for ArrayInputStream.
  };

  class ProtoErrorCollector : public google::protobuf::compiler::MultiFileErrorCollector {
   public:
    explicit ProtoErrorCollector(std::vector<std::string>* errors)
        : errors_(errors) {}
    void RecordError(absl::string_view file, int line, int col,
                     absl::string_view detail) override {
      if (line > 0) {
        errors_->push_back(absl::StrCat(file, ":", line, ": ", detail));
      } else {
        errors_->push_back(absl::StrCat(file, ": ", detail));
      }
    }

   private:
    std::vector<std::string>* const errors_;
  };

 protected:
  // Converts a MultiStmtResult to a single Value, or errors if the given
  // `multi_result` does not produce exactly one result.
  //
  // Engines can use this function to implement `ExecuteStatement` with
  // `ExecuteGeneralizedStatement`.
  absl::StatusOr<Value> MultiStmtResultToValue(
      const absl::StatusOr<MultiStmtResult>& multi_result);
};

// Users who subclass TestDriver should implement this method and have it
// return a new (non-NULL) instance of the TestDriver subclass.
//
// The framework in compliance_test_launcher.cc (invoked from the
// googlesql_compliance_test BUILD rule) will call this function to create a
// TestDriver instance.  The caller takes ownership of the returned object.
//
// TODO This strategy doesn't work if we need to link two TestDrivers
// into the same binary.  If we need that, we could switch to a more
// sophisticated registration mechanism like util/registration/registerer.h.
TestDriver* GetComplianceTestDriver();

// Wrapper class to encapsulate the helper classes like the error collector
// for convenience.
class ProtoImporter {
 public:
  explicit ProtoImporter(bool runs_as_test) {
    if (runs_as_test) {
      proto_source_tree_ = CreateProtoSourceTree();
    } else {
      proto_source_tree_ = std::make_unique<TestDriver::ProtoSourceTree>("");
    }
    proto_error_collector_ =
        std::make_unique<TestDriver::ProtoErrorCollector>(&errors_);
    importer_ = std::make_unique<google::protobuf::compiler::Importer>(
        proto_source_tree_.get(), proto_error_collector_.get());
  }

  google::protobuf::compiler::Importer* importer() const { return importer_.get(); }

  absl::Status Import(absl::string_view filename) {
    importer_->Import(std::string(filename));
    if (!errors_.empty()) {
      return absl::InternalError(absl::StrJoin(errors_, "\n"));
    }
    return absl::OkStatus();
  }

 private:
  std::vector<std::string> errors_;
  std::unique_ptr<google::protobuf::compiler::SourceTree> proto_source_tree_;
  std::unique_ptr<TestDriver::ProtoErrorCollector> proto_error_collector_;
  std::unique_ptr<google::protobuf::compiler::Importer> importer_;
};

}  // namespace googlesql

// TODO: Remove when geography crashes are resolved.
ABSL_DECLARE_FLAG(bool, googlesql_test_driver_enable_geography);

#endif  // GOOGLESQL_COMPLIANCE_TEST_DRIVER_H_
