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

#ifndef GOOGLESQL_RESOLVED_AST_RESOLVED_COLUMN_H_
#define GOOGLESQL_RESOLVED_AST_RESOLVED_COLUMN_H_

#include <string>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/type.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/serialization.pb.h"
#include "absl/base/nullability.h"
#include "absl/hash/hash.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace googlesql {

// A column produced by part of a query (e.g. a scan or subquery).
//
// This is used in the column_list of a resolved AST node to represent a
// "slot" in the "tuple" produced by that logical operator.
// This is also used in expressions in ResolvedColumnRef to point at the
// column selected during name resolution.
//
// The column_id is the definitive identifier for a ResolvedColumn, and
// the column_id should be used to match a ResolvedColumnRef in an expression
// to the scan that produces that column.  column_ids are unique within
// a query.  If the same table is scanned multiple times, distinct column_ids
// will be chosen for each scan.
//
// Joins and other combining nodes may propagate ResolvedColumns from their
// inputs, with the same column_ids.
class ResolvedColumn {
 public:
  // Default constructor makes an uninitialized ResolvedColumn.
  ResolvedColumn() = default;
  ResolvedColumn(const ResolvedColumn&) = default;
  ResolvedColumn& operator=(const ResolvedColumn&) = default;

  // Construct a ResolvedColumn with the given <column_id> and
  // {<type> or <<annotated_type>}.
  //
  // WARNING: The constructor that takes <type> will not propagate collation or
  // other type annotations.  When creating a new ResolvedColumn from an
  // existing ResolvedColumn or ResolvedExpr, it is necessary to use
  // the <annotated_type> constructor to avoid bugs where annotations are lost.
  //
  // TODO: Maybe the <type> constructor should be deprecated, or
  // we should find another way to make sure the safe one is used where needed.
  //
  // <table_name> and <name> are for display only, have no defined meaning and
  // are required to be non-empty.
  ResolvedColumn(int column_id, IdString table_name, IdString name,
                 const Type* type)
      : ResolvedColumn(column_id, table_name, name,
                       AnnotatedType(type, /*annotation_map=*/nullptr)) {}
  ResolvedColumn(int column_id, IdString table_name, IdString name,
                 AnnotatedType annotated_type)
      : column_id_(column_id),
        table_name_(table_name),
        name_(name),
        annotated_type_(annotated_type) {
    ABSL_DCHECK_GT(column_id, 0) << "column_id should be positive";
    ABSL_DCHECK(!table_name.empty());
    ABSL_DCHECK(!name.empty());
    ABSL_DCHECK(annotated_type.type != nullptr);
  }

  // Return true if this ResolvedColumn has been initialized.
  bool IsInitialized() const { return column_id_ > 0; }

  // Reset this object so IsInitialized returns false.
  void Clear() {
    column_id_ = -1;
    table_name_.clear();
    name_.clear();
    annotated_type_ = AnnotatedType();
  }

  // Return "<table>.<column>#<column_id>".
  std::string DebugString() const;

  // Return "<column>#<column_id>".
  std::string ShortDebugString() const;

  absl::Status SaveTo(
      FileDescriptorSetMap* file_descriptor_set_map,
      ResolvedColumnProto* proto) const;

  static absl::StatusOr<ResolvedColumn> RestoreFrom(
      const ResolvedColumnProto& proto,
      const ResolvedNode::RestoreParams& params);

  int column_id() const { return column_id_; }

  // Get the table and column name.  The _id forms return an IdString so
  // do not have to copy a string.  The non-_id forms are slower and should
  // not be used in googlesql analysis code.
  //
  // <table_name> and <name> are for display only, have no defined meaning and
  // are required to be non-empty.  Semantic behavior must never be defined
  // using these names.
  std::string table_name() const { return table_name_.ToString(); }
  std::string name() const { return name_.ToString(); }
  IdString table_name_id() const { return table_name_; }
  IdString name_id() const { return name_; }

  const Type* type() const { return annotated_type_.type; }

  const AnnotationMap* /*absl_nullable*/ type_annotation_map() const {
    return annotated_type_.annotation_map;
  }

  AnnotatedType annotated_type() const { return annotated_type_; }

  // Equality is defined using column_id only.
  bool operator==(const ResolvedColumn& other) const {
    return column_id_ == other.column_id_;
  }

  // Inequality is defined using column_id only.
  bool operator!=(const ResolvedColumn& other) const {
    return !(*this == other);
  }

  // Order by column_id, so these can be stored in a set.
  bool operator<(const ResolvedColumn& other) const {
    return column_id_ < other.column_id_;
  }

  template <typename H>
  friend H AbslHashValue(H h, const ResolvedColumn& c) {
    return H::combine(std::move(h), c.column_id_);
  }

 private:
  // Globally unique ID (within one query) for this ResolvedColumn.
  // Always positive if valid.
  int column_id_ = -1;

  // Table name (or alias) this column comes from.
  // Not necessarily unique or meaningful - used only for DebugString to
  // make the resolved AST understandable when printed.
  IdString table_name_;

  // The name of this column.  Matches the identifier that could have been
  // used to select this column (if it was properly scoped).
  IdString name_;

  // The type and annotations.  Doesn't own either pointer.
  AnnotatedType annotated_type_;
};

// A vector of columns produced by an operation like a scan or subquery.
// This effectively describes the "tuple" output row that comes out of a
// logical operation.
typedef std::vector<ResolvedColumn> ResolvedColumnList;

// Return a formatted ResolvedColumnList.
// Uses an abbreviated output format if all columns have the same table_name.
std::string ResolvedColumnListToString(const ResolvedColumnList& columns);

// DEPRECATED: Use absl::Hash<ResolvedColumn> directly instead.
using ResolvedColumnHasher = absl::Hash<ResolvedColumn>;

}  // namespace googlesql

#endif  // GOOGLESQL_RESOLVED_AST_RESOLVED_COLUMN_H_
