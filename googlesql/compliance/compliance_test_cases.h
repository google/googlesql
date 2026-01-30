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

// Compliance unit tests infrastructure for GoogleSQL.
//
#ifndef GOOGLESQL_COMPLIANCE_COMPLIANCE_TEST_CASES_H_
#define GOOGLESQL_COMPLIANCE_COMPLIANCE_TEST_CASES_H_

#include <stddef.h>
#include <stdint.h>

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/compliance/sql_test_base.h"
#include "googlesql/compliance/test_driver.h"
#include "googlesql/public/value.h"
#include "googlesql/testing/test_function.h"
#include "googlesql/testing/test_value.h"
#include "gtest/gtest.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/status.h"

namespace googlesql {

// A common environment for codebased tests using ComplianceCodebasedTests
// harness and its several subclasses. We cannot simply use SetUpTestCase in
// ComplianceCodebasedTests because it has several subclasses that instantiate
// the ShardedTest<N> template. SetUpTestCase will run once per subclass, and
// that will get expensive.
class CodebasedTestsEnvironment : public ::testing::Environment {
 public:
  ~CodebasedTestsEnvironment() override {}

  bool skip_codebased_tests();

  void SetUp() override;
  void TearDown() override;
};

// The compliance code-based tests operate in two distinct modes:
// 1) testing the reference driver
// 2) testing an engine driver
//
// When testing the reference driver, the queries are executed
// against the reference driver and the result is compared against
// the test case result.
//
// When testing an engine driver, the queries are executed against
// both the engine driver and the reference driver, and the
// results from the two drivers are compared.
class ComplianceCodebasedTests : public SQLTestBase {
 public:
  const std::string GetTestSuiteName() override {
    return "ComplianceCodebasedTests";
  }

  static void SetUpTestSuite();

 protected:
  ComplianceCodebasedTests();

  ~ComplianceCodebasedTests() override;

  absl::Status CreateDatabase(const TestDatabase& test_db) override {
    // Do nothing, we already created the database during test case setup.
    return absl::OkStatus();
  }

  // This overrides SQLTestBase::SetUp() to avoid re-initializing the database
  // to empty before each test.
  void SetUp() override {}

  // Obtains a vector of FunctionTestParams's via 'get_function_tests'.  For
  // each test param, constructs a SQL statement (e.g., 'SELECT '+sql_string)
  // and executes it. Query parameters are set from FunctionTestParams and are
  // named @p0, @p1, etc. Examples of sql_string are '@p0 + @p1', 'sqrt(@p0)',
  // etc.
  void RunStatementTests(
      absl::Span<const QueryParamsWithResult> statement_tests,
      absl::string_view sql_string);

  // Takes 'operator_name' as input and creates a sql string of the kind @p0
  // op_name @p1 ... op_name @pn.
  void RunFunctionTestsInfix(
      absl::Span<const QueryParamsWithResult> function_tests,
      absl::string_view operator_name);

  // Creates a sql string of the kind @p0 IN (@p1, @p2, ...)
  void RunFunctionTestsInOperator(
      absl::Span<const QueryParamsWithResult> function_tests);

  // Creates a sql string of the kind @p0 IN UNNEST([@p1, @p2, ...])
  void RunFunctionTestsInOperatorUnnestArray(
      absl::Span<const QueryParamsWithResult> function_tests);

  // Runs given function tests.
  // function_name(@p0, @p1, ..., @pn).
  void RunFunctionTestsPrefix(
      absl::Span<const QueryParamsWithResult> function_tests,
      absl::string_view function_name);

  // Return the original vector of tests, extended with an additional
  // SAFE-mode function call for each case that returns an error.
  std::vector<FunctionTestCall> AddSafeFunctionCalls(
      const std::vector<FunctionTestCall>& calls);

  // For each original FunctionTestCall, build QueryParamsWithResult enabled for
  // FEATURE_DATE_ARITHMETICS language feature.
  std::vector<QueryParamsWithResult> GetFunctionTestsDateArithmetics(
      absl::Span<const FunctionTestCall> tests);

  // For each function test call, runs the original version of the function in
  // addition to the SAFE version of the function, if the language feature is
  // enabled.
  void RunFunctionCalls(
      const std::vector<FunctionTestCall>& function_calls);

  // Same as above, but creates FunctionTestCall from the provided
  // QueryParamsWithResults and function_name.
  void RunFunctionCalls(absl::Span<const QueryParamsWithResult> test_cases,
                        absl::string_view function_name);

