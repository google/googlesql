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

// Test repetitive queries and bisect to find limits of engine behaviour

#include "googlesql/compliance/depth_limit_detector_test_cases.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "googlesql/compliance/depth_limit_detector_internal.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "gtest/gtest.h"
#include "absl/flags/flag.h"
#include "absl/functional/any_invocable.h"
#include "absl/functional/function_ref.h"
#include "googlesql/base/check.h"
#include "absl/log/log.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"

// Small limit as we are experimenting with these tests.
// This customary limit is used for manually controlling the compliance tests.
// Consider passing the runtime control structure instead of using this flag
// in other tests.
ABSL_FLAG(std::size_t, depth_limit_detector_max_sql_bytes, 1000,
          "Do not expand recursive test queries beyond this number of bytes");
ABSL_FLAG(absl::Duration, depth_limit_detector_max_seed_probing_duration,
          absl::Seconds(10),
          "Approximate length of time to search for biggest nested query test "
          "case for test seeds");
ABSL_FLAG(absl::Duration, depth_limit_detector_total_seed_probing_duration,
          absl::Minutes(2),
          "Approximate total length of time to search for biggest nested query "
          "test seeds");

namespace googlesql {

std::ostream& operator<<(std::ostream& os,
                         const DepthLimitDetectorReturnCondition& condition) {
  return os << "depth=[" << condition.starting_depth << ","
            << condition.ending_depth << "],status=" << condition.return_status;
}
std::ostream& operator<<(std::ostream& os,
                         const DepthLimitDetectorTestResult& test_result) {
  os << "depth_limit_detector_test_name="
     << test_result.depth_limit_test_case_name << ",conditions={";
  for (const auto& condition :
       test_result.depth_limit_detector_return_conditions) {
    os << condition << ",";
  }
  return os << "}";
}
std::ostream& operator<<(std::ostream& os,
                         const DepthLimitDetectorTestCase& test_case) {
  return os << test_case.depth_limit_test_case_name;
}

namespace {

// Output a template to a string, performing different actions depending
// on the type of the template.
// Currently supports raw strings, incrementing numbers for generation of unique
// names and repeating other templates.
void InstantiateDepthLimitDetectorTemplate(absl::string_view part,
                                           std::ostream& output, int) {
  output << part;
}
void InstantiateDepthLimitDetectorTemplate(
    depth_limit_detector_internal::DepthLimitDetectorDepthNumber,
    std::ostream& output, int depth) {
  output << depth;
}

void InstantiateDepthLimitDetectorTemplate(
    const depth_limit_detector_internal::DepthLimitDetectorRepeatedTemplate&
        depth_limit_template,
    std::ostream& output, int depth) {
  for (int i = 1; i <= depth; ++i) {
    for (auto const& part : depth_limit_template) {
      std::visit(
          [&](auto const& typed_part) {
            InstantiateDepthLimitDetectorTemplate(typed_part, output, i);
          },
          part);
    }
  }
}
void InstantiateDepthLimitDetectorTemplate(
    const DepthLimitDetectorTemplate& depth_limit_template,
    std::ostream& output, int depth) {
  for (auto const& part : depth_limit_template) {
    std::visit(
        [&](auto const& typed_part) {
          InstantiateDepthLimitDetectorTemplate(typed_part, output, depth);
        },
        part);
  }
}

// Quickly compare statuses without considering complex payloads.
inline bool StatusCodeAndMessageEqual(absl::Status a, absl::Status b) {
  return a.code() == b.code() && a.message() == b.message();
}

// Add a new test result to those known, either enlarging a range or
// creating a whole new one.
//
// This function can only handle errors that appear once as a single contiguous
// range in the output domain.
// For example, if the tested function returns AAAABBB as depth increases, so
// f(1) = A, f(2) = A, f(3) = A, f(4), = A, f(5) = B, etc. this integration
// called iteratively will create {A:[1-4], B:[5-7]}.
// However, if a function has a pattern like AAABAAA then there is guarantee
// that we would notice the B in the middle. If we do, we fail a ABSL_CHECK.
void IntegrateTestResult(DepthLimitDetectorTestResult* result, int depth,
                         absl::Status status) {
  auto insertion_point = std::lower_bound(
      result->depth_limit_detector_return_conditions.begin(),
      result->depth_limit_detector_return_conditions.end(), depth,
      [](const DepthLimitDetectorReturnCondition& i, int depth) {
        return i.ending_depth < depth;
      });
  if (insertion_point !=
      result->depth_limit_detector_return_conditions.begin()) {
    auto preceding_point = insertion_point - 1;
    if (insertion_point ==
            result->depth_limit_detector_return_conditions.end() ||
        !StatusCodeAndMessageEqual(insertion_point->return_status, status)) {
      if (StatusCodeAndMessageEqual(preceding_point->return_status, status)) {
        insertion_point = preceding_point;
      } else {
        ABSL_CHECK(depth > preceding_point->ending_depth)  // Crash OK
            << "Disection algorithm cannot split a range "
            << "depth=" << depth << " status=" << status
            << " insertion_point=" << *insertion_point
            << " preceding_point=" << *preceding_point;
      }
    }
  }
  if (insertion_point != result->depth_limit_detector_return_conditions.end() &&
      StatusCodeAndMessageEqual(insertion_point->return_status, status)) {
    insertion_point->starting_depth =
        std::min(insertion_point->starting_depth, depth);
    insertion_point->ending_depth =
        std::max(insertion_point->ending_depth, depth);
    auto following_point = insertion_point + 1;
    if (following_point !=
        result->depth_limit_detector_return_conditions.end()) {
      ABSL_CHECK(following_point->starting_depth > depth)  // Crash OK
          << "Disection algorithm cannot overlap a range at the beginning "
          << "depth=" << depth << " status=" << status
          << " insertion_point=" << *insertion_point
          << " following_point=" << *following_point;
    }
    return;
  }

  result->depth_limit_detector_return_conditions.insert(
      insertion_point,
      DepthLimitDetectorReturnCondition({.starting_depth = depth,
                                         .ending_depth = depth,
                                         .return_status = status}));
}

}  // namespace

std::string DepthLimitDetectorTemplateToString(
    const DepthLimitDetectorTemplate& depth_limit_template, int depth) {
  std::ostringstream output;
  InstantiateDepthLimitDetectorTemplate(depth_limit_template, output, depth);
  return output.str();
}
std::string DepthLimitDetectorTemplateToString(
    const DepthLimitDetectorTestCase& depth_case, int depth) {
  return DepthLimitDetectorTemplateToString(depth_case.depth_limit_template,
                                            depth);
}

LanguageOptions DepthLimitDetectorTestCaseLanguageOptions(
    const DepthLimitDetectorTestCase& depth_case) {
  googlesql::LanguageOptions language_options;
  language_options.SetEnabledLanguageFeatures(
      depth_case.depth_limit_required_features);
  language_options.SetSupportsAllStatementKinds();
  return language_options;
}

DepthLimitDetectorTestResult RunDepthLimitDetectorTestCase(
    DepthLimitDetectorTestCase const& depth_limit_case,
    absl::FunctionRef<absl::Status(absl::string_view)> test_driver_function) {
  DepthLimitDetectorRuntimeControl runtime_control;
  runtime_control.max_sql_bytes =
      absl::GetFlag(FLAGS_depth_limit_detector_max_sql_bytes);
  return RunDepthLimitDetectorTestCase(
      depth_limit_case, std::move(test_driver_function), runtime_control);
}

// Instantiates a query template at many depths in order to expose complex
// resource exhausted conditions (e.g. stack overflows).
DepthLimitDetectorTestResult RunDepthLimitDetectorTestCase(
    DepthLimitDetectorTestCase const& depth_limit_case,
    absl::FunctionRef<absl::Status(absl::string_view)> test_driver_function,
    const DepthLimitDetectorRuntimeControl& runtime_control) {
  testing::Test::RecordProperty(
      "depth_limit_detector_test_case_name",
      std::string(depth_limit_case.depth_limit_test_case_name));

  DepthLimitDetectorTestResult result;
  result.depth_limit_test_case_name =
      depth_limit_case.depth_limit_test_case_name;

  auto TryDepth = [&](absl::string_view sql, int depth) {
    auto status = test_driver_function(sql);
    IntegrateTestResult(&result, depth, status);
  };

  absl::Time start_probe = absl::Now();
  // Increase depth by 5% each iteration.
  for (int depth = 1; depth < depth_limit_case.depth_limit_max_depth;
       depth = std::max(depth + 1, (depth * 21) / 20)) {
    absl::Time start_try = absl::Now();
    auto sql = DepthLimitDetectorTemplateToString(
        depth_limit_case.depth_limit_template, depth);
    if (sql.size() > runtime_control.max_sql_bytes) {
      break;
    }
    TryDepth(sql, depth);
    absl::Duration try_duration = absl::Now() - start_try;
    absl::Duration probe_duration = absl::Now() - start_probe;
    if (try_duration + probe_duration >= runtime_control.max_probing_duration) {
      ABSL_LOG(INFO) << "DepthLimitDetector cutting short as already spent "
                << probe_duration << " and next try at least " << try_duration
                << " on " << depth_limit_case;
      break;
    }
    if (try_duration > absl::Seconds(10)) {
      ABSL_LOG(INFO) << "DepthLimitDetector took " << try_duration
                << " probing depth " << depth << " with " << sql.size()
                << " bytes, spent " << probe_duration << " total on "
                << depth_limit_case;
    }
  }

  bool continued_disection;
  int last_ending_depth = 0;
  do {
    continued_disection = false;
    for (DepthLimitDetectorReturnCondition const& condition :
         result.depth_limit_detector_return_conditions) {
      if (condition.starting_depth > last_ending_depth + 1) {
        continued_disection = true;
        int depth = (condition.starting_depth + 1 + last_ending_depth) / 2;
        TryDepth(DepthLimitDetectorTemplateToString(
                     depth_limit_case.depth_limit_template, depth),
                 depth);
        break;
      }
      last_ending_depth = condition.ending_depth;
    }
  } while (continued_disection);

  return result;
}

absl::AnyInvocable<std::vector<std::tuple<std::string>>() const>
DepthLimitDetectorSeeds(
    absl::AnyInvocable<absl::Status(absl::string_view) const>
        test_driver_function) {
  return [test_driver_function = std::move(test_driver_function)]() {
    std::vector<std::tuple<std::string>> seeds;

    absl::Time start_seeding_time = absl::Now();
    absl::Time target_ending_time =
        start_seeding_time +
        absl::GetFlag(FLAGS_depth_limit_detector_total_seed_probing_duration);
    int64_t remaining_cases = AllDepthLimitDetectorTestCases().size();
    for (const googlesql::DepthLimitDetectorTestCase& test_case :
         AllDepthLimitDetectorTestCases()) {
      DepthLimitDetectorRuntimeControl control;
      control.max_probing_duration = std::min(
          (target_ending_time - absl::Now()) / remaining_cases,
          absl::GetFlag(FLAGS_depth_limit_detector_max_seed_probing_duration));
      DepthLimitDetectorTestResult result = RunDepthLimitDetectorTestCase(
          test_case, test_driver_function, control);
      ABSL_LOG(INFO) << "DepthLimitDetectorSeeds "
                << result.depth_limit_test_case_name << " first condition "
                << result.depth_limit_detector_return_conditions[0];
      for (const DepthLimitDetectorReturnCondition& cond :
           result.depth_limit_detector_return_conditions) {
        seeds.push_back(
            DepthLimitDetectorTemplateToString(test_case, cond.starting_depth));
        seeds.push_back(
            DepthLimitDetectorTemplateToString(test_case, cond.ending_depth));
      }
      remaining_cases--;
    }
    ABSL_LOG(INFO) << "DepthLimitDetector seeds finished in "
              << absl::Now() - start_seeding_time << " with "
              << (target_ending_time - absl::Now()) << " to spare";
    return seeds;
  };
}

absl::Span<const std::reference_wrapper<const DepthLimitDetectorTestCase>>
AllDepthLimitDetectorTestCases() {
  using R = depth_limit_detector_internal::DepthLimitDetectorRepeatedTemplate;
  using N = depth_limit_detector_internal::DepthLimitDetectorDepthNumber;
  static std::vector<DepthLimitDetectorTestCase>* cases =
      new std::vector<DepthLimitDetectorTestCase>{
          {
              .depth_limit_test_case_name = "recursive_protobuf_field",
              .depth_limit_template =
                  {"WITH t AS (SELECT CAST(NULL AS googlesql_test.RecursivePB) "
                   "AS recursive_pb) SELECT ",
                   R({"recursive_pb."}), "int64_val FROM t"},
          },
          {.depth_limit_test_case_name = "recursive_repeated_protobuf_field",
           .depth_limit_template =
               {"WITH t AS (SELECT CAST(NULL AS googlesql_test.RecursivePB) "
                "AS pb) SELECT pb.",
                R({"repeated_recursive_pb[0]."}), "int64_val FROM t"},
           .depth_limit_required_features =
               {
                   LanguageFeature::FEATURE_BARE_ARRAY_ACCESS,
               }},
          {
              .depth_limit_test_case_name = "nested_ors",
              .depth_limit_template =
                  {"WITH t AS (SELECT 'x' AS f), u AS (SELECT * FROM t WHERE",
                   R({"("}), "TRUE", R({" OR ((f) = ('x')))"}),
                   ") SELECT * FROM u"},
          },
          {.depth_limit_test_case_name = "repeated_ors",
           .depth_limit_template =
               {
                   "WITH t AS (SELECT 'x' AS f) SELECT * FROM t WHERE FALSE",
                   R({" OR f = 'x'"}),
               }},
          {
              .depth_limit_test_case_name = "nested_select",
              .depth_limit_template = {R({"SELECT ("}), "SELECT 1", R({")"}),
                                       " AS c"},
          },
          {
              .depth_limit_test_case_name = "nested_expr_select",
              .depth_limit_template = {"SELECT ", R({"("}), "(SELECT 1) + 2",
                                       R({")"}), " AS c"},
          },
          {
              .depth_limit_test_case_name = "nested_where_in",
              .depth_limit_template = {"WITH d AS (SELECT 1 AS id) ",
                                       R({"SELECT * FROM d WHERE id IN ("}),
                                       "1", R({")"})},
              .depth_limit_max_depth =
                  100,  // As the reference implementations gets very slow
          },
          {
              .depth_limit_test_case_name = "nested_select_from",
              .depth_limit_template = {R({"SELECT * FROM ("}), "SELECT 1",
                                       R({")"}), " AS c"},
          },
          {
              .depth_limit_test_case_name = "nested_cast",
              .depth_limit_template = {"SELECT ", R({"CAST("}), "1",
                                       R({" AS DOUBLE)"}), " AS c"},
          },
          {
              .depth_limit_test_case_name = "nested_safe_cast",
              .depth_limit_template = {"SELECT ", R({"SAFE_CAST("}), "1",
                                       R({" AS DOUBLE)"}), " AS c"},
          },
          {.depth_limit_test_case_name = "nested_least",
           .depth_limit_template = {"SELECT ", R({"LEAST(1,"}), "2", R({")"}),
                                    " AS c"}},
          {
              .depth_limit_test_case_name = "nested_if",
              .depth_limit_template = {"SELECT ", R({"IF(1>2,3,"}), "4",
                                       R({")"}), " AS c"},
          },
          {
              .depth_limit_test_case_name = "nested_md5",
              .depth_limit_template = {"SELECT ", R({"MD5("}), "''", R({")"}),
                                       " AS c"},
          },
          {.depth_limit_test_case_name = "nested_struct_type",
           .depth_limit_template = {"SELECT CAST(NULL AS ", R({"STRUCT<"}),
                                    "DOUBLE", R({">"}), ") AS c"}},
          {
              .depth_limit_test_case_name = "nested_struct",
              .depth_limit_template = {"SELECT ",
                                       R({
                                           "STRUCT(",
                                       }),
                                       "1 AS f", R({") AS S"})},
          },
          {.depth_limit_test_case_name = "nested_flatten",
           .depth_limit_template = {"SELECT ", R({"FLATTEN("}), "NULL",
                                    R({")"}), " AS c"},
           .depth_limit_required_features =
               {
                   LanguageFeature::FEATURE_UNNEST_AND_FLATTEN_ARRAYS,
               }},
          {.depth_limit_test_case_name = "nested_struct_to_json_string",
           .depth_limit_template = {"SELECT TO_JSON_STRING((SELECT ",
                                    R({"STRUCT(["}), R({"] AS f)"}),
                                    ")) as s"}},
          {.depth_limit_test_case_name = "deep_json_extract_string_array",
           .depth_limit_template = {"SELECT JSON_EXTRACT_STRING_ARRAY('",
                                    R({"{f:["}), "7", R({"]}"}), "', '$",
                                    R({".f[0]"}), "') AS j"},
           .depth_limit_required_features =
               {LanguageFeature::FEATURE_JSON_ARRAY_FUNCTIONS}},
          {
              .depth_limit_test_case_name = "nested_join_unnest",
              .depth_limit_template =
                  {
                      "SELECT t.a FROM (SELECT 1 a, [1,2,3] b) t",
                      R({" INNER JOIN UNNEST (t.b) AS j", N(), " ON (t.a = j",
                         N(), ")"}),
                  },
          },
          {
              .depth_limit_test_case_name = "nested_unnest_struct",
              .depth_limit_template =
                  {R({"SELECT AS STRUCT * FROM UNNEST([STRUCT(1, ("}),
                   "SELECT NULL", R({"))])"}), " AS c"},
              .depth_limit_max_depth =
                  100,  // As the reference implementations gets very slow
          },
          {.depth_limit_test_case_name = "nested_replace_fields",
           .depth_limit_template = {"SELECT ", R({"REPLACE_FIELDS("}),
                                    "CAST('int64_key_1: 1 int64_key_2: 2' AS "
                                    "`googlesql_test.KitchenSinkPB`)",
                                    R({", 1 AS int64_key_1)"}), "AS c"},
           .depth_limit_required_features =
               {
                   LanguageFeature::FEATURE_REPLACE_FIELDS,
               }},
          {
              .depth_limit_test_case_name = "repeated_bitwise_not",
              .depth_limit_template = {"SELECT ", R({"~"}), "1 AS c"},
          },
          {
              .depth_limit_test_case_name = "repeated_plus_one",
              .depth_limit_template = {"SELECT 1", R({"+1"}), " AS c"},
          },
          {
              .depth_limit_test_case_name = "repeated_divide_two",
              .depth_limit_template = {"SELECT 1.0", R({"/2"}), " AS c"},
          },
          {
              .depth_limit_test_case_name = "repeated_minus_one",
              .depth_limit_template = {"SELECT 1", R({"-1"}), " AS c"},
          },
          {
              .depth_limit_test_case_name = "repeated_unary_plus",
              .depth_limit_template = {"SELECT +", R({"+"}), "1 AS c"},
          },
          {
              .depth_limit_test_case_name = "pipe_wheres",
              .depth_limit_template = {"FROM (SELECT 1 c)",
                                       R({"|> WHERE c > ", N()})},
              .depth_limit_required_features =
                  {
                      LanguageFeature::FEATURE_PIPES,
                  },
          },
          {
              .depth_limit_test_case_name = "pipe_joins",
              .depth_limit_template = {"FROM (SELECT 1 c)",
                                       R({"|> JOIN (SELECT 2 d", N(), ") ON d",
                                          N(), " = c"})},
              .depth_limit_required_features =
                  {
                      LanguageFeature::FEATURE_PIPES,
                  },
          },
          {
              .depth_limit_test_case_name = "pipe_extends",
              .depth_limit_template = {"FROM (SELECT 1 c)",
                                       R({"|> EXTEND c+1 AS c", N()})},
              .depth_limit_required_features =
                  {
                      LanguageFeature::FEATURE_PIPES,
                  },
          },
          {
              .depth_limit_test_case_name = "pipe_set",
              .depth_limit_template = {"FROM (SELECT 1 c)",
                                       R({"|> SET c = ", N()})},
              .depth_limit_required_features =
                  {
                      LanguageFeature::FEATURE_PIPES,
                  },
              // TODO: b/384695779 - Remove below limit after n^3 memory fixed.
              .depth_limit_max_depth = 100,
          },
          {
              .depth_limit_test_case_name = "pipe_limit",
              .depth_limit_template = {"FROM (SELECT 1 c)",
                                       R({"|> LIMIT ", N(), " OFFSET ", N()})},
              .depth_limit_required_features =
                  {
                      LanguageFeature::FEATURE_PIPES,
                  },
          },
          {
              .depth_limit_test_case_name = "pipe_drop",
              .depth_limit_template = {"FROM (SELECT 1 AS c",
                                       R({", ", N(), " AS c", N()}), ")",
                                       R({"|> DROP c", N()})},
              .depth_limit_required_features =
                  {
                      LanguageFeature::FEATURE_PIPES,
                  },
          },
          {
              .depth_limit_test_case_name = "pipe_order_by",
              .depth_limit_template = {"FROM (SELECT 1 c)",
                                       R({"|> ORDER BY c+", N()})},
              .depth_limit_required_features =
                  {
                      LanguageFeature::FEATURE_PIPES,
                  },
          },
          {
              .depth_limit_test_case_name = "pipe_assert",
              .depth_limit_template = {"FROM (SELECT 1 c)",
                                       R({"|> ASSERT ( SELECT TRUE "}),
                                       R({")"})},
              .depth_limit_required_features =
                  {
                      LanguageFeature::FEATURE_PIPES,
                      LanguageFeature::FEATURE_PIPE_ASSERT,
                  },
          },
          {
              .depth_limit_test_case_name = "with_joins",
              .depth_limit_template = {"WITH ",
                                       R({"t", N(), " AS (SELECT 1 AS c),"}),
                                       " u AS (SELECT 1 AS c) SELECT * FROM u",
                                       R({" JOIN t", N(), " ON t", N(),
                                          ".c = u.c"})},
              .depth_limit_max_depth =
                  500,  // As the reference implementations gets very slow
          },
          {
              .depth_limit_test_case_name = "with_joins_consecutive",
              .depth_limit_template = {"WITH ",
                                       R({"t", N(), " AS (SELECT 1 AS c),"}),
                                       " u AS (SELECT 1 AS c) SELECT * FROM u",
                                       R({" JOIN t", N()}), R({" USING (c)"})},
              .depth_limit_required_features =
                  {
                      LanguageFeature::FEATURE_ALLOW_CONSECUTIVE_ON,
                  },
          },
          {
              .depth_limit_test_case_name = "to_json_struct_array",
              .depth_limit_template = {"SELECT TO_JSON((SELECT ",
                                       R({"STRUCT(["}), "1", R({"] AS f)"}),
                                       "))", R({"[\"f\"][0]"})},
              .depth_limit_required_features =
                  {
                      LanguageFeature::FEATURE_JSON_TYPE,
                  },
          },
          {
              .depth_limit_test_case_name = "to_json_nested_struct",
              .depth_limit_template = {"SELECT TO_JSON((SELECT ",
                                       R({"STRUCT("}), "1", R({" AS f)"}), "))",
                                       R({"[\"f\"]"})},
              .depth_limit_required_features =
                  {
                      LanguageFeature::FEATURE_JSON_TYPE,
                  },
          },
          {
              .depth_limit_test_case_name = "with_union_all",
              .depth_limit_template = {"WITH ",
                                       R({"t", N(), " AS (SELECT 1 AS c),"}),
                                       " u AS (SELECT 1 AS c) SELECT * FROM u",
                                       R({" UNION ALL (SELECT * FROM t", N(),
                                          ")"})},
              .depth_limit_max_depth =
                  10000,  // As the reference implementations gets very slow
          },
          {
              .depth_limit_test_case_name =
                  "create_table_minus_one_default_column",
              .depth_limit_template = {"CREATE TABLE t (c DOUBLE DEFAULT (",
                                       R({"-1"}), "))"},
              .depth_limit_required_features =
                  {LanguageFeature::FEATURE_COLUMN_DEFAULT_VALUE},
          },
          {
              .depth_limit_test_case_name = "select_many_columns",
              .depth_limit_template = {"SELECT 1 AS c0",
                                       R({", ", N(), " AS c", N()})},
              .depth_limit_max_depth =
                  5000,  // As the reference implementations gets very slow
          }};
  static std::vector<std::reference_wrapper<const DepthLimitDetectorTestCase>>*
      case_references = [&]() {
        std::vector<std::reference_wrapper<const DepthLimitDetectorTestCase>>*
            refs = new std::vector<
                std::reference_wrapper<const DepthLimitDetectorTestCase>>();
        for (int64_t i = 0; i < cases->size(); ++i) {
          (*refs).emplace_back((*cases)[i]);
        }
        return refs;
      }();
  return *case_references;
}

absl::Span<const std::reference_wrapper<const DepthLimitDetectorTestCase>>
JSONDepthLimitDetectorTestCases() {
  using R = depth_limit_detector_internal::DepthLimitDetectorRepeatedTemplate;
  using N = depth_limit_detector_internal::DepthLimitDetectorDepthNumber;
  static std::vector<DepthLimitDetectorTestCase>* cases =
      new std::vector<DepthLimitDetectorTestCase>{
          {
              .depth_limit_test_case_name = "nested_json_object",
              .depth_limit_template = {R({"{"}), "\"field\": true", R({"}"})},
          },
          {
              .depth_limit_test_case_name =
                  "nested_json_object_with_fields_at_each_depth",
              .depth_limit_template = {R({"{ \"a\":", N(), ","}), "\"b\": true",
                                       R({"}"})},
          },
          {
              .depth_limit_test_case_name = "nested_json_array_bool",
              .depth_limit_template = {R({"["}), "true", R({"]"})},
          },
          {
              .depth_limit_test_case_name = "nested_json_array_null",
              .depth_limit_template = {R({"["}), "null", R({"]"})},
          },
      };
  static std::vector<std::reference_wrapper<const DepthLimitDetectorTestCase>>*
      case_references = new std::vector<
          std::reference_wrapper<const DepthLimitDetectorTestCase>>(
          cases->begin(), cases->end());
  return *case_references;
}
}  // namespace googlesql
