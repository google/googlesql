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

#ifndef GOOGLESQL_COMMON_MATCH_RECOGNIZE_NFA_BUILDER_H_
#define GOOGLESQL_COMMON_MATCH_RECOGNIZE_NFA_BUILDER_H_

#include <functional>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "googlesql/common/match_recognize/nfa.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_visitor.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace googlesql::functions::match_recognize {

// The maximum supported value for an explicit quantifier bound (e.g. A{n}).
// This value must be small enough such that simple patterns with quantifiers
// using this bound can succeed without hitting the kMaxSupportedEdges limit
// in the NFA.
constexpr int kMaxSupportedQuantifierBound = kMaxSupportedEdges / 10;

class NFABuilder : public ResolvedASTVisitor {
 public:
  using ParameterEvaluator = std::function<absl::StatusOr<Value>(
      std::variant<int, absl::string_view>)>;

  struct Options {
    ParameterEvaluator parameter_evaluator = nullptr;

    // Temporary option to bypass epsilon remover to unblock unit tests.
    //
    // As NFAs produced with this option set are not valid for matching (
    // NFAMatchPartition assumes that the NFA it receives is the result of
    // epsilon removal), this is a temporary option solely to unblock unit tests
    // of NFABuilder code around pattern anchors, while epsilon remover support
    // for anchors isn't implemented yet.
    //
    // TODO: Once epsilon remover support for anchors is
    // implemented, remove this option and update the tests.
    bool disable_epsilon_remover_for_test = false;
  };

  absl::Status DefaultVisit(const ResolvedNode* node) override {
    return absl::UnimplementedError(absl::StrCat(
        "Pattern node ", node->node_kind_string(), " not yet implemented"));
  }

  absl::Status VisitResolvedMatchRecognizePatternVariableRef(
      const ResolvedMatchRecognizePatternVariableRef* node) override;
  absl::Status VisitResolvedMatchRecognizePatternOperation(
      const ResolvedMatchRecognizePatternOperation* node) override;
  absl::Status VisitResolvedMatchRecognizePatternEmpty(
      const ResolvedMatchRecognizePatternEmpty* node) override;
  absl::Status VisitResolvedMatchRecognizePatternQuantification(
      const ResolvedMatchRecognizePatternQuantification* node) override;
  absl::Status VisitResolvedMatchRecognizePatternAnchor(
      const ResolvedMatchRecognizePatternAnchor* node) override;

  static absl::StatusOr<std::unique_ptr<const NFA>> BuildNFAForPattern(
      const ResolvedMatchRecognizeScan& scan,
      ParameterEvaluator parameter_evaluator = nullptr) {
    return BuildNFAForPattern(scan,
                              {.parameter_evaluator = parameter_evaluator});
  }

  static absl::StatusOr<std::unique_ptr<const NFA>> BuildNFAForPattern(
      const ResolvedMatchRecognizeScan& scan, const Options& options);

 private:
  explicit NFABuilder(ParameterEvaluator parameter_evaluator)
      : parameter_evaluator_(parameter_evaluator) {}

  // Defines the start and final states of the subgraph associated with a
  // particular AST node.
  struct Subgraph {
    NFAState start;
    NFAState end;
  };

  absl::Status BuildConcatSubgraph(int num_operands);
  absl::Status BuildAlternationSubgraph(int num_operands);

  // Handles Foo?, Foo{0}, Foo{,n}, and Foo{m,n}.
  absl::Status BuildBoundedQuantifierSubgraph(
      int lower_bound, int upper_bound,
      const ResolvedMatchRecognizePatternQuantification& node);

  // Handles Foo*, Foo+, and Foo{m,}.
  absl::Status BuildUnboundedQuantifierSubgraph(
      int lower_bound, const ResolvedMatchRecognizePatternQuantification& node);

  // Removes the given number of entries from the stack, replacing them with
  // a single stack entry {start, end}.
  void ReplaceOperandsOnStack(NFAState start, NFAState end, int num_operands);

  // Evaluates a quantifier bound expression, which must produce an integer.
  absl::StatusOr<int> EvaluateQuantifierBound(
      const ResolvedExpr& quantifier_bound_expr);

  // Evaluates a literal or query parameter to a Value.
  absl::StatusOr<Value> EvaluateLiteralOrQueryParam(const ResolvedExpr& expr);

  // Maps pattern variable names (uppercase) to ids.
  absl::flat_hash_map<std::string, PatternVariableId> var_name_to_id_;

  // The NFA being built.
  std::unique_ptr<NFA> nfa_;

  // The stack of subgraphs being built. When a node is visited, its start/end
  // states are pushed onto the stack. After visiting the children, the Visit()
  // method for the parent node pops the child results off the stack in order to
  // build the subgraph for itself. When the root node is finished processing,
  // the stack is left with one element, which specifies the start/end of the
  // entire pattern.
  std::vector<Subgraph> subgraph_stack_;

  // Evaluates query parameters available at compile-time, nullptr if
  // compile-time evaluation of query parameters is unavailable.
  ParameterEvaluator parameter_evaluator_;
};
}  // namespace googlesql::functions::match_recognize
#endif  // GOOGLESQL_COMMON_MATCH_RECOGNIZE_NFA_BUILDER_H_
