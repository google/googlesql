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

#ifndef GOOGLESQL_PARSER_AST_NODE_H_
#define GOOGLESQL_PARSER_AST_NODE_H_

#include <stddef.h>

#include <any>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "googlesql/base/arena_allocator.h"

#include "googlesql/parser/ast_enums.pb.h"
#include "googlesql/parser/ast_node_internal.h"
#include "googlesql/parser/ast_node_kind.h"
#include "googlesql/public/parse_location.h"
#include "absl/base/attributes.h"
#include "absl/base/log_severity.h"
#include "absl/base/macros.h"
#include "absl/container/inlined_vector.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"

// This header file has the definition of ASTNode, the superclass of all
// AST classes. It should not be included directly. Include parse_tree.h.
//
// During the AST construction process, AddChild / AddChildren() add to the
// children_ vector.  In InitFields(), we store pointers to children into named
// member fields with more specific types.  This allows users to navigate to
// specific child objects directly.
//
// The AST can be used in two forms -
//   * using standard accessors like child() and parent() and
//     the visitor interface,
//   * using specialized getters that get specific children by name.

namespace googlesql {

class ParseTreeVisitor;
class ParseTreeStatusVisitor;
class NonRecursiveParseTreeVisitor;
class VisitResult;

// Converts a SchemaObjectKind to the SQL name of that kind.
absl::string_view SchemaObjectKindToName(SchemaObjectKind schema_object_kind);

// Checks whether SchemaObjectKind can be snapshotted or not.
bool SchemaObjectAllowedForSnapshot(SchemaObjectKind schema_object_kind);

// TODO: C++23 -- When "deducing this" is available in relevant toolchians,
// change member functions of ASTNode that currently `return this;` so that they
// return the type of the object they are called on rather than `ASTNode*`.

// Base class for all AST nodes.
class ASTNode : public googlesql_base::ArenaOnlyGladiator {
 public:
  explicit ASTNode(ASTNodeKind node_kind) : node_kind_(node_kind) {}
  ASTNode(const ASTNode&) = delete;
  ASTNode& operator=(const ASTNode&) = delete;

  virtual ~ASTNode();

  ASTNodeKind node_kind() const { return node_kind_; }

  // Returns a one-line description of this node, including modifiers but
  // without child nodes. Use DebugString() to get a multiline description that
  // includes child nodes.
  virtual std::string SingleNodeDebugString() const;

  void set_parent(ASTNode* parent) { parent_ = parent; }
  ASTNode* parent() const { return parent_; }

  // Appends a child to the back of the children list. This should not be called
  // directly from the parser, instead use functions provided by
  // parser_internal.h that help add children while also extending the location
  // range of the parent node.
  void AddChild(ASTNode* child);

  // Adds 'child' to front of the list of children and extends the location
  // range of this node by setting the start location of this to the start
  // location of 'child'.
  void AddChildFront(ASTNode* child);

  // This must be called after adding all children, to initialize the fields
  // based on the added children. This should be overridden in each subclass to
  // initialize the fields by using FieldLoader.
  virtual absl::Status InitFields() = 0;

  // Access to child nodes with generic types.
  int num_children() const {
    return children_.size();
  }
  const ASTNode* child(int i) const { return children_[i]; }
  ASTNode* mutable_child(int i) { return children_[i]; }

  // Returns the index of the first child of a node kind or -1 if not found.
  int find_child_index(ASTNodeKind kind) const {
    for (int i = 0; i < children_.size(); i++) {
      if (children_[i]->node_kind_ == kind) {
        return i;
      }
    }
    return -1;
  }

  // Swap the positions of any 2 children of this ASTNode.
  bool SwapChildren(int idx_a, int idx_b) {
    if (idx_a >= children_.size() || idx_b >= children_.size()) {
      return false;
    }
    std::swap(children_[idx_a], children_[idx_b]);
    return true;
  }

  // Returns whether or not this node is a specific node type.
  template <typename NodeType>
  bool Is() const {
    return node_kind_ == NodeType::kConcreteNodeKind;
  }

