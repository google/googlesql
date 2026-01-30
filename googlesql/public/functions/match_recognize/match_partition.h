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

#ifndef GOOGLESQL_PUBLIC_FUNCTIONS_MATCH_RECOGNIZE_MATCH_PARTITION_H_
#define GOOGLESQL_PUBLIC_FUNCTIONS_MATCH_RECOGNIZE_MATCH_PARTITION_H_

#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "googlesql/public/value.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace googlesql::functions::match_recognize {

// Options to control matching behavior, corresponding to various elements in
// the MATCH_RECOGNIZE clause.
struct MatchOptions {
  // Evaluates query parameters which were unavailable at compile time.
  //
  // If a parameter_evaluator was already supplied in the PatternOptions used
  // to generate the CompiledPattern, this field should be nullptr.
  //
  // If parameter_evaluator is nullptr both here and in the PatternOptions,
  // matching will still work, however, the use of query parameters as
  // quantifier bounds will not be supported.
  std::function<absl::StatusOr<Value>(
      std::variant<int, absl::string_view> param_name_or_index)>
      parameter_evaluator;
};

struct Match {
  // A number identifying this match. This is the value that the MATCH_NUMBER()
  // function will evaluate to inside the MEASURES clause.
  //
  // Match id numbers are sorted in increasing order of start_row_index;
  // they start at 1 at the beginning of the partition and advance
  // sequentially. Empty matches still advance the match id, even if not
  // requested in the results.
  int match_id = -1;

  // Zero-based index of the match's starting row in the partition.
  int start_row_index = -1;

  // Identifies which pattern variable was actually matched by each row in the
  // match.
  //
  // Each element is a zero based index into the pattern_var_list field of
  // the ResolvedMatchRecognizeScan, which can be used to determine the name of
  // the variable. The length of the 'pattern_vars_by_row' vector denotes the
  // length of the match. Element 0 in the vector is for the row at
  // `start_row_index`.
  std::vector<int> pattern_vars_by_row;
};

struct MatchResult {
  // New matches detected, not yet reported in prior calls to AddRow().
  //
  // Note: As matches are reported only when their bounds and matched pattern
  // variables have been fully determined, sometimes, the reporting of a match
  // may be delayed until several rows after the match actually ends. The caller
  // should be prepared for this, and should not expect matches to end at the
  // current row.
  std::vector<Match> new_matches;
};

// Represents a partition which contains a row sequence used to match against
// a compiled pattern.
//
// Sample usage:
//   bool done = false;
//   while (!done) {
//     if (GetNextRow(row)) {
//        <save the current row for later use when it is part of a match>
//        GOOGLESQL_ASSIGN_OR_RETURN(match_result,
//            match_partition.AddRow(EvaluateDefineExprs(row));
//        done = !match_result.additional_matches_possible;
//     } else {
//        GOOGLESQL_ASSIGN_OR_RETURN(match_result, match_partition.Finalize());
//        done = true;
//     }
//     for (const Match& match : match_result.new_matches) {
//        // We have a match from row `match.start_row_index` (inclusive) to
//        // row `match.start_row_index + match.pattern_vars_by_row.size()`
//        // (exclusive). Loop over all saved rows inside the match and
//        // aggregate the MEASURES expressions, using the `pattern_vars_by_row`
//        // field to determine which rows apply to which aggregations.
//     }
//   }
//
class MatchPartition {
 public:
  virtual ~MatchPartition() = default;
  // TODO: Add a mechanism to prevent a single AddRow() or
  // Finalize() call from using too much memory from reporting too many matches
  // all at once. This can be done through either adding fields to MatchOptions
  // or by adding additional functions to the MatchPartition class to retrieve
  // the match results in batches.

  // Adds a row to be included as part of the input, after all rows from prior
  // AddRow() calls.
  //
  // Each row is presented as a list of boolean values, sorted according to the
  // order of the pattern variables in the `pattern_var_list` field of the
  // ResolvedMatchRecognizeScan (that is, the order that the pattern variables
  // are defined in the DEFINE clause).
  //
  // Pattern variables which require evaluation using a match-dependent window
  // are unsupported.
  virtual absl::StatusOr<MatchResult> AddRow(
      const std::vector<bool>& row_pattern_variables) = 0;

  // Notifies the matcher that no additional rows will be added. After this
  // call, AddRow() and Finalize() cannot be called again.
  //
  // Note: Omitting the Finalize() call is allowed if a prior MatchResult
  // has `additional_matches_possible` set to false.
  virtual absl::StatusOr<MatchResult> Finalize() = 0;

  // Returns a string describing the internal state of the match window.
  virtual std::string DebugString() const = 0;
};
}  // namespace googlesql::functions::match_recognize
#endif  // GOOGLESQL_PUBLIC_FUNCTIONS_MATCH_RECOGNIZE_MATCH_PARTITION_H_
