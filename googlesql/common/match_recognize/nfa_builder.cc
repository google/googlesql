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

#include "googlesql/common/match_recognize/nfa_builder.h"

#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include "googlesql/common/match_recognize/epsilon_remover.h"
#include "googlesql/common/match_recognize/nfa.h"
#include "googlesql/public/cast.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_enums.pb.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql::functions::match_recognize {

absl::StatusOr<std::unique_ptr<const NFA>> NFABuilder::BuildNFAForPattern(
    const ResolvedMatchRecognizeScan& scan,
    const NFABuilder::Options& options) {
  NFABuilder builder(options.parameter_evaluator);
  GOOGLESQL_ASSIGN_OR_RETURN(builder.nfa_,
                   NFA::Create(static_cast<int>(
                       scan.pattern_variable_definition_list_size())));
  for (int i = 0; i < scan.pattern_variable_definition_list_size(); ++i) {
    builder.var_name_to_id_[scan.pattern_variable_definition_list(i)->name()] =
        PatternVariableId(i);
  }
  GOOGLESQL_RETURN_IF_ERROR(scan.pattern()->Accept(&builder));
  GOOGLESQL_RETURN_IF_ERROR(scan.pattern()->CheckFieldsAccessed());
  GOOGLESQL_RET_CHECK_EQ(builder.subgraph_stack_.size(), 1);

  NFAState start = builder.nfa_->NewState();
  NFAState final = builder.nfa_->NewState();
  GOOGLESQL_RETURN_IF_ERROR(
      builder.nfa_->AddEdge(start, builder.subgraph_stack_[0].start));
  GOOGLESQL_RETURN_IF_ERROR(builder.nfa_->AddEdge(builder.subgraph_stack_[0].end, final));

  builder.nfa_->SetAsStartState(start);
  builder.nfa_->SetAsFinalState(final);

  if (options.disable_epsilon_remover_for_test) {
    return std::move(builder.nfa_);
  } else {
    return RemoveEpsilons(*builder.nfa_);
  }
}

absl::Status NFABuilder::VisitResolvedMatchRecognizePatternEmpty(
    const ResolvedMatchRecognizePatternEmpty* node) {
  NFAState start = nfa_->NewState();
  NFAState end = nfa_->NewState();

  GOOGLESQL_RETURN_IF_ERROR(nfa_->AddEdge(start, end));
  subgraph_stack_.push_back({.start = start, .end = end});
  return absl::OkStatus();
}

absl::Status NFABuilder::VisitResolvedMatchRecognizePatternOperation(
    const ResolvedMatchRecognizePatternOperation* node) {
  for (const auto& operand : node->operand_list()) {
    GOOGLESQL_RETURN_IF_ERROR(operand->Accept(this));
  }

  switch (node->op_type()) {
    case ResolvedMatchRecognizePatternOperation::CONCAT:
      return BuildConcatSubgraph(node->operand_list_size());
    case ResolvedMatchRecognizePatternOperation::ALTERNATE:
      return BuildAlternationSubgraph(node->operand_list_size());
    default:
      return absl::UnimplementedError(absl::StrCat(
          "Pattern operation type ",
          ResolvedMatchRecognizePatternOperationEnums::
              MatchRecognizePatternOperationType_Name(node->op_type())));
  }
}

absl::Status NFABuilder::BuildAlternationSubgraph(int num_operands) {
  NFAState start = nfa_->NewState();
  NFAState end = nfa_->NewState();

  // Connect the start state to the start state of each alternative, and the end
  // state of each alternative to the end state.
  for (int i = static_cast<int>(subgraph_stack_.size()) - num_operands;
       i < subgraph_stack_.size(); ++i) {
    const Subgraph& subgraph = subgraph_stack_[i];
    GOOGLESQL_RETURN_IF_ERROR(nfa_->AddEdge(start, subgraph.start));
    GOOGLESQL_RETURN_IF_ERROR(nfa_->AddEdge(subgraph.end, end));
  }
  ReplaceOperandsOnStack(start, end, num_operands);
  return absl::OkStatus();
}

absl::Status NFABuilder::BuildConcatSubgraph(int num_operands) {
  NFAState start = nfa_->NewState();
  NFAState end = nfa_->NewState();

  // Connect the start state of each child's subgraph to the end state of the
  // previous child's subgraph (or the overall start state for the first child).
  NFAState prior_end_state = start;
  for (int i = static_cast<int>(subgraph_stack_.size()) - num_operands;
       i < subgraph_stack_.size(); ++i) {
    const Subgraph& subgraph = subgraph_stack_[i];

    GOOGLESQL_RETURN_IF_ERROR(nfa_->AddEdge(prior_end_state, subgraph.start));
    prior_end_state = subgraph.end;
  }

  // Connect the end of the final child's subgraph to the overall end.
  GOOGLESQL_RETURN_IF_ERROR(nfa_->AddEdge(prior_end_state, end));

  ReplaceOperandsOnStack(start, end, num_operands);
  return absl::OkStatus();
}

