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

#ifndef GOOGLESQL_SCRIPTING_CONTROL_FLOW_GRAPH_H_
#define GOOGLESQL_SCRIPTING_CONTROL_FLOW_GRAPH_H_

#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/parser/parse_tree.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace googlesql {

class ControlFlowNode;
class ControlFlowGraph;

// Represents a potential transition from one node to another.
class ControlFlowEdge {
 public:
  enum class Kind {
    // The previous statement completed normally, without an exception.
    kNormal,

    // An IF, ELSEIF, or WHILE condition was just evaluated, with TRUE result.
    kTrueCondition,

    // An IF, ELSEIF, or WHILE condition was just evaluated, with FALSE result.
    kFalseCondition,

    // The previous statement (or expression) threw an exception.
    kException,
  };

  // Describes side effects to the execution of the script that happen as a
  // result of transitioning along a control-flow edge.
  struct SideEffects {
    // The names of variables which go out of scope.  For edges which terminate
    // the script, this does *NOT* include variables in the top-level scope
    // (but may include variables from any inner blocks which are exited). This
    // allows script variables to be inspected through
    // ScriptExecutor::DebugString(), after the script has terminated, which
    // many tests rely upon.
    std::set<std::string> destroyed_variables;

    // True if this edge enters an exception handler.  This is possible only
    // when kind() == Kind::kException and the exception handler is non-empty.
    bool exception_handler_entered = false;

    // The number of exception handlers exited.  This represents the number of
    // times that the script executor will need to pop its record of the
    // "current" exception.  This affects the values of @@error.* system
    // variables, as well as the details of the exception raised through the
    // standalone RAISE statement.
    int num_exception_handlers_exited = 0;

    // The number of FOR...IN loops exited. This represents the number of
    // times that the script executor will need to pop from its record of the
    // "current" stack of FOR loops. This also allows FOR loops to be inspected
    // through ScriptExecutor::DebugString(), which tests rely on.
    int num_for_loops_exited = 0;
  };

  const ControlFlowNode* predecessor() const { return predecessor_; }
  const ControlFlowNode* successor() const { return successor_; }
  Kind kind() const { return kind_; }

  // Computes the side effects that result from this edge.
  // Note: While quick for most practical scripts, this function can take
  // O(<length of script>) runtime to complete in the worst case, so this
  // function should be called only when necessary.  Worst-case behavior can be
  // realized in scripts with an excessive number of nested blocks or variable
  // declarations.
  SideEffects ComputeSideEffects() const;

  // Returns a single-line debug string of this edge.  Does not include the
  // details of the successor/predecessor nodes.
  std::string DebugString() const;

 private:
  friend class ControlFlowGraphBuilder;
  ControlFlowEdge(const ControlFlowNode* predecessor,
                  const ControlFlowNode* successor, Kind kind,
                  const ASTNode* exit_to, ControlFlowGraph* graph)
      : predecessor_(predecessor),
        successor_(successor),
        kind_(kind),
        exit_to_(exit_to) {}

  // Node which executes prior to this transition (owned by graph_).
  const ControlFlowNode* predecessor_;

  // Node which will execute next, after this transition (owned by graph_).
  const ControlFlowNode* successor_;

  // Specifies when this transition happens
  Kind kind_;

  // When non-nullptr, indicates that all AST ancestors from <predecessor_>
  // (inclusive) to <exit_to_> (exclusive), are being exited.  This is used to
  // calculate the edge's side effects.  A nullptr value indicates that the edge
  // has no side effects.
  const ASTNode* exit_to_;
};

// Returns the string-representation of a ControlFlowEdge::Kind enumeration.
// This function is intended only for debug logging.
std::string ControlFlowEdgeKindString(ControlFlowEdge::Kind kind);

// A node in a control flow graph representing the execution of one AST node.
//
// Control-flow nodes are only generated for leaf-level statements and
// IF/ELSEIF/WHILE conditions.  Node objects do not exist for compound
// statements or expressions (other than IF/ELSEIF/WHILE conditions).
//
// Each script ends with a sentinel node with a NULL ast node, which is reached
// whenever a script terminates without an exception.  Edges will transition
// into this node from the last statement in the script, and from each RETURN
// statement.
//
// If the script contains a LOOP statement with an empty body, a node will be
// created with an edge to itself, representing the empty loop body.  This node
// will have a NULL AST-node corresponding to the empty statement list; this is
// the only time that an ASTStatementList will have a ControlFlowNode
// associated with it.
class ControlFlowNode {
 public:
  // Used to disambiguate which node we have when multiple control flow nodes
  // exist for the same ast node.
  //
  // Note: As these values are persisted in ScriptExecutorStateProto, the
  // numerical enum values cannot be changed.
  enum class Kind {
    // This is the only control flow node possible for this AST node.
    kDefault = 0,

    // The initial iteration of a for-loop, responsible for creating the loop
    // iterator.
    kForInitial = 1,

