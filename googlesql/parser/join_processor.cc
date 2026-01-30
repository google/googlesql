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

#include "googlesql/parser/join_processor.h"

#include <memory>
#include <stack>
#include <string>

#include "googlesql/parser/ast_node_factory.h"
#include "googlesql/parser/ast_node_kind.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/public/parse_location.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace googlesql {
namespace parser {

// Returns the number of unmatched joins in the 'node'. If the node does not
// represent a join expression, returns 0.
static int GetUnmatchedJoinCount(const ASTNode* node) {
  if (node->node_kind() == AST_JOIN) {
    auto join = node->GetAsOrDie<ASTJoin>();
    return join->unmatched_join_count();
  }

  return 0;
}

// Get the hint of the join.
static ASTNode* GetJoinHint(ASTJoin* join) {
  for (int i = 1; i < join->num_children(); ++i) {
    if (join->mutable_child(i)->GetAsOrNull<ASTHint>() != nullptr) {
      return join->mutable_child(i);
    }
  }

  return nullptr;
}

// Returns true if the transformation algorithm need to be performed for the
// 'node'.
static bool IsTransformationNeeded(const ASTNode* node) {
  if (node->node_kind() == AST_JOIN) {
    const ASTJoin* join = node->GetAsOrDie<ASTJoin>();
    return join->transformation_needed();
  }

  return false;
}

// Returns true if the join is a qualified join.
static bool IsQualifiedJoin(const ASTJoin* join) {
  if (join->join_type() == googlesql::ASTJoin::CROSS ||
      join->join_type() == googlesql::ASTJoin::COMMA ||
      join->natural()) {
    return false;
  }

  return true;
}

// Returns the join node if 'n' is a cross, comma, or natural join. Otherwise,
// returns nullptr.
static ASTJoin* GetCrossCommaOrNaturalJoin(ASTNode* n) {
  if (n->node_kind() != AST_JOIN) {
    return nullptr;
  }

  ASTJoin* join = n->GetAsOrDie<ASTJoin>();
  if (IsQualifiedJoin(join)) {
    return nullptr;
  }

  return join;
}

static std::stack<ASTNode*> FlattenJoinExpression(ASTNode* node) {
  std::stack<ASTNode*> q;
  while (node != nullptr) {
    if (node->node_kind() == AST_JOIN) {
      ASTJoin* join = node->GetAsOrDie<ASTJoin>();

      // Get the fields of the join. join->lhs, join->rhs() etc cannot be used
      // since InitFields() has not been called yet.
      ASTNode* lhs = nullptr;
      ASTNode* rhs = nullptr;
      ASTNode* on_clause = nullptr;
      ASTNode* using_clause = nullptr;
      ASTOnOrUsingClauseList* clause_list = nullptr;
      // We set this field on `join` so we can pull this value back out of
      // the join in later in processing - this is significantly less
      // error prone than fiddling with the stack.

      ASTLocation* join_location = nullptr;
      lhs = join->mutable_child(0);
      for (int i = 1; i < join->num_children(); ++i) {
        ASTNode* child = join->mutable_child(i);
        if (child->GetAsOrNull<ASTTableExpression>() != nullptr) {
          rhs = child;
        } else if (child->node_kind() == AST_ON_CLAUSE) {
          on_clause = child;
        } else if (child->node_kind() == AST_USING_CLAUSE) {
          using_clause = child;
        } else if (child->node_kind() == AST_ON_OR_USING_CLAUSE_LIST) {
          clause_list = child->GetAsOrDie<ASTOnOrUsingClauseList>();
        } else if (child->node_kind() == AST_LOCATION) {
          join_location = child->GetAsOrDie<ASTLocation>();
          join->set_join_location(join_location);
        } else if (child->node_kind() == AST_HINT) {
          // Ignore
        } else {
          ABSL_LOG(ERROR) << "Unexpected node kind encountered: "
                      << child->node_kind();
        }
      }

      // Add nodes to the stack.
      if (clause_list != nullptr) {
        // Add the items in the clause list to the stack
        // if the join contains the clause list.
        for (int i = clause_list->num_children() - 1; i >= 0; --i) {
          q.push(clause_list->mutable_child(i));
        }
      } else if (on_clause != nullptr) {
        q.push(on_clause);
      } else if (using_clause != nullptr) {
        q.push(using_clause);
      }

      q.push(rhs);
      q.push(join);
      node = lhs;
    } else {
      q.push(node);
      break;
    }
  }

  return q;
}

ASTNode* MakeInternalError(ErrorInfo* error_info,
                           const ParseLocationRange& error_location,
                           absl::string_view error_message) {
  error_info->location = error_location;
  error_info->message = absl::StrCat("Internal error: ", error_message);
  return nullptr;
}

ASTNode* MakeSyntaxError(ErrorInfo* error_info,
                         const ParseLocationRange& error_location,
                         absl::string_view error_message) {
  error_info->location = error_location;
  error_info->message = absl::StrCat("Syntax error: ", error_message);
  return nullptr;
}

// This class is used to generate the error message when there are more joins
// than join conditions. When this happens, the error message will contain the
// counts of joins and join conditions, e.g.:
//
//   The number of join conditions is 2, but the number of joins that require a
//   join condition is 3.
//
//   select * from a join b join c join d on cond1 on cond2;
//                   ^
// Note that the numbers are calculated for a join block, not for the whole
// expression. For example, for this query:
//
//  select * from a join b on cond1 join c join d on cond2;
//
// The error generated is:
//
//   The number of join conditions is 1, but the number of joins that require a
//   join condition is 2.
//
//   select * from a join b on cond1 join c join d on cond2;
//                                   ^
// instead of this one if the joins and join conditions in the whole expression
// are counted:
//
//   The number of join conditions is 2, but the number of joins that require a
//   join condition is 3.
//
// The error location is the location of the last join node that does not have a
// matching join condition. For example, for this query:
//
//  select * from a join b join c join d join e on cond1 on cond2;
//
// the matching between joins and join conditions is illustrated as:
//
//  select * from a join b join c join d join e on cond1 on cond2;
//                                 ^      ^     ^        ^
//                                 |      |     |        |
//                                 |      -------        |
//                                 -----------------------
//
// The last join that is not matched is the join "b join c", and that is the
// error location. At this point in the stack, joins that are matched with join
// conditions are already replaced by newly created joins, so to locate the
// error join node, we just need to pop items from the stack until we encounter
// the first qualified join that is not newly created. To check if a join node
// is a newly created node, we use the hash set created_joins_.
class JoinErrorTracker {
 public:
  JoinErrorTracker() { join_condition_count_ = join_count_ = 0; }

