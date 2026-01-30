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

#include "googlesql/compliance/sql_test_base.h"

#include <string.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <iosfwd>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/base/path.h"
#include "googlesql/common/float_margin.h"
#include "googlesql/common/internal_value.h"
#include "googlesql/common/measure_analysis_utils.h"
#include "googlesql/common/options_utils.h"
#include "googlesql/common/status_payload_utils.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/compliance/compliance_label.pb.h"
#include "googlesql/compliance/compliance_label_extractor.h"
#include "googlesql/compliance/known_error.pb.h"
#include "googlesql/compliance/legal_runtime_errors.h"
#include "googlesql/compliance/matchers.h"
#include "googlesql/compliance/measure_test_tables.h"
#include "googlesql/compliance/sql_test_filebased_options.h"
#include "googlesql/compliance/test_database_catalog.h"
#include "googlesql/compliance/test_driver.h"
#include "googlesql/compliance/test_driver.pb.h"
#include "googlesql/compliance/test_util.h"
#include "googlesql/public/analyzer.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/feature_label_extractor.h"
#include "googlesql/public/functions/string.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/parse_helpers.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/simple_catalog_util.h"
#include "googlesql/public/strings.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/types/value_equality_check_options.h"
#include "googlesql/public/value.h"
#include "googlesql/reference_impl/evaluation.h"
#include "googlesql/reference_impl/reference_driver.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "googlesql/resolved_ast/sql_builder.h"
#include "googlesql/testing/test_value.h"
#include "googlesql/testing/type_util.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/cleanup/cleanup.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/node_hash_set.h"
#include "absl/flags/flag.h"
#include "absl/functional/bind_front.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "googlesql/base/source_location.h"
#include "absl/types/span.h"
#include "file_based_test_driver/file_based_test_driver.h"
#include "file_based_test_driver/run_test_case_result.h"
#include "file_based_test_driver/test_case_options.h"
#include "googlesql/base/file_util.h"  
#include "google/protobuf/descriptor.h"
#include "google/protobuf/text_format.h"  
#include "google/protobuf/text_format.h"
#include "googlesql/base/map_util.h"
#include "farmhash.h"
#include "re2/re2.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

ABSL_FLAG(std::vector<std::string>, known_error_files, {},
          "Comma-separated list of known error filenames");
ABSL_FLAG(bool, report_all_errors, false,
          "(mostly) Ignore the known_error_files; run all statements except "
          "CRASHES_DO_NOT_RUN");

ABSL_FLAG(bool, ignore_wrong_results, false,
          "The test will accept any OK status regardless of the produced "
          "value. This is useful when testing compliance of a component but "
          "not a full evaluation engine. For instance, it can be used to write "
          "a test to determine which compliance queries successfully generate "
          "a logical query plan.");
ABSL_FLAG(bool, ignore_wrong_error_codes, false,
          "The test will accept any status regardless of the error code. This "
          "is useful for engines that cannot plumb through the original error "
          "during failure tests.");
ABSL_FLAG(
    bool, equate_allow_unimplemented_and_allow_error_codes, false,
    "ALLOW_UNIMPLEMENTED and ALLOW_ERROR codes are treated as equivalent. This "
    "is useful for test runs where the reason for error is not important. "
    "Other error codes like ALLOW_ERROR_OR_WRONG_ANSWER are still "
    "distinguished.");
ABSL_FLAG(bool, googlesql_compliance_accept_all_test_output, false,
          "Pretend no tests failed by ignoring their output.");
// Ideally we would rename this to --statement_name_pattern, but that might
// break some command line somewhere.
ABSL_FLAG(std::string, query_name_pattern, "",
          "Only test statements with names matching this pattern. This filter "
          "works only for statements in .test files. No effect if left empty.");

// These flag may be enabled to enforce stricter treatment of known_error files.
// It should not be used in presubmits for tests outside the googlesql
// directory, but it can be useful in team triaged continuous builds.
ABSL_FLAG(bool, googlesql_compliance_allow_removable_known_errors, true,
          "When true, labels in known_error file are allowed to apply to "
          "queries which pass.");
ABSL_FLAG(bool, googlesql_compliance_allow_upgradable_known_errors, true,
          "When true, known error listings that need to be upgraded are "
          "allowed in the known errors file.");
ABSL_FLAG(
    bool, googlesql_detect_falsly_required_features, false,
    "When true, the reference implementation test will fail test cases that "
    "declare a required feature but do not actually require that feature.  "
    "This flag has no effect when run with other engines.");
ABSL_FLAG(bool, googlesql_compliance_write_labels_to_file, false,
          "When true, cache compliance labels across different tests and write "
          "the labels to undeclared output in a textproto file format.");
ABSL_FLAG(bool, googlesql_compliance_enforce_no_reference_label, false,
          "When true requires that Special:NoCompliance label is applied to "
          "exactly the tests that return an unimplemented error from the "
          "reference implementation.");
ABSL_FLAG(bool, googlesql_compliance_extract_labels_using_all_rewrites, false,
          "When true, applies all rewrites to the ResolvedAST used for "
          "compliance label extraction. Note that this logic only applies to "
          "compliance label extraction and not test execution.");
ABSL_FLAG(bool, googlesql_compliance_print_array_orderedness, false,
          "When true, includes the 'known order:', 'unknown order:' prefix "
          "on array values with two or more elements.");

ABSL_FLAG(bool, googlesql_verify_compliance_goldens, false,
          "When true the compliance test is verifying the expected or 'golden' "
          "results for statements. Otherwise we are using the Reference "
          "Implementation to verify engine results.");

namespace googlesql {

namespace {

// This RTTI helper class allows temporarily modifying language settings on the
// reference driver, and it will automatically restore original settings upon
// destruction.
class AutoLanguageOptions {
 public:
  explicit AutoLanguageOptions(ReferenceDriver* reference_driver)
      : reference_driver_(reference_driver) {
    original_options_ = reference_driver_->GetSupportedLanguageOptions();
  }
  ~AutoLanguageOptions() {
    reference_driver_->SetLanguageOptions(original_options_);
  }

 protected:
  ReferenceDriver* reference_driver_;
  LanguageOptions original_options_;
};

}  // anonymous namespace

// Constants for code-based statement names.
// A name must be RE2 safe. All unsafe characters will be replaced by '@'.
constexpr char kSafeChar = '@';
constexpr absl::string_view kSafeString(&kSafeChar, 1);

// A compliance label for tests that require legacy behavior according to
// the LanguageFeatureOptions::ideally_enabled annotation.
constexpr absl::string_view kLegacyBehaviorLabel = "Special:LegacyBehavior";
// A compliance label for tests that require an in_development LanguageFeature.
constexpr absl::string_view kInDevelopmentLabel = "Special:InDevelopment";
// A compliance label for tests where the reference impl returns kUnimplemented.
constexpr absl::string_view kNoReferenceLabel = "Special:NoReference";
// A compliance label that is attached to tests that do not compile for the
// reference implementation. Such tests aren't useful for validating engine
// implementation completeness.
constexpr absl::string_view kNoCompileLabel = "Special:NoCompileTest";
// Labels that indicate which product modes a test expected to work on.
constexpr absl::string_view kProductModeInternalAndExternalLabel =
    "ProductMode:InternalAndExternal";
constexpr absl::string_view kProductModeInternalLabel =
    "ProductMode:InternalOnly";
constexpr absl::string_view kProductModeExternalLabel =
    "ProductMode:ExternalOnly";
// Labels that indicate which time resolution a test expected to work on.
constexpr absl::string_view kTimeResolutionAnyLabel = "TimeResolution:Any";
constexpr absl::string_view kTimeResolutionNanosLabel =
    "TimeResolution:NanosOnly";
constexpr absl::string_view kTimeResolutionMicrosLabel =
    "TimeResolution:MicrosOnly";

// We include broken [prepare_database] statements with this name in some
// test files to make sure the framework is handling things correctly when
// an engine does not support something inside a function or view definition.
constexpr absl::string_view kSkipFailedReferenceSetup =
    "skip_failed_reference_setup";

// For a long string, a signature is generated, which is the left 8
// characters of the string, followed by the fingerprint of the string,
// followed by the right 8 characters of the string.
constexpr int kLengthOfLeftSlice = 8;
constexpr int kLengthOfRightSlice = 8;

constexpr size_t kLogBufferSize =
    15000;

// Stats over all tests in a test case.
class Stats {
 public:
  Stats();
  Stats(const Stats&) = delete;
  Stats& operator=(const Stats&) = delete;

  static void RecordProperty(const std::string& key, const std::string& value) {
    SQLTestBase::RecordProperty(key, value);
  }
  static void RecordProperty(const std::string& key, int value) {
    SQLTestBase::RecordProperty(key, value);
  }

  // Record executed statements.
  void RecordExecutedStatement();

  // As above, but no-op if `check_only` is true.
  void RecordExecutedStatement(bool check_only) {
    if (!check_only) {
      return RecordExecutedStatement();
    }
  }

  // A to-be-removed-from-known-errors statement was in `was_mode`.
  void RecordToBeRemovedFromKnownErrorsStatement(const std::string& full_name,
                                                 KnownErrorMode was_mode);

  // As above, but a no-op if `check_only` is true.
  void RecordToBeRemovedFromKnownErrorsStatement(const std::string& full_name,
                                                 const KnownErrorMode was_mode,
                                                 bool check_only) {
    if (!check_only) {
      return RecordToBeRemovedFromKnownErrorsStatement(full_name, was_mode);
    }
  }

  // A to-be-upgraded statement was in `was_mode`, and will
  // upgrade to `new_mode`.
  void RecordToBeUpgradedStatement(const std::string& full_name,
                                   KnownErrorMode was_mode,
                                   KnownErrorMode new_mode);

  // As above, but a no-op if `check_only` is true.
  void RecordToBeUpgradedStatement(const std::string& full_name,
                                   const KnownErrorMode was_mode,
                                   const KnownErrorMode new_mode,
                                   bool check_only) {
    if (!check_only) {
      return RecordToBeUpgradedStatement(full_name, was_mode, new_mode);
    }
  }

  // The failed statement will be put into to-be-added-to-known-errors list
  // with `new_mode`. The statement was in `was_mode`.
  void RecordFailedStatement(absl::string_view msg, const std::string& location,
                             const std::string& full_name,
                             KnownErrorMode was_mode, KnownErrorMode new_mode);

  // As above, but a no-op if `check_only` is true.
  void RecordFailedStatement(absl::string_view msg, const std::string& location,
                             const std::string& full_name,
                             const KnownErrorMode was_mode,
                             const KnownErrorMode new_mode, bool check_only) {
    if (!check_only) {
      RecordFailedStatement(msg, location, full_name, was_mode, new_mode);
    }
  }

  // Composes a cancellation report, records it as a failure.
  void RecordCancelledStatement(const std::string& location,
                                const std::string& full_name,
                                KnownErrorMode was_mode,
                                KnownErrorMode new_mode,
                                absl::string_view reason,
                                absl::string_view detail);

  // Records a known error statement with its mode and the set of labels that
  // caused it to fail.
  void RecordKnownErrorStatement(const std::string& location,
                                 KnownErrorMode mode,
                                 const absl::btree_set<std::string>& by_set);

  // Record the runtime duration of a executed statement.
  void RecordStatementExecutionTime(absl::Duration elapsed);

  // Record a failure that is not resolved by adding a known error entry.
  void RecordFailure(absl::string_view error_string) {
    failures_.emplace_back(error_string);
  }

  // Record compliance label info for one test query.
  // The actual error mode, i.e. test error mode (KnownErrorMode) is generated
  // by KnownErrorFilter::MatchAndExplain.
  // In both reference implementation test and engine test, compliance labels
  // will be extracted and written to undeclared output file.
  // This function can only be called after matchers are invoked by EXPECT_THAT.
  void RecordComplianceTestsLabelsProto(
      absl::string_view test_name, absl::string_view sql,
      const std::map<std::string, Value>& params, absl::string_view location,
      KnownErrorMode actual_error_mode,
      const absl::btree_set<std::string>& label_set,
      absl::StatusCode expected_error_code,
      absl::StatusCode actual_error_code) {
    // We don't run most of this function in presubmit, so we add the check
    // before the early return to guard against regressions.
    ABSL_CHECK(!test_name.empty());
    ABSL_CHECK(!absl::EndsWith(test_name, ":"));
    // Skip building the protos when not requested to prevent long-running RQG
    // tests from consuming too much memory.
    // See b/238890147
    if (!absl::GetFlag(FLAGS_googlesql_compliance_write_labels_to_file)) {
      return;
    }
    static const LazyRE2 kExtractLocation = {R"(FILE-(.+\.\w+)-LINE-(\d+).*)"};
    absl::string_view file = "";
    int line = -1;
    if (!location.empty()) {
      bool matched = RE2::FullMatch(location, *kExtractLocation, &file, &line);
      ABSL_DCHECK(matched) << "Failed to find filename and line in " << location;
    }

    ComplianceTestCaseLabels* test_case = labels_proto_.add_test_cases();
    test_case->set_test_name(test_name);
    test_case->set_test_query(sql);
    test_case->mutable_test_location()->set_file(file);
    test_case->mutable_test_location()->set_line(line);
    static constexpr int kMaxParameterLiteralSize = 1000;
    std::string trunc_msg = "[TRUNCATED]";
    for (auto& [param_name, param_value] : params) {
      ComplianceTestCaseLabels::Param* param = test_case->add_param();
      param->set_param_name(param_name);
      std::string param_value_literal =
          param_value.GetSQLLiteral(PRODUCT_EXTERNAL);
      if (param_value_literal.size() > kMaxParameterLiteralSize) {
        param_value_literal.resize(kMaxParameterLiteralSize - trunc_msg.size());
        param_value_literal.append(trunc_msg);
      }
      param->set_param_value_literal(param_value_literal);
    }
    test_case->set_test_error_mode(actual_error_mode);
    for (const std::string& label : label_set) {
      test_case->add_compliance_labels(label);
    }
  }

  // Gets the most recently appended test case to the labels proto. This should
  // only be used for testing SqlTestBase, not by other clients.
  const ComplianceTestCaseLabels& GetLastComplianceTestCaseLabels() {
    ABSL_DCHECK(!labels_proto_.test_cases().empty());
    return labels_proto_.test_cases().Get(labels_proto_.test_cases_size() - 1);
  }

  void LogGoogletestProperties() const;

  // Print report to log file.
  void LogReport() const;

  // Write compliance labels proto to undeclared outputs file in textproto file
  // format.
  // This outputs all labels from all test cases stored in labels_proto_.
  // It only takes effect when the global flag is on, in order to prevent
  // long-running RQG from consuming too much memory. See b/238890147.
  void WriteComplianceLabels() {
  }

  // Used to tell RecordFailedCode/FileBasedStatement(...) whether we are
  // running a file-based test.
  void StartFileBasedStatements() { file_based_statements_ = true; }
  void EndFileBasedStatements() { file_based_statements_ = false; }
  // Used by ::testing::Matcher<const absl::StatusOr<Value>&> to call
  // the right RecordFailed*Statement().
  bool IsFileBasedStatement() { return file_based_statements_; }

 private:
  // Accepts an iterable (e.g., set or vector) of strings, and log it in
  // batches.
  template <class Iterable>
  void LogBatches(const Iterable& iterable, const std::string& title,
                  absl::string_view delimiter) const;

  // Composes the following string.
  //   "label: <label>    # was: <was_mode> - new mode: <new_mode>"
  std::string LabelString(absl::string_view label, KnownErrorMode was_mode,
                          KnownErrorMode new_mode) const;

  int num_executed_ = 0;
  std::vector<std::string> failures_;

  std::set<std::string> to_be_added_to_known_errors_;
  std::set<std::string> to_be_removed_from_known_errors_;
  std::set<std::string> to_be_upgraded_;
  std::unordered_map<KnownErrorMode, std::set<std::string>>
      error_mode_to_new_failures_;
  std::unordered_map<KnownErrorMode, std::string>
      error_mode_to_example_message_;

  // Compliance labels represented by ComplianceTestsLabels Proto message.
  // This accumulates labels across different test cases and will be written to
  // file after all tests are processed.
  ComplianceTestsLabels labels_proto_;

  int num_known_errors_ = 0;

