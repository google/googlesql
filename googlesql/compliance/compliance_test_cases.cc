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

#include "googlesql/compliance/compliance_test_cases.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/base/path.h"
#include "google/protobuf/message.h"
#include "googlesql/common/float_margin.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/common/testing/testing_proto_util.h"
#include "googlesql/compliance/depth_limit_detector_test_cases.h"
#include "googlesql/compliance/functions_testlib.h"
#include "googlesql/compliance/sql_test_base.h"
#include "googlesql/compliance/test_driver.h"
#include "googlesql/public/civil_time.h"
#include "googlesql/public/functions/date_time_util.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/strings.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/value.h"
#include "googlesql/reference_impl/reference_driver.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "googlesql/testing/test_function.h"
#include "googlesql/testing/test_value.h"
#include "googlesql/testing/type_util.h"
#include "googlesql/testing/using_test_value.cc"  // NOLINT
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/attributes.h"
#include "absl/base/casts.h"
#include "absl/container/flat_hash_set.h"
#include "absl/flags/flag.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/types/span.h"

ABSL_FLAG(std::string, file_pattern, "*.test", "File pattern for test files.");

// Two flags to help engines control their sharding of compliance test cases.
// When codebased tests are disabled, we do not need to set up the test
// environment. When file based tests are disabled, we do not need to try
// reading any files.
ABSL_FLAG(bool, googlesql_run_codebased_tests, true,
          "Run the code based compliance tests.");
ABSL_FLAG(bool, googlesql_run_filebased_tests, true,
          "Run the file based compliance tests.");

using googlesql::test_values::kIgnoresOrder;