    // Subsequent iterations of a for-loop, advances the iterator rather than
    // creating a new one.
    kForAdvance = 2,
  };
  using EdgeMap =
      absl::flat_hash_map<ControlFlowEdge::Kind, const ControlFlowEdge*>;

  const ASTNode* ast_node() const { return ast_node_; }
  Kind kind() const { return kind_; }
  const ControlFlowGraph* graph() const { return graph_; }
  const EdgeMap& successors() const { return successors_; }
  std::vector<const ControlFlowEdge*> predecessors() const {
    return predecessors_;
  }

  // Returns a simple string identifiying the node in the context of the script.
  // It does not contain a dump of the successors or predecessors.
  std::string DebugString() const;

  // Returns a string describing the node's successors.
  // The return value is a multiline string, with each line indented with
  // <indent>.
  std::string SuccessorsDebugString(absl::string_view indent = "") const;

 private:
  friend class ControlFlowGraphBuilder;
  ControlFlowNode(const ASTNode* ast_node, Kind kind, ControlFlowGraph* graph)
      : ast_node_(ast_node), kind_(kind), graph_(graph) {}

  // The AST node to execute. NULL for the sentinel node indicating the end of
  // the script.
  const ASTNode* ast_node_;

  // Used to disambiguate which type of node this is, within the same AST node.
  Kind kind_;

  // The control flow graph containing this node.  The lifetimes of all AST
  // nodes, control-flow nodes, and control-flow edges are owned here.
  ControlFlowGraph* graph_;

  // List of edges that can potentially execute after execution of <ast_node_>
  // has fully completed.
  EdgeMap successors_;

  // List of edges can can potentially execute immediately prior to this node.
  std::vector<const ControlFlowEdge*> predecessors_;
};

class ControlFlowGraph {
 public:
  // Creates a ControlFlowGraph for the given script.
  // (Both arguments must remain alive while the control-flow graph is alive).
  static absl::StatusOr<std::unique_ptr<const ControlFlowGraph>> Create(
      const ASTScript* script, absl::string_view script_text);

  // Node to execute at the start of the script, or <end_node()> if the script
  // terminates without executing any statements.  Never null.
  const ControlFlowNode* start_node() const { return start_node_; }

  // Sentinel node to mark the end of the script.  This is reached whenever the
  // script terminates normally, and has a null ast node.
  //
  // Returns nullptr if normal termination is not possible in any code path.
  const ControlFlowNode* end_node() const { return end_node_.get(); }

  // The control-flow node associated with the given AST node, or null if
  // <ast_node> is unreachable or has no control-flow node associated with it.
  // Control-flow nodes are generated for leaf statements and IF/ELSEIF/WHILE
  // conditions only.
  //
  const ControlFlowNode* GetControlFlowNode(const ASTNode* ast_node,
                                            ControlFlowNode::Kind kind) const;

  // Returns all reachable nodes in the script. Nodes are returned in an order
  // which guarantees that, after ignoring back edges, each node appears after
  // all of its predecessors.
  //
  // Note: Currently, this is equivalent to simply sorting nodes by the
  // positions of their AST nodes, with the exception that the node to advance
  // a for-loop comes after the body. This could change in the future as new
  // scripting features get added.
  std::vector<const ControlFlowNode*> GetAllNodes() const;

  // The script used to generate this graph.
  const ASTScript* ast_script() const { return ast_script_; }

  // The text used to generate <ast_script_>.  Externally owned.
  absl::string_view script_text() const { return script_text_; }

  // Returns a debug string for this graph.  The returned string is multi-line
  // and describes the start node, along with all edges in the graph.
  std::string DebugString() const;

 private:
  friend class ControlFlowGraphBuilder;
  explicit ControlFlowGraph(const ASTScript* ast_script,
                            absl::string_view script_text);

  // Node to execute at the start of the script.  This is normally set to the
  // first statement in the script.  If the script is empty, <start_node_> will
  // be set to <end_node_>.  Owned by either <node_map_> or <end_node_>.
  const ControlFlowNode* start_node_;

  // Sentinel control-flow node representing a successful termination of the
  // script. This node will have a NULL AST node, and will typically contain a
  // predecessor for the last statement, plus any return statements, and no
  // successor.  (If the script is guarnateed to either loop forever or throw
  // an unhandled exception, <end_node_> will be nullptr).
  std::unique_ptr<ControlFlowNode> end_node_;

  // Maps each AST node and node kind to its corresponding ControlFlowNode, and
  // owns the lifetime of all ControlFlowNode's.
  absl::flat_hash_map<std::pair<const ASTNode*, ControlFlowNode::Kind>,
                      std::unique_ptr<ControlFlowNode>>
      node_map_;

  // Owns the lifetime of all ControlFlowEdge objects.  (The order of elements
  // in this vector is arbitrary).
  absl::flat_hash_set<std::unique_ptr<ControlFlowEdge>> edges_;

  // Script used to generate this graph (externally owned).
  const ASTScript* ast_script_;

  // Text used to generate <ast_script_>.  Used in DebugString() messages.
  absl::string_view script_text_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_SCRIPTING_CONTROL_FLOW_GRAPH_H_