  bool file_based_statements_ = false;
};

Stats::Stats()
{}

void Stats::RecordExecutedStatement() { num_executed_++; }

std::string Stats::LabelString(absl::string_view label,
                               const KnownErrorMode was_mode,
                               const KnownErrorMode new_mode) const {
  std::string label_string = absl::StrCat("  label: \"", label, "\"");
  if (new_mode) {
    absl::StrAppend(&label_string,
                    "    # mode: ", KnownErrorMode_Name(new_mode));
  }
  if (was_mode) {
    absl::StrAppend(&label_string, new_mode ? "," : "    #",
                    " was: ", KnownErrorMode_Name(was_mode));
  }
  return label_string;
}

void Stats::RecordToBeRemovedFromKnownErrorsStatement(
    const std::string& full_name, const KnownErrorMode was_mode) {
  to_be_removed_from_known_errors_.insert(
      LabelString(full_name, was_mode, KnownErrorMode::NONE));
}

void Stats::RecordToBeUpgradedStatement(const std::string& full_name,
                                        const KnownErrorMode was_mode,
                                        const KnownErrorMode new_mode) {
  to_be_upgraded_.insert(LabelString(full_name, was_mode, new_mode));
}

void Stats::RecordFailedStatement(absl::string_view msg,
                                  const std::string& location,
                                  const std::string& full_name,
                                  const KnownErrorMode was_mode,
                                  const KnownErrorMode new_mode) {
  if (file_based_statements_) RecordProperty(location, "Failed");
  to_be_added_to_known_errors_.insert(
      LabelString(full_name, was_mode, new_mode));
  error_mode_to_new_failures_[new_mode].insert(full_name);
  error_mode_to_example_message_[new_mode] = msg;
  failures_.emplace_back(msg);
}

void Stats::RecordCancelledStatement(const std::string& location,
                                     const std::string& full_name,
                                     const KnownErrorMode was_mode,
                                     const KnownErrorMode new_mode,
                                     absl::string_view reason,
                                     absl::string_view detail) {
  ++num_executed_;
  std::string report =
      absl::StrCat("Location: ", location, "\n", "    Name: ", full_name, "\n",
                   "  Reason: ", reason, "\n", "  Detail: ", detail);
  RecordFailedStatement(report, location, full_name, was_mode, new_mode);
  RecordProperty(location, absl::StrCat("Cancelled: ", reason, ": ", detail));
}

void Stats::RecordKnownErrorStatement(
    const std::string& location, const KnownErrorMode mode,
    const absl::btree_set<std::string>& by_set) {
  num_known_errors_++;

  const std::string by =
      absl::StrCat("Mode: ", KnownErrorMode_Name(mode),
                   ", Due to: ", absl::StrJoin(by_set, ", "));

  RecordProperty(location, by);
}

void Stats::RecordStatementExecutionTime(absl::Duration elapsed) {
}

void Stats::LogGoogletestProperties() const {
  RecordProperty("Passed", num_executed_ - failures_.size());
  RecordProperty("Failed", failures_.size());
  RecordProperty("KnownErrors", num_known_errors_);
  RecordProperty(
      "Compliance",
      absl::StrCat((((num_executed_ - failures_.size()) * 1000 /
                     std::max(1, num_executed_ + num_known_errors_)) /
                    10.0),
                   "%"));
}

template <class Iterable>
void Stats::LogBatches(const Iterable& iterable, const std::string& title,
                       absl::string_view delimiter) const {
  std::vector<std::string> batch;
  int batch_string_size = 0;
  int batch_count = 0;

  auto LogOneBatch = [&title, delimiter, &batch_count, &batch,
                      &batch_string_size] {
    ++batch_count;
    // Always "====" to the beginning and "==== End " to the end so
    // extract_compliance_results.py can recognize it.
    ABSL_LOG(INFO) << "\n==== " << title << " #" << batch_count
              << "\n"
              // Leave enough space for "End".
              << absl::StrJoin(batch, delimiter).substr(0, kLogBufferSize - 200)
              << (batch.empty() ? std::string() : "\n") << "==== End " << title
              << " #" << batch_count;
    batch.clear();
    batch_string_size = 0;
  };

  for (const std::string& item : iterable) {
    if (batch_string_size >= kLogBufferSize / 2) {
      LogOneBatch();
    }
    batch.emplace_back(item);
    batch_string_size += item.size();
  }
  LogOneBatch();
}

void Stats::LogReport() const {
  std::vector<std::string> known_error_files =
      absl::GetFlag(FLAGS_known_error_files);
  const std::string compliance_report_title = "GOOGLESQL COMPLIANCE REPORT";
  // Always "====" to the beginning and "==== End " to the end so
  // extract_compliance_results.py can recognize it.
  ABSL_LOG(INFO) << "\n"
            << "==== " << compliance_report_title << "\n"
            << "[  PASSED  ] " << num_executed_ - failures_.size()
            << " statements.\n"
            << "[  FAILED  ] " << failures_.size() << " statements.\n"
            << "[KNOWN_ERRS] " << num_known_errors_ << " statements.\n"
            << "[COMPLIANCE] "
            << (((num_executed_ - failures_.size()) * 1000 /
                 std::max(1, num_executed_ + num_known_errors_)) /
                10.0)
            << "%.\n"
            << "==== End " << compliance_report_title;

  LogBatches(failures_, "Failures Summary", "\n==\n");
  LogBatches(to_be_added_to_known_errors_,
             "To Be Added To Known Errors Statements", "\n");
  LogBatches(to_be_removed_from_known_errors_,
             "To Be Removed From Known Errors Statements", "\n");
  LogBatches(to_be_upgraded_, "To Be Upgraded Statements", "\n");

  ABSL_LOG(INFO) << "\n==== RELATED KNOWN ERROR FILES ====\n"
            << absl::StrJoin(known_error_files, "\n")
            << "\n==== END RELATED KNOWN ERROR FILES ====\n";
  if (!error_mode_to_new_failures_.empty()) {
    std::vector<std::string> suggestions;
    for (const auto& [error_mode, cases] : error_mode_to_new_failures_) {
      KnownErrorFile enclosing_proto;
      KnownErrorEntry* labels_proto = enclosing_proto.add_known_errors();
      std::string example_msg =
          error_mode_to_example_message_.find(error_mode)->second;
      labels_proto->set_mode(error_mode);
      // Give an example reason, the last part of the message, which is often
      // more informative. This likely needs to be edited manually.
      labels_proto->set_reason("Example: " +
                               example_msg.substr(example_msg.size() > 100
                                                      ? example_msg.size() - 80
                                                      : 0));
      for (const auto& label : cases) {
        labels_proto->add_label(label);
      }
      google::protobuf::TextFormat::Printer printer;
      printer.SetPrintMessageFieldsInIndexOrder(true);  // So mode comes first

      printer.PrintToString(enclosing_proto, &suggestions.emplace_back());
    }
    LogBatches(suggestions, "Suggestions for adding known errors:", "\n");
  }
}

using ComplianceTestCaseResult = SQLTestBase::ComplianceTestCaseResult;

std::unique_ptr<Stats> SQLTestBase::stats_;
std::unique_ptr<MatcherCollection<absl::Status>>
    SQLTestBase::legal_runtime_errors_;

SQLTestBase::TestCaseInspectorFn* SQLTestBase::test_case_inspector_ = nullptr;

const ComplianceTestCaseLabels&
SQLTestBase::TESTONLY_ComplianceTestCaseLabels() {
  return stats_->GetLastComplianceTestCaseLabels();
}

void SQLTestBase::TESTONLY_SetTestFileOptions(
    std::unique_ptr<FilebasedSQLTestFileOptions> test_file_options) {
  test_file_options_ = std::move(test_file_options);
}

// gMock matcher that checkes if a statement result (StatusOr<Value>) has OK
// status.
MATCHER(ReturnsOk, "OK") { return arg.ok(); }

// gMock matcher that matches a statement result (StatusOr<Value>) with a
// string. Not supposed to be called directly. Use KnownErrorFilter.
MATCHER_P(ReturnsString, expected, expected) {
  return SQLTestBase::ToString(arg) == expected;
}

// gMock matcher that matches a SQLTestBase::TestResults with a string. Not
// supposed to be called directly. Use KnownErrorFilter.
MATCHER_P(IsTestResultsToString, expected, expected) {
  return arg.ToString() == expected;
}

namespace {

std::string GetLocationString(const StatementResult& result) {
  return absl::StrCat(result.procedure_name,
                      result.procedure_name.empty() ? "" : "() ", "[",
                      result.line, ":", result.column, "]");
}

bool CompareStatementResult(const StatementResult& expected,
                            const StatementResult& actual,
                            FloatMargin float_margin, std::string* reason) {
  if (expected.procedure_name != actual.procedure_name ||
      expected.line != actual.line || expected.column != actual.column) {
    *reason = absl::StrCat("Location mismatch; expected ",
                           GetLocationString(expected), "; got ",
                           GetLocationString(actual));
    return false;
  }
  if (expected.result.ok()) {
    if (actual.result.ok()) {
      if (InternalValue::Equals(
              expected.result.value(), actual.result.value(),
              {.interval_compare_mode = IntervalCompareMode::kSqlEquals,
               .float_margin = float_margin,
               .reason = reason})) {
        return true;
      }
    } else {
      *reason = absl::StrCat("Expected statement to succeed, but it failed: ",
                             actual.result.status().ToString());
    }
  } else {
    // Ignore differences in error code or error message, since this is expected
    // across different engines. However, internal errors happen only in case of
    // a bug, so they will always fail.
    if (absl::IsInternal(actual.result.status())) {
      *reason = absl::StrCat("Expected statement to fail with ",
                             expected.result.status().ToString(),
                             ", but got an internal error instead: ",
                             actual.result.status().ToString());
    } else if (actual.result.status().ok()) {
      *reason =
          absl::StrCat("Expected statement to fail with ",
                       expected.result.status().ToString(),
                       ", but the statement instead succeeded ", "with result ",
                       SQLTestBase::ToString(actual.result));
    } else {
      return true;
    }
  }
  absl::StrAppend(reason, " at ", GetLocationString(expected));
  return false;
}

std::string StatementResultToString(const StatementResult& stmt_result) {
  absl::StatusOr<ComplianceTestCaseResult> status_or;
  if (stmt_result.result.ok()) {
    status_or = stmt_result.result.value();
  } else {
    // For file-based tests we do not care about the error message mode.
    absl::Status status_without_error_mode_payload =
    stmt_result.result.status();
    status_without_error_mode_payload.ErasePayload(kErrorMessageModeUrl);
    status_or = std::move(status_without_error_mode_payload);
  }

  if (stmt_result.line == 0 || stmt_result.column == 0) {
    // No location information available.
    return SQLTestBase::ToString(status_or);
  }

  return absl::StrCat(GetLocationString(stmt_result), " ",
                      SQLTestBase::ToString(status_or));
}

bool CompareScriptResults(const ScriptResult& expected,
                          const ScriptResult& actual, FloatMargin float_margin,
                          std::string* reason) {
  reason->clear();
  // Compare common statement results
  for (int i = 0; i < std::min(expected.statement_results.size(),
                               actual.statement_results.size());
       ++i) {
    if (!CompareStatementResult(expected.statement_results.at(i),
                                actual.statement_results.at(i), float_margin,
                                reason)) {
      return false;
    }
  }

  // Check for missing/extra statement results.
  if (actual.statement_results.size() < expected.statement_results.size()) {
    for (size_t i = actual.statement_results.size();
         i < expected.statement_results.size(); ++i) {
      absl::StrAppend(reason, "Missing statement result ",
                      StatementResultToString(expected.statement_results.at(i)),
                      "\n");
    }
    return false;
  }
  if (actual.statement_results.size() > expected.statement_results.size()) {
    for (size_t i = expected.statement_results.size();
         i < actual.statement_results.size(); ++i) {
      absl::StrAppend(reason, "Extra statement result ",
                      StatementResultToString(actual.statement_results.at(i)));
    }
    return false;
  }
  return true;
}
}  // namespace

// TODO: b/388309312 - Change the string representation to start with
// "MultiStmtResult" instead.
std::string ScriptResultToString(const ScriptResult& result) {
  return absl::StrCat(
      "ScriptResult\n",
      absl::StrJoin(result.statement_results, "\n",
                    [](std::string* out, const StatementResult& stmt_result) {
                      absl::StrAppend(out,
                                      StatementResultToString(stmt_result));
                    }));
}

// gMock matcher that matches a statement result (StatusOr<Value>) with a
// StatusOr<value>. Not supposed to be called directly. Use KnownErrorFilter.
MATCHER_P2(ReturnsStatusOrValue, expected, float_margin,
           SQLTestBase::ToString(expected)) {
  bool passed = true;
  std::string reason;
  if (expected.ok() != arg.ok()) {
    passed = false;
  } else if (expected.ok()) {
    if (std::holds_alternative<Value>(expected.value()) &&
        std::holds_alternative<Value>(arg.value())) {
      passed = InternalValue::Equals(
          std::get<Value>(expected.value()), std::get<Value>(arg.value()),
          {.interval_compare_mode = IntervalCompareMode::kSqlEquals,
           .float_margin = float_margin,
           .reason = &reason});
    } else if (std::holds_alternative<ScriptResult>(expected.value()) &&
               std::holds_alternative<ScriptResult>(arg.value())) {
      passed = CompareScriptResults(std::get<ScriptResult>(expected.value()),
                                    std::get<ScriptResult>(arg.value()),
                                    float_margin, &reason);
    }
  } else if (!absl::GetFlag(FLAGS_ignore_wrong_error_codes)) {
    // Any known error code would be OK.
    passed = SQLTestBase::legal_runtime_errors()->Matches(arg.status());
  }

  if (!passed) {
    absl::string_view trimmed_reason;
    absl::Status status;
    ABSL_CHECK(googlesql::functions::RightTrimBytes(reason, "\n", &trimmed_reason,
                                               &status));

    std::string error;
    if (expected.ok() && std::holds_alternative<Value>(expected.value()) &&
        googlesql::testing::HasFloatingPointNumber(
            std::get<Value>(expected.value()).type())) {
      error = absl::StrCat("\nFloat comparison: ", float_margin.DebugString());
    }
    *result_listener << absl::StrCat(
        error, trimmed_reason.empty() ? "" : "\n Details: ", trimmed_reason);
    return false;
  }
  return true;
}

namespace {

std::string ReplaceChar(absl::string_view str, absl::string_view unsafe_chars,
                        char safe_char) {
  std::string safe = std::string(str);
  for (char& ch : safe) {
    if (absl::StrContains(unsafe_chars, ch)) {
      ch = safe_char;
    }
  }

  return safe;
}

// Returns name of file-based tests.
// Replaces dots in file name with underscores, e.g. 'foo.test' => 'foo_test'.
// Motivation: when a label does not contain regex characters like '.',
// we bypass RE2 construction, see AddKnownErrorEntry, which is much faster.
// Note that label 'foo_test' is still matched by known error 'foo.test', so
// this replacement is safe with regards to legacy known error .
std::string FileBasedTestName(absl::string_view filename,
                              absl::string_view name) {
  std::string cleaned_file_name =
      absl::StrReplaceAll(googlesql_base::Basename(filename), {{".", "_"}});
  return absl::StrCat(cleaned_file_name, ":", name);
}

}  // namespace

// gMock matcher that uses another matcher to match a statement result
// (StatusOr<Value>). Bypasses a known error statement and returns true.
// In '--report_all_errors mode', matches all but CRASHES_DO_NOT_RUN statements.
// If the statement passed, records it in the to-be-removed-from-known-error
// list. Bypasses recording in check-only mode.
template <class ResultsType>
class KnownErrorFilter
    : public ::testing::MatcherInterface<const ResultsType&> {
 public:
  // 'expected_status' is the same status expected by 'matcher' or OK if
  // 'matcher' is expecting something other than an error status.
  KnownErrorFilter(SQLTestBase* sql_test, absl::Status expected_status,
                   const ::testing::Matcher<const ResultsType&>& matcher,
                   bool check_only = false)
      : sql_test_(sql_test),
        matcher_(matcher),
        expected_status_(expected_status),
        check_only_(check_only) {}

  // Write out a CSV format line with detailed results of each test case.
  // These can be extracted into a file using extract_compliance_results.sh.
  void LogToCSV(bool passed, KnownErrorMode known_error_mode) const {
    // CSV columns:
    // TargetName, TestPrefix, TestName, Passed, KnownError, KnownErrorMode
    // NOTE: Column names are duplicated in extract_compliance_results.py.
    ABSL_LOG(INFO) << "CSV: \""
              << "\",\""
              << (sql_test_->stats_->IsFileBasedStatement()
                      ? sql_test_->full_name_
                      : sql_test_->GetNamePrefix())
              << "\",\"" << sql_test_->full_name_ << "\","
              << (passed ? "true" : "false") << ","
              << (known_error_mode != KnownErrorMode::NONE ? "true" : "false")
              << "," << KnownErrorMode_Name(known_error_mode) << std::endl;
  }

  bool MatchAndExplain(
      const ResultsType& result,
      ::testing::MatchResultListener* listener) const override {
    // Matchers are meant to be idempotent and can be called multiple times by
    // gtest in the case of failures. Side effects like recording stats need to
    // be guarded so that we don't record multiple failures for a single query.
    if (cached_match_result_.has_value()) {
      if (listener->IsInterested()) {
        *listener->stream() << cached_match_result_string_;
      }
      return cached_match_result_.value();
    }

    const KnownErrorMode from_mode = sql_test_->known_error_mode();
    // CRASHES_DO_NOT_RUN is always skipped, return here.
    if (KnownErrorMode::CRASHES_DO_NOT_RUN == from_mode ||
        absl::GetFlag(FLAGS_googlesql_compliance_accept_all_test_output)) {
      LogToCSV(/*passed=*/false, from_mode);
      cached_match_result_ = true;
      return true;
    }

    const bool passed = matcher_.MatchAndExplain(result, listener);
    cached_match_result_ = passed;
    LogToCSV(passed, from_mode);

    // Gets the right known_error mode for the statement.
    KnownErrorMode to_mode = from_mode;
    if (passed) {
      to_mode = KnownErrorMode::NONE;
    } else if (result.status().ok()) {
      if (absl::GetFlag(FLAGS_ignore_wrong_results)) {
        return true;  // With this flag, we consider this success.
      }
      to_mode = KnownErrorMode::ALLOW_ERROR_OR_WRONG_ANSWER;
    } else if (result.status().code() != absl::StatusCode::kUnimplemented) {
      to_mode = KnownErrorMode::ALLOW_ERROR;
    } else {
      to_mode = KnownErrorMode::ALLOW_UNIMPLEMENTED;
    }
    // Record test labels for tests, but not for prepare_database sections.
    if (sql_test_->statement_workflow_ != SQLTestBase::NOT_A_TEST) {
      // Record actual error mode of current test case to global stats.
      sql_test_->stats_->RecordComplianceTestsLabelsProto(
          sql_test_->full_name_, sql_test_->sql_, sql_test_->parameters_,
          sql_test_->location(), to_mode, sql_test_->compliance_labels_,
          expected_status_.code(), result.status().code());
    }
    // If true, mismatching error codes does not trigger test failure.
    bool ignore_error_code = false;
    if (absl::GetFlag(FLAGS_equate_allow_unimplemented_and_allow_error_codes)) {
      // If equate_allow_unimplemented_and_allow_error_codes=true,
      // ALLOW_UNIMPLEMENTED and ALLOW_ERROR error codes should be considered
      // equivalent.
      if ((to_mode == KnownErrorMode::ALLOW_UNIMPLEMENTED &&
           from_mode == KnownErrorMode::ALLOW_ERROR) ||
          (from_mode == KnownErrorMode::ALLOW_UNIMPLEMENTED &&
           to_mode == KnownErrorMode::ALLOW_ERROR)) {
        ignore_error_code = true;
      }
    }
    if (to_mode > from_mode && !ignore_error_code) {
      // 1. to_mode > 0 = from_mode: A failed non-known_error statement.
      // 2. to_mode > from_mode > 0: A known-error statement failed in a more
      //    severe mode.
      // In either case, log it as a failed statement.
      std::stringstream describe;
      matcher_.DescribeTo(&describe);
      // If listener->IsInterested() is false, there is no explanation of the
      // failure and the report will have missing info since we cache the
      // results of this call so that this matcher is idempotent. We create our
      // own listener and call MatchAndExplain a second time to ensure we have
      // the failure info in the initial report we record and in the cached
      // value used for future calls.
      //
      // Note that this only happens for real failures so we will rarely need
      // to copy large match result strings around.
      std::string extra;
      if (listener->IsInterested()) {
        std::stringstream ss;
        ss << listener->stream()->rdbuf();
        extra = ss.str();
      } else {
        ::testing::StringMatchResultListener string_listener;
        matcher_.MatchAndExplain(result, &string_listener);
        extra = string_listener.str();
      }
      std::string report = sql_test_->GenerateFailureReport(
          describe.str(), SQLTestBase::ToString(result), extra);
      cached_match_result_string_ = std::move(extra);
      sql_test_->stats_->RecordFailedStatement(report, sql_test_->location_,
                                               sql_test_->full_name_, from_mode,
                                               to_mode, check_only_);
      // The framework didn't record execution of a known-error statement in
      // ExecuteTestCase(...). Do it now.
      if (from_mode) {
        sql_test_->stats_->RecordExecutedStatement(check_only_);
      }
      // Fail the test.
      return false;
    } else if (from_mode) {
      // A known error statement. Record it.
      sql_test_->RecordKnownErrorStatement(to_mode, check_only_);
      if (!to_mode) {
        // A to-be-removed-from-known_errors statement.
        sql_test_->stats_->RecordToBeRemovedFromKnownErrorsStatement(
            sql_test_->full_name_, from_mode, check_only_);
        if (!absl::GetFlag(
                FLAGS_googlesql_compliance_allow_removable_known_errors)) {
          cached_match_result_string_ =
              absl::StrCat("A known error rule applies to a passing test '",
                           sql_test_->full_name_,
                           "'. Remove or refine the known error entry.");
          sql_test_->stats_->RecordFailure(cached_match_result_string_);
          return false;
        }
      } else if (to_mode < from_mode) {
        // A to-be-upgraded statement.
        sql_test_->stats_->RecordToBeUpgradedStatement(
            sql_test_->full_name_, from_mode, to_mode, check_only_);
        if (!absl::GetFlag(
                FLAGS_googlesql_compliance_allow_upgradable_known_errors) &&
            !ignore_error_code) {
          cached_match_result_string_ =
              absl::StrCat("A known error rule should be upgraded for '",
                           sql_test_->full_name_, "'.");
          sql_test_->stats_->RecordFailure(cached_match_result_string_);
          return false;
        }
      }
      // Don't fail the test.
      return true;
    }
    // This implies to_mode <= from_mode && from_mode == 0. Thus to_mode == 0.
    ABSL_DCHECK_EQ(to_mode, 0);
    // Not a known error, and the test passed.
    return true;
  }

  void DescribeTo(::std::ostream* os) const override {
    matcher_.DescribeTo(os);
  }

  void DescribeNegationTo(::std::ostream* os) const override {
    matcher_.DescribeNegationTo(os);
  }

 private:
  SQLTestBase* sql_test_;
  ::testing::Matcher<const ResultsType&> matcher_;
  absl::Status expected_status_;  // used for logging compliance labels
  bool check_only_;
  mutable std::optional<bool> cached_match_result_;
  mutable std::string cached_match_result_string_;
};

std::string SQLTestBase::GenerateFailureReport(const std::string& expected,
                                               const std::string& actual,
                                               const std::string& extra) const {
  return absl::StrCat(
      "   Statement: ", sql_, "\n", "  Params: ", ToString(parameters_), "\n",
      "Location: ", location_, "\n", "    Name: ", full_name_, "\n",
      "  Labels: ", absl::StrJoin(effective_labels_, ", "), "\n",
      "Expected: ", expected, "\n", "  Actual: ", actual, extra,
      extra.empty() ? "" : "\n");
}

FloatMargin SQLTestBase::GetFloatEqualityMargin(
    absl::StatusOr<ComplianceTestCaseResult> actual,
    absl::StatusOr<ComplianceTestCaseResult> expected, int max_ulp_bits,
    bool& out_makes_equality_pass) {
  FloatMargin expanded_float_margin = kDefaultFloatMargin;
  int ulp_bits = 0;
  ::testing::StringMatchResultListener listener;
  out_makes_equality_pass = false;
  while (true) {
    expanded_float_margin = FloatMargin::UlpMargin(ulp_bits);

    // Only trying. Do not call EXPECT_THAT() to fail the test. The
    // Returns() matcher will log a failure for every unsuccessful try,
    // though it won't fail the test. Check "Failures Summary" in the log
    // file to see all unsuccessful tries.
    if (ReturnsCheckOnly(actual, expanded_float_margin)
            .MatchAndExplain(expected, &listener)) {
      out_makes_equality_pass = true;
      break;
    }

    // Either we found a Ulp that worked and terminated the while-loop. Or
    // if we have already tried kMaxUlpBits, we should terminate the
    // while-loop here.
    if (max_ulp_bits <= ulp_bits) break;

    // The current Ulp bits didn't work, double it.
    if (ulp_bits == 0) {
      ulp_bits = FloatMargin::kDefaultUlpBits;
    } else {
      ulp_bits = (ulp_bits * 2 > max_ulp_bits) ? max_ulp_bits : ulp_bits * 2;
    }
  }
  if (ulp_bits > 0) {
    ABSL_LOG(INFO) << "Maximum Ulp bits tried: " << ulp_bits;
  }
  return expanded_float_margin;
}

// static
absl::Status SQLTestBase::ValidateFirstColumnPrimaryKey(
    const TestDatabase& test_db, const LanguageOptions& language_options) {
  for (const auto& [table_name, test_table] : test_db.tables) {
    // Do not check primary key column if it is a value table.
    if (test_table.options.is_value_table()) continue;

    GOOGLESQL_RETURN_IF_ERROR(googlesql::ValidateFirstColumnPrimaryKey(
        table_name, test_table.table_as_value, language_options));
  }
  return absl::OkStatus();
}

::testing::Matcher<const absl::StatusOr<ComplianceTestCaseResult>&>
SQLTestBase::ReturnsSuccess() {
  return ::testing::MakeMatcher(
      new KnownErrorFilter<absl::StatusOr<ComplianceTestCaseResult>>(
          this, absl::OkStatus(), ReturnsOk()));
}

::testing::Matcher<const absl::StatusOr<ComplianceTestCaseResult>&>
SQLTestBase::Returns(const ComplianceTestCaseResult& result,
                     const absl::Status& status, FloatMargin float_margin) {
  if (status.ok()) {
    return ::testing::MakeMatcher(
        new KnownErrorFilter<absl::StatusOr<ComplianceTestCaseResult>>(
            this, status,
            ReturnsStatusOrValue(
                absl::StatusOr<ComplianceTestCaseResult>(result),
                float_margin)));
  } else {
    return ::testing::MakeMatcher(
        new KnownErrorFilter<absl::StatusOr<ComplianceTestCaseResult>>(
            this, status,
            ReturnsStatusOrValue(
                absl::StatusOr<ComplianceTestCaseResult>(status),
                float_margin)));
  }
}

::testing::Matcher<const absl::StatusOr<ComplianceTestCaseResult>&>
SQLTestBase::Returns(const absl::StatusOr<ComplianceTestCaseResult>& result,
                     FloatMargin float_margin) {
  return ::testing::MakeMatcher(
      new KnownErrorFilter<absl::StatusOr<ComplianceTestCaseResult>>(
          this, result.status(), ReturnsStatusOrValue(result, float_margin)));
}

::testing::Matcher<const absl::StatusOr<ComplianceTestCaseResult>&>
SQLTestBase::ReturnsCheckOnly(
    const absl::StatusOr<ComplianceTestCaseResult>& result,
    FloatMargin float_margin) {
  return ::testing::MakeMatcher(
      new KnownErrorFilter<absl::StatusOr<ComplianceTestCaseResult>>(
          this, result.status(), ReturnsStatusOrValue(result, float_margin),
          /*check_only=*/true));
}

::testing::Matcher<const absl::StatusOr<ComplianceTestCaseResult>&>
SQLTestBase::Returns(absl::string_view result) {
  return ::testing::MakeMatcher(
      new KnownErrorFilter<absl::StatusOr<ComplianceTestCaseResult>>(
          this, absl::OkStatus(), ReturnsString(result)));
}

::testing::Matcher<const SQLTestBase::TestResults&> SQLTestBase::ToStringIs(
    absl::string_view result) {
  return ::testing::MakeMatcher(new KnownErrorFilter<TestResults>(
      this, absl::OkStatus(), IsTestResultsToString(result)));
}

::testing::Matcher<const absl::StatusOr<ComplianceTestCaseResult>&>
SQLTestBase::Returns(
    const ::testing::Matcher<const absl::StatusOr<ComplianceTestCaseResult>&>
        matcher) {
  return ::testing::MakeMatcher(
      new KnownErrorFilter<absl::StatusOr<ComplianceTestCaseResult>>(
          this, absl::OkStatus(), matcher));
}

static bool IsOnResolverErrorFilebasedAllowList(absl::string_view full_name) {
  // These prefixes are for filebased compliance tests that have analyzer error
  // queries in them. Such tests should be analyzer tests. As compliance tests,
  // they don't test any useful engine code. They also cause some noise in
  // compliance scores. By matching prefixes, we aren't fully preventing new
  // such cases from being added to these existing files, but at least we are
  // preventing new files from including such tests.
  // TODO: Clean up the ~900 tests, burn down this list.
  for (absl::string_view prefix :
       {"aggregation_queries_test:aggregation_string_agg_error_",
        "analytic_hll_count_test:analytic_hll_count_merge_partial_over_unbound",
        "analytic_lag_test:lag_window_frame",
        "analytic_lead_test:lead_window_frame",
        "analytic_row_number_test:row_number_3",
        "analytic_sum_test:analytic_sum_range_orderby_bool_",
        "anonymization_test:",
        "aggregation_threshold_test:",
        "arithmetic_functions_test:arithmetic_functions_3",
        "array_aggregation_test:array_concat_agg_array",
        "bytes_test:",
        "collation_test:",
        "d3a_count_test:",
        "geography_analytic_functions_test:st_clusterdbscan_",
        "geography_queries_2_test:",
        "hll_count_test:",
        "json_queries_test:json_",
        "keys_test:keys_2",
        "logical_functions_test:logical_not_",
        "proto2_unknown_enums_test:",
        "proto_fields_test:has_repeated_scalar_fields",
        "proto_constructor_test:",
        "proto3_fields_test:has_repeated_scalar_fields",
        "replace_fields_test:replace_fields_proto_named_extension",
        "strings_test:"}) {
    if (absl::StartsWith(full_name, prefix)) {
      return true;
    }
  }
  return false;
}

static bool IsOnResolverErrorCodebasedAllowList(absl::string_view full_name) {
  // These are codebased compliance tests that fail in the Resolver during
  // reference implementation tests. Tests that fail in the resolver should be
  // analyzer tests, not compliance tests. These stragglers are allow listed
  // because they were introduced before hygiene enforcement was added, and
  // require a more subtle fix.
  // Don't add to this list.
  // TODO: Fix these and enable enforcement of resolver success.
  return absl::StartsWith(full_name, "code:TablesampleRepeatable") ||
         absl::StartsWith(full_name, "code:Like_with_constant_pattern_");
}

static bool IsIdeallyEnabled(LanguageFeature feature) {
  using FeatureToEnabledMap = absl::flat_hash_map<LanguageFeature, bool>;
  static const FeatureToEnabledMap* kIsEnabledMap = []() {
    auto* is_enabled_map = new FeatureToEnabledMap;
    const google::protobuf::EnumDescriptor* enum_desc =
        google::protobuf::GetEnumDescriptor<LanguageFeature>();
    for (int i = 0; i < enum_desc->value_count(); ++i) {
      const google::protobuf::EnumValueDescriptor* value_desc = enum_desc->value(i);
      const LanguageFeatureOptions& feature_options =
          value_desc->options().GetExtension(language_feature_options);
      is_enabled_map->emplace(
          static_cast<LanguageFeature>(value_desc->number()),
          feature_options.ideally_enabled());
    }
    return is_enabled_map;
  }();
  return kIsEnabledMap->at(feature);
}

static bool IsInDevelopment(LanguageFeature feature) {
  using FeatureToInDevelopmentMap = absl::flat_hash_map<LanguageFeature, bool>;
  static const FeatureToInDevelopmentMap* kIsInDevelopmentMap = []() {
    auto* is_in_development = new FeatureToInDevelopmentMap;
    const google::protobuf::EnumDescriptor* enum_desc =
        google::protobuf::GetEnumDescriptor<LanguageFeature>();
    for (int i = 0; i < enum_desc->value_count(); ++i) {
      const google::protobuf::EnumValueDescriptor* value_desc = enum_desc->value(i);
      const LanguageFeatureOptions& feature_options =
          value_desc->options().GetExtension(language_feature_options);
      is_in_development->emplace(
          static_cast<LanguageFeature>(value_desc->number()),
          feature_options.in_development());
    }
    return is_in_development;
  }();
  return kIsInDevelopmentMap->at(feature);
}

static void ExtractComplianceLabelsFromResolvedAST(
    absl::string_view sql, const std::map<std::string, Value>& parameters,
    bool require_resolver_success, absl::string_view test_name,
    TypeFactory* type_factory, ReferenceDriver* reference_driver,
    const std::set<LanguageFeature>& required_features,
    const std::set<LanguageFeature>& forbidden_features,
    const absl::btree_set<std::string>& explicit_labels,
    absl::btree_set<std::string>& compliance_labels) {
  compliance_labels.clear();
  absl::string_view time_resolution_label = kTimeResolutionAnyLabel;
  for (const LanguageFeature feature : required_features) {
    if (!IsIdeallyEnabled(feature)) {
      compliance_labels.emplace(kLegacyBehaviorLabel);
    }
    if (IsInDevelopment(feature)) {
      compliance_labels.emplace(kInDevelopmentLabel);
    }
    LanguageVersion version = LanguageFeatureVersion(feature);
    std::string version_name = version == LANGUAGE_VERSION_UNSPECIFIED
                                   ? ""
                                   : LanguageVersion_Name(version);
    compliance_labels.insert(absl::StrCat("LanguageFeature:", version_name, ":",
                                          LanguageFeature_Name(feature)));
    if (feature == FEATURE_TIMESTAMP_NANOS) {
      time_resolution_label = kTimeResolutionNanosLabel;
    }
  }
  for (const LanguageFeature feature : forbidden_features) {
    if (IsIdeallyEnabled(feature) && feature != FEATURE_TIMESTAMP_NANOS) {
      compliance_labels.emplace(kLegacyBehaviorLabel);
    }
    // We don't do anything with in development features that are forbidden.
    if (feature == FEATURE_TIMESTAMP_NANOS) {
      time_resolution_label = kTimeResolutionMicrosLabel;
    }
  }
  compliance_labels.emplace(time_resolution_label);
  for (const std::string& label : explicit_labels) {
    if (label != test_name) {
      compliance_labels.insert(label);
    }
  }
  // What are the right LanguageOptions to use for extracting compliance labels?
  // They aren't what the engine supports because we need to see the
  // ResolvedAST nodes that appear in queries that aren't supported by the
  // engine. They must include "required_features" and must not include
  // "forbidden_features". What about ProductMode?  Actually, we would like to
  // include a label for only supported by PRODUCT_INTERNAL, only supported
  // PRODUCT_EXTERNAL, or supported in all product modes.
  AutoLanguageOptions options_cleanup(reference_driver);
  LanguageOptions language_options = reference_driver->language_options();
  language_options.SetEnabledLanguageFeatures({});
  language_options.SetSupportedStatementKinds({
      RESOLVED_QUERY_STMT,
      RESOLVED_INSERT_STMT,
      RESOLVED_UPDATE_STMT,
      RESOLVED_DELETE_STMT,
      RESOLVED_GENERALIZED_QUERY_STMT,
      RESOLVED_GENERALIZED_QUERY_SUBPIPELINE,
  });
  for (LanguageFeature feature : required_features) {
    language_options.EnableLanguageFeature(feature);
  }
  for (LanguageFeature feature : forbidden_features) {
    language_options.DisableLanguageFeature(feature);
  }
  language_options.EnableAllReservableKeywords();

  // Get a ResolvedAST for PRODUCT_INTERNAL.
  language_options.set_product_mode(PRODUCT_INTERNAL);
  reference_driver->SetLanguageOptions(language_options);
  std::optional<bool> product_internal_uses_unsupported_type = false;
  // TODO: Refactor ReferenceDriver::GetAnalyzerOptions to take
  //     LanguageOptions as an argument so we don't have to set the state and
  //     then re-set it using AutoLanguageOptions
  absl::StatusOr<AnalyzerOptions> product_internal_analyzer_options_or_err =
      reference_driver->GetAnalyzerOptions(
          parameters, product_internal_uses_unsupported_type);
  absl::Status product_internal_analyze_status;
  std::unique_ptr<const AnalyzerOutput> product_internal_analyzer_out;

  // For the purpose of compliance label extraction, rewriters should be
  // disabled by default.
  absl::btree_set<ResolvedASTRewrite> rewrites_for_label_extraction = {};
  if (absl::GetFlag(
          FLAGS_googlesql_compliance_extract_labels_using_all_rewrites)) {
    rewrites_for_label_extraction = internal::GetAllRewrites();
    rewrites_for_label_extraction.erase(REWRITE_INLINE_SQL_FUNCTIONS);
    rewrites_for_label_extraction.erase(REWRITE_INLINE_SQL_TVFS);
    rewrites_for_label_extraction.erase(REWRITE_INLINE_SQL_VIEWS);
    rewrites_for_label_extraction.erase(REWRITE_INLINE_SQL_UDAS);
  }

  if (product_internal_analyzer_options_or_err.ok()) {
    AnalyzerOptions analyzer_options =
        *product_internal_analyzer_options_or_err;
    analyzer_options.set_enabled_rewrites(rewrites_for_label_extraction);
    product_internal_analyze_status =
        AnalyzeStatement(sql, analyzer_options, reference_driver->catalog(),
                         type_factory, &product_internal_analyzer_out);
    if (product_internal_analyze_status.ok() &&
        !*product_internal_uses_unsupported_type) {
      // Check the plan for unsupported types too. Above we only checked params.
      product_internal_uses_unsupported_type =
          ReferenceDriver::UsesUnsupportedType(
              analyzer_options.language(),
              product_internal_analyzer_out->resolved_statement());
    }
  }

  // Repeat with mode PRODUCT_EXTERNAL
  language_options.set_product_mode(PRODUCT_EXTERNAL);
  reference_driver->SetLanguageOptions(language_options);
  std::optional<bool> product_external_uses_unsupported_type = false;
  absl::StatusOr<AnalyzerOptions> product_external_analyzer_options_or_err =
      reference_driver->GetAnalyzerOptions(
          parameters, product_external_uses_unsupported_type);
  absl::Status product_external_analyze_status;
  std::unique_ptr<const AnalyzerOutput> product_external_analyzer_out;
  if (product_external_analyzer_options_or_err.ok()) {
    AnalyzerOptions analyzer_options =
        *product_external_analyzer_options_or_err;
    analyzer_options.set_enabled_rewrites(rewrites_for_label_extraction);
    product_external_analyze_status =
        AnalyzeStatement(sql, analyzer_options, reference_driver->catalog(),
                         type_factory, &product_external_analyzer_out);
    if (product_external_analyze_status.ok() &&
        !*product_external_uses_unsupported_type) {
      // Check the plan for unsupported types too. Above we only checked params.
      product_external_uses_unsupported_type =
          ReferenceDriver::UsesUnsupportedType(
              analyzer_options.language(),
              product_external_analyzer_out->resolved_statement());
    }
  }

  bool internal_compiles = product_internal_analyzer_options_or_err.ok() &&
                           product_internal_analyze_status.ok() &&
                           !*product_internal_uses_unsupported_type;
  bool external_compiles = product_external_analyzer_options_or_err.ok() &&
                           product_external_analyze_status.ok() &&
                           !*product_external_uses_unsupported_type;
  const ResolvedStatement* statement = nullptr;
  auto add_labels_from = [&](const AnalyzerOutput& output) {
    return ExtractComplianceLabelsNoCompression(output, compliance_labels);
  };
  if (!internal_compiles && !external_compiles) {
    compliance_labels.emplace(kNoCompileLabel);
    if (require_resolver_success) {
      // We want to show at least one of the errors with the failure report to
      // help folks debug when they see this failure.
      absl::Status to_report = absl::OkStatus();
      to_report.Update(product_internal_analyzer_options_or_err.status());
      to_report.Update(product_internal_analyze_status);
      to_report.Update(product_external_analyzer_options_or_err.status());
      to_report.Update(product_external_analyze_status);
      ADD_FAILURE()
          << "Test '" << test_name << "' does not successfully compile "
          << "with maximum features plus required less forbidden. This test is "
          << "unhealthy. Probably it represents an analyzer failure test. "
          << "Analyzer failures should be tested in the analyzer tests, not "
          << "compliance tests, please move the test there. Another less "
          << "common possibility is that the test case should have forbidden "
          << "features that aren't currently explicit. In that case, "
          << "annotate the test with the appropriate forbidden feature.\n"
          << to_report << "\n";
    }
  } else if (internal_compiles && external_compiles) {
    compliance_labels.emplace(kProductModeInternalAndExternalLabel);
    // This is an arbitrary choice.
    statement = product_internal_analyzer_out->resolved_statement();
    GOOGLESQL_EXPECT_OK(add_labels_from(*product_internal_analyzer_out));
  } else if (internal_compiles && !external_compiles) {
    compliance_labels.emplace(kProductModeInternalLabel);
    statement = product_internal_analyzer_out->resolved_statement();
    GOOGLESQL_EXPECT_OK(add_labels_from(*product_internal_analyzer_out));
  } else if (!internal_compiles && external_compiles) {
    compliance_labels.emplace(kProductModeExternalLabel);
    statement = product_external_analyzer_out->resolved_statement();
    GOOGLESQL_EXPECT_OK(add_labels_from(*product_external_analyzer_out));
  } else {
    ABSL_LOG(FATAL) << "Unreachable";
  }
}

absl::StatusOr<ComplianceTestCaseResult> SQLTestBase::RunSQL(
    absl::string_view sql, const SQLTestCase::ParamsMap& params,
    bool permit_compile_failure) {
  GOOGLESQL_RET_CHECK(!IsFileBasedStatement()) << "This function is for codebased tests.";
  GOOGLESQL_RET_CHECK_EQ(statement_workflow_, NORMAL);
  sql_ = sql;
  parameters_ = params;
  full_name_ = GenerateCodeBasedStatementName(sql_, parameters_);

  absl::btree_set<std::string> labels = GetCodeBasedLabels();
  labels.insert(full_name_);
  effective_labels_ = labels;

  ABSL_LOG(INFO) << "Starting code-based test: " << full_name_;
  LogStrings(labels, "Effective labels: ");

  GOOGLESQL_RETURN_IF_ERROR(InspectTestCase());

  known_error_mode_ = IsKnownError(labels, &by_set_);
  if (KnownErrorMode::CRASHES_DO_NOT_RUN == known_error_mode_) {
    // Do not run CRASHES_DO_NOT_RUN.
    RecordKnownErrorStatement();
    return absl::CancelledError("Known Error");
  }
  return ExecuteTestCase().driver_output();
}

void SQLTestBase::RunSQLOnFeaturesAndValidateResult(
    absl::string_view sql, const std::map<std::string, Value>& params,
    const std::set<LanguageFeature>& required_features,
    const std::set<LanguageFeature>& forbidden_features,
    const Value& expected_value, const absl::Status& expected_status,
    const FloatMargin& float_margin) {
  full_name_ = GenerateCodeBasedStatementName(sql, params);

  ABSL_CHECK(!script_mode_) << "Codebased tests don't run in script mode.";
  // TODO: Refactor so that extract labels can be in known_errors.
  bool require_resolver_success =
      !IsOnResolverErrorCodebasedAllowList(full_name_) && IsVerifyingGoldens();
  EXPECT_FALSE(full_name_.empty());
  ExtractComplianceLabelsFromResolvedAST(
      sql, params, require_resolver_success, full_name_,
      reference_driver()->type_factory(), reference_driver(), required_features,
      forbidden_features, GetCodeBasedLabels(), compliance_labels_);

  if (IsVerifyingGoldens()) {
    auto* ref_driver = reference_driver();
    absl::Cleanup reset_language_options =
        [original = driver()->GetSupportedLanguageOptions(), ref_driver]() {
          ref_driver->SetLanguageOptions(original);
        };

    LanguageOptions language_options;
    language_options.SetEnabledLanguageFeatures(required_features);
    reference_driver()->SetLanguageOptions(language_options);

    auto run_result = RunSQL(sql, params, /*permit_compile_failure=*/false);
    EXPECT_THAT(run_result,
                Returns(expected_value, expected_status, float_margin))
        << "FullName: " << full_name() << "; "
        << "Labels: " << absl::StrJoin(GetCodeBasedLabels(), ", ");

    if (absl::GetFlag(FLAGS_googlesql_compliance_enforce_no_reference_label)) {
      if (absl::IsUnimplemented(run_result.status())) {
        EXPECT_TRUE(compliance_labels_.contains(kNoReferenceLabel))
            << "Test '" << full_name() << "' returns unimplemented from the "
            << "reference implementation and must be labeled with '"
            << kNoReferenceLabel << "'. " << run_result.status();
      } else {
        EXPECT_FALSE(compliance_labels_.contains(kNoReferenceLabel))
            << "Test '" << full_name()
            << "' does not return unimplemented from "
            << "the reference implementation and must not be labeled with '"
            << kNoReferenceLabel << "'. " << run_result.status();
      }
    }

    if (!absl::GetFlag(FLAGS_googlesql_detect_falsly_required_features) ||
        (test_case_options_ != nullptr &&
         test_case_options_->skip_required_feature_integrity_check())) {
      return;
    }

    // For each feature, assert that removing it makes the test fail. This
    // is a "falsely required" feature.
    absl::StatusOr<ComplianceTestCaseResult> result_to_check =
        expected_status.ok()
            ? absl::StatusOr<ComplianceTestCaseResult>(
                  ComplianceTestCaseResult(expected_value))
            : absl::StatusOr<ComplianceTestCaseResult>(expected_status);
    for (const auto& feature : required_features) {
      EXPECT_FALSE(IsFeatureFalselyRequired(
          feature, /*require_inclusive=*/true, sql, params, required_features,
          run_result.status(), result_to_check, float_margin))
          << "Feature is falsely required: " << full_name() << ": " << sql
          << ": " << LanguageFeature_Name(feature);
    }
    for (const auto& feature : forbidden_features) {
      EXPECT_FALSE(IsFeatureFalselyRequired(
          feature, /*require_inclusive=*/false, sql, params, required_features,
          run_result.status(), result_to_check, float_margin))
          << "Feature is falsely prohibited: " << full_name() << ": " << sql
          << ": " << LanguageFeature_Name(feature);
    }
  } else {
    bool driver_enables_right_features = true;
    for (LanguageFeature feature : required_features) {
      driver_enables_right_features &= DriverSupportsFeature(feature);
    }
    for (LanguageFeature feature : forbidden_features) {
      driver_enables_right_features &= !DriverSupportsFeature(feature);
    }

    if (!driver_enables_right_features) {
      stats_->RecordComplianceTestsLabelsProto(
          full_name_, sql, parameters_, location_,
          KnownErrorMode::ALLOW_UNIMPLEMENTED, compliance_labels_,
          expected_status.code(), absl::StatusCode::kUnimplemented);
      return;  // Skip this test.
    }
    // Only run once. Use reference engine to check result. We don't compare
    // the reference engine output against QueryParamsWithResult::results()
    // because it isn't clear what feature set to use.
    sql_ = sql;  // To supply a const std::string&
    ReferenceDriver::ExecuteStatementAuxOutput aux_output;
    absl::StatusOr<Value> reference_result =
        reference_driver()->ExecuteStatementForReferenceDriver(
            sql_, params, GetExecuteStatementOptions(), aux_output);
    if (aux_output.uses_unsupported_type.value_or(false)) {
      stats_->RecordComplianceTestsLabelsProto(
          full_name_, sql_, parameters_, location_,
          KnownErrorMode::ALLOW_UNIMPLEMENTED, compliance_labels_,
          expected_status.code(), absl::StatusCode::kUnimplemented);
      return;  // Skip this test. It uses types not supported by the driver.
    }
    SCOPED_TRACE(sql);
    // TODO: Should we do something with 'is_deterministic_output'?
    EXPECT_THAT(RunSQL(sql, params), Returns(reference_result, float_margin))
        << "FullName: " << full_name() << "; "
        << "Labels: " << absl::StrJoin(GetCodeBasedLabels(), ", ");
  }
}

static std::unique_ptr<ReferenceDriver> CreateTestSetupDriver() {
  LanguageOptions options;
  // For test setup (e.g., PrepareTable) always use PRODUCT_INTERNAL mode, since
  // a lot of test tables use internal types, and failure is non recoverable.
  options.set_product_mode(googlesql::ProductMode::PRODUCT_INTERNAL);
  // Enable all possible language features.
  // We deliberately do not enable language features in development, since it
  // may result in the generation of UDFs that rely on development features
  // which are not enabled by test drivers.
  options.EnableMaximumLanguageFeatures();
  // Required for some CREATE TABLE statements in file based tests, remove this
  // when type-linked language features are enabled by default.
  options.EnableLanguageFeature(FEATURE_COLLATION_SUPPORT);
  options.EnableLanguageFeature(FEATURE_MAP_TYPE);
  // Allow CREATE TABLE AS SELECT in [prepare_database] statements.
  options.AddSupportedStatementKind(RESOLVED_CREATE_TABLE_AS_SELECT_STMT);
  options.AddSupportedStatementKind(RESOLVED_CREATE_CONSTANT_STMT);
  options.AddSupportedStatementKind(RESOLVED_CREATE_TABLE_FUNCTION_STMT);
  options.AddSupportedStatementKind(RESOLVED_CREATE_FUNCTION_STMT);
  options.AddSupportedStatementKind(RESOLVED_CREATE_VIEW_STMT);

  auto driver = std::make_unique<ReferenceDriver>(options);
  // Create an empty database so that we can later load protos and enums.
  auto test_db_proto = std::make_unique<TestDatabaseProto>();
  auto serialize_status =
      SerializeTestDatabase(TestDatabase{}, test_db_proto.get());
  GOOGLESQL_DCHECK_OK(serialize_status);
  GOOGLESQL_CHECK_OK(driver->CreateDatabase(*test_db_proto));
  return driver;
}

SQLTestBase::SQLTestBase()
    : test_setup_driver_(CreateTestSetupDriver()),
      test_driver_owner_(GetComplianceTestDriver()),
      test_driver_(test_driver_owner_.get()),
      reference_driver_owner_(
          ReferenceDriver::CreateFromTestDriver(test_driver_).release()),
      reference_driver_(reference_driver_owner_.get()),
      execute_statement_type_factory_(std::make_unique<TypeFactory>()) {
  std::vector<std::string> known_error_files =
      absl::GetFlag(FLAGS_known_error_files);
  if (!known_error_files.empty()) {
    LoadKnownErrorFiles(known_error_files);
  }
}

SQLTestBase::SQLTestBase(TestDriver* test_driver,
                         ReferenceDriver* reference_driver)
    : test_setup_driver_(CreateTestSetupDriver()),
      test_driver_(test_driver),
      reference_driver_(reference_driver),
      execute_statement_type_factory_(std::make_unique<TypeFactory>()) {
  // Both drivers should be provided, or no driver should be provided.
  ABSL_CHECK_EQ(reference_driver_ != nullptr, test_driver_ != nullptr);
  // If both drivers are provided, they should be different objects so that
  // we don't need special conditions sprinkled around the setup code to handle
  // the case where setup is non-idempotent. It is only meta-tests of the test
  // framework and test-suite where it is tempting to make these the same
  // object.
  ABSL_CHECK(test_driver_ == nullptr || test_driver_ != reference_driver_);
  std::vector<std::string> known_error_files =
      absl::GetFlag(FLAGS_known_error_files);
  if (!known_error_files.empty()) {
    LoadKnownErrorFiles(known_error_files);
  }
}

SQLTestBase::~SQLTestBase() {}

const std::unique_ptr<file_based_test_driver::TestCaseOptions>&
SQLTestBase::options() {
  return test_file_options_->options_;
}

absl::Status SQLTestBase::CreateDatabase(const TestDatabase& test_db) {
  GOOGLESQL_RETURN_IF_ERROR(ValidateFirstColumnPrimaryKey(
      test_db, driver()->GetSupportedLanguageOptions()));

  auto test_db_proto = std::make_unique<TestDatabaseProto>();
  GOOGLESQL_RETURN_IF_ERROR(SerializeTestDatabase(test_db, test_db_proto.get()));

  absl::Status create_db_status = driver()->CreateDatabase(*test_db_proto);
  if (!create_db_status.ok()) {
    if (create_db_status.code() == absl::StatusCode::kUnimplemented &&
        create_db_status.message() ==
            "Test driver does not support creating database from proto") {
      // Temporary fallback to the in-memory overload, until external
      // drivers are all migrated to read from the proto.
      GOOGLESQL_RETURN_IF_ERROR(driver()->CreateDatabase(test_db));
    } else {
      return create_db_status;
    }
  }
  if (UseLeakyDescriptors()) {
    GOOGLESQL_RETURN_IF_ERROR(
        reference_driver()->CreateDatabaseWithLeakyDescriptors(test_db));
  } else {
    GOOGLESQL_RETURN_IF_ERROR(reference_driver()->CreateDatabase(*test_db_proto));
  }
  return absl::OkStatus();
}

absl::StatusOr<ComplianceTestCaseResult> SQLTestBase::ExecuteStatement(
    absl::string_view sql, const std::map<std::string, Value>& parameters) {
  return ExecuteTestCase().driver_output();
}

// Converts a multi-statement result to a single value if there is only one
// result.
static absl::StatusOr<SQLTestBase::ComplianceTestCaseResult>
ToValueIfSingleResult(absl::StatusOr<MultiStmtResult> multi_result) {
  if (!multi_result.ok()) {
    return multi_result.status();
  }
  const std::vector<StatementResult>& statement_results =
      multi_result->statement_results;
  if (statement_results.size() == 1) {
    const absl::StatusOr<Value>& result = statement_results[0].result;
    if (!result.ok()) {
      return result.status();
    }
    return result.value();
  }
  return *multi_result;
}

absl::StatusOr<SQLTestBase::ComplianceTestCaseResult>
SQLTestBase::ExecuteStatementReferenceDriver(
    ReferenceDriver::ExecuteStatementAuxOutput& aux_output) {
  std::unique_ptr<TestDatabase> test_db;
  if (test_case_options_ != nullptr &&
      test_case_options_->use_test_database_copy()) {
    // Creates a test database to verify statements with side effects, such
    // as DDL and DML statements. A copy is created so that test cases can
    // be executed independently.
    test_db = std::make_unique<TestDatabase>(test_db_);
  }
  return ToValueIfSingleResult(
      reference_driver()->ExecuteGeneralizedStatementForReferenceDriver(
          sql_, parameters_, GetExecuteStatementOptions(), aux_output,
          /*database=*/test_db.get()));
}

SQLTestBase::TestResults SQLTestBase::ExecuteTestCase() {
  sql_ = absl::StripAsciiWhitespace(sql_);

  // Splits this long log statement into smaller pieces that are less than
  // kLogBufferSize and ideally, split on newlines.
  std::string sql_log_string = absl::StrCat(
      "Running statement:\n  ", sql_,
      (parameters_.empty()
           ? "\nNo Parameters\n"
           : absl::StrCat("\nParameters:\n", ToString(parameters_))),
      "\n  Location: ", location_, "\n      Name: ", full_name_);
  for (::absl::string_view chunk :
       ::absl::StrSplit(sql_log_string, ::googlesql::LogChunkDelimiter())) {
    ABSL_LOG(INFO) << chunk;
  }
  GOOGLESQL_VLOG(1) << "Features: "
          << driver_language_options().GetEnabledLanguageFeaturesAsString();

  // A known error can still fail the test if it fails in a more
  // severe mode. Since the framework doesn't know the outcome yet, it defers
  // the recording till a later time, when
  // KnownErrorFilter::MatchAndExplain(...) is executed.
  if (!known_error_mode_) stats_->RecordExecutedStatement();

  // Time the statement execution time to gather some simple performance
  // metrics.
  absl::Time start_time = absl::Now();
  absl::StatusOr<ComplianceTestCaseResult> result;
  std::optional<bool> is_deterministic_output = std::nullopt;
  if (IsVerifyingGoldens()) {
    AutoLanguageOptions options_cleanup(reference_driver());
    reference_driver()->SetLanguageOptions(
        reference_driver_->language_options());
    if (script_mode_) {
      is_deterministic_output = true;
      ReferenceDriver::ExecuteScriptAuxOutput aux_output;
      result = reference_driver()->ExecuteScriptForReferenceDriver(
          sql_, parameters_, GetExecuteStatementOptions(), aux_output);
      is_deterministic_output = aux_output.is_deterministic_output;
    } else {
      ReferenceDriver::ExecuteStatementAuxOutput aux_output;
      result = ExecuteStatementReferenceDriver(aux_output);
      is_deterministic_output = aux_output.is_deterministic_output;
    }
  } else {
    if (script_mode_) {
      result = driver()->ExecuteScript(sql_, parameters_,
                                       execute_statement_type_factory());
    } else {
      result = ToValueIfSingleResult(driver()->ExecuteGeneralizedStatement(
          sql_, parameters_, execute_statement_type_factory()));
    }
  }
  stats_->RecordStatementExecutionTime(absl::Now() - start_time);
  return TestResults{result, is_deterministic_output};
}

void SQLTestBase::RunSQLTests(absl::string_view filename) {
  // The current test is a file-based test.
  stats_->StartFileBasedStatements();

  InitFileState();

  if (!file_based_test_driver::RunTestCasesFromFiles(
          filename, absl::bind_front(&SQLTestBase::RunTestFromFile, this))) {
    ABSL_LOG(ERROR) << "Encountered failures when testing file: " << filename;
  }

  // End of the file-based test.
  stats_->EndFileBasedStatements();
}

// This function is used as a callback from
// file_based_test_driver::RunTestCasesFromFiles
void SQLTestBase::RunTestFromFile(
    absl::string_view sql,
    file_based_test_driver::RunTestCaseResult* test_result) {
  InitStatementState(sql, test_result);
  StepPrepareTimeZoneProtosEnums();
  StepSkipUnsupportedTest();
  StepPrepareDatabase();
  StepCheckKnownErrors();
  StepCreateDatabase();
  SkipEmptyTest();
  StepExecuteStatementCheckResult();
}

absl::btree_set<std::string> SQLTestBase::GetCodeBasedLabels() {
  absl::btree_set<std::string> label_set;
  for (const std::string& label : code_based_labels_) {
    label_set.insert(label);
  }
  return label_set;
}

void SQLTestBase::InitFileState() {
  file_workflow_ = CREATE_DATABASE;
  test_db_.clear();
  test_file_options_ =
      std::make_unique<FilebasedSQLTestFileOptions>(test_setup_driver_.get());
}

void SQLTestBase::InitStatementState(
    absl::string_view sql,
    file_based_test_driver::RunTestCaseResult* test_result) {
  statement_workflow_ = NORMAL;
  sql_ = sql;
  parameters_.clear();
  full_name_ = "";
  known_error_mode_ = KnownErrorMode::NONE;
  by_set_.clear();
  test_result_ = test_result;
  effective_labels_.clear();
  compliance_labels_.clear();

  // A creating-table section does not have a name. Here we create a location
  // string without the name. Later on we will update the location string
  // when name is available.
  location_ = Location(test_result->filename(), test_result->line());

  std::string reason = "";
  auto status_or = test_file_options_->ProcessTestCase(sql, &reason);
  if (status_or.ok()) {
    test_case_options_ = std::move(*status_or);
    sql_ = test_case_options_->sql();
    parameters_ = test_case_options_->params();
  } else {
    ABSL_CHECK(!reason.empty()) << status_or.status();
    CheckCancellation(status_or.status(), reason);
  }
}

void SQLTestBase::StepSkipUnsupportedTest() {
  if (statement_workflow_ != NORMAL) {
    return;
  }

  // The reference implementation must be capable of running all tests for
  // verification of goldens. That doesn't mean it needs to run all tests when
  // running as the engine under test.
  if (IsVerifyingGoldens()) {
    return;
  }

  std::vector<std::string> skip_test_reasons;
  // The reference implementation can be configured to run tests with any
  // combination of language features enabled. All other engines will skip
  // tests that require an incompatible set of features.
  if (!driver()->IsReferenceImplementation()) {
    for (LanguageFeature required_feature :
         test_case_options_->required_features()) {
      if (!driver_language_options().LanguageFeatureEnabled(required_feature)) {
        skip_test_reasons.push_back(absl::StrCat(
            "Required feature, ", LanguageFeature_Name(required_feature),
            ", not supported by engine"));
        break;
      }
    }

    for (LanguageFeature forbidden_feature :
         test_case_options_->forbidden_features()) {
      if (driver_language_options().LanguageFeatureEnabled(forbidden_feature)) {
        skip_test_reasons.push_back(absl::StrCat(
            "Forbidden feature, ", LanguageFeature_Name(forbidden_feature),
            ", present in engine's feature list"));
        break;
      }
    }
  }

  absl::StatusOr<bool> status_or_skip_test_for_primary_key_mode =
      driver()->SkipTestsWithPrimaryKeyMode(
          test_case_options_->primary_key_mode());
  CheckCancellation(status_or_skip_test_for_primary_key_mode.status(),
                    "Failed to interpret primary key mode");
  if (statement_workflow_ == CANCELLED) {
    return;
  }

  if (status_or_skip_test_for_primary_key_mode.value()) {
    skip_test_reasons.push_back(
        "Driver requested to skip tests in primary-key mode");
  }

  if (absl::GetFlag(FLAGS_googlesql_compliance_accept_all_test_output)) {
    skip_test_reasons.push_back(
        "Command-line flag --googlesql_compliance_accept_all_test_output is "
        "set");
  }

  if (!skip_test_reasons.empty()) {
    ABSL_LOG(INFO) << "Test was skipped due to the following reasons:\n"
              << absl::StrJoin(skip_test_reasons, "\n  ");
    test_result_->set_ignore_test_output(true);
    statement_workflow_ = FEATURE_MISMATCH;
  }
}

void SQLTestBase::StepPrepareTimeZoneProtosEnums() {
  if (statement_workflow_ != NORMAL) return;
  // Handles default time zone first, because it may affect results of
  // statements that are used to generate parameters or table contents.
  if (!test_file_options_->default_timezone().empty()) {
    CheckCancellation(
        SetDefaultTimeZone(std::string(test_file_options_->default_timezone())),
        "Failed to set default time zone");
  }
  if (statement_workflow_ == CANCELLED) return;

  // Handles proto and enum loading second, because the table being created
  // may use these types.
  ABSL_CHECK(test_case_options_ != nullptr);
  if (!test_case_options_->proto_file_names().empty() ||
      !test_case_options_->proto_message_names().empty() ||
      !test_case_options_->proto_enum_names().empty()) {
    if (CREATE_DATABASE == file_workflow_) {
      CheckCancellation(LoadProtosAndEnums(), "Failed to load protos or enums");
    } else {
      absl::Status status(absl::StatusCode::kInvalidArgument,
                          "A group of [load_proto_files], [load_proto_names], "
                          "and [load_enum_names] must collocate with either a "
                          "[prepare_database] or the first statement.");
      CheckCancellation(status, "Wrong placement of load protos or enums");
    }
  }
}

bool SQLTestBase::AllowsFailedReferenceSetup() const {
  return test_case_options_->name() == kSkipFailedReferenceSetup;
}

absl::Status SQLTestBase::AddConstants(
    absl::Span<const std::string> create_constant_stmts, bool cache_stmts) {
  absl::Status reference_status =
      reference_driver()->AddSqlConstants(create_constant_stmts);
  if (!AllowsFailedReferenceSetup()) {
    GOOGLESQL_RETURN_IF_ERROR(reference_status);
  }
  absl::Status driver_status = driver()->AddSqlConstants(create_constant_stmts);
  if (!driver_status.ok()) {
    // We don't want to fail the test because of a database setup failure.
    // Any test statements that depend on this schema object should cause
    // the test to fail in a more useful way.
    ABSL_LOG(ERROR) << "Prepare database failed with error: " << driver_status;
  }
  if (cache_stmts && reference_status.ok() && driver_status.ok()) {
    for (const auto& stmt : create_constant_stmts) {
      constant_stmt_cache_.push_back(stmt);
    }
  }
  return absl::OkStatus();
}

absl::Status SQLTestBase::AddViews(
    absl::Span<const std::string> create_view_stmts, bool cache_stmts) {
  absl::Status reference_status =
      reference_driver()->AddViews(create_view_stmts);
  if (!AllowsFailedReferenceSetup()) {
    GOOGLESQL_RETURN_IF_ERROR(reference_status);
  }
  absl::Status driver_status = driver()->AddViews(create_view_stmts);
  if (!driver_status.ok()) {
    // We don't want to fail the test because of a database setup failure.
    // Any test statements that depend on this schema object should cause
    // the test to fail in a more useful way.
    ABSL_LOG(ERROR) << "Prepare database failed with error: " << driver_status;
  }
  if (cache_stmts && reference_status.ok() && driver_status.ok()) {
    for (const auto& stmt : create_view_stmts) {
      view_stmt_cache_.push_back(stmt);
    }
  }
  return absl::OkStatus();
}

absl::Status SQLTestBase::AddFunctions(
    absl::Span<const std::string> create_function_stmts, bool cache_stmts) {
  absl::Status reference_status =
      reference_driver()->AddSqlUdfs(create_function_stmts);
  if (!AllowsFailedReferenceSetup()) {
    GOOGLESQL_RETURN_IF_ERROR(reference_status);
  }
  absl::Status driver_status = driver()->AddSqlUdfs(create_function_stmts);
  if (!driver_status.ok()) {
    // We don't want to fail the test because of a database setup failure.
    // Any test statements that depend on this schema object should cause
    // the test to fail in a more useful way.
    ABSL_LOG(ERROR) << "Prepare database failed with error: " << driver_status;
  }
  if (cache_stmts && reference_status.ok() && driver_status.ok()) {
    for (const auto& stmt : create_function_stmts) {
      udf_stmt_cache_.push_back(stmt);
    }
  }
  return absl::OkStatus();
}

void SQLTestBase::AddTVF() {
  // Setup catalog with current test_db_ as property graph creation will be
  // based on existing tables.
  TypeFactory type_factory;
  TestDatabaseCatalog test_catalog;
  auto test_db_proto = std::make_unique<TestDatabaseProto>();
  CheckCancellation(SerializeTestDatabase(test_db_, test_db_proto.get()),
                    "Failed to serialize test database");
  CheckCancellation(test_catalog.SetTestDatabase(*test_db_proto),
                    "Failed to set test database before TVF creation");

  LanguageOptions lang_options;
  lang_options.SetEnabledLanguageFeatures(
      test_case_options_->required_features());
  lang_options.AddSupportedStatementKind(RESOLVED_CREATE_TABLE_FUNCTION_STMT);
  AnalyzerOptions analyzer_options(lang_options);
  CheckCancellation(
      test_catalog.SetLanguageOptions(analyzer_options.language()),
      "Failed to set language options on the dummy catalog before TVF "
      "creation");
  std::unique_ptr<const AnalyzerOutput> analyzer_output;
  CheckCancellation(AddTVFFromCreateTableFunction(
                        sql_, analyzer_options, /*allow_persistent=*/true,
                        analyzer_output, *test_catalog.catalog()),
                    "Failed to resolve CREATE TABLE FUNCTION statement");

  if (statement_workflow_ == CANCELLED) {
    return;
  }
  std::string tvf_name =
      IdentifierPathToString(analyzer_output->resolved_statement()
                                 ->GetAs<ResolvedCreateTableFunctionStmt>()
                                 ->name_path());
  auto [_, is_new] = test_db_.tvfs.insert({tvf_name, sql_});
  if (!is_new) {
    CheckCancellation(absl::InvalidArgumentError(
                          absl::StrFormat("TVF %s already exists", tvf_name)),
                      "Failed to create TVF");
  }
}

void SQLTestBase::StepPrepareDatabase() {
  if (test_case_options_ != nullptr &&
      !test_case_options_->prepare_database()) {
    // This is the first statement.
    file_workflow_ = FIRST_STATEMENT;
    return;
  }

  switch (statement_workflow_) {
    case NOT_A_TEST:
    case KNOWN_CRASH:
      ABSL_LOG(FATAL) << "Unexpected state in prepare database. "
                 << statement_workflow_;
    case CANCELLED:
      return;
    case SKIPPED:
    case FEATURE_MISMATCH:
      // We are going to skip this schema object, but it is important that
      // downstream steps don't assume it is a test.
      statement_workflow_ = NOT_A_TEST;
      return;
    case NORMAL:
      // [perpare_database] statements are not tests. Set this early so that
      // early returns from this function do not leave the statement_workflow_
      // in a NORMAL state.
      statement_workflow_ = NOT_A_TEST;
      break;
  }

  // In all cases where test_case_options_ is not set we should have returned
  // early.
  ABSL_CHECK(test_case_options_ != nullptr);

  // When running [prepare_database] statements with the reference driver, we
  // may need to enable extra features that the reference driver does not
  // enable by default, for instance features which are still marked as
  // in development.
  AutoLanguageOptions options_cleanup(reference_driver_);
  if (!test_case_options_->prepare_database_additional_features().empty()) {
    LanguageOptions language_options_copy =
        reference_driver_->language_options();
    for (LanguageFeature extra_feature :
         test_case_options_->prepare_database_additional_features()) {
      language_options_copy.EnableLanguageFeature(extra_feature);
    }
    reference_driver_->SetLanguageOptions(language_options_copy);
  }

  if (CREATE_DATABASE != file_workflow_) {
    absl::Status status(absl::StatusCode::kInvalidArgument,
                        "All [prepare_database] must be placed at the top "
                        "of a *.test file");
    CheckCancellation(status, "Wrong placement of prepare_database");
  }

  ResolvedNodeKind stmt_kind = GetStatementKind(sql_);

  if (stmt_kind == RESOLVED_CREATE_PROPERTY_GRAPH_STMT) {
    TypeFactory type_factory;
    // Setup catalog with current test_db_ as property graph creation will be
    // based on existing tables.
    TestDatabaseCatalog test_catalog;
    auto test_db_proto = std::make_unique<TestDatabaseProto>();
    CheckCancellation(SerializeTestDatabase(test_db_, test_db_proto.get()),
                      "Failed to serialize test database");
    CheckCancellation(
        test_catalog.SetTestDatabase(*test_db_proto),
        "Failed to set test database before property graph creation");
    LanguageOptions lang_options;
    lang_options.set_product_mode(googlesql::ProductMode::PRODUCT_INTERNAL);
    lang_options.EnableMaximumLanguageFeaturesForDevelopment();
    lang_options.AddSupportedStatementKind(RESOLVED_CREATE_PROPERTY_GRAPH_STMT);
    CheckCancellation(
        test_catalog.SetLanguageOptions(lang_options),
        "Failed to set language options before property graph creation");
    AnalyzerOptions options(lang_options);
    std::vector<std::unique_ptr<const AnalyzerOutput>> artifacts;
    CheckCancellation(AddPropertyGraphFromCreatePropertyGraphStmt(
                          sql_, options, artifacts, *test_catalog.catalog()),
                      "Failed to populate property graph");
    ABSL_DCHECK(!artifacts.empty());
    auto& analyzer_output = artifacts.front();
    ABSL_DCHECK(analyzer_output != nullptr);
    ABSL_DCHECK(analyzer_output->resolved_statement() != nullptr)
        << analyzer_output->resolved_node()->DebugString();
    ABSL_DCHECK(analyzer_output->resolved_statement()
               ->Is<ResolvedCreatePropertyGraphStmt>());
    auto name_path = analyzer_output->resolved_statement()
                         ->GetAs<ResolvedCreatePropertyGraphStmt>()
                         ->name_path();
    std::string graph_name = name_path.back();

    SQLBuilder::SQLBuilderOptions sql_builder_options(lang_options);
    SQLBuilder sql_builder(std::move(sql_builder_options));
    CheckCancellation(
        analyzer_output->resolved_statement()->Accept(&sql_builder),
        "SqlBuilder failed on the ResolvedCreatePropertyGraphStmt");
    auto [_, is_new] =
        test_db_.property_graph_defs.insert({graph_name, sql_builder.sql()});
    if (!is_new) {
      CheckCancellation(absl::InvalidArgumentError(absl::StrFormat(
                            "Property graph %s already exists", graph_name)),
                        "Failed to create graph DDL");
    }
    return;
  }

  if (stmt_kind == RESOLVED_CREATE_CONSTANT_STMT) {
    GOOGLESQL_EXPECT_OK(AddConstants({sql_}, /*cache_stmts=*/true));
    return;
  }
  if (stmt_kind == RESOLVED_CREATE_FUNCTION_STMT) {
    GOOGLESQL_EXPECT_OK(AddFunctions({sql_}, /*cache_stmts=*/true));
    return;
  }
  if (stmt_kind == RESOLVED_CREATE_VIEW_STMT) {
    GOOGLESQL_EXPECT_OK(AddViews({sql_}, /*cache_stmts=*/true));
    return;
  }

  if (stmt_kind == RESOLVED_CREATE_TABLE_AS_SELECT_STMT) {
    ReferenceDriver::ExecuteStatementAuxOutput aux_output;
    CheckCancellation(
        test_setup_driver_
            ->ExecuteStatementForReferenceDriver(
                sql_, parameters_, ReferenceDriver::ExecuteStatementOptions(),
                aux_output, &test_db_)
            .status(),
        "Failed to create table");
    if (statement_workflow_ == CANCELLED) return;
    const std::vector<std::string>& created_table_names =
        aux_output.created_table_names;
    EXPECT_LE(created_table_names.size(), 1);
    std::string table_name =
        created_table_names.empty() ? "" : created_table_names[0];
    ABSL_CHECK(googlesql_base::ContainsKey(test_db_.tables, table_name));
    *test_db_.tables[table_name].options.mutable_required_features() =
        test_case_options_->required_features();

    // Output to golden files for validation purpose.
    test_result_->AddTestOutput(
        test_db_.tables[table_name].table_as_value.Format());
    return;
  }

  if (stmt_kind == RESOLVED_CREATE_TABLE_FUNCTION_STMT) {
    AddTVF();
    return;
  }

  absl::Status status(
      absl::StatusCode::kInvalidArgument,
      "Only CREATE TABLE AS (SELECT...) statements and CREATE TEMP FUNCTION"
      " statements are supported for [prepare_database]");
  CheckCancellation(status, "Invalid CREATE TABLE statement");
}

void SQLTestBase::StepCheckKnownErrors() {
  if (statement_workflow_ == NOT_A_TEST || statement_workflow_ == CANCELLED ||
      statement_workflow_ == SKIPPED) {
    return;
  }

  absl::string_view name = test_case_options_->name();
  const std::string& filename = test_result_->filename();
  location_ = Location(filename, test_result_->line(), name);
  full_name_ = FileBasedTestName(filename, name);

  effective_labels_ =
      EffectiveLabels(full_name_, test_case_options_->local_labels(),
                      test_file_options_->global_labels());

  if (statement_workflow_ != NORMAL) {
    return;
  }

  if (!absl::GetFlag(FLAGS_query_name_pattern).empty()) {
    const RE2 regex(absl::GetFlag(FLAGS_query_name_pattern));
    ABSL_CHECK(regex.ok()) << "Invalid regex: "  // Crash OK
                      << absl::GetFlag(FLAGS_query_name_pattern)
                      << "; error: " << regex.error();
    if (!RE2::FullMatch(full_name_, regex)) {
      test_result_->set_ignore_test_output(true);
      statement_workflow_ = SKIPPED;
      return;
    }
  }

  known_error_mode_ = IsKnownError(effective_labels_, &by_set_);
  if (KnownErrorMode::CRASHES_DO_NOT_RUN == known_error_mode_) {
    RecordKnownErrorStatement();
    statement_workflow_ = KNOWN_CRASH;
  }
}

void SQLTestBase::StepCreateDatabase() {
  is_catalog_initialized_ = true;
  if (statement_workflow_ != NORMAL) return;

  if (!test_db_.empty()) {
    CheckCancellation(CreateDatabase(), "Failed to create database");
  }
}

void SQLTestBase::SkipEmptyTest() {
  // If "sql_" is empty, skip it. This allows sections containing only test
  // case options and comments.
  if (sql_.empty()) {
    test_result_->set_ignore_test_output(true);
    statement_workflow_ = NOT_A_TEST;
  }
}

SQLTestBase::TestResults::TestResults(
    absl::StatusOr<ComplianceTestCaseResult> driver_output,
    std::optional<bool> is_deterministic)
    : driver_output_(driver_output), is_deterministic_(is_deterministic) {}

absl::string_view SQLTestBase::TestResults::ToString() const {
  if (cached_to_string_.empty()) {
    cached_to_string_ = SQLTestBase::ToString(driver_output_);

    if (is_deterministic_.has_value() && !*is_deterministic_) {
      absl::StrAppend(
          &cached_to_string_,
          "\n\nNOTE: Reference implementation reports non-determinism.");
    }
  }
  return cached_to_string_;
}

void SQLTestBase::StepExecuteStatementCheckResult() {
  if (statement_workflow_ == NOT_A_TEST || statement_workflow_ == CANCELLED ||
      statement_workflow_ == SKIPPED) {
    return;
  }

  if (!InspectTestCase().ok()) {
    statement_workflow_ = CANCELLED;
    return;
  }

  bool should_extract_labels =
      // We don't include scripts in engine compliance report yet.
      !script_mode_
      // This case excludes some tests that use SQLTestBase but aren't
      // compliance tests. Such tests haven't initialized the requisite
      // data-structures.
      && is_catalog_initialized_;
  if (should_extract_labels) {
    bool require_resolver_success =
        !IsOnResolverErrorFilebasedAllowList(full_name_) &&
        // Only require success for reference right now. Failures in engine
        // test drivers (e.g. setting up the catalog) also cause these failures.
        IsVerifyingGoldens();
    EXPECT_FALSE(full_name_.empty()) << sql_;
    ExtractComplianceLabelsFromResolvedAST(
        sql_, parameters_, require_resolver_success, full_name_,
        reference_driver()->type_factory(), reference_driver(),
        test_case_options_->required_features(),
        test_case_options_->forbidden_features(), effective_labels_,
        compliance_labels_);
  } else {
    ABSL_LOG(INFO) << "Skip extracting compliance labels " << full_name_;
  }

  if (statement_workflow_ == FEATURE_MISMATCH) {
    stats_->RecordComplianceTestsLabelsProto(
        full_name_, sql_, parameters_, location_,
        KnownErrorMode::ALLOW_UNIMPLEMENTED, compliance_labels_,
        absl::StatusCode::kOk, absl::StatusCode::kUnimplemented);
  } else if (statement_workflow_ == KNOWN_CRASH) {
    stats_->RecordComplianceTestsLabelsProto(
        full_name_, sql_, parameters_, location_,
        KnownErrorMode::CRASHES_DO_NOT_RUN, compliance_labels_,
        absl::StatusCode::kOk, absl::StatusCode::kUnavailable);
  }

  if (statement_workflow_ != NORMAL) {
    return;
  }

  if (IsVerifyingGoldens()) {
    // Check results against golden files.
    // All features in [required_features] will be turned on.
    TestResults test_result =
        RunTestWithFeaturesEnabled(test_case_options_->required_features());

    if (absl::GetFlag(FLAGS_googlesql_compliance_enforce_no_reference_label)) {
      if (absl::IsUnimplemented(test_result.driver_output().status())) {
        EXPECT_TRUE(compliance_labels_.contains(kNoReferenceLabel))
            << "Test '" << full_name() << "' returns unimplemented from the "
            << "reference implementation and must be labeled with '"
            << kNoReferenceLabel << "'. "
            << test_result.driver_output().status();
      } else {
        EXPECT_FALSE(compliance_labels_.contains(kNoReferenceLabel))
            << "Test '" << full_name()
            << "' does not return unimplemented from "
            << "the reference implementation and must not be labeled with '"
            << kNoReferenceLabel << "'. "
            << test_result.driver_output().status();
      }
    }

    if (absl::GetFlag(FLAGS_googlesql_detect_falsly_required_features) &&
        (test_case_options_ == nullptr ||
         !test_case_options_->skip_required_feature_integrity_check())) {
      RunAndCompareTestWithoutEachRequiredFeatures(
          test_case_options_->required_features(), test_result);
    }

    ParseAndCompareExpectedResults(test_result);
  } else {
    // Check results against the reference implementation.
    test_result_->set_ignore_test_output(true);

    // This runs just once, with the LanguageOptions specified by the engine's
    // TestDriver, and compares to the reference impl results for those same
    // LanguageOptions.
    bool uses_unsupported_type = false;
    absl::StatusOr<ComplianceTestCaseResult> ref_result;
    if (script_mode_) {
      ReferenceDriver::ExecuteScriptAuxOutput aux_output;
      ref_result = reference_driver()->ExecuteScriptForReferenceDriver(
          sql_, parameters_, GetExecuteStatementOptions(), aux_output);
      uses_unsupported_type = aux_output.uses_unsupported_type.value_or(false);
    } else {
      ReferenceDriver::ExecuteStatementAuxOutput aux_output;
      ref_result = ExecuteStatementReferenceDriver(aux_output);
      uses_unsupported_type = aux_output.uses_unsupported_type.value_or(false);
    }
    if (uses_unsupported_type) {
      ABSL_LOG(INFO) << "Test skipped because it uses a type which is not supported "
                   "in the engine";
      stats_->RecordComplianceTestsLabelsProto(
          full_name_, sql_, parameters_, location_,
          KnownErrorMode::ALLOW_UNIMPLEMENTED, compliance_labels_,
          absl::StatusCode::kOk, absl::StatusCode::kUnimplemented);
      return;  // Skip this test. It uses types not supported by the driver.
    }
    if (absl::IsUnimplemented(ref_result.status())) {
      ABSL_LOG(INFO) << "Test skipped because the result is unimplemented in the "
                   "reference implementation: "
                << ref_result.status();
      // This test is not implemented by the reference implementation. Skip
      // checking the results because we have no results to check against.
      stats_->RecordComplianceTestsLabelsProto(
          full_name_, sql_, parameters_, location_,
          KnownErrorMode::ALLOW_UNIMPLEMENTED, compliance_labels_,
          absl::StatusCode::kUnimplemented, absl::StatusCode::kUnimplemented);
      return;
    }
    TestResults actual_result = ExecuteTestCase();
    SCOPED_TRACE(absl::StrCat("Testcase: ", full_name_, "\nSQL:\n", sql_));
    EXPECT_THAT(actual_result.driver_output(),
                Returns(ref_result, kDefaultFloatMargin));
  }
}

SQLTestBase::TestResults SQLTestBase::RunTestWithFeaturesEnabled(
    const std::set<LanguageFeature>& features_set) {
  LanguageOptions language_options =
      reference_driver()->GetSupportedLanguageOptions();
  language_options.SetEnabledLanguageFeatures(features_set);
  if (test_case_options_->reserve_match_recognize()) {
    GOOGLESQL_CHECK_OK(language_options.EnableReservableKeyword("MATCH_RECOGNIZE"));
  }
  if (test_case_options_->reserve_graph_table()) {
    GOOGLESQL_CHECK_OK(language_options.EnableReservableKeyword("GRAPH_TABLE"));
  }
  AutoLanguageOptions auto_options(reference_driver());
  reference_driver()->SetLanguageOptions(language_options);
  return ExecuteTestCase();
}

void SQLTestBase::RunAndCompareTestWithoutEachRequiredFeatures(
    const std::set<LanguageFeature>& required_features,
    TestResults& test_result) {
  for (const auto feature_to_check : required_features) {
    EXPECT_TRUE(IsFeatureRequired(feature_to_check, test_result))
        << LanguageFeature_Name(feature_to_check)
        << " was not actually required for " << full_name_ << "!";
  }
}

bool SQLTestBase::IsFeatureRequired(LanguageFeature feature_to_check,
                                    TestResults& test_result) {
  std::set<LanguageFeature> enabled_features =
      test_case_options_->required_features();
  enabled_features.erase(feature_to_check);
  TestResults new_result = RunTestWithFeaturesEnabled(enabled_features);
  return new_result.ToString() != test_result.ToString();
}

void SQLTestBase::ParseAndCompareExpectedResults(TestResults& test_result) {
  test_result_->AddTestOutput(std::string(test_result.ToString()));

  ABSL_DCHECK_EQ(statement_workflow_, NORMAL);
  if (test_case_options_->extract_labels()) {
    test_result_->AddTestOutput(absl::StrJoin(compliance_labels_, "\n"));
  }
  absl::string_view expected_string = "";
  if (test_result_->parts().size() >= 2) {
    expected_string = test_result_->parts()[1];
    expected_string = absl::StripSuffix(expected_string, "\n");
  }

  EXPECT_THAT(test_result, ToStringIs(std::string(expected_string)));
}

void SQLTestBase::CheckCancellation(const absl::Status& status,
                                    absl::string_view reason) {
  if (!status.ok()) {
    if (nullptr != test_result_) {
      // Code-based tests and random statements do not have test_result_
      // defined, in which case, no need to add test output.
      test_result_->AddTestOutput(absl::StrCat(
          reason, ": ", internal::StatusToString(status), " at ", location_));
    }

    statement_workflow_ = CANCELLED;
    if (known_error_mode_ < KnownErrorMode::ALLOW_ERROR) {
      // 1. known_error_mode_ = 0 < ALLOW_ERROR: A normal statement.
      // 2. 0 < known_error_mode_ < ALLOW_ERROR: A known error statement with
      // mode
      //    less severe than ALLOW_ERROR.
      // In either case, the statement fails the test.
      if (fail_unittest_on_cancelled_statement_) {
        GOOGLESQL_EXPECT_OK(status) << "  Reason: " << reason << " at " << location_;
      }
      stats_->RecordCancelledStatement(location_, full_name_, known_error_mode_,
                                       KnownErrorMode::ALLOW_ERROR, reason,
                                       internal::StatusToString(status));
    } else {
      RecordKnownErrorStatement();
      if (known_error_mode_ > KnownErrorMode::ALLOW_ERROR) {
        // A nontrivial known_error_mode_ implies the statement has a
        // full_name_. This statement can be upgraded.
        stats_->RecordToBeUpgradedStatement(full_name_, known_error_mode_,
                                            KnownErrorMode::ALLOW_ERROR);
      }
    }
  }
}

bool SQLTestBase::IsFeatureFalselyRequired(
    LanguageFeature feature, bool require_inclusive, absl::string_view sql,
    const std::map<std::string, Value>& param_map,
    const std::set<LanguageFeature>& required_features,
    const absl::Status& initial_run_status,
    const absl::StatusOr<ComplianceTestCaseResult>& expected_result,
    const FloatMargin& expected_float_margin) {
  LanguageOptions::LanguageFeatureSet features_minus_one(
      required_features.begin(), required_features.end());
  if (require_inclusive) {
    // this is the "required feature" case"
    ABSL_DCHECK(features_minus_one.contains(feature));
    features_minus_one.erase(feature);
  } else {
    // this is the "prohibited feature" case.
    ABSL_DCHECK(!features_minus_one.contains(feature));
    features_minus_one.insert(feature);
  }
  LanguageOptions language_options;
  language_options.SetEnabledLanguageFeatures(features_minus_one);
  reference_driver()->SetLanguageOptions(language_options);
  auto modified_run_result = RunSQL(sql, param_map);
  if (!modified_run_result.ok()) {
    // The test case is expecting an error with error message. If we see the
    // same expected error and message with and without 'feature', we can
    // conclude that 'feature' is not actually required.
    return modified_run_result.status() == initial_run_status;
  }

  if (absl::IsOutOfRange(initial_run_status) &&
      absl::IsInvalidArgument(modified_run_result.status())) {
    // This is an expected case where we are testing an evaluation time error in
    // the feature protected path, but without the flag we get a compiletime
    // error. We handle this as a special case because the 'ReturnsCheckOnly'
    // matcher is loose with these error codes. Since we are using the
    // complement of the matcher's result, its looseness becomes over-tightness
    // causing false postives.
    return false;
  }

  // The test case is expecting a result. If we see the same result with
  // and without 'feature', we can conclude that 'feature' is not
  // actually required.
  return ::testing::Value(
      modified_run_result,
      ReturnsCheckOnly(expected_result, expected_float_margin));
}

absl::Status SQLTestBase::AddKnownErrorEntry(
    const KnownErrorEntry& known_error_entry) {
  for (int j = 0; j < known_error_entry.label_size(); j++) {
    const std::string& label = known_error_entry.label(j);
    // Modes are ordered in severity. If a label presents in multiple
    // known error entries, the label's mode is the highest one.
    label_info_map_[label].mode =
        std::max(known_error_entry.mode(), label_info_map_[label].mode);
    // If the label is not a regex, store it directly.
    // Otherwise, store the compiled regex.
    if (SafeString(label) == label) {
      known_error_labels_.insert(label);
    } else if (googlesql_base::InsertIfNotPresent(&known_error_regex_strings_, label)) {
      std::unique_ptr<RE2> regex(new RE2(label));
      if (regex->ok()) {
        known_error_regexes_.push_back(std::move(regex));
      } else {
        return ::googlesql_base::UnknownErrorBuilder()
               << "Invalid known error regex " << label;
      }
    }

    if (!known_error_entry.reason().empty()) {
      label_info_map_[label].reason.insert(known_error_entry.reason());
    }
  }
  return absl::OkStatus();
}

void SQLTestBase::LoadKnownErrorFiles(absl::Span<const std::string> files) {
  for (const std::string& file : files) {
    GOOGLESQL_CHECK_OK(LoadKnownErrorFile(file));
  }

  if (GOOGLESQL_VLOG_IS_ON(3)) {
    LogStrings(known_error_labels_, "Known Error labels: ");
    LogStrings(known_error_regex_strings_, "Known Error regexes: ");
    LogReason();
    LogMode();
  }
}

absl::btree_set<std::string> SQLTestBase::EffectiveLabels(
    absl::string_view full_name, absl::Span<const std::string> labels,
    absl::Span<const std::string> global_labels) const {
  absl::btree_set<std::string> effective_labels;
  effective_labels.emplace(full_name);
  effective_labels.insert(labels.begin(), labels.end());
  effective_labels.insert(global_labels.begin(), global_labels.end());

  LogStrings(effective_labels, "Effective labels: ");

  return effective_labels;
}

template <typename ContainerType>
void SQLTestBase::LogStrings(const ContainerType& strings,
                             const std::string& prefix) const {
  const int flush_threshold = static_cast<int>(0.95 * kLogBufferSize);
  std::string buf;
  for (const auto& s : strings) {
    const int logged_size = s.size() + 2;
    const bool want_flush = buf.size() + logged_size > flush_threshold;
    if (!buf.empty() && want_flush) {
      ABSL_LOG(INFO) << prefix << buf;
      buf.clear();
    }
    absl::StrAppend(&buf, buf.empty() ? "" : ", ", s);
  }
  if (!buf.empty()) {
    ABSL_LOG(INFO) << prefix << buf;
  }
}

void SQLTestBase::AddCodeBasedLabels(std::vector<std::string> labels) {
  for (const std::string& label : labels) {
    code_based_labels_.emplace_back(label);
  }
}

void SQLTestBase::RemoveCodeBasedLabels(std::vector<std::string> labels) {
  for (std::vector<std::string>::const_reverse_iterator iter = labels.rbegin();
       iter != labels.rend(); iter++) {
    ABSL_CHECK_EQ(*iter, *(code_based_labels_.rbegin()))
        << "Found corrupted code-based labels. Always use "
           "auto label = MakeScopedLabel(...){...} to avoid this.";
    code_based_labels_.pop_back();
  }
}

std::string SQLTestBase::SafeString(absl::string_view str) const {
  return ReplaceChar(str, "[](){}.*+^$\\?|\"", kSafeChar);
}

std::string SQLTestBase::SignatureOfString(absl::string_view str) const {
  std::string escape = absl::CHexEscape(str);

  if (escape.size() <= kLengthOfLeftSlice + kLengthOfRightSlice) {
    std::string safe = SafeString(escape);
    return absl::StrCat("<", safe, ">");
  }
  uint64_t hash = farmhash::Fingerprint64(escape);
  uint64_t* hash_ptr = &hash;
  std::string hash_raw_str(reinterpret_cast<const char*>(hash_ptr),
                           sizeof(hash));
  std::string mid_raw = absl::WebSafeBase64Escape(hash_raw_str);
  ABSL_CHECK_EQ(mid_raw.size(), 11) << mid_raw;
  std::string mid = mid_raw.substr(0, 11);
  std::string left = SafeString(escape.substr(0, kLengthOfLeftSlice));
  std::string right = SafeString(
      escape.substr(escape.size() - kLengthOfRightSlice, kLengthOfRightSlice));
  return absl::StrCat("<", left, "_", mid, "_", right, ">");
}

std::string SQLTestBase::SignatureOfCompositeValue(const Value& value) const {
  std::string str = value.DebugString();
  absl::string_view trimmed_view;
  absl::Status status;
  ABSL_CHECK(googlesql::functions::TrimBytes(str, "[]{}", &trimmed_view, &status));
  return SignatureOfString(trimmed_view);
}

std::string SQLTestBase::ValueToSafeString(const Value& value) const {
  if (value.is_null()) return "_NULL";
  if (!value.is_valid()) return "_INVALID";
  std::string str;
  switch (value.type_kind()) {
    case TYPE_FLOAT:
    case TYPE_DOUBLE:
      str = absl::StrReplaceAll(value.DebugString(),
                                {{".", kSafeString}, {"+", ""}});
      return absl::StrCat("_", str);
    case TYPE_STRING:
      return absl::StrCat("_", SignatureOfString(value.string_value()));
    case TYPE_BYTES:
      return absl::StrCat("_", SignatureOfString(value.bytes_value()));
    case TYPE_TIMESTAMP: {
      // We use unix micros since the epoch when the result is an
      // even number of micros, otherwise we use the full string
      // value (i.e. 2006-01-02 03:04:05.123456789+00).
      absl::Time base_time = value.ToTime();
      int64_t nanos =
          absl::ToInt64Nanoseconds(absl::UTCTimeZone().At(base_time).subsecond);
      if (nanos % 1000 == 0) {
        return absl::StrCat("_", value.ToUnixMicros());
      }
      return absl::StrCat("_", SignatureOfCompositeValue(value));
    }
    case TYPE_GEOGRAPHY:
      return absl::StrCat("_", SignatureOfString(value.DebugString()));
    case TYPE_JSON:
      return absl::StrCat("_", SignatureOfString(value.DebugString()));
    case TYPE_ENUM:
      return absl::StrCat("_", value.DebugString());
    case TYPE_TOKENLIST:
      return absl::StrCat("_", SignatureOfString(value.DebugString()));
    case TYPE_DATE:
    case TYPE_ARRAY:
    case TYPE_STRUCT:
    case TYPE_PROTO:
      return absl::StrCat("_", SignatureOfCompositeValue(value));
    case TYPE_RANGE:
      return absl::StrFormat("_%s_%s", ValueToSafeString(value.start()),
                             ValueToSafeString(value.end()));
    case TYPE_MAP:
      return absl::StrCat("_", SignatureOfCompositeValue(value));
    default:
      // TODO: This debugstring needs to be escaped for regex unsafe
      //     characters. And probably spaces too?
      return absl::StrCat("_", value.DebugString());
  }
}

void SQLTestBase::SetNamePrefix(absl::string_view name_prefix,
                                bool need_result_type_name,
                                googlesql_base::SourceLocation loc) {
  location_ = Location(loc.file_name(), loc.line());
  name_prefix_ = name_prefix;
  name_prefix_need_result_type_name_ = need_result_type_name;
  result_type_name_.clear();
}

void SQLTestBase::SetResultTypeName(absl::string_view result_type_name) {
  result_type_name_ = result_type_name;
}

std::string SQLTestBase::GetNamePrefix() const {
  std::string name_prefix = name_prefix_;
  if (name_prefix.empty()) {
    // Uses test name as the prefix if not set.
    const ::testing::TestInfo* test_info =
        ::testing::UnitTest::GetInstance()->current_test_info();
    if (test_info != nullptr) {
      name_prefix = test_info->name();
    }
  }
  ABSL_CHECK(!name_prefix.empty())
      << "Found an empty name prefix. Always use SetNamePrefix(...) or "
      << "TEST_F(...) to avoid this.";
  if (name_prefix_need_result_type_name_) {
    ABSL_CHECK(!result_type_name_.empty())
        << "Name prefix " << name_prefix_
        << " needs a result type, but none was specified";
    absl::StrAppend(&name_prefix, "_", result_type_name_);
  }
  return absl::StrCat("code:", name_prefix);
}

std::string SQLTestBase::GenerateCodeBasedStatementName(
    absl::string_view sql,
    const std::map<std::string, Value>& parameters) const {
  std::vector<std::string> param_strs;
  std::string value;
  param_strs.reserve(parameters.size());
  for (const std::pair<const std::string, Value>& entry : parameters) {
    param_strs.emplace_back(absl::StrCat(
        absl::StrReplaceAll(entry.second.type()->TypeName(
                                product_mode(), use_external_float32()),
                            {{".", "_"}}),
        ValueToSafeString(entry.second)));
  }

  std::string name = absl::StrReplaceAll(GetNamePrefix(), {{".", "_"}});
  if (!param_strs.empty()) {
    absl::StrAppend(&name, "_", absl::StrJoin(param_strs, "_"));
  }
  // If the name is not safe then we cannot create known error entries.
  ABSL_CHECK(RE2::FullMatch(name, name)) << "Name is not RE2 safe " << name;
  return name;
}

std::string SQLTestBase::ToString(const TestResults& result) {
  return std::string(result.ToString());
}

// static
std::string SQLTestBase::ToString(
    const absl::StatusOr<ComplianceTestCaseResult>& status) {
  std::string result_string;
  if (!status.ok()) {
    // For file-based tests we do not care about the error message mode.
    absl::Status status_without_error_mode_payload = status.status();
    status_without_error_mode_payload.ErasePayload(kErrorMessageModeUrl);
    result_string =
        absl::StrCat("ERROR: ",
    internal::StatusToString(status_without_error_mode_payload));
  } else if (std::holds_alternative<Value>(status.value())) {
    const Value& value = std::get<Value>(status.value());
    ABSL_CHECK(!value.is_null());
    ABSL_CHECK(value.is_valid());
    if (format_value_content_options_ == nullptr) {
      result_string = InternalValue::FormatInternal(
          value, {.force_type_at_top_level = true,
                  .include_array_ordereness = absl::GetFlag(
                      FLAGS_googlesql_compliance_print_array_orderedness)});
    } else {
      result_string =
          InternalValue::FormatInternal(value, *format_value_content_options_);
    }
  } else {
    result_string =
        ScriptResultToString(std::get<ScriptResult>(status.value()));
  }
  absl::string_view trimmed_result;
  absl::Status ignored_status;
  ABSL_CHECK(googlesql::functions::RightTrimBytes(result_string, "\n",
                                             &trimmed_result, &ignored_status));
  return std::string(trimmed_result);
}

InternalValue::FormatValueContentOptions*
    SQLTestBase::format_value_content_options_ = nullptr;

void SQLTestBase::SetFormatValueContentOptions(
    InternalValue::FormatValueContentOptions options) {
  delete format_value_content_options_;
  format_value_content_options_ =
      new InternalValue::FormatValueContentOptions(std::move(options));
}

std::string SQLTestBase::ToString(
    const std::map<std::string, Value>& parameters) {
  std::vector<std::string> param_strs;
  param_strs.reserve(parameters.size());
  InternalValue::FormatValueContentOptions format_options;
  format_options.mode =
      InternalValue::FormatValueContentOptions::Mode::kSQLExpression;
  format_options.force_type_at_top_level = true;
  for (const std::pair<const std::string, Value>& entry : parameters) {
    param_strs.emplace_back(absl::StrCat(
        "  @", entry.first, " = ",
        InternalValue::FormatInternal(entry.second, format_options)));
  }
  std::sort(param_strs.begin(), param_strs.end());
  return absl::StrJoin(param_strs, "\n");
}

void SQLTestBase::LogReason() const {
  std::vector<std::string> ref;
  ref.reserve(label_info_map_.size());
  for (const std::pair<const std::string, LabelInfo>& entry : label_info_map_) {
    ref.emplace_back(absl::StrCat(
        entry.first, ": {", absl::StrJoin(entry.second.reason, ", "), "}"));
  }
  GOOGLESQL_VLOG(3) << "Reason: " << absl::StrJoin(ref, ", ");
}

void SQLTestBase::LogMode() const {
  std::vector<std::string> ref;
  ref.reserve(label_info_map_.size());
  for (const std::pair<const std::string, LabelInfo>& entry : label_info_map_) {
    ref.emplace_back(absl::StrCat(entry.first, ": ",
                                  KnownErrorMode_Name(entry.second.mode)));
  }
  GOOGLESQL_VLOG(3) << "Mode: " << absl::StrJoin(ref, ", ");
}

absl::Status SQLTestBase::LoadKnownErrorFile(absl::string_view filename) {
  std::string text;
  absl::Status status = internal::GetContents(filename, &text);
  if (!status.ok()) {
    if (status.code() == absl::StatusCode::kNotFound) {
      return ::googlesql_base::InvalidArgumentErrorBuilder()
             << "ERROR: Known Error file " << filename << " does not exist";
    } else {
      return ::googlesql_base::InvalidArgumentErrorBuilder()
             << "ERROR: Unable to get known error contents from " << filename
             << ": " << internal::StatusToString(status);
    }
  }

  KnownErrorFile known_error_file;
  if (!google::protobuf::TextFormat::ParseFromString(text, &known_error_file)) {
    return ::googlesql_base::InvalidArgumentErrorBuilder()
           << "ERROR: Failed to parse known error file " << filename;
  }

  for (int i = 0; i < known_error_file.known_errors_size(); i++) {
    GOOGLESQL_RETURN_IF_ERROR(AddKnownErrorEntry(known_error_file.known_errors(i)));
  }

  return absl::OkStatus();
}

KnownErrorMode SQLTestBase::IsKnownError(
    const absl::btree_set<std::string>& effective_labels,
    absl::btree_set<std::string>* by_set) const {
  by_set->clear();

  KnownErrorMode mode = KnownErrorMode::NONE;
  KnownErrorMode individual_mode = KnownErrorMode::NONE;

  for (const std::string& label : effective_labels) {
    if (known_error_labels_.contains(label)) {
      individual_mode = googlesql_base::FindOrDie(label_info_map_, label).mode;
      mode = std::max(mode, individual_mode);
      by_set->insert(label);
      ABSL_LOG(INFO) << "Statement matches known error by label: " << label
                << " in mode: " << KnownErrorMode_Name(individual_mode);
    }

    for (const std::unique_ptr<RE2>& regex : known_error_regexes_) {
      if (RE2::FullMatch(label, *regex)) {
        individual_mode =
            googlesql_base::FindOrDie(label_info_map_, regex->pattern()).mode;
        mode = std::max(mode, individual_mode);
        by_set->insert(regex->pattern());
        ABSL_LOG(INFO) << "Statement matches known error by regex: "
                  << regex->pattern()
                  << " in mode: " << KnownErrorMode_Name(individual_mode);
      }
    }
  }

  // In --report_all_errors mode, ignore entries unless they are
  // CRASHES_DO_NOT_RUN mode.
  if (absl::GetFlag(FLAGS_report_all_errors) &&
      mode != KnownErrorMode::CRASHES_DO_NOT_RUN) {
    mode = KnownErrorMode::NONE;
    by_set->clear();
  }

  return mode;
}

void SQLTestBase::RecordKnownErrorStatement(KnownErrorMode mode) {
  // 'test_result_' is not set in a code-based test.
  if (test_result_ != nullptr) {
    // Among other things, 'ignore_test_output' flag controls the 'TEST_OUTPUT'
    // section in a log file. When set to false, statement result will be
    // outputted to the 'TEST_OUTPUT' section. When set to true, statement
    // result is ignored and the golden is copied into the 'TEST_OUTPUT'
    // section. Because the golden tool uses the 'TEST_OUTPUT' section to update
    // goldens, setting 'ignore_test_output' to true prevents a "known error"
    // statement (which usually generates wrong test result) from polluting the
    // goldens.
    //
    // Note that this only applies to the reference engine, which is tested
    // against the golden. Other engines are tested against the reference
    // engine, and 'ignore_test_output' is always set to true for other engines.
    // See the 'else' branch in StepExecuteStatementCheckResult().
    test_result_->set_ignore_test_output(true);
  }
  stats_->RecordKnownErrorStatement(location_, mode, by_set_);
}

std::string SQLTestBase::Location(absl::string_view filename, int line_num,
                                  absl::string_view name) const {
  if (name.empty()) {
    return absl::StrCat("FILE-", googlesql_base::Basename(filename), "-LINE-", line_num);
  } else {
    return absl::StrCat("FILE-", googlesql_base::Basename(filename), "-LINE-", line_num,
                        "-NAME-", name);
  }
}

ProductMode SQLTestBase::product_mode() const {
  return driver()->GetSupportedLanguageOptions().product_mode();
}

bool SQLTestBase::use_external_float32() const {
  return driver()->GetSupportedLanguageOptions().product_mode() ==
             ProductMode::PRODUCT_EXTERNAL &&
         !driver()->GetSupportedLanguageOptions().LanguageFeatureEnabled(
             LanguageFeature::FEATURE_DISABLE_FLOAT32);
}

void SQLTestBase::SetUp() { GOOGLESQL_EXPECT_OK(CreateDatabase(TestDatabase{})); }

// Add predefined tables with measure columns to `test_db` if the test case
// requires the FEATURE_ENABLE_MEASURES feature.
// It is currently impossible to define measures via SQL or use DDL to add
// measure columns to a table. This makes compliance testing for measures
// difficult; as a result we use this temporary workaround to add predefined
// tables with measure columns to the test database.
// TODO: b/350555383 - Remove this function once we can define measures via SQL
// or use DDL to add measure columns to a table.
void SQLTestBase::MaybeAddMeasureTables(
    const FilebasedSQLTestCaseOptions& test_case_options,
    TestDatabase& test_db) {
  const bool add_tables_with_measures =
      test_case_options.required_features().find(
          LanguageFeature::FEATURE_ENABLE_MEASURES) !=
      test_case_options.required_features().end();
  if (!add_tables_with_measures) {
    return;
  }

  const bool add_measures_with_udas =
      test_case_options.required_features().find(
          LanguageFeature::FEATURE_MULTILEVEL_AGGREGATION_ON_UDAS) !=
      test_case_options.required_features().end();

  TestDatabase measure_db =
      GetMeasureTablesTestDatabase(add_measures_with_udas);

  // Adds the measure tables and measure function definitions to the output test
  // database.
  for (auto& [table_name, table] : measure_db.tables) {
    *table.options.mutable_required_features() =
        test_case_options.required_features();
  }
  test_db.tables.insert(measure_db.tables.begin(), measure_db.tables.end());
  test_db.measure_function_defs.insert(test_db.measure_function_defs.end(),
                                       measure_db.measure_function_defs.begin(),
                                       measure_db.measure_function_defs.end());
}

absl::Status SQLTestBase::CreateDatabase() {
  // Workaround for adding tables with measure columns to the test database.
  // TODO: b/350555383 - Remove this workaround once we can define measures via
  // SQL or use DDL to add measure columns to a table.
  MaybeAddMeasureTables(*test_case_options_, test_db_);

  GOOGLESQL_RETURN_IF_ERROR(CreateDatabase(test_db_));

  if (!constant_stmt_cache_.empty()) {
    GOOGLESQL_RETURN_IF_ERROR(AddConstants(constant_stmt_cache_, /*cache_stmts=*/false));
  }

  if (!udf_stmt_cache_.empty()) {
    GOOGLESQL_RETURN_IF_ERROR(AddFunctions(udf_stmt_cache_, /*cache_stmts=*/false));
  }
  if (!view_stmt_cache_.empty()) {
    GOOGLESQL_RETURN_IF_ERROR(AddViews(view_stmt_cache_, /*cache_stmts=*/false));
  }

  // Only create test database once.
  test_db_.clear();

  return absl::OkStatus();
}

absl::Status SQLTestBase::LoadProtosAndEnums() {
  const std::set<std::string>& files = test_case_options_->proto_file_names();
  const std::set<std::string>& protos =
      test_case_options_->proto_message_names();
  const std::set<std::string>& enums = test_case_options_->proto_enum_names();

  if (!files.empty() || !protos.empty() || !enums.empty()) {
    test_db_.proto_files.insert(files.begin(), files.end());
    test_db_.proto_names.insert(protos.begin(), protos.end());
    test_db_.enum_names.insert(enums.begin(), enums.end());
    GOOGLESQL_RETURN_IF_ERROR(
        reference_driver_->LoadProtoEnumTypes(files, protos, enums));
  }

  return absl::OkStatus();
}

absl::Status SQLTestBase::SetDefaultTimeZone(
    const std::string& default_time_zone) {
  GOOGLESQL_RETURN_IF_ERROR(test_setup_driver_->SetDefaultTimeZone(default_time_zone));
  GOOGLESQL_RETURN_IF_ERROR(test_driver_->SetDefaultTimeZone(default_time_zone));
  GOOGLESQL_RETURN_IF_ERROR(reference_driver_->SetDefaultTimeZone(default_time_zone));
  return absl::OkStatus();
}

bool SQLTestBase::IsVerifyingGoldens() const {
  return absl::GetFlag(FLAGS_googlesql_verify_compliance_goldens);
}

TypeFactory* SQLTestBase::table_type_factory() {
  return test_setup_driver_->type_factory();
}

void SQLTestBase::ResetExecuteStatementTypeFactory() {
  execute_statement_type_factory_ = std::make_unique<TypeFactory>();
}

ReferenceDriver::ExecuteStatementOptions
SQLTestBase::GetExecuteStatementOptions() const {
  ReferenceDriver::ExecuteStatementOptions options;
  options.primary_key_mode = test_case_options_ == nullptr
                                 ? PrimaryKeyMode::DEFAULT
                                 : test_case_options_->primary_key_mode();
  return options;
}

LanguageOptions SQLTestBase::driver_language_options() {
  return driver()->GetSupportedLanguageOptions();
}

bool SQLTestBase::DriverSupportsFeature(LanguageFeature feature) {
  const bool enabled =
      driver_language_options().LanguageFeatureEnabled(feature);
  if (IsVerifyingGoldens()) {
    // If the tests depend on whether some feature is enabled in the reference
    // implementation, and that feature is disabled, then something is
    // probably wrong. This ABSL_CHECK helps prevent tests from being silently
    // skipped.
    EXPECT_TRUE(enabled) << LanguageFeature_Name(feature);
  }
  return enabled;
}

bool SQLTestBase::IsFileBasedStatement() const {
  return stats_->IsFileBasedStatement();
}

MatcherCollection<absl::Status>* SQLTestBase::legal_runtime_errors() {
  if (legal_runtime_errors_ == nullptr) {
    legal_runtime_errors_ = LegalRuntimeErrorMatcher("LegalRuntimeErrors");
  }
  return legal_runtime_errors_.get();
}

// Collects global stats.
class SQLTestEnvironment : public ::testing::Environment {
 public:
  // Set up global resource SQLTestBase::stats_.
  void SetUp() override { SQLTestBase::stats_ = std::make_unique<Stats>(); }

  // Call Stats::LogGoogletestProperties() to output statistics to continuous
  // integration environments.
  //
  // Call Stats::LogReport() to output all failed statements to the log file
  // in the format:
  //
  //   ===== To Be Added To Known Errors Statements =====
  //     label: "<filename>:<statement_name>"
  //     label: "code:<test_name>"
  //     ...
  //   === End To Be Added To Known Errors Statements ===
  //
  // The labels can be copied/pasted into a known_errors file.
  void TearDown() override {
    SQLTestBase::stats_->LogGoogletestProperties();
    SQLTestBase::stats_->LogReport();
    if (absl::GetFlag(FLAGS_googlesql_compliance_write_labels_to_file)) {
      SQLTestBase::stats_->WriteComplianceLabels();
    }
  }
};

// Enable human-readable display of ScriptResult objects in Sponge logs of
// failing tests.
std::ostream& operator<<(std::ostream& os, const ScriptResult& result) {
  return os << SQLTestBase::ToString(result);
}

namespace {
static bool module_initialization_complete = []() {
  ::testing::AddGlobalTestEnvironment(new SQLTestEnvironment);
  return true;
} ();
}  // namespace

}  // namespace googlesql