namespace googlesql {

const char kAnonymousColumnName[] = "";
const char kColA[] = "ColA";

// Column names for test tables.
const char kPrimaryKey[] = "primary_key";
const char kBoolColumnName[] = "bool_val";
const char kDoubleColumnName[] = "double_val";
const char kInt64ColumnName[] = "int64_val";
const char kStringColumnName[] = "str_val";

// The test environment will be initialized by SetUpTestCase of the first test
// class that uses it. We do not set up before that point because it will
// conflict with engine-specific test environments that must be set up before
// the driver may be accessed.
static CodebasedTestsEnvironment* GetCodeBasedTestsEnvironment() {
  static CodebasedTestsEnvironment* env = []() {
    CodebasedTestsEnvironment* code_based_tests_environment =
        new CodebasedTestsEnvironment;
    code_based_tests_environment->SetUp();
    // Register the global environment. Even though it is too late to get auto
    // SetUp, we still want to trigger auto TearDown.
    ::testing::AddGlobalTestEnvironment(code_based_tests_environment);
    return code_based_tests_environment;
  }();
  return env;
}

static Value MakeProtoValue(const google::protobuf::Message* msg) {
  const ProtoType* proto_type;
  GOOGLESQL_CHECK_OK(test_values::static_type_factory()->MakeProtoType(
      msg->GetDescriptor(), &proto_type));
  absl::Cord bytes;
  ABSL_CHECK(msg->SerializeToCord(&bytes));
  return Value::Proto(proto_type, bytes);
}

static std::string ParametersWithSeparator(int num_parameters,
                                           absl::string_view separator) {
  std::vector<std::string> arg_str;
  arg_str.reserve(num_parameters);
  for (int i = 0; i < num_parameters; i++) {
    arg_str.push_back(absl::StrCat("@p", i));
  }
  return absl::StrJoin(arg_str, separator);
}

static std::string InitParametersWithNamedValueArguments(
    const QueryParamsWithResult& query_params_with_result,
    absl::string_view separator) {
  std::vector<std::string> arg_str;
  size_t num_ordinal_params = query_params_with_result.ordinal_params().size();
  arg_str.reserve(query_params_with_result.num_params());
  for (int i = 0; i < num_ordinal_params; i++) {
    arg_str.push_back(absl::StrCat("@p", i));
  }

  auto named_value_params = query_params_with_result.named_value_params_names();
  for (int i = 0; i < named_value_params.size(); i++) {
    arg_str.push_back(
        absl::StrCat(named_value_params[i], "=>@p", num_ordinal_params + i));
  }
  return absl::StrJoin(arg_str, separator);
}

static Value Singleton(const ValueConstructor& v) {
  return StructArray({kColA}, {{v}});
}

static void ConvertResultsToSingletons(QueryParamsWithResult* param) {
  param->MutateResultValue([](Value& value) { value = Singleton(value); });
}

// Make a label "type_<kind_name>" based on <type>.
// e.g. type_INT64, type_DATE, type_PROTO, type_ENUM.
static std::string MakeLabelForType(const Type* type) {
  return absl::StrCat("type_",
                      Type::TypeKindToString(type->kind(), PRODUCT_INTERNAL));
}

// Get type labels from MakeLabelForType for the parameters and return value
// specified in <query_params_with_result>.
static std::vector<std::string> GetTypeLabels(
    const QueryParamsWithResult& query_params_with_result) {
  std::vector<std::string> labels;
  for (const Value& param : query_params_with_result.params()) {
    labels.push_back(MakeLabelForType(param.type()));
  }
  labels.push_back(MakeLabelForType(query_params_with_result.result().type()));
  return labels;
}

// Returns a SQL literal for the value.
static std::string MakeLiteral(const Value& value) {
  ABSL_LOG_IF(FATAL, value.is_null()) << "Null value " << value.DebugString();

  switch (value.type_kind()) {
    case TYPE_STRING:
      return ToStringLiteral(value.string_value());
    case TYPE_BYTES:
      return ToBytesLiteral(value.bytes_value());
    default:
      ABSL_LOG(FATAL) << "Not supported type " << value.DebugString();
  }
}

// Returns all comparison operators for "=" and "<" operators.
static std::string ComparisonExpression(const FunctionTestCall& f) {
  if (f.function_name == "=" || f.function_name == "RangeEquals") {
    return "@p0 = @p1 AND @p1 = @p0 AND @p0 <= @p1 AND @p0 >= @p1 AND "
           "NOT(@p0 != @p1) AND NOT(@p0 < @p1) AND NOT(@p0 > @p1)";
  } else if (f.function_name == "<" || f.function_name == "RangeLessThan") {
    return "@p0 < @p1 AND @p1 > @p0 AND @p0 <= @p1 AND @p0 != @p1 AND "
           "NOT(@p0 > @p1) AND NOT(@p0 >= @p1) AND NOT(@p0 = @p1)";
  } else {
    ABSL_LOG(FATAL) << f.function_name;
  }
}

static std::vector<FunctionTestCall> WrapFunctionTestWithFeature(
    absl::Span<const FunctionTestCall> tests, LanguageFeature feature) {
  std::vector<FunctionTestCall> wrapped_tests;
  wrapped_tests.reserve(tests.size());
  for (auto call : tests) {
    call.params = call.params.WrapWithFeature(feature);
    wrapped_tests.emplace_back(call);
  }
  return wrapped_tests;
}

std::vector<FunctionTestCall> WrapFeatureAdditionalStringFunctions(
    absl::Span<const FunctionTestCall> tests) {
  return WrapFunctionTestWithFeature(tests,
                                     FEATURE_ADDITIONAL_STRING_FUNCTIONS);
}

std::vector<FunctionTestCall> WrapFeatureAliasesForStringAndDateFunctions(
    absl::Span<const FunctionTestCall> tests) {
  return WrapFunctionTestWithFeature(
      tests, FEATURE_ALIASES_FOR_STRING_AND_DATE_FUNCTIONS);
}

std::vector<FunctionTestCall> WrapFeatureLastDay(
    absl::Span<const FunctionTestCall> tests) {
  std::vector<FunctionTestCall> wrapped_tests;
  for (auto call : tests) {
    QueryParamsWithResult::FeatureSet feature_set = {
        FEATURE_ADDITIONAL_STRING_FUNCTIONS, FEATURE_CIVIL_TIME};
    functions::DateTimestampPart date_part =
        static_cast<functions::DateTimestampPart>(
            call.params.param(1).enum_value());
    switch (date_part) {
      case functions::WEEK_MONDAY:
      case functions::WEEK_TUESDAY:
      case functions::WEEK_WEDNESDAY:
      case functions::WEEK_THURSDAY:
      case functions::WEEK_FRIDAY:
      case functions::WEEK_SATURDAY:
        feature_set.insert(FEATURE_WEEK_WITH_WEEKDAY);
        break;
      default:
        break;
    }

    call.params = call.params.WrapWithFeatureSet(feature_set);
    wrapped_tests.emplace_back(call);
  }
  return wrapped_tests;
}

static std::vector<QueryParamsWithResult> WrapFeatureJSON(
    absl::Span<const QueryParamsWithResult> tests) {
  std::vector<QueryParamsWithResult> wrapped_tests;
  wrapped_tests.reserve(tests.size());
  for (auto& test_case : tests) {
    wrapped_tests.emplace_back(test_case.WrapWithFeature(FEATURE_JSON_TYPE));
  }
  return wrapped_tests;
}

static std::vector<QueryParamsWithResult> WrapFeatureCollation(
    absl::Span<const QueryParamsWithResult> tests) {
  std::vector<QueryParamsWithResult> wrapped_tests;
  wrapped_tests.reserve(tests.size());
  for (auto& test_case : tests) {
    QueryParamsWithResult::FeatureSet feature_set = {
        FEATURE_ANNOTATION_FRAMEWORK, FEATURE_COLLATION_SUPPORT};
    wrapped_tests.emplace_back(test_case.WrapWithFeatureSet(feature_set));
  }
  return wrapped_tests;
}

// Owned by
// CodebasedTestsEnvironment::{SetUp,TearDown}(). 'code_based_reference_driver'
// is NULL if and only if either 'code_based_test_driver' is NULL, or
// 'code_based_test_driver->IsReferenceImplementation()' is true.
static TestDriver* code_based_test_driver = nullptr;
static ReferenceDriver* code_based_reference_driver = nullptr;

bool CodebasedTestsEnvironment::skip_codebased_tests() {
  return !absl::GetFlag(FLAGS_googlesql_run_codebased_tests);
}

// The compliance code-based tests operate in two distinct modes:
// 1) testing the reference driver
// 2) testing an engine driver
//
// When testing the reference driver, the statements are executed
// against the reference driver and the result is compared against
// the test case result.
//
// When testing an engine driver, the statements are executed against
// both the engine driver and the reference driver, and the
// results from the two drivers are compared.

// static
void ComplianceCodebasedTests::SetUpTestSuite() {
  ABSL_CHECK(GetCodeBasedTestsEnvironment() != nullptr);
}

void CodebasedTestsEnvironment::SetUp() {
  // The advantage of skipping codebased tests is to skip this potentially
  // very expensive setup.
  if (skip_codebased_tests()) {
    ABSL_LOG(INFO) << "Skipping codebased tests.";
    return;
  }
  ABSL_LOG(INFO) << "Setting up codebased tests environment.";
  // Create a test driver, reference driver (if necessary), and
  // default test database to use for all the code-based tests.
  Value table_empty;
  Value table1;
  Value table2;
  Value table3;
  Value table_large;
  Value table_all_null;
  Value table_distincts;
  std::vector<std::string> table_columns = {
      "primary_key", "bool_val", "double_val", "int64_val", "str_val"};
  std::vector<const Type*> table_column_types = {
      Int64Type(), BoolType(), DoubleType(), Int64Type(), StringType()};
  std::vector<std::string> primary_key = {kPrimaryKey};
  std::vector<std::string> bool_column = {kBoolColumnName};
  std::vector<std::string> double_column = {kDoubleColumnName};
  std::vector<std::string> int64_column = {kInt64ColumnName};
  std::vector<std::string> string_column = {kStringColumnName};
  // Create an empty test / result table.
  std::vector<StructType::StructField> fields;
  fields.reserve(table_columns.size());
  for (int i = 0; i < table_columns.size(); ++i) {
    fields.emplace_back(
        StructType::StructField(table_columns[i], table_column_types[i]));
  }
  const StructType* table_struct = MakeStructType(fields);
  const ArrayType* array_type = MakeArrayType(table_struct);
  table_empty = Value::EmptyArray(array_type);

  // Create non-empty test / result tables.
  table1 = StructArray(table_columns,
                       {
                           {1ll, True(), 0.1, 1ll, "1"},
                           {2ll, False(), 0.2, 2ll, "2"},
                       },
                       kIgnoresOrder);
  table2 = StructArray(table_columns,
                       {
                           {1ll, True(), 0.3, 3ll, "3"},
                           {2ll, False(), 0.4, 4ll, "4"},
                       },
                       kIgnoresOrder);
  table3 = StructArray(table_columns,
                       {
                           {1ll, True(), 0.5, 5ll, "5"},
                           {2ll, False(), 0.6, 6ll, "6"},
                       },
                       kIgnoresOrder);
  table_large = StructArray(
      table_columns,
      {
          {1ll, NullBool(), NullDouble(), NullInt64(), NullString()},
          {2ll, True(), NullDouble(), NullInt64(), NullString()},
          {3ll, False(), 0.2, NullInt64(), NullString()},
          {4ll, True(), 0.3, 3ll, NullString()},
          {5ll, False(), 0.4, 4ll, "4"},
          {6ll, True(), 0.5, 5ll, "5"},
          {7ll, False(), 0.6, 6ll, "6"},
          {8ll, True(), 0.7, 7ll, "7"},
          {9ll, False(), 0.8, 8ll, "8"},
          {10ll, True(), 0.9, 9ll, "9"},
          {11ll, False(), 1.0, 10ll, "10"},
      },
      kIgnoresOrder);
  table_all_null = StructArray(
      table_columns,
      {
          {1ll, NullBool(), NullDouble(), NullInt64(), NullString()},
      },
      kIgnoresOrder);
  table_distincts =
      StructArray({"primary_key", "distinct_1", "distinct_2", "distinct_4",
                   "distinct_8", "distinct_16", "distinct_2B", "distinct_4B"},
                  {
                      {1ll, 1ll, 1ll, 1ll, 1ll, 1ll, /**/ 1ll, 1ll},
                      {2ll, 1ll, 2ll, 2ll, 2ll, 2ll, /**/ 1ll, 1ll},
                      {3ll, 1ll, 1ll, 3ll, 3ll, 3ll, /**/ 1ll, 1ll},
                      {4ll, 1ll, 2ll, 4ll, 4ll, 4ll, /**/ 1ll, 1ll},
                      {5ll, 1ll, 1ll, 1ll, 5ll, 5ll, /**/ 1ll, 2ll},
                      {6ll, 1ll, 2ll, 2ll, 6ll, 6ll, /**/ 1ll, 2ll},
                      {7ll, 1ll, 1ll, 3ll, 7ll, 7ll, /**/ 1ll, 2ll},
                      {8ll, 1ll, 2ll, 4ll, 8ll, 8ll, /**/ 1ll, 2ll},
                      {9ll, 1ll, 1ll, 1ll, 1ll, 9ll, /**/ 2ll, 3ll},
                      {10ll, 1ll, 2ll, 2ll, 2ll, 10ll, /**/ 2ll, 3ll},
                      {11ll, 1ll, 1ll, 3ll, 3ll, 11ll, /**/ 2ll, 3ll},
                      {12ll, 1ll, 2ll, 4ll, 4ll, 12ll, /**/ 2ll, 3ll},
                      {13ll, 1ll, 1ll, 1ll, 5ll, 13ll, /**/ 2ll, 4ll},
                      {14ll, 1ll, 2ll, 2ll, 6ll, 14ll, /**/ 2ll, 4ll},
                      {15ll, 1ll, 1ll, 3ll, 7ll, 15ll, /**/ 2ll, 4ll},
                      {16ll, 1ll, 2ll, 4ll, 8ll, 16ll, /**/ 2ll, 4ll},
                  },
                  kIgnoresOrder);

  code_based_test_driver = GetComplianceTestDriver();

  TestDatabase test_db;
  test_db.tables.emplace("TableEmpty", TestTable{table_empty});
  test_db.tables.emplace("Table1", TestTable{table1});
  test_db.tables.emplace("Table2", TestTable{table2});
  test_db.tables.emplace("Table3", TestTable{table3});
  test_db.tables.emplace("TableLarge", TestTable{table_large});
  test_db.tables.emplace("TableAllNull", TestTable{table_all_null});
  test_db.tables.emplace("TableDistincts", TestTable{table_distincts});
  for (const std::string& proto_file : testing::GoogleSqlTestProtoFilepaths()) {
    test_db.proto_files.insert(proto_file);
  }
  for (const std::string& proto_name : testing::GoogleSqlTestProtoNames()) {
    test_db.proto_names.insert(proto_name);
  }
  for (const std::string& enum_name : testing::GoogleSqlTestEnumNames()) {
    test_db.enum_names.insert(enum_name);
  }

  Value mytable = StructArray({"int64_val", "str_val"},
                              {{1ll, "foo"}, {2ll, "bar"}}, kIgnoresOrder);
  test_db.tables.emplace("MyTable", TestTable{mytable});

  test_db.tables.emplace(
      "T", TestTable{StructArray({"x", "y"}, {{1ll, "foo"}, {2ll, "bar"}})});

  GOOGLESQL_CHECK_OK(SQLTestBase::ValidateFirstColumnPrimaryKey(
      test_db, code_based_test_driver->GetSupportedLanguageOptions()));

  auto test_db_proto = std::make_unique<TestDatabaseProto>();
  GOOGLESQL_ASSERT_OK(SerializeTestDatabase(test_db, test_db_proto.get()));

  absl::Status status = code_based_test_driver->CreateDatabase(*test_db_proto);
  if (!status.ok()) {
    if (status.code() == absl::StatusCode::kUnimplemented &&
        status.message() ==
            "Test driver does not support creating database from proto") {
      ASSERT_FALSE(code_based_test_driver->IsReferenceImplementation());
      GOOGLESQL_ASSERT_OK(code_based_test_driver->CreateDatabase(test_db));
    } else {
      GOOGLESQL_ASSERT_OK(status);
    }
  }

  code_based_reference_driver =
      ReferenceDriver::CreateFromTestDriver(code_based_test_driver).release();
  GOOGLESQL_EXPECT_OK(code_based_reference_driver->CreateDatabase(*test_db_proto));
}

void CodebasedTestsEnvironment::TearDown() {
  delete code_based_test_driver;
  delete code_based_reference_driver;
}

ComplianceCodebasedTests::ComplianceCodebasedTests()
    : SuperclassAlias(code_based_test_driver, code_based_reference_driver) {
  // Sanity check the initialization of the test driver. The superclass
  // constructor will sanity check that 'code_based_reference_driver' is
  // consistent with 'code_based_test_driver'.
  ABSL_CHECK_EQ(GetCodeBasedTestsEnvironment()->skip_codebased_tests(),
           code_based_test_driver == nullptr);

  if (code_based_test_driver == nullptr) {
    // Sanity check that any subclass that overrides DriverCanRunTests() also
    // calls the superclass method
    // ComplianceCodebasedTests::DriverCanRunTests().
    ABSL_CHECK(!DriverCanRunTests());
  }
}

ComplianceCodebasedTests::~ComplianceCodebasedTests() {}

void ComplianceCodebasedTests::RunStatementTests(
    absl::Span<const QueryParamsWithResult> statement_tests,
    absl::string_view sql_string) {
  return RunStatementTestsCustom(
      statement_tests,
      [sql_string = std::string(sql_string)](const QueryParamsWithResult& p) {
        return sql_string;
      });
}

void ComplianceCodebasedTests::RunFunctionTestsInfix(
    absl::Span<const QueryParamsWithResult> function_tests,
    absl::string_view operator_name) {
  return RunStatementTestsCustom(
      function_tests, [operator_name = std::string(operator_name)](
                          const QueryParamsWithResult& p) {
        return ParametersWithSeparator(p.num_params(),
                                       absl::StrCat(" ", operator_name, " "));
      });
}

void ComplianceCodebasedTests::RunFunctionTestsInOperator(
    absl::Span<const QueryParamsWithResult> function_tests) {
  return RunStatementTestsCustom(
      function_tests, [](const QueryParamsWithResult& p) {
        std::vector<std::string> arg_str;
        for (int i = 1; i < p.num_params(); i++) {
          arg_str.push_back(absl::StrCat("@p", i));
        }
        return absl::StrCat("@p0 IN (", absl::StrJoin(arg_str, ","), ")");
      });
}

void ComplianceCodebasedTests::RunFunctionTestsInOperatorUnnestArray(
    absl::Span<const QueryParamsWithResult> function_tests) {
  return RunStatementTestsCustom(
      function_tests, [](const QueryParamsWithResult& p) {
        std::vector<std::string> arg_str;
        for (int i = 1; i < p.num_params(); i++) {
          arg_str.push_back(absl::StrCat("@p", i));
        }
        return absl::StrCat("@p0 IN UNNEST([", absl::StrJoin(arg_str, ","),
                            "])");
      });
}

void ComplianceCodebasedTests::RunFunctionTestsPrefix(
    absl::Span<const QueryParamsWithResult> function_tests,
    absl::string_view function_name) {
  return RunStatementTestsCustom(
      function_tests, [function_name = std::string(function_name)](
                          const QueryParamsWithResult& p) {
        return absl::StrCat(function_name, "(",
                            ParametersWithSeparator(p.num_params(), ", "), ")");
      });
}

template <typename FCT>
void ComplianceCodebasedTests::RunFunctionTestsCustom(
    absl::Span<const FunctionTestCall> function_tests, FCT get_sql_string) {
  for (const auto& params : function_tests) {
    std::string pattern = get_sql_string(params);
    std::string sql = absl::StrCat("SELECT ", pattern, " AS ", kColA);
    QueryParamsWithResult new_params = params.params;
    ConvertResultsToSingletons(&new_params);
    SetNamePrefix(params.function_name);
    auto label = MakeScopedLabel(GetTypeLabels(params.params));
    RunStatementOnFeatures(sql, new_params);
  }
}

std::vector<FunctionTestCall> ComplianceCodebasedTests::AddSafeFunctionCalls(
    const std::vector<FunctionTestCall>& calls) {
  std::vector<FunctionTestCall> safe_calls = calls;
  for (const FunctionTestCall& call : calls) {
    bool has_errors = false;
    for (const auto& map_iter : call.params.results()) {
      if (!map_iter.second.status.ok()) {
        has_errors = true;
      }
    }
    // Don't add SAFE tests for functions that never had any errors.
    if (!has_errors) continue;

    FunctionTestCall safe_call = call;
    safe_call.params.AddRequiredFeature(FEATURE_SAFE_FUNCTION_CALL);
    safe_call.function_name = absl::StrCat("safe.", call.function_name);
    safe_call.params.MutateResult(
        [](QueryParamsWithResult::Result& mutable_result) {
          if (mutable_result.status.code() == absl::StatusCode::kOutOfRange) {
            mutable_result.status = absl::OkStatus();
            mutable_result.result = Value::Null(mutable_result.result.type());
          }
        });
    safe_calls.push_back(safe_call);
  }
  return safe_calls;
}

// If a function name starts with "safe.", add a prefix "safe_error_mode__"
// onto it.  This makes it easier to add known error entries like
// "safe_error_mode__.*" without excluding tests for function that actually
// start with "SAFE_".
static std::string AddPrefixForSafeFunctionCalls(
    absl::string_view function_name) {
  if (absl::StartsWith(function_name, "safe.")) {
    return absl::StrCat("safe_error_mode__", function_name);
  }
  return std::string(function_name);
}

void ComplianceCodebasedTests::RunFunctionCalls(
    const std::vector<FunctionTestCall>& function_calls) {
  for (const auto& call : AddSafeFunctionCalls(function_calls)) {
    std::string sql = absl::Substitute(
        "SELECT $0($1) AS $2", call.function_name,
        ParametersWithSeparator(call.params.num_params(), ", "), kColA);
    QueryParamsWithResult new_params = call.params;
    ConvertResultsToSingletons(&new_params);
    SetNamePrefix(AddPrefixForSafeFunctionCalls(call.function_name));
    auto label = MakeScopedLabel(GetTypeLabels(call.params));
    RunStatementOnFeatures(sql, new_params);
  }
}

void ComplianceCodebasedTests::RunFunctionCalls(
    absl::Span<const QueryParamsWithResult> test_cases,
    absl::string_view function_name) {
  std::vector<FunctionTestCall> function_test_calls;
  function_test_calls.reserve(test_cases.size());
  for (const QueryParamsWithResult& test_case : test_cases) {
    function_test_calls.push_back({function_name, test_case});
  }
  RunFunctionCalls(function_test_calls);
}

void ComplianceCodebasedTests::RunFunctionCallsWithNamedValueArguments(
    const std::vector<FunctionTestCall>& function_calls) {
  for (const auto& call : AddSafeFunctionCalls(function_calls)) {
    std::string sql = absl::Substitute(
        "SELECT $0($1) AS $2", call.function_name,
        InitParametersWithNamedValueArguments(call.params, ", "), kColA);
    QueryParamsWithResult new_params = call.params;
    ConvertResultsToSingletons(&new_params);
    SetNamePrefix(AddPrefixForSafeFunctionCalls(call.function_name));
    auto label = MakeScopedLabel(GetTypeLabels(call.params));
    RunStatementOnFeatures(sql, new_params);
  }
}

void ComplianceCodebasedTests::RunNormalizeFunctionCalls(
    const std::vector<FunctionTestCall>& function_calls) {
  for (const auto& call : AddSafeFunctionCalls(function_calls)) {
    std::string sql = absl::Substitute(
        "SELECT $0(@p0$1) AS $2", call.function_name,
        call.params.num_params() <= 1
            ? ""
            : absl::StrCat(", ", call.params.param(1).EnumDisplayName()),
        kColA);
    QueryParamsWithResult new_params = call.params;
    ConvertResultsToSingletons(&new_params);
    SetNamePrefix(AddPrefixForSafeFunctionCalls(call.function_name));
    auto label = MakeScopedLabel(GetTypeLabels(call.params));
    RunStatementOnFeatures(sql, new_params);
  }
}

void ComplianceCodebasedTests::RunAggregationFunctionCalls(
    const std::vector<FunctionTestCall>& function_calls) {
  for (const auto& call : AddSafeFunctionCalls(function_calls)) {
    std::vector<std::string> arg_str = {"x"};
    for (int i = 1; i < call.params.num_params(); i++) {
      arg_str.push_back(absl::StrCat("@p", i));
    }

    std::string sql = absl::Substitute(
        "SELECT $0($1) AS $2 FROM UNNEST(@p0) AS x", call.function_name,
        absl::StrJoin(arg_str, ", "), kColA);
    QueryParamsWithResult new_params = call.params;
    ConvertResultsToSingletons(&new_params);
    SetNamePrefix(AddPrefixForSafeFunctionCalls(call.function_name));
    auto label = MakeScopedLabel(GetTypeLabels(call.params));
    RunStatementOnFeatures(sql, new_params);
  }
}

template <typename FCT>
void ComplianceCodebasedTests::RunStatementTestsCustom(
    absl::Span<const QueryParamsWithResult> statement_tests,
    FCT get_sql_string) {
  for (const auto& params : statement_tests) {
    std::string pattern = get_sql_string(params);
    std::string sql = absl::StrCat("SELECT ", pattern, " AS ", kColA);
    QueryParamsWithResult new_params = params;
    ConvertResultsToSingletons(&new_params);
    SetResultTypeName(params.result().type()->TypeName(PRODUCT_INTERNAL));
    auto label = MakeScopedLabel(GetTypeLabels(params));
    RunStatementOnFeatures(sql, new_params);
  }
}

void ComplianceCodebasedTests::RunStatementOnFeatures(
    absl::string_view sql, const QueryParamsWithResult& params) {
  if (!DriverCanRunTests()) {
    return;
  }

  std::map<std::string, Value> param_map;
  for (int i = 0; i < params.num_params(); i++) {
    std::string param_name = absl::StrCat("p", i);
    param_map[param_name] = params.param(i);
  }
  RunSQLOnFeaturesAndValidateResult(
      sql, param_map, params.required_features(), params.prohibited_features(),
      params.result(), params.status(), params.float_margin());
}

std::vector<QueryParamsWithResult>
ComplianceCodebasedTests::GetFunctionTestsDateArithmetics(
    absl::Span<const FunctionTestCall> tests) {
  std::vector<QueryParamsWithResult> out;
  for (const auto& test : tests) {
    // Only look at tests which use DAY as datepart
    if (test.params.param(2).is_null() ||
        test.params.param(2).enum_value() !=
            functions::DateTimestampPart::DAY) {
      continue;
    }
    QueryParamsWithResult params = test.params;
    params.AddRequiredFeature(FEATURE_DATE_ARITHMETICS);
    out.push_back(params);
  }
  return out;
}

TEST_F(ComplianceCodebasedTests, TestQueryParameters) {
  if (!DriverCanRunTests()) {
    return;
  }
  // Tests query parameter support.
  SetNamePrefix("Param");
  EXPECT_THAT(RunSQL("SELECT @ColA AS ColA", {{"ColA", Int64(5)}}),
              Returns(Singleton(Int64(5))));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestDepthLimitDetectorTestCases, 40) {
  for (const DepthLimitDetectorTestCase& depth_case :
       Shard(AllDepthLimitDetectorTestCases())) {
    bool driver_enables_right_features = true;
    LanguageOptions options =
        DepthLimitDetectorTestCaseLanguageOptions(depth_case);
    for (LanguageFeature feature : options.GetEnabledLanguageFeatures()) {
      driver_enables_right_features &= DriverSupportsFeature(feature);
    }
    if (!driver_enables_right_features) {
      ABSL_LOG(INFO) << "Skipping " << depth_case
                << " as not all features supported";
      continue;
    }
    auto label = MakeScopedLabel(absl::StrFormat(
        "DepthLimitDetector:%s", absl::FormatStreamed(depth_case)));
    SetNamePrefix(absl::StrFormat("DepthLimitDetector_%s",
                                  absl::FormatStreamed(depth_case)));
    absl::Status run_small =
        RunSQL(DepthLimitDetectorTemplateToString(depth_case, 3)).status();
    if (!run_small.ok()) {
      ABSL_LOG(INFO) << "Skipping " << depth_case << " as small example returned "
                << run_small;
      continue;
    }

    DepthLimitDetectorTestResult result =
        RunDepthLimitDetectorTestCase(depth_case, [&](absl::string_view sql) {
          return driver()
              ->ExecuteStatement(std::string(sql), {},
                                 execute_statement_type_factory())
              .status();
        });
    ABSL_LOG(INFO) << "Depth limit disection finished: " << result;
  }
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestArithmeticFunctions_Negative, 1) {
  SetNamePrefix("Negative");
  RunStatementTests(Shard(GetFunctionTestsUnaryMinus()), "-@p0");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestArithmeticFunctions_Add, 1) {
  SetNamePrefix("Add");
  RunStatementTests(Shard(GetFunctionTestsAdd()), "@p0 + @p1");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestArithmeticFunctions_Subtract, 1) {
  SetNamePrefix("Subtract");
  RunStatementTests(Shard(GetFunctionTestsSubtract()), "@p0 - @p1");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestArithmeticFunctions_Multiply, 1) {
  SetNamePrefix("Multiply");
  RunStatementTests(Shard(GetFunctionTestsMultiply()), "@p0 * @p1");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestArithmeticFunctions_Divide, 1) {
  SetNamePrefix("Divide");
  RunStatementTests(Shard(GetFunctionTestsDivide()), "@p0 / @p1");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestArithmeticFunctions_Mod, 1) {
  RunFunctionCalls(Shard(GetFunctionTestsModulo()), "MOD");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestArithmeticFunctions_Div, 1) {
  RunFunctionCalls(Shard(GetFunctionTestsDiv()), "DIV");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestArithmeticFunctions_CoercedAdd,
               1) {
  SetNamePrefix("CoercedAdd");
  RunStatementTests(Shard(GetFunctionTestsCoercedAdd()), "@p0 + @p1");
}

SHARDED_TEST_F(ComplianceCodebasedTests,
               TestArithmeticFunctions_CoercedSubtract, 1) {
  SetNamePrefix("CoercedSubtract");
  RunStatementTests(Shard(GetFunctionTestsCoercedSubtract()), "@p0 - @p1");
}

SHARDED_TEST_F(ComplianceCodebasedTests,
               TestArithmeticFunctions_CoercedMultiply, 1) {
  SetNamePrefix("CoercedMultiply");
  RunStatementTests(Shard(GetFunctionTestsCoercedMultiply()), "@p0 * @p1");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestArithmeticFunctions_CoercedDivide,
               1) {
  SetNamePrefix("CoercedDivide");
  RunStatementTests(Shard(GetFunctionTestsCoercedDivide()), "@p0 / @p1");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestArithmeticFunctions_CoercedMod,
               1) {
  RunFunctionCalls(Shard(GetFunctionTestsCoercedModulo()), "MOD");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestArithmeticFunctions_CoercedDiv,
               1) {
  RunFunctionCalls(Shard(GetFunctionTestsCoercedDiv()), "DIV");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestArithmeticFunctions_SafeAdd, 1) {
  RunFunctionCalls(Shard(GetFunctionTestsSafeAdd()), "SAFE_ADD");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestArithmeticFunctions_SafeSubtract,
               1) {
  RunFunctionCalls(Shard(GetFunctionTestsSafeSubtract()), "SAFE_SUBTRACT");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestArithmeticFunctions_SafeMultiply,
               1) {
  RunFunctionCalls(Shard(GetFunctionTestsSafeMultiply()), "SAFE_MULTIPLY");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestArithmeticFunctions_SafeDivide,
               1) {
  RunFunctionCalls(Shard(GetFunctionTestsSafeDivide()), "SAFE_DIVIDE");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestArithmeticFunctions_SafeNegate,
               1) {
  RunFunctionCalls(Shard(GetFunctionTestsSafeNegate()), "SAFE_NEGATE");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestArrayConcatOperator, 1) {
  SetNamePrefix("ConcatOperator");
  RunStatementTests(Shard(GetFunctionTestsArrayConcatOperator()), "@p0 || @p1");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestStringConcatOperator, 1) {
  SetNamePrefix("ConcatOperator");
  RunStatementTests(Shard(GetFunctionTestsStringConcatOperator()),
                    "@p0 || @p1");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestGreatestFunctions, 5) {
  SetNamePrefix("Greatest");
  RunFunctionTestsPrefix(Shard(GetFunctionTestsGreatest()), "Greatest");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestLeastFunctions, 4) {
  SetNamePrefix("Least");
  RunFunctionTestsPrefix(Shard(GetFunctionTestsLeast()), "Least");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestArrayFirstFunctions, 1) {
  SetNamePrefix("ArrayFirst");
  RunFunctionTestsPrefix(Shard(GetFunctionTestsArrayFirst(/*is_safe=*/false)),
                         "ARRAY_FIRST");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestSafeArrayFirstFunctions, 1) {
  SetNamePrefix("SafeArrayFirst");
  RunFunctionTestsPrefix(Shard(GetFunctionTestsArrayFirst(/*is_safe=*/true)),
                         "SAFE.ARRAY_FIRST");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestArrayLastFunctions, 1) {
  SetNamePrefix("ArrayLast");
  RunFunctionTestsPrefix(Shard(GetFunctionTestsArrayLast(/*is_safe=*/false)),
                         "ARRAY_LAST");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestSafeArrayLastFunctions, 1) {
  SetNamePrefix("SafeArrayLast");
  RunFunctionTestsPrefix(Shard(GetFunctionTestsArrayLast(/*is_safe=*/true)),
                         "SAFE.ARRAY_LAST");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestArraySliceFunctions, 2) {
  SetNamePrefix("ArraySlice");
  RunFunctionTestsPrefix(Shard(GetFunctionTestsArraySlice(/*is_safe=*/false)),
                         "ARRAY_SLICE");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestSafeArraySliceFunctions, 2) {
  SetNamePrefix("SafeArraySlice");
  RunFunctionTestsPrefix(Shard(GetFunctionTestsArraySlice(/*is_safe=*/true)),
                         "SAFE.ARRAY_SLICE");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestArrayFirstNFunctions, 1) {
  SetNamePrefix("ArrayFirstN");
  RunFunctionTestsPrefix(Shard(GetFunctionTestsArrayFirstN(/*is_safe=*/false)),
                         "ARRAY_FIRST_N");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestSafeArrayFirstNFunctions, 1) {
  SetNamePrefix("SafeArrayFirstN");
  RunFunctionTestsPrefix(Shard(GetFunctionTestsArrayFirstN(/*is_safe=*/true)),
                         "SAFE.ARRAY_FIRST_N");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestArrayLastNFunctions, 1) {
  SetNamePrefix("ArrayLastN");
  RunFunctionTestsPrefix(Shard(GetFunctionTestsArrayLastN(/*is_safe=*/false)),
                         "ARRAY_LAST_N");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestSafeArrayLastNFunctions, 1) {
  SetNamePrefix("SafeArrayLastN");
  RunFunctionTestsPrefix(Shard(GetFunctionTestsArrayLastN(/*is_safe=*/true)),
                         "SAFE.ARRAY_LAST_N");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestArrayRemoveFirstNFunctions, 1) {
  SetNamePrefix("ArrayRemoveFirstN");
  RunFunctionTestsPrefix(
      Shard(GetFunctionTestsArrayRemoveFirstN(/*is_safe=*/false)),
      "ARRAY_REMOVE_FIRST_N");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestSafeArrayRemoveFirstNFunctions,
               1) {
  SetNamePrefix("SafeArrayRemoveFirstN");
  RunFunctionTestsPrefix(
      Shard(GetFunctionTestsArrayRemoveFirstN(/*is_safe=*/true)),
      "SAFE.ARRAY_REMOVE_FIRST_N");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestArrayRemoveLastNFunctions, 1) {
  SetNamePrefix("ArrayRemoveLastN");
  RunFunctionTestsPrefix(
      Shard(GetFunctionTestsArrayRemoveLastN(/*is_safe=*/false)),
      "ARRAY_REMOVE_LAST_N");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestSafeArrayRemoveLastNFunctions, 1) {
  SetNamePrefix("SafeArrayRemoveLastN");
  RunFunctionTestsPrefix(
      Shard(GetFunctionTestsArrayRemoveLastN(/*is_safe=*/true)),
      "SAFE.ARRAY_REMOVE_LAST_N");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestArrayMinFunctions, 1) {
  SetNamePrefix("ArrayMin");
  RunFunctionTestsPrefix(Shard(GetFunctionTestsArrayMin(/*is_safe=*/false)),
                         "ARRAY_MIN");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestSafeArrayMinFunctions, 1) {
  SetNamePrefix("SafeArrayMin");
  RunFunctionTestsPrefix(Shard(GetFunctionTestsArrayMin(/*is_safe=*/true)),
                         "SAFE.ARRAY_MIN");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestArrayMaxFunctions, 1) {
  SetNamePrefix("ArrayMax");
  RunFunctionTestsPrefix(Shard(GetFunctionTestsArrayMax(/*is_safe=*/false)),
                         "ARRAY_MAX");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestSafeArrayMaxFunctions, 1) {
  SetNamePrefix("SafeArrayMax");
  RunFunctionTestsPrefix(Shard(GetFunctionTestsArrayMax(/*is_safe=*/true)),
                         "SAFE.ARRAY_MAX");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestArraySumFunctions, 1) {
  SetNamePrefix("ArraySum");
  RunFunctionTestsPrefix(Shard(GetFunctionTestsArraySum(/*is_safe=*/false)),
                         "ARRAY_SUM");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestSafeArraySumFunctions, 1) {
  SetNamePrefix("SafeArraySum");
  RunFunctionTestsPrefix(Shard(GetFunctionTestsArraySum(/*is_safe=*/true)),
                         "SAFE.ARRAY_SUM");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestArrayAvgFunctions, 1) {
  SetNamePrefix("ArrayAvg");
  RunFunctionTestsPrefix(Shard(GetFunctionTestsArrayAvg(/*is_safe=*/false)),
                         "ARRAY_AVG");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestSafeArrayAvgFunctions, 1) {
  SetNamePrefix("SafeArrayAvg");
  RunFunctionTestsPrefix(Shard(GetFunctionTestsArrayAvg(/*is_safe=*/true)),
                         "SAFE.ARRAY_AVG");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestArrayOffsetFunctions, 3) {
  SetNamePrefix("ArrayOffset");
  RunFunctionTestsPrefix(Shard(GetFunctionTestsArrayOffset(/*is_safe=*/false)),
                         "ARRAY_OFFSET");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestSafeArrayOffsetFunctions, 3) {
  SetNamePrefix("SafeArrayOffset");
  RunFunctionTestsPrefix(Shard(GetFunctionTestsArrayOffset(/*is_safe=*/true)),
                         "SAFE.ARRAY_OFFSET");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestArrayFindFunctions, 3) {
  SetNamePrefix("ArrayFind");
  RunFunctionTestsPrefix(Shard(GetFunctionTestsArrayFind(/*is_safe=*/false)),
                         "ARRAY_FIND");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestSafeArrayFindFunctions, 3) {
  SetNamePrefix("SafeArrayFind");
  RunFunctionTestsPrefix(Shard(GetFunctionTestsArrayFind(/*is_safe=*/true)),
                         "SAFE.ARRAY_FIND");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestArrayOffsetsFunctions, 1) {
  SetNamePrefix("ArrayOffsets");
  RunFunctionTestsPrefix(Shard(GetFunctionTestsArrayOffsets(/*is_safe=*/false)),
                         "ARRAY_OFFSETS");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestSafeArrayOffsetsFunctions, 1) {
  SetNamePrefix("SafeArrayOffsets");
  RunFunctionTestsPrefix(Shard(GetFunctionTestsArrayOffsets(/*is_safe=*/true)),
                         "SAFE.ARRAY_OFFSETS");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestArrayFindAllFunctions, 1) {
  SetNamePrefix("ArrayFindAll");
  RunFunctionTestsPrefix(Shard(GetFunctionTestsArrayFindAll(/*is_safe=*/false)),
                         "ARRAY_FIND_ALL");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestSafeArrayFindAllFunctions, 1) {
  SetNamePrefix("SafeArrayFindAll");
  RunFunctionTestsPrefix(Shard(GetFunctionTestsArrayFindAll(/*is_safe=*/true)),
                         "SAFE.ARRAY_FIND_ALL");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestLogicalFunctions_AND, 1) {
  SetNamePrefix("And");
  RunFunctionTestsInfix(Shard(GetFunctionTestsAnd()), "AND");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestLogicalFunctions_OR, 1) {
  SetNamePrefix("Or");
  RunFunctionTestsInfix(Shard(GetFunctionTestsOr()), "OR");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestLogicalFunctions_NOT, 1) {
  SetNamePrefix("Not");
  RunStatementTests(Shard(GetFunctionTestsNot()), "NOT @p0");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestComparisonFunctions_EQ, 3) {
  SetNamePrefix("EQ");
  RunStatementTests(Shard(GetFunctionTestsEqual()), "@p0 = @p1");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestComparisonFunctions_NE, 3) {
  SetNamePrefix("NE");
  RunStatementTests(Shard(GetFunctionTestsNotEqual()), "@p0 != @p1");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestComparisonFunctions_GT, 2) {
  SetNamePrefix("GT");
  RunStatementTests(Shard(GetFunctionTestsGreater()), "@p0 > @p1");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestComparisonFunctions_GE, 3) {
  SetNamePrefix("GE");
  RunStatementTests(Shard(GetFunctionTestsGreaterOrEqual()), "@p0 >= @p1");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestComparisonFunctions_StructIn, 1) {
  SetNamePrefix("IN");
  RunFunctionTestsInOperator(Shard(GetFunctionTestsStructIn()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestComparisonFunctions_InList, 1) {
  SetNamePrefix("InList");
  RunFunctionTestsInOperator(Shard(GetFunctionTestsIn()));
}

SHARDED_TEST_F(ComplianceCodebasedTests,
               TestComparisonFunctions_InArrayWithNulls, 1) {
  // SELECT <expr> IN UNNEST([...<array with NULLs>...]])
  SetNamePrefix("InArray_WithNulls");
  RunFunctionTestsInOperatorUnnestArray(Shard(GetFunctionTestsInWithNulls()));
}

SHARDED_TEST_F(ComplianceCodebasedTests,
               TestComparisonFunctions_InArrayWithoutNulls, 1) {
  // SELECT <expr> IN UNNEST([...<array without NULLs>...]])
  SetNamePrefix("InArray_WithoutNulls");
  RunFunctionTestsInOperatorUnnestArray(
      Shard(GetFunctionTestsInWithoutNulls()));

  // Empty matching set.
  RunFunctionTestsInOperatorUnnestArray(
      {{{String("x")}, false}, {{NullString()}, false}});
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestComparisonFunctions_IsNull, 1) {
  SetNamePrefix("IsNull");
  RunStatementTests(Shard(GetFunctionTestsIsNull()), "@p0 IS NULL");
  RunStatementTests(Shard(WrapFeatureJSON(GetFunctionTestsJsonIsNull())),
                    "@p0 IS NULL");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestComparisonFunctions_IsNotNull, 1) {
  SetNamePrefix("IsNotNull");
  RunStatementTests(InvertResults(Shard(GetFunctionTestsIsNull())),
                    "@p0 IS NOT NULL");
  RunStatementTests(
      WrapFeatureJSON(InvertResults(Shard(GetFunctionTestsJsonIsNull()))),
      "@p0 IS NOT NULL");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestComparisonFunctions_LT, 2) {
  SetNamePrefix("LT");
  RunStatementTests(Shard(GetFunctionTestsLess()), "@p0 < @p1");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestComparisonFunctions_LE, 3) {
  SetNamePrefix("LE");
  RunStatementTests(Shard(GetFunctionTestsLessOrEqual()), "@p0 <= @p1");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestCastFunction, 14) {
  // TODO: This needs to be sensitive to ProductMode, or
  // maybe just switched to PRODUCT_EXTERNAL.
  auto format_fct = [](const QueryParamsWithResult& p) {
    std::string optional_format_param = "";
    if (p.num_params() == 2 || p.num_params() == 3) {
      optional_format_param = absl::StrCat(
          " FORMAT ",
          p.param(1).is_null()
              ? "NULL"
              : absl::StrCat("\'", p.param(1).string_value(), "\'"));
      if (p.num_params() == 3) {
        absl::StrAppend(
            &optional_format_param, " AT TIME ZONE ",
            p.param(2).is_null()
                ? "CAST(NULL AS STRING)"
                : absl::StrCat("\'", p.param(2).string_value(), "\'"));
      }
    }
    return absl::StrCat("CAST(@p0 AS ",
                        p.result().type()->TypeName(PRODUCT_INTERNAL),
                        optional_format_param, ")");
  };
  SetNamePrefix("CastTo", true /* Need Result Type */);
  RunStatementTestsCustom(Shard(GetFunctionTestsCast()), format_fct);
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestSafeCastFunction, 4) {
  auto format_fct = [](const QueryParamsWithResult& p) {
    std::string optional_format_param = "";
    if (p.num_params() == 2 || p.num_params() == 3) {
      optional_format_param = absl::StrCat(
          " FORMAT ",
          p.param(1).is_null()
              ? "NULL"
              : absl::StrCat("\'", p.param(1).string_value(), "\'"));
      if (p.num_params() == 3) {
        absl::StrAppend(
            &optional_format_param, " AT TIME ZONE ",
            p.param(2).is_null()
                ? "NULL"
                : absl::StrCat("\'", p.param(2).string_value(), "\'"));
      }
    }
    return absl::StrCat("SAFE_CAST(@p0 AS ",
                        p.result().type()->TypeName(PRODUCT_INTERNAL),
                        optional_format_param, ")");
  };
  SetNamePrefix("SafeCastTo", true /* Need Result Type */);
  RunStatementTestsCustom(Shard(GetFunctionTestsSafeCast()), format_fct);
}

SHARDED_TEST_F(ComplianceCodebasedTests,
               TestCastArraysWithNullsOfDifferentTypesFunction, 3) {
  auto format_fct = [](const QueryParamsWithResult& p) {
    return absl::StrCat("CAST(@p0 AS ",
                        p.result().type()->TypeName(PRODUCT_INTERNAL), ")");
  };
  SetNamePrefix("CastArraysBetweenDifferentType_WithNulls",
                true /* Need Result Type */);
  RunStatementTestsCustom(Shard(GetFunctionTestsCastBetweenDifferentArrayTypes(
                              /*arrays_with_nulls=*/true)),
                          format_fct);
}

SHARDED_TEST_F(ComplianceCodebasedTests,
               TestCastArraysWithoutNullsOfDifferentTypesFunction, 3) {
  auto format_fct = [](const QueryParamsWithResult& p) {
    return absl::StrCat("CAST(@p0 AS ",
                        p.result().type()->TypeName(PRODUCT_INTERNAL), ")");
  };
  SetNamePrefix("CastArraysBetweenDifferentType_WithoutNulls",
                true /* Need Result Type */);
  RunStatementTestsCustom(Shard(GetFunctionTestsCastBetweenDifferentArrayTypes(
                              /*arrays_with_nulls=*/false)),
                          format_fct);
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestCastMapFunction, 5) {
  auto format_fct = [](const QueryParamsWithResult& p) {
    return absl::StrCat("CAST(@p0 AS ",
                        p.result().type()->TypeName(PRODUCT_INTERNAL), ")");
  };
  SetNamePrefix("CastMap", /*need_result_type_name=*/true);
  RunStatementTestsCustom(Shard(GetFunctionTestsMapCast()), format_fct);
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestBitCastFunctions, 1) {
  SetNamePrefix("BitCast");
  RunFunctionCalls(Shard(GetFunctionTestsBitCast()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestGenerateArray, 1) {
  SetNamePrefix("GenerateArray");
  RunFunctionCalls(Shard(GetFunctionTestsGenerateArray()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestRangeBucket, 1) {
  SetNamePrefix("RangeBucket");
  RunFunctionCalls(Shard(GetFunctionTestsRangeBucket()));
}

namespace {

// Wraps test cases for functions that take a DateTimestampPart enum arguments.
// Converts the date/time part arguments to strings so that they are not
// added as enum-typed parameter bindings which can trip engines that don't
// support enum types. The date/time part parameter bindings can anyways not be
// used as arguments in the actual functions as they always have to be literals
// and so do not actually appear in the generated test case query.
std::vector<FunctionTestCall> ConvertDateTimePartBindingsToString(
    std::vector<FunctionTestCall> tests, int date_part_param_idx) {
  for (auto& test_case : tests) {
    if (test_case.params.num_params() <= date_part_param_idx) {
      continue;
    }
    const auto& date_part_param = test_case.params.param(date_part_param_idx);
    *test_case.params.mutable_param(date_part_param_idx) = Value::String(
        date_part_param.is_null()
            ? "NULL"
            : functions::DateTimestampPartToSQL(date_part_param.enum_value()));
  }
  return tests;
}

}  // namespace

SHARDED_TEST_F(ComplianceCodebasedTests, TestGenerateDateArray, 1) {
  SetNamePrefix("GenerateDateArray");
  auto generate_date_array_format_fct = [](const FunctionTestCall& f) {
    if (f.params.params().size() == 2) {
      return absl::Substitute("$0(@p0, @p1)", f.function_name);
    }
    if (f.params.params().size() == 3) {
      // Degenerate case.
      return absl::Substitute("$0(@p0, @p1, INTERVAL @p2)", f.function_name);
    }
    return absl::Substitute("$0(@p0, @p1, INTERVAL @p2 $1)", f.function_name,
                            f.params.param(3).string_value());
  };
  RunFunctionTestsCustom(
      Shard(ConvertDateTimePartBindingsToString(
          GetFunctionTestsGenerateDateArray(), /*date_part_param_idx=*/3)),
      generate_date_array_format_fct);
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestGenerateTimestampArray, 1) {
  SetNamePrefix("GenerateTimestampArray");
  auto generate_timestamp_array_format_fct = [](const FunctionTestCall& f) {
    if (f.params.params().size() == 2) {
      return absl::Substitute("$0(@p0, @p1)", f.function_name);
    }
    if (f.params.params().size() == 3) {
      // Degenerate case.
      return absl::Substitute("$0(@p0, @p1, INTERVAL @p2)", f.function_name);
    }
    return absl::Substitute("$0(@p0, @p1, INTERVAL @p2 $1)", f.function_name,
                            f.params.param(3).string_value());
  };
  RunFunctionTestsCustom(
      Shard(ConvertDateTimePartBindingsToString(
          GetFunctionTestsGenerateTimestampArray(), /*date_part_param_idx=*/3)),
      generate_timestamp_array_format_fct);
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestStringJsonQuery, 1) {
  SetNamePrefix("StringJsonQuery");
  RunFunctionCalls(Shard(GetFunctionTestsStringJsonQuery()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestStringJsonExtract, 1) {
  SetNamePrefix("StringJsonExtract");
  RunFunctionCalls(Shard(GetFunctionTestsStringJsonExtract()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestStringJsonValue, 1) {
  SetNamePrefix("StringJsonValue");
  RunFunctionCalls(Shard(GetFunctionTestsStringJsonValue()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestStringJsonExtractScalar, 1) {
  SetNamePrefix("StringJsonExtractScalar");
  RunFunctionCalls(Shard(GetFunctionTestsStringJsonExtractScalar()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestStringJsonQueryArray, 1) {
  SetNamePrefix("StringJsonQueryArray");
  RunFunctionCalls(Shard(GetFunctionTestsStringJsonQueryArray()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestStringJsonExtractArray, 1) {
  SetNamePrefix("StringJsonExtractArray");
  RunFunctionCalls(Shard(GetFunctionTestsStringJsonExtractArray()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestStringJsonValueArray, 1) {
  SetNamePrefix("StringJsonValueArray");
  RunFunctionCalls(Shard(GetFunctionTestsStringJsonValueArray()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestStringJsonExtractStringArray, 1) {
  SetNamePrefix("StringJsonExtractStringArray");
  RunFunctionCalls(Shard(GetFunctionTestsStringJsonExtractStringArray()));
}

namespace {

// Adds FEATURE_JSON_TYPE as a required feature to all tests in 'tests'.
std::vector<FunctionTestCall> EnableJsonFeatureForTest(
    std::vector<FunctionTestCall> tests) {
  for (auto& test_case : tests) {
    test_case.params.AddRequiredFeature(FEATURE_JSON_TYPE);
  }
  return tests;
}

// Wraps test cases with FEATURE_JSON_VALUE_EXTRACTION_FUNCTIONS.
// If a test case already has a feature set, do not wrap it.
std::vector<FunctionTestCall> EnableJsonValueExtractionFunctionsForTest(
    std::vector<FunctionTestCall> tests) {
  for (auto& test_case : tests) {
    if (test_case.params.required_features().empty()) {
      test_case.params = test_case.params.WrapWithFeatureSet(
          {FEATURE_JSON_TYPE, FEATURE_JSON_VALUE_EXTRACTION_FUNCTIONS});
    }
  }
  return tests;
}

// Wraps test cases with FEATURE_JSON_LAX_VALUE_EXTRACTION_FUNCTIONS.
// If a test case already has a feature set, do not wrap it.
std::vector<FunctionTestCall> EnableJsonLaxValueExtractionFunctionsForTest(
    std::vector<FunctionTestCall> tests) {
  for (auto& test_case : tests) {
    if (test_case.params.required_features().empty()) {
      test_case.params = test_case.params.WrapWithFeatureSet(
          {FEATURE_JSON_TYPE, FEATURE_JSON_LAX_VALUE_EXTRACTION_FUNCTIONS});
    }
  }
  return tests;
}

// Wraps test cases with FEATURE_JSON_CONSTRUCTOR_FUNCTIONS and
// FEATURE_JSON_TYPE.
std::vector<FunctionTestCall> EnableJsonConstructorFunctionsForTest(
    std::vector<FunctionTestCall> tests) {
  for (auto& test_case : tests) {
    test_case.params = test_case.params.AddRequiredFeatures(
        {FEATURE_JSON_TYPE, FEATURE_JSON_CONSTRUCTOR_FUNCTIONS});
  }
  return tests;
}

// Wraps test cases with FEATURE_JSON_MUTATOR_FUNCTIONS and
// FEATURE_JSON_TYPE.
std::vector<FunctionTestCall> EnableJsonMutatorFunctionsForTest(
    std::vector<FunctionTestCall> tests) {
  for (auto& test_case : tests) {
    test_case.params = test_case.params.AddRequiredFeatures(
        {FEATURE_JSON_TYPE, FEATURE_JSON_MUTATOR_FUNCTIONS});
  }
  return tests;
}

}  // namespace

SHARDED_TEST_F(ComplianceCodebasedTests, TestNativeJsonQuery, 1) {
  SetNamePrefix("NativeJsonQuery");
  RunFunctionCalls(
      Shard(EnableJsonFeatureForTest(GetFunctionTestsNativeJsonQuery())));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestNativeJsonExtract, 1) {
  SetNamePrefix("NativeJsonExtract");
  RunFunctionCalls(
      Shard(EnableJsonFeatureForTest(GetFunctionTestsNativeJsonExtract())));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestNativeJsonValue, 1) {
  SetNamePrefix("NativeJsonValue");
  RunFunctionCalls(
      Shard(EnableJsonFeatureForTest(GetFunctionTestsNativeJsonValue())));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestNativeJsonExtractScalar, 1) {
  SetNamePrefix("NativeJsonExtractScalar");
  RunFunctionCalls(Shard(
      EnableJsonFeatureForTest(GetFunctionTestsNativeJsonExtractScalar())));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestNativeJsonQueryArray, 1) {
  SetNamePrefix("NativeJsonQueryArray");
  RunFunctionCalls(
      Shard(EnableJsonFeatureForTest(GetFunctionTestsNativeJsonQueryArray())));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestNativeJsonExtractArray, 1) {
  SetNamePrefix("NativeJsonExtractArray");
  RunFunctionCalls(Shard(
      EnableJsonFeatureForTest(GetFunctionTestsNativeJsonExtractArray())));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestNativeJsonValueArray, 1) {
  SetNamePrefix("NativeJsonValueArray");
  RunFunctionCalls(
      Shard(EnableJsonFeatureForTest(GetFunctionTestsNativeJsonValueArray())));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestNativeJsonExtractStringArray, 1) {
  SetNamePrefix("NativeJsonExtractStringArray");
  RunFunctionCalls(Shard(EnableJsonFeatureForTest(
      GetFunctionTestsNativeJsonExtractStringArray())));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestJsonQueryLax, 1) {
  SetNamePrefix("JsonQueryLax");
  std::vector<FunctionTestCall> tests = GetFunctionTestsJsonQueryLax();
  for (auto& test_case : tests) {
    test_case.params.AddRequiredFeatures(
        {FEATURE_JSON_TYPE, FEATURE_JSON_QUERY_LAX});
  }
  RunFunctionCalls(Shard(tests));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestToJsonString, 3) {
  SetNamePrefix("ToJsonString");
  RunFunctionCalls(Shard(GetFunctionTestsToJsonString()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestToJson, 3) {
  SetNamePrefix("ToJson");
  auto to_json_fct = [](const FunctionTestCall& f) {
    if (f.params.params().size() == 1) {
      return absl::Substitute("$0(@p0)", f.function_name);
    }
    return absl::Substitute("$0(@p0, stringify_wide_numbers=>@p1)",
                            f.function_name);
  };
  RunFunctionTestsCustom(
      Shard(EnableJsonFeatureForTest(GetFunctionTestsToJson())), to_json_fct);
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestSafeToJson, 3) {
  SetNamePrefix("SafeToJson");
  auto safe_to_json_fct = [](const FunctionTestCall& f) {
    return absl::Substitute("$0(@p0)", f.function_name);
  };
  RunFunctionTestsCustom(
      Shard(EnableJsonFeatureForTest(GetFunctionTestsSafeToJson())),
      safe_to_json_fct);
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestParseJson, 1) {
  SetNamePrefix("ParseJson");
  auto parse_json_fn_expr = [](const FunctionTestCall& f) {
    if (f.params.params().size() == 1) {
      return absl::Substitute("$0(@p0)", f.function_name);
    }
    return absl::Substitute("$0(@p0, wide_number_mode=>@p1)", f.function_name);
  };
  RunFunctionTestsCustom(
      Shard(EnableJsonFeatureForTest(GetFunctionTestsParseJson())),
      parse_json_fn_expr);
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestConvertJson, 1) {
  SetNamePrefix("ConvertJson");
  auto convert_json_fn_expr = [](const FunctionTestCall& f) {
    if (f.params.params().size() == 1) {
      return absl::Substitute("$0(@p0)", f.function_name);
    }
    return absl::Substitute("$0(@p0, wide_number_mode=>@p1)", f.function_name);
  };
  RunFunctionTestsCustom(Shard(EnableJsonValueExtractionFunctionsForTest(
                             GetFunctionTestsConvertJson())),
                         convert_json_fn_expr);
  RunFunctionTestsCustom(Shard(EnableJsonValueExtractionFunctionsForTest(
                             GetFunctionTestsConvertJsonIncompatibleTypes())),
                         convert_json_fn_expr);
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestJsonStripNulls, 1) {
  SetNamePrefix("JsonStripNulls");

  RunFunctionCallsWithNamedValueArguments(Shard(
      EnableJsonMutatorFunctionsForTest(GetFunctionTestsJsonStripNulls())));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestConvertJsonLaxBool, 1) {
  SetNamePrefix("ConvertJsonLaxBool");
  RunFunctionCalls(Shard(EnableJsonLaxValueExtractionFunctionsForTest(
      GetFunctionTestsConvertJsonLaxBool())));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestConvertJsonLaxInt64, 1) {
  SetNamePrefix("ConvertJsonLaxInt64");
  RunFunctionCalls(Shard(EnableJsonLaxValueExtractionFunctionsForTest(
      GetFunctionTestsConvertJsonLaxInt64())));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestConvertJsonLaxFloat64, 1) {
  SetNamePrefix("ConvertJsonLaxFloat64");
  RunFunctionCalls(Shard(EnableJsonLaxValueExtractionFunctionsForTest(
      GetFunctionTestsConvertJsonLaxFloat64())));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestConvertJsonLaxDouble, 1) {
  SetNamePrefix("ConvertJsonLaxDouble");
  RunFunctionCalls(Shard(EnableJsonLaxValueExtractionFunctionsForTest(
      GetFunctionTestsConvertJsonLaxDouble())));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestConvertJsonLaxString, 1) {
  SetNamePrefix("ConvertJsonLaxString");
  RunFunctionCalls(Shard(EnableJsonLaxValueExtractionFunctionsForTest(
      GetFunctionTestsConvertJsonLaxString())));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestJsonArray, 2) {
  SetNamePrefix("JsonArray");
  RunFunctionCalls(Shard(
      EnableJsonConstructorFunctionsForTest(GetFunctionTestsJsonArray())));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestJsonObject, 5) {
  SetNamePrefix("JsonObject");
  std::vector<FunctionTestCall> tests = GetFunctionTestsJsonObject();
  std::vector<FunctionTestCall> tests2 = GetFunctionTestsJsonObjectArrays();
  tests.insert(tests.end(), std::make_move_iterator(tests2.begin()),
               std::make_move_iterator(tests2.end()));

  RunFunctionCalls(Shard(EnableJsonConstructorFunctionsForTest(tests)));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestJsonRemove, 1) {
  SetNamePrefix("JsonRemove");
  RunFunctionCalls(
      Shard(EnableJsonMutatorFunctionsForTest(GetFunctionTestsJsonRemove())));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestJsonSet, 1) {
  SetNamePrefix("JsonSet");

  RunFunctionCallsWithNamedValueArguments(
      Shard(EnableJsonMutatorFunctionsForTest(GetFunctionTestsJsonSet())));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestJsonArrayInsert, 1) {
  SetNamePrefix("JsonArrayInsert");

  RunFunctionCallsWithNamedValueArguments(Shard(
      EnableJsonMutatorFunctionsForTest(GetFunctionTestsJsonArrayInsert())));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestJsonArrayAppend, 1) {
  SetNamePrefix("JsonArrayAppend");

  RunFunctionCallsWithNamedValueArguments(Shard(
      EnableJsonMutatorFunctionsForTest(GetFunctionTestsJsonArrayAppend())));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestJsonKeys, 1) {
  SetNamePrefix("JsonKeys");

  RunFunctionCallsWithNamedValueArguments(Shard(GetFunctionTestsJsonKeys()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestJsonFlatten, 1) {
  SetNamePrefix("JsonFlatten");
  RunFunctionCalls(Shard(GetFunctionTestsJsonFlatten()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestHash, 1) {
  SetNamePrefix("Hash");
  RunFunctionCalls(Shard(GetFunctionTestsHash()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestFarmFingerprint, 1) {
  SetNamePrefix("FarmFingerprint");
  RunFunctionCalls(Shard(GetFunctionTestsFarmFingerprint()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestErrorFunction, 1) {
  SetNamePrefix("ErrorFunction");
  RunFunctionCalls(Shard(GetFunctionTestsError()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestRegexpFunctions, 1) {
  SetNamePrefix("Regex");
  RunFunctionCalls(Shard(GetFunctionTestsRegexp()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestRegexp2Functions, 1) {
  auto label = MakeScopedLabel("regexp2_test:func_regexp_extract");
  RunFunctionCalls(
      Shard(GetFunctionTestsRegexp2(/*=include_feature_set=*/true)));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestRegexpExtractGroupsFunctions, 2) {
  auto label = MakeScopedLabel("RegexpExtractGroups");
  auto regexp_extract_groups_fct = [](const FunctionTestCall& f) {
    ABSL_CHECK_EQ(f.params.num_params(), 2);
    // The regex pattern (second argument) must be a literal.
    std::string regex_literal =
        f.params.param(1).is_null() ? "NULL" : MakeLiteral(f.params.param(1));
    return absl::Substitute("regexp_extract_groups(@p0, $0)", regex_literal);
  };
  std::vector<FunctionTestCall> tests = GetFunctionTestsRegexpExtractGroups();
  std::vector<FunctionTestCall> auto_casting_tests =
      GetFunctionTestsRegexpExtractGroupsWithAutoCasting(
          /*autocast_enabled=*/true);
  tests.insert(tests.end(), auto_casting_tests.begin(),
               auto_casting_tests.end());
  RunFunctionTestsCustom(Shard(tests), regexp_extract_groups_fct);
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestRegexpInstrFunctions, 5) {
  SetNamePrefix("RegexpInstr");
  RunFunctionCalls(Shard(GetFunctionTestsRegexpInstr()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestLike, 5) {
  SetNamePrefix("Like");
  auto query_params_with_results = GetFunctionTestsLike();
  // The LIKE pattern is not a constant, the regexp is constructed and
  // compiled at evaluation time.
  RunStatementTests(Shard(query_params_with_results), "@p0 LIKE @p1");

  // Generate queries where the LIKE pattern is a constant, the regexp is
  // constructed and compiled only once.
  SetNamePrefix("Like_with_constant_pattern");
  RunStatementTestsCustom(
      Shard(query_params_with_results), [](const QueryParamsWithResult& p) {
        std::string p0 = p.param(0).is_null() ? "@p0" : MakeLiteral(p.param(0));
        std::string p1 = p.param(1).is_null() ? "@p1" : MakeLiteral(p.param(1));
        return absl::Substitute("$0 LIKE $1", p0, p1);
      });
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestLikeWithCollation, 12) {
  auto query_params_with_results_with_collation =
      WrapFeatureCollation(GetFunctionTestsLikeWithCollation());
  SetNamePrefix("LikeWithCollationTextPatternUndCi");
  RunStatementTests(Shard(query_params_with_results_with_collation),
                    "COLLATE(@p0, 'und:ci') LIKE COLLATE(@p1, 'und:ci')");
  SetNamePrefix("LikeWithCollationTextUndCi");
  RunStatementTests(Shard(query_params_with_results_with_collation),
                    "COLLATE(@p0, 'und:ci') LIKE @p1");
  SetNamePrefix("LikeWithCollationPatternUndCi");
  RunStatementTests(Shard(query_params_with_results_with_collation),
                    "@p0 LIKE COLLATE(@p1, 'und:ci')");
  auto query_params_with_results =
      WrapFeatureCollation(GetFunctionTestsLikeString());
  SetNamePrefix("LikeWithCollationTextPatternBinary");
  RunStatementTests(Shard(query_params_with_results),
                    "COLLATE(@p0, 'binary') LIKE COLLATE(@p1, 'binary')");
  SetNamePrefix("LikeWithCollationTextBinary");
  RunStatementTests(Shard(query_params_with_results),
                    "COLLATE(@p0, 'binary') LIKE @p1");
  SetNamePrefix("LikeWithCollationPatternBinary");
  RunStatementTests(Shard(query_params_with_results),
                    "@p0 LIKE COLLATE(@p1, 'binary')");
  SetNamePrefix("LikeWithCollationTextPatternEmpty");
  RunStatementTests(Shard(query_params_with_results),
                    "COLLATE(@p0, '') LIKE COLLATE(@p1, '')");
  SetNamePrefix("LikeWithCollationTextEmpty");
  RunStatementTests(Shard(query_params_with_results),
                    "COLLATE(@p0, '') LIKE @p1");
  SetNamePrefix("LikeWithCollationPatternEmpty");
  RunStatementTests(Shard(query_params_with_results),
                    "@p0 LIKE COLLATE(@p1, '')");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestNotLike, 2) {
  SetNamePrefix("NotLike");
  RunStatementTests(InvertResults(Shard(GetFunctionTestsLike())),
                    "@p0 NOT LIKE @p1");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestNotLikeWithCollation, 6) {
  auto query_params_with_results_with_collation =
      WrapFeatureCollation(InvertResults(GetFunctionTestsLikeWithCollation()));
  SetNamePrefix("NotLikeWithCollationTextPatternUndCi");
  RunStatementTests(Shard(query_params_with_results_with_collation),
                    "COLLATE(@p0, 'und:ci') NOT LIKE COLLATE(@p1, 'und:ci')");
  SetNamePrefix("NotLikeWithCollationTextUndCi");
  RunStatementTests(Shard(query_params_with_results_with_collation),
                    "COLLATE(@p0, 'und:ci') NOT LIKE @p1");
  SetNamePrefix("NotLikeWithCollationPatternUndCi");
  RunStatementTests(Shard(query_params_with_results_with_collation),
                    "@p0 NOT LIKE COLLATE(@p1, 'und:ci')");
  auto query_params_with_results =
      WrapFeatureCollation(InvertResults(GetFunctionTestsLikeString()));
  SetNamePrefix("NotLikeWithCollationTextPatternBinary");
  RunStatementTests(Shard(query_params_with_results),
                    "COLLATE(@p0, 'binary') NOT LIKE COLLATE(@p1, 'binary')");
  SetNamePrefix("NotLikeWithCollationTextBinary");
  RunStatementTests(Shard(query_params_with_results),
                    "COLLATE(@p0, 'binary') NOT LIKE @p1");
  SetNamePrefix("NotLikeWithCollationPatternBinary");
  RunStatementTests(Shard(query_params_with_results),
                    "@p0 NOT LIKE COLLATE(@p1, 'binary')");
  SetNamePrefix("NotLikeWithCollationTextPatternEmpty");
  RunStatementTests(Shard(query_params_with_results),
                    "COLLATE(@p0, '') NOT LIKE COLLATE(@p1, '')");
  SetNamePrefix("NotLikeWithCollationTextEmpty");
  RunStatementTests(Shard(query_params_with_results),
                    "COLLATE(@p0, '') NOT LIKE @p1");
  SetNamePrefix("NotLikeWithCollationPatternEmpty");
  RunStatementTests(Shard(query_params_with_results),
                    "@p0 NOT LIKE COLLATE(@p1, '')");
}

TEST_F(ComplianceCodebasedTests, TestSimpleTable) {
  if (!DriverCanRunTests()) {
    return;
  }
  Value mytable = StructArray({"int64_val", "str_val"},
                              {{1ll, "foo"}, {2ll, "bar"}}, kIgnoresOrder);

  SetNamePrefix("Table");
  EXPECT_THAT(RunSQL("SELECT int64_val, str_val FROM MyTable"),
              Returns(mytable));

  // Scanned rows can be returned in any order.
  SetNamePrefix("PermutedTable");
  Value permuted_mytable = StructArray(
      {"int64_val", "str_val"}, {{2ll, "bar"}, {1ll, "foo"}}, kIgnoresOrder);
  EXPECT_THAT(RunSQL("SELECT int64_val, str_val FROM MyTable"),
              Returns(permuted_mytable));
}

TEST_F(ComplianceCodebasedTests, TestAggregation) {
  if (!DriverCanRunTests()) {
    return;
  }
  auto label = MakeScopedLabel("code:TestAggregation");

  // ANY_VALUE
  // Read a table of all NULL values.
  Value any_result_nulls =
      StructArray({"bool_any", "double_any", "int64_any", "str_val"},
                  {
                      {NullBool(), NullDouble(), NullInt64(), NullString()},
                  });
  SetNamePrefix("TableAllNull");
  EXPECT_THAT(RunSQL("SELECT ANY_VALUE(bool_val) bool_any, "
                     "       ANY_VALUE(double_val) double_any, "
                     "       ANY_VALUE(int64_val) int64_any, "
                     "       ANY_VALUE(str_val) str_val "
                     "FROM TableAllNull"),
              Returns(any_result_nulls));
  // There are too many possible results to comprehensively check the result.
  SetNamePrefix("TableLarge");
  EXPECT_THAT(
      RunSQL(
          "SELECT ANY_VALUE(bool_val) bool_any, "
          "       ANY_VALUE(double_val) double_any, "
          "       ANY_VALUE(int64_val) int64_any, ANY_VALUE(str_val) str_val "
          "FROM TableLarge"),
      ReturnsSuccess());
  // ANY_VALUE queries with tractable non-deterministic results.
  SetNamePrefix("TableLargeBool");
  EXPECT_THAT(
      RunSQL("SELECT ANY_VALUE(bool_val) FROM TableLarge"),
      Returns(::testing::AnyOf(
          EqualsValue(StructArray({kAnonymousColumnName}, {{NullBool()}})),
          EqualsValue(StructArray({kAnonymousColumnName}, {{Bool(true)}})),
          EqualsValue(StructArray({kAnonymousColumnName}, {{Bool(false)}})))));
  SetNamePrefix("TableLargeDouble");
  EXPECT_THAT(
      RunSQL("SELECT ANY_VALUE(double_val) FROM TableLarge"),
      Returns(::testing::AnyOf(
          EqualsValue(StructArray({kAnonymousColumnName}, {{NullDouble()}})),
          EqualsValue(StructArray({kAnonymousColumnName}, {{Double(0.2)}})),
          EqualsValue(StructArray({kAnonymousColumnName}, {{Double(0.3)}})),
          EqualsValue(StructArray({kAnonymousColumnName}, {{Double(0.4)}})),
          EqualsValue(StructArray({kAnonymousColumnName}, {{Double(0.5)}})),
          EqualsValue(StructArray({kAnonymousColumnName}, {{Double(0.6)}})),
          EqualsValue(StructArray({kAnonymousColumnName}, {{Double(0.7)}})),
          EqualsValue(StructArray({kAnonymousColumnName}, {{Double(0.8)}})),
          EqualsValue(StructArray({kAnonymousColumnName}, {{Double(0.9)}})),
          EqualsValue(StructArray({kAnonymousColumnName}, {{Double(1.0)}})),
          EqualsValue(StructArray({kAnonymousColumnName}, {{Double(0.2)}})),
          EqualsValue(StructArray({kAnonymousColumnName}, {{Double(0.2)}})))));
  SetNamePrefix("TableLargeInt64");
  EXPECT_THAT(
      RunSQL("SELECT ANY_VALUE(int64_val) FROM TableLarge"),
      Returns(::testing::AnyOf(
          ReturnsNoValue("Known Error"),
          EqualsValue(StructArray({kAnonymousColumnName}, {{NullInt64()}})),
          EqualsValue(StructArray({kAnonymousColumnName}, {{Int64(3)}})),
          EqualsValue(StructArray({kAnonymousColumnName}, {{Int64(4)}})),
          EqualsValue(StructArray({kAnonymousColumnName}, {{Int64(5)}})),
          EqualsValue(StructArray({kAnonymousColumnName}, {{Int64(6)}})),
          EqualsValue(StructArray({kAnonymousColumnName}, {{Int64(7)}})),
          EqualsValue(StructArray({kAnonymousColumnName}, {{Int64(8)}})),
          EqualsValue(StructArray({kAnonymousColumnName}, {{Int64(9)}})),
          EqualsValue(StructArray({kAnonymousColumnName}, {{Int64(10)}})))));
  SetNamePrefix("TableLargeString");
  EXPECT_THAT(
      RunSQL("SELECT ANY_VALUE(str_val) FROM TableLarge"),
      Returns(::testing::AnyOf(
          ReturnsNoValue("Known Error"),
          EqualsValue(StructArray({kAnonymousColumnName}, {{NullString()}})),
          EqualsValue(StructArray({kAnonymousColumnName}, {{String("4")}})),
          EqualsValue(StructArray({kAnonymousColumnName}, {{String("5")}})),
          EqualsValue(StructArray({kAnonymousColumnName}, {{String("6")}})),
          EqualsValue(StructArray({kAnonymousColumnName}, {{String("7")}})),
          EqualsValue(StructArray({kAnonymousColumnName}, {{String("8")}})),
          EqualsValue(StructArray({kAnonymousColumnName}, {{String("9")}})),
          EqualsValue(StructArray({kAnonymousColumnName}, {{String("10")}})))));
}  // namespace googlesql

SHARDED_TEST_F(ComplianceCodebasedTests, TestConditionals_If, 1) {
  SetNamePrefix("If");
  RunFunctionTestsPrefix(Shard(GetFunctionTestsIf()), "If");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestConditionals_IfNull, 1) {
  SetNamePrefix("IfNull");
  RunFunctionTestsPrefix(Shard(GetFunctionTestsIfNull()), "IfNull");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestConditionals_NullIf, 1) {
  SetNamePrefix("NullIf");
  RunFunctionTestsPrefix(Shard(GetFunctionTestsNullIf()), "NullIf");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestConditionals_ZeroIfNull, 1) {
  SetNamePrefix("ZeroIfNull");
  RunFunctionTestsPrefix(Shard(GetFunctionTestsZeroIfNull_NullIfZero(
                             /*is_zero_if_null=*/true, /*is_safe=*/false)),
                         "ZEROIFNULL");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestConditionals_NullIfZero, 1) {
  SetNamePrefix("NullIfZero");
  RunFunctionTestsPrefix(Shard(GetFunctionTestsZeroIfNull_NullIfZero(
                             /*is_zero_if_null=*/false, /*is_safe=*/false)),
                         "NULLIFZERO");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestConditionals_SafeZeroIfNull, 1) {
  SetNamePrefix("SafeZeroIfNull");
  RunFunctionTestsPrefix(Shard(GetFunctionTestsZeroIfNull_NullIfZero(
                             /*is_zero_if_null=*/true, /*is_safe=*/true)),
                         "SAFE.ZEROIFNULL");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestConditionals_SafeNullIfZero, 1) {
  SetNamePrefix("SafeNullIfZero");
  RunFunctionTestsPrefix(Shard(GetFunctionTestsZeroIfNull_NullIfZero(
                             /*is_zero_if_null=*/false, /*is_safe=*/true)),
                         "SAFE.NULLIFZERO");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestConditionals_Coalesce, 1) {
  SetNamePrefix("Coalesce");
  RunFunctionTestsPrefix(Shard(GetFunctionTestsCoalesce()), "Coalesce");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestBitwiseFunctions_bNOT, 1) {
  SetNamePrefix("bNot");
  RunStatementTests(Shard(GetFunctionTestsBitwiseNot()), "~@p0");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestBitwiseFunctions_bOR, 1) {
  SetNamePrefix("bOr");
  RunStatementTests(Shard(GetFunctionTestsBitwiseOr()), "@p0 | @p1");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestBitwiseFunctions_bXOR, 1) {
  SetNamePrefix("bXor");
  RunStatementTests(Shard(GetFunctionTestsBitwiseXor()), "@p0 ^ @p1");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestBitwiseFunctions_bAND, 1) {
  SetNamePrefix("bAnd");
  RunStatementTests(Shard(GetFunctionTestsBitwiseAnd()), "@p0 & @p1");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestBitwiseFunctions_bLEFTSHIFT, 1) {
  SetNamePrefix("LeftShift");
  RunStatementTests(Shard(GetFunctionTestsBitwiseLeftShift()), "@p0 << @p1");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestBitwiseFunctions_bRIGHTSHIFT, 1) {
  SetNamePrefix("RightShift");
  RunStatementTests(Shard(GetFunctionTestsBitwiseRightShift()), "@p0 >> @p1");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestBitCount, 1) {
  SetNamePrefix("BitCount");
  RunStatementTests(Shard(GetFunctionTestsBitCount()), "BIT_COUNT(@p0)");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestArrayAtOffset, 1) {
  SetNamePrefix("ArrayAtOffset");
  RunStatementTests(Shard(GetFunctionTestsAtOffset()), "@p0[OFFSET(@p1)]");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestArrayAtOrdinal, 1) {
  SetNamePrefix("ArrayAtOrdinal");
  RunStatementTests(Shard(GetFunctionTestsAtOffset()), "@p0[ORDINAL(@p1 + 1)]");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestSafeArrayAtOffset, 1) {
  SetNamePrefix("SafeArrayAtOffset");
  RunStatementTests(Shard(GetFunctionTestsSafeAtOffset()),
                    "@p0[SAFE_OFFSET(@p1)]");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestSafeArrayAtOrdinal, 1) {
  SetNamePrefix("SafeArrayAtOrdinal");
  RunStatementTests(Shard(GetFunctionTestsSafeAtOffset()),
                    "@p0[SAFE_ORDINAL(@p1 + 1)]");
}

TEST_F(ComplianceCodebasedTests, TestProto) {
  if (!DriverCanRunTests()) {
    return;
  }
  googlesql_test::KitchenSinkPB p;
  // Roundtrip proto parameter.
  // TODO: can we expect the same byte serialization in general?
  p.Clear();
  // Set all required fields.
  p.set_int64_key_1(3);
  p.set_int64_key_2(0);
  SetNamePrefix("Proto");
  RunStatementOnFeatures("SELECT @p0 AS ColA",
                         {/*arguments=*/{MakeProtoValue(&p)},
                          /*result=*/Singleton(MakeProtoValue(&p))});
  p.Clear();
  // Set all required fields.
  p.set_int64_key_1(3);
  p.set_int64_key_2(0);
  // Missing int32 with default 77.
  SetNamePrefix("ProtoInt32");
  RunStatementOnFeatures("SELECT @p0.int32_val a, @p0.has_int32_val b",
                         {/*arguments=*/{MakeProtoValue(&p)},
                          /*result=*/StructArray({"a", "b"}, {{77, False()}})});
  p.Clear();
  // Set all required fields.
  p.set_int64_key_1(3);
  p.set_int64_key_2(0);
  // int32 set to 123.
  p.set_int32_val(123);
  SetNamePrefix("ProtoInt32");
  RunStatementOnFeatures("SELECT @p0.int32_val a, @p0.has_int32_val b",
                         {/*arguments=*/{MakeProtoValue(&p)},
                          /*result=*/StructArray({"a", "b"}, {{123, True()}})});
  p.Clear();
  // Set all required fields.
  p.set_int64_key_1(3);
  p.set_int64_key_2(0);
  SetNamePrefix("ProtoInt64");
  RunStatementOnFeatures(
      "SELECT @p0.int64_key_1 a, @p0.has_int64_key_1 b",
      {/*arguments=*/{MakeProtoValue(&p)},
       /*result=*/StructArray({"a", "b"}, {{Int64(3), True()}})});
  p.Clear();
  // Set all required fields.
  p.set_int64_key_1(3);
  p.set_int64_key_2(0);
  // Nested repeated value. nested_int64 has default 88.
  googlesql_test::KitchenSinkPB::Nested* n = p.mutable_nested_value();
  n->add_nested_repeated_int64(300);
  n->add_nested_repeated_int64(301);
  SetNamePrefix("ProtoNested");
  RunStatementOnFeatures(
      "SELECT nv.nested_int64 a, b FROM "
      "(SELECT @p0.nested_value nv) t, t.nv.nested_repeated_int64 b",
      {/*arguments=*/{MakeProtoValue(&p)},
       /*result=*/StructArray(
           {"a", "b"}, {{Int64(88), Int64(301)}, {Int64(88), Int64(300)}},
           kIgnoresOrder)});
  // TODO: port all tests from evaluation_test.cc
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestMathFunctions_Math, 4) {
  // No need to set PREFIX, RunFunctionCalls() will do it.
  RunFunctionCalls(Shard(GetFunctionTestsMath()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestMathFunctions_Rounding, 1) {
  // No need to set PREFIX, RunFunctionCalls() will do it.
  RunFunctionCalls(Shard(GetFunctionTestsRounding()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestMathFunctions_Trigonometric, 1) {
  // No need to set PREFIX, RunFunctionCalls() will do it.
  RunFunctionCalls(Shard(GetFunctionTestsTrigonometric()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestMathFunctions_InverseTrigonometric,
               1) {
  RunFunctionCalls(Shard(GetFunctionTestsInverseTrigonometric()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestMathFunctions_Pi, 1) {
  // No need to set PREFIX, RunFunctionCalls() will do it.
  RunFunctionCalls(Shard(GetFunctionTestsPi(/*include_safe_calls=*/true)));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestMathFunctions_Cbrt, 1) {
  // No need to set PREFIX, RunFunctionCalls() will do it.
  RunFunctionCalls(Shard(GetFunctionTestsCbrt()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestNetFunctions, 3) {
  // No need to set PREFIX, RunFunctionCalls() will do it.
  RunFunctionCalls(Shard(GetFunctionTestsNet()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestDateTimeFunctionsDateDiffFormat,
               1) {
  // Note that date part arguments (DAY, WEEK, etc.) are required to
  // be date part names (not quoted literals) or NULL in the SQL syntax.
  // Parameters are not allowed.  So several functions in this file that
  // generate function calls with date part arguments must handle them
  // specially.
  auto date_diff_format_fct = [](const FunctionTestCall& f) {
    ABSL_CHECK_EQ(3, f.params.num_params());
    return absl::Substitute("$0(@p0, @p1, $1)", f.function_name,
                            f.params.param(2).string_value());
  };
  RunFunctionTestsCustom(
      Shard(ConvertDateTimePartBindingsToString(
          GetFunctionTestsDateAndTimestampDiff(), /*date_part_param_idx=*/2)),
      date_diff_format_fct);
}

// Even with 500 query shards, this test takes a long time relative to the
// others. The number of shards for this case is picked so that each shard has
// ~300 queries.
SHARDED_TEST_F(ComplianceCodebasedTests, TestDateTimeFunctionsExtractFormat,
               140) {
  auto extract_format_fct = [](const FunctionTestCall& f) {
    if (f.params.num_params() != 2 && f.params.num_params() != 3) {
      ABSL_LOG(FATAL) << "Unexpected number of parameters: "
                 << f.params.num_params();
    } else {
      const std::string& date_part_sql = f.params.param(1).string_value();
      if (f.params.num_params() == 2) {
        return absl::Substitute("$0($1 FROM @p0)", f.function_name,
                                date_part_sql);
      } else {
        ABSL_CHECK_EQ(f.params.num_params(), 3);
        return absl::Substitute("$0($1 FROM @p0 AT TIME ZONE @p2)",
                                f.function_name, date_part_sql);
      }
    }
  };

  RunFunctionTestsCustom(
      Shard(ConvertDateTimePartBindingsToString(GetFunctionTestsExtractFrom(),
                                                /*date_part_param_idx=*/1)),
      extract_format_fct);
}

// Even with 500 query shards, this test takes a long time relative to the
// others. The number of shards for this case is picked so that each shard has
// ~300 queries.
SHARDED_TEST_F(ComplianceCodebasedTests, TestDateTimeFunctions_Standard, 60) {
  RunFunctionCalls(Shard(GetFunctionTestsDateTimeStandardFunctionCalls()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestDateTimeFunctions_Conversion, 1) {
  // Always call Shard() inside a SHARDED_TEST_F().
  RunFunctionCalls(Shard(GetFunctionTestsTimestampConversion()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestTimestampFromDate, 1) {
  RunFunctionCalls(Shard(GetFunctionTestsTimestampFromDate()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestDateAddSubFunctions, 1) {
  auto date_diff_format_fct = [](const FunctionTestCall& f) {
    ABSL_CHECK_EQ(3, f.params.num_params());
    return absl::Substitute("$0(@p0, INTERVAL @p1 $1)", f.function_name,
                            f.params.param(2).string_value());
  };
  RunFunctionTestsCustom(
      Shard(ConvertDateTimePartBindingsToString(GetFunctionTestsDateAddSub(),
                                                /*date_part_param_idx=*/2)),
      date_diff_format_fct);
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestDateMathOperators, 1) {
  const auto& add_tests =
      GetFunctionTestsDateArithmetics(GetFunctionTestsDateAdd());
  SetNamePrefix("TestDateMathOperators_Add");
  RunStatementTests(Shard(add_tests), "@p0 + @p1");
  SetNamePrefix("TestDateMathOperators_Add_Reverse");
  RunStatementTests(Shard(add_tests), "@p1 + @p0");
  SetNamePrefix("TestDateMathOperators_Sub");
  RunStatementTests(
      Shard(GetFunctionTestsDateArithmetics(GetFunctionTestsDateSub())),
      "@p0 - @p1");
}
SHARDED_TEST_F(ComplianceCodebasedTests, TestDateAliasFunctions, 1) {
  const auto& add_tests =
      GetFunctionTestsDateArithmetics(GetFunctionTestsAddDate());
  SetNamePrefix("TestDateMathOperators_Add");
  RunStatementTests(Shard(add_tests), "@p0 + @p1");
  SetNamePrefix("TestDateMathOperators_Add_Reverse");
  RunStatementTests(Shard(add_tests), "@p1 + @p0");
  SetNamePrefix("TestDateMathOperators_Sub");
  RunStatementTests(
      Shard(GetFunctionTestsDateArithmetics(GetFunctionTestsSubDate())),
      "@p0 - @p1");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestFromProto3Conversions, 1) {
  RunFunctionCalls(Shard(GetFunctionTestsFromProto()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestFromProto3TimeOfDay, 1) {
  RunStatementTests(Shard(GetFunctionTestsFromProto3TimeOfDay()),
                    "from_proto(@p0)");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestToProto3Conversions, 1) {
  RunFunctionCalls(Shard(GetFunctionTestsToProto()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestToProto3TimeOfDay, 1) {
  RunStatementTests(Shard(GetFunctionTestsToProto3TimeOfDay()),
                    "to_proto(@p0)");
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestDatetimeAddSubFunctions, 2) {
  auto datetime_add_sub = [](const FunctionTestCall& f) {
    ABSL_CHECK_EQ(3, f.params.num_params());
    return absl::Substitute("$0(@p0, INTERVAL @p1 $1)", f.function_name,
                            f.params.param(2).string_value());
  };
  RunFunctionTestsCustom(
      Shard(ConvertDateTimePartBindingsToString(
          GetFunctionTestsDatetimeAddSub(), /*date_part_param_idx=*/2)),
      datetime_add_sub);
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestDatetimeDiffFunctions, 5) {
  auto datetime_diff = [](const FunctionTestCall& f) {
    ABSL_CHECK_EQ(3, f.params.num_params());
    return absl::Substitute("$0(@p0, @p1, $1)", f.function_name,
                            f.params.param(2).string_value());
  };
  RunFunctionTestsCustom(
      Shard(ConvertDateTimePartBindingsToString(GetFunctionTestsDatetimeDiff(),
                                                /*date_part_param_idx=*/2)),
      datetime_diff);
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestLastDayFunctions, 1) {
  auto last_day = [](const FunctionTestCall& f) {
    ABSL_CHECK_EQ("last_day", f.function_name);
    return absl::Substitute(
        "$0(@p0, $1)", f.function_name,
        functions::DateTimestampPartToSQL(f.params.param(1).enum_value()));
  };
  RunFunctionTestsCustom(Shard(WrapFeatureLastDay(GetFunctionTestsLastDay())),
                         last_day);
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestNextDayFunctions, 1) {
  RunFunctionCalls(Shard(GetFunctionTestsNextDay()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestAddMonthsFunctions, 1) {
  RunFunctionCalls(Shard(GetFunctionTestsAddMonths()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestMonthsBetweenFunctions, 1) {
  RunFunctionCalls(Shard(GetFunctionTestsMonthsBetween()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestDateTruncFunctions, 1) {
  auto date_trunc = [](const FunctionTestCall& f) {
    ABSL_CHECK_EQ("date_trunc", f.function_name);
    ABSL_CHECK_EQ(2, f.params.num_params());
    // We can't have a null DatePart.
    ABSL_CHECK(!f.params.param(1).is_null());
    return absl::Substitute("date_trunc(@p0, $0)",
                            f.params.param(1).string_value());
  };
  RunFunctionTestsCustom(
      Shard(ConvertDateTimePartBindingsToString(GetFunctionTestsDateTrunc(),
                                                /*date_part_param_idx=*/1)),
      date_trunc);
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestDatetimeTruncFunctions, 1) {
  auto datetime_trunc = [](const FunctionTestCall& f) {
    ABSL_CHECK_EQ(2, f.params.num_params());
    return absl::Substitute("$0(@p0, $1)", f.function_name,
                            f.params.param(1).string_value());
  };
  RunFunctionTestsCustom(
      Shard(ConvertDateTimePartBindingsToString(GetFunctionTestsDatetimeTrunc(),
                                                /*date_part_param_idx=*/1)),
      datetime_trunc);
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestTimeAddSubFunctions, 2) {
  auto time_add_sub = [](const FunctionTestCall& f) {
    ABSL_CHECK_EQ(3, f.params.num_params());
    return absl::Substitute("$0(@p0, INTERVAL @p1 $1)", f.function_name,
                            f.params.param(2).string_value());
  };
  RunFunctionTestsCustom(
      Shard(ConvertDateTimePartBindingsToString(GetFunctionTestsTimeAddSub(),
                                                /*date_part_param_idx=*/2)),
      time_add_sub);
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestTimeDiffFunctions, 1) {
  auto time_diff = [](const FunctionTestCall& f) {
    ABSL_CHECK_EQ(3, f.params.num_params());
    return absl::Substitute("$0(@p0, @p1, $1)", f.function_name,
                            f.params.param(2).string_value());
  };
  RunFunctionTestsCustom(
      Shard(ConvertDateTimePartBindingsToString(GetFunctionTestsTimeDiff(),
                                                /*date_part_param_idx=*/2)),
      time_diff);
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestTimeTruncFunctions, 1) {
  auto time_trunc = [](const FunctionTestCall& f) {
    ABSL_CHECK_EQ(2, f.params.num_params());
    return absl::Substitute("$0(@p0, $1)", f.function_name,
                            f.params.param(1).string_value());
  };
  RunFunctionTestsCustom(
      Shard(ConvertDateTimePartBindingsToString(GetFunctionTestsTimeTrunc(),
                                                /*date_part_param_idx=*/1)),
      time_trunc);
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestTimestampAddSubFunctions, 1) {
  auto timestamp_diff_format_fct = [](const FunctionTestCall& f) {
    ABSL_CHECK_EQ(3, f.params.num_params());
    return absl::Substitute("$0(@p0, INTERVAL @p1 $1)", f.function_name,
                            f.params.param(2).string_value());
  };
  RunFunctionTestsCustom(
      Shard(ConvertDateTimePartBindingsToString(
          GetFunctionTestsTimestampAddSub(), /*date_part_param_idx=*/2)),
      timestamp_diff_format_fct);
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestTimestampTruncFunctions, 6) {
  auto timestamp_trunc_format_fct = [](const FunctionTestCall& f) {
    if (f.params.num_params() != 2 && f.params.num_params() != 3) {
      ABSL_LOG(FATAL) << "Unexpected number of parameters: "
                 << f.params.num_params();
    } else {
      absl::string_view date_part_sql = f.params.param(1).string_value();
      if (f.params.num_params() == 2) {
        return absl::Substitute("$0(@p0, $1)", f.function_name, date_part_sql);
      } else {
        ABSL_CHECK_EQ(f.params.num_params(), 3);
        return absl::Substitute("$0(@p0, $1, @p2)", f.function_name,
                                date_part_sql);
      }
    }
  };
  RunFunctionTestsCustom(
      Shard(ConvertDateTimePartBindingsToString(
          GetFunctionTestsTimestampTrunc(), /*date_part_param_idx=*/1)),
      timestamp_trunc_format_fct);
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestTimestampConversionFunctions, 1) {
  // Always call Shard() inside a SHARDED_TEST_F().
  RunFunctionCalls(Shard(GetFunctionTestsTimestampConversion()));
}

SHARDED_TEST_F(ComplianceCodebasedTests,
               TestCivilTimeConstructionFunctions_Date, 1) {
  RunFunctionCalls(Shard(GetFunctionTestsDateConstruction()));
}

// Date is considered a "civil time" type, and the reference implementation now
// supports it with civil time code, but it predates FEATURE_CIVIL_TIME.
SHARDED_TEST_F(ComplianceCodebasedTests,
               TestCivilTimeConstructionFunctions_DateFromTimestamp, 1) {
  RunFunctionCalls(Shard(GetFunctionTestsDateFromTimestamp()));
}

SHARDED_TEST_F(ComplianceCodebasedTests,
               TestCivilTimeConstructionFunctions_Time, 1) {
  RunFunctionCalls(Shard(GetFunctionTestsTimeConstruction()));
}

SHARDED_TEST_F(ComplianceCodebasedTests,
               TestCivilTimeConstructionFunctions_DateTime, 1) {
  RunFunctionCalls(Shard(GetFunctionTestsDatetimeConstruction()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestCivilTimeConversionFunctions_Date,
               1) {
  RunFunctionCalls(Shard(GetFunctionTestsConvertDatetimeToTimestamp()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestCivilTimeConversionFunctions_Time,
               1) {
  RunFunctionCalls(Shard(GetFunctionTestsConvertTimestampToTime()));
}

SHARDED_TEST_F(ComplianceCodebasedTests,
               TestCivilTimeConversionFunctions_DateTime, 1) {
  RunFunctionCalls(Shard(GetFunctionTestsConvertTimestampToDatetime()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestTimestampBucketFunctions, 1) {
  RunFunctionCalls(Shard(GetFunctionTestsTimestampBucket()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestDatetimeBucketFunctions, 1) {
  RunFunctionCalls(Shard(GetFunctionTestsDatetimeBucket()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestDateBucketFunctions, 1) {
  RunFunctionCalls(Shard(GetFunctionTestsDateBucket()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestOctetLengthFunctions, 1) {
  RunFunctionCalls(Shard(GetFunctionTestsOctetLength()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestAsciiFunctions, 1) {
  RunFunctionCalls(Shard(GetFunctionTestsAscii()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestUnicodeFunctions, 1) {
  RunFunctionCalls(Shard(GetFunctionTestsUnicode()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestChrFunctions, 1) {
  RunFunctionCalls(Shard(GetFunctionTestsChr()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestStringFunctions, 6) {
  // No need to set PREFIX, RunFunctionCalls() will do it.
  RunFunctionCalls(Shard(GetFunctionTestsString()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestStringSubstringFunctions, 1) {
  // No need to set PREFIX, RunFunctionCalls() will do it.
  RunFunctionCalls(
      Shard(WrapFeatureAdditionalStringFunctions(GetFunctionTestsSubstring())));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestStringLcaseFunctions, 1) {
  // No need to set PREFIX, RunFunctionCalls() will do it.
  RunFunctionCalls(Shard(
      WrapFeatureAliasesForStringAndDateFunctions(GetFunctionTestsLcase())));
}
SHARDED_TEST_F(ComplianceCodebasedTests, TestStringUcaseFunctions, 1) {
  // No need to set PREFIX, RunFunctionCalls() will do it.
  RunFunctionCalls(Shard(
      WrapFeatureAliasesForStringAndDateFunctions(GetFunctionTestsUcase())));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestStringSoundexFunctions, 1) {
  // No need to set PREFIX, RunFunctionCalls() will do it.
  RunFunctionCalls(
      Shard(WrapFeatureAdditionalStringFunctions(GetFunctionTestsSoundex())));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestStringTranslateFunctions, 1) {
  // No need to set PREFIX, RunFunctionCalls() will do it.
  RunFunctionCalls(
      Shard(WrapFeatureAdditionalStringFunctions(GetFunctionTestsTranslate())));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestStringInstrFunctions1, 1) {
  // No need to set PREFIX, RunFunctionCalls() will do it.
  RunFunctionCalls(
      Shard(WrapFeatureAdditionalStringFunctions(GetFunctionTestsInstr1())));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestStringInstrFunctions2, 1) {
  // No need to set PREFIX, RunFunctionCalls() will do it.
  RunFunctionCalls(
      Shard(WrapFeatureAdditionalStringFunctions(GetFunctionTestsInstr2())));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestStringInstrFunctions3, 1) {
  // No need to set PREFIX, RunFunctionCalls() will do it.
  RunFunctionCalls(
      Shard(WrapFeatureAdditionalStringFunctions(GetFunctionTestsInstr3())));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestStringInitCapFunctions, 1) {
  // No need to set PREFIX, RunFunctionCalls() will do it.
  RunFunctionCalls(
      Shard(WrapFeatureAdditionalStringFunctions(GetFunctionTestsInitCap())));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestStringSplitSubstrFunction, 1) {
  // TODO: Add compliance tests for split_substr with collation.
  RunFunctionCalls(Shard(GetFunctionTestsSplitSubstr(/*skip_collation=*/true)));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestStringParseNumericFunctions, 1) {
  RunFunctionCalls(Shard(GetFunctionTestsParseNumeric()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestArrayFunctions, 1) {
  RunFunctionCalls(Shard(GetFunctionTestsArray()));
}

// Six way sharding puts each shard at ~100 queries as of Q1'17.
SHARDED_TEST_F(ComplianceCodebasedTests, TestFormatFunction, 11) {
  SetNamePrefix("Format");
  RunFunctionCalls(Shard(GetFunctionTestsFormat()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestNormalizeFunctions, 1) {
  RunNormalizeFunctionCalls(Shard(GetFunctionTestsNormalize()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestBase32Functions, 1) {
  RunFunctionCalls(Shard(GetFunctionTestsBase32()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestBase64Functions, 1) {
  RunFunctionCalls(Shard(GetFunctionTestsBase64()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestHexFunctions, 1) {
  RunFunctionCalls(Shard(GetFunctionTestsHex()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestReverseFunctions, 1) {
  RunFunctionCalls(Shard(GetFunctionTestsReverse()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestCodePointsFunctions, 1) {
  RunFunctionCalls(Shard(GetFunctionTestsCodePoints()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, IntervalCtor, 1) {
  SetNamePrefix("IntervalConstructor");
  auto interval_ctor = [](const FunctionTestCall& f) {
    ABSL_CHECK(f.function_name.empty());
    return absl::Substitute("INTERVAL @p0 $0",
                            f.params.param(1).string_value());
  };
  RunFunctionTestsCustom(Shard(GetFunctionTestsIntervalConstructor()),
                         interval_ctor);
}

SHARDED_TEST_F(ComplianceCodebasedTests, IntervalComparisons, 1) {
  SetNamePrefix("IntervalComparisons");
  RunFunctionTestsCustom(Shard(GetFunctionTestsIntervalComparisons()),
                         ComparisonExpression);
}

SHARDED_TEST_F(ComplianceCodebasedTests, RangeComparisons, 1) {
  SetNamePrefix("RangeComparisons");
  RunFunctionTestsCustom(Shard(GetFunctionTestsRangeComparisons()),
                         ComparisonExpression);
}

SHARDED_TEST_F(ComplianceCodebasedTests, UuidComparisons, 1) {
  SetNamePrefix("UuidComparisons");
  RunFunctionTestsCustom(Shard(GetFunctionTestsUuidComparisons()),
                         ComparisonExpression);
}

SHARDED_TEST_F(ComplianceCodebasedTests, RangeOverlaps, 1) {
  SetNamePrefix("RangeOverlaps");
  RunFunctionTestsCustom(
      Shard(GetFunctionTestsRangeOverlaps()),
      [](const FunctionTestCall& f) { return "range_overlaps(@p0, @p1)"; });
}

SHARDED_TEST_F(ComplianceCodebasedTests, RangeIntersect, 1) {
  SetNamePrefix("RangeIntersect");
  RunFunctionTestsCustom(
      Shard(GetFunctionTestsRangeIntersect()),
      [](const FunctionTestCall& f) { return "range_intersect(@p0, @p1)"; });
}

SHARDED_TEST_F(ComplianceCodebasedTests, GenerateRangeArray, 2) {
  SetNamePrefix("GenerateRangeArray");
  auto sql_string_fn = [](const FunctionTestCall& f) {
    ABSL_CHECK_GE(f.params.num_params(), 2);
    return (f.params.num_params() == 2) ? "generate_range_array(@p0, @p1)"
                                        : "generate_range_array(@p0, @p1, @p2)";
  };
  RunFunctionTestsCustom(Shard(GetFunctionTestsGenerateTimestampRangeArray()),
                         sql_string_fn);
  RunFunctionTestsCustom(
      Shard(GetFunctionTestsGenerateTimestampRangeArrayExtras()),
      sql_string_fn);
  RunFunctionTestsCustom(Shard(GetFunctionTestsGenerateDateRangeArray()),
                         sql_string_fn);
  RunFunctionTestsCustom(Shard(GetFunctionTestsGenerateDateRangeArrayExtras()),
                         sql_string_fn);
  RunFunctionTestsCustom(Shard(GetFunctionTestsGenerateDatetimeRangeArray()),
                         sql_string_fn);
  RunFunctionTestsCustom(
      Shard(GetFunctionTestsGenerateDatetimeRangeArrayExtras()), sql_string_fn);
}

SHARDED_TEST_F(ComplianceCodebasedTests, RangeContains, 1) {
  SetNamePrefix("RangeContains");
  RunFunctionTestsCustom(
      Shard(GetFunctionTestsRangeContains()),
      [](const FunctionTestCall& f) { return "range_contains(@p0, @p1)"; });
}

SHARDED_TEST_F(ComplianceCodebasedTests, IntervalUnaryMinus, 1) {
  SetNamePrefix("IntervalUnaryMinus");
  RunStatementTests(Shard(GetFunctionTestsIntervalUnaryMinus()), "-@p0");
}

SHARDED_TEST_F(ComplianceCodebasedTests, IntervalDateTimestampSubtractions, 1) {
  SetNamePrefix("IntervalDateTimestampSubtractions");
  RunStatementTests(Shard(GetDateTimestampIntervalSubtractions()),
                    "CAST(@p0 - @p1 AS STRING)");
  SetNamePrefix("IntervalDatetimeTimeSubtractions");
  RunStatementTests(Shard(GetDatetimeTimeIntervalSubtractions()),
                    "CAST(@p0 - @p1 AS STRING)");
  SetNamePrefix("IntervalDateTimestampSubtractions_Reverse");
  RunStatementTests(Shard(GetDateTimestampIntervalSubtractions()),
                    "CAST(-(@p1 - @p0) AS STRING)");
  SetNamePrefix("IntervalDatetimeTimeSubtractions_Reverse");
  RunStatementTests(Shard(GetDatetimeTimeIntervalSubtractions()),
                    "CAST(-(@p1 - @p0) AS STRING)");
}

SHARDED_TEST_F(ComplianceCodebasedTests, DateTimestampAddSubInterval, 6) {
  SetNamePrefix("TimestampAddInterval");
  RunStatementTests(Shard(GetTimestampAddSubInterval()), "@p0 + @p1");
  SetNamePrefix("TimestampSubInterval");
  RunStatementTests(Shard(GetTimestampAddSubInterval()), "@p0 - (-@p1)");
  SetNamePrefix("DatetimeAddInterval");
  RunStatementTests(Shard(GetDatetimeAddSubInterval()), "@p0 + @p1");
  SetNamePrefix("DatetimeSubInterval");
  RunStatementTests(Shard(GetDatetimeAddSubInterval()), "@p0 - (-@p1)");
}

SHARDED_TEST_F(ComplianceCodebasedTests, IntervalAddInterval, 1) {
  SetNamePrefix("IntervalAddInterval");
  RunStatementTests(Shard(GetFunctionTestsIntervalAdd()), "@p0 + @p1");
  SetNamePrefix("IntervalAddInterval_Reverse");
  RunStatementTests(Shard(GetFunctionTestsIntervalAdd()), "@p1 + @p0");
  SetNamePrefix("IntervalAddInterval_Negated");
  RunStatementTests(Shard(GetFunctionTestsIntervalAdd()), "@p0 - (-@p1)");
}

SHARDED_TEST_F(ComplianceCodebasedTests, IntervalSubInterval, 1) {
  SetNamePrefix("IntervalSubInterval");
  RunStatementTests(Shard(GetFunctionTestsIntervalSub()), "@p0 - @p1");
  SetNamePrefix("IntervalSubInterval_AddNegative");
  RunStatementTests(Shard(GetFunctionTestsIntervalSub()), "@p0 + (-@p1)");
  SetNamePrefix("IntervalSubInterval_Reversed");
  RunStatementTests(Shard(GetFunctionTestsIntervalSub()), "-(@p1 - @p0)");
}

SHARDED_TEST_F(ComplianceCodebasedTests, IntervalMultiplyInt64, 1) {
  SetNamePrefix("IntervalMultiplyInt64");
  RunStatementTests(Shard(GetFunctionTestsIntervalMultiply()), "@p0 * @p1");
  SetNamePrefix("IntervalMultiplyInt64_Reversed");
  RunStatementTests(Shard(GetFunctionTestsIntervalMultiply()), "@p1 * @p0");
  SetNamePrefix("IntervalMultiplyInt64_Negated");
  RunStatementTests(Shard(GetFunctionTestsIntervalMultiply()),
                    "(-@p0) * (-@p1)");
}

SHARDED_TEST_F(ComplianceCodebasedTests, IntervalDivideInt64, 1) {
  SetNamePrefix("IntervalDivideInt64");
  RunStatementTests(Shard(GetFunctionTestsIntervalDivide()), "@p0 / @p1");
  SetNamePrefix("IntervalDivideInt64_Negated");
  RunStatementTests(Shard(GetFunctionTestsIntervalDivide()), "(-@p0) / (-@p1)");
}

SHARDED_TEST_F(ComplianceCodebasedTests, ExtractFromInterval, 1) {
  SetNamePrefix("ExtractFromInterval");
  auto extract_fn = [](const QueryParamsWithResult& p) {
    return absl::Substitute("EXTRACT($0 FROM @p0)", p.param(1).string_value());
  };
  RunStatementTestsCustom(Shard(GetFunctionTestsExtractInterval()), extract_fn);
}

SHARDED_TEST_F(ComplianceCodebasedTests, JustifyInterval, 1) {
  SetNamePrefix("JustifyInterval");
  auto fn = [](const FunctionTestCall& f) {
    // Test cases expect result to be interval converted to string to compare
    // exact datetime parts.
    return absl::Substitute("CAST($0(@p0) AS STRING)", f.function_name);
  };
  RunFunctionTestsCustom(Shard(GetFunctionTestsJustifyInterval()), fn);
}

SHARDED_TEST_F(ComplianceCodebasedTests, ToSecondsInterval, 1) {
  SetNamePrefix("ToSecondsInterval");
  auto fn = [](const FunctionTestCall& f) {
    // Test cases expect result to be an interval converted to a string to
    // compare exact datetime parts.
    return absl::Substitute("CAST($0(@p0) AS STRING)", f.function_name);
  };
  RunFunctionTestsCustom(Shard(GetFunctionTestsToSecondsInterval()), fn);
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestsFromProtoDuration, 1) {
  SetNamePrefix("FromProtoDuration");
  RunFunctionCalls(Shard(GetFunctionTestsFromProtoDuration()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestsToProtoInterval, 1) {
  SetNamePrefix("ToProtoInterval");
  RunFunctionCalls(Shard(GetFunctionTestsToProtoInterval()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestsCosineDistance, 1) {
  SetNamePrefix("CosineDistance");
  RunFunctionCalls(Shard(GetFunctionTestsCosineDistance()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestsApproxCosineDistance, 1) {
  SetNamePrefix("ApproxCosineDistance");
  RunFunctionCalls(
      Shard(AddSafeFunctionCalls(GetFunctionTestsApproxCosineDistance())));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestsEuclideanDistance, 1) {
  SetNamePrefix("EuclideanDistance");
  RunFunctionCalls(Shard(GetFunctionTestsEuclideanDistance()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestsApproxEuclideanDistance, 1) {
  SetNamePrefix("ApproxEuclideanDistance");
  RunFunctionCalls(
      Shard(AddSafeFunctionCalls(GetFunctionTestsApproxEuclideanDistance())));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestsDotProduct, 1) {
  SetNamePrefix("DotProduct");
  RunFunctionCalls(Shard(GetFunctionTestsDotProduct()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestDotProduct, 1) {
  SetNamePrefix("ApproxDotProduct");
  RunFunctionCalls(
      Shard(AddSafeFunctionCalls(GetFunctionTestsApproxDotProduct())));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestsManhattanDistance, 1) {
  SetNamePrefix("ManhattanDistance");
  RunFunctionCalls(Shard(GetFunctionTestsManhattanDistance()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestsL1Norm, 1) {
  SetNamePrefix("L1Norm");
  RunFunctionCalls(Shard(GetFunctionTestsL1Norm()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestsL2Norm, 1) {
  SetNamePrefix("L2Norm");
  RunFunctionCalls(Shard(GetFunctionTestsL2Norm()));
}

SHARDED_TEST_F(ComplianceCodebasedTests, TestEditDistance, 1) {
  SetNamePrefix("EditDistance");
  auto edit_distance_fn = [](const FunctionTestCall& f) {
    if (f.params.params().size() == 2) {
      return absl::Substitute("$0(@p0, @p1)", f.function_name);
    }
    return absl::Substitute("$0(@p0, @p1, max_distance=>@p2)", f.function_name);
  };

  std::vector<FunctionTestCall> tests = GetFunctionTestsEditDistance();
  for (auto& test_case : tests) {
    if (test_case.params.params().size() == 3) {
      test_case.params.AddRequiredFeature(FEATURE_NAMED_ARGUMENTS);
    }
  }
  std::vector<FunctionTestCall> tests_bytes =
      GetFunctionTestsEditDistanceBytes();
  for (auto& test_case : tests_bytes) {
    test_case.params.AddRequiredFeature(FEATURE_ENABLE_EDIT_DISTANCE_BYTES);
    if (test_case.params.params().size() == 3) {
      test_case.params.AddRequiredFeature(FEATURE_NAMED_ARGUMENTS);
    }
  }
  tests.insert(tests.end(), tests_bytes.begin(), tests_bytes.end());

  RunFunctionTestsCustom(Shard(tests), edit_distance_fn);
}

// Wrap the proto field test cases with civil time typed values. If the type of
// the parameter or the result of the test case is TIME or DATETIME, wrap the
// test case so that the original expected result requires
// FEATURE_CIVIL_TIME in the feature set, and an empty feature set should
// expect to result in an error.
ABSL_MUST_USE_RESULT static const std::vector<QueryParamsWithResult>
WrapProtoFieldTestCasesForCivilTime(
    const std::vector<QueryParamsWithResult>& original_results) {
  std::vector<QueryParamsWithResult> results(original_results);
  for (auto& each : results) {
    // There's only one parameter for each ProtoField test case.
    ABSL_CHECK_EQ(1, each.num_params());
    // Wrap the result when the required feature set is empty.
    if (each.required_features().empty()) {
      if (each.param(0).type()->UsingFeatureV12CivilTimeType() ||
          each.result().type()->UsingFeatureV12CivilTimeType()) {
        each.AddRequiredFeature(FEATURE_CIVIL_TIME);
      }
    }
  }
  return results;
}

void ComplianceCodebasedTests::TestProtoFieldImpl(
    const Value& null_value, const Value& empty_value,
    const Value& filled_value, absl::string_view proto_name,
    const std::string& field_name, const ValueConstructor& expected_default,
    const ValueConstructor& expected_filled_value,
    const absl::Status& expected_status, const TestProtoFieldOptions& options) {
  ABSL_LOG(INFO) << "TestProtoFieldImpl " << field_name;
  const std::string type_for_prefix =
      absl::StrCat(Type::TypeKindToString(expected_filled_value.type()->kind(),
                                          PRODUCT_INTERNAL),
                   ":", proto_name, "::", field_name, ":");

  // Get the proto field on a NULL proto, a mostly empty proto, and a proto
  // with the values filled in.
  SetNamePrefix(absl::StrCat("GetProtoField_", type_for_prefix));
  RunStatementTests(
      WrapProtoFieldTestCasesForCivilTime(
          {{{null_value}, Value::Null(expected_filled_value.type())},
           {{empty_value}, expected_default.get()},
           {{filled_value}, expected_filled_value, expected_status}}),
      absl::StrCat("@p0.", field_name));

  // has_<field> exists on all non-repeated fields.
  if (!expected_default.type()->IsArray()) {
    // Read has_<field> on a NULL proto, a mostly empty proto, and a proto
    // with the values filled in.
    SetNamePrefix(absl::StrCat("HasProtoField_", type_for_prefix));
    RunStatementTests(
        WrapProtoFieldTestCasesForCivilTime(
            {{{null_value}, NullBool()},
             {{empty_value}, Bool(options.expected_has_result_in_empty_value)},
             {{filled_value}, Bool(true)}}),
        absl::StrCat("@p0.has_", field_name));
  }

  if (options.test_build_proto_field) {
    // Build a proto with NEW, setting this field, and using either
    // a NULL value or the value from <expected_filled_value>, which should
    // result in the same proto as <filled_value>.  This is the inverse of
    // the GetProtoField operation above.
    SetNamePrefix(absl::StrCat("BuildProtoField_", type_for_prefix));
    RunStatementTests(
        WrapProtoFieldTestCasesForCivilTime({
            {{Value::Null(expected_filled_value.type())}, empty_value},
            {{expected_filled_value}, filled_value},
        }),
        absl::StrCat("NEW googlesql_test.", proto_name,
                     "(1 AS int64_key_1, "
                     "2 AS int64_key_2, @p0 AS ",
                     field_name, ")"));
  }
}

std::vector<std::function<void()>>
ComplianceCodebasedTests::GetProtoFieldTests() {
  googlesql_test::KitchenSinkPB empty_proto;
  empty_proto.set_int64_key_1(1);
  empty_proto.set_int64_key_2(2);

  const Value empty_value = MakeProtoValue(&empty_proto);
  const Value null_value = Value::Null(empty_value.type());

  googlesql_test::CivilTimeTypesSinkPB empty_civil_time_proto;
  empty_civil_time_proto.set_int64_key_1(1);
  empty_civil_time_proto.set_int64_key_2(2);

  const Value empty_civil_time_value = MakeProtoValue(&empty_civil_time_proto);
  const Value null_civil_time_value =
      Value::Null(empty_civil_time_value.type());

  const absl::Status ok_status = absl::OkStatus();
  const absl::Status error_status(absl::StatusCode::kOutOfRange,
                                  "Corrupted protocol buffer");

  // Options to TestProtoFieldImpl.  Because COLLECT_TEST() converts TEST_*()
  // macros to lambda functions, and the lambda functions capture 'options' by
  // value, changes of options inside a TEST_*() are confined within the lambda
  // function.
  TestProtoFieldOptions options;

  std::vector<std::function<void(void)>> all_functions;

// The following marco converts TEST_*() macros to lambdas and collects them
// into the all_functions list.
#define COLLECT_TEST(f) all_functions.push_back([=]() mutable f)

// The following macros test various fields on protos. The fields with regular
// types and civil time types are wrapped in different proto messages
// (KitchenSinkPB and CivilTimeTypesSinkPB) thus requires the test to work on
// these two proto messages, otherwise the test logic is similar and shared
// between them.

// Helper macro to test non-repeated fields on a proto.
#define TEST_FIELD_ON_PROTO(proto_name, empty_proto, empty_value, null_value, \
                            field_name, setter_value, expected_default,       \
                            expected_value, expected_status)                  \
  {                                                                           \
    googlesql_test::proto_name filled_proto = empty_proto;                    \
    filled_proto.set_##field_name(setter_value);                              \
    const Value filled_value = MakeProtoValue(&filled_proto);                 \
    TestProtoFieldImpl(null_value, empty_value, filled_value, #proto_name,    \
                       #field_name, expected_default, expected_value,         \
                       expected_status, options);                             \
  }
#define TEST_FIELD(field_name, setter_value, expected_default, expected_value) \
  TEST_FIELD_ON_PROTO(KitchenSinkPB, empty_proto, empty_value, null_value,     \
                      field_name, setter_value, expected_default,              \
                      expected_value, ok_status)
#define TEST_CIVIL_TIME_FIELD(field_name, setter_value, expected_default, \
                              expected_value)                             \
  TEST_FIELD_ON_PROTO(CivilTimeTypesSinkPB, empty_civil_time_proto,       \
                      empty_civil_time_value, null_civil_time_value,      \
                      field_name, setter_value, expected_default,         \
                      expected_value, ok_status);
#define TEST_CIVIL_TIME_FIELD_INVALID_VALUE(field_name, setter_value,    \
                                            expected_default)            \
  {                                                                      \
    Value expected_invalid_value = Value::Null(expected_default.type()); \
    options.test_build_proto_field = false;                              \
    TEST_FIELD_ON_PROTO(CivilTimeTypesSinkPB, empty_civil_time_proto,    \
                        empty_civil_time_value, null_civil_time_value,   \
                        field_name, setter_value, expected_default,      \
                        expected_invalid_value, error_status);           \
  }

// Helper macros to test repeated fields on a proto
#define TEST_REPEATED_FIELD_ON_PROTO(proto_name, empty_proto, empty_value, \
                                     null_value, field_name, setter_value, \
                                     element_value, expected_status)       \
  {                                                                        \
    googlesql_test::proto_name filled_proto = empty_proto;                 \
    filled_proto.add_##field_name(setter_value);                           \
    filled_proto.add_##field_name(setter_value);                           \
    const Value filled_value = MakeProtoValue(&filled_proto);              \
    const ArrayType* array_type =                                          \
        test_values::MakeArrayType(element_value.type());                  \
    TestProtoFieldImpl(                                                    \
        null_value, empty_value, filled_value, #proto_name, #field_name,   \
        Value::EmptyArray(array_type),                                     \
        Value::Array(array_type, {element_value, element_value}),          \
        expected_status, options);                                         \
  }
#define TEST_REPEATED_FIELD(field_name, setter_value, element_value)    \
  TEST_REPEATED_FIELD_ON_PROTO(KitchenSinkPB, empty_proto, empty_value, \
                               null_value, field_name, setter_value,    \
                               element_value, ok_status)
#define TEST_REPEATED_CIVIL_TIME_FIELD(field_name, setter_value,              \
                                       element_value)                         \
  TEST_REPEATED_FIELD_ON_PROTO(CivilTimeTypesSinkPB, empty_civil_time_proto,  \
                               empty_civil_time_value, null_civil_time_value, \
                               field_name, setter_value, element_value,       \
                               ok_status)
#define TEST_REPEATED_CIVIL_TIME_FIELD_INVALID_VALUE(field_name, setter_value, \
                                                     invalid_value)            \
  {                                                                            \
    options.test_build_proto_field = false;                                    \
    TEST_REPEATED_FIELD_ON_PROTO(CivilTimeTypesSinkPB, empty_civil_time_proto, \
                                 empty_civil_time_value,                       \
                                 null_civil_time_value, field_name,            \
                                 setter_value, invalid_value, error_status);   \
  }

// Helper macros to test non-repeated nested message fields on a proto
#define TEST_MESSAGE_FIELD_ON_PROTO(proto_name, empty_proto, empty_value,      \
                                    null_value, field_name, field_value,       \
                                    expected_status)                           \
  {                                                                            \
    const Value wrapped_field_value = MakeProtoValue(&field_value);            \
    googlesql_test::proto_name filled_proto = empty_proto;                     \
    *filled_proto.mutable_##field_name() = field_value;                        \
    TestProtoFieldImpl(null_value, empty_value, MakeProtoValue(&filled_proto), \
                       #proto_name, #field_name,                               \
                       Value::Null(wrapped_field_value.type()),                \
                       wrapped_field_value, expected_status, options);         \
  }
#define TEST_MESSAGE_FIELD(field_name, field_value)                    \
  TEST_MESSAGE_FIELD_ON_PROTO(KitchenSinkPB, empty_proto, empty_value, \
                              null_value, field_name, field_value, ok_status)
#define TEST_MESSAGE_CIVIL_TIME_FIELD(field_name, field_value)               \
  TEST_MESSAGE_FIELD_ON_PROTO(CivilTimeTypesSinkPB, empty_civil_time_proto,  \
                              empty_civil_time_value, null_civil_time_value, \
                              field_name, field_value, ok_status)

// Helper macros to test repeated nested message fields on a proto
#define TEST_REPEATED_MESSAGE_FIELD(field_name, element_value)          \
  {                                                                     \
    const Value wrapped_element_value = MakeProtoValue(&element_value); \
    googlesql_test::KitchenSinkPB filled_proto = empty_proto;           \
    *filled_proto.add_##field_name() = element_value;                   \
    *filled_proto.add_##field_name() = element_value;                   \
    const ArrayType* array_type =                                       \
        test_values::MakeArrayType(wrapped_element_value.type());       \
    TestProtoFieldImpl(                                                 \
        null_value, empty_value, MakeProtoValue(&filled_proto),         \
        "KitchenSinkPB", #field_name, Value::EmptyArray(array_type),    \
        Value::Array(array_type,                                        \
                     {wrapped_element_value, wrapped_element_value}),   \
        ok_status, options);                                            \
  }

  // Test basic scalar types.
  COLLECT_TEST(TEST_FIELD(int32_val, 12, Value::Int32(77), Value::Int32(12)));
  COLLECT_TEST(
      TEST_FIELD(uint32_val, 12, Value::Uint32(777), Value::Uint32(12)));
  COLLECT_TEST(TEST_FIELD(int64_val, 12, Value::Int64(0), Value::Int64(12)));
  COLLECT_TEST(TEST_FIELD(uint64_val, 12, Value::Uint64(0), Value::Uint64(12)));
  COLLECT_TEST(TEST_FIELD(string_val, "sss", Value::String("default_name"),
                          Value::String("sss")));
  COLLECT_TEST(
      TEST_FIELD(float_val, 12.5, Value::Float(0), Value::Float(12.5)));
  COLLECT_TEST(
      TEST_FIELD(double_val, 12.5, Value::Double(0), Value::Double(12.5)));
  COLLECT_TEST(
      TEST_FIELD(bytes_val, "bbb", Value::Bytes(""), Value::Bytes("bbb")));
  COLLECT_TEST(TEST_FIELD(bool_val, true, Value::Bool(0), Value::Bool(12)));
  COLLECT_TEST(
      TEST_FIELD(fixed32_val, 12, Value::Uint32(0), Value::Uint32(12)));
  COLLECT_TEST(
      TEST_FIELD(fixed64_val, 12, Value::Uint64(0), Value::Uint64(12)));
  COLLECT_TEST(TEST_FIELD(sfixed32_val, 12, Value::Int32(0), Value::Int32(12)));
  COLLECT_TEST(TEST_FIELD(sfixed64_val, 12, Value::Int64(0), Value::Int64(12)));
  COLLECT_TEST(TEST_FIELD(sint32_val, 12, Value::Int32(0), Value::Int32(12)));
  COLLECT_TEST(TEST_FIELD(sint64_val, 12, Value::Int64(0), Value::Int64(12)));

  // Negative integer values, which encode differently in protos.
  COLLECT_TEST(TEST_FIELD(int32_val, -12, Value::Int32(77), Value::Int32(-12)));
  COLLECT_TEST(TEST_FIELD(int64_val, -12, Value::Int64(0), Value::Int64(-12)));
  COLLECT_TEST(TEST_FIELD(sint32_val, -12, Value::Int32(0), Value::Int32(-12)));
  COLLECT_TEST(TEST_FIELD(sint64_val, -12, Value::Int64(0), Value::Int64(-12)));
  COLLECT_TEST(
      TEST_FIELD(sfixed32_val, -12, Value::Int32(0), Value::Int32(-12)));
  COLLECT_TEST(
      TEST_FIELD(sfixed64_val, -12, Value::Int64(0), Value::Int64(-12)));

  // Test a required field.  We already have 1 filled in to make it valid
  // so don't get a default.
  options.expected_has_result_in_empty_value = true;
  options.test_build_proto_field = false;
  COLLECT_TEST(TEST_FIELD(int64_key_1, 15, Value::Int64(1), Value::Int64(15)));
  options = TestProtoFieldOptions();

  // Test repeated basic scalars.
  COLLECT_TEST(TEST_REPEATED_FIELD(repeated_int32_val, 12, Value::Int32(12)));
  COLLECT_TEST(TEST_REPEATED_FIELD(repeated_uint32_val, 12, Value::Uint32(12)));
  COLLECT_TEST(TEST_REPEATED_FIELD(repeated_int64_val, 12, Value::Int64(12)));
  COLLECT_TEST(TEST_REPEATED_FIELD(repeated_uint64_val, 12, Value::Uint64(12)));
  COLLECT_TEST(
      TEST_REPEATED_FIELD(repeated_string_val, "abc", Value::String("abc")));
  COLLECT_TEST(
      TEST_REPEATED_FIELD(repeated_bytes_val, "def", Value::Bytes("def")));
  COLLECT_TEST(TEST_REPEATED_FIELD(repeated_bool_val, true, Value::Bool(true)));
  COLLECT_TEST(
      TEST_REPEATED_FIELD(repeated_fixed32_val, 12, Value::Uint32(12)));
  COLLECT_TEST(
      TEST_REPEATED_FIELD(repeated_fixed64_val, 12, Value::Uint64(12)));
  COLLECT_TEST(
      TEST_REPEATED_FIELD(repeated_sfixed32_val, -12, Value::Int32(-12)));
  COLLECT_TEST(
      TEST_REPEATED_FIELD(repeated_sfixed64_val, -12, Value::Int64(-12)));
  COLLECT_TEST(
      TEST_REPEATED_FIELD(repeated_sint32_val, -12, Value::Int32(-12)));
  COLLECT_TEST(
      TEST_REPEATED_FIELD(repeated_sint64_val, -12, Value::Int64(-12)));

  // Test packed repeated scalars.
  COLLECT_TEST(
      TEST_REPEATED_FIELD(repeated_int32_packed, 12, Value::Int32(12)));
  COLLECT_TEST(
      TEST_REPEATED_FIELD(repeated_uint32_packed, 12, Value::Uint32(12)));
  COLLECT_TEST(
      TEST_REPEATED_FIELD(repeated_int64_packed, 12, Value::Int64(12)));
  COLLECT_TEST(
      TEST_REPEATED_FIELD(repeated_uint64_packed, 12, Value::Uint64(12)));
  COLLECT_TEST(
      TEST_REPEATED_FIELD(repeated_bool_packed, true, Value::Bool(true)));
  COLLECT_TEST(
      TEST_REPEATED_FIELD(repeated_fixed32_packed, 12, Value::Uint32(12)));
  COLLECT_TEST(
      TEST_REPEATED_FIELD(repeated_fixed64_packed, 12, Value::Uint64(12)));
  COLLECT_TEST(
      TEST_REPEATED_FIELD(repeated_sfixed32_packed, -12, Value::Int32(-12)));
  COLLECT_TEST(
      TEST_REPEATED_FIELD(repeated_sfixed64_packed, -12, Value::Int64(-12)));
  COLLECT_TEST(
      TEST_REPEATED_FIELD(repeated_sint32_packed, -12, Value::Int32(-12)));
  COLLECT_TEST(
      TEST_REPEATED_FIELD(repeated_sint64_packed, -12, Value::Int64(-12)));
  const EnumType* enum_type =
      test_values::MakeEnumType(googlesql_test::TestEnum_descriptor());
  COLLECT_TEST(TEST_REPEATED_FIELD(
      repeated_enum_packed, googlesql_test::TESTENUMNEGATIVE,
      Value::Enum(enum_type, googlesql_test::TESTENUMNEGATIVE)));

  // Dates with default and alternate encodings, and as int32 and int64,
  // and with different forms of the annotation.
  COLLECT_TEST(TEST_FIELD(date, 16101, Value::Date(0), Value::Date(16101)));
  COLLECT_TEST(TEST_FIELD(date, -16101, Value::Date(0), Value::Date(-16101)));
  COLLECT_TEST(TEST_FIELD(date64, 16101, Value::Date(0), Value::Date(16101)));
  COLLECT_TEST(TEST_FIELD(date64, -16101, Value::Date(0), Value::Date(-16101)));
  // The DATE_DECIMAL encoding treats 0 as NULL, so we get defaults of NULL.
  // That won't round trip when we write it back because we'll omit
  // writing a field for the NULL.
  COLLECT_TEST(TEST_FIELD(date_decimal, 20140131, Value::NullDate(),
                          Value::Date(16101)));
  options.test_build_proto_field = false;
  COLLECT_TEST(
      TEST_FIELD(date_decimal, 0, Value::NullDate(), Value::NullDate()));
  options = TestProtoFieldOptions();

  COLLECT_TEST(TEST_FIELD(date64_decimal, 20140131, Value::NullDate(),
                          Value::Date(16101)));
  COLLECT_TEST(TEST_FIELD(date_decimal_legacy, 20140131, Value::NullDate(),
                          Value::Date(16101)));
  COLLECT_TEST(TEST_FIELD(date64_decimal_legacy, 20140131, Value::NullDate(),
                          Value::Date(16101)));

  COLLECT_TEST(TEST_REPEATED_FIELD(repeated_date, 16101, Value::Date(16101)));
  COLLECT_TEST(TEST_REPEATED_FIELD(repeated_date64, 16101, Value::Date(16101)));

  // TEST_TIMESTAMP_FIELDS invokes TEST_FIELD on all variations of
  // the TIMESTAMP-typed fields.  This includes fields with both the
  // (googlesql.format) annotation as well as the deprecated
  // (googlesql.type) annotation at seconds, millis, and micros
  // precision.
#define TEST_TIMESTAMP_FIELDS_1(value_prefix)   \
  TEST_FIELD(timestamp_seconds, value_prefix,   \
             Value::TimestampFromUnixMicros(0), \
             Value::TimestampFromUnixMicros(value_prefix##000000))
#define TEST_TIMESTAMP_FIELDS_2(value_prefix)     \
  TEST_FIELD(timestamp_millis, value_prefix##123, \
             Value::TimestampFromUnixMicros(0),   \
             Value::TimestampFromUnixMicros(value_prefix##123000))
#define TEST_TIMESTAMP_FIELDS_3(value_prefix)        \
  TEST_FIELD(timestamp_micros, value_prefix##123456, \
             Value::TimestampFromUnixMicros(0),      \
             Value::TimestampFromUnixMicros(value_prefix##123456))
#define TEST_TIMESTAMP_FIELDS_4(value_prefix)        \
  TEST_FIELD(timestamp_seconds_format, value_prefix, \
             Value::TimestampFromUnixMicros(0),      \
             Value::TimestampFromUnixMicros(value_prefix##000000))
#define TEST_TIMESTAMP_FIELDS_5(value_prefix)            \
  TEST_FIELD(timestamp_millis_format, value_prefix##123, \
             Value::TimestampFromUnixMicros(0),          \
             Value::TimestampFromUnixMicros(value_prefix##123000))
#define TEST_TIMESTAMP_FIELDS_6(value_prefix)               \
  TEST_FIELD(timestamp_micros_format, value_prefix##123456, \
             Value::TimestampFromUnixMicros(0),             \
             Value::TimestampFromUnixMicros(value_prefix##123456))

  COLLECT_TEST(TEST_TIMESTAMP_FIELDS_1(1409338039));
  COLLECT_TEST(TEST_TIMESTAMP_FIELDS_2(1409338039));
  COLLECT_TEST(TEST_TIMESTAMP_FIELDS_3(1409338039));
  COLLECT_TEST(TEST_TIMESTAMP_FIELDS_4(1409338039));
  COLLECT_TEST(TEST_TIMESTAMP_FIELDS_5(1409338039));
  COLLECT_TEST(TEST_TIMESTAMP_FIELDS_6(1409338039));
  COLLECT_TEST(TEST_TIMESTAMP_FIELDS_1(-1409338039));
  COLLECT_TEST(TEST_TIMESTAMP_FIELDS_2(-1409338039));
  COLLECT_TEST(TEST_TIMESTAMP_FIELDS_3(-1409338039));
  COLLECT_TEST(TEST_TIMESTAMP_FIELDS_4(-1409338039));
  COLLECT_TEST(TEST_TIMESTAMP_FIELDS_5(-1409338039));
  COLLECT_TEST(TEST_TIMESTAMP_FIELDS_6(-1409338039));
  COLLECT_TEST(
      TEST_REPEATED_FIELD(repeated_timestamp_micros, 1409338039111222,
                          Value::TimestampFromUnixMicros(1409338039111222)));
  COLLECT_TEST(
      TEST_REPEATED_FIELD(repeated_timestamp_micros_format, 1409338039111222,
                          Value::TimestampFromUnixMicros(1409338039111222)));

  // Timestamps after 1970-01-01 are normal uint64 values
  COLLECT_TEST(TEST_FIELD(timestamp_uint64, 1412453870568456,
                          Value::TimestampFromUnixMicros(0),
                          Value::TimestampFromUnixMicros(1412453870568456)));
  // Timestamps before 1970-01-01 are very large uint64 values which are
  // bit casted to negative int64 values
  COLLECT_TEST(TEST_FIELD(timestamp_uint64,
                          absl::bit_cast<uint64_t>(-1394820405112092),
                          Value::TimestampFromUnixMicros(0),
                          Value::TimestampFromUnixMicros(-1394820405112092)));

  // Timestamp with default before 1970-01-01
  COLLECT_TEST(TEST_FIELD(timestamp_uint64_default, 1,
                          // bit_cast equivalent to default 18446467286025782675
                          Value::TimestampFromUnixMicros(-276787683768941),
                          Value::TimestampFromUnixMicros(1)));

#define TEST_TIME_MICROS_FIELDS(value)                                         \
  {                                                                            \
    TEST_CIVIL_TIME_FIELD(                                                     \
        time_micros, value.Packed64TimeMicros(),                               \
        Value::Time(TimeValue::FromHMSAndMicros(0, 0, 0, 0)),                  \
        Value::Time(value));                                                   \
    TEST_CIVIL_TIME_FIELD(                                                     \
        time_micros_default, value.Packed64TimeMicros(),                       \
        Value::Time(TimeValue::FromHMSAndMicros(12, 34, 56, 654321)),          \
        Value::Time(value));                                                   \
    TEST_REPEATED_CIVIL_TIME_FIELD(                                            \
        repeated_time_micros, value.Packed64TimeMicros(), Value::Time(value)); \
  }
  COLLECT_TEST(
      TEST_TIME_MICROS_FIELDS(TimeValue::FromHMSAndMicros(0, 0, 0, 0)));
  COLLECT_TEST(
      TEST_TIME_MICROS_FIELDS(TimeValue::FromHMSAndMicros(23, 59, 59, 999999)));
  COLLECT_TEST(
      TEST_TIME_MICROS_FIELDS(TimeValue::FromHMSAndMicros(12, 34, 56, 654321)));

#define TEST_TIME_MICROS_FIELDS_INVALID_VALUE(value)                          \
  {                                                                           \
    TEST_CIVIL_TIME_FIELD_INVALID_VALUE(                                      \
        time_micros, value,                                                   \
        Value::Time(TimeValue::FromHMSAndMicros(0, 0, 0, 0)));                \
    TEST_CIVIL_TIME_FIELD_INVALID_VALUE(                                      \
        time_micros_default, value,                                           \
        Value::Time(TimeValue::FromHMSAndMicros(12, 34, 56, 654321)));        \
    TEST_REPEATED_CIVIL_TIME_FIELD_INVALID_VALUE(repeated_time_micros, value, \
                                                 Value::NullTime());          \
  }

  COLLECT_TEST(TEST_TIME_MICROS_FIELDS_INVALID_VALUE(-1));
  COLLECT_TEST(TEST_TIME_MICROS_FIELDS_INVALID_VALUE(0x1800000000));  // 24h
  COLLECT_TEST(TEST_TIME_MICROS_FIELDS_INVALID_VALUE(0xF0000000));    // 60m
  COLLECT_TEST(TEST_TIME_MICROS_FIELDS_INVALID_VALUE(0x3C00000));     // 60s
  COLLECT_TEST(TEST_TIME_MICROS_FIELDS_INVALID_VALUE(0xF4240));  // 1M micros
  // The lower 37 bits actually decode to 12:34:56.654321, but there is
  // something in the higher, unused bits, making it invalid.
  COLLECT_TEST(TEST_TIME_MICROS_FIELDS_INVALID_VALUE(0x100000c8b89fbf1));

#define TEST_DATETIME_MICROS_FIELDS(value)                                    \
  {                                                                           \
    TEST_CIVIL_TIME_FIELD(datetime_micros, value.Packed64DatetimeMicros(),    \
                          Value::NullDatetime(), Value::Datetime(value));     \
    TEST_CIVIL_TIME_FIELD(datetime_micros_default,                            \
                          value.Packed64DatetimeMicros(),                     \
                          Value::Datetime(DatetimeValue::FromYMDHMSAndMicros( \
                              1970, 1, 1, 0, 0, 0, 0)),                       \
                          Value::Datetime(value));                            \
    TEST_REPEATED_CIVIL_TIME_FIELD(repeated_datetime_micros,                  \
                                   value.Packed64DatetimeMicros(),            \
                                   Value::Datetime(value));                   \
  }

  COLLECT_TEST(TEST_DATETIME_MICROS_FIELDS(
      DatetimeValue::FromYMDHMSAndMicros(1, 1, 1, 0, 0, 0, 0)));
  COLLECT_TEST(TEST_DATETIME_MICROS_FIELDS(
      DatetimeValue::FromYMDHMSAndMicros(9999, 12, 31, 23, 59, 59, 999999)));
  COLLECT_TEST(TEST_DATETIME_MICROS_FIELDS(
      DatetimeValue::FromYMDHMSAndMicros(1970, 1, 1, 0, 0, 0, 0)));
  COLLECT_TEST(TEST_DATETIME_MICROS_FIELDS(
      DatetimeValue::FromYMDHMSAndMicros(2016, 2, 16, 13, 35, 57, 456789)));

#define TEST_DATETIME_MICROS_FIELDS_INVALID_VALUE(value)                  \
  {                                                                       \
    TEST_CIVIL_TIME_FIELD_INVALID_VALUE(datetime_micros,                  \
                                        value.Packed64DatetimeMicros(),   \
                                        Value::NullDatetime());           \
    TEST_CIVIL_TIME_FIELD_INVALID_VALUE(                                  \
        datetime_micros_default, value.Packed64DatetimeMicros(),          \
        Value::Datetime(                                                  \
            DatetimeValue::FromYMDHMSAndMicros(1970, 1, 1, 0, 0, 0, 0))); \
    TEST_REPEATED_CIVIL_TIME_FIELD_INVALID_VALUE(                         \
        repeated_datetime_micros, value.Packed64DatetimeMicros(),         \
        Value::NullDatetime());                                           \
  }

  COLLECT_TEST(TEST_DATETIME_MICROS_FIELDS_INVALID_VALUE(
      DatetimeValue::FromYMDHMSAndMicros(0, 0, 0, -1, -1, -1, -1)));
  COLLECT_TEST(TEST_DATETIME_MICROS_FIELDS_INVALID_VALUE(
      DatetimeValue::FromYMDHMSAndMicros(99999, 99, 99, 99, 99, 99, 9999999)));

  // Test enums.
  COLLECT_TEST(TEST_FIELD(test_enum, googlesql_test::TESTENUM2,
                          Value::Enum(enum_type, googlesql_test::TESTENUM0),
                          Value::Enum(enum_type, googlesql_test::TESTENUM2)));
  COLLECT_TEST(TEST_REPEATED_FIELD(
      repeated_test_enum, googlesql_test::TESTENUMNEGATIVE,
      Value::Enum(enum_type, googlesql_test::TESTENUMNEGATIVE)));

  // Test nested proto.
  googlesql_test::KitchenSinkPB::Nested nested;
  nested.set_nested_int64(1234);
  nested.add_nested_repeated_int64(55);
  nested.add_nested_repeated_int64(-66);
  COLLECT_TEST(TEST_MESSAGE_FIELD(nested_value, nested));
  COLLECT_TEST(TEST_REPEATED_MESSAGE_FIELD(nested_repeated_value, nested));

  // Test nested civil time proto
  googlesql_test::CivilTimeTypesSinkPB::NestedCivilTimeFields civil_time_nested;
  civil_time_nested.set_time_micros(
      TimeValue::FromHMSAndMicros(12, 34, 56, 654321).Packed64TimeMicros());
  civil_time_nested.set_datetime_micros(
      DatetimeValue::FromYMDHMSAndMicros(2016, 3, 3, 16, 29, 48, 456789)
          .Packed64DatetimeMicros());
  COLLECT_TEST(TEST_MESSAGE_CIVIL_TIME_FIELD(nested_civil_time_fields,
                                             civil_time_nested));

  googlesql_test::CivilTimeTypesSinkPB::NestedCivilTimeRepeatedFields
      civil_time_nested_repeated;
  civil_time_nested_repeated.add_repeated_datetime_micros(
      TimeValue::FromHMSAndMicros(0, 0, 0, 0).Packed64TimeMicros());
  civil_time_nested_repeated.add_repeated_datetime_micros(
      TimeValue::FromHMSAndMicros(23, 59, 59, 999999).Packed64TimeMicros());
  civil_time_nested_repeated.add_repeated_datetime_micros(
      DatetimeValue::FromYMDHMSAndMicros(1970, 1, 1, 0, 0, 0, 0)
          .Packed64DatetimeMicros());
  COLLECT_TEST(TEST_MESSAGE_CIVIL_TIME_FIELD(nested_civil_time_repeated_fields,
                                             civil_time_nested_repeated));

  // Test group.
  googlesql_test::KitchenSinkPB::OptionalGroup optional_group;
  optional_group.set_int64_val(-123);
  optional_group.add_optionalgroupnested()->set_int64_val(555);
  COLLECT_TEST(TEST_MESSAGE_FIELD(optional_group, optional_group));

  googlesql_test::KitchenSinkPB::NestedRepeatedGroup nrg;
  nrg.set_id(10);
  nrg.set_idstr("abc");
  COLLECT_TEST(TEST_REPEATED_MESSAGE_FIELD(nested_repeated_group, nrg));

  // Default handling.
  COLLECT_TEST(
      TEST_FIELD(int_with_no_default, 100, Value::Int64(0), Value::Int64(100)));
  COLLECT_TEST(
      TEST_FIELD(int_with_default, 100, Value::Int64(17), Value::Int64(100)));
  COLLECT_TEST(TEST_FIELD(int_with_no_default_nullable, 100, Value::NullInt64(),
                          Value::Int64(100)));
  COLLECT_TEST(TEST_FIELD(int_with_default_nullable, 100, Value::NullInt64(),
                          Value::Int64(100)));

  // These are annotated with is_struct and is_wrapper, but we treat them
  // as regular protos and don't unwrap them to googlesql types.
  googlesql_test::NullableInt nullable_int;
  nullable_int.set_value(111);
  googlesql_test::KeyValueStruct key_value_struct;
  key_value_struct.set_key("aaa");
  COLLECT_TEST(TEST_MESSAGE_FIELD(nullable_int, nullable_int));
  COLLECT_TEST(TEST_MESSAGE_FIELD(key_value, key_value_struct));
  COLLECT_TEST(TEST_REPEATED_MESSAGE_FIELD(nullable_int_array, nullable_int));
  COLLECT_TEST(TEST_REPEATED_MESSAGE_FIELD(key_value_array, key_value_struct));

  COLLECT_TEST(TEST_FIELD(has_confusing_name, "abc", Value::String(""),
                          Value::String("abc")));
  // This field is actually called MIXED_case, but generated proto methods
  // are lowercased, and lower-case SQL works too.
  COLLECT_TEST(
      TEST_FIELD(mixed_case, "abc", Value::String(""), Value::String("abc")));

  googlesql_test::EmptyMessage empty_message;
  COLLECT_TEST(TEST_MESSAGE_FIELD(empty_message, empty_message));

  return all_functions;
}

bool ComplianceCodebasedTests::DriverCanRunTests() {
  return !GetCodeBasedTestsEnvironment()->skip_codebased_tests();
}

// Test reading proto fields, reading has_<field>, and building protos with
// particular fields set, for various field types in KitchenSinkPB.
// Even with 500 query shards of this tests take a long time relative to others.
// The number of shards for this case is picked so that each shard has ~100
// functions.
SHARDED_TEST_F(ComplianceCodebasedTests, TestProtoFields, 9) {
  for (const auto& function : Shard(GetProtoFieldTests())) {
    function();
  }
}

TEST_F(ComplianceCodebasedTests, TestWideStruct) {
  if (!DriverCanRunTests()) {
    return;
  }

  SetNamePrefix("TestWideStruct");
  int kWideFields = 1000;
  std::vector<StructField> fields;
  std::vector<Value> values;
  for (int i = 0; i < kWideFields; i++) {
    fields.push_back(StructField(absl::StrCat("x", i), Int64Type()));
    values.push_back(Value::Int64(i));
  }
  const StructType* struct_type = MakeStructType(fields);
  Value struct_value = Value::Struct(struct_type, values);
  // Query for entire STRUCT.
  RunStatementTests({QueryParamsWithResult({ValueConstructor(struct_value)},
                                           ValueConstructor(struct_value))},
                    "@p0");
  // Fetch first, last and middle fields.
  for (int i : {0, kWideFields - 1, kWideFields / 2}) {
    RunStatementTests(
        {QueryParamsWithResult({ValueConstructor(struct_value), i},
                               ValueConstructor(Value::Int64(i)))},
        absl::StrCat("@p0.x", i));
  }
}

TEST_F(ComplianceCodebasedTests, TestDeepStruct) {
  if (!DriverCanRunTests()) {
    return;
  }

  SetNamePrefix("TestDeepStruct");
  int kDeepFields = 20;

  StructField struct_field("x", Int64Type());
  const StructType* struct_type = nullptr;
  Value struct_value = Value::Int64(1);
  for (int i = 0; i < kDeepFields; i++) {
    struct_type = MakeStructType({struct_field});
    struct_value = Value::Struct(struct_type, {struct_value});
    struct_field = StructField("x", struct_type);
  }

  // Query for entire STRUCT.
  RunStatementTests({QueryParamsWithResult({ValueConstructor(struct_value)},
                                           ValueConstructor(struct_value))},
                    "@p0");

  // Query for every field in the chain.
  for (int i = 0; i < kDeepFields; i++) {
    Value field_value = struct_value;
    std::string field_refs;
    for (int j = 0; j < i; j++) {
      absl::StrAppend(&field_refs, ".x");
      field_value = field_value.field(0);
    }
    RunStatementTests(
        {QueryParamsWithResult({ValueConstructor(struct_value), i},
                               ValueConstructor(field_value))},
        absl::StrCat("@p0", field_refs));
  }
}

TEST_F(ComplianceCodebasedTests, TestDeepArray) {
  if (!DriverCanRunTests()) {
    return;
  }

  SetNamePrefix("TestDeepArray");
  int kDepth = 10;

  const ArrayType* array_type = MakeArrayType(Int64Type());
  Value array_value =
      Value::Array(array_type, {Value::Int64(1), Value::Int64(2)});
  for (int i = 0; i < kDepth; i++) {
    StructField struct_field("x", array_type);
    const StructType* struct_type = MakeStructType({struct_field});
    Value struct_value = Value::Struct(struct_type, {array_value});
    array_type = MakeArrayType(struct_type);
    array_value = Value::Array(array_type, {struct_value});
    // Query for entire ARRAY.
    RunStatementTests({QueryParamsWithResult({ValueConstructor(array_value)},
                                             ValueConstructor(array_value))},
                      "@p0");
  }

  SetNamePrefix("TestDeepArray_UNNEST");
  std::string q = "@p0";
  for (int i = 0; i < kDepth; i++) {
    q = absl::Substitute("(SELECT t.x FROM UNNEST($0) t)", q);
  }
  RunStatementTests(
      {QueryParamsWithResult(
          {ValueConstructor(array_value)},
          ValueConstructor(Value::Array(MakeArrayType(Int64Type()),
                                        {Value::Int64(1), Value::Int64(2)})))},
      q);
}

TEST_F(ComplianceCodebasedTests, TestRecursiveProto) {
  if (!DriverCanRunTests()) {
    return;
  }

  const int kDepth = 20;

  googlesql_test::RecursiveMessage message;
  // TODO: jmorcos - Is this supposed to work with the static type factory?
  const ProtoType* proto_type =
      test_values::MakeProtoType(message.GetDescriptor());

  // NULL recursive message.
  SetNamePrefix("TestRecursiveProto_NULL");
  RunStatementTests(
      {QueryParamsWithResult({}, ValueConstructor(Value::Null(proto_type)))},
      "CAST(NULL AS googlesql_test.RecursiveMessage)");

  // Add one more recursion level at every step.
  SetNamePrefix("TestRecursiveProto_Message");
  googlesql_test::RecursiveMessage* innermost_message = &message;
  Value proto_value;
  for (int i = 0; i < kDepth; i++) {
    absl::Cord bytes;
    ABSL_CHECK(message.SerializeToCord(&bytes));
    proto_value = Value::Proto(proto_type, bytes);
    RunStatementTests({QueryParamsWithResult({ValueConstructor(proto_value), i},
                                             ValueConstructor(proto_value))},
                      "@p0");

    innermost_message->set_int64_field(i);
    innermost_message = innermost_message->mutable_recursive_msg();
  }
  absl::Cord bytes_3611;
  ABSL_CHECK(message.SerializeToCord(&bytes_3611));
  proto_value = Value::Proto(proto_type, bytes_3611);

  // Select message subfields.
  SetNamePrefix("TestRecursiveProto_SubMessage");
  std::string field_refs;
  innermost_message = &message;
  for (int i = 0; i < kDepth; i++) {
    absl::Cord bytes;
    ABSL_CHECK(innermost_message->SerializeToCord(&bytes));
    Value field_value = Value::Proto(proto_type, bytes);
    RunStatementTests({QueryParamsWithResult({ValueConstructor(proto_value), i},
                                             ValueConstructor(field_value))},
                      absl::StrCat("@p0", field_refs));
    absl::StrAppend(&field_refs, ".recursive_msg");
    innermost_message = innermost_message->mutable_recursive_msg();
  }

  // Select message subfield at level deeper than what exists in the message
  // results in NULL.
  RunStatementTests(
      {QueryParamsWithResult({ValueConstructor(proto_value)},
                             ValueConstructor(Value::Null(proto_type)))},
      absl::StrCat("@p0", field_refs, ".recursive_msg"));

  // Select scalar field of message subfield.
  SetNamePrefix("TestRecursiveProto_ScalarField");
  field_refs.clear();
  innermost_message = &message;
  for (int i = 0; i < kDepth; i++) {
    RunStatementTests({QueryParamsWithResult({ValueConstructor(proto_value), i},
                                             Value::Int64(i))},
                      absl::StrCat("@p0", field_refs, ".int64_field"));
    absl::StrAppend(&field_refs, ".recursive_msg");
    innermost_message = innermost_message->mutable_recursive_msg();
  }
}

// Test that TABLESAMPLE queries are repeatable if given a REPEAT input.
TEST_F(ComplianceCodebasedTests, TablesampleRepeatableTests) {
  if (!DriverCanRunTests() || !DriverSupportsFeature(FEATURE_TABLESAMPLE)) {
    return;
  }

  auto test_query_is_repeatable = [&](absl::string_view query) {
    constexpr int kNumIterations = 10;
    std::vector<absl::StatusOr<ComplianceTestCaseResult>> results;
    results.reserve(kNumIterations);
    for (int i = 0; i < kNumIterations; ++i) {
      results.push_back(RunSQL(query));
    }

    for (int i = 1; i < kNumIterations; ++i) {
      EXPECT_EQ(results[0].status(), results[i].status());
    }

    // An engine may fail to execute the query, in which case the test is
    // ignored.
    if (!results[0].ok()) {
      return;
    }

    Value first = std::get<Value>(*results[0]);
    for (int i = 1; i < kNumIterations; ++i) {
      Value rerun = std::get<Value>(*results[i]);
      EXPECT_THAT(rerun, Returns(first)) << full_name();
    }
  };

  SetNamePrefix("TablesampleRepeatable_25Percent");
  test_query_is_repeatable(R"(
    SELECT primary_key
    FROM TableLarge
    TABLESAMPLE BERNOULLI(25 PERCENT) REPEATABLE(0)
    ORDER BY primary_key
  )");

  SetNamePrefix("TablesampleRepeatable_0Percent");
  test_query_is_repeatable(R"(
    SELECT primary_key
    FROM TableLarge
    TABLESAMPLE BERNOULLI(0 PERCENT) REPEATABLE(10)
    ORDER BY primary_key
  )");

  SetNamePrefix("TablesampleRepeatable_80Percent");
  test_query_is_repeatable(R"(
    SELECT primary_key
    FROM TableLarge
    TABLESAMPLE BERNOULLI(80 PERCENT) REPEATABLE(11)
    ORDER BY primary_key
    LIMIT 1
  )");

  SetNamePrefix("TablesampleRepeatable_2000Rows");
  test_query_is_repeatable(R"(
    SELECT primary_key
    FROM TableLarge
    TABLESAMPLE RESERVOIR(2000 ROWS) REPEATABLE(3250)
    ORDER BY primary_key
  )");

  SetNamePrefix("TablesampleRepeatable_5Rows");
  test_query_is_repeatable(R"(
    SELECT primary_key
    FROM TableLarge
    TABLESAMPLE RESERVOIR(5 ROWS) REPEATABLE(-532)
    ORDER BY primary_key
  )");

  SetNamePrefix("TablesampleRepeatable_1Rows");
  test_query_is_repeatable(R"(
    SELECT primary_key
    FROM TableLarge
    TABLESAMPLE RESERVOIR(1 ROWS) REPEATABLE(32)
    ORDER BY primary_key
  )");

  SetNamePrefix("TablesampleRepeatable_0Rows");
  test_query_is_repeatable(R"(
    SELECT primary_key
    FROM TableLarge
    TABLESAMPLE RESERVOIR(0 ROWS) REPEATABLE(1003932)
    ORDER BY primary_key
    LIMIT 1
  )");
}

TEST_F(ComplianceCodebasedTests, IntegerRoundtrip) {
  if (!DriverCanRunTests()) {
    return;
  }

  SetNamePrefix("IntegerRoundtrip");
  std::vector<QueryParamsWithResult> tests;
  // Note - the below transformation only works correctly with non-negative
  // integers.
  for (int64_t number : std::vector<int64_t>({
           0,
           1,
           22,
           300,
           4848,
           55555,
           601111,
           7771234,
           59483726,
           123456789,
           9876543210,
           122333444455555,
           888887777666554,
           999999999999999999,
           1000000000000000000,
           5647382910564738291,
           std::numeric_limits<int64_t>::max() - 1,
           std::numeric_limits<int64_t>::max(),
       })) {
    // After transformation the result should be exactly the same as original
    // number.
    tests.push_back(QueryParamsWithResult({number}, number));
  }

  RunStatementTests(
      tests,
      // 1. Convert number to string
      // 2. Split string into digits array
      // 3. Sort digits array in reverse order (by offset)
      // 4. Convert string digits to integer and multiply by 10^offset
      // 5. Sum them up
      // The resulting number should be the same as the original number.
      R"(
          (select sum(cast(d as INT64) * cast(pow(10, o) as INT64)) from
            unnest(array(
              select d from
                unnest(split(cast(@p0 as string), "")) d WITH OFFSET o
                order by o desc)) d with offset o)
      )");
}

class ComplianceFilebasedTests : public SQLTestBase {
 public:
  const std::string GetTestSuiteName() override {
    return "ComplianceFilebasedTests";
  }

  // Use File::Match(...) to return all test files matching
  // kTestFilenamePattern. The result will be fed into
  // INSTANTIATE_TEST_CASE_P(...) to instantiate all file-base tests.
  static std::vector<std::string> AllTestFiles() {
    if (!absl::GetFlag(FLAGS_googlesql_run_filebased_tests)) {
      return {};
    }
    std::vector<std::string> test_filenames;
    std::string file_path =
        googlesql_base::JoinPath(absl::NullSafeStringView(getenv("TEST_SRCDIR")),
                       "_main/googlesql/compliance/testdata",
                       absl::GetFlag(FLAGS_file_pattern));
    GOOGLESQL_CHECK_OK(googlesql::internal::Match(file_path, &test_filenames));
    ABSL_CHECK(!test_filenames.empty()) << "No test files found at " << file_path;
    return test_filenames;
  }

 protected:
  ComplianceFilebasedTests() {}
  ~ComplianceFilebasedTests() override {}
};
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(
    ComplianceFilebasedTests);  // TODO (broken link)

// All file-based tests.
TEST_P(ComplianceFilebasedTests, FilebasedTest) { RunSQLTests(GetParam()); }

namespace {
std::string NameForFilebasedTest(
    const ::testing::TestParamInfo<std::string>& info) {
  return absl::StrReplaceAll(googlesql_base::Basename(info.param), {{".", "_"}});
}
}  // namespace

INSTANTIATE_TEST_SUITE_P(
    FilebasedTest, ComplianceFilebasedTests,
    ::testing::ValuesIn(ComplianceFilebasedTests::AllTestFiles()),
    NameForFilebasedTest);
}  // namespace googlesql
