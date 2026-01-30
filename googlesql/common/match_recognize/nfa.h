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

#ifndef GOOGLESQL_COMMON_MATCH_RECOGNIZE_NFA_H_
#define GOOGLESQL_COMMON_MATCH_RECOGNIZE_NFA_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/base/nullability.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"

namespace googlesql::functions::match_recognize {

// Maximum supported number of edges in an NFA. This limit exists to avoid
// using too much time or memory building the pattern, or in matching
// (space requires for matching is O(# rows * # edges).
constexpr int kMaxSupportedEdges = 100000;

// A state in an NFA.
// A state object is a wrapper around an 'int' value which holds the state
// number. It is cheap to copy, and intended to be passed around by value.
class NFAState {
 public:
  // A default-constructed object holds a sentinel "invalid" value. To create
  // a new NFAObjectId with a unique value, use NFAObjectMap::AddNew().
  NFAState() = default;
  NFAState(const NFAState&) = default;
  NFAState& operator=(const NFAState&) = default;
  explicit NFAState(int state_number) : value_(state_number) {}

  // Stringification support (for test/debugging purposes only).
  template <typename Sink>
  friend void AbslStringify(Sink& sink, const NFAState& state) {
    absl::Format(&sink, "%s%d", "N", state.value_);
  }

  // Equality comparison support.
  bool operator==(const NFAState& other) const {
    return value_ == other.value_;
  }

  bool operator!=(const NFAState& other) const {
    return value_ != other.value_;
  }

  // Hashing support
  template <typename H>
  friend H AbslHashValue(H h, const NFAState& state) {
    return H::combine(std::move(h), state.value_);
  }

  // Returns true if this is a valid state created by NFAObjectIdMap::AddNew().
  // For a default-constructed NFAObjectId, the return value is false.
  bool IsValid() const { return value_ >= 0; }

  // Underlying integer value.
  int value() const { return value_; }

 private:
  int value_ = -1;
};

class PatternVariableId {
 public:
  // A default-constructed object holds a sentinel "invalid" value. To create
  // a new NFAObjectId with a unique value, use NFAObjectMap::AddNew().
  constexpr PatternVariableId() = default;
  constexpr PatternVariableId(const PatternVariableId&) = default;
  constexpr PatternVariableId& operator=(const PatternVariableId&) = default;
  explicit constexpr PatternVariableId(int value) : value_(value) {}

  // Equality comparison support.
  constexpr bool operator==(const PatternVariableId& other) const {
    return value_ == static_cast<const PatternVariableId&>(other).value_;
  }

  constexpr bool operator!=(const PatternVariableId& other) const {
    return value_ != static_cast<const PatternVariableId&>(other).value_;
  }

  // Returns true if this is a valid state created by NFAObjectIdMap::AddNew().
  // For a default-constructed NFAObjectId, the return value is false.
  constexpr bool IsValid() const { return value_ >= 0; }

  // Underlying integer value.
  constexpr int value() const { return value_; }

 private:
  int value_ = -1;
};

// Represents an edge in an NFA.
//
// Note: We do not store the 'from' state or precedence here, as these are
// implied by how the edge was looked up in the map containing it.
struct NFAEdge {
  NFAEdge() = default;
  explicit NFAEdge(NFAState target) : target(target) {}

  explicit NFAEdge(PatternVariableId var, NFAState target)
      : pattern_var(var), target(target) {}

  explicit NFAEdge(std::optional<PatternVariableId> var, NFAState target)
      : pattern_var(var), target(target) {}

  // Marks the edge as head-anchored or tail anchored.
  //
  // A head-anchored edge is valid only at the very start of the partition, a
  // tail-anchored edge is valid only at the very end of the partition.
  //
  // If the pattern consumes a row (e.g. non-nullopt pattern_var), anchors are
  // evaluated before the row gets consumed (which effectively means that a
  // head-anchored edge can consume a row and still be reachable, while a tail-
  // anchored edge cannot).
  NFAEdge& SetHeadAnchored(bool head_anchored = true) {
    is_head_anchored = head_anchored;
    return *this;
  }
  NFAEdge& SetTailAnchored(bool tail_anchored = true) {
    is_tail_anchored = tail_anchored;
    return *this;
  }

  // Equality support
  bool operator==(const NFAEdge& other) const {
    if (target != other.target) {
      return false;
    }
    if (pattern_var.has_value()) {
      return other.pattern_var.has_value() &&
             *other.pattern_var == *pattern_var;
    }
    if (is_head_anchored != other.is_head_anchored) {
      return false;
    }
    if (is_tail_anchored != other.is_tail_anchored) {
      return false;
    }
    return !other.pattern_var.has_value();
  }

  bool operator!=(const NFAEdge& other) const { return !(*this == other); }

  std::string DebugString() const {
    std::string result;
    absl::StrAppend(&result, " to ", target);
    if (is_head_anchored) {
      absl::StrAppend(&result, " (head-anchored)");
    }
    if (is_tail_anchored) {
      absl::StrAppend(&result, " (tail-anchored)");
    }
    if (pattern_var.has_value()) {
      absl::StrAppend(&result, " (consuming V_", pattern_var->value(), ")");
    }
    return result;
  }

