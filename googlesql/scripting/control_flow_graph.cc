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

#include "googlesql/scripting/control_flow_graph.h"

#include <algorithm>
#include <cstddef>
#include <list>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "googlesql/parser/ast_node_kind.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parse_tree_errors.h"
#include "googlesql/parser/parse_tree_visitor.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/scripting/script_segment.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "googlesql/base/check.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_builder.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

std::string ControlFlowEdgeKindString(ControlFlowEdge::Kind kind) {
  switch (kind) {
    case ControlFlowEdge::Kind::kNormal:
      return "kNormal";
    case ControlFlowEdge::Kind::kFalseCondition:
      return "kFalseCondition";
    case ControlFlowEdge::Kind::kTrueCondition:
      return "kTrueCondition";
    case ControlFlowEdge::Kind::kException:
      return "kException";
    default:
      return absl::StrCat("Unknown ControlFlowEdge::Kind value: ", kind);
  }
}

namespace {

std::string DebugLocationText(const ASTNode* node,
                              const absl::string_view script_text) {
  std::string node_text;
  ParseLocationPoint pos = node->location().start();
  ParseLocationTranslator translator(script_text);
  absl::StatusOr<std::pair<int, int>> line_and_column =
      translator.GetLineAndColumnAfterTabExpansion(pos);
  if (line_and_column.ok()) {
    absl::StrAppend(&node_text, " [at ", line_and_column.value().first, ":",
                    line_and_column.value().second, "]");
  }
  return node_text;
}

// Returns a single line consisting of the text used to form <node>
// (truncated if necessary), along with the line and column number.  This is
// intended to be used for debug output that is simply concerned with
// identifying the node, as opposed to the node's AST structure that would be
// visible with node->DebugString().
std::string DebugNodeIdentifier(const ASTNode* node,
                                const absl::string_view script_text) {
  std::string node_text = std::string(
      ScriptSegment::FromASTNode(script_text, node).GetSegmentText());
  absl::StripAsciiWhitespace(&node_text);
  size_t newline_idx = node_text.find('\n');
  if (newline_idx != node_text.npos) {
    node_text = absl::StrCat(node_text.substr(0, newline_idx), "...");
  }
  absl::StrAppend(&node_text, DebugLocationText(node, script_text));
  return node_text;
}

// Indicates whether a particular statement type can/must throw an exception.
enum class ThrowSemantics {
  kNoThrow,
  kCanThrow,
  kMustThrow,
};

// Represents the incomplete making of an edge, where the predecessor and edge
// kind is known, but the successor is not known yet.  The incomplete edge will
// be converted to a complete edge, once the successor is known.
struct IncompleteEdge {
  IncompleteEdge() = default;
  IncompleteEdge(const IncompleteEdge& edge) = delete;
  IncompleteEdge(IncompleteEdge&& edge) = default;
  IncompleteEdge& operator=(const IncompleteEdge& edge) = delete;
  IncompleteEdge& operator=(IncompleteEdge&& edge) = default;

  ControlFlowNode* predecessor;
  ControlFlowEdge::Kind kind;
};

// Information about a statement needed while constructing the control flow
// graph, and discarded when the graph is complete.
//
// Unlike ControlFlowNodes, NodeData objects are created for statement lists,
// and everything that can go inside of a statement list (including compound
// statements such as IF and BEGIN).
//
// (We do not create NodeData objects for condition expressions or ELSEIF
//  clauses, as they are dealt with entirely within the visitors for their
//  enclosing statements).
struct NodeData {
  // The AST node
  const ASTNode* ast_node = nullptr;

  // The control-flow node that is entered when this AST begins; nullptr if
  // this AST node begins and enters without executing any code (e.g.
  // empty BEGIN block).
  ControlFlowNode* start = nullptr;

  // List of edges between somewhere within <ast_node> and whatever comes after
  // it, assuming no exceptions.
  //
  // Once the successor node to <ast_node> is known, each entry in <end_nodes>
  // will be converted to an edge as follows:
  //  - predecessor: entry.first
  //  - successor: Whichever node follows <ast_node>
  //  - kind: entry.second
  std::list<IncompleteEdge> end_edges;

  // Returns true if this node is skipped entirely in the control-flow graph.
  // This arises when execution of the node is completely empty, causing any
  // edge leading into the node to instead go to whatever follows it.
  // Examples include an empty statement list or BEGIN/END block.
  bool empty() const { return start == nullptr; }

  // Return true if this node refers to a statement require special
  // control-flow, rather than simply advancing to the next statement in the
  // enclosing statement list.
  bool IsSpecialControlFlowStatement() const {
    switch (ast_node->node_kind()) {
      case AST_BREAK_STATEMENT:
      case AST_CONTINUE_STATEMENT:
      case AST_RETURN_STATEMENT:
        return true;
      default:
        return false;
    }
  }

  // Adds an end edge to this NodeData.
  void AddOpenEndEdge(ControlFlowNode* cfg_node, ControlFlowEdge::Kind kind) {
    IncompleteEdge edge;
    edge.predecessor = cfg_node;
    edge.kind = kind;

    end_edges.emplace_front(std::move(edge));
  }

  // Moves all end edges from <node_data> into here.
  void TakeEndEdgesFrom(NodeData* node_data) {
    end_edges.splice(end_edges.begin(), std::move(node_data->end_edges));
  }

  std::string DebugString(absl::string_view script_text) const {
    std::string debug_string = DebugNodeIdentifier(ast_node, script_text);
    absl::StrAppend(&debug_string, " (");
    absl::StrAppend(
        &debug_string,
        "start: ", (start == nullptr ? "<empty>" : start->DebugString()),
        ", end edges: ",
        absl::StrJoin(
            end_edges, ", ", [](std::string* out, const IncompleteEdge& edge) {
              absl::StrAppend(out, edge.predecessor->DebugString(), "( ",
                              ControlFlowEdgeKindString(edge.kind), ")");
            }));
    absl::StrAppend(&debug_string, ")");
    return debug_string;
  }
};

// Contains information about a pending loop statement while we are processing
// its body.
struct LoopData {
  // List of control flow nodes that will break out of the current loop.
  std::vector<ControlFlowNode*> break_nodes;
  // List of control flow nodes that will continue the current loop.
  std::vector<ControlFlowNode*> continue_nodes;
};
// Contains information about a pending block with label while we are
// processing its body.
struct BlockData {
  // List of control flow nodes that will break out of the current block.
  std::vector<ControlFlowNode*> break_nodes;
};

// Contains information about a pending block.
struct BlockWithExceptionHandlerData {
  // Lists control-flow nodes which might throw an exception handled by the
  // current block.
  std::vector<ControlFlowNode*> handled_nodes;
};

// This class keeps track of script labels.
class LabelTracker {
 public:
  absl::Status EnterLoop(const ASTLoopStatement* node, LoopData* loop_data) {
    GOOGLESQL_RET_CHECK(node->label() != nullptr);
    IdString label_id = node->label()->name()->GetAsIdString();
    auto pair = label_data_map_.emplace(label_id, loop_data);
    if (!pair.second) {
      return LabelAlreadyExistsError(node, label_id.ToStringView());
    }
    return absl::OkStatus();
  }

