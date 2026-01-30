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

#ifndef GOOGLESQL_RESOLVED_AST_RESOLVED_NODE_H_
#define GOOGLESQL_RESOLVED_AST_RESOLVED_NODE_H_

#include <memory>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "googlesql/public/catalog.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/public/type.h"
#include "googlesql/resolved_ast/resolved_ast.pb.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "googlesql/resolved_ast/serialization.pb.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "google/protobuf/descriptor.h"
#include "googlesql/base/status.h"

namespace googlesql {

// Using forward declarations here to avoid circular includes.
class ResolvedASTVisitor;
class ResolvedColumn;

// This is the base class for the resolved AST.
// Subclasses are in the generated file resolved_ast.h.
// ResolvedNodeKind enum is in the generated file resolved_node_kind.h.
// See gen_resolved_ast.py.
// View code generated classes at (broken link).
//
// HTML documentation for the class hierarchy is generated in resolved_ast.html.
// A viewable copy is available at (broken link).
//
// In this hierarchy, classes are either abstract or leaves.
// The non-leaf classes (like ResolvedExpression) have no members and are meant
// for organization only.
//
// Classes in this hierarchy always take ownership of their children.
class ResolvedNode {
 public:
  using SUPER = void;  // Indicates that ResolvedNode has no parent.

  ResolvedNode() = default;
  ResolvedNode(const ResolvedNode&) = delete;
  ResolvedNode& operator=(const ResolvedNode&) = delete;
  virtual ~ResolvedNode() = default;

  // Return this node's kind.
  // e.g. googlesql::RESOLVED_TABLE_SCAN for ResolvedTableScan.
  virtual ResolvedNodeKind node_kind() const = 0;

  // Return this node's kind as a string.
  // e.g. "TableScan" for ResolvedTableScan.
  virtual std::string node_kind_string() const = 0;

  // Return true if this node is under this superclass node type.
  virtual bool IsScan() const { return false; }
  virtual bool IsExpression() const { return false; }
  virtual bool IsStatement() const { return false; }

  // Calls the node-specific Visit... method inside the ResolvedASTVisitor.
  virtual absl::Status Accept(ResolvedASTVisitor* visitor) const;

  // Calls the above Accept() method on all the direct children of this node.
  virtual absl::Status ChildrenAccept(ResolvedASTVisitor* visitor) const;

  // Returns whether or not this node is a specific node type.
  template <class SUBTYPE>
  bool Is() const {
    return dynamic_cast<const SUBTYPE*>(this) != nullptr;
  }

  // Return this node static_cast as a SUBTYPE node.
  // Use only when this node is known to be of that type.
  // Subclasses specialize this to give stronger compile-time errors
  // for impossible casts.
  template <typename SUBTYPE>
  const SUBTYPE* GetAs() const {
    return static_cast<const SUBTYPE*>(this);
  }
  template <typename SUBTYPE>
  SUBTYPE* GetAs() {
    return static_cast<SUBTYPE*>(this);
  }

  // The SaveTo(FileDescriptorSetMap*, NodeProto*) methods serializes the node
  // into given proto type. There may be multiple versions in order to support
  // inheritance properly.
  // The FileDescriptorSetMap argument is used to serialize googlesql::Type
  // objects that are defined with proto/enum descriptors. See comments above
  // Type::SerializeToProtoAndDistinctFileDescriptors() for detailed usage.
  absl::Status SaveTo(FileDescriptorSetMap* file_descriptor_set_map,
                      ResolvedNodeProto* proto) const;

  virtual absl::Status SaveTo(FileDescriptorSetMap* file_descriptor_set_map,
                              AnyResolvedNodeProto* proto) const = 0;

  // This contains parameters for deserialization. In general, all parameters
  // must be non-null, although individual nodes may only use a subset
  // of the parameters. This does not take ownership of any input.
  //
  // For deserialization to succeed, special requirements exist for FullNames of
  // objects from nested catalogs. Consider a function name like x.y.z.Function.
  // Function must be findable in the catalog under the following lookup
  // path:
  //
  //   FindFunction({x, y, z, Function}, ...)
  //
  // That is, the dot-separated function name parts are the path elements. This
  // same property must apply to Tables and Procedures as well.
  struct RestoreParams {
    RestoreParams(std::vector<const google::protobuf::DescriptorPool*> pools,
                  Catalog* catalog, TypeFactory* type_factory,
                  IdStringPool* string_pool)
        : pools(std::move(pools)),
          catalog(catalog),
          type_factory(type_factory),
          string_pool(string_pool) {}

