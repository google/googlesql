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

#include "googlesql/parser/parse_tree.h"

#include <algorithm>
#include <any>
#include <cstddef>
#include <functional>
#include <optional>
#include <queue>
#include <set>
#include <string>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/common/thread_stack.h"
#include "googlesql/common/utf_util.h"
#include "googlesql/parser/ast_node_kind.h"
#include "googlesql/parser/parse_tree_errors.h"
#include "googlesql/parser/parse_tree_visitor.h"
// This is not a header -- it is a generated part of this source file.
#include "googlesql/parser/parse_tree_accept_methods.inc"  
#include "googlesql/parser/visit_result.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/public/strings.h"
#include "absl/base/attributes.h"
#include "absl/flags/flag.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

ABSL_FLAG(bool, output_asc_explicitly, false,
          "If true, outputs the asc explicitly in "
          "ASTOrderingExpression::SingleNodeDebugString and "
          "Unparser::visitASTOrderingExpression.");

namespace googlesql {

ASTNode::~ASTNode() = default;

void ASTNode::AddChild(ASTNode* child) {
  ABSL_DCHECK(child != nullptr);
  children_.push_back(child);
  child->set_parent(this);
}

void ASTNode::AddChildFront(ASTNode* child) {
  ABSL_DCHECK(child != nullptr);
  children_.insert(children_.begin(), child);
  child->set_parent(this);
}

absl::Status ASTNode::TraverseNonRecursiveHelper(
    const VisitResult& result, NonRecursiveParseTreeVisitor* visitor,
    std::vector<std::function<absl::Status()>>* stack) {
  // Push actions in the reverse order that they will execute in.
  if (result.continuation() != nullptr) {
    stack->push_back(result.continuation());
  }
  if (result.node_for_child_visit() != nullptr) {
    const ASTNode* node = result.node_for_child_visit();
    for (int i = node->num_children() - 1; i >= 0; --i) {
      const ASTNode* child = node->child(i);
      stack->push_back([visitor, child, stack]() -> absl::Status {
        GOOGLESQL_ASSIGN_OR_RETURN(VisitResult child_result, child->Accept(visitor));
        return TraverseNonRecursiveHelper(child_result, visitor, stack);
      });
    }
  }
  if (result.should_terminate()) {
    stack->clear();
  }

  return absl::OkStatus();
}

absl::Status ASTNode::TraverseNonRecursive(
    NonRecursiveParseTreeVisitor* visitor) const {
  std::vector<std::function<absl::Status()>> stack;
  stack.push_back([this, &stack, visitor]() -> absl::Status {
    GOOGLESQL_ASSIGN_OR_RETURN(VisitResult root_result, Accept(visitor));
    return TraverseNonRecursiveHelper(root_result, visitor, &stack);
  });
  while (!stack.empty()) {
    std::function<absl::Status()> task = stack.back();
    stack.pop_back();
    GOOGLESQL_RETURN_IF_ERROR(task());
  }
  return absl::OkStatus();
}

void ASTNode::Accept(ParseTreeVisitor* visitor, void* data) const {
  visitor->visit(this, data);
}

absl::Status ASTNode::Accept(ParseTreeStatusVisitor& visitor,
                             std::any& output) const {
  if (!ThreadHasEnoughStack()) {
    return absl::ResourceExhaustedError(
        "Parse tree is too deep to visit in available stack space.");
  }
  return visitor.Visit(this, output);
}

void ASTNode::ChildrenAccept(ParseTreeVisitor* visitor, void* data) const {
  for (int i = 0; i < children_.size(); ++i) {
    children_[i]->Accept(visitor, data);
  }
}

absl::Status ASTNode::ChildrenAccept(ParseTreeStatusVisitor& visitor,
                                     std::any& output) const {
  for (int i = 0; i < children_.size(); ++i) {
    GOOGLESQL_RETURN_IF_ERROR(children_[i]->Accept(visitor, output));
  }
  return absl::OkStatus();
}

std::string ASTNode::GetNodeKindString() const {
  return NodeKindToString(node_kind());
}

std::string ASTNode::SingleNodeDebugString() const {
  return NodeKindToString(node_kind());
}

// This function is not inlined, to minimize the stack usage of Dump().
ABSL_ATTRIBUTE_NOINLINE bool ASTNode::Dumper::DumpNode() {
  out_->append(current_depth_ * 2, ' ');
  const ParseLocationRange& range = node_->location();
  absl::StrAppend(out_, node_->SingleNodeDebugString(), " [", range.GetString(),
                  "]");
  /*
  // This can be uncommented to add "parenthesized" markers into the ASTNode
  // debug strings to make debugging parenthesization issues easier.
  if ((node_->IsExpression() &&
       node_->GetAsOrDie<ASTExpression>()->parenthesized()) ||
      (node_->IsQueryExpression() &&
       node_->GetAsOrDie<ASTQueryExpression>()->parenthesized())) {
    absl::StrAppend(out_, " parenthesized");
  }
  */

  // Show the actual text indicated by the position range, but only if the
  // position range falls entirely within the bounds of the input string and
  // the end position appears at or after the start position.
  if (sql_.has_value() && range.start().GetByteOffset() >= 0 &&
      range.end().GetByteOffset() >= range.start().GetByteOffset() &&
      range.end().GetByteOffset() <= sql_->size()) {
    absl::string_view node_substr = sql_->substr(
        range.start().GetByteOffset(),
        range.end().GetByteOffset() - range.start().GetByteOffset());
    absl::StatusOr<std::string> status_or_summary_str =
        GetSummaryString(node_substr, 30);
    if (status_or_summary_str.ok()) {
      absl::StrAppend(out_, " [", status_or_summary_str.value(), "]");
    }
  }
  absl::StrAppend(out_, separator_);
  if (current_depth_ >= max_depth_) {
    out_->append(current_depth_ * 2, ' ');
    absl::StrAppend(out_, "  Subtree skipped (reached max depth ", max_depth_,
                    ")", separator_);
    return false;
  }
  return true;
}

// This function is recursive. To minimize the risk of stack overflow, it
// has minimal arguments and local variables, and it does not call any inline
// function with large local variables. In particular, it does not call
// StrCat or StrAppend or any inline function that calls these 2 functions,
// which takes 48 bytes (= sizeof(AlphaNum)) per argument (the compiler does
// not let the temporary objects share memory). The current implementation
// has 48 bytes frame size.
void ASTNode::Dumper::Dump() {
  if (!DumpNode()) {
    return;
  }
  ++current_depth_;
  const auto& children = node_->children_;
  for (ASTNode* n : children) {
    if (n != nullptr) {
      node_ = n;
      Dump();
    }
  }
  --current_depth_;
}

std::string ASTNode::DebugString(int max_depth) const {
  std::string out;
  Dumper(this, "\n", max_depth, std::nullopt, &out).Dump();
  return out;
}

std::string ASTNode::DebugString(absl::string_view sql, int max_depth) const {
  std::string out;
  Dumper(this, "\n", max_depth, sql, &out).Dump();
  return out;
}

std::string ASTNode::GetLocationString() const {
  return parse_location_range_.GetString();
}

// NOTE: An equivalent method on ResolvedNodes exists in
// ../resolved_ast/resolved_node.cc.
void ASTNode::GetDescendantsWithKindsImpl(
    const std::set<int>& node_kinds,
    std::vector<const ASTNode*>* found_nodes,
    bool continue_traversal) const {
  found_nodes->clear();

  // Use non-recursive traversal to avoid stack issues.
  std::queue<const ASTNode*> node_queue;
  node_queue.push(this);

  while (!node_queue.empty()) {
    const ASTNode* node = node_queue.front();
    node_queue.pop();

    if (googlesql_base::ContainsKey(node_kinds, node->node_kind())) {
      // Emit this node.
      found_nodes->push_back(node);

      if (!continue_traversal) {
        continue;
      }
    }
    // Queue its children for traversal.
    for (int i = 0; i < node->num_children(); ++i) {
      node_queue.push(node->child(i));
    }
  }
}

std::string ASTSetOperation::SingleNodeDebugString() const {
  const ASTSetOperationMetadata& effective_metadata =
      *metadata_->set_operation_metadata_list(0);
  return absl::StrCat(ASTNode::SingleNodeDebugString(), "(",
                      effective_metadata.GetSQLForOperation(), ")");
}

std::string ASTQuery::SingleNodeDebugString() const {
  std::string result = ASTNode::SingleNodeDebugString();
  if (is_pivot_input()) {
    absl::StrAppend(&result, " (pivot input)");
  }
  return result;
}

std::string ASTSelect::SingleNodeDebugString() const {
  std::vector<std::string> select_attrs;
  if (distinct()) {
    select_attrs.push_back("distinct=true");
  }
  if (select_attrs.empty()) {
    return ASTNode::SingleNodeDebugString();
  }
  return absl::StrCat(ASTNode::SingleNodeDebugString(), "(",
                      absl::StrJoin(select_attrs, ", "), ")");
}

std::string ASTSelectAs::SingleNodeDebugString() const {
  if (as_mode_ == TYPE_NAME) {
    return ASTNode::SingleNodeDebugString();
  } else {
    return absl::StrCat(ASTNode::SingleNodeDebugString(),
                        "(as_mode=", as_mode() == VALUE ? "VALUE" : "STRUCT",
                        ")");
  }
}

std::string ASTGroupBy::SingleNodeDebugString() const {
  return absl::StrCat(ASTNode::SingleNodeDebugString(),
                      and_order_by() ? "(and_order_by=true)" : "");
}

std::string ASTGroupingItemOrder::SingleNodeDebugString() const {
  // There is no UNSPECIFIED case in this node.
  return absl::StrCat(ASTNode::SingleNodeDebugString(),
                      ordering_spec_ == ASTOrderingExpression::UNSPECIFIED
                          ? "(UNSPECIFIED)"
                          : (descending() ? "(DESC)" : "(ASC)"));
}

std::string ASTAlias::GetAsString() const {
  return identifier()->GetAsString();
}

absl::string_view ASTAlias::GetAsStringView() const {
  return identifier()->GetAsStringView();
}

std::string ASTIntoAlias::GetAsString() const {
  return identifier()->GetAsString();
}

absl::string_view ASTIntoAlias::GetAsStringView() const {
  return identifier()->GetAsStringView();
}

const ASTNode* ASTTableExpression::alias_location() const {
  const ASTAlias* ast_alias = alias();
  if (ast_alias != nullptr) return ast_alias->identifier();
  return this;
}

bool ASTTableElementList::HasConstraints() const {
  for (int i = 0; i < num_children(); ++i) {
    if (dynamic_cast<const ASTTableConstraint*>(child(i))) {
      return true;
    }
  }
  return false;
}

std::string ASTUnpivotClause::GetSQLForNullFilter() const {
  switch (null_filter_) {
    case kUnspecified:
      return "";
    case kInclude:
      return "INCLUDE NULLS";
    case kExclude:
      return "EXCLUDE NULLS";
  }
}

std::string ASTUnpivotClause::SingleNodeDebugString() const {
  std::string nulls_filter = null_filter_ != kUnspecified
                                 ? absl::StrCat("(", GetSQLForNullFilter(), ")")
                                 : "";
  return absl::StrCat(ASTNode::SingleNodeDebugString(), nulls_filter);
}

std::string ASTJoin::SingleNodeDebugString() const {
  std::vector<std::string> join_attrs;
  if (natural()) {
    join_attrs.push_back("NATURAL");
  }
  if (join_type() != DEFAULT_JOIN_TYPE) {
    // Show "Join(COMMA)" rather than "Join(,)" for comma join.
    join_attrs.push_back(join_type() == COMMA ? "COMMA" : GetSQLForJoinType());
  }
  if (join_hint() != NO_JOIN_HINT) {
    join_attrs.push_back(GetSQLForJoinHint());
  }

  if (join_attrs.empty()) {
    return ASTNode::SingleNodeDebugString();
  }

  return absl::StrCat(ASTNode::SingleNodeDebugString(), "(",
                      absl::StrJoin(join_attrs, ", "), ")");
}

std::string ASTJoin::GetSQLForJoinType() const {
  switch (join_type_) {
    case DEFAULT_JOIN_TYPE:
      return "";
    case COMMA:
      return ",";
    case CROSS:
      return "CROSS";
    case FULL:
      return "FULL";
    case INNER:
      return "INNER";
    case LEFT:
      return "LEFT";
    case RIGHT:
      return "RIGHT";
  }
}

std::string ASTJoin::GetSQLForJoinHint() const {
  switch (join_hint_) {
    case NO_JOIN_HINT:
      return "";
    case HASH:
      return "HASH";
    case LOOKUP:
      return "LOOKUP";
  }
}

std::string ASTNullOrder::SingleNodeDebugString() const {
  return absl::StrCat(ASTNode::SingleNodeDebugString(),
                      nulls_first() ? "(NULLS FIRST)" : "(NULLS LAST)");
}

std::string ASTOrderingExpression::SingleNodeDebugString() const {
  return absl::StrCat(ASTNode::SingleNodeDebugString(),
                      descending()
                          ? "(DESC)"
                          : (ordering_spec() == UNSPECIFIED ||
                                     !absl::GetFlag(FLAGS_output_asc_explicitly)
                                 ? "(ASC)"
                                 : "(ASC EXPLICITLY)"));
}

std::string ASTPrimaryKeyElement::SingleNodeDebugString() const {
  std::string ordering;
  if (descending()) {
    ordering = "(DESC)";
  } else if (ascending()) {
    ordering = "(ASC EXPLICITLY)";
  } else {
    ordering = "(ASC)";
  }
  return absl::StrCat(ASTNode::SingleNodeDebugString(), ordering);
}

absl::Status
ASTGeneralizedPathExpression::VerifyIsPureGeneralizedPathExpression(
    const ASTExpression* path, std::string* node_kind_out) {
  while (true) {
    switch (path->node_kind()) {
      case AST_PATH_EXPRESSION:
        return absl::OkStatus();
      case AST_DOT_GENERALIZED_FIELD:
        path = path->GetAs<ASTDotGeneralizedField>()->expr();
        break;
      case AST_DOT_IDENTIFIER:
        path = path->GetAs<ASTDotIdentifier>()->expr();
        break;
      case AST_ARRAY_ELEMENT:
        path = path->GetAs<ASTArrayElement>()->array();
        break;
      default:
        // This returns the rightmost error.
        if (node_kind_out != nullptr) {
          *node_kind_out = path->GetNodeKindString();
        }
        return MakeSqlErrorAt(path) << "Expected pure generalized path "
                                    << "expression, but found node kind "
                                    << path->GetNodeKindString();
    }
  }
}

std::string ASTBinaryExpression::GetSQLForOperator() const {
  switch (op_) {
    case NOT_SET:
      return "<UNKNOWN OPERATOR>";
    case LIKE:
      return is_not_ ? "NOT LIKE" : "LIKE";
    case IS:
      return is_not_ ? "IS NOT" : "IS";
    case EQ:
      return "=";
    case NE:
      return "!=";
    case NE2:
      return "<>";
    case GT:
      return ">";
    case LT:
      return "<";
    case GE:
      return ">=";
    case LE:
      return "<=";
    case BITWISE_OR:
      return "|";
    case BITWISE_XOR:
      return "^";
    case BITWISE_AND:
      return "&";
    case PLUS:
      return "+";
    case MINUS:
      return "-";
    case MULTIPLY:
      return "*";
    case DIVIDE:
      return "/";
    case CONCAT_OP:
      return "||";
    case DISTINCT:
      return is_not_ ? "IS NOT DISTINCT FROM" : "IS DISTINCT FROM";
    case IS_SOURCE_NODE:
      return is_not_ ? "IS NOT SOURCE OF" : "IS SOURCE OF";
    case IS_DEST_NODE:
      return is_not_ ? "IS NOT DESTINATION OF" : "IS DESTINATION OF";
  }
}

bool ASTBinaryExpression::IsAllowedInComparison() const {
  if (parenthesized()) return true;
  switch (op()) {
    case LIKE:
    case IS:
    case EQ:
    case NE:
    case NE2:
    case GT:
    case LT:
    case GE:
    case LE:
      return false;
    default:
      return true;
  }
}

std::string ASTBinaryExpression::SingleNodeDebugString() const {
  return absl::StrCat(ASTNode::SingleNodeDebugString(), "(",
                      GetSQLForOperator(), ")");
}

std::string ASTBitwiseShiftExpression::SingleNodeDebugString() const {
  return absl::StrCat(ASTNode::SingleNodeDebugString(), "(",
                      is_left_shift() ? "<<" : ">>", ")");
}

std::string ASTInExpression::SingleNodeDebugString() const {
  return absl::StrCat(ASTNode::SingleNodeDebugString(), "(",
                      is_not_ ? "NOT " : "", "IN", ")");
}

std::string ASTLikeExpression::SingleNodeDebugString() const {
  return absl::StrCat(ASTNode::SingleNodeDebugString(), "(",
                      is_not_ ? "NOT " : "", "LIKE", ")");
}

std::string ASTAnySomeAllOp::SingleNodeDebugString() const {
  return absl::StrCat(ASTNode::SingleNodeDebugString(), "(",
                      GetSQLForOperator(), ")");
}

std::string ASTAnySomeAllOp::GetSQLForOperator() const {
  switch (op_) {
    case kAny:
      return "ANY";
    case kSome:
      return "SOME";
    case kAll:
      return "ALL";
    case kUninitialized:
      return "<UNINITIALIZED ANY/SOME/ALL OPERATOR>";
  }
}

std::string ASTQuantifiedComparisonExpression::GetSQLForOperator() const {
  switch (op_) {
    case ASTBinaryExpression::EQ:
      return "=";
    case ASTBinaryExpression::NE:
      return "!=";
    case ASTBinaryExpression::NE2:
      return "<>";
    case ASTBinaryExpression::GT:
      return ">";
    case ASTBinaryExpression::LT:
      return "<";
    case ASTBinaryExpression::GE:
      return ">=";
    case ASTBinaryExpression::LE:
      return "<=";
    default:
      return "<UNKNOWN OPERATOR>";
  }
}

std::string ASTQuantifiedComparisonExpression::SingleNodeDebugString() const {
  return absl::StrCat(ASTNode::SingleNodeDebugString(), "(",
                      GetSQLForOperator(), ")");
}

std::string ASTBetweenExpression::SingleNodeDebugString() const {
  return absl::StrCat(ASTNode::SingleNodeDebugString(), "(",
                      is_not_ ? "NOT " : "", "BETWEEN)");
}

std::string ASTUnaryExpression::SingleNodeDebugString() const {
  // Include the image and location information
  return absl::StrCat(ASTNode::SingleNodeDebugString(), "(",
                      GetSQLForOperator(), ")");
}

std::string ASTUnaryExpression::GetSQLForOperator() const {
  switch (op_) {
    case NOT_SET:
      return "<UNKNOWN OPERATOR>";
    case NOT:
      return "NOT";
    case BITWISE_NOT:
      return "~";
    case MINUS:
      return "-";
    case PLUS:
      return "+";
    case IS_UNKNOWN:
      return "IS UNKNOWN";
    case IS_NOT_UNKNOWN:
      return "IS NOT UNKNOWN";
  }
}

std::string ASTCastExpression::SingleNodeDebugString() const {
  return absl::StrCat(ASTNode::SingleNodeDebugString(),
                      is_safe_cast_ ? "(return_null_on_error=true)" : "");
}

std::string ASTDropStatement::SingleNodeDebugString() const {
  std::string out = absl::StrCat(ASTNode::SingleNodeDebugString(), " ",
                                 SchemaObjectKindToName(schema_object_kind()));
  std::vector<std::string> params;
  if (is_if_exists()) {
    params.push_back("is_if_exists");
  }
  if (drop_mode() != ASTDropStatement::DropMode::DROP_MODE_UNSPECIFIED) {
    params.push_back(
        absl::StrCat("drop_mode=", GetSQLForDropMode(drop_mode())));
  }
  if (!params.empty()) {
    absl::StrAppend(&out, "(", absl::StrJoin(params, ", "), ")");
  }
  return out;
}

// static
std::string ASTDropStatement::GetSQLForDropMode(DropMode drop_mode) {
  switch (drop_mode) {
    case DropMode::DROP_MODE_UNSPECIFIED:
      return "";
    case DropMode::RESTRICT:
      return "RESTRICT";
    case DropMode::CASCADE:
      return "CASCADE";
  }
}

std::string ASTDropEntityStatement::SingleNodeDebugString() const {
  const std::string out = ASTNode::SingleNodeDebugString();
  if (!is_if_exists()) {
    return out;
  }
  return absl::StrCat(out, "(is_if_exists)");
}

std::string ASTDropFunctionStatement::SingleNodeDebugString() const {
  const std::string node_name = ASTNode::SingleNodeDebugString();
  return (!is_if_exists()) ? node_name
                           : absl::StrCat(node_name, "(is_if_exists)");
}

std::string ASTDropTableFunctionStatement::SingleNodeDebugString() const {
  const std::string node_name = ASTNode::SingleNodeDebugString();
  return (!is_if_exists()) ? node_name
                           : absl::StrCat(node_name, "(is_if_exists)");
}

std::string ASTDropRowAccessPolicyStatement::SingleNodeDebugString() const {
  const std::string node_name = ASTNode::SingleNodeDebugString();
  if (!is_if_exists()) {
    return node_name;
  }
  return absl::StrCat(node_name, "(is_if_exists)");
}

std::string ASTDropSearchIndexStatement::SingleNodeDebugString() const {
  const std::string node_name = ASTNode::SingleNodeDebugString();
  if (!is_if_exists()) {
    return node_name;
  }
  return absl::StrCat(node_name, "(is_if_exists)");
}

std::string ASTDropMaterializedViewStatement::SingleNodeDebugString() const {
  const std::string node_name = ASTNode::SingleNodeDebugString();
  if (!is_if_exists()) {
    return node_name;
  }
  return absl::StrCat(node_name, "(is_if_exists)");
}

std::string ASTDropSnapshotTableStatement::SingleNodeDebugString() const {
  const std::string node_name = ASTNode::SingleNodeDebugString();
  if (!is_if_exists()) {
    return node_name;
  }
  return absl::StrCat(node_name, "(is_if_exists)");
}

std::string ASTDropVectorIndexStatement::SingleNodeDebugString() const {
  const std::string node_name = ASTNode::SingleNodeDebugString();
  if (!is_if_exists()) {
    return node_name;
  }
  return absl::StrCat(node_name, "(is_if_exists)");
}

std::string ASTExportMetadataStatement::SingleNodeDebugString() const {
  return absl::StrCat(ASTNode::SingleNodeDebugString(), " ",
                      SchemaObjectKindToName(schema_object_kind()));
}

std::string ASTPathExpression::ToIdentifierPathString(
    size_t max_prefix_size) const {
  const int end = max_prefix_size == 0
                      ? names_.size()
                      : std::min(names_.size(), max_prefix_size);
  std::string ret;
  for (int i = 0; i < end; ++i) {
    if (i != 0) ret += ".";
    ret += ToIdentifierLiteral(names_[i]->GetAsStringView());
  }
  return ret;
}

std::vector<std::string> ASTPathExpression::ToIdentifierVector() const {
  std::vector<std::string> ret;
  ret.reserve(names_.size());
  for (const ASTIdentifier* name : names_) {
    ret.push_back(name->GetAsString());
  }
  return ret;
}

std::vector<IdString> ASTPathExpression::ToIdStringVector() const {
  std::vector<IdString> ret;
  ret.reserve(names_.size());
  for (const ASTIdentifier* name : names_) {
    ret.push_back(name->GetAsIdString());
  }
  return ret;
}

std::string ASTParameterExpr::SingleNodeDebugString() const {
  if (name() != nullptr) {
    return ASTNode::SingleNodeDebugString();
  } else {
    return absl::StrCat(ASTNode::SingleNodeDebugString(), "(", position_, ")");
  }
}

std::string ASTFunctionCall::SingleNodeDebugString() const {
  std::vector<std::string> modifiers;
  if (distinct()) {
    modifiers.push_back("distinct=true");
  }
  if (is_chained_call()) {
    modifiers.push_back("is_chained_call=true");
  }
  if (modifiers.empty()) {
    return ASTNode::SingleNodeDebugString();
  } else {
    return absl::StrCat(ASTNode::SingleNodeDebugString(), "(",
                        absl::StrJoin(modifiers, ", "), ")");
  }
}

std::string ASTWindowFrame::SingleNodeDebugString() const {
  return absl::StrCat(ASTNode::SingleNodeDebugString(), "(",
                      GetFrameUnitString(), ")");
}

// static
std::string ASTWindowFrame::FrameUnitToString(FrameUnit unit) {
  switch (unit) {
    case ROWS:
      return "ROWS";
    case RANGE:
      return "RANGE";
    default:
      ABSL_LOG(ERROR) << "Unknown analytic window frame unit: " << unit;
      return "";
  }
}

std::string ASTWindowFrame::GetFrameUnitString() const {
  return FrameUnitToString(frame_unit_);
}

std::string ASTWindowFrameExpr::SingleNodeDebugString() const {
  return absl::StrCat(ASTNode::SingleNodeDebugString(), "(",
                      GetBoundaryTypeString(), ")");
}

// static
std::string ASTWindowFrameExpr::BoundaryTypeToString(BoundaryType type) {
  switch (type) {
    case UNBOUNDED_PRECEDING:
      return "UNBOUNDED PRECEDING";
    case OFFSET_PRECEDING:
      return "OFFSET PRECEDING";
    case CURRENT_ROW:
      return "CURRENT ROW";
    case OFFSET_FOLLOWING:
      return "OFFSET FOLLOWING";
    case UNBOUNDED_FOLLOWING:
      return "UNBOUNDED FOLLOWING";
    default:
      ABSL_LOG(ERROR) << "Unknown analytic window frame expression type:" << type;
      return "";
  }
}

std::string ASTWindowFrameExpr::GetBoundaryTypeString() const {
  return BoundaryTypeToString(boundary_type_);
}

std::string ASTExpressionSubquery::ModifierToString(Modifier modifier) {
  switch (modifier) {
    case ARRAY:
      return "ARRAY";
    case EXISTS:
      return "EXISTS";
    case VALUE:
      return "VALUE";
    case NONE:
      return "";
  }
}

std::string ASTExpressionSubquery::SingleNodeDebugString() const {
  if (modifier_ == NONE) {
    return ASTNode::SingleNodeDebugString();
  } else {
    return absl::StrCat(ASTNode::SingleNodeDebugString(),
                        "(modifier=", ModifierToString(modifier_), ")");
  }
}

std::string ASTIdentifier::SingleNodeDebugString() const {
  return absl::StrCat(ASTNode::SingleNodeDebugString(), "(",
                      ToIdentifierLiteral(id_string_), ")");
}

bool ASTIntLiteral::is_hex() const {
  if (absl::StartsWith(image(), "0x") || absl::StartsWith(image(), "0X")) {
    return true;
  }
  return false;
}

std::string ASTDateOrTimeLiteral::SingleNodeDebugString() const {
  return absl::StrCat("DateOrTimeLiteral(", TypeKind_Name(type_kind_), ")");
}

void ASTCreateStatement::CollectModifiers(
    std::vector<std::string>* modifiers) const {
  switch (scope_) {
    case ASTCreateStatement::PRIVATE:
      modifiers->push_back("is_private");
      break;
    case ASTCreateStatement::PUBLIC:
      modifiers->push_back("is_public");
      break;
    case ASTCreateStatement::TEMPORARY:
      modifiers->push_back("is_temp");
      break;
    case ASTCreateStatement::DEFAULT_SCOPE:
      break;
  }

  if (is_or_replace_) {
    modifiers->push_back("is_or_replace");
  }
  if (is_if_not_exists_) {
    modifiers->push_back("is_if_not_exists");
  }
}

std::string ASTCreateStatement::SingleNodeDebugString() const {
  std::vector<std::string> modifiers;
  CollectModifiers(&modifiers);
  if (modifiers.empty()) {
    return ASTNode::SingleNodeDebugString();
  } else {
    return absl::StrCat(ASTNode::SingleNodeDebugString(), "(",
                        absl::StrJoin(modifiers, ", "), ")");
  }
}

std::string ASTFunctionParameter::SingleNodeDebugString() const {
  std::vector<std::string> modifiers;
  if (is_not_aggregate()) {
    modifiers.push_back("is_not_aggregate=true");
  }
  if (procedure_parameter_mode() != ProcedureParameterMode::NOT_SET) {
    modifiers.push_back(absl::StrCat(
        "mode=", ProcedureParameterModeToString(procedure_parameter_mode())));
  }
  if (default_value()) {
    modifiers.push_back(absl::StrCat(
        "default_value=(", default_value()->SingleNodeDebugString(), ")"));
  }
  if (modifiers.empty()) {
    return ASTNode::SingleNodeDebugString();
  } else {
    return absl::StrCat(ASTNode::SingleNodeDebugString(), "(",
                        absl::StrJoin(modifiers, ", "), ")");
  }
}

// static
std::string ASTFunctionParameter::ProcedureParameterModeToString(
    ProcedureParameterMode mode) {
  switch (mode) {
    case ProcedureParameterMode::IN:
      return "IN";
    case ProcedureParameterMode::OUT:
      return "OUT";
    case ProcedureParameterMode::INOUT:
      return "INOUT";
    case ProcedureParameterMode::NOT_SET:
      return "";
  }
}

bool ASTFunctionParameter::IsTableParameter() const {
  return (tvf_schema_ != nullptr ||
          (templated_parameter_type_ != nullptr &&
           templated_parameter_type_->kind() ==
               ASTTemplatedParameterType::ANY_TABLE));
}

bool ASTFunctionDeclaration::IsTemplated() const {
  for (const ASTFunctionParameter* parameter :
           parameters()->parameter_entries()) {
    if (parameter->templated_parameter_type() != nullptr) {
      return true;
    }
  }
  return false;
}

std::string ASTCreateFunctionStmtBase::SingleNodeDebugString() const {
  return ASTCreateStatement::SingleNodeDebugString();
}

static std::string SqlForSqlSecurity(
    ASTCreateStatement::SqlSecurity sql_security) {
  switch (sql_security) {
    case ASTCreateStatement::SQL_SECURITY_INVOKER:
      return "SQL SECURITY INVOKER";
    case ASTCreateStatement::SQL_SECURITY_DEFINER:
      return "SQL SECURITY DEFINER";
    case ASTCreateStatement::SQL_SECURITY_UNSPECIFIED:
      return "";
  }
}

static std::string SqlForExternalSecurity(
    ASTCreateStatement::SqlSecurity external_security) {
  switch (external_security) {
    case ASTCreateStatement::SQL_SECURITY_INVOKER:
      return "EXTERNAL SECURITY INVOKER";
    case ASTCreateStatement::SQL_SECURITY_DEFINER:
      return "EXTERNAL SECURITY DEFINER";
    case ASTCreateStatement::SQL_SECURITY_UNSPECIFIED:
      return "";
  }
}

static std::string SqlForDeterminismLevel(
    ASTCreateFunctionStmtBase::DeterminismLevel level) {
  switch (level) {
    case ASTCreateFunctionStmtBase::NOT_DETERMINISTIC:
      return "NOT DETERMINISTIC";
    case ASTCreateFunctionStmtBase::DETERMINISTIC:
      return "DETERMINISTIC";
    case ASTCreateFunctionStmtBase::VOLATILE:
      return "VOLATILE";
    case ASTCreateFunctionStmtBase::STABLE:
      return "STABLE";
    case ASTCreateFunctionStmtBase::IMMUTABLE:
      return "IMMUTABLE";
    case ASTCreateFunctionStmtBase::DETERMINISM_UNSPECIFIED:
      return "";
  }
}

std::string ASTCreateProcedureStatement::SingleNodeDebugString() const {
  std::string security_str =
      external_security() != SQL_SECURITY_UNSPECIFIED
          ? absl::StrCat("(", SqlForExternalSecurity(external_security()), ")")
          : "";
  return absl::StrCat(ASTCreateStatement::SingleNodeDebugString(),
                      security_str);
}

std::string ASTCreateProcedureStatement::GetSqlForExternalSecurity() const {
  return SqlForExternalSecurity(external_security());
}

std::string ASTCreateFunctionStatement::SingleNodeDebugString() const {
  std::string aggregate = is_aggregate() ? "(is_aggregate=true)" : "";
  std::string security_str =
      sql_security() != SQL_SECURITY_UNSPECIFIED
          ? absl::StrCat("(", GetSqlForSqlSecurity(), ")")
          : "";
  std::string determinism =
      determinism_level() != DETERMINISM_UNSPECIFIED
          ? absl::StrCat("(", GetSqlForDeterminismLevel(), ")")
          : "";
  return absl::StrCat(ASTCreateFunctionStmtBase::SingleNodeDebugString(),
                      aggregate, security_str, determinism);
}

std::string ASTCreateFunctionStmtBase::GetSqlForSqlSecurity() const {
  return SqlForSqlSecurity(sql_security());
}

std::string ASTCreateFunctionStmtBase::GetSqlForDeterminismLevel() const {
  return SqlForDeterminismLevel(determinism_level());
}

void ASTCreateViewStatementBase::CollectModifiers(
    std::vector<std::string>* modifiers) const {
  ASTCreateStatement::CollectModifiers(modifiers);
  if (sql_security() != SQL_SECURITY_UNSPECIFIED) {
    modifiers->push_back(GetSqlForSqlSecurity());
  }
  if (recursive_) {
    modifiers->push_back("recursive");
  }
}

std::string ASTCreateViewStatementBase::GetSqlForSqlSecurity() const {
  return SqlForSqlSecurity(sql_security());
}

std::string ASTCreateTableFunctionStatement::SingleNodeDebugString() const {
  return ASTCreateFunctionStmtBase::SingleNodeDebugString();
}

// Validates that 'target_path' is an ASTPathExpression (which is required for a
// non-nested DML statement). In that case, returns 'target_path' as an
// ASTPathExpression. Otherwise returns an error based on
// 'statement_type'.
static absl::StatusOr<const ASTPathExpression*>
GetTargetPathForNonNestedDMLStatement(
    absl::string_view statement_type,
    const ASTGeneralizedPathExpression* target_path) {
  GOOGLESQL_DCHECK_OK(ASTGeneralizedPathExpression::VerifyIsPureGeneralizedPathExpression(
      target_path));
  if (target_path->node_kind() == AST_PATH_EXPRESSION) {
    return target_path->GetAs<ASTPathExpression>();
  }

  // Find the parent node of the ASTPathExpression and use its right hand side
  // as the AST location for the error message.
  const ASTExpression* expr = target_path;
  const ASTNode* expr_rhs;
  while (true) {
    const ASTExpression* expr_lhs;

    switch (expr->node_kind()) {
      case AST_DOT_GENERALIZED_FIELD: {
        const auto* dot_generalized_field =
            expr->GetAs<ASTDotGeneralizedField>();
        expr_lhs = dot_generalized_field->expr();
        expr_rhs = dot_generalized_field->path();
        break;
      }
      case AST_DOT_IDENTIFIER: {
        const auto* dot_identifier = expr->GetAs<ASTDotIdentifier>();
        expr_lhs = dot_identifier->expr();
        expr_rhs = dot_identifier->name();
        break;
      }
      case AST_ARRAY_ELEMENT: {
        const auto* array_element = expr->GetAs<ASTArrayElement>();
        expr_lhs = array_element->array();
        expr_rhs = array_element->position();
        break;
      }
      default:
        GOOGLESQL_RET_CHECK_FAIL() << "Unexpected node kind in "
                         << "GetTargetPathForNonNestedDMLStatement(): "
                         << target_path->GetNodeKindString();
    }

    if (expr_lhs->node_kind() == AST_PATH_EXPRESSION) {
      break;
    }
    expr = expr_lhs;
  }

  return MakeSqlErrorAt(expr_rhs) << "Non-nested " << statement_type
                                  << " statement requires a table name";
}

absl::StatusOr<const ASTPathExpression*>
ASTDeleteStatement::GetTargetPathForNonNested() const {
  return GetTargetPathForNonNestedDMLStatement(/*statement_type=*/"DELETE",
                                               target_path_);
}

absl::StatusOr<const ASTPathExpression*>
ASTTruncateStatement::GetTargetPathForNonNested() const {
  return GetTargetPathForNonNestedDMLStatement(/*statement_type=*/"TRUNCATE",
                                               target_path_);
}

std::string ASTInsertStatement::SingleNodeDebugString() const {
  if (insert_mode_ == DEFAULT_MODE) {
    return ASTNode::SingleNodeDebugString();
  } else {
    return absl::StrCat(ASTNode::SingleNodeDebugString(),
                        "(insert_mode=", GetSQLForInsertMode(), ")");
  }
}

std::string ASTInsertStatement::GetSQLForInsertMode() const {
  switch (insert_mode_) {
    case DEFAULT_MODE:
      return "";
    case REPLACE:
      return "REPLACE";
    case UPDATE:
      return "UPDATE";
    case IGNORE:
      return "IGNORE";
  }
}

std::string ASTOnConflictClause::SingleNodeDebugString() const {
  if (conflict_action_ == NOT_SET) {
    return ASTNode::SingleNodeDebugString();
  } else {
    return absl::StrCat(ASTNode::SingleNodeDebugString(),
                        "(conflict_action=", GetSQLForConflictAction(), ")");
  }
}

std::string ASTOnConflictClause::GetSQLForConflictAction() const {
  switch (conflict_action_) {
    case NOT_SET:
      return "<INVALID ACTION MODE>";
    case NOTHING:
      return "NOTHING";
    case UPDATE:
      return "UPDATE";
  }
}

absl::StatusOr<const ASTPathExpression*>
ASTInsertStatement::GetTargetPathForNonNested() const {
  return GetTargetPathForNonNestedDMLStatement(/*statement_type=*/"INSERT",
                                               target_path_);
}

absl::StatusOr<const ASTPathExpression*>
ASTUpdateStatement::GetTargetPathForNonNested() const {
  return GetTargetPathForNonNestedDMLStatement(/*statement_type=*/"UPDATE",
                                               target_path_);
}

std::string ASTMergeAction::SingleNodeDebugString() const {
  const char* action_mode_string = nullptr;
  switch (action_type_) {
    case INSERT:
      action_mode_string = "INSERT";
      break;
    case UPDATE:
      action_mode_string = "UPDATE";
      break;
    case DELETE:
      action_mode_string = "DELETE";
      break;
    case NOT_SET:
      action_mode_string = "<INVALID ACTION MODE>";
      break;
  }
  return absl::StrCat(ASTNode::SingleNodeDebugString(), "(", action_mode_string,
                      ")");
}

std::string ASTMergeWhenClause::SingleNodeDebugString() const {
  return absl::StrCat(ASTNode::SingleNodeDebugString(),
                      "(match_type=", GetSQLForMatchType(), ")");
}

std::string ASTMergeWhenClause::GetSQLForMatchType() const {
  switch (match_type_) {
    case MATCHED:
      return "MATCHED";
    case NOT_MATCHED_BY_SOURCE:
      return "NOT_MATCHED_BY_SOURCE";
    case NOT_MATCHED_BY_TARGET:
      return "NOT_MATCHED_BY_TARGET";
    case NOT_SET:
      ABSL_LOG(ERROR) << "Match type of merge match clause is not set.";
      return "";
  }
}

std::string ASTSampleSize::GetSQLForUnit() const {
  ABSL_DCHECK_NE(unit_, NOT_SET);
  if (unit_ == NOT_SET) return "<UNKNOWN UNIT>";
  return unit_ == ROWS ? "ROWS" : "PERCENT";
}

std::string ASTEmptyRowPattern::SingleNodeDebugString() const {
  return absl::StrCat(ASTNode::SingleNodeDebugString(),
                      parenthesized() ? "(parenthesized=true)" : "");
}

std::string ASTQuantifier::SingleNodeDebugString() const {
  return absl::StrCat(ASTNode::SingleNodeDebugString(),
                      is_reluctant_ ? "(is_reluctant=true)" : "");
}

std::string ASTGeneratedColumnInfo::GetSqlForStoredMode() const {
  switch (stored_mode_) {
    case ASTGeneratedColumnInfo::NON_STORED:
      return "";
    case ASTGeneratedColumnInfo::STORED:
      return "STORED";
    case ASTGeneratedColumnInfo::STORED_VOLATILE:
      return "STORED VOLATILE";
  }
}

std::string ASTGeneratedColumnInfo::GetSqlForGeneratedMode() const {
  switch (generated_mode_) {
    case ASTGeneratedColumnInfo::ALWAYS:
      return "ALWAYS";
    case ASTGeneratedColumnInfo::BY_DEFAULT:
      return "BY DEFAULT";
  }
}

std::string ASTGeneratedColumnInfo::SingleNodeDebugString() const {
  std::string stored_mode = GetSqlForStoredMode();
  std::string generated_mode = GetSqlForGeneratedMode();
  std::replace(generated_mode.begin(), generated_mode.end(), ' ', '_');
  if (stored_mode.empty()) {
    return absl::StrCat(ASTNode::SingleNodeDebugString(), "(",
                        "generated_mode=", generated_mode, ")");
  }
  std::replace(stored_mode.begin(), stored_mode.end(), ' ', '_');
  return absl::StrCat(ASTNode::SingleNodeDebugString(), "(",
                      "generated_mode=", generated_mode, ", ",
                      "stored_mode=", stored_mode, ")");
}

std::string ASTNotNullColumnAttribute::SingleNodeSqlString() const {
  return "NOT NULL";
}

std::string ASTHiddenColumnAttribute::SingleNodeSqlString() const {
  return "HIDDEN";
}

std::string ASTPrimaryKeyColumnAttribute::SingleNodeSqlString() const {
  return "PRIMARY KEY";
}

std::string ASTPrimaryKeyColumnAttribute::SingleNodeDebugString() const {
  return absl::StrCat(ASTNode::SingleNodeDebugString(), "(",
                      (enforced_ ? "" : "NOT "), "ENFORCED)");
}

std::string ASTPrimaryKey::SingleNodeDebugString() const {
  return absl::StrCat(ASTNode::SingleNodeDebugString(), "(",
                      (enforced_ ? "" : "NOT "), "ENFORCED)");
}

std::string ASTForeignKeyColumnAttribute::SingleNodeSqlString() const {
  return "REFERENCES";
}

bool ASTColumnSchema::ContainsAttribute(ASTNodeKind node_kind) const {
  if (attributes() == nullptr) {
    return false;
  }
  for (const ASTColumnAttribute* attribute : attributes()->values()) {
    if (attribute->node_kind() == node_kind) {
      return true;
    }
  }
  return false;
}

std::string ASTCreateIndexStatement::SingleNodeDebugString() const {
  if (is_unique_ || is_search_ || is_vector_) {
    std::string ret = ASTNode::SingleNodeDebugString();
    absl::StrAppend(&ret, "(");
    if (is_unique_) {
      absl::StrAppend(&ret, "UNIQUE");
      if (is_search_ || is_vector_) {
        absl::StrAppend(&ret, ",");
      }
    }
    if (is_search_) {
      absl::StrAppend(&ret, "SEARCH");
    }
    if (is_vector_) {
      absl::StrAppend(&ret, "VECTOR");
    }
    absl::StrAppend(&ret, ")");
    return ret;
  } else {
    return ASTNode::SingleNodeDebugString();
  }
}

std::string ASTForeignKeyReference::SingleNodeDebugString() const {
  return absl::StrCat(ASTNode::SingleNodeDebugString(),
                      "(MATCH ", GetSQLForMatch(),
                      (enforced_ ? " " : " NOT "), "ENFORCED)");
}

std::string ASTForeignKeyReference::GetSQLForMatch() const {
  switch (match_) {
    case SIMPLE:
      return "SIMPLE";
    case FULL:
      return "FULL";
    case NOT_DISTINCT:
      return "NOT DISTINCT";
  }
}

std::string ASTForeignKeyActions::SingleNodeDebugString() const {
  return absl::StrCat(ASTNode::SingleNodeDebugString(),
                      "(ON UPDATE ", GetSQLForAction(update_action_),
                      " ON DELETE ", GetSQLForAction(delete_action_),
                      ")");
}

std::string ASTForeignKeyActions::GetSQLForAction(Action action) {
  switch (action) {
    case NO_ACTION:
      return "NO ACTION";
    case RESTRICT:
      return "RESTRICT";
    case CASCADE:
      return "CASCADE";
    case SET_NULL:
      return "SET NULL";
  }
}

std::string ASTCheckConstraint::SingleNodeDebugString() const {
  return absl::StrCat(ASTNode::SingleNodeDebugString(),
                      is_enforced_ ? "(ENFORCED)" : "(NOT ENFORCED)");
}

std::string ASTSetCollateClause::GetSQLForAlterAction() const {
  return "SET DEFAULT COLLATE";
}
std::string ASTAlterSubEntityAction::GetSQLForAlterAction() const {
  return absl::StrCat("ALTER ", type()->GetAsString());
}

std::string ASTAlterSubEntityAction::SingleNodeDebugString() const {
  return absl::StrCat(ASTNode::SingleNodeDebugString(),
                      is_if_exists_ ? "(is_if_exists)" : "");
}

std::string ASTAddSubEntityAction::GetSQLForAlterAction() const {
  return absl::StrCat("ADD ", type()->GetAsString());
}

std::string ASTAddSubEntityAction::SingleNodeDebugString() const {
  return absl::StrCat(ASTNode::SingleNodeDebugString(),
                      is_if_not_exists_ ? "(is_if_not_exists)" : "");
}

std::string ASTDropSubEntityAction::GetSQLForAlterAction() const {
  return absl::StrCat("DROP ", type()->GetAsString());
}

std::string ASTDropSubEntityAction::SingleNodeDebugString() const {
  return absl::StrCat(ASTNode::SingleNodeDebugString(),
                      is_if_exists_ ? "(is_if_exists)" : "");
}

std::string ASTSetOptionsAction::GetSQLForAlterAction() const {
  return "SET OPTIONS";
}

std::string ASTSetAsAction::GetSQLForAlterAction() const {
  return "SET AS";
}

std::string ASTAddConstraintAction::SingleNodeDebugString() const {
  return absl::StrCat(ASTNode::SingleNodeDebugString(),
                      is_if_not_exists() ? "(is_if_not_exists)" : "");
}

std::string ASTAddConstraintAction::GetSQLForAlterAction() const {
  return "ADD CONSTRAINT";
}

std::string ASTDropConstraintAction::SingleNodeDebugString() const {
  return absl::StrCat(ASTNode::SingleNodeDebugString(),
                      is_if_exists() ? "(is_if_exists)" : "");
}

std::string ASTDropConstraintAction::GetSQLForAlterAction() const {
  return "DROP CONSTRAINT";
}

std::string ASTDropPrimaryKeyAction::SingleNodeDebugString() const {
  return absl::StrCat(ASTNode::SingleNodeDebugString(),
                      is_if_exists() ? "(is_if_exists)" : "");
}

std::string ASTDropPrimaryKeyAction::GetSQLForAlterAction() const {
  return "DROP PRIMARY KEY";
}

std::string ASTAlterConstraintSetOptionsAction::SingleNodeDebugString() const {
  return absl::StrCat(ASTNode::SingleNodeDebugString(),
                      is_if_exists() ? "(is_if_exists)" : "");
}

std::string ASTAlterConstraintSetOptionsAction::GetSQLForAlterAction() const {
  return "ALTER CONSTRAINT SET OPTIONS";
}

std::string ASTAlterConstraintEnforcementAction::SingleNodeDebugString() const {
  return absl::StrCat(ASTNode::SingleNodeDebugString(),
                      is_if_exists() ? "(is_if_exists)" : "");
}

std::string ASTAlterConstraintEnforcementAction::GetSQLForAlterAction() const {
  return "ALTER CONSTRAINT [NOT] ENFORCED";
}

std::string ASTAddColumnAction::SingleNodeDebugString() const {
  return absl::StrCat(ASTNode::SingleNodeDebugString(),
                      is_if_not_exists() ? "(is_if_not_exists)" : "");
}

std::string ASTAddColumnAction::GetSQLForAlterAction() const {
  return "ADD COLUMN";
}

std::string ASTAddColumnIdentifierAction::SingleNodeDebugString() const {
  return absl::StrCat(ASTNode::SingleNodeDebugString(),
                      is_if_not_exists() ? "(is_if_not_exists)" : "");
}

std::string ASTAddColumnIdentifierAction::GetSQLForAlterAction() const {
  return "ADD COLUMN OPTIONS";
}

std::string ASTRebuildAction::GetSQLForAlterAction() const { return "REBUILD"; }

std::string ASTColumnPosition::SingleNodeDebugString() const {
  return absl::StrCat(ASTNode::SingleNodeDebugString(),
                      type() == PRECEDING ? "(PRECEDING)" : "(FOLLOWING)");
}

std::string ASTAlterColumnOptionsAction::SingleNodeDebugString() const {
  return absl::StrCat(ASTNode::SingleNodeDebugString(),
                      is_if_exists() ? "(is_if_exists)" : "");
}

std::string ASTAlterColumnOptionsAction::GetSQLForAlterAction() const {
  return "ALTER COLUMN SET OPTIONS";
}

std::string ASTAlterColumnTypeAction::SingleNodeDebugString() const {
  return absl::StrCat(ASTNode::SingleNodeDebugString(),
                      is_if_exists() ? "(is_if_exists)" : "");
}

std::string ASTAlterColumnTypeAction::GetSQLForAlterAction() const {
  return "ALTER COLUMN SET DATA TYPE";
}

std::string ASTAlterColumnSetDefaultAction::SingleNodeDebugString() const {
  return absl::StrCat(ASTNode::SingleNodeDebugString(),
                      is_if_exists() ? "(is_if_exists)" : "");
}

std::string ASTAlterColumnSetDefaultAction::GetSQLForAlterAction() const {
  return "ALTER COLUMN SET DEFAULT";
}

std::string ASTAlterColumnDropDefaultAction::SingleNodeDebugString() const {
  return absl::StrCat(ASTNode::SingleNodeDebugString(),
                      is_if_exists() ? "(is_if_exists)" : "");
}

std::string ASTAlterColumnDropDefaultAction::GetSQLForAlterAction() const {
  return "ALTER COLUMN DROP DEFAULT";
}

std::string ASTAlterColumnDropNotNullAction::SingleNodeDebugString() const {
  return absl::StrCat(ASTNode::SingleNodeDebugString(),
                      is_if_exists() ? "(is_if_exists)" : "");
}

std::string ASTAlterColumnDropNotNullAction::GetSQLForAlterAction() const {
  return "ALTER COLUMN DROP NOT NULL";
}

std::string ASTAlterColumnDropGeneratedAction::SingleNodeDebugString() const {
  return absl::StrCat(ASTNode::SingleNodeDebugString(),
                      is_if_exists() ? "(is_if_exists)" : "");
}

std::string ASTAlterColumnDropGeneratedAction::GetSQLForAlterAction() const {
  return "ALTER COLUMN DROP GENERATED";
}

std::string ASTAlterColumnSetGeneratedAction::SingleNodeDebugString() const {
  return absl::StrCat(ASTNode::SingleNodeDebugString(),
                      is_if_exists() ? "(is_if_exists)" : "");
}

std::string ASTAlterColumnSetGeneratedAction::GetSQLForAlterAction() const {
  return "ALTER COLUMN SET GENERATED";
}

std::string ASTSpannerAlterColumnAction::GetSQLForAlterAction() const {
  return "ALTER COLUMN";
}

std::string ASTSpannerSetOnDeleteAction::GetSQLForAlterAction() const {
  return "SET ON DELETE";
}
std::string ASTDropColumnAction::SingleNodeDebugString() const {
  return absl::StrCat(ASTNode::SingleNodeDebugString(),
                      is_if_exists() ? "(is_if_exists)" : "");
}

std::string ASTDropColumnAction::GetSQLForAlterAction() const {
  return "DROP COLUMN";
}

std::string ASTRenameColumnAction::SingleNodeDebugString() const {
  return absl::StrCat(ASTNode::SingleNodeDebugString(),
                      is_if_exists() ? "(is_if_exists)" : "");
}

std::string ASTRenameColumnAction::GetSQLForAlterAction() const {
  return "RENAME COLUMN";
}

std::string ASTGrantToClause::GetSQLForAlterAction() const {
  return "GRANT TO";
}

std::string ASTRestrictToClause::GetSQLForAlterAction() const {
  return "RESTRICT TO";
}

std::string ASTAddToRestricteeListClause::GetSQLForAlterAction() const {
  return "ADD";
}

std::string ASTAddTtlAction::GetSQLForAlterAction() const {
  return "ADD ROW DELETION POLICY";
}

std::string ASTReplaceTtlAction::GetSQLForAlterAction() const {
  return "REPLACE ROW DELETION POLICY";
}

std::string ASTDropTtlAction::GetSQLForAlterAction() const {
  return "DROP ROW DELETION POLICY";
}

std::string ASTRemoveFromRestricteeListClause::GetSQLForAlterAction() const {
  return "REMOVE";
}

std::string ASTFilterFieldsArg::SingleNodeDebugString() const {
  return absl::StrCat(ASTNode::SingleNodeDebugString(), "(",
                      GetSQLForOperator(), ")");
}

std::string ASTPrintableLeaf::SingleNodeDebugString() const {
  return absl::StrCat(std::string(ASTNode::SingleNodeDebugString()), "(",
                      image_, ")");
}

std::string ASTWithClause::SingleNodeDebugString() const {
  return recursive_ ? "WithClause (recursive)" : "WithClause";
}

std::string ASTFilterFieldsArg::GetSQLForOperator() const {
  switch (filter_type_) {
    case NOT_SET:
      return "<UNKNOWN>";
    case INCLUDE:
      return "+";
    case EXCLUDE:
      return "-";
  }
}

std::string ASTFilterUsingClause::GetSQLForAlterAction() const {
  return "FILTER USING";
}

std::string ASTRevokeFromClause::SingleNodeDebugString() const {
  return absl::StrCat(ASTNode::SingleNodeDebugString(),
                      is_revoke_from_all() ? "(is_revoke_from_all)" : "");
}

std::string ASTRevokeFromClause::GetSQLForAlterAction() const {
  return "REVOKE FROM";
}

std::string ASTRenameToClause::GetSQLForAlterAction() const {
  return "RENAME TO";
}

std::string ASTAlterStatementBase::SingleNodeDebugString() const {
  const std::string node_name = ASTNode::SingleNodeDebugString();
  if (!is_if_exists()) {
    return node_name;
  }
  return absl::StrCat(node_name, "(is_if_exists)");
}

std::string ASTGqlMatch::SingleNodeDebugString() const {
  const std::string node_name = ASTNode::SingleNodeDebugString();
  if (optional()) {
    return absl::StrCat(node_name, " (optional)");
  }
  return node_name;
}

std::string ASTGraphNodeTableReference::SingleNodeDebugString() const {
  const std::string node_name = ASTNode::SingleNodeDebugString();
  std::string ref_type;
  switch (node_reference_type()) {
    case ASTGraphNodeTableReference::NODE_REFERENCE_TYPE_UNSPECIFIED:
      ref_type = "<UNSPECIFIED>";
      break;
    case ASTGraphNodeTableReference::SOURCE:
      ref_type = "SOURCE";
      break;
    case ASTGraphNodeTableReference::DESTINATION:
      ref_type = "DESTINATION";
      break;
  }

  return absl::StrCat(node_name, "(", ref_type, ")");
}

std::string ASTGraphLabelOperation::SingleNodeDebugString() const {
  const std::string node_name = ASTNode::SingleNodeDebugString();
  std::string op;
  switch (op_type()) {
    case ASTGraphLabelOperation::OPERATION_TYPE_UNSPECIFIED:
      op = "<UNSPECIFIED>";
      break;
    case ASTGraphLabelOperation::NOT:
      op = "NOT";
      break;
    case ASTGraphLabelOperation::AND:
      op = "AND";
      break;
    case ASTGraphLabelOperation::OR:
      op = "OR";
      break;
  }

  return absl::StrCat(node_name, "(", op, ")");
}

std::string ASTGraphPathPattern::SingleNodeDebugString() const {
  if (parenthesized()) {
    return absl::StrCat("Parenthesized", NodeKindToString(node_kind()));
  }
  return NodeKindToString(node_kind());
}

std::string ASTAuxLoadDataPartitionsClause::SingleNodeDebugString() const {
  return absl::StrCat(ASTNode::SingleNodeDebugString(),
                      is_overwrite_ ? "(is_overwrite)" : "");
}

std::string ASTAuxLoadDataStatement::SingleNodeDebugString() const {
  std::string result("");
  switch (insertion_mode_) {
    case ASTAuxLoadDataStatement::InsertionMode::APPEND:
      result = "(into";
      break;
    case ASTAuxLoadDataStatement::InsertionMode::OVERWRITE:
      result = "(overwrite";
      break;
    default:
      ABSL_LOG(ERROR) << "Unexpected InsertionMode for Load Data Statement: "
                  << insertion_mode_;
      result = "(unspecified";
  }
  if (is_temp_table_) {
    absl::StrAppend(&result, ", is_temp");
  }
  absl::StrAppend(&result, ")");
  return absl::StrCat(ASTNode::SingleNodeDebugString(), result);
}

absl::string_view SchemaObjectKindToName(SchemaObjectKind schema_object_kind) {
  switch (schema_object_kind) {
    case SchemaObjectKind::kAggregateFunction:
      return "AGGREGATE FUNCTION";
    case SchemaObjectKind::kApproxView:
      return "APPROX VIEW";
    case SchemaObjectKind::kConnection:
      return "CONNECTION";
    case SchemaObjectKind::kConstant:
      return "CONSTANT";
    case SchemaObjectKind::kDatabase:
      return "DATABASE";
    case SchemaObjectKind::kExternalTable:
      return "EXTERNAL TABLE";
    case SchemaObjectKind::kExternalSchema:
      return "EXTERNAL SCHEMA";
    case SchemaObjectKind::kFunction:
      return "FUNCTION";
    case SchemaObjectKind::kIndex:
      return "INDEX";
    case SchemaObjectKind::kMaterializedView:
      return "MATERIALIZED VIEW";
    case SchemaObjectKind::kModel:
      return "MODEL";
    case SchemaObjectKind::kProcedure:
      return "PROCEDURE";
    case SchemaObjectKind::kSchema:
      return "SCHEMA";
    case SchemaObjectKind::kTable:
      return "TABLE";
    case SchemaObjectKind::kTableFunction:
      return "TABLE FUNCTION";
    case SchemaObjectKind::kView:
      return "VIEW";
    case SchemaObjectKind::kPropertyGraph:
      return "PROPERTY GRAPH";
    case SchemaObjectKind::kSequence:
      return "SEQUENCE";
    default:
      return "<INVALID SCHEMA OBJECT KIND>";
  }
}

std::string ASTOptionsEntry::GetSQLForOperator() const {
  switch (assignment_op_) {
    case NOT_SET:
      return "<UNKNOWN OPERATOR>";
    case ASSIGN:
      return "=";
    case ADD_ASSIGN:
      return "+=";
    case SUB_ASSIGN:
      return "-=";
  }
}

bool SchemaObjectAllowedForSnapshot(SchemaObjectKind schema_object_kind) {
  return schema_object_kind == SchemaObjectKind::kSchema;
}

static std::string SetOperationTypeToString(
    const ASTSetOperationType* op_type) {
  static constexpr char kUnknownSetOperator[] = "<UNKNOWN SET OPERATOR>";
  if (op_type == nullptr) {
    return kUnknownSetOperator;
  }
  switch (op_type->value()) {
    case googlesql::ASTSetOperation::UNION:
      return "UNION";
    case googlesql::ASTSetOperation::INTERSECT:
      return "INTERSECT";
    case googlesql::ASTSetOperation::EXCEPT:
      return "EXCEPT";
    case googlesql::ASTSetOperation::NOT_SET:
      return kUnknownSetOperator;
  }
}

static std::string SetOperationAllOrDistinctToString(
    const ASTSetOperationAllOrDistinct* all_or_distinct) {
  if (all_or_distinct == nullptr) {
    return "";
  }
  switch (all_or_distinct->value()) {
    case googlesql::ASTSetOperation::ALL:
      return "ALL";
    case googlesql::ASTSetOperation::DISTINCT:
      return "DISTINCT";
    case googlesql::ASTSetOperation::NOT_SET:
      return "";
  }
}

std::string ASTSetOperationMetadata::GetSQLForOperation() const {
  std::string op_type_str = SetOperationTypeToString(op_type());
  std::string all_or_distinct_str =
      SetOperationAllOrDistinctToString(all_or_distinct());
  // TODO: b/350557288 - Here we preserve the original implementation and do
  // not include the metadata related to CORRESPONDING. Once all callers have
  // been updated we should update this function to include other metadata.
  return absl::StrCat(op_type_str, " ", all_or_distinct_str);
}

const ASTAliasedQuery* ASTWithClauseEntry::aliased_query() const {
  ABSL_DCHECK_EQ(num_children(), 1);
  return aliased_query_;
}

const ASTAliasedGroupRows* ASTWithClauseEntry::aliased_group_rows() const {
  ABSL_DCHECK_EQ(num_children(), 1);
  return aliased_group_rows_;
}

}  // namespace googlesql