  // Return this node cast as a NodeType.
  // Use only when this node is known to be that type, otherwise, behavior
  // is undefined.
  template <typename NodeType>
  ABSL_DEPRECATED("Use GetAsOrDie or GetAsOrNull")
  const NodeType* GetAs() const {
    return static_cast<const NodeType*>(this);
  }

  // Return this node cast as a NodeType, or null if this is not possible.
  template <typename NodeType>
  const NodeType* GetAsOrNull() const {
    static_assert(std::is_base_of<ASTNode, NodeType>::value,
                  "NodeType must be a member of the ASTNode class hierarchy");
    return ast_node_internal::GetAsOrNullImpl<const NodeType, const ASTNode>(
        this);
  }

  // Return this node cast as a NodeType, or null if this is not possible.
  template <typename NodeType>
  NodeType* GetAsOrNull() {
    static_assert(std::is_base_of<ASTNode, NodeType>::value,
                  "NodeType must be a member of the ASTNode class hierarchy");
    return ast_node_internal::GetAsOrNullImpl<NodeType, ASTNode>(this);
  }

  // Return this node cast as a NodeType.
  // Use only when this node is known to be that type, otherwise it will crash.
  template <typename NodeType>
  const NodeType* GetAsOrDie() const {
    const NodeType* as_node_type = GetAsOrNull<NodeType>();
    ABSL_CHECK(as_node_type != nullptr) << "Could not cast " << GetNodeKindString()
                                   << " to the specified NodeType";
    return as_node_type;
  }

  // Return this node cast as a NodeType.
  // Use only when this node is known to be that type, otherwise it will crash.
  template <typename NodeType>
  NodeType* GetAsOrDie() {
    NodeType* as_node_type = GetAsOrNull<NodeType>();
    ABSL_CHECK(as_node_type != nullptr) << "Could not cast " << GetNodeKindString()
                                   << " to the specified NodeType";
    return as_node_type;
  }

  // Get all descendants of this node (inclusive) that have a type in
  // <node_kinds>.  Returns the matching nodes in <*found_nodes>.
  // Order of the output vector is not defined.
  //
  // When a node is found, the traversal does not continue below that node to
  // its children, so no returned node will ever be an ancestor of another.
  //
  // BE VERY CAREFUL using this to extract specific node types.  Think carefully
  // about whether you want the traversal to continue into nodes like
  // expression subqueries.  If not, including those kinds in <node_kinds> will
  // prevent traversing underneath those nodes.
  void GetDescendantSubtreesWithKinds(const std::set<int>& node_kinds,
                              std::vector<const ASTNode*>* found_nodes) const {
    GetDescendantsWithKindsImpl(node_kinds, found_nodes,
                                false /* continue_traversal */);
  }

  // Similar to above. It continues traversal below the found node.
  void GetDescendantsWithKinds(const std::set<int>& node_kinds,
                               std::vector<const ASTNode*>* found_nodes) const {
    GetDescendantsWithKindsImpl(node_kinds, found_nodes,
                                true /* continue_traversal */);
  }

  // Traverses the tree depth-first, using a non-recursive visitor.  Each
  // visit() method, instead of visiting the children directly, returns a
  // VisitResult object describing which children to visit and which action to
  // perform after the children are visited.
  //
  // TraverseNonRecursive() may be used as an alternative to traditional
  // visitors when stack overflow caused by a deep parse tree is a concern.
  //
  // Returns OK if all visit() methods return an OK status.  If a visit method
  // returns an error status, the traversal is aborted immediately, and the
  // failed status from the visit() method is returned here.
  absl::Status TraverseNonRecursive(
      NonRecursiveParseTreeVisitor* visitor) const;

  // Accept the visitor.
  virtual absl::Status Accept(ParseTreeStatusVisitor& visitor,
                              std::any& output) const = 0;
  ABSL_DEPRECATED("Re-base your visitor on ParseTreeStatusVisitor.")
  virtual void Accept(ParseTreeVisitor* visitor, void* data) const = 0;