    RestoreParams() = delete;
    RestoreParams(const RestoreParams&) = default;

    // This must contain all of the referenced types emitted from SaveTo
    // into the file_descriptor_set_map's keys.
    std::vector<const google::protobuf::DescriptorPool*> pools;

    // This must contain all referenced objects used when analyzing the
    // original AST.
    Catalog* catalog = nullptr;

    // This must be capable of constructing any types referenced by the
    // original AST.
    TypeFactory* type_factory = nullptr;

    // This is used to store any IdStrings allocated during
    // deserialization.
    IdStringPool* string_pool = nullptr;
  };

  // Deserializes any node type from <proto>.
  static absl::StatusOr<std::unique_ptr<ResolvedNode>> RestoreFrom(
      const AnyResolvedNodeProto& proto, const RestoreParams& params);

  // Specifies that <node> should be annotated with <annotation> in its tree
  // dump.
  struct NodeAnnotation {
    const ResolvedNode* node;
    absl::string_view annotation;
  };

  struct DebugStringConfig {
    absl::Span<const NodeAnnotation> annotations = {};
    // If set to true, the 'accessed' bit will be printed:
    //   {*} - the field has been accessed
    //   { } - the field has not been accessed
    bool print_accessed = false;
    // If set to true, columns will be printed with {c} in the nodes where the
    // column is created.
    bool print_created_columns = false;

    // If set to true, the debug string will use box glyphs like "├─",  instead
    // of ASCII characters like "+-".
    bool use_box_glyphs = false;
  };

  // Returns a string representation of this tree and all descendants, for
  // testing and debugging purposes. Each entry in <annotations> will cause the
  // subtree <annotation.node> to be annotated with the string
  // <annotation.annotation>. This can be used to explain the context of
  // certain individual nodes in the tree.

  std::string DebugString(const DebugStringConfig& config) const;
  std::string DebugString() const { return DebugString(DebugStringConfig{}); }

  // Check if any semantically meaningful fields have not been accessed in
  // this node or its children. If so, return a descriptive error indicating
  // some feature is unimplemented.
  //
  // Classes in this hierarchy track which member fields have been accessed.
  // Fields are tagged to indicate whether they can be ignored.  Some fields
  // can be ignored if they have a default value.
  //
  // If a non-ignorable member is not accessed by a query engine, that engine
  // must not be interpreting that field, and so would interpret a query
  // incorrectly.  In such cases, CheckFieldsAccessed() will return a
  // absl::StatusCode::kUnimplemented error with a message indicating what
  // feature is unimplemented and what field wasn't accessed.
  //
  // Engines should always call this method to ensure they are interpreting a
  // GoogleSQL query safely and not missing anything. We assume that if an
  // engine reads a field and sees a value it does not understand, the engine
  // itself will generate an unimplemented error.
  //
  // If an error is returned, it will include a DebugString printout with
  // accessed fields will be marked with '{*}', and unaccessed fields will be
  // marked with '{ }'.
  absl::Status CheckFieldsAccessed() const {
    return CheckFieldsAccessedImpl(this);
  }

  // Verifies that no non-ignorable fields are accessed. Used for testing
  // purposes to verify that methods such as DebugString(), that should not mark
  // fields as accessed, do not accidentally do so.
  virtual absl::Status CheckNoFieldsAccessed() const;

  // Reset the field accessed markers in this node and its children.
  virtual void ClearFieldsAccessed() const;

  // Set all fields as accessed in this node and its children.
  //
  // Engines can use this when this node has no semantic effect on the query.
  // e.g. An unused WITH subquery or a never-taken IF branch.
  //
  // If this node is meaningful in the query, and some fields can be safely
  // ignored, prefer to add no-op accesses to those fields explicitly.  This
  // ensures that new fields added in the future are not accidentally ignored.
  virtual void MarkFieldsAccessed() const;

  // Returns in 'child_nodes' all non-NULL ResolvedNodes that are children of
  // this node. The order of 'child_nodes' is deterministic, but callers should
  // not depend on how the roles (fields) correspond to locations, especially
  // because NULL children are omitted. For cases where the roles of child nodes
  // matter, use the specific getter functions provided by subclasses.
  virtual void GetChildNodes(
      std::vector<const ResolvedNode*>* child_nodes) const {
    child_nodes->clear();
  }