  // For each function test call, runs the original version of the function in
  // addition to the SAFE version of the function, if the language feature is
  // enabled. In addition, handles named value arguments if provided.
  void RunFunctionCallsWithNamedValueArguments(
      const std::vector<FunctionTestCall>& function_calls);

  // Runs the given normalize function tests.
  // function_name(@p0 [, mode]).
  // This is slightly different from the RunFunctionCalls above as we need to
  // handle the normalize mode like an identifier instead of a parameter.
  void RunNormalizeFunctionCalls(
      const std::vector<FunctionTestCall>& function_calls);

  // Runs the given aggregation function tests.
  // SELECT function_name(x, @p1, ..., @pn)
  // FROM UNNEST(@p0) AS x
  void RunAggregationFunctionCalls(
      const std::vector<FunctionTestCall>& function_calls);

  // Same as above but accepts a callback 'get_sql_string' instead of a fixed
  // string. The callback can be used to construct custom function invocations
  // such as 'CAST(@p0 AS <typename>)'. It has the signature
  //   std::function<string (const FunctionTestParams& p)>
  // FCT can accept all kinds of callable objects including lambdas with
  // captures. Since std::function is banned by the C++ styleguide, FCT is
  // specified as a template parameter.
  template <typename FCT>
  void RunStatementTestsCustom(
      absl::Span<const QueryParamsWithResult> statement_tests,
      FCT get_sql_string);

  // Same as above but takes vector<FunctionTestCall> instead.
  template <typename FCT>
  void RunFunctionTestsCustom(absl::Span<const FunctionTestCall> function_tests,
                              FCT get_sql_string);

  // Runs a statement with required and/or prohibited features.
  void RunStatementOnFeatures(absl::string_view sql,
                              const QueryParamsWithResult& params);

  // Options that modify which specific tests we run or what behavior we
  // expect for a particular field in TestProtoFieldImpl.
  struct TestProtoFieldOptions {
    TestProtoFieldOptions() {}

    // Expected result when we read has_<field> on <empty_value>.
    bool expected_has_result_in_empty_value = false;

    // Whether to do the proto construction tests for this field.
    bool test_build_proto_field = true;
  };

  // Implementation for TestProtoField that tests operations for one
  // particular field of the proto message identified by <proto_name>.
  //   null_value - a NULL proto message
  //   empty_value - a proto message with just the required fields filled in
  //   filled_value - a proto message with the tested field filled in
  //   proto_name - the name of the proto message we're testing
  //   field_name - the name of the field we're testing
  //   expected_default - the value we expect when reading an unset value
  //                      for this field
  //   expected_filled_value - the value we expect when reading the
  //                           field from <filled_value>
  //   expected_status - the expected error status if the test statement to
  //                     access the proto field is expected to fail. By default
  //                     it's an OK status indicating the test statement will
  //                     succeed.
  //   options - behavior modifiers defined in the options class above
  void TestProtoFieldImpl(
      const Value& null_value, const Value& empty_value,
      const Value& filled_value, absl::string_view proto_name,
      const std::string& field_name, const ValueConstructor& expected_default,
      const ValueConstructor& expected_filled_value,
      const absl::Status& expected_status = absl::OkStatus(),
      const TestProtoFieldOptions& options = TestProtoFieldOptions());

  // Returns a list of functions to test proto fields.
  std::vector<std::function<void()>> GetProtoFieldTests();

  // Returns true if the driver under test is capable of running
  // the compliance tests.
  virtual bool DriverCanRunTests();

 private:
  // HACK: Introduce an alias for the superclass (SQLTestBase) so that build
  // rule for the unique name tests can just replace all occurrences of the
  // string "SQLTestBase" in the cc file without breaking anything.
  using SuperclassAlias = SQLTestBase;
};

// Defines a class to evenly shard a test.
template <size_t NumShards, typename BaseT = ComplianceCodebasedTests>
class ShardedTest : public BaseT {
 public:
  static constexpr size_t kMaxQueriesPerShard = 500;

  ~ShardedTest() override {
    EXPECT_TRUE(sharded_) << "Please call Shard() in test " << test_name();
  }