  absl::Status ExitLoop(const IdString& label) {
    GOOGLESQL_RET_CHECK(label_data_map_.erase(label) == 1);
    return absl::OkStatus();
  }

  absl::Status EnterBlock(const ASTBeginEndBlock* node, BlockData* block_data) {
    GOOGLESQL_RET_CHECK(node->label() != nullptr);
    IdString label_id = node->label()->name()->GetAsIdString();
    auto pair = label_data_map_.emplace(label_id, block_data);
    if (!pair.second) {
      return LabelAlreadyExistsError(node, label_id.ToStringView());
    }
    return absl::OkStatus();
  }

  absl::Status ExitBlock(const IdString& label) {
    GOOGLESQL_RET_CHECK(label_data_map_.erase(label) == 1);
    return absl::OkStatus();
  }

  absl::Status OnLabeledBreak(const ASTBreakStatement* node,
                              NodeData* node_data) {
    GOOGLESQL_RET_CHECK(node->label() != nullptr);
    IdString label = node->label()->name()->GetAsIdString();
    auto it = label_data_map_.find(label);
    if (it == label_data_map_.end()) {
      return LabelNotExitsError(node, label.ToStringView());
    }
    if (std::holds_alternative<LoopData*>(it->second)) {
      std::get<LoopData*>(it->second)->break_nodes.push_back(node_data->start);
    } else {
      std::get<BlockData*>(it->second)->break_nodes.push_back(node_data->start);
    }
    return absl::OkStatus();
  }

  absl::Status OnLabeledContinue(const ASTContinueStatement* node,
                                 NodeData* node_data) {
    GOOGLESQL_RET_CHECK(node->label() != nullptr);
    IdString label = node->label()->name()->GetAsIdString();
    auto it = label_data_map_.find(label);
    if (it == label_data_map_.end()) {
      return LabelNotExitsError(node, label.ToStringView());
    } else if (!std::holds_alternative<LoopData*>(it->second)) {
      return MakeSqlErrorAt(node)
             << node->GetKeywordText() << " with label must refer to a loop";
    }
    std::get<LoopData*>(it->second)->continue_nodes.push_back(node_data->start);
    return absl::OkStatus();
  }

  absl::Status LabelAlreadyExistsError(const ASTNode* node,
                                       absl::string_view label) const {
    return MakeSqlErrorAt(node) << "Label " << label << " already exists";
  }
  absl::Status LabelNotExitsError(const ASTNode* node,
                                  absl::string_view label) const {
    return MakeSqlErrorAt(node)
           << "Label " << label << " does not exist or is out of scope";
  }

 private:
  // Maps label IdString to corresponding LoopData or BlockData. Each entry
  // is owned by LoopTracker::loop_data_ or BlockTracker::block_data_.
  absl::flat_hash_map<IdString, std::variant<LoopData*, BlockData*>,
                      IdStringCaseHash, IdStringCaseEqualFunc>
      label_data_map_;
};

// This class keeps track of loops and their Loopdata, and forwards calls to
// LabelTracker when appropriate.
class LoopTracker {
 public:
  explicit LoopTracker(LabelTracker* label_tracker)
      : label_tracker_(label_tracker) {}

  absl::StatusOr<LoopData*> EnterLoop(const ASTLoopStatement* node) {
    auto data = std::make_unique<LoopData>();
    if (node->label() != nullptr) {
      GOOGLESQL_RETURN_IF_ERROR(label_tracker_->EnterLoop(node, data.get()));
    }
    loop_data_.push_back(std::move(data));
    return loop_data_.back().get();
  }

  absl::Status ExitLoop(const ASTLoopStatement* node) {
    if (node->label() != nullptr) {
      GOOGLESQL_RETURN_IF_ERROR(
          label_tracker_->ExitLoop(node->label()->name()->GetAsIdString()));
    }
    loop_data_.pop_back();
    return absl::OkStatus();
  }

  absl::Status OnUnlabeledBreak(const ASTBreakStatement* node,
                                NodeData* node_data) {
    GOOGLESQL_RET_CHECK(node->label() == nullptr);
    if (loop_data_.empty()) {
      return MakeSqlErrorAt(node)
             << node->GetKeywordText()
             << " without label is only allowed inside of a loop body";
    }
    loop_data_.back()->break_nodes.push_back(node_data->start);
    return absl::OkStatus();
  }

  absl::Status OnUnlabeledContinue(const ASTContinueStatement* node,
                                   NodeData* node_data) {
    GOOGLESQL_RET_CHECK(node->label() == nullptr);
    if (loop_data_.empty()) {
      return MakeSqlErrorAt(node) << node->GetKeywordText()
                                  << " is only allowed inside of a loop body";
    }
    loop_data_.back()->continue_nodes.push_back(node_data->start);
    return absl::OkStatus();
  }

 private:
  // Stack keeping track of additional pending information on loops
  // that we are currently visiting the body of.
  // One entry per loop statement, with the innermost loop at the back of
  // the list.
  std::vector<std::unique_ptr<LoopData>> loop_data_;

  LabelTracker* label_tracker_;
};

// This class keeps track of blocks and their Blockdata, and forwards calls to
// LabelTracker when appropriate.
class BlockTracker {
 public:
  explicit BlockTracker(LabelTracker* label_tracker)
      : label_tracker_(label_tracker) {}

  absl::StatusOr<BlockData*> EnterBlock(const ASTBeginEndBlock* node) {
    if (node->label() == nullptr) {
      return nullptr;
    }
    auto data = std::make_unique<BlockData>();
    GOOGLESQL_RETURN_IF_ERROR(label_tracker_->EnterBlock(node, data.get()));
    block_data_.push_back(std::move(data));
    return block_data_.back().get();
  }

  absl::Status ExitBlock(const ASTBeginEndBlock* node) {
    if (node->label() != nullptr) {
      GOOGLESQL_RETURN_IF_ERROR(
          label_tracker_->ExitBlock(node->label()->name()->GetAsIdString()));
      block_data_.pop_back();
    }
    return absl::OkStatus();
  }

 private:
  // Stack keeping track of additional pending information on blocks with label
  // that we are currently visiting the body of.
  // One entry per block, with the innermost block at the back of the list.
  std::vector<std::unique_ptr<BlockData>> block_data_;

  LabelTracker* label_tracker_;
};

}  // namespace

// Parse tree visitor to set up a ControlFlowGraph.
class ControlFlowGraphBuilder : public NonRecursiveParseTreeVisitor {
 public:
  explicit ControlFlowGraphBuilder(ControlFlowGraph* graph)
      : loop_tracker_(&label_tracker_),
        block_tracker_(&label_tracker_),
        graph_(graph) {}