  // If present, indicates that taking this edge consumes an input row, and
  // is legal only when the row consumed has this pattern variable evaluating to
  // TRUE.
  std::optional<PatternVariableId> pattern_var;

  // The state the NFA transitions to when taking this edge.
  NFAState target;

  // If true, the edge is head-anchored.
  bool is_head_anchored = false;

  // If true, the edge is tail-anchored.
  bool is_tail_anchored = false;
};

// Represents an NFA for pattern matching, as described in
// (broken link).
class NFA {
 public:
  // Constructs an empty NFA to match the given number of pattern variables,
  // with no edges, no states.
  static absl::StatusOr<std::unique_ptr<NFA>> Create(int num_pattern_variables);
  NFA(const NFA&) = delete;
  NFA& operator=(const NFA&) = delete;

  // Indicates what type of validation on an NFA we are doing.
  enum class ValidationMode {
    // Validates only properties which are guaranteed by construction
    // in NFABuilder, and needed for the epsilon remover to function properly:
    // - The start state, final state, and the target states of all edges are
    //   valid and exist in this NFA.
    // - No edges into the start state.
    // - No edges out of the final state.
    // - All states are reachable from the start state.
    // - All edges into the final state must be epsilon edges.
    kForEpsilonRemover,

    // Validates properties guaranteed by the epsilon remover, which are needed
    // for matching. This includes all of the above properties, plus the
    // additional requirement that epsilon edges are allowed *only* on the
    // transition to the final state (e.g. every edge to a nonfinal state must
    // consume a row).
    kForMatching,
  };

  // Adds a new state to the NFA with no edges coming into or out of it.
  NFAState NewState();

  // Adds a new edge from a given state, with lower precedence than all edges
  // previously added from the same state.
  absl::Status AddEdge(NFAState state, NFAState target) {
    return AddEdge(state, std::nullopt, target);
  }
  absl::Status AddEdge(NFAState state, PatternVariableId var, NFAState target) {
    GOOGLESQL_RET_CHECK_GE(var.value(), 0);
    return AddEdge(state, std::optional<PatternVariableId>{var}, target);
  }
  absl::Status AddEdge(NFAState state, std::optional<PatternVariableId> var,
                       NFAState target) {
    return AddEdge(state, NFAEdge(var, target));
  }
  absl::Status AddEdge(NFAState state, const NFAEdge& edge);

  // Returns the start state of the NFA (or an invalid state id if
  // SetAsStartState() has not yet been called).
  NFAState start_state() const { return start_state_; }

  // Sets the given state as the NFA's start state.
  // (The argument must have been previously returned by NewState()).
  void SetAsStartState(NFAState state) { start_state_ = state; }

  // Returns the final state of the NFA (or an invalid state id if
  // SetAsFinalState() has not yet been called).
  NFAState final_state() const { return final_state_; }

  // Sets the given state as the NFA's final state.
  // (The argument must have been previously returned by NewState()).
  void SetAsFinalState(NFAState state) { final_state_ = state; }

  // Returns a list of edges in the NFA from the given state.
  const std::vector<NFAEdge>& GetEdgesFrom(NFAState state) const {
    return edges_[state.value()];
  }

  // Returns a string describing the NFA graph in DOT format, suitable for
  // graphviz visualization. For testing/debugging purposes only.
  //
  // 'pattern_var_names' specifies the names of the pattern variables to use in
  // graph labels. If omitted, default names will be chosen, based on the
  // variable id numbers.
  std::string AsDot(absl::Span<const std::string> pattern_var_names = {}) const;

  // Returns the number of states in this NFA. State numbers range from 0
  // to <num_states() - 1>, inclusive.
  int num_states() const { return static_cast<int>(edges_.size()); }

  // Returns the number of pattern variables that can be referred to by any edge
  // in the NFA.
  int num_pattern_variables() const { return num_pattern_variables_; }

  // Validates the NFA for either matching or epsilon removal (see the
  // ValidationMode enum for details).
  absl::Status Validate(ValidationMode mode) const;

  // Returns an empty NFA that recognizes the same pattern variables as this
  // one. The caller is responsible for adding edges and setting the start/final
  // state on the returned object.
  std::unique_ptr<NFA> CreateEmptyNFA() const;

 private:
  explicit NFA(int num_pattern_variables);

  absl::Status ValidateInternal(ValidationMode mode) const;

  // The state that the NFA must be in at the beginning of a match.
  // (This is set to an invalid state id until SetAsStartState() is called).
  NFAState start_state_;

  // The state that the NFA must be in at the end of a match.
  // (This is set to an invalid state id until SetAsFinalState() is called).
  NFAState final_state_;

  // List of all edges emanating from each state in the NFA.
  // Outer vector is indexed by state number, inner vector sorts edges from each
  // state according to precedence.
  //
  // The size of the outer vector denotes the number of states.
  std::vector<std::vector<NFAEdge>> edges_;

  // The total number of edges in the NFA. Used to enforce complexity limit.
  int edge_count_ = 0;

  // Number of pattern variables that can be referred to by edges in the NFA.
  const int num_pattern_variables_ = 0;
};
}  // namespace googlesql::functions::match_recognize

#endif  // GOOGLESQL_COMMON_MATCH_RECOGNIZE_NFA_H_