  // Visit children in order.
  absl::Status ChildrenAccept(ParseTreeStatusVisitor& visitor,
                              std::any& output) const;
  ABSL_DEPRECATED("Re-base your visitor on ParseTreeStatusVisitor.")
  void ChildrenAccept(ParseTreeVisitor* visitor, void* data) const;

  // Returns a multiline tree dump. Parse locations are represented as integer
  // ranges.
  std::string DebugString(int max_depth = 512) const;

  // Returns a multiline tree dump similar to debug string, but represents parse
  // locations as fragments from the original text, supplied in <sql>, rather
  // than raw integer values.
  std::string DebugString(absl::string_view sql, int max_depth = 512) const;

  const ParseLocationRange& location() const { return parse_location_range_; }
  const ParseLocationPoint& start_location() const {
    return location().start();
  }
  const ParseLocationPoint& end_location() const { return location().end(); }

  // Sets the location range of this node and returns `this` to allow a chained
  // function or to be used in a return statement.
  ASTNode* set_location(const ParseLocationRange& range) {
    parse_location_range_ = range;
    return this;
  }
  // Sets the start location of this node and returns `this` to allow a chained
  // function or to be used in a return statement.
  ASTNode* set_start_location(const ParseLocationPoint& point) {
    parse_location_range_.set_start(point);
    return this;
  }
  // Sets the end location of this node and returns `this` to allow a chained
  // function or to be used in a return statement.
  ASTNode* set_end_location(const ParseLocationPoint& point) {
    parse_location_range_.set_end(point);
    return this;
  }
  // Sets the location range of the operator keyword and returns `this` to allow
  // a chained function or to be used in a return statement.
  ASTNode* set_operator_keyword_location(const ParseLocationRange& range) {
    operator_keyword_location_ = range;
    return this;
  }
  // Returns the location range of the operator keyword. The value can be empty.
  const ParseLocationRange& operator_keyword_location() const {
    return operator_keyword_location_;
  }

  virtual bool IsTableExpression() const { return false; }
  virtual bool IsQueryExpression() const { return false; }
  virtual bool IsExpression() const { return false; }
  virtual bool IsType() const { return false; }
  virtual bool IsLeaf() const { return false; }
  virtual bool IsStatement() const { return false; }
  virtual bool IsScriptStatement() const { return false; }
  virtual bool IsLoopStatement() const { return false; }
  virtual bool IsSqlStatement() const { return false; }
  virtual bool IsDdlStatement() const { return false; }
  virtual bool IsCreateStatement() const { return false; }
  virtual bool IsAlterStatement() const { return false; }
  virtual bool IsQuantifier() const { return false; }

  std::string GetNodeKindString() const;

  ABSL_DEPRECATED("Inline me!")
  const ParseLocationRange& GetParseLocationRange() const { return location(); }

  // If both the start and end positions have the same filename (this is
  // normally expected), then gets the position span of this node in the form:
  //   [filename:]byte_offset-byte_offset
  // Otherwise returns the position span in the form:
  //   filename:byte_offset-filename:byte_offset
  std::string GetLocationString() const;

  static std::string NodeKindToString(ASTNodeKind node_kind);

 protected:
  // Dispatches to non-recursive visitor implementation.
  // Used by TraverseNonRecursive().
  virtual absl::StatusOr<VisitResult> Accept(
      NonRecursiveParseTreeVisitor* visitor) const = 0;

  // Similar to GetDescendantsWithKinds. If 'continue_traversal' is true,
  // continues traversal below the found node.
  void GetDescendantsWithKindsImpl(const std::set<int>& node_kinds,
                                   std::vector<const ASTNode*>* found_nodes,
                                   bool continue_traversal) const;