  // Removes unreachable nodes, and edges between them, from the graph.
  absl::Status PruneUnreachable() {
    // First, walk the graph to find out all nodes and edges which are
    // reachable.
    absl::flat_hash_set<const ControlFlowNode*> visited_nodes;
    absl::flat_hash_set<const ControlFlowEdge*> visited_edges;

    std::vector<const ControlFlowNode*> stack = {graph_->start_node()};
    while (!stack.empty()) {
      const ControlFlowNode* node = stack.back();
      stack.pop_back();
      if (googlesql_base::InsertIfNotPresent(&visited_nodes, node)) {
        for (const auto& succ : node->successors()) {
          GOOGLESQL_RET_CHECK(googlesql_base::InsertIfNotPresent(&visited_edges, succ.second));
          stack.push_back(succ.second->successor());
        }
      }
    }

    // Remove unreachable edges from the predecessor list of any reachable
    // nodes.
    for (const auto& [ast_node, cfg_node] : graph_->node_map_) {
      if (visited_nodes.contains(cfg_node)) {
        RemoveUnreachablePredecessors(visited_nodes, visited_edges,
                                      cfg_node.get());
      }
    }
    RemoveUnreachablePredecessors(visited_nodes, visited_edges,
                                  graph_->end_node_.get());

    // Delete unreachable edges
    for (auto it = graph_->edges_.begin(); it != graph_->edges_.end();) {
      if (!visited_edges.contains(it->get())) {
        auto it_copy = it;
        ++it;
        graph_->edges_.erase(it_copy);
      } else {
        ++it;
      }
    }

    // Delete unreachable nodes
    for (auto it = graph_->node_map_.begin(); it != graph_->node_map_.end();) {
      if (!visited_nodes.contains(it->second.get())) {
        auto it_copy = it;
        ++it;
        graph_->node_map_.erase(it_copy);
      } else {
        ++it;
      }
    }

    if (!visited_nodes.contains(graph_->end_node_.get())) {
      graph_->end_node_.reset();
    }

    return absl::OkStatus();
  }

  absl::StatusOr<VisitResult> visitASTBreakStatement(
      const ASTBreakStatement* node) override {
    GOOGLESQL_ASSIGN_OR_RETURN(NodeData * node_data,
                     AddNodeDataAndGraphNode(node, ThrowSemantics::kNoThrow));
    if (node->label() == nullptr) {
      GOOGLESQL_RETURN_IF_ERROR(loop_tracker_.OnUnlabeledBreak(node, node_data));
    } else {
      GOOGLESQL_RETURN_IF_ERROR(label_tracker_.OnLabeledBreak(node, node_data));
    }
    return VisitResult::Empty();
  }

  absl::StatusOr<VisitResult> visitASTContinueStatement(
      const ASTContinueStatement* node) override {
    GOOGLESQL_ASSIGN_OR_RETURN(NodeData * node_data,
                     AddNodeDataAndGraphNode(node, ThrowSemantics::kNoThrow));
    if (node->label() == nullptr) {
      GOOGLESQL_RETURN_IF_ERROR(loop_tracker_.OnUnlabeledContinue(node, node_data));
    } else {
      GOOGLESQL_RETURN_IF_ERROR(label_tracker_.OnLabeledContinue(node, node_data));
    }
    return VisitResult::Empty();
  }

  absl::StatusOr<VisitResult> visitASTRaiseStatement(
      const ASTRaiseStatement* node) override {
    GOOGLESQL_RETURN_IF_ERROR(
        AddNodeDataAndGraphNode(node, ThrowSemantics::kMustThrow).status());
    return VisitResult::Empty();
  }

  absl::StatusOr<VisitResult> visitASTReturnStatement(
      const ASTReturnStatement* node) override {
    GOOGLESQL_ASSIGN_OR_RETURN(NodeData * node_data,
                     AddNodeDataAndGraphNode(node, ThrowSemantics::kNoThrow));
    GOOGLESQL_RETURN_IF_ERROR(LinkNodes(node_data->start, graph_->end_node_.get(),
                              ControlFlowEdge::Kind::kNormal,
                              graph_->ast_script()));
    return VisitResult::Empty();
  }

  absl::StatusOr<VisitResult> defaultVisit(const ASTNode* node) override {
    if (node->IsStatement()) {
      const ASTStatement* stmt = node->GetAsOrDie<ASTStatement>();
      if (DoesASTNodeHaveCFGNode(stmt)) {
        GOOGLESQL_RETURN_IF_ERROR(
            AddNodeDataAndGraphNode(node->GetAsOrDie<ASTStatement>()).status());
      } else {
        // Unsupported scripting statement
        return googlesql_base::UnimplementedErrorBuilder()
               << "ControlFlowGraphBuilder: (" << DebugNodeIdentifier(node)
               << "): "
               << "Script statement kind " << node->SingleNodeDebugString()
               << " not implemented";
      }
      return VisitResult::Empty();
    }
    return VisitResult::VisitChildren(node);
  }

  absl::StatusOr<VisitResult> visitASTExceptionHandlerList(
      const ASTExceptionHandlerList* node) override {
    return VisitResult::VisitChildren(node);
  }

  absl::StatusOr<VisitResult> visitASTExceptionHandler(
      const ASTExceptionHandler* node) override {
    return VisitResult::VisitChildren(node);
  }

  absl::StatusOr<VisitResult> visitASTStatementList(
      const ASTStatementList* node) override {
    // Check if this block adds any new exception handlers.
    bool try_block = false;
    if (node->parent()->node_kind() == AST_BEGIN_END_BLOCK &&
        node->parent()
            ->GetAsOrDie<ASTBeginEndBlock>()
            ->has_exception_handler()) {
      GOOGLESQL_RET_CHECK(node->parent()
                    ->GetAsOrDie<ASTBeginEndBlock>()
                    ->statement_list_node() == node)
          << "Node should be the statement list of its enclosing block";
      exception_handler_data_stack_.push_back(
          exception_handler_block_data_map_
              .at(node->parent()->GetAsOrDie<ASTBeginEndBlock>())
              .get());
      try_block = true;
    }

    return VisitResult::VisitChildren(node, [=]() -> absl::Status {
      if (try_block) {
        GOOGLESQL_RET_CHECK(node->parent()
                      ->GetAsOrDie<ASTBeginEndBlock>()
                      ->statement_list_node() == node)
            << "Node should be the statement list of its enclosing block";
        exception_handler_data_stack_.pop_back();
      }

      GOOGLESQL_ASSIGN_OR_RETURN(NodeData * stmt_list_node_data, CreateNodeData(node));
      // Link together each of the statements in the list
      std::unique_ptr<NodeData> prev_nonempty_child_data;
      std::unique_ptr<NodeData> curr_child_data;

      for (int i = 0; i < node->num_children(); ++i) {
        GOOGLESQL_ASSIGN_OR_RETURN(curr_child_data, TakeNodeData(node->child(i)));
        if (curr_child_data->empty()) {
          continue;
        }

        if (prev_nonempty_child_data != nullptr &&
            !prev_nonempty_child_data->IsSpecialControlFlowStatement()) {
          GOOGLESQL_RETURN_IF_ERROR(LinkEndNodes(prev_nonempty_child_data.get(),
                                       curr_child_data->start, node));
        }

        if (stmt_list_node_data->start == nullptr) {
          stmt_list_node_data->start = curr_child_data->start;
        }
        prev_nonempty_child_data = std::move(curr_child_data);
      }

      if (!stmt_list_node_data->empty() &&
          !prev_nonempty_child_data->IsSpecialControlFlowStatement()) {
        stmt_list_node_data->end_edges =
            std::move(prev_nonempty_child_data->end_edges);
      }

      return absl::OkStatus();
    });
  }