absl::Status NFABuilder::VisitResolvedMatchRecognizePatternVariableRef(
    const ResolvedMatchRecognizePatternVariableRef* node) {
  NFAState start = nfa_->NewState();
  NFAState end = nfa_->NewState();
  auto it = var_name_to_id_.find(node->name());
  GOOGLESQL_RET_CHECK(it != var_name_to_id_.end());
  PatternVariableId varid = it->second;
  GOOGLESQL_RETURN_IF_ERROR(nfa_->AddEdge(start, varid, end));

  subgraph_stack_.push_back({.start = start, .end = end});
  return absl::OkStatus();
}

absl::Status NFABuilder::VisitResolvedMatchRecognizePatternAnchor(
    const ResolvedMatchRecognizePatternAnchor* node) {
  NFAState start = nfa_->NewState();
  NFAState end = nfa_->NewState();
  NFAEdge edge(end);
  switch (node->mode()) {
    case ResolvedMatchRecognizePatternAnchor::START:
      edge.is_head_anchored = true;
      break;
    case ResolvedMatchRecognizePatternAnchor::END:
      edge.is_tail_anchored = true;
      break;
    default:
      return absl::InternalError(absl::StrCat(
          "Invalid anchor mode: ",
          ResolvedMatchRecognizePatternAnchorEnums::Mode_Name(node->mode())));
  }
  GOOGLESQL_RETURN_IF_ERROR(nfa_->AddEdge(start, edge));
  subgraph_stack_.push_back({.start = start, .end = end});
  return absl::OkStatus();
}

absl::Status NFABuilder::VisitResolvedMatchRecognizePatternQuantification(
    const ResolvedMatchRecognizePatternQuantification* node) {
  GOOGLESQL_ASSIGN_OR_RETURN(int lower_bound,
                   EvaluateQuantifierBound(*node->lower_bound()));
  std::optional<int> upper_bound;
  if (node->upper_bound() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(upper_bound,
                     EvaluateQuantifierBound(*node->upper_bound()));
    if (lower_bound > *upper_bound) {
      return absl::OutOfRangeError(
          absl::StrCat("Quantifier lower bound, ", lower_bound,
                       ", exceeds upper bound, ", *upper_bound));
    }
  }

  if (upper_bound.has_value()) {
    return BuildBoundedQuantifierSubgraph(lower_bound, *upper_bound, *node);
  } else {
    return BuildUnboundedQuantifierSubgraph(lower_bound, *node);
  }
}

absl::Status NFABuilder::BuildBoundedQuantifierSubgraph(
    int lower_bound, int upper_bound,
    const ResolvedMatchRecognizePatternQuantification& node) {
  // TODO: Currently, we represent bounded quantifiers as a
  // separate state and edge for each repetition count. An alternate
  // representation will be needed to handle the case where the quantifier value
  // is either very large (to save memory), or contained in a query parameter
  // that isn't known yet at compile-time.
  NFAState start = nfa_->NewState();
  NFAState end = nfa_->NewState();

  NFAState curr = start;
  for (int i = 0; i < upper_bound; ++i) {
    GOOGLESQL_RETURN_IF_ERROR(node.operand()->Accept(this));
    Subgraph subgraph = subgraph_stack_.back();
    subgraph_stack_.pop_back();

    if (i >= lower_bound) {
      // Lower bound met, but upper bound not met. Can either repeat or move
      // on.
      if (node.is_reluctant()) {
        GOOGLESQL_RETURN_IF_ERROR(nfa_->AddEdge(curr, end));
        GOOGLESQL_RETURN_IF_ERROR(nfa_->AddEdge(curr, subgraph.start));
      } else {
        GOOGLESQL_RETURN_IF_ERROR(nfa_->AddEdge(curr, subgraph.start));
        GOOGLESQL_RETURN_IF_ERROR(nfa_->AddEdge(curr, end));
      }
    } else {
      // Lower bound not met - repeating again is the only option.
      GOOGLESQL_RETURN_IF_ERROR(nfa_->AddEdge(curr, subgraph.start));
    }
    curr = subgraph.end;
  }
  // Upper bound met - moving on is the only option.
  GOOGLESQL_RETURN_IF_ERROR(nfa_->AddEdge(curr, end));

  if (lower_bound == upper_bound) {
    // In the case of Foo{n}, is_reluctant() has no effect, as the repetition
    // count is fixed. Add a dummy access to satisfy access checks.
    node.is_reluctant();

    if (upper_bound == 0) {
      // In the case of Foo{0}, operand() has no effect, as, Foo{0} is
      // semantically equivalent to empty (), regardless of the operand.
      // Add a dummy access to satisfy access checks.
      node.operand()->MarkFieldsAccessed();
    }
  }
  subgraph_stack_.push_back({.start = start, .end = end});
  return absl::OkStatus();
}

