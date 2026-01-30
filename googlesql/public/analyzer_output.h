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

#ifndef GOOGLESQL_PUBLIC_ANALYZER_OUTPUT_H_
#define GOOGLESQL_PUBLIC_ANALYZER_OUTPUT_H_

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "googlesql/base/arena.h"
#include "googlesql/common/timer_util.h"
#include "googlesql/parser/parser.h"
#include "googlesql/parser/parser_runtime_info.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output_properties.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/proto/logging.pb.h"
#include "googlesql/public/types/type.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/time/time.h"

namespace googlesql {

// Performance breakdown of the analyzer.
//
// Logically this separates Analysis into the following non-overlapping spans:
//  - Parser                                                [parser_elapsed]
//  - Resolver                                              [resolver_elapsed]
//    - Catalog Calls (not tracked yet)
//    - re-entrant calls to Analyzer (not tracked separately)
//  - Rewriters                                             [rewriters_elapsed]
//    - Pass 1
//      - Rewriter A                                        [rewriter_detail[A]]
//      - Rewriter B                                        [rewriter_detail[B]]
//    ...
//    - Pass N
//      - Rewriter 1
//      - Rewriter 2
//  - Validator                                             [validator_elapsed]
//
// Caveats: When the analyzer is invoked with an already parsed input
//          it will preserve the ParserRuntimeInfo if that was provided.
//
// Time spent in the validator is not counted in either rewriter or resolver.
//
// There overall_timer provides a measure of the total resources spent.
class AnalyzerRuntimeInfo {
 public:
  AnalyzerRuntimeInfo() : impl_(std::make_unique<Impl>()) {}

  AnalyzerRuntimeInfo(AnalyzerRuntimeInfo&&) = default;
  AnalyzerRuntimeInfo(const AnalyzerRuntimeInfo&);
  AnalyzerRuntimeInfo& operator=(const AnalyzerRuntimeInfo&);

  // This adds up all of the independent spans of time to produce an
  // appoximation for the total time spent performing analysis.  Note
  // that the parser _may_ be called outside of the analyzer, which could
  // result in overcounting of parser time, as well as this value.
  // see parser_elapsed_duration for more information.
  absl::Duration sum_elapsed_duration() const {
    return parser_runtime_info().parser_timed_value().elapsed_duration() +
           resolver_timed_value().elapsed_duration() +
           rewriters_timed_value().elapsed_duration() +
           validator_timed_value().elapsed_duration();
  }

  const ParserRuntimeInfo& parser_runtime_info() const {
    return impl_->parser_runtime_info;
  }
  ParserRuntimeInfo& parser_runtime_info() {
    return impl_->parser_runtime_info;
  }

  // Total time in this GoogleSQL API call.
  internal::TimedValue& overall_timed_value() const {
    return impl_->overall_timed_value;
  }

  internal::TimedValue& resolver_timed_value() const {
    return impl_->resolver_timed_value;
  }

  internal::TimedValue& rewriters_timed_value() const {
    return impl_->rewriters_timed_value;
  }

  struct RewriterDetails {
    size_t count;
    internal::TimedValue timed_value;

    absl::Duration elapsed_duration() const {
      return timed_value.elapsed_duration();
    }
    void AccumulateAll(const RewriterDetails& rhs);
  };
  RewriterDetails& rewriters_details(ResolvedASTRewrite rewriter) {
    return impl_->rewriters_details[rewriter];
  }
  const RewriterDetails& rewriters_details(ResolvedASTRewrite rewriter) const;

  internal::TimedValue& validator_timed_value() const {
    return impl_->validator_timed_value;
  }

  void AccumulateAll(const AnalyzerRuntimeInfo& rhs);

  AnalyzerLogEntry log_entry() const;

  // Print a human readable representation of this object.
  // If total_runs is provided, printed information will be an average over the
  // total runs.
  std::string DebugString(std::optional<int> total_runs) const;