  absl::StatusOr<VisitResult> visitASTScript(const ASTScript* node) override {
    graph_->end_node_ = absl::WrapUnique(
        new ControlFlowNode(nullptr, ControlFlowNode::Kind::kDefault, graph_));
    return VisitResult::VisitChildren(node, [=]() -> absl::Status {
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const NodeData> stmt_list_data,
                       TakeNodeData(node->statement_list_node()));
      if (stmt_list_data->empty()) {
        // The entire script does not execute any code.
        graph_->start_node_ = graph_->end_node();
      } else {
        graph_->start_node_ = stmt_list_data->start;
      }
      GOOGLESQL_RETURN_IF_ERROR(
          LinkEndNodes(stmt_list_data.get(), graph_->end_node_.get(), node));
      return absl::OkStatus();
    });
  }

  absl::StatusOr<VisitResult> visitASTIfStatement(
      const ASTIfStatement* node) override {
    return VisitResult::VisitChildren(node, [=]() -> absl::Status {
      GOOGLESQL_ASSIGN_OR_RETURN(NodeData * if_stmt_node_data, CreateNodeData(node));
      GOOGLESQL_ASSIGN_OR_RETURN(ControlFlowNode * condition, AddGraphNode(node));
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<NodeData> then_list_data,
                       TakeNodeData(node->then_list()));

      if_stmt_node_data->start = condition;
      if (then_list_data->empty()) {
        if_stmt_node_data->AddOpenEndEdge(
            condition, ControlFlowEdge::Kind::kTrueCondition);
      } else {
        GOOGLESQL_RETURN_IF_ERROR(LinkNodes(condition, then_list_data->start,
                                  ControlFlowEdge::Kind::kTrueCondition));
      }
      if_stmt_node_data->TakeEndEdgesFrom(then_list_data.get());

      ControlFlowNode* prev_condition = condition;
      if (node->elseif_clauses() != nullptr) {
        for (const ASTElseifClause* elseif_clause :
             node->elseif_clauses()->elseif_clauses()) {
          GOOGLESQL_ASSIGN_OR_RETURN(ControlFlowNode * curr_condition,
                           AddGraphNode(elseif_clause));
          GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<NodeData> body_data,
                           TakeNodeData(elseif_clause->body()));

          GOOGLESQL_RETURN_IF_ERROR(LinkNodes(prev_condition, curr_condition,
                                    ControlFlowEdge::Kind::kFalseCondition));
          if (!body_data->empty()) {
            GOOGLESQL_RETURN_IF_ERROR(LinkNodes(curr_condition, body_data->start,
                                      ControlFlowEdge::Kind::kTrueCondition));
          } else {
            if_stmt_node_data->AddOpenEndEdge(
                curr_condition, ControlFlowEdge::Kind::kTrueCondition);
          }
          if_stmt_node_data->TakeEndEdgesFrom(body_data.get());
          prev_condition = curr_condition;
        }
      }

      bool has_nonempty_else = false;
      if (node->else_list() != nullptr) {
        GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<NodeData> else_data,
                         TakeNodeData(node->else_list()));
        if (!else_data->empty()) {
          has_nonempty_else = true;
          GOOGLESQL_RETURN_IF_ERROR(LinkNodes(prev_condition, else_data->start,
                                    ControlFlowEdge::Kind::kFalseCondition));
        }
        if_stmt_node_data->TakeEndEdgesFrom(else_data.get());
      }
      if (!has_nonempty_else) {
        if_stmt_node_data->AddOpenEndEdge(
            prev_condition, ControlFlowEdge::Kind::kFalseCondition);
      }
      return absl::OkStatus();
    });
  }

  absl::StatusOr<VisitResult> visitASTCaseStatement(
      const ASTCaseStatement* node) override {
    return VisitResult::VisitChildren(node, [=]() -> absl::Status {
      GOOGLESQL_ASSIGN_OR_RETURN(NodeData * case_stmt_node_data, CreateNodeData(node));
      GOOGLESQL_ASSIGN_OR_RETURN(ControlFlowNode * case_stmt_cfg_node,
                       AddGraphNode(node, node->expression() == nullptr
                                              ? ThrowSemantics::kNoThrow
                                              : ThrowSemantics::kCanThrow));
      case_stmt_node_data->start = case_stmt_cfg_node;
      ControlFlowNode* prev_condition = nullptr;

      GOOGLESQL_RET_CHECK(node->when_then_clauses() != nullptr);
      for (const ASTWhenThenClause* when_then_clause :
           node->when_then_clauses()->when_then_clauses()) {
        GOOGLESQL_ASSIGN_OR_RETURN(ControlFlowNode * curr_condition,
                         AddGraphNode(when_then_clause));
        GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<NodeData> body_data,
                         TakeNodeData(when_then_clause->body()));

        if (prev_condition == nullptr) {
          // First condition
          GOOGLESQL_RETURN_IF_ERROR(LinkNodes(case_stmt_cfg_node, curr_condition,
                                    ControlFlowEdge::Kind::kNormal,
                                    /*exit_to=*/nullptr));
        } else {
          // All subsequent conditions
          GOOGLESQL_RETURN_IF_ERROR(LinkNodes(prev_condition, curr_condition,
                                    ControlFlowEdge::Kind::kFalseCondition));
        }
        if (!body_data->empty()) {
          GOOGLESQL_RETURN_IF_ERROR(LinkNodes(curr_condition, body_data->start,
                                    ControlFlowEdge::Kind::kTrueCondition));
        } else {
          case_stmt_node_data->AddOpenEndEdge(
              curr_condition, ControlFlowEdge::Kind::kTrueCondition);
        }
        case_stmt_node_data->TakeEndEdgesFrom(body_data.get());
        prev_condition = curr_condition;
      }

      bool has_nonempty_else = false;
      if (node->else_list() != nullptr) {
        GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<NodeData> else_data,
                         TakeNodeData(node->else_list()));
        if (!else_data->empty()) {
          has_nonempty_else = true;
          GOOGLESQL_RETURN_IF_ERROR(LinkNodes(prev_condition, else_data->start,
                                    ControlFlowEdge::Kind::kFalseCondition));
        }
        case_stmt_node_data->TakeEndEdgesFrom(else_data.get());
      }
      if (!has_nonempty_else) {
        case_stmt_node_data->AddOpenEndEdge(
            prev_condition, ControlFlowEdge::Kind::kFalseCondition);
      }
      return absl::OkStatus();
    });
  }

  absl::StatusOr<VisitResult> visitASTWhileStatement(
      const ASTWhileStatement* node) override {
    GOOGLESQL_ASSIGN_OR_RETURN(LoopData * loop_data, loop_tracker_.EnterLoop(node));
    return VisitResult::VisitChildren(node, [=]() -> absl::Status {
      ControlFlowNode* loop_cfg_node = nullptr;
      GOOGLESQL_ASSIGN_OR_RETURN(loop_cfg_node, AddGraphNode(node));
      GOOGLESQL_ASSIGN_OR_RETURN(NodeData * while_stmt_node_data, CreateNodeData(node));
      while_stmt_node_data->start = loop_cfg_node;
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const NodeData> body_data,
                       TakeNodeData(node->body()));
      if (node->condition() != nullptr) {
        GOOGLESQL_RETURN_IF_ERROR(
            LinkNodes(loop_cfg_node,
                      body_data->empty() ? loop_cfg_node : body_data->start,
                      ControlFlowEdge::Kind::kTrueCondition,
                      /*exit_to=*/nullptr));
        GOOGLESQL_RETURN_IF_ERROR(LinkEndNodes(body_data.get(), loop_cfg_node, node));
        while_stmt_node_data->AddOpenEndEdge(
            loop_cfg_node, ControlFlowEdge::Kind::kFalseCondition);
      } else {
        GOOGLESQL_RETURN_IF_ERROR(
            LinkNodes(loop_cfg_node,
                      body_data->empty() ? loop_cfg_node : body_data->start,
                      ControlFlowEdge::Kind::kNormal,
                      /*exit_to=*/nullptr));
        GOOGLESQL_RETURN_IF_ERROR(LinkEndNodes(body_data.get(), loop_cfg_node, node));
      }

      // Handle BREAK/CONTINUE statements inside the loop.
      for (ControlFlowNode* break_node : loop_data->break_nodes) {
        while_stmt_node_data->AddOpenEndEdge(break_node,
                                             ControlFlowEdge::Kind::kNormal);
      }
      for (ControlFlowNode* continue_node : loop_data->continue_nodes) {
        GOOGLESQL_RETURN_IF_ERROR(LinkNodes(continue_node, loop_cfg_node,
                                  ControlFlowEdge::Kind::kNormal, node));
      }
      GOOGLESQL_RETURN_IF_ERROR(loop_tracker_.ExitLoop(node));
      return absl::OkStatus();
    });
  }

  absl::StatusOr<VisitResult> visitASTRepeatStatement(
      const ASTRepeatStatement* node) override {
    GOOGLESQL_ASSIGN_OR_RETURN(LoopData * loop_data, loop_tracker_.EnterLoop(node));
    return VisitResult::VisitChildren(node, [=]() -> absl::Status {
      GOOGLESQL_ASSIGN_OR_RETURN(ControlFlowNode * repeat_cfg_node,
                       AddGraphNode(node, ThrowSemantics::kNoThrow));
      GOOGLESQL_ASSIGN_OR_RETURN(ControlFlowNode * until_cfg_node,
                       AddGraphNode(node->until_clause()));
      GOOGLESQL_ASSIGN_OR_RETURN(NodeData * repeat_stmt_node_data, CreateNodeData(node));
      repeat_stmt_node_data->start = repeat_cfg_node;
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const NodeData> body_data,
                       TakeNodeData(node->body()));

      GOOGLESQL_RETURN_IF_ERROR(
          LinkNodes(repeat_cfg_node,
                    body_data->empty() ? until_cfg_node : body_data->start,
                    ControlFlowEdge::Kind::kNormal,
                    /*exit_to=*/nullptr));
      GOOGLESQL_RETURN_IF_ERROR(LinkEndNodes(body_data.get(), until_cfg_node, node));
      GOOGLESQL_RETURN_IF_ERROR(LinkNodes(until_cfg_node, repeat_cfg_node,
                                ControlFlowEdge::Kind::kFalseCondition,
                                /*exit_to=*/nullptr));
      repeat_stmt_node_data->AddOpenEndEdge(
          until_cfg_node, ControlFlowEdge::Kind::kTrueCondition);

      // Handle BREAK/CONTINUE statements inside the loop.
      for (ControlFlowNode* break_node : loop_data->break_nodes) {
        repeat_stmt_node_data->AddOpenEndEdge(break_node,
                                              ControlFlowEdge::Kind::kNormal);
      }
      for (ControlFlowNode* continue_node : loop_data->continue_nodes) {
        GOOGLESQL_RETURN_IF_ERROR(LinkNodes(continue_node, until_cfg_node,
                                  ControlFlowEdge::Kind::kNormal, node));
      }
      GOOGLESQL_RETURN_IF_ERROR(loop_tracker_.ExitLoop(node));
      return absl::OkStatus();
    });
  }

  absl::StatusOr<VisitResult> visitASTForInStatement(
      const ASTForInStatement* node) override {
    GOOGLESQL_ASSIGN_OR_RETURN(LoopData * loop_data, loop_tracker_.EnterLoop(node));
    return VisitResult::VisitChildren(node, [=]() -> absl::Status {
      GOOGLESQL_ASSIGN_OR_RETURN(ControlFlowNode * initial_cfg_node,
                       AddGraphNode(node, ControlFlowNode::Kind::kForInitial));
      GOOGLESQL_ASSIGN_OR_RETURN(NodeData * for_in_stmt_node_data, CreateNodeData(node));
      for_in_stmt_node_data->start = initial_cfg_node;
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const NodeData> body_data,
                       TakeNodeData(node->body()));

      // If there are more rows from the query, node will evaluate to
      // kTrueCondition. If not, node will evaluate to kFalseCondition.
      GOOGLESQL_ASSIGN_OR_RETURN(ControlFlowNode * continue_cfg_node,
                       AddGraphNode(node, ControlFlowNode::Kind::kForAdvance));
      GOOGLESQL_RETURN_IF_ERROR(
          LinkNodes(initial_cfg_node,
                    body_data->empty() ? continue_cfg_node : body_data->start,
                    ControlFlowEdge::Kind::kTrueCondition,
                    /*exit_to=*/nullptr));
      GOOGLESQL_RETURN_IF_ERROR(
          LinkNodes(continue_cfg_node,
                    body_data->empty() ? continue_cfg_node : body_data->start,
                    ControlFlowEdge::Kind::kTrueCondition,
                    /*exit_to=*/nullptr));
      GOOGLESQL_RETURN_IF_ERROR(LinkEndNodes(body_data.get(), continue_cfg_node, node));
      for_in_stmt_node_data->AddOpenEndEdge(
          initial_cfg_node, ControlFlowEdge::Kind::kFalseCondition);
      for_in_stmt_node_data->AddOpenEndEdge(
          continue_cfg_node, ControlFlowEdge::Kind::kFalseCondition);

      // Handle BREAK/CONTINUE statements inside the loop.
      for (ControlFlowNode* break_node : loop_data->break_nodes) {
        for_in_stmt_node_data->AddOpenEndEdge(break_node,
                                              ControlFlowEdge::Kind::kNormal);
      }
      for (ControlFlowNode* continue_node : loop_data->continue_nodes) {
        GOOGLESQL_RETURN_IF_ERROR(LinkNodes(continue_node, continue_cfg_node,
                                  ControlFlowEdge::Kind::kNormal, node));
      }
      GOOGLESQL_RETURN_IF_ERROR(loop_tracker_.ExitLoop(node));
      return absl::OkStatus();
    });
  }

  absl::StatusOr<VisitResult> visitASTBeginEndBlock(
      const ASTBeginEndBlock* node) override {
    if (node->has_exception_handler()) {
      exception_handler_block_data_map_[node] =
          std::make_unique<BlockWithExceptionHandlerData>();
    }
    GOOGLESQL_ASSIGN_OR_RETURN(BlockData * block_data, block_tracker_.EnterBlock(node));
    return VisitResult::VisitChildren(node, [=]() -> absl::Status {
      GOOGLESQL_ASSIGN_OR_RETURN(NodeData * node_data, CreateNodeData(node));

      // Create a node for entering the block.
      GOOGLESQL_ASSIGN_OR_RETURN(ControlFlowNode * begin_entry_node,
                       AddGraphNode(node, ThrowSemantics::kNoThrow));
      node_data->start = begin_entry_node;

      // Handle the regular statement list
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<NodeData> stmt_list_data,
                       TakeNodeData(node->statement_list_node()));
      if (stmt_list_data->empty()) {
        node_data->AddOpenEndEdge(begin_entry_node,
                                  ControlFlowEdge::Kind::kNormal);
      } else {
        GOOGLESQL_RETURN_IF_ERROR(LinkNodes(begin_entry_node, stmt_list_data->start,
                                  ControlFlowEdge::Kind::kNormal));
      }

      node_data->TakeEndEdgesFrom(stmt_list_data.get());

      if (node->has_exception_handler()) {
        GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<NodeData> handler_stmt_list_data,
                         TakeNodeData(node->handler_list()
                                          ->exception_handler_list()
                                          .front()
                                          ->statement_list()));

        // As end edges from the exception handler terminate the block,
        // propagate them to the block.  Don't mark the current block as
        // "exited" because the variable scope has already exited when the
        // primary statement list terminated, earlier.  We do, however, need
        // to increment the exited-exception-handler count, since the handler
        // is being exited.
        node_data->TakeEndEdgesFrom(handler_stmt_list_data.get());

        // Add edges from all statements in the block (which can throw) to the
        // start of the handler.
        BlockWithExceptionHandlerData* block_data =
            exception_handler_block_data_map_.at(node).get();
        for (ControlFlowNode* cfg_node : block_data->handled_nodes) {
          // Add the edge from <cfg_node> to the exception handler.
          if (!handler_stmt_list_data->empty()) {
            GOOGLESQL_RETURN_IF_ERROR(LinkNodes(cfg_node, handler_stmt_list_data->start,
                                      ControlFlowEdge::Kind::kException, node));
          } else {
            node_data->AddOpenEndEdge(cfg_node,
                                      ControlFlowEdge::Kind::kException);
          }
        }
        exception_handler_block_data_map_.erase(node);
      }
      if (node->label() != nullptr) {
        // Handle BREAK with label.
        GOOGLESQL_RET_CHECK(block_data != nullptr);
        for (ControlFlowNode* break_node : block_data->break_nodes) {
          node_data->AddOpenEndEdge(break_node, ControlFlowEdge::Kind::kNormal);
        }
      }
      GOOGLESQL_RETURN_IF_ERROR(block_tracker_.ExitBlock(node));
      return absl::OkStatus();
    });
  }

 private:
  // Looks up, returns, and removes from the map, the NodeData object associated
  // with <node> from the map. TakeNodeData() may be called only once per AST
  // node.
  absl::StatusOr<std::unique_ptr<NodeData>> TakeNodeData(const ASTNode* node) {
    auto it = node_data_.find(node);
    if (it == node_data_.end()) {
      GOOGLESQL_RET_CHECK_FAIL() << "Unable to locate node data for "
                       << DebugNodeIdentifier(node);
    }
    std::unique_ptr<NodeData> result = std::move(it->second);
    node_data_.erase(it);
    return result;
  }

  // Creates an empty NodeData object for the given AST node and inserts it into
  // the NodeData map.
  absl::StatusOr<NodeData*> CreateNodeData(const ASTNode* node) {
    auto pair = node_data_.emplace(node, std::make_unique<NodeData>());
    if (!pair.second) {
      return googlesql_base::InternalErrorBuilder()
             << "Node data for " << DebugNodeIdentifier(node)
             << " already exists";
    }
    pair.first->second->ast_node = node;
    return pair.first->second.get();
  }

  bool DoesASTNodeHaveCFGNode(const ASTStatement* node) {
    switch (node->node_kind()) {
      case AST_VARIABLE_DECLARATION:
      case AST_SYSTEM_VARIABLE_ASSIGNMENT:
      case AST_SINGLE_ASSIGNMENT:
      case AST_ASSIGNMENT_FROM_STRUCT:
      case AST_BREAK_STATEMENT:
      case AST_CONTINUE_STATEMENT:
        return true;
      default:
        // TODO: Also consider RETURN statements and IF/WHILE
        // condition nodes.
        return node->IsSqlStatement();
    }
  }
  std::string DebugNodeIdentifier(const ASTNode* node) {
    ABSL_CHECK(node != nullptr);
    return googlesql::DebugNodeIdentifier(node, graph_->script_text());
  }

  absl::StatusOr<ControlFlowNode*> AddGraphNode(
      const ASTNode* ast_node, ThrowSemantics throw_semantics) {
    return AddGraphNode(ast_node, ControlFlowNode::Kind::kDefault,
                        throw_semantics);
  }

  absl::StatusOr<ControlFlowNode*> AddGraphNode(
      const ASTNode* ast_node,
      ControlFlowNode::Kind kind = ControlFlowNode::Kind::kDefault,
      ThrowSemantics throw_semantics = ThrowSemantics::kCanThrow) {
    auto emplace_result = graph_->node_map_.emplace(
        std::make_pair(ast_node, kind),
        absl::WrapUnique(new ControlFlowNode(ast_node, kind, graph_)));
    if (!emplace_result.second) {
      return googlesql_base::InternalErrorBuilder()
             << "Graph node already exists for AST node "
             << DebugNodeIdentifier(ast_node);
    }
    ControlFlowNode* cfg_node = emplace_result.first->second.get();
    if (throw_semantics != ThrowSemantics::kNoThrow &&
        !exception_handler_data_stack_.empty()) {
      exception_handler_data_stack_.back()->handled_nodes.push_back(cfg_node);
    }
    return cfg_node;
  }

  absl::StatusOr<NodeData*> AddNodeDataAndGraphNode(
      const ASTStatement* ast_stmt,
      ThrowSemantics throw_semantics = ThrowSemantics::kCanThrow) {
    GOOGLESQL_ASSIGN_OR_RETURN(ControlFlowNode * cfg_node,
                     AddGraphNode(ast_stmt, throw_semantics));
    GOOGLESQL_ASSIGN_OR_RETURN(NodeData * node_data, CreateNodeData(ast_stmt));
    node_data->start = cfg_node;
    if (throw_semantics != ThrowSemantics::kMustThrow) {
      node_data->AddOpenEndEdge(cfg_node, ControlFlowEdge::Kind::kNormal);
    }
    return node_data;
  }

  absl::Status LinkEndNodes(const NodeData* pred, ControlFlowNode* succ,
                            const ASTNode* exit_to) {
    ABSL_CHECK(succ != nullptr);
    for (const auto& edge : pred->end_edges) {
      GOOGLESQL_RETURN_IF_ERROR(LinkNodes(edge.predecessor, succ, edge.kind, exit_to));
    }
    return absl::OkStatus();
  }

  absl::Status LinkNodes(ControlFlowNode* cfg_pred, ControlFlowNode* cfg_succ,
                         ControlFlowEdge::Kind kind,
                         const ASTNode* exit_to = nullptr) {
    ABSL_CHECK(cfg_pred != nullptr);
    ABSL_CHECK(cfg_succ != nullptr);
    if (kind == ControlFlowEdge::Kind::kException) {
      // Everything can throw except for begin, repeat, break, continue, return,
      // and empty statement list.
      GOOGLESQL_RET_CHECK(cfg_pred->ast_node()->node_kind() != AST_BEGIN_END_BLOCK &&
                cfg_pred->ast_node()->node_kind() != AST_REPEAT_STATEMENT &&
                cfg_pred->ast_node()->node_kind() != AST_BREAK_STATEMENT &&
                cfg_pred->ast_node()->node_kind() != AST_CONTINUE_STATEMENT &&
                cfg_pred->ast_node()->node_kind() != AST_RETURN_STATEMENT &&
                cfg_pred->ast_node()->node_kind() != AST_STATEMENT_LIST)
          << "Unexpected node kind throwing exception: "
          << cfg_pred->ast_node()->SingleNodeDebugString();
    } else if (cfg_pred->ast_node()->node_kind() == AST_FOR_IN_STATEMENT ||
               cfg_pred->ast_node()->node_kind() == AST_WHEN_THEN_CLAUSE ||
               cfg_pred->ast_node()->node_kind() == AST_IF_STATEMENT ||
               cfg_pred->ast_node()->node_kind() == AST_ELSEIF_CLAUSE ||
               cfg_pred->ast_node()->node_kind() == AST_UNTIL_CLAUSE ||
               (cfg_pred->ast_node()->node_kind() == AST_WHILE_STATEMENT &&
                cfg_pred->ast_node()
                        ->GetAsOrDie<ASTWhileStatement>()
                        ->condition() != nullptr)) {
      // Although AST_FOR_IN_STATEMENT is not strictly a condition, it is
      // treated as one based on whether there are more rows from the query.
      GOOGLESQL_RET_CHECK(kind == ControlFlowEdge::Kind::kTrueCondition ||
                kind == ControlFlowEdge::Kind::kFalseCondition)
          << "conditional statement must use true/false condition"
          << cfg_pred->DebugString();
    } else if (cfg_pred->ast_node()->IsStatement() ||
               cfg_pred->ast_node()->node_kind() == AST_STATEMENT_LIST) {
      GOOGLESQL_RET_CHECK(kind == ControlFlowEdge::Kind::kNormal)
          << "Unconditional statement must use normal edge"
          << cfg_pred->DebugString();
    } else {
      GOOGLESQL_RET_CHECK_FAIL() << "unexpected ast node: "
                       << cfg_pred->ast_node()->GetNodeKindString();
    }
    std::unique_ptr<ControlFlowEdge> edge_holder = absl::WrapUnique(
        new ControlFlowEdge(cfg_pred, cfg_succ, kind, exit_to, graph_));
    const ControlFlowEdge* edge = edge_holder.get();
    graph_->edges_.insert(std::move(edge_holder));

    // Mark edge as successor of predecessor
    if (!cfg_pred->successors_.emplace(kind, edge).second) {
      return googlesql_base::InternalErrorBuilder()
             << "Node " << cfg_pred->DebugString()
             << " already contains a successor with kind "
             << ControlFlowEdgeKindString(kind) << ": "
             << cfg_pred->successors_.at(kind)->successor()->DebugString();
    }

    // Mark edge as predecessor of successor
    cfg_succ->predecessors_.emplace_back(edge);

    return absl::OkStatus();
  }

  void RemoveUnreachablePredecessors(
      const absl::flat_hash_set<const ControlFlowNode*>& reachable_nodes,
      const absl::flat_hash_set<const ControlFlowEdge*>& reachable_edges,
      ControlFlowNode* node) {
    std::vector<const ControlFlowEdge*> reachable_predecessors;
    reachable_predecessors.reserve(node->predecessors().size());
    for (const ControlFlowEdge* pred : node->predecessors()) {
      if (reachable_edges.contains(pred)) {
        reachable_predecessors.push_back(pred);
      }
    }

    if (reachable_predecessors.size() != node->predecessors().size()) {
      node->predecessors_ = reachable_predecessors;
    }
  }

  // Keeps track of temporary information about each AST node needed while
  // building the graph.
  absl::flat_hash_map<const ASTNode*, std::unique_ptr<NodeData>> node_data_;

  // Maps each ASTBeginEndBlock with an exception handler to its associated
  // BlockWithExceptionHandlerData.  The map is built up as we traverse the
  // block; when a block is finished being visted, it is used to add edges from
  // each statement in the block to the start of the exception handler.
  absl::flat_hash_map<const ASTBeginEndBlock*,
                      std::unique_ptr<BlockWithExceptionHandlerData>>
      exception_handler_block_data_map_;

  // Stack keeping track of the statements that might throw in each block with
  // an exception handler.  One entry per BEGIN block with an exception handler,
  // with the innermost block at the back of the list.
  // Each entry is owned by <exception_handler_block_data_map_>.
  std::vector<BlockWithExceptionHandlerData*> exception_handler_data_stack_;

  LabelTracker label_tracker_;
  LoopTracker loop_tracker_;
  BlockTracker block_tracker_;

  // The ControlFlowGraph being built.
  ControlFlowGraph* graph_;
};