  // Shards a list of functions to <num_shards> of equal-sized shards.
  template <class ElementType>
  std::vector<ElementType> Shard(const std::vector<ElementType>& list) {
    sharded_ = true;
    std::vector<ElementType> shard;
    for (size_t i = 0; i < list.size(); ++i) {
      if ((i % NumShards) == index()) {
        shard.emplace_back(list.at(i));
      }
    }
    size_t total_queries = NumShards * (shard.size() + 1);
    size_t num_shards = total_queries / kMaxQueriesPerShard +
                     ((total_queries % kMaxQueriesPerShard) != 0);
    EXPECT_LE(shard.size(), kMaxQueriesPerShard)
        << "A shard of " << test_name() << " has over " << kMaxQueriesPerShard
        << " queries. Please increase number of shards to " << num_shards;
    return shard;
  }

  // Shards a span of test cases to <num_shards> of equal-sized shards,
  template <class ElementType>
  absl::Span<const ElementType> Shard(absl::Span<const ElementType> span) {
    sharded_ = true;
    size_t slice_size = (span.size() + NumShards - 1) / NumShards;
    EXPECT_LE(slice_size, kMaxQueriesPerShard)
        << "A shard of " << test_name() << " has over " << kMaxQueriesPerShard
        << " queries. Please increase number of shards to "
        << ((slice_size + kMaxQueriesPerShard - 1) / kMaxQueriesPerShard);

    return span.subspan(std::min(slice_size * index(), span.size()),
                        slice_size);
  }

  // Returns a list of shard indices as strings.
  static std::vector<std::string> AllShards() {
    std::vector<std::string> shards;
    shards.reserve(NumShards);
    for (size_t i = 0; i < NumShards; ++i) {
      shards.emplace_back(absl::StrCat(i));
    }
    return shards;
  }

  // Called when all shards should be skipped (e.g. because they test something
  // optional).
  void SkipAllShards() { sharded_ = true; }

  // Accessors for test_name and index.
  void set_test_name(absl::string_view test_name) { test_name_ = test_name; }
  const std::string& test_name() const { return test_name_; }

  void set_index(const size_t index) { index_ = index; }
  size_t index() const { return index_; }

  // Runs the test.
  virtual void RunTest() = 0;

 private:
  size_t index_ = 0;
  std::string test_name_ = "";
  bool sharded_ = false;
};

template <size_t NumShards, typename BaseT>
constexpr size_t ShardedTest<NumShards, BaseT>::kMaxQueriesPerShard;

// SHARDED_TEST_F(...) shards a function test into equal-sized test shards.
// It helps balance the load of test shards and reduce test timeouts.
// To use, simply replace TEST_F with SHARDED_TEST_F, give it an additional
// parameter: <num_shards>, and enclose GetFunctionXYZ() by Shard().
//
// For example:
//
// TEST_F(ComplianceCodebasedTests, TestXYZ)
// {
//   RunFunctionTests(GetFunctionXYZ());
// }
//
// shall be converted to
//
// SHARDED_TEST_F(ComplianceCodebasedTests, TestXYZ, 3)
// {
//   RunFunctionTests(Shard(GetFunctionTestXYZ()));
// }
//
// Use SHARDED_TEST_F() when possible. It ensures test shard size is within
// limit.
//
#define _SHARDED_TEST_PASTE1(x, y) x##y
#define _SHARDED_TEST_PASTE(x, y) _SHARDED_TEST_PASTE1(x, y)
#define _SHARDED_TEST_CLASS(x) _SHARDED_TEST_PASTE(ShardedTest_, x)
#define SHARDED_TEST_F(test_class, test_name, num_shards)         \
  class _SHARDED_TEST_CLASS(test_name)                            \
      : public ShardedTest<num_shards, test_class> {              \
   public:                                                        \
    void RunTest() override {                                     \
      if (!DriverCanRunTests()) {                                 \
        SkipAllShards();                                          \
        return;                                                   \
      }                                                           \
      RunTestInternal();                                          \
    }                                                             \
                                                                  \
   private:                                                       \
    void RunTestInternal();                                       \
  };                                                              \
  TEST_P(ShardedTest_##test_name, test_name) {                    \
    set_test_name(#test_name);                                    \
    uint32_t index;                                               \
    ABSL_CHECK(absl::SimpleAtoi(GetParam(), &index));                  \
    set_index(index);                                             \
    RunTest();                                                    \
  }                                                               \
  INSTANTIATE_TEST_SUITE_P(                                       \
      test_name, ShardedTest_##test_name,                         \
      ::testing::ValuesIn(ShardedTest_##test_name::AllShards())); \
  void _SHARDED_TEST_CLASS(test_name)::RunTestInternal()

}  // namespace googlesql

#endif  // GOOGLESQL_COMPLIANCE_COMPLIANCE_TEST_CASES_H_
