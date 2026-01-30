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

#include <algorithm>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

#include "googlesql/common/match_recognize/match_test_result.pb.h"
#include "googlesql/common/match_recognize/test_matcher.h"
#include "googlesql/common/testing/proto_matchers.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/functions/match_recognize/match_partition.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast_enums.pb.h"
#include "gtest/gtest.h"
#include "googlesql/base/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/source_location.h"
#include "absl/types/span.h"

namespace googlesql::functions::match_recognize {
namespace {
using testing::EqualsProto;
using ::testing::TestParamInfo;
using ::testing::ValuesIn;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;

std::vector<std::vector<int>> Repeat(absl::Span<const std::vector<int>> input,
                                     int rep_count) {
  std::vector<std::vector<int>> result;
  for (int i = 0; i < rep_count; ++i) {
    result.insert(result.end(), input.begin(), input.end());
  }
  return result;
}

std::vector<std::vector<int>> Concat(
    const std::vector<std::vector<int>>& input1,
    const std::vector<std::vector<int>>& input2) {
  std::vector<std::vector<int>> result(input1.begin(), input1.end());
  result.insert(result.end(), input2.begin(), input2.end());
  return result;
}

template <typename... Args>
std::vector<std::vector<int>> Concat(absl::Span<const std::vector<int>> first,
                                     Args... args) {
  std::vector<std::vector<int>> result(first.begin(), first.end());
  std::vector<std::vector<int>> rest = Concat(args...);
  result.insert(result.end(), rest.begin(), rest.end());
  return result;
}

// The result of a test.
// Either a MatchPartitionResultProto text proto (if success is expected),
// or a non-ok status code (if failure is expected).
using TestResult = std::variant<absl::StatusCode, std::string>;

// A concrete test test describing specific inputs and expected outputs.
// Each one of these objects defines a test case in the Sponge log.
struct TestCase {
  // The name of the test in the Sponge log. Restricted to alphanumeric
  // characters only and must be unique across all TestCase objects.
  std::string name;

  // Source location indicating where the fields in this TestCase object come
  // from. This is added to the Sponge log, along with the test name.
  googlesql_base::SourceLocation location;

  // MATCH_RECOGNIZE pattern that input should be matched against (e.g. "A B*").
  std::string pattern;

  // Input data to be matched against the pattern:
  // - Outer vector contains one element per row.
  // - For each row, the inner vector is a list of pattern variables satisfied
  //   by that row (the first pattern variable, alphabetically, referenced in
  //   the query is 0, the second is id 1, etc., so if the pattern is "A B*",
  //   {0} means a row that satisifes "A", but not "B".
  std::vector<std::vector<int>> row_data;

  // Additional test configuration (e.g. query parameters, overlapping mode,
  // deferred compilation mode, etc.)
  TestMatchOptions config;

  // Expected test result.
  TestResult expected_result;
};

std::string BoolToString(bool value) { return value ? "true" : "false"; }

// Prints a human-readable representation of a TestCase object. This determines
// the value_param text for the test case in the Sponge log.
void PrintTo(const TestCase& test_case, std::ostream* os) {
  // Print basic information
  (*os) << test_case.location.file_name() << ", line "
        << test_case.location.line() << "\n";
  (*os) << "pattern: " << test_case.pattern << "\n";
  (*os) << "overlapping mode: "
        << ((test_case.config.after_match_skip_mode ==
             ResolvedMatchRecognizeScanEnums::NEXT_ROW)
                ? "true"
                : "false");
  (*os) << "\n";
  (*os) << "longest match mode: ";
  if (!test_case.config.longest_match_mode_sql.empty()) {
    (*os) << test_case.config.longest_match_mode_sql << "\n";
  } else {
    (*os) << "false (defaulted)\n";
  }
  (*os) << "deferred query parameters: "
        << BoolToString(test_case.config.defer_query_parameters);
  (*os) << "\n";
  (*os) << "round trip serialization: "
        << BoolToString(test_case.config.round_trip_serialization);
  (*os) << "\n";

  // Print query parameters, if we have them.
  const QueryParameterData& query_params = test_case.config.query_parameters;
  std::string param_label =
      test_case.config.defer_query_parameters ? "deferred param" : "param";
  if (query_params.HasNamedQueryParams()) {
    // Sort query parameter names in alphabetical order, rather than the order
    // of the hash map.
    std::vector<std::string> param_names;
    for (const auto& [param_name, param_value] :
         query_params.GetNamedQueryParams()) {
      param_names.push_back(param_name);
    }
    std::sort(param_names.begin(), param_names.end());
    for (const std::string& param_name : param_names) {
      (*os) << param_label << " @" << param_name << ": "
            << query_params.GetNamedQueryParam(param_name).DebugString()
            << "\n";
    }
  } else if (query_params.HasPositionalQueryParams()) {
    int param_index = 0;
    for (const Value& param : query_params.GetPositionalQueryParams()) {
      (*os) << param_label << " " << param_index << ": " << param.DebugString()
            << "\n";
      ++param_index;
    }
  }

  // Print row data.
  if (test_case.row_data.empty()) {
    (*os) << "<no rows>\n";
  } else {
    for (int i = 0; i < test_case.row_data.size(); ++i) {
      (*os) << "row " << i << ": ["
            << absl::StrJoin(test_case.row_data[i], ", ") << "]" << "\n";
    }
  }

  // Print expected result
  if (std::holds_alternative<std::string>(test_case.expected_result)) {
    (*os) << "Expected:\n" << std::get<std::string>(test_case.expected_result);
  } else {
    (*os) << "Expected: Status: "
          << std::get<absl::StatusCode>(test_case.expected_result);
  }
}

// Describes a condition for which a particular test result is valid.
struct TestCondition {
  // Specifies whether overlapping mode needs to be on or off in order for this
  // test result to apply. If nullopt, the test result applies regardless of
  // overlapping mode.
  std::optional<bool> overlapping_mode;

  // Specifies whether longest-match mode needs to be on or off in order for
  // this test result to apply. If nullopt, the test result applies regardless
  // of longest-match mode.
  std::optional<bool> longest_match_mode;

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const TestCondition& cond) {
    std::vector<std::string> fields;
    if (cond.overlapping_mode.has_value()) {
      fields.push_back(
          absl::StrCat("overlapping: ", BoolToString(*cond.overlapping_mode)));
    }
    if (cond.longest_match_mode.has_value()) {
      fields.push_back(absl::StrCat("longest_match: ",
                                    BoolToString(*cond.longest_match_mode)));
    }
    absl::Format(&sink, "{%s}", absl::StrJoin(fields, ", "));
  }

  // Returns true if all test cases satisfied by 'cond' are satisfied by this
  // as well.
  bool AppliesWhen(const TestCondition& cond) const {
    if (overlapping_mode.has_value() && cond.overlapping_mode.has_value() &&
        (*overlapping_mode != *cond.overlapping_mode)) {
      return false;
    }
    if (longest_match_mode.has_value() && cond.longest_match_mode.has_value() &&
        (*longest_match_mode != *cond.longest_match_mode)) {
      return false;
    }
    return true;
  }
};

// Result of a test under certain conditions. A TestCaseTemplate defines a list
// of these objects, which collectively, define the expected result for all
// cases; TestCase has only the actual result matching the specific test case.
struct ConditionalTestResult {
  // The condition for which this result can be used.
  TestCondition condition;

  // The expected result of the test, subject to the above conditions.
  TestResult result;

  // Name of another test case to import the expected result from, empty to use
  // 'result' directly.
  std::string test_case_result_import;
};

// Template for constructing a collection of test cases that share common
// attributes (e.g. pattern, input, expected result).
//
// Each TestCaseTemplate object consists of input data, a list of patterns,
// and expected results for overlapping and non-overlapping mode. Each template
// automatically constructs a test case for all possible pattern/overlapping
// mode combinations.
class TestCaseBuilder;
class TestCaseTemplate {
 public:
  explicit TestCaseTemplate(const absl::string_view name,
                            const googlesql_base::SourceLocation& source_location =
                                googlesql_base::SourceLocation::current())
      : name_(name), source_location_(source_location) {}