absl::StatusOr<std::unique_ptr<const ControlFlowGraph>>
ControlFlowGraph::Create(const ASTScript* ast_script,
                         absl::string_view script_text) {
  auto graph = absl::WrapUnique(new ControlFlowGraph(ast_script, script_text));
  ControlFlowGraphBuilder builder(graph.get());
  GOOGLESQL_RETURN_IF_ERROR(ast_script->TraverseNonRecursive(&builder));
  GOOGLESQL_RETURN_IF_ERROR(builder.PruneUnreachable());
  return graph;
}

ControlFlowGraph::ControlFlowGraph(const ASTScript* ast_script,
                                   absl::string_view script_text)
    : start_node_(nullptr),
      ast_script_(ast_script),
      script_text_(script_text) {}

namespace {
void AddDestroyedVariables(const ASTStatementList* stmt_list,
                           const ASTVariableDeclaration* current_stmt,
                           std::set<std::string>* destroyed_variables) {
  for (const ASTStatement* stmt : stmt_list->statement_list()) {
    if (stmt->node_kind() != AST_VARIABLE_DECLARATION) {
      // Variable declarations are allowed only at the start of a block, so
      // no more variable declarations in this block can exist.
      break;
    }
    for (const ASTIdentifier* id : stmt->GetAs<ASTVariableDeclaration>()
                                       ->variable_list()
                                       ->identifier_list()) {
      destroyed_variables->emplace(id->GetAsString());
    }

    if (stmt == current_stmt) {
      // Ignore variable declarations in the current block, after the current
      // statement, since they haven't executed yet.  Note that we can't skip
      // variables in the current statement itself, since it is possible for
      // a DECLARE statement, itself, to fail after some variables have been
      // added, but not others.
      break;
    }
  }
}
}  // namespace