 private:
  // We use a p-impl style implementation to move the storage onto the heap.
  // This this object is somewhat large, appears on the stack multiple
  // times in the analyzer, and the analyzer can be invoked recursively
  // (such as for lazy module catalog constrution), we use this technique
  // in this case.
  struct Impl {
    // LINT.IfChange
    // Be sure to update AccumulateAll if new fields are added.
    ParserRuntimeInfo parser_runtime_info;
    internal::TimedValue resolver_timed_value;
    absl::flat_hash_map<ResolvedASTRewrite, RewriterDetails> rewriters_details;
    internal::TimedValue rewriters_timed_value;
    internal::TimedValue validator_timed_value;
    internal::TimedValue overall_timed_value;
  };
  std::unique_ptr<Impl> impl_;
  friend class AnalyzerOutputMutator;
};

class AnalyzerOutput {
 public:
  AnalyzerOutput(
      std::shared_ptr<IdStringPool> id_string_pool,
      std::shared_ptr<googlesql_base::UnsafeArena> arena,
      std::unique_ptr<const ResolvedStatement> statement,
      const AnalyzerOutputProperties& analyzer_output_properties,
      std::unique_ptr<ParserOutput> parser_output,
      const std::vector<absl::Status>& deprecation_warnings,
      const QueryParametersMap& undeclared_parameters,
      const std::vector<const Type*>& undeclared_positional_parameters,
      int max_column_id, bool has_graph_references = false);
  AnalyzerOutput(
      std::shared_ptr<IdStringPool> id_string_pool,
      std::shared_ptr<googlesql_base::UnsafeArena> arena,
      std::unique_ptr<const ResolvedExpr> expr,
      const AnalyzerOutputProperties& analyzer_output_properties,
      std::unique_ptr<ParserOutput> parser_output,
      const std::vector<absl::Status>& deprecation_warnings,
      const QueryParametersMap& undeclared_parameters,
      const std::vector<const Type*>& undeclared_positional_parameters,
      int max_column_id, bool has_graph_references = false);
  AnalyzerOutput(const AnalyzerOutput&) = delete;
  AnalyzerOutput& operator=(const AnalyzerOutput&) = delete;
  ~AnalyzerOutput();

  // Present for output from AnalyzeStatement.
  // IdStrings in this resolved AST are allocated in the IdStringPool attached
  // to this AnalyzerOutput, and copies of those IdStrings will be valid only
  // if the IdStringPool is still alive.
  const ResolvedStatement* resolved_statement() const {
    return statement_.get();
  }

  // Present for output from AnalyzeExpression.
  // IdStrings in this resolved AST are allocated in the IdStringPool attached
  // to this AnalyzerOutput, and copies of those IdStrings will be valid only
  // if the IdStringPool is still alive.
  const ResolvedExpr* resolved_expr() const { return expr_.get(); }

  // Will return either resolved_statement() or resolved_expr() depending on
  // which is set, or nullptr if neither.
  const ResolvedNode* resolved_node() const {
    if (resolved_statement() != nullptr) {
      return resolved_statement();
    }
    return resolved_expr();
  }

  // These are warnings for use of deprecated features.
  // The statuses will have code INVALID_ARGUMENT and will include a location,
  // when possible. They will also have DeprecationWarning protos attached to
  // them.
  // If there are multiple warnings with the same error message and
  // DeprecationWarning::Kind, they will be deduplicated..
  const std::vector<absl::Status>& deprecation_warnings() const {
    return deprecation_warnings_;
  }

  // Returns the undeclared query parameters found in the query and their
  // inferred types. If none are present, returns an empty set.
  const QueryParametersMap& undeclared_parameters() const {
    return undeclared_parameters_;
  }

  // Returns undeclared positional parameters found the query and their inferred
  // types. The index in the vector corresponds with the position of the
  // undeclared parameter--for example, the first element in the vector is the
  // type of the undeclared parameter at position 1 and so on.
  const std::vector<const Type*>& undeclared_positional_parameters() const {
    return undeclared_positional_parameters_;
  }

  bool has_graph_references() const { return has_graph_references_; }

  // Returns the IdStringPool that stores IdStrings allocated for the
  // resolved AST.  This was propagated from AnalyzerOptions.
  std::shared_ptr<IdStringPool> id_string_pool() const {
    return id_string_pool_;
  }

  // Returns the arena() that was propagated from AnalyzerOptions. This contains
  // some or all of the resolved AST and parse tree.
  std::shared_ptr<googlesql_base::UnsafeArena> arena() const { return arena_; }

  const AnalyzerOutputProperties& analyzer_output_properties() const {
    return analyzer_output_properties_;
  }

  // Returns the maximum column id that has been allocated.
  // Column ids above this number are unused.
  int max_column_id() const { return max_column_id_; }

  const AnalyzerRuntimeInfo& runtime_info() const { return runtime_info_; }

 private:
  friend class AnalyzerOutputMutator;

  // This IdStringPool and arena must be kept alive for the Resolved trees below
  // to be valid.
  std::shared_ptr<IdStringPool> id_string_pool_;
  std::shared_ptr<googlesql_base::UnsafeArena> arena_;

  std::unique_ptr<const ResolvedStatement> statement_;
  std::unique_ptr<const ResolvedExpr> expr_;

  AnalyzerOutputProperties analyzer_output_properties_;

  // AnalyzerOutput can (but is not guaranteed to) take ownership of the parser
  // output so deleting the parser AST can be deferred.  Deleting the parser
  // AST is expensive.  This allows engines to defer AnalyzerOutput cleanup
  // until after critical-path work is done.  May be NULL.
  std::unique_ptr<ParserOutput> parser_output_;

  std::vector<absl::Status> deprecation_warnings_;

  QueryParametersMap undeclared_parameters_;
  std::vector<const Type*> undeclared_positional_parameters_;
  int max_column_id_;
  bool has_graph_references_ = false;
  AnalyzerRuntimeInfo runtime_info_;
};
}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_ANALYZER_OUTPUT_H_