absl::Status NFABuilder::BuildUnboundedQuantifierSubgraph(
    int lower_bound, const ResolvedMatchRecognizePatternQuantification& node) {
  // TODO: When the upper bound is large, consider a more efficient
  // way to represent this than a separate node in the graph for each repetition
  // count.

  NFAState start = nfa_->NewState();
  NFAState end = nfa_->NewState();

  if (lower_bound == 0) {
    // Foo* case
    GOOGLESQL_RETURN_IF_ERROR(node.operand()->Accept(this));
    Subgraph subgraph = subgraph_stack_.back();
    subgraph_stack_.pop_back();

    GOOGLESQL_RETURN_IF_ERROR(nfa_->AddEdge(subgraph.end, start));
    if (node.is_reluctant()) {
      GOOGLESQL_RETURN_IF_ERROR(nfa_->AddEdge(start, end));
      GOOGLESQL_RETURN_IF_ERROR(nfa_->AddEdge(start, subgraph.start));
    } else {
      GOOGLESQL_RETURN_IF_ERROR(nfa_->AddEdge(start, subgraph.start));
      GOOGLESQL_RETURN_IF_ERROR(nfa_->AddEdge(start, end));
    }
  } else {
    // Foo{m,} case (m > 0).
    NFAState curr = start;
    NFAState last_rep_start;

    // Mandatory repetitions up to the lower bound.
    for (int i = 0; i < lower_bound; ++i) {
      GOOGLESQL_RETURN_IF_ERROR(node.operand()->Accept(this));
      Subgraph subgraph = subgraph_stack_.back();
      subgraph_stack_.pop_back();

      GOOGLESQL_RETURN_IF_ERROR(nfa_->AddEdge(curr, subgraph.start));
      last_rep_start = subgraph.start;
      curr = subgraph.end;
    }

    // Allow the last repetition to repeat indefinitely.
    if (node.is_reluctant()) {
      GOOGLESQL_RETURN_IF_ERROR(nfa_->AddEdge(curr, end));
      GOOGLESQL_RETURN_IF_ERROR(nfa_->AddEdge(curr, last_rep_start));
    } else {
      GOOGLESQL_RETURN_IF_ERROR(nfa_->AddEdge(curr, last_rep_start));
      GOOGLESQL_RETURN_IF_ERROR(nfa_->AddEdge(curr, end));
    }
  }
  subgraph_stack_.push_back({.start = start, .end = end});
  return absl::OkStatus();
}

static std::variant<int, absl::string_view> AsParamVariant(
    const ResolvedParameter& param) {
  if (param.name().empty()) {
    return param.position();
  } else {
    return param.name();
  }
}

absl::StatusOr<Value> NFABuilder::EvaluateLiteralOrQueryParam(
    const ResolvedExpr& expr) {
  if (expr.Is<ResolvedLiteral>()) {
    return expr.GetAs<ResolvedLiteral>()->value();
  } else if (expr.Is<ResolvedParameter>()) {
    if (parameter_evaluator_ == nullptr) {
      return absl::InternalError(
          "Missing parameter evaluator; it should not be possible to reach "
          "here, as CompiledPattern should have deferred compilation to "
          "runtime");
    }
    return parameter_evaluator_(
        AsParamVariant(*expr.GetAs<ResolvedParameter>()));
  }
  return absl::OutOfRangeError("Unsupported quantifier bounds");
}

absl::StatusOr<int> NFABuilder::EvaluateQuantifierBound(
    const ResolvedExpr& quantifier_bound_expr) {
  if (!quantifier_bound_expr.type()->IsInt64()) {
    return absl::OutOfRangeError("Expected INT64 value for quantifier bound");
  }
  Value value;
  if (quantifier_bound_expr.Is<ResolvedCast>()) {
    GOOGLESQL_ASSIGN_OR_RETURN(value,
                     EvaluateLiteralOrQueryParam(
                         *quantifier_bound_expr.GetAs<ResolvedCast>()->expr()));
    GOOGLESQL_ASSIGN_OR_RETURN(value, googlesql::CastValue(
                                value, absl::UTCTimeZone(), LanguageOptions(),
                                quantifier_bound_expr.type(), nullptr,
                                /*canonicalize_zero=*/false));
  } else {
    GOOGLESQL_ASSIGN_OR_RETURN(value, EvaluateLiteralOrQueryParam(quantifier_bound_expr));
  }

  if (value.is_null()) {
    return absl::OutOfRangeError(
        "NULL as quantifier bound value is not allowed");
  }

  if (value.int64_value() < 0) {
    return absl::OutOfRangeError(
        "Negative quantifier bound value is not allowed");
  }

  if (value.int64_value() > kMaxSupportedQuantifierBound) {
    return absl::OutOfRangeError(absl::StrCat(
        "Quantifier bound value exceeds maximum supported value of ",
        kMaxSupportedQuantifierBound));
  }

  return static_cast<int>(value.int64_value());
}

void NFABuilder::ReplaceOperandsOnStack(NFAState start, NFAState end,
                                        int num_operands) {
  subgraph_stack_.resize(subgraph_stack_.size() - num_operands);
  subgraph_stack_.push_back({.start = start, .end = end});
}
}  // namespace googlesql::functions::match_recognize