ControlFlowEdge::SideEffects ControlFlowEdge::ComputeSideEffects() const {
  SideEffects side_effects;
  if (exit_to_ == nullptr) {
    return side_effects;
  }
  const ASTVariableDeclaration* current_decl_stmt =
      predecessor_->ast_node()->GetAsOrNull<ASTVariableDeclaration>();

  for (const ASTNode* node = predecessor_->ast_node();
       node != exit_to_ && node != nullptr; node = node->parent()) {
    switch (node->node_kind()) {
      case AST_STATEMENT_LIST:
        if (node->parent()->node_kind() == AST_BEGIN_END_BLOCK) {
          AddDestroyedVariables(node->GetAsOrDie<ASTStatementList>(),
                                current_decl_stmt,
                                &side_effects.destroyed_variables);
        }
        break;
      case AST_EXCEPTION_HANDLER:
        ++side_effects.num_exception_handlers_exited;
        break;
      case AST_FOR_IN_STATEMENT:
        ++side_effects.num_for_loops_exited;
        side_effects.destroyed_variables.emplace(
            node->GetAsOrDie<ASTForInStatement>()->variable()->GetAsString());
        break;
      default:
        break;
    }
  }

  for (const ASTNode* node = successor_->ast_node();
       node != exit_to_ && node != nullptr; node = node->parent()) {
    if (node->node_kind() == AST_EXCEPTION_HANDLER) {
      side_effects.exception_handler_entered = true;
      break;
    }
  }
  return side_effects;
}