  // FieldLoader is used to implement InitFields in a subclass. The usage
  // is to create a FieldLoader and then call the AddXYZ function to extract
  // pointers from children_ into other member fields.
  //
  // The calls to FieldLoader.AddXYZ are made in the order they are expected in
  // the parser.
  // Example:
  //   void InitFields() final {
  //     FieldLoader fl(this);
  //     fl.AddRequired(&field1_);
  //     fl.AddRequired(&field2_);
  //     fl.AddOptional(&field3_, AST_PATH_EXPRESSION);
  //     fl.AddRequired(&field4_);
  //     fl.AddOptionalExpression(&field5_);
  //     fl.AddRestAsRepeated(&field6_);
  //   }
  //
  // - AddRequired crashes if there is no remaining child to be assigned.
  // - AddOptional skips assignment if there is no remaining child, or if it
  //   is not of the correct type.
  // - AddOptionalExpression and AddOptionalType act as AddOptional, but
  //   call IsExpression() and IsType(), respectively, instead of checking a
  //   type value.
  // - AddRestAsRepeated appends all remaining children to the value, which
  //   should be a vector or InlinedVector.
  // - ~FieldLoader crashes if not all children were assigned.
  class FieldLoader {
   public:
    explicit FieldLoader(ASTNode* node)
        : node_(node), index_(0), end_(node_->num_children()) {
      if (GOOGLESQL_DEBUG_MODE) {
        for (int i = 0; i < end_; ++i) {
          ABSL_DCHECK(node_->child(i) != nullptr);
        }
      }
    }

    FieldLoader(const FieldLoader&) = delete;
    FieldLoader& operator=(const FieldLoader&) = delete;

    ~FieldLoader() { ABSL_DCHECK(was_finalized_); }

    // Gets the next child element into *v. Crashes if not available.
    template <typename T>
    absl::Status AddRequired(const T** v) {
      GOOGLESQL_RET_CHECK_LT(index_, end_);
      *v = static_cast<const T*>(node_->child(index_++));
      return absl::OkStatus();
    }

    // Gets the next child element into *v, if its node_kind is
    // <expected_node_kind>.
    template <typename T>
    void AddOptional(const T** v, int expected_node_kind) {
      if (index_ < end_ &&
          node_->child(index_)->node_kind() == expected_node_kind) {
        *v = static_cast<const T*>(node_->child(index_++));
      }
    }

    // Adds the next child element into *v, if it exists and its node_kind is
    // `Subkind` or a subclass.
    template <typename Subkind, typename T>
    void AddOptionalIfSubkind(const T** v) {
      if (index_ < end_ &&
          node_->child(index_)->GetAsOrNull<Subkind>() != nullptr) {
        *v = static_cast<const T*>(node_->child(index_++));
      }
    }

    // Appends all remaining child elements to <v>.
    template <typename T>
    void AddRestAsRepeated(absl::Span<const T* const>* v) {
      static_assert(std::is_base_of<ASTNode, T>::value,
                    "Must be a subclass of ASTNode");
      if (end_ != index_) {
        *v = absl::MakeSpan(
            reinterpret_cast<T**>(&node_->children_[index_]),
            end_ - index_);
        index_ = end_;
      }  // else, it remains an empty Span.
    }

    // Gets the next child element into *v, if IsExpression() is true for it.
    template <typename T>
    void AddOptionalExpression(const T** v) {
      if (index_ < end_ && node_->child(index_)->IsExpression()) {
        *v = static_cast<const T*>(node_->child(index_++));
      }
    }

    // Gets the next child element into *v, if IsType() is true for it.
    template <typename T>
    void AddOptionalType(const T** v) {
      if (index_ < end_ && node_->child(index_)->IsType()) {
        *v = static_cast<const T*>(node_->child(index_++));
      }
    }

    // Gets the next child element into *v, if it's a subclass of ASTQuantifier.
    template <typename T>
    void AddOptionalQuantifier(const T** v) {
      if (index_ < end_ && node_->child(index_)->IsQuantifier()) {
        *v = static_cast<const T*>(node_->child(index_++));
      }
    }