  void increment_join_count() { ++join_count_; }

  int join_count() const { return join_count_; }

  int join_condition_count() const { return join_condition_count_; }

  void increment_join_condition_count() {
    ++join_condition_count_;
    if (join_condition_count_ == join_count_) {
      // A new join block starts, so the counts are reset.
      join_count_ = join_condition_count_ = 0;
    }
  }

  // To locate the qualified join node where the error occurs, we will search in
  // the stack for the qualified join nodes that are in the original
  // flattend_join_expression, skipping qualified join nodes that are created in
  // ProcessFlattenedJoinExpression(). Since there is no way to distinguish the
  // original qualified join nodes from newly created qualified join nodes by
  // themselves, we use a hash set created_joins_ to keep track of the newly
  // created qualified join nodes.
  void add_created_join(const ASTJoin* join) { created_joins_.insert(join); }

  bool is_created_join(const ASTJoin* join) const {
    return created_joins_.contains(join);
  }

 private:
  int join_count_;
  int join_condition_count_;
  absl::flat_hash_set<const ASTJoin*> created_joins_;
};

static ASTNode* GenerateError(const JoinErrorTracker& error_tracker,
                              std::stack<ASTNode*>* stack,
                              ErrorInfo* error_info) {
  ParseLocationRange location = stack->top()->location();

  // We report the error at the first qualified join of the join block, which is
  // the bottomest qualified, not created, join in the stack. Search for this
  // join node in the stack here.
  ASTJoin* join = nullptr;
  while (!stack->empty()) {
    ASTJoin* item = stack->top()->GetAsOrNull<ASTJoin>();
    if (item != nullptr && !error_tracker.is_created_join(item) &&
        IsQualifiedJoin(item)) {
      join = item;
    }

    stack->pop();
  }

  if (join == nullptr) {
    return MakeInternalError(error_info, location,
                             "Failed to find the qualified join");
  }

  auto join_type_name = join->join_type() == ASTJoin::DEFAULT_JOIN_TYPE
                            ? "INNER"
                            : join->GetSQLForJoinType();
  return MakeSyntaxError(
      error_info, join->join_location()->location(),
      absl::StrCat("The number of join conditions is ",
                   error_tracker.join_condition_count(),
                   " but the number of joins that require a join condition is ",
                   error_tracker.join_count(), ". ", join_type_name, " JOIN",
                   " must have an ON or USING clause"));
}

ASTNode* ProcessFlattenedJoinExpression(
    ASTNodeFactory& node_factory,
    std::stack<ASTNode*>* flattened_join_expression, ErrorInfo* error_info) {
  ParseLocationRange location = flattened_join_expression->top()->location();

  std::stack<ASTNode*> stack;
  JoinErrorTracker error_tracker;

  while (!flattened_join_expression->empty()) {
    ASTNode* item = flattened_join_expression->top();
    flattened_join_expression->pop();
    ASTJoin* join = GetCrossCommaOrNaturalJoin(item);
    if (join != nullptr) {
      // If it's CROSS, COMMA, or NATURAL JOIN, create a new CROSS, COMMA,
      // NATURAL JOIN.
      if (stack.empty()) {
        return MakeInternalError(error_info, item->location(),
                                 "Stack should not be empty");
      }

      ASTNode* lhs = stack.top();
      stack.pop();
      ASTNode* rhs = flattened_join_expression->top();
      flattened_join_expression->pop();

      ASTLocation* join_location = node_factory.CreateASTNode<ASTLocation>(
          join->join_location()->location());

      ASTJoin* new_join = node_factory.CreateASTNode<ASTJoin>(
          ParseLocationRange(), {lhs, GetJoinHint(join), join_location, rhs});
      new_join->set_join_type(join->join_type());
      new_join->set_join_hint(join->join_hint());
      new_join->set_natural(join->natural());

      new_join->set_start_location(join->location().start());
      new_join->set_end_location(rhs->location().end());

      stack.push(new_join);
    } else if (item->node_kind() == AST_ON_CLAUSE ||
               item->node_kind() == AST_USING_CLAUSE) {
      // Create JOIN with on/using clause.
      if (stack.size() < 3) {
        return MakeInternalError(
            error_info, item->location(),
            "Stack should contain at least 3 items at this point");
      }
      error_tracker.increment_join_condition_count();

      ASTNode* rhs = stack.top();
      stack.pop();
      ASTJoin* join = stack.top()->GetAsOrDie<ASTJoin>();
      stack.pop();
      ASTNode* lhs = stack.top();
      stack.pop();

      ASTLocation* join_location = node_factory.CreateASTNode<ASTLocation>(
          join->join_location()->location());

      ASTJoin* new_join = node_factory.CreateASTNode<ASTJoin>(
          location, {lhs, GetJoinHint(join), join_location, rhs, item});
      new_join->set_join_type(join->join_type());
      new_join->set_join_hint(join->join_hint());
      new_join->set_natural(join->natural());

      new_join->set_start_location(join->location().start());
      new_join->set_end_location(item->location().end());

      error_tracker.add_created_join(new_join);
      stack.push(new_join);
    } else {
      if (item->node_kind() == AST_JOIN) {
        error_tracker.increment_join_count();
      }

      stack.push(item);
    }
  }

  // All items in the flattened_join_expression are processed.
  if (stack.empty()) {
    return MakeInternalError(error_info, location, "Stack should not be empty");
  }

  if (stack.size() == 1) {
    return stack.top()->GetAsOrDie<ASTJoin>();
  } else {
    // When we reach here, it means there are more JOINs than ON/USING clauses.
    // E.g.
    //   SELECT * FROM t1 JOIN t2 JOIN t3 JOIN t4 ON cond1 ON cond2;
    //
    // Generate error in this case.
    return GenerateError(error_tracker, &stack, error_info);
  }
}

static bool ContainsCommaJoin(const ASTNode* node) {
  const ASTJoin* join = nullptr;
  if (node->node_kind() == AST_JOIN) {
    join = node->GetAsOrDie<ASTJoin>();
    return join->contains_comma_join();
  }

  return false;
}

static const ASTJoin::ParseError* GetParseError(const ASTNode* node) {
  if (node->node_kind() == AST_JOIN) {
    const ASTJoin* join = node->GetAsOrDie<ASTJoin>();
    return join->parse_error();
  }

  return nullptr;
}

ASTNode* JoinRuleAction(const ParseLocationRange& start_location,
                        const ParseLocationRange& end_location, ASTNode* lhs,
                        bool natural, ASTJoin::JoinType join_type,
                        ASTJoin::JoinHint join_hint, ASTNode* hint,
                        ASTNode* table_primary,
                        ASTNode* on_or_using_clause_list,
                        ASTLocation* join_location,
                        ASTNodeFactory& node_factory, ErrorInfo* error_info) {
  auto clause_list =
      on_or_using_clause_list == nullptr
          ? nullptr
          : on_or_using_clause_list->GetAsOrDie<ASTOnOrUsingClauseList>();
  int clause_count = clause_list == nullptr ? 0 : clause_list->num_children();

  int unmatched_join_count = GetUnmatchedJoinCount(lhs);

  // Increment unmatched_join_count if the current join is a qualified join,
  // i.e. not CROSS JOIN, nor NATURAL JOIN.
  if (join_type != ASTJoin::CROSS && natural == false) {
    unmatched_join_count++;
  }

  if (clause_count >= 2) {
    // Our decision is not to support mixing consecutive ON/USING clauses with
    // COMMA JOINs. Generate error if they are used together.
    if (ContainsCommaJoin(lhs)) {
      auto* error_node = clause_list->child(1);
      return MakeSyntaxError(
          error_info, error_node->location(),
          absl::StrCat(
              "Unexpected keyword ",
              (error_node->node_kind() == AST_ON_CLAUSE ? "ON" : "USING")));
    }
  }

  // Create the JOIN node
  ASTJoin* join;
  if (clause_count == 0 || clause_count == 1) {
    ASTNode* on_or_using_clause = nullptr;
    if (clause_count == 1) {
      on_or_using_clause = clause_list->mutable_child(0);
    }
    join = node_factory.CreateASTNode<ASTJoin>(
        {start_location.start(), end_location.end()},
        {lhs, hint, join_location, table_primary, on_or_using_clause});
    join->set_transformation_needed(IsTransformationNeeded(lhs));
  } else {
    join = node_factory.CreateASTNode<ASTJoin>(
        {start_location.start(), end_location.end()},
        {lhs, hint, join_location, table_primary, clause_list});
    join->set_transformation_needed(true);
  }

  join->set_natural(natural);
  join->set_join_type(join_type);
  join->set_join_hint(join_hint);
  join->set_unmatched_join_count(unmatched_join_count - clause_count);
  join->set_contains_comma_join(ContainsCommaJoin(lhs));

  // Detects and processes the error when there are more ON/USING clauses than
  // JOINs.
  const ASTJoin::ParseError* parse_error = GetParseError(lhs);
  if (parse_error != nullptr || clause_count > unmatched_join_count) {
    auto* error_node = parse_error != nullptr ?
                       parse_error->error_node :
                       clause_list->child(unmatched_join_count);
    std::string message =
        parse_error != nullptr ?
        parse_error->message :
        absl::StrCat(
            "The number of join conditions is ", clause_count,
            " but the number of joins that require a join condition is "
            "only ",
            unmatched_join_count, ". Unexpected keyword ",
            (error_node->node_kind() == AST_ON_CLAUSE ? "ON" : "USING"));

    if (clause_count >= 2) {
      // Consecutive ON/USING clauses are used. Returns the error in this case.
      return MakeSyntaxError(error_info, error_node->location(), message);
    } else {
      // Does not throw the error to maintain the backward compatibility. Saves
      // the error instead.
      join->set_parse_error(std::make_unique<ASTJoin::ParseError>(
          ASTJoin::ParseError{error_node, message}));
    }
  }

  return join;
}

ASTNode* CommaJoinRuleAction(const ParseLocationRange& start_location,
                             const ParseLocationRange& end_location,
                             ASTNode* lhs, ASTNode* table_primary,
                             ASTLocation* comma_location,
                             ASTNodeFactory& node_factory,
                             ErrorInfo* error_info) {
  if (IsTransformationNeeded(lhs)) {
    return MakeSyntaxError(
        error_info, start_location,
        "Comma join is not allowed after consecutive ON/USING clauses");
  }

  auto* comma_join = node_factory.CreateASTNode<ASTJoin>(
      {start_location.start(), end_location.end()},
      {lhs, comma_location, table_primary});
  comma_join->set_join_type(ASTJoin::COMMA);
  comma_join->set_contains_comma_join(true);
  return comma_join;
}

ASTNode* TransformJoinExpression(ASTNode* node, ASTNodeFactory& node_factory,
                                 ErrorInfo* error_info) {
  if (!IsTransformationNeeded(node)) return node;

  std::stack<ASTNode*> flattened_join_expression = FlattenJoinExpression(node);
  return ProcessFlattenedJoinExpression(node_factory,
                                        &flattened_join_expression, error_info);
}

}  // namespace parser
}  // namespace googlesql