namespace {
void AddSideEffectsToDebugString(const ControlFlowEdge& edge,
                                 std::string* debug_string) {
  ControlFlowEdge::SideEffects side_effects = edge.ComputeSideEffects();
  if (!side_effects.destroyed_variables.empty()) {
    std::vector<std::string> destroyed_variables_vector(
        side_effects.destroyed_variables.begin(),
        side_effects.destroyed_variables.end());
    std::sort(destroyed_variables_vector.begin(),
              destroyed_variables_vector.end());
    absl::StrAppend(debug_string, " [destroying ",
                    absl::StrJoin(destroyed_variables_vector, ", "), "]");
  }
  if (side_effects.exception_handler_entered) {
    absl::StrAppend(debug_string, " [entering exception handler]");
  }
  if (side_effects.num_exception_handlers_exited != 0) {
    absl::StrAppend(debug_string, " [exiting ",
                    side_effects.num_exception_handlers_exited,
                    " exception handler(s)]");
  }
  if (side_effects.num_for_loops_exited != 0) {
    absl::StrAppend(debug_string, " [exiting ",
                    side_effects.num_for_loops_exited, " FOR loop(s)]");
  }
}
}  // namespace

std::string ControlFlowEdge::DebugString() const {
  std::string debug_string;
  absl::StrAppend(&debug_string, predecessor_->DebugString(), " => (",
                  ControlFlowEdgeKindString(kind_), ") ",
                  successor_->DebugString());
  AddSideEffectsToDebugString(*this, &debug_string);

  return debug_string;
}