    // Appends remaining child elements to <v>, stopping when the next
    // child is !IsExpression().
    template <typename T>
    void AddRepeatedWhileIsExpression(absl::Span<const T* const>* v) {
      static_assert(std::is_base_of<ASTNode, T>::value,
                    "Must be a subclass of ASTNode");
      int start = index_;
      while (index_ < end_ && node_->child(index_)->IsExpression()) {
        index_++;
      }
      if (start != index_) {
        *v = absl::MakeSpan(
            reinterpret_cast<T**>(&node_->children_[start]),
            index_ - start);
      }  // else, it remains an empty Span.
    }

    // Appends remaining child elements to <v>, stopping when the node kind of
    // the next child is not 'node_kind'.
    template <typename T>
    void AddRepeatedWhileIsNodeKind(absl::Span<const T* const>* v,
                                    int node_kind) {
      static_assert(std::is_base_of<ASTNode, T>::value,
                    "Must be a subclass of ASTNode");
      int start = index_;
      while (index_ < end_ && node_->child(index_)->node_kind() == node_kind) {
        index_++;
      }
      if (start != index_) {
        *v = absl::MakeSpan(
            reinterpret_cast<T**>(&node_->children_[start]),
            index_ - start);
      }  // else, it remains an empty Span.
    }

    absl::Status Finalize() {
      was_finalized_ = true;
      if (index_ != end_) {
        std::string child_str;
        if (index_ > 0) {
          child_str = "\nChildren are:";
          for (int i = 0; i < end_; ++i) {
            absl::StrAppend(&child_str, i, ":\n",
                            node_->child(i)->DebugString(), "\n");
          }
        }
        GOOGLESQL_RET_CHECK_EQ(index_, end_)
            << "While constructing a " << node_->GetNodeKindString()
            << " AstNode, FieldLoader "
            << "Did not consume last " << (end_ - index_) << " children. "
            << "Next child is a " << node_->child(index_)->GetNodeKindString()
            << child_str;
      }
      return absl::OkStatus();
    }

   private:
    ASTNode* const node_;
    int index_;
    int end_;
    bool was_finalized_ = false;
  };

 private:
  // Helper class for DebugString().
  class Dumper {
   public:
    Dumper(const ASTNode* node, absl::string_view separator, int max_depth,
           std::optional<absl::string_view> sql, std::string* out)
        : node_(node),
          separator_(separator),
          max_depth_(max_depth),
          current_depth_(0),
          sql_(sql),
          out_(out) {}
    Dumper(const Dumper&) = delete;
    Dumper& operator=(const Dumper&) = delete;
    void Dump();

   private:
    bool DumpNode();
    const ASTNode* node_;
    const absl::string_view separator_;
    const int max_depth_;
    int current_depth_;
    std::optional<absl::string_view> sql_;
    std::string* out_;
  };

  // Helper function for TraverseNonRecursive, invoked immediately after each
  // call to the visitor.
  //   - <result> specifies the value returned by the visitor.
  //   - <stack> specifies a list of pending visit operations that are needed to
  //         complete the traversal; they will be executed in reverse order.
  //         TraverseNonRecursiveHelper() adds or removes items to the stack as
  //         necessary to implement the result of the visitor.
  static absl::Status TraverseNonRecursiveHelper(
      const VisitResult& result, NonRecursiveParseTreeVisitor* visitor,
      std::vector<std::function<absl::Status()>>* stack);

  // Expands the end of parse_location_range_ to include expand_range.
  void ExpandLocationRangeEnd(const ParseLocationRange& expand_range);

  ASTNodeKind node_kind_;

  ASTNode* parent_ = nullptr;

  // The location range of the node.
  ParseLocationRange parse_location_range_;

  // The location range of the node's keyword.
  ParseLocationRange operator_keyword_location_;

  // Many nodes have one to two children, so InlinedVector saves allocations.
  absl::InlinedVector<ASTNode*, 4> children_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_PARSER_AST_NODE_H_