  TestCaseTemplate& AddPattern(absl::string_view pattern) {
    patterns_.push_back(std::string(pattern));
    return *this;
  }

  TestCaseTemplate& AddPatterns(std::vector<std::string> patterns) {
    patterns_.insert(patterns_.end(), patterns.begin(), patterns.end());
    return *this;
  }

  TestCaseTemplate& SetRowData(const std::vector<std::vector<int>>& row_data) {
    row_data_ = row_data;
    return *this;
  }

  // Sets the configuration that tests should be run against.
  TestCaseTemplate& SetConfig(const TestMatchOptions& config) {
    config_ = config;
    return *this;
  }

  // Registers an expected test result with no condition.
  TestCaseTemplate& AddResult(const TestResult& result) {
    return AddResult({}, result);
  }

  // Registers an expected test result under a particular condition.
  // The first result added whose condition is satisfied by a particular test
  // configuration is the one that is used for that configuration.
  TestCaseTemplate& AddResult(const TestCondition& condition,
                              const TestResult& result) {
    results_.push_back({.condition = condition, .result = result});
    return *this;
  }

  // Registers an expected test result that applies to non-overlapping test
  // configurations only. The first result added that matches a test's
  // configuration is the one used.
  TestCaseTemplate& AddOverlappingResult(const TestResult& result) {
    return AddResult({.overlapping_mode = true}, result);
  }

  // Registers an expected test result that applies to overlapping test
  // configurations only. The first result added that matches a test's
  // configuration is the one used.
  TestCaseTemplate& AddNonOverlappingResult(const TestResult& result) {
    return AddResult({.overlapping_mode = false}, result);
  }

  // Imports all results from another test case when the given condition is
  // satisfied.
  TestCaseTemplate& AddImportedResults(const TestCondition& condition,
                                       absl::string_view test_case_name) {
    results_.push_back(
        {.condition = condition,
         .test_case_result_import = std::string(test_case_name)});
    return *this;
  }

  // Returns the list of test cases constructed from this template.
  std::vector<TestCase> MakeTestCases(const TestCaseBuilder& builder) const;

  absl::string_view name() const { return name_; }
  const std::vector<ConditionalTestResult>& results() const { return results_; }

 private:
  struct TestCaseConfig {
    bool overlapping_mode = false;
    bool longest_match_mode = false;
    int pattern_index = 0;
    bool defer_query_parameters = false;
  };
  // Makes one specific test case for the given configuration.
  TestCase MakeTestCase(const TestCaseConfig& config,
                        const TestCaseBuilder& builder) const;

  std::string name_;
  googlesql_base::SourceLocation source_location_;
  std::vector<std::string> patterns_;
  std::vector<std::vector<int>> row_data_;
  TestMatchOptions config_;
  std::vector<ConditionalTestResult> results_;
};

// Helper class to put together the full list of test cases for all the various
// scenarios.
class TestCaseBuilder {
 public:
  TestCaseBuilder() { AddAllTestCases(); }

  std::vector<TestCase> MakeTestCases() const {
    std::vector<TestCase> test_cases;
    for (const TestCaseTemplate& test_case_template : templates_) {
      std::vector<TestCase> new_test_cases =
          test_case_template.MakeTestCases(*this);
      test_cases.insert(test_cases.end(), new_test_cases.begin(),
                        new_test_cases.end());
    }
    return test_cases;
  }

  // Returns the expected result of the test with the given name and condition.
  // Useful for reusing expected results across multiple tests.
  //
  // Crashes if no matching test case exists.
  TestResult GetExpectedResult(absl::string_view test_case_name,
                               const TestCondition& condition,
                               const googlesql_base::SourceLocation& location) const;

 private:
  void AddAllTestCases();

  void AddBasicTestCases();
  void AddOverlappingMatchesTestCases();
  void AddQuestionTestCases();
  void AddBoundedQuantifierTestCases();
  void AddUnboundedQuantifierTestCases();
  void AddQueryParameterTestCases();
  void AddAnchorTestCases();
  void AddTestCases(const TestCaseTemplate& test_case_template);