namespace {

ParseLocationPoint GetCanonicalNodePosition(const ControlFlowNode* node) {
  switch (node->kind()) {
    case ControlFlowNode::Kind::kDefault:
    case ControlFlowNode::Kind::kForInitial:
      return node->ast_node()->location().start();
    case ControlFlowNode::Kind::kForAdvance:
      return node->ast_node()->location().end();
  }
}

bool CompareControlFlowNodesByScriptLocation(const ControlFlowNode* node1,
                                             const ControlFlowNode* node2) {
  // A nullptr AST node means the sentinal end node; this should always come
  // last.
  if (node1->ast_node() == nullptr && node2->ast_node() == nullptr) {
    return false;
  }
  if (node1->ast_node() != nullptr && node2->ast_node() == nullptr) {
    return true;
  }
  if (node2->ast_node() != nullptr && node1->ast_node() == nullptr) {
    return false;
  }

  int node1_offset = GetCanonicalNodePosition(node1).GetByteOffset();
  int node2_offset = GetCanonicalNodePosition(node2).GetByteOffset();

  if (node1_offset < node2_offset) {
    return true;
  }

  if (node1_offset > node2_offset) {
    return false;
  }

  // For two identical ast nodes, order by kind.
  return static_cast<int>(node1->kind()) < static_cast<int>(node2->kind());
}
}  // namespace

std::string ControlFlowNode::DebugString() const {
  if (ast_node_ != nullptr) {
    if (ast_node_->node_kind() == AST_STATEMENT_LIST) {
      // We normally don't generate control-flow nodes for statement lists, but
      // empty loop bodies (without a condition) are an exception, since it's
      // the only way to represent an infinite loop that doesn't execute any
      // code.
      return absl::StrCat("<empty loop body>",
                          DebugLocationText(ast_node_, graph_->script_text()));
    }
    std::string result = DebugNodeIdentifier(ast_node_, graph_->script_text());
    switch (kind_) {
      case Kind::kDefault:
        break;
      case Kind::kForInitial:
        absl::StrAppend(&result, " (initialize loop)");
        break;
      case Kind::kForAdvance:
        absl::StrAppend(&result, " (advance loop)");
        break;
    }
    return result;
  } else {
    return "<end>";
  }
}

std::string ControlFlowNode::SuccessorsDebugString(
    absl::string_view indent) const {
  std::vector<std::string> lines;
  for (ControlFlowEdge::Kind kind :
       {ControlFlowEdge::Kind::kNormal, ControlFlowEdge::Kind::kTrueCondition,
        ControlFlowEdge::Kind::kFalseCondition,
        ControlFlowEdge::Kind::kException}) {
    auto it = successors_.find(kind);
    if (it != successors_.end()) {
      lines.push_back(absl::StrCat(indent, "(", ControlFlowEdgeKindString(kind),
                                   ") => ",
                                   it->second->successor()->DebugString()));
      AddSideEffectsToDebugString(*it->second, &lines.back());
    }
  }
  return absl::StrJoin(lines, "\n");
}

std::vector<const ControlFlowNode*> ControlFlowGraph::GetAllNodes() const {
  std::vector<const ControlFlowNode*> nodes;
  nodes.reserve(node_map_.size() + 1);
  for (const auto& entry : node_map_) {
    nodes.push_back(entry.second.get());
  }
  if (end_node() != nullptr) {
    nodes.push_back(end_node());
  }

  // Sort nodes so they are presented in the order that they appear in the
  // script, rather than the order returned by the hash map iterator.
  std::sort(nodes.begin(), nodes.end(),
            CompareControlFlowNodesByScriptLocation);

  return nodes;
}

std::string ControlFlowGraph::DebugString() const {
  ABSL_CHECK(start_node_ != nullptr);
  std::string debug_string;
  absl::StrAppend(&debug_string, "start: ", start_node_->DebugString(),
                  "\nedges:");

  std::vector<const ControlFlowNode*> nodes = GetAllNodes();

  for (const ControlFlowNode* node : nodes) {
    if (node != end_node_.get()) {
      absl::StrAppend(&debug_string, "\n  ", node->DebugString(), "\n",
                      node->SuccessorsDebugString("    "));
    }
  }
  return debug_string;
}

const ControlFlowNode* ControlFlowGraph::GetControlFlowNode(
    const ASTNode* ast_node, ControlFlowNode::Kind kind) const {
  auto it = node_map_.find(std::make_pair(ast_node, kind));
  if (it != node_map_.end()) {
    return it->second.get();
  }
  return nullptr;
}

}  // namespace googlesql