  // Adds in 'mutable_child_node_ptrs' mutable pointers to all non-NULL
  // ResolvedNodes that are children of this node.
  virtual void AddMutableChildNodePointers(
      std::vector<std::unique_ptr<const ResolvedNode>*>*
          mutable_child_node_ptrs) {}

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
  void GetDescendantsWithKinds(
      const std::set<ResolvedNodeKind>& node_kinds,
      std::vector<const ResolvedNode*>* found_nodes) const;

  // Get all descendants of this node (inclusive) where <filter_method>
  // returns true.  Returns the matching nodes in <*found_nodes>.
  // Order of the output vector is not defined.
  //
  // Unlike GetDescendantsWithKinds, this traversal does not stop at emitted
  // nodes, so the result may include nodes that are ancestors of each other.
  //
  // Example:
  //   node->GetDescendantsMatching(&ResolvedNode::IsScan, &found_nodes);
  void GetDescendantsSatisfying(
      bool (ResolvedNode::*filter_method)() const,
      std::vector<const ResolvedNode*>* found_nodes) const;

  // Return a list of ResolvedColumns created by direct fields of this node.
  // This doesn't include columns nested inside node-typed fields.
  //
  // This is generated from the resolved AST based on `column_is_created` and
  // `column_list_is_created_columns` attributes.
  virtual std::vector<ResolvedColumn> GetColumnsCreated() const;

  // Return a list of ResolvedColumns referenced by fields of this node.
  // This doesn't include columns nested inside node-typed fields.
  //
  // This is generated from the resolved AST based on `column_is_created` and
  // `column_list_is_created_columns` attributes.
  virtual std::vector<ResolvedColumn> GetColumnsReferenced() const;

  // Records the parse location range.
  void SetParseLocationRange(const ParseLocationRange& parse_location_range);

  // Clears the parse location range.
  void ClearParseLocationRange();

  // Returns the previously recorded parsed location range, or NULL. Parse
  // location ranges are only filled for some nodes (ResolvedLiterals in
  // particular) and only if AnalyzerOption::record_parse_locations() is set.
  // DEPRECATED: Use GetParseLocationRangeOrNULL().
  const ParseLocationRange* GetParseLocationOrNULL() const {
    return parse_location_range_.get();
  }
  const ParseLocationRange* GetParseLocationRangeOrNULL() const {
    return parse_location_range_.get();
  }

  // Records the parse location range.
  void SetOperatorKeywordLocationRange(const ParseLocationRange& range);

  // Get the keyword location range. The value is only filled for some nodes and
  // only if AnalyzerOption::record_parse_locations() is set. For example, this
  // function returns the location of the keyword in "INSERT" for
  // ResolvedInsertStmt.
  const ParseLocationRange* GetOperatorKeywordLocationRangeOrNULL() const {
    return operator_keyword_parse_location_range_.get();
  }

  // Clears the operator keyword location range.
  void ClearOperatorKeywordLocationRange();

  // Returns the depth of the Resolved AST tree rooted at the current node.
  // Considers all descendants of the current node, and returns the maximum
  // depth. Returns 1 if the current node is a leaf.
  int GetTreeDepth() const;

 protected:
  // Struct used to collect all fields that should be printed in DebugString.
  struct DebugStringField {
    DebugStringField(absl::string_view name_in, absl::string_view value_in,
                     bool accessed_in, bool column_created_in)
        : name(name_in),
          value(value_in),
          accessed(accessed_in),
          column_created(column_created_in) {}
    DebugStringField(absl::string_view name_in, const ResolvedNode* node_in,
                     bool accessed_in, bool column_created_in = false)
        : name(name_in),
          nodes({node_in}),
          accessed(accessed_in),
          column_created(column_created_in) {}

    template <typename T>
    DebugStringField(absl::string_view name_in, const std::vector<T>& nodes_in,
                     bool accessed_in, bool column_created_in = false)
        : name(name_in),
          accessed(accessed_in),
          column_created(column_created_in) {
      for (const auto& node : nodes_in) nodes.push_back(node.get());
    }

    // If name is non-empty, "<name>=" will be printed in front of the values.
    std::string name;

    // One of the two fields below will be filled in.
    // If nodes is non-empty, all nodes are non-NULL.
    std::string value;  // Print this value directly.
    std::vector<const ResolvedNode*>
        nodes;  // Print DebugString for these nodes.

    // Indicates that the field has been accessed.
    bool accessed;
    // Indicates that the field is a column that created in this node.
    bool column_created;
  };