  std::vector<TestCaseTemplate> templates_;
};

TestCase TestCaseTemplate::MakeTestCase(const TestCaseConfig& config,
                                        const TestCaseBuilder& builder) const {
  TestCase test_case;
  test_case.name = name_;
  test_case.location = source_location_;
  test_case.pattern = patterns_[config.pattern_index];
  test_case.row_data = row_data_;
  test_case.config = config_;

  if (patterns_.size() > 1) {
    // Add a unique suffix to the test name to indicate the pattern.
    absl::StrAppend(&test_case.name, config.pattern_index);
  }
  if (config.overlapping_mode) {
    absl::StrAppend(&test_case.name, "Overlapping");
    test_case.config.after_match_skip_mode =
        ResolvedMatchRecognizeScanEnums::NEXT_ROW;
  }
  if (config.longest_match_mode) {
    absl::StrAppend(&test_case.name, "LongestMatch");
    test_case.config.longest_match_mode_sql = "TRUE";
  }
  if (config.defer_query_parameters) {
    absl::StrAppend(&test_case.name, "ParamsDeferred");
    test_case.config.defer_query_parameters = true;

    // Serialization support is not yet implemented in the
    // deferred-query-parameters scenario, so run the test without it.
    // TODO: Enable serialization for these tests, once
    // implemented.
    test_case.config.round_trip_serialization = false;
  }
  bool result_found = false;
  for (const ConditionalTestResult& result : results_) {
    if (result.condition.overlapping_mode != std::nullopt &&
        *result.condition.overlapping_mode != config.overlapping_mode) {
      continue;
    }
    if (result.condition.longest_match_mode != std::nullopt &&
        *result.condition.longest_match_mode != config.longest_match_mode) {
      continue;
    }
    if (result.test_case_result_import.empty()) {
      test_case.expected_result = result.result;
    } else {
      test_case.expected_result = builder.GetExpectedResult(
          result.test_case_result_import,
          {.overlapping_mode = config.overlapping_mode,
           .longest_match_mode = config.longest_match_mode},
          source_location_);
    }
    result_found = true;
    break;
  }
  ABSL_CHECK(result_found) << "No test result found for line "
                      << source_location_.line()
                      << " (overlapping_mode = " << config.overlapping_mode
                      << ", longest_match_mode = " << config.longest_match_mode
                      << ")";
  return test_case;
}

std::vector<TestCase> TestCaseTemplate::MakeTestCases(
    const TestCaseBuilder& builder) const {
  std::vector<TestCase> test_cases;
  for (bool overlapping_mode : {false, true}) {
    for (bool longest_match_mode : {false, true}) {
      for (int pattern_index = 0; pattern_index < patterns_.size();
           ++pattern_index) {
        TestCaseConfig config = {.overlapping_mode = overlapping_mode,
                                 .longest_match_mode = longest_match_mode,
                                 .pattern_index = pattern_index,
                                 .defer_query_parameters = false};
        test_cases.push_back(MakeTestCase(config, builder));

        if (config_.query_parameters.HasQueryParams()) {
          // Add an additional copy of the test case with query parameters
          // deferred.
          config.defer_query_parameters = true;
          test_cases.push_back(MakeTestCase(config, builder));
        }
      }
    }
  }
  return test_cases;
}

TestResult TestCaseBuilder::GetExpectedResult(
    absl::string_view test_case_name, const TestCondition& condition,
    const googlesql_base::SourceLocation& location) const {
  for (const TestCaseTemplate& test_case_template : templates_) {
    if (test_case_template.name() != test_case_name) {
      continue;
    }
    for (const ConditionalTestResult& result : test_case_template.results()) {
      if (!result.condition.AppliesWhen(condition)) {
        continue;
      }
      return result.result;
    }
  }
  ABSL_LOG(FATAL) << "No expected result found for " << test_case_name
             << " with condition " << absl::StrCat(condition) << " (at "
             << location.file_name() << ", line " << location.line() << ")";
}

void TestCaseBuilder::AddTestCases(const TestCaseTemplate& test_case_template) {
  templates_.push_back(test_case_template);
}

void TestCaseBuilder::AddAllTestCases() {
  AddBasicTestCases();
  AddOverlappingMatchesTestCases();
  AddQuestionTestCases();
  AddBoundedQuantifierTestCases();
  AddUnboundedQuantifierTestCases();
  AddQueryParameterTestCases();
  AddAnchorTestCases();
}

void TestCaseBuilder::AddBasicTestCases() {
  AddTestCases(TestCaseTemplate("EmptyPartitionNoMatch")
                   .AddPattern("A B B C")
                   .SetRowData({})
                   .AddResult(R"pb(finalize {})pb"));
  AddTestCases(TestCaseTemplate("PartitionWithNoMatchingSymbols")
                   .AddPattern("A B B C")
                   .SetRowData({{}, {}, {}, {}})
                   .AddResult(
                       R"pb(add_row { rep_count: 4 }
                            finalize {})pb"));
  AddTestCases(TestCaseTemplate("SingleMatchAtStartFoundAtEnd")
                   .AddPattern("A B B C")
                   .SetRowData({{0}, {1}, {1}, {2}, {0}})
                   .AddResult(
                       R"pb(add_row { rep_count: 5 }
                            finalize {
                              match: "(position 0, length 4): A, B, B, C"

                            })pb"));
  AddTestCases(
      TestCaseTemplate("SingleMatchAtEnd")
          .AddPattern("A B B C")
          .SetRowData({{}, {0}, {1}, {1}, {2}})
          .AddResult(
              R"pb(add_row { rep_count: 5 }
                   finalize { match: "(position 1, length 4): A, B, B, C" }
              )pb"));
  AddTestCases(
      TestCaseTemplate("MultipleMatchesWithGap")
          .AddPattern("A B B C")
          .SetRowData({{}, {0}, {1}, {1}, {2}, {2}, {0}, {1}, {1}, {2}})
          .AddResult(
              R"pb(add_row { rep_count: 5 }
                   add_row { match: "(position 1, length 4): A, B, B, C" }
                   add_row { rep_count: 4 }
                   finalize { match: "(position 6, length 4): A, B, B, C" }
              )pb"));
  AddTestCases(TestCaseTemplate("EmptyMatches")
                   .AddPattern("A | ()")
                   .SetRowData({{}, {}, {}, {}})
                   .AddResult(R"pb(add_row { match: "(position 0, empty)" }
                                   add_row { match: "(position 1, empty)" }
                                   add_row { match: "(position 2, empty)" }
                                   add_row { match: "(position 3, empty)" }
                                   finalize {}
                   )pb"));
  AddTestCases(
      TestCaseTemplate("EmptyAndNonEmptyMatches")
          .AddPattern("A | ()")
          .SetRowData({{}, {0}, {}, {0}})
          .AddResult(R"pb(add_row { match: "(position 0, empty)" }
                          add_row {}
                          add_row {
                            match: "(position 1, length 1): A"
                            match: "(position 2, empty)"
                          }
                          add_row {}
                          finalize { match: "(position 3, length 1): A" }
          )pb"));
}

void TestCaseBuilder::AddOverlappingMatchesTestCases() {
  AddTestCases(
      TestCaseTemplate("TwoOverlappingMatches")
          .AddPattern("A B B C")
          .SetRowData({{}, {0}, {0, 1}, {1}, {1, 2}, {2}, {2}})
          .AddNonOverlappingResult(
              R"pb(add_row { rep_count: 6 }
                   add_row { match: "(position 1, length 4): A, B, B, C" }
                   finalize {}
              )pb")
          .AddOverlappingResult(
              R"pb(add_row { rep_count: 6 }
                   add_row {
                     match: "(position 1, length 4): A, B, B, C"
                     match: "(position 2, length 4): A, B, B, C"
                   }
                   finalize {})pb"));

  // A long sequence of rows, where every row matches every pattern variable.
  // This creates an overlapping match starting at almost every row.
  //
  // Note: Showing match id's for this one test as a sanity check that match
  // ids are generated properly, as most tests omit them for brevity.
  AddTestCases(
      TestCaseTemplate("ChainOfOverlappingMatches")
          .AddPattern("A B B C")
          .SetRowData(Repeat({{0, 1, 2}}, 18))
          .SetConfig({.show_match_ids = true})
          .AddNonOverlappingResult(
              R"pb(add_row { rep_count: 18 }
                   finalize {
                     match: "(position 0, id 1, length 4): A, B, B, C"
                     match: "(position 4, id 2, length 4): A, B, B, C"
                     match: "(position 8, id 3, length 4): A, B, B, C"
                     match: "(position 12, id 4, length 4): A, B, B, C"
                   }
              )pb")
          .AddOverlappingResult(
              R"pb(add_row { rep_count: 18 }
                   finalize {
                     match: "(position 0, id 1, length 4): A, B, B, C"
                     match: "(position 1, id 2, length 4): A, B, B, C"
                     match: "(position 2, id 3, length 4): A, B, B, C"
                     match: "(position 3, id 4, length 4): A, B, B, C"
                     match: "(position 4, id 5, length 4): A, B, B, C"
                     match: "(position 5, id 6, length 4): A, B, B, C"
                     match: "(position 6, id 7, length 4): A, B, B, C"
                     match: "(position 7, id 8, length 4): A, B, B, C"
                     match: "(position 8, id 9, length 4): A, B, B, C"
                     match: "(position 9, id 10, length 4): A, B, B, C"
                     match: "(position 10, id 11, length 4): A, B, B, C"
                     match: "(position 11, id 12, length 4): A, B, B, C"
                     match: "(position 12, id 13, length 4): A, B, B, C"
                     match: "(position 13, id 14, length 4): A, B, B, C"
                     match: "(position 14, id 15, length 4): A, B, B, C"
                   })pb"));

  AddTestCases(
      TestCaseTemplate("MatchFromEveryRowToEnd")
          .AddPattern("A*")
          .SetRowData(Repeat({{0}}, 5))
          .AddNonOverlappingResult(
              R"pb(add_row { rep_count: 5 }
                   finalize { match: "(position 0, length 5): A, A, A, A, A" }
              )pb")
          .AddOverlappingResult(
              R"pb(add_row { rep_count: 5 }
                   finalize {
                     match: "(position 0, length 5): A, A, A, A, A"
                     match: "(position 1, length 4): A, A, A, A"
                     match: "(position 2, length 3): A, A, A"
                     match: "(position 3, length 2): A, A"
                     match: "(position 4, length 1): A"
                   }
              )pb"));

  AddTestCases(
      TestCaseTemplate("TwoRowsRepeating")
          .AddPattern("(A B)*")
          .SetRowData(Repeat({{0}, {1}}, 5))
          .AddNonOverlappingResult(
              R"pb(
                add_row { rep_count: 10 }
                finalize {
                  match: "(position 0, length 10): A, B, A, B, A, B, A, B, A, B"
                })pb")
          .AddOverlappingResult(
              R"pb(
                add_row { rep_count: 10 }
                finalize {
                  match: "(position 0, length 10): A, B, A, B, A, B, A, B, A, B"
                  match: "(position 1, empty)"
                  match: "(position 2, length 8): A, B, A, B, A, B, A, B"
                  match: "(position 3, empty)"
                  match: "(position 4, length 6): A, B, A, B, A, B"
                  match: "(position 5, empty)"
                  match: "(position 6, length 4): A, B, A, B"
                  match: "(position 7, empty)"
                  match: "(position 8, length 2): A, B"
                  match: "(position 9, empty)"
                })pb"));
}

void TestCaseBuilder::AddQuestionTestCases() {
  std::vector<std::vector<int>> simple_question_input = {{}, {0}, {0}, {}};
  AddTestCases(TestCaseTemplate("SimpleQuestionGreedy")
                   .AddPatterns({"A?", "A{0,1}", "(A?)?", "A|", "(A|)?"})
                   .SetRowData(simple_question_input)
                   .AddResult(R"pb(add_row { match: "(position 0, empty)" }
                                   add_row { rep_count: 2 }
                                   add_row {
                                     match: "(position 1, length 1): A"
                                     match: "(position 2, length 1): A"
                                     match: "(position 3, empty)"
                                   }
                                   finalize {})pb"));
  AddTestCases(TestCaseTemplate("SimpleQuestionReluctant")
                   .AddPatterns({"A??", "A{0,1}?", "(A??)??", "|A", "(|A)?",
                                 "(|A)??", "(A|)??"})
                   .SetRowData(simple_question_input)
                   .AddResult({.longest_match_mode = false},
                              R"pb(add_row { match: "(position 0, empty)" }
                                   add_row { rep_count: 2 }
                                   add_row {
                                     match: "(position 1, empty)"
                                     match: "(position 2, empty)"
                                     match: "(position 3, empty)"
                                   }
                                   finalize {})pb")
                   .AddImportedResults({.longest_match_mode = true},
                                       "SimpleQuestionGreedy"));

  std::vector<std::vector<int>> complex_question_input = {{0, 1}, {0}, {0, 1},
                                                          {1},    {0}, {0}};
  AddTestCases(TestCaseTemplate("ComplexQuestionGreedy")
                   .AddPatterns({"(A B)? A", "(A B A) | A", "((A B) | ()) A",
                                 "((A B) | ()) (A | A)"})
                   .SetRowData(complex_question_input)
                   .AddNonOverlappingResult(
                       R"pb(add_row { rep_count: 6 }
                            finalize {
                              match: "(position 0, length 1): A"
                              match: "(position 1, length 1): A"
                              match: "(position 2, length 3): A, B, A"
                              match: "(position 5, length 1): A"
                            })pb")
                   .AddOverlappingResult(
                       R"pb(add_row { rep_count: 6 }
                            finalize {
                              match: "(position 0, length 1): A"
                              match: "(position 1, length 1): A"
                              match: "(position 2, length 3): A, B, A"
                              match: "(position 4, length 1): A"
                              match: "(position 5, length 1): A"
                            })pb"));
  AddTestCases(TestCaseTemplate("ComplexQuestionReluctant")
                   .AddPatterns({"(A B)?? A", "(| A B) A", "A | (A B A)",
                                 "A () | (A B () A)"})
                   .SetRowData(complex_question_input)
                   .AddResult({.longest_match_mode = false},
                              R"pb(add_row { rep_count: 6 }
                                   finalize {
                                     match: "(position 0, length 1): A"
                                     match: "(position 1, length 1): A"
                                     match: "(position 2, length 1): A"
                                     match: "(position 4, length 1): A"
                                     match: "(position 5, length 1): A"
                                   })pb")
                   .AddImportedResults({.longest_match_mode = true},
                                       "ComplexQuestionGreedy"));

  std::vector<std::vector<int>> complex_question2_input = {
      {0, 2}, {1, 2}, {2}, {0}, {1}, {2}, {0}};
  AddTestCases(TestCaseTemplate("ComplexQuestion2Greedy")
                   .AddPattern("(A B)? C")
                   .SetRowData(complex_question2_input)
                   .AddNonOverlappingResult(
                       R"pb(add_row { rep_count: 7 }
                            finalize {
                              match: "(position 0, length 3): A, B, C"
                              match: "(position 3, length 3): A, B, C"
                            })pb")
                   .AddOverlappingResult(R"pb(
                     add_row { rep_count: 7 }
                     finalize {
                       match: "(position 0, length 3): A, B, C"
                       match: "(position 1, length 1): C"
                       match: "(position 2, length 1): C"
                       match: "(position 3, length 3): A, B, C"
                       match: "(position 5, length 1): C"
                     })pb"));
  AddTestCases(
      TestCaseTemplate("ComplexQuestion2Reluctant")
          .AddPattern("(A B)?? C")
          .SetRowData(complex_question2_input)
          .AddResult({.overlapping_mode = false, .longest_match_mode = false},
                     R"pb(add_row { rep_count: 7 }
                          finalize {
                            match: "(position 0, length 1): C"
                            match: "(position 1, length 1): C"
                            match: "(position 2, length 1): C"
                            match: "(position 3, length 3): A, B, C"
                          })pb")
          .AddResult({.overlapping_mode = true, .longest_match_mode = false},
                     R"pb(add_row { rep_count: 7 }
                          finalize {
                            match: "(position 0, length 1): C"
                            match: "(position 1, length 1): C"
                            match: "(position 2, length 1): C"
                            match: "(position 3, length 3): A, B, C"
                            match: "(position 5, length 1): C"
                          })pb")
          .AddImportedResults({.longest_match_mode = true},
                              "ComplexQuestion2Greedy"));
}

void TestCaseBuilder::AddBoundedQuantifierTestCases() {
  std::vector<std::vector<int>> bound_zero_to_n_input =
      Concat({{}, {}, {0}, {}}, Repeat({{0}}, 10));
  AddTestCases(TestCaseTemplate("BoundZeroToNGreedy")
                   .AddPatterns({"A{,3}", "A{0,3}", "(A A A)|(A A)|A|",
                                 "(A A A) | A{,2}", "(A A A)|(A A)|A?"})
                   .SetRowData(bound_zero_to_n_input)
                   .AddNonOverlappingResult(
                       R"pb(add_row { match: "(position 0, empty)" }
                            add_row { match: "(position 1, empty)" }
                            add_row {}
                            add_row {
                              match: "(position 2, length 1): A"
                              match: "(position 3, empty)"
                            }
                            add_row { rep_count: 10 }
                            finalize {
                              match: "(position 4, length 3): A, A, A"
                              match: "(position 7, length 3): A, A, A"
                              match: "(position 10, length 3): A, A, A"
                              match: "(position 13, length 1): A"
                            })pb")
                   .AddOverlappingResult(
                       R"pb(add_row { match: "(position 0, empty)" }
                            add_row { match: "(position 1, empty)" }
                            add_row {}
                            add_row {
                              match: "(position 2, length 1): A"
                              match: "(position 3, empty)"
                            }
                            add_row { rep_count: 10 }
                            finalize {
                              match: "(position 4, length 3): A, A, A"
                              match: "(position 5, length 3): A, A, A"
                              match: "(position 6, length 3): A, A, A"
                              match: "(position 7, length 3): A, A, A"
                              match: "(position 8, length 3): A, A, A"
                              match: "(position 9, length 3): A, A, A"
                              match: "(position 10, length 3): A, A, A"
                              match: "(position 11, length 3): A, A, A"
                              match: "(position 12, length 2): A, A"
                              match: "(position 13, length 1): A"
                            })pb"));
  AddTestCases(TestCaseTemplate("BoundZeroToNReluctant")
                   .AddPatterns({"A{,3}?", "A{0,3}?", "|A|(A A)|(A A A)",
                                 "A{,2}?|(A A A)", "A??|(A A)|(A A A)"})
                   .SetRowData(bound_zero_to_n_input)
                   .AddResult({.longest_match_mode = false},
                              R"pb(add_row { match: "(position 0, empty)" }
                                   add_row { match: "(position 1, empty)" }
                                   add_row {}
                                   add_row {
                                     match: "(position 2, empty)"
                                     match: "(position 3, empty)"
                                   }
                                   add_row { rep_count: 10 }
                                   finalize {
                                     match: "(position 4, empty)"
                                     match: "(position 5, empty)"
                                     match: "(position 6, empty)"
                                     match: "(position 7, empty)"
                                     match: "(position 8, empty)"
                                     match: "(position 9, empty)"
                                     match: "(position 10, empty)"
                                     match: "(position 11, empty)"
                                     match: "(position 12, empty)"
                                     match: "(position 13, empty)"
                                   })pb")
                   .AddImportedResults({.longest_match_mode = true},
                                       "BoundZeroToNGreedy"));

  // Greedy/reluctantness doesn't matter when the upper and lower bound are both
  // zero.
  AddTestCases(TestCaseTemplate("BoundZeroToZero")
                   .AddPatterns({"A{0}", "A{,0}", "A{0,0}", "A{0,0}?", "A{,0}?",
                                 "(A{0})?"})
                   .SetRowData(Repeat({{0}}, 4))
                   .AddResult(
                       R"pb(add_row { match: "(position 0, empty)" }
                            add_row { match: "(position 1, empty)" }
                            add_row { match: "(position 2, empty)" }
                            add_row { match: "(position 3, empty)" }
                            finalize {})pb"));

  AddTestCases(
      TestCaseTemplate("BoundMToNGreedy")
          .AddPatterns({"A{2,3}", "A A{1,2}", "A A A?", "A{2} A?",
                        "(A A A)|(A A)", "A{3} | A{2}"})
          .SetRowData(
              Concat(Repeat({{0}}, 4), Repeat({{}}, 4), Repeat({{0}}, 2)))
          .AddNonOverlappingResult(
              R"pb(add_row { rep_count: 4 }
                   add_row { match: "(position 0, length 3): A, A, A" }
                   add_row { rep_count: 5 }
                   finalize { match: "(position 8, length 2): A, A" })pb")
          .AddOverlappingResult(
              R"pb(add_row { rep_count: 4 }
                   add_row {
                     match: "(position 0, length 3): A, A, A"
                     match: "(position 1, length 3): A, A, A"
                     match: "(position 2, length 2): A, A"
                   }
                   add_row { rep_count: 5 }
                   finalize { match: "(position 8, length 2): A, A" })pb"));

  AddTestCases(
      TestCaseTemplate("BoundMToNReluctant")
          .AddPatterns({"A{2,3}?", "A A{1,2}?", "A A A??", "A{2} A??",
                        "(A A)|(A A A)", "A{2} | A{3}"})
          .SetRowData(
              Concat(Repeat({{0}}, 4), Repeat({{}}, 4), Repeat({{0}}, 2)))
          .AddResult(
              {.overlapping_mode = false, .longest_match_mode = false},
              R"pb(add_row { rep_count: 4 }
                   add_row {
                     match: "(position 0, length 2): A, A"
                     match: "(position 2, length 2): A, A"
                   }
                   add_row { rep_count: 5 }
                   finalize { match: "(position 8, length 2): A, A" })pb")
          .AddResult(
              {.overlapping_mode = true, .longest_match_mode = false},
              R"pb(add_row { rep_count: 4 }
                   add_row {
                     match: "(position 0, length 2): A, A"
                     match: "(position 1, length 2): A, A"
                     match: "(position 2, length 2): A, A"
                   }
                   add_row { rep_count: 5 }
                   finalize { match: "(position 8, length 2): A, A" })pb")
          .AddImportedResults({.longest_match_mode = true}, "BoundMToNGreedy"));

  AddTestCases(TestCaseTemplate("BoundMToNComplexGreedy")
                   .AddPatterns({"((A|B){1,2}){1,2}",
                                 "(A|B){4}|(A|B){3}|(A|B){2}|(A|B){1}"})
                   .SetRowData(Concat(Repeat({{0}}, 4), Repeat({{1}}, 4),
                                      Repeat({{0, 1}}, 2)))
                   .AddNonOverlappingResult(
                       R"pb(add_row { rep_count: 10 }
                            finalize {
                              match: "(position 0, length 4): A, A, A, A"
                              match: "(position 4, length 4): B, B, B, B"
                              match: "(position 8, length 2): A, A"
                            })pb")
                   .AddOverlappingResult(
                       R"pb(add_row { rep_count: 10 }
                            finalize {
                              match: "(position 0, length 4): A, A, A, A"
                              match: "(position 1, length 4): A, A, A, B"
                              match: "(position 2, length 4): A, A, B, B"
                              match: "(position 3, length 4): A, B, B, B"
                              match: "(position 4, length 4): B, B, B, B"
                              match: "(position 5, length 4): B, B, B, A"
                              match: "(position 6, length 4): B, B, A, A"
                              match: "(position 7, length 3): B, A, A"
                              match: "(position 8, length 2): A, A"
                              match: "(position 9, length 1): A"
                            })pb"));
  AddTestCases(TestCaseTemplate("BoundMToNComplexReluctant")
                   .AddPatterns({"((A|B){1,2}?){1,2}?",
                                 "(A|B){1}|(A|B){2}|(A|B){3}|(A|B){4}"})
                   .SetRowData(Concat(Repeat({{0}}, 4), Repeat({{1}}, 4),
                                      Repeat({{0, 1}}, 2)))
                   .AddResult({.longest_match_mode = false},
                              R"pb(add_row { rep_count: 10 }
                                   finalize {
                                     match: "(position 0, length 1): A"
                                     match: "(position 1, length 1): A"
                                     match: "(position 2, length 1): A"
                                     match: "(position 3, length 1): A"
                                     match: "(position 4, length 1): B"
                                     match: "(position 5, length 1): B"
                                     match: "(position 6, length 1): B"
                                     match: "(position 7, length 1): B"
                                     match: "(position 8, length 1): A"
                                     match: "(position 9, length 1): A"
                                   })pb")
                   .AddImportedResults({.longest_match_mode = true},
                                       "BoundMToNComplexGreedy"));
}

void TestCaseBuilder::AddUnboundedQuantifierTestCases() {
  AddTestCases(
      TestCaseTemplate("UnboundedGreedyStar")
          .AddPatterns({"A*", "A* A*", "A* A*?", "A+ | ", "A{2,} | A+ |",
                        "(A*)*", "(A*)+", "(A+)*"})
          .SetRowData(
              Concat(Repeat({{0}}, 4), Repeat({{}}, 4), Repeat({{0}}, 2)))
          .AddNonOverlappingResult(
              R"pb(add_row { rep_count: 4 }
                   add_row {
                     match: "(position 0, length 4): A, A, A, A"
                     match: "(position 4, empty)"
                   }
                   add_row { match: "(position 5, empty)" }
                   add_row { match: "(position 6, empty)" }
                   add_row { match: "(position 7, empty)" }
                   add_row { rep_count: 2 }
                   finalize { match: "(position 8, length 2): A, A" })pb")
          .AddOverlappingResult(
              R"pb(
                add_row { rep_count: 4 }
                add_row {
                  match: "(position 0, length 4): A, A, A, A"
                  match: "(position 1, length 3): A, A, A"
                  match: "(position 2, length 2): A, A"
                  match: "(position 3, length 1): A"
                  match: "(position 4, empty)"
                }
                add_row { match: "(position 5, empty)" }
                add_row { match: "(position 6, empty)" }
                add_row { match: "(position 7, empty)" }
                add_row { rep_count: 2 }
                finalize {
                  match: "(position 8, length 2): A, A"
                  match: "(position 9, length 1): A"
                })pb"));
  AddTestCases(TestCaseTemplate("UnboundedReluctantStar")
                   .AddPatterns({"A*?", "A*? A*?", " | A+?", "A?? | A{2,}?",
                                 "(A*?)*?", "(A*?)+?", "(A+?)*?"})
                   .SetRowData(Concat(Repeat({{0}}, 4), Repeat({{}}, 4),
                                      Repeat({{0}}, 2)))
                   .AddResult({.longest_match_mode = false},
                              R"pb(add_row { rep_count: 4 }
                                   add_row {
                                     match: "(position 0, empty)"
                                     match: "(position 1, empty)"
                                     match: "(position 2, empty)"
                                     match: "(position 3, empty)"
                                     match: "(position 4, empty)"
                                   }
                                   add_row { match: "(position 5, empty)" }
                                   add_row { match: "(position 6, empty)" }
                                   add_row { match: "(position 7, empty)" }
                                   add_row { rep_count: 2 }
                                   finalize {
                                     match: "(position 8, empty)"
                                     match: "(position 9, empty)"
                                   })pb")
                   .AddImportedResults({.longest_match_mode = true},
                                       "UnboundedGreedyStar"));
  AddTestCases(
      TestCaseTemplate("UnboundedGreedyPlus")
          .AddPatterns({"A+", "A+ A*", "A* A+", "A+ A*?", "A (A*)", "(A+)+",
                        "A{2,} | A+", "A{1,}"})
          .SetRowData(
              Concat(Repeat({{0}}, 4), Repeat({{}}, 4), Repeat({{0}}, 2)))
          .AddNonOverlappingResult(
              R"pb(add_row { rep_count: 4 }
                   add_row { match: "(position 0, length 4): A, A, A, A" }
                   add_row { rep_count: 5 }
                   finalize { match: "(position 8, length 2): A, A" })pb")
          .AddOverlappingResult(
              R"pb(add_row { rep_count: 4 }
                   add_row {
                     match: "(position 0, length 4): A, A, A, A"
                     match: "(position 1, length 3): A, A, A"
                     match: "(position 2, length 2): A, A"
                     match: "(position 3, length 1): A"
                   }
                   add_row { rep_count: 5 }
                   finalize {
                     match: "(position 8, length 2): A, A"
                     match: "(position 9, length 1): A"
                   })pb"));
  AddTestCases(
      TestCaseTemplate("UnboundedReluctantPlus")
          .AddPatterns({"A+?", "A+? A*?", "A*? A+?", "A+? A*?", "A (A*?)",
                        "(A+?)+?", "A+? | A{2,}?", "A{1,}?"})
          .SetRowData(
              Concat(Repeat({{0}}, 4), Repeat({{}}, 4), Repeat({{0}}, 2)))
          .AddResult({.longest_match_mode = false},
                     R"pb(add_row { rep_count: 4 }
                          add_row {
                            match: "(position 0, length 1): A"
                            match: "(position 1, length 1): A"
                            match: "(position 2, length 1): A"
                            match: "(position 3, length 1): A"
                          }
                          add_row { rep_count: 5 }
                          finalize {
                            match: "(position 8, length 1): A"
                            match: "(position 9, length 1): A"
                          })pb")
          .AddImportedResults({.longest_match_mode = true},
                              "UnboundedGreedyPlus"));
  AddTestCases(
      TestCaseTemplate("UnboundedGreedyThreeOrMore")
          .AddPatterns({"A{3,}", "A A{2,}", "A A A+", "A A A A*"})
          .SetRowData(
              Concat(Repeat({{0}}, 4), Repeat({{}}, 4), Repeat({{0}}, 2)))
          .AddNonOverlappingResult(
              R"pb(add_row { rep_count: 4 }
                   add_row { match: "(position 0, length 4): A, A, A, A" }
                   add_row { rep_count: 5 }
                   finalize {})pb")
          .AddOverlappingResult(
              R"pb(add_row { rep_count: 4 }
                   add_row {
                     match: "(position 0, length 4): A, A, A, A"
                     match: "(position 1, length 3): A, A, A"
                   }
                   add_row { rep_count: 5 }
                   finalize {})pb"));
  AddTestCases(
      TestCaseTemplate("UnboundedReluctantThreeOrMore")
          .AddPatterns({"A{3,}?", "A A{2,}?", "A A A+?", "A A A A*?"})
          .SetRowData(
              Concat(Repeat({{0}}, 4), Repeat({{}}, 4), Repeat({{0}}, 2)))
          .AddResult({.overlapping_mode = false, .longest_match_mode = false},
                     R"pb(add_row { rep_count: 4 }
                          add_row { match: "(position 0, length 3): A, A, A" }
                          add_row { rep_count: 5 }
                          finalize {})pb")
          .AddResult({.overlapping_mode = true, .longest_match_mode = false},
                     R"pb(add_row { rep_count: 4 }
                          add_row {
                            match: "(position 0, length 3): A, A, A"
                            match: "(position 1, length 3): A, A, A"
                          }
                          add_row { rep_count: 5 }
                          finalize {})pb")
          .AddImportedResults({.longest_match_mode = true},
                              "UnboundedGreedyThreeOrMore"));

  AddTestCases(
      TestCaseTemplate("UnboundedComplex")
          .AddPatterns({"A* B{2,} C", "(A*)* B B+ C", "A* B (B+)+ C"})
          .SetRowData(Concat(Repeat({{0}}, 4), Repeat({{1}}, 4),
                             Repeat({{0, 1, 2}}, 2)))
          .AddNonOverlappingResult(
              R"pb(add_row { rep_count: 10 }
                   finalize {
                     match: "(position 0, length 10): A, A, A, A, B, B, B, B, B, C"
                   })pb")
          .AddOverlappingResult(
              R"pb(add_row { rep_count: 10 }
                   finalize {
                     match: "(position 0, length 10): A, A, A, A, B, B, B, B, B, C"
                     match: "(position 1, length 9): A, A, A, B, B, B, B, B, C"
                     match: "(position 2, length 8): A, A, B, B, B, B, B, C"
                     match: "(position 3, length 7): A, B, B, B, B, B, C"
                     match: "(position 4, length 6): B, B, B, B, B, C"
                     match: "(position 5, length 5): B, B, B, B, C"
                     match: "(position 6, length 4): B, B, B, C"
                     match: "(position 7, length 3): B, B, C"
                   })pb"));

  AddTestCases(
      TestCaseTemplate("GreedyMatchIsNotLongestMatch")
          .AddPattern("A B+ (C D)?")
          .SetRowData({{0}, {1}, {1}, {1, 2}, {3}})
          .AddResult({.longest_match_mode = false},
                     R"pb(add_row { rep_count: 5 }
                          finalize {
                            match: "(position 0, length 4): A, B, B, B"
                          })pb")
          .AddResult({.longest_match_mode = true},
                     R"pb(add_row { rep_count: 5 }
                          finalize {
                            match: "(position 0, length 5): A, B, B, C, D"
                          })pb"));
}

void TestCaseBuilder::AddQueryParameterTestCases() {
  std::vector<std::vector<int>> input =
      Concat(Repeat({{0}}, 6), {{}, {0}, {}, {0}, {0}, {0}, {}, {0}, {0}});
  TestResult non_overlapping_expected_result =
      R"pb(add_row { rep_count: 6 }
           add_row {
             match: "(position 0, length 4): A, A, A, A"
             match: "(position 4, length 2): A, A"
           }
           add_row { rep_count: 5 }
           add_row { match: "(position 9, length 3): A, A, A" }
           add_row { rep_count: 2 }
           finalize { match: "(position 13, length 2): A, A" })pb";
  TestResult overlapping_expected_result =
      R"pb(add_row { rep_count: 6 }
           add_row {
             match: "(position 0, length 4): A, A, A, A"
             match: "(position 1, length 4): A, A, A, A"
             match: "(position 2, length 4): A, A, A, A"
             match: "(position 3, length 3): A, A, A"
             match: "(position 4, length 2): A, A"
           }
           add_row { rep_count: 5 }
           add_row {
             match: "(position 9, length 3): A, A, A"
             match: "(position 10, length 2): A, A"
           }
           add_row { rep_count: 2 }
           finalize { match: "(position 13, length 2): A, A" })pb";

  AddTestCases(TestCaseTemplate("NamedParamsAsQuantifierBounds")
                   .AddPattern("A{@foo, @bar}")
                   .SetRowData(input)
                   .SetConfig({.query_parameters =
                                   QueryParameterData::CreateNamed()
                                       .AddNamedParam("foo", values::Int64(2))
                                       .AddNamedParam("bar", values::Int64(4))})
                   .AddNonOverlappingResult(non_overlapping_expected_result)
                   .AddOverlappingResult(overlapping_expected_result));

  AddTestCases(TestCaseTemplate("PositionalParamsAsQuantifierBounds")
                   .AddPattern("A{?, ?}")
                   .SetRowData(input)
                   .SetConfig({.query_parameters =
                                   QueryParameterData::CreatePositional()
                                       .AddPositionalParam(values::Int64(2))
                                       .AddPositionalParam(values::Int64(4))})
                   .AddNonOverlappingResult(non_overlapping_expected_result)
                   .AddOverlappingResult(overlapping_expected_result));

  AddTestCases(
      TestCaseTemplate("NullQuantifierBound")
          .AddPattern("A{@foo, @bar}")
          .SetRowData({})
          .SetConfig({.query_parameters =
                          QueryParameterData::CreateNamed()
                              .AddNamedParam("foo", values::NullInt64())
                              .AddNamedParam("bar", values::Int64(4))})
          .AddResult(absl::StatusCode::kOutOfRange));

  AddTestCases(TestCaseTemplate("NegativeQuantifierBound")
                   .AddPattern("A{@foo, @bar}")
                   .SetRowData({})
                   .SetConfig({.query_parameters =
                                   QueryParameterData::CreateNamed()
                                       .AddNamedParam("foo", values::Int64(-2))
                                       .AddNamedParam("bar", values::Int64(4))})
                   .AddResult(absl::StatusCode::kOutOfRange));

  AddTestCases(TestCaseTemplate("LowerBoundGreaterThanUpperBound")
                   .AddPattern("A{@foo, @bar}")
                   .SetRowData({})
                   .SetConfig({.query_parameters =
                                   QueryParameterData::CreateNamed()
                                       .AddNamedParam("foo", values::Int64(4))
                                       .AddNamedParam("bar", values::Int64(2))})
                   .AddResult(absl::StatusCode::kOutOfRange));

  AddTestCases(
      TestCaseTemplate("StarQuantifierNestedInBoundedQuantifier")
          .AddPattern("((A*){,6}){10}")
          .SetRowData({{0}, {0}, {}, {0}, {0}})
          .AddNonOverlappingResult(
              R"pb(add_row { rep_count: 2 }
                   add_row {
                     match: "(position 0, length 2): A, A"
                     match: "(position 2, empty)"
                   }
                   add_row { rep_count: 2 }
                   finalize { match: "(position 3, length 2): A, A" })pb")
          .AddOverlappingResult(
              R"pb(add_row { rep_count: 2 }
                   add_row {
                     match: "(position 0, length 2): A, A"
                     match: "(position 1, length 1): A"
                     match: "(position 2, empty)"
                   }
                   add_row { rep_count: 2 }
                   finalize {
                     match: "(position 3, length 2): A, A"
                     match: "(position 4, length 1): A"
                   })pb"));
}

void TestCaseBuilder::AddAnchorTestCases() {
  AddTestCases(
      TestCaseTemplate("HeadAnchorBasic")
          .AddPatterns({"^A A", "^ ^ A A", "^ ^ ^ A A"})
          .SetRowData({{0}, {0}, {0}, {0}})
          .AddResult(R"pb(add_row { rep_count: 2 }
                          add_row { match: "(position 0, length 2): A, A" }
                          add_row {}
                          finalize {})pb"));
  AddTestCases(TestCaseTemplate("TailAnchorBasic")
                   .AddPatterns({"A A$", "A A $ $", "A A $ $ $"})
                   .SetRowData({{0}, {0}, {0}, {0}})
                   .AddResult(R"pb(add_row { rep_count: 4 }
                                   finalize {
                                     match: "(position 2, length 2): A, A"
                                   })pb"));

  AddTestCases(TestCaseTemplate("HeadAnchorEmptyMatch")
                   .AddPatterns({"(A?) ^ (A?)", "(A?) ^ () () (A?)",
                                 "(A?) () () ^ (A?)", "() ^ () ^ (A?)"})
                   .SetRowData({{}, {}, {}, {}})
                   .AddResult(R"pb(add_row { match: "(position 0, empty)" }
                                   add_row { rep_count: 3 }
                                   finalize {})pb"));

  AddTestCases(TestCaseTemplate("TailAnchorNoMatchOnEmpty")
                   .AddPatterns({"(A?) $ (A?)", "(A?) $ () () (A?)",
                                 "(A?) () () $ (A?)", "() $ () $ (A?)"})
                   .SetRowData({{}, {}, {}, {}})
                   // No match is expected because an empty match is not allowed
                   // after the last row.
                   .AddResult(R"pb(add_row { rep_count: 4 }
                                   finalize {})pb"));

  AddTestCases(TestCaseTemplate("NoEmptyMatchOnEmptyData")
                   .AddPatterns({"A?", "(A?) ^ (A?)", "(A?) ^ () () (A?)",
                                 "(A?) () () ^ (A?)", "() ^ () ^ (A?)",
                                 "(A?) $", "(A?)$$", "^(A?)$"})
                   .SetRowData({})
                   .AddResult(R"pb(finalize {})pb"));

  AddTestCases(TestCaseTemplate("AnchorsForceMatchOfEntirePartition")
                   // Normally, the reluctant quantifier would favor the
                   // shortest possible matches (empty), but the anchors force
                   // the match to cover everything.
                   .AddPatterns({"^ A*? $"})
                   .SetRowData({{0}, {0}, {0}})
                   .AddResult(R"pb(add_row { rep_count: 3 }
                                   finalize {
                                     match: "(position 0, length 3): A, A, A"
                                   })pb"));

  AddTestCases(
      TestCaseTemplate("TailAnchorOnStarForcesMatchToEnd")
          // The tail anchor effectively forces relucant * to behave like a
          // greedy *.
          .AddPatterns({"A*? $"})
          .SetRowData({{0}, {0}, {0}})
          .AddNonOverlappingResult(
              R"pb(add_row { rep_count: 3 }
                   finalize { match: "(position 0, length 3): A, A, A" })pb")
          .AddOverlappingResult(
              R"pb(add_row { rep_count: 3 }
                   finalize {
                     match: "(position 0, length 3): A, A, A"
                     match: "(position 1, length 2): A, A"
                     match: "(position 2, length 1): A"
                   })pb"));

  AddTestCases(TestCaseTemplate("AnchorsInAlternation")
                   .AddPatterns({"(^A) | (B$) | C"})
                   .SetRowData(Repeat({{0, 1, 2}}, 4))
                   .AddResult(R"pb(add_row { rep_count: 4 }
                                   finalize {
                                     match: "(position 0, length 1): A"
                                     match: "(position 1, length 1): C"
                                     match: "(position 2, length 1): C"
                                     match: "(position 3, length 1): B"
                                   })pb"));

  AddTestCases(
      TestCaseTemplate("AnchorsInQuantifierOperand")
          .AddPatterns({"(^A)+ B* (C$)+", "((^A)+)+ B* ((C$)+)+",
                        "((^A)+){1,8} B* ((C$)+){1,8}",
                        "(((^A)+){1,8}){1,10} B* (((C$)+){1,8}){1,10}"})
          .SetRowData(Repeat({{0, 1, 2}}, 6))
          .AddResult(R"pb(add_row { rep_count: 6 }
                          finalize {
                            match: "(position 0, length 6): A, B, B, B, B, C"
                          })pb"));

  AddTestCases(TestCaseTemplate("OptionalAnchorIsNoAnchor")
                   .AddPatterns({"(^)? A B ($)?", "(^|) A B ($|)"})
                   .SetRowData(Repeat({{0, 1}}, 4))
                   .AddNonOverlappingResult(
                       R"pb(add_row { rep_count: 4 }
                            finalize {
                              match: "(position 0, length 2): A, B"
                              match: "(position 2, length 2): A, B"
                            })pb")
                   .AddOverlappingResult(
                       R"pb(add_row { rep_count: 4 }
                            finalize {
                              match: "(position 0, length 2): A, B"
                              match: "(position 1, length 2): A, B"
                              match: "(position 2, length 2): A, B"
                            })pb"));

  // Note: This case leads to an epsilon-removed NFA with an unreachable final
  // state, so is good to test to make sure nothing strange happens. There
  // should not be any matches.
  AddTestCases(TestCaseTemplate("HeadAndTailAnchorTogetherAreUnsatisfiable")
                   .AddPatterns({"^$ A", "$^ A"})
                   .SetRowData(Repeat({{0}}, 3))
                   .AddResult(R"pb(add_row { rep_count: 3 }
                                   finalize {})pb"));
  AddTestCases(TestCaseTemplate("HeadAndTailAnchorTogetherAreUnsatisfiable2")
                   .AddPatterns({"A ^$", "A $^"})
                   .SetRowData(Repeat({{0}}, 3))
                   .AddResult(R"pb(add_row { rep_count: 3 }
                                   finalize {})pb"));
  AddTestCases(TestCaseTemplate("AnchorsInMiddleOfPattern")
                   .AddPatterns({"A ^ A", "A $ A"})
                   .SetRowData(Repeat({{0}}, 3))
                   .AddResult(R"pb(add_row { rep_count: 3 }
                                   finalize {})pb"));

  AddTestCases(
      TestCaseTemplate("HeadAnchorPrecedingComplexPattern")
          .AddPatterns({"^ A ((B C){2,}? D){,4}"})
          .SetRowData(Concat(Repeat({{0, 1, 2, 3}}, 14), Repeat({{3}}, 2)))
          .AddResult(
              {.longest_match_mode = false},
              R"pb(add_row { rep_count: 15 }
                   add_row {
                     match: "(position 0, length 11): A, B, C, B, C, D, B, C, B, C, D"
                   }
                   finalize {})pb")
          .AddResult(
              {.longest_match_mode = true},
              R"pb(add_row { rep_count: 15 }
                   add_row {
                     match: "(position 0, length 15): A, B, C, B, C, D, B, C, B, C, B, C, B, C, D"

                   }
                   finalize {})pb"));
  AddTestCases(
      TestCaseTemplate("TailAnchorFollowingComplexPattern")
          .AddPatterns({"A ((B C){2,}? D){,4} $"})
          .SetRowData(Repeat({{0, 1, 2, 3}}, 15))
          .AddNonOverlappingResult(
              R"pb(add_row { rep_count: 15 }
                   finalize {
                     match: "(position 0, length 15): A, B, C, B, C, D, B, C, B, C, B, C, B, C, D"
                   })pb")
          .AddOverlappingResult(
              R"pb(add_row { rep_count: 15 }
                   finalize {
                     match: "(position 0, length 15): A, B, C, B, C, D, B, C, B, C, B, C, B, C, D"
                     match: "(position 1, length 14): A, B, C, B, C, B, C, B, C, B, C, B, C, D"
                     match: "(position 2, length 13): A, B, C, B, C, D, B, C, B, C, B, C, D"
                     match: "(position 3, length 12): A, B, C, B, C, B, C, B, C, B, C, D"
                     match: "(position 4, length 11): A, B, C, B, C, D, B, C, B, C, D"
                     match: "(position 5, length 10): A, B, C, B, C, B, C, B, C, D"
                     match: "(position 7, length 8): A, B, C, B, C, B, C, D"
                     match: "(position 9, length 6): A, B, C, B, C, D"
                     match: "(position 14, length 1): A"
                   })pb"));

  // This pattern matches a single character in all cases, but the relative
  // precedence between A vs. B vs. empty depends on whether we are at the
  // start or end of the partition.
  AddTestCases(TestCaseTemplate("AnchorChangesPrecedence")
                   .AddPatterns({"^|B$|(A?)"})
                   .SetRowData(Repeat({{0, 1}}, 4))
                   .AddResult({.longest_match_mode = false},
                              R"pb(add_row { rep_count: 4 }
                                   finalize {
                                     match: "(position 0, empty)"
                                     match: "(position 1, length 1): A"
                                     match: "(position 2, length 1): A"
                                     match: "(position 3, length 1): B"
                                   })pb")
                   .AddResult({.longest_match_mode = true},
                              R"pb(add_row { rep_count: 4 }
                                   finalize {
                                     match: "(position 0, length 1): A"
                                     match: "(position 1, length 1): A"
                                     match: "(position 2, length 1): A"
                                     match: "(position 3, length 1): B"
                                   })pb"));
}

class CompiledPatternTest : public ::testing::TestWithParam<TestCase> {};

TEST_P(CompiledPatternTest, Matching) {
  const TestCase& test_case = GetParam();

  absl::StatusOr<MatchPartitionResultProto> actual =
      Match(test_case.pattern, test_case.row_data, test_case.config);

  const TestResult& test_result = test_case.expected_result;

  if (std::holds_alternative<std::string>(test_result)) {
    EXPECT_THAT(actual,
                IsOkAndHolds(EqualsProto(std::get<std::string>(test_result))));
  } else {
    EXPECT_THAT(actual.status(),
                StatusIs(std::get<absl::StatusCode>(test_result)));
  }
}

INSTANTIATE_TEST_SUITE_P(
    CompiledPatternTest, CompiledPatternTest,
    ValuesIn(TestCaseBuilder().MakeTestCases()),
    [](const TestParamInfo<CompiledPatternTest::ParamType>& info) {
      return info.param.name;
    });

// TODO: Make this a parameterized test, so it can be tested
// against each algorithm impl.
TEST(CompiledPatternTest, AddRowAfterFinalize) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<MatchPartition> partition,
                       CreateMatchPartition("A B B C"));
  GOOGLESQL_ASSERT_OK(partition->Finalize());
  EXPECT_THAT(partition->AddRow({false, false, false}),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(CompiledPatternTest, WrongNumberOfPatternVarsPassedToAddRow) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<MatchPartition> partition,
                       CreateMatchPartition("A B B C"));
  EXPECT_THAT(partition->AddRow({true, false}),
              StatusIs(absl::StatusCode::kInternal));
  EXPECT_THAT(partition->AddRow({true, false, false, true}),
              StatusIs(absl::StatusCode::kInternal));
}

class LongestMatchModeAsQueryParamTest
    : public ::testing::TestWithParam<std::tuple<bool, Value, bool>> {
 public:
  // Returns true if testing named parameters, false for positional parameters.
  bool NamedParams() const { return std::get<0>(GetParam()); }

  // Returns true if the query parameter used to determine longest match is
  // true.
  Value ParamValue() const { return std::get<1>(GetParam()); }

  // Returns true if parameter values should be deferred until runtime.
  bool DeferParams() const { return std::get<2>(GetParam()); }
};

TEST_P(LongestMatchModeAsQueryParamTest, LongestMatchModeQueryParamTest) {
  QueryParameterData query_params;
  if (NamedParams()) {
    query_params =
        QueryParameterData::CreateNamed().AddNamedParam("p", ParamValue());
  } else {
    query_params =
        QueryParameterData::CreatePositional().AddPositionalParam(ParamValue());
  }
  absl::StatusOr<MatchPartitionResultProto> result =
      Match("A | (B C)", {Repeat({{0, 1, 2}}, 3)},
            {
                .query_parameters = query_params,
                .defer_query_parameters = DeferParams(),
                .longest_match_mode_sql = NamedParams() ? "@p" : "?",
                .round_trip_serialization = !DeferParams(),
            });

  TestResult expected_result;
  if (ParamValue().is_null()) {
    expected_result = absl::StatusCode::kOutOfRange;
  } else if (ParamValue().bool_value()) {
    expected_result =
        R"pb(add_row { rep_count: 3 }
             finalize {
               match: "(position 0, length 2): B, C"
               match: "(position 2, length 1): A"
             })pb";
  } else {
    expected_result = R"pb(
      add_row { rep_count: 3 }
      finalize {
        match: "(position 0, length 1): A"
        match: "(position 1, length 1): A"
        match: "(position 2, length 1): A"
      })pb";
  }
  if (std::holds_alternative<std::string>(expected_result)) {
    EXPECT_THAT(
        result,
        IsOkAndHolds(EqualsProto(std::get<std::string>(expected_result))));
  } else {
    EXPECT_THAT(result, StatusIs(std::get<absl::StatusCode>(expected_result)));
  }
}

INSTANTIATE_TEST_SUITE_P(
    LongestMatchModeAsQueryParamTest, LongestMatchModeAsQueryParamTest,
    ::testing::Combine(::testing::Bool(),  // named vs. positional
                       ::testing::Values(values::Bool(false),
                                         values::Bool(true),
                                         values::NullBool()),
                       ::testing::Bool()  // deferred vs. not deferred
                       ),
    [](const TestParamInfo<LongestMatchModeAsQueryParamTest::ParamType>& info) {
      std::string test_suffix;
      absl::StrAppend(&test_suffix,
                      std::get<0>(info.param) ? "Named" : "Positional");
      absl::StrAppend(&test_suffix, std::get<1>(info.param).DebugString());
      absl::StrAppend(&test_suffix,
                      std::get<2>(info.param) ? "Deferred" : "NotDeferred");
      return test_suffix;
    });

}  // namespace
}  // namespace googlesql::functions::match_recognize