  // Add all fields that should be printed in DebugString to <fields>.
  // Recursively calls same method in the superclass.
  //
  // Note: implementations should use raw member fields, rather than
  // accessor methods so that DebugString() does not accidentally cause fields
  // to be marked as "accessed".
  virtual void CollectDebugStringFields(
      std::vector<DebugStringField>* fields) const;

  // Get the name string displayed for this node in the DebugString.
  // Normally it's just node_kind_string(), but it can be customized.
  //
  // Note: implementations should use raw member fields, rather than
  // accessor methods so that DebugString() does not accidentally cause fields
  // to be marked as "accessed".
  virtual std::string GetNameForDebugString(
      const DebugStringConfig& config) const;

  // Return true if the fields returned from CollectDebugStringFields would
  // contain any field with a non-empty <nodes> vector.  These are the fields
  // that would have child nodes in the DebugString tree.
  bool HasDebugStringFieldsWithNodes() const;

  // Helpers used in custom format methods to format nodes as an assignment,
  // "<name> := <node>", with <node>'s children as children of this node.
  void CollectDebugStringFieldsWithNameFormat(
      const ResolvedNode* node, std::vector<DebugStringField>* fields) const;
  std::string GetNameForDebugStringWithNameFormat(
      absl::string_view name, const ResolvedNode* node) const;

  // Helper function to implement CheckFieldsAccessed(), which plumbs through
  // the "root" node of the tree. This is used to provide a better error message
  // about which specific field is not accessed.
  virtual absl::Status CheckFieldsAccessedImpl(const ResolvedNode* root) const {
    return absl::OkStatus();
  }

  // Helper function to allow implementations of CheckFieldsAccessedImpl()
  // to call another object's CheckFieldsAccessedImpl() method.
  static absl::Status CheckFieldsAccessedInternal(const ResolvedNode* node,
                                                  const ResolvedNode* root) {
    return node->CheckFieldsAccessedImpl(root);
  }

  // Given a vector of unique_ptrs, this returns a vector of raw pointers.
  // The output vector then owns the pointers, and the input vector is cleared.
  template <class T>
  static std::vector<T*> ReleaseVectorOfUniquePtrs(
      std::vector<std::unique_ptr<T>>* input) {
    std::vector<T*> output;
    for (auto& input_unique_ptr : *input) {
      output.push_back(input_unique_ptr.release());
    }
    input->clear();
    return output;
  }

  // This enum disambiguates constructor overloads to specifically choose
  // the new (potentially unique_ptr based) constructor.  Once this becomes
  // the only constructor, this enum can be dropped.
  enum class ConstructorOverload { NEW_CONSTRUCTOR };

 private:
  // Print the tree recursively.
  // annotations specifies additional annotations to display for specific nodes
  // prefix1 is the indentation to attach to child nodes.
  // prefix2 is the indentation to attach to the root of this tree.
  static void DebugStringImpl(const ResolvedNode* node,
                              const DebugStringConfig& config,
                              absl::string_view prefix1,
                              absl::string_view prefix2, std::string* output);

  static void AppendAnnotations(const ResolvedNode* node,
                                absl::Span<const NodeAnnotation> annotations,
                                std::string* output);

  // DebugString on these call protected methods.
  friend class ResolvedComputedColumn;
  friend class ResolvedMakeProtoField;
  friend class ResolvedOutputColumn;

  std::unique_ptr<ParseLocationRange> parse_location_range_;  // May be NULL.

  // The location range of the operator keyword. May be NULL.
  std::unique_ptr<ParseLocationRange> operator_keyword_parse_location_range_;
};

// Downcast a unique_ptr<const ResolvedNode> to a
// unique_ptr<const ResolvedNodeSubtype>.
//
// This is like node->GetAs<ResolvedNodeSubtype> but for nodes in unique_ptrs.
//
// Example:
//   std::unique_ptr<const ResolvedScan> scan = ...;
//   GOOGLESQL_RET_CHECK_EQ(scan->node_kind(), RESOLVED_FILTER_SCAN);
//   std::unique_ptr<const ResolvedFilterScan> filter_scan =
//       GetAsResolvedNode<ResolvedFilterScan>(std::move(scan));
template <typename SUBTYPE, typename TYPE>
std::unique_ptr<const SUBTYPE> GetAsResolvedNode(
    std::unique_ptr<const TYPE> node) {
  return absl::WrapUnique<const SUBTYPE>(
      node.release()->template GetAs<SUBTYPE>());
}

}  // namespace googlesql

#endif  // GOOGLESQL_RESOLVED_AST_RESOLVED_NODE_H_
