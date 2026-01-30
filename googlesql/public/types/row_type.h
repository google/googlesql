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

#ifndef GOOGLESQL_PUBLIC_TYPES_ROW_TYPE_H_
#define GOOGLESQL_PUBLIC_TYPES_ROW_TYPE_H_

#include <any>
#include <cstdint>
#include <string>
#include <vector>

#include "googlesql/public/options.pb.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/value_equality_check_options.h"
#include "absl/hash/hash.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace googlesql {

// These have to be forward declared to avoid circular dependencies.
class Column;
class Table;

// RowType represents ROW<TableName> and MULTIROW<TableName> types.
// ROW<TableName> represents a row of that table.
// MULTIROW<TableName> represents an unordered collection of rows of that table.
//
// RowTypes show up two ways:
// 1. As the type produced by a TableScan of T.  This is always ROW<T>.
// 2. As the type for a join producing rows of T, joining from SourceT.
//    This is ROW<T> if the join is known to produce at most one row, and
//    MULTIROW<T> if the join can produce multiple rows.
//
// These cases can be distinguished with IsSingleRow/IsMultiRow and IsJoin.
//
// See (broken link).
//
// Row types holds a pointer to a Table object from the Catalog.
// To avoid circular dependencies calling methods on Table, this also holds
// `table_name`, passed in at construction time.
//
// RowTypes for joins also bind in the source table (where it was joined
// from) and the source and target join columns (like a foreign key definition).
//
// Row types disallow most operations (comparison, hashing, etc).
class RowType : public Type {
 public:
  // The table this ROW or MULTIROW points at.
  // Values of this type are like references to rows of that table.
  const Table* table() const { return table_; }
  const std::string& table_name() const { return table_name_; }

  // For join ROWs or MULTIROWs (where IsJoin is true), these are present and
  // indicate the columns used to load joined rows.
  // The `bound_source_columns` on `bound_source_table` join to
  // `bound_columns on `table`.
  // For non-join ROWs (representing table scans), these are unset.
  const std::vector<const Column*>& bound_columns() const {
    return bound_columns_;
  }
  const Table* bound_source_table() const { return bound_source_table_; }
  const std::vector<const Column*>& bound_source_columns() const {
    return bound_source_columns_;
  }

  const RowType* AsRow() const override { return this; }

  // Indicates if this a ROW or MULTIROW type.
  // RowTypes for non-join cases are always single ROWs.
  bool IsSingleRow() const override { return !multi_row_; }
  bool IsMultiRow() const override { return multi_row_; }

  // Indicates if this a RowType for a join column.
  // For join columns, the RowType can be ROW or MULTIROW.
  // For non-join columns, it is always ROW.
  bool IsJoin() const { return !bound_columns_.empty(); }

  // Returns "ROW" or "MULTIROW".
  absl::string_view GetBaseTypeName() const;

  // For join RowTypes (which can be ROW<T> or MULTIROW<T>), this is the
  // corresponding non-join ROW<T> type produced as the element type when
  // scanning the join RowType like an array scan.  This works for both ROW<T>
  // and MULTIROW<T> since both can be scanned in joins like arrays,
  // producing 0, 1, or more rows.
  // The element type for both is a non-join ROW<T> type.
  //
  // For non-join RowTypes, this is nullptr.  Non-join RowTypes come from table
  // scans and already represent exactly one row, and can't be further
  // unnested like array scans.
  const RowType* element_type() const { return element_type_; }

  // ComponentTypes() is defined based on the element type.
  // APIs like AnnotationMap::Create(type) use this to create the correct
  // structure.
  std::vector<const Type*> ComponentTypes() const override {
    if (element_type_ == nullptr) {
      return {};
    }
    return {element_type_};
  }

  bool EqualsForSameKind(const Type* that, bool equivalent) const override {
    // A RowType is only equal to itself; two RowTypes for the same Table
    // are not considered equal or equivalent.
    return this == that;
  }

  bool HasAnyFields() const override;

  bool SupportsOrdering(const LanguageOptions& language_options,
                        std::string* type_description) const override {
    if (type_description) {
      *type_description = std::string(GetBaseTypeName());
    }
    return false;
  }

  bool SupportsEquality() const override { return false; }

  // The DebugString for join RowTypes includes "ROW<T (join)>" or
  // "MULTIROW<T from (join)>".
  void DebugStringImpl(bool details, TypeOrStringVector* stack,
                       std::string* debug_string) const override;

  std::string TypeName(ProductMode mode) const override;

  absl::StatusOr<std::string> TypeNameWithModifiers(
      const TypeModifiers& type_modifiers, ProductMode mode) const override;

  bool IsSupportedType(const LanguageOptions& language_options) const override;

  absl::HashState HashTypeParameter(absl::HashState state) const override;

  std::string CapitalizedName() const override;

  bool ValueContentEquals(
      const ValueContent& x, const ValueContent& y,
      const ValueEqualityCheckOptions& options) const override {
    // ROW types do not support equality.
    return false;
  }

  bool ValueContentLess(const ValueContent& x, const ValueContent& y,
                        const Type* other_type) const override {
    // ROW types do not support ordering.
    return false;
  }

  absl::HashState HashValueContent(const ValueContent& value,
                                   absl::HashState state) const override {
    // ROW types do not support hashing.
    return state;
  }

  std::string FormatValueContent(
      const ValueContent& value,
      const FormatValueContentOptions& options) const override;

  absl::Status SerializeValueContent(const ValueContent& value,
                                     ValueProto* value_proto) const override;

  absl::Status DeserializeValueContent(const ValueProto& value_proto,
                                       ValueContent* value) const override;

  // This is a Table::LazyColumnsTableScanContext owned by this RowType.
  // See description there.  This is used for Table-defined state shared across
  // all LazyColumn lookups associated with the same ResolvedTableScan.
  // This uses an assumption that the resolver makes a separate RowType for
  // each distinct table scan with `read_as_row_type`.
  std::any* GetTableScanContext() const { return &table_scan_context_; }

 private:
  // Constructor for non-join ROW type.
  RowType(const TypeFactoryBase* type_factory, const Table* table,
          const std::string& table_name);
  // Constructor for join ROW or MULTIROW.
  RowType(const TypeFactoryBase* type_factory, const Table* table,
          const std::string& table_name, bool multi_row,
          std::vector<const Column*> bound_columns,
          const Table* bound_source_table,
          std::vector<const Column*> bound_source_columns,
          const RowType* element_type);
  ~RowType() override;

  int64_t GetEstimatedOwnedMemoryBytesSize() const override {
    // The RowType only stores a pointer to the Table, not the Table itself.
    return sizeof(*this);
  }

  absl::Status SerializeToProtoAndDistinctFileDescriptorsImpl(
      const BuildFileDescriptorMapOptions& options, TypeProto* type_proto,
      FileDescriptorSetMap* file_descriptor_set_map) const override;

  bool SupportsGroupingImpl(const LanguageOptions& language_options,
                            const Type** no_grouping_type) const override {
    // ROW types do not support grouping.
    if (no_grouping_type != nullptr) {
      *no_grouping_type = this;
    }
    return false;
  }

  bool SupportsPartitioningImpl(
      const LanguageOptions& language_options,
      const Type** no_partitioning_type) const override {
    // ROW types do not support partitioning.
    if (no_partitioning_type != nullptr) {
      *no_partitioning_type = this;
    }
    return false;
  }

  HasFieldResult HasFieldImpl(const std::string& name, int* field_id,
                              bool include_pseudo_fields) const override;

  const Table* table_;

  const std::vector<const Column*> bound_columns_;
  const Table* bound_source_table_ = nullptr;
  const std::vector<const Column*> bound_source_columns_;

  // The table name from `table_->FullName()`, bound in at construction time.
  const std::string table_name_;

  // Indicates if this is a ROW or MULTIROW type.
  // For join RowTypes, this indicates if can produce multiple rows.
  // For non-join RowTypes, this must be false.
  bool multi_row_;

  const RowType* element_type_ = nullptr;

  // A Table::LazyColumnsTableScanContext owned by this RowType.
  // This is returned as a mutable object by GetTableScanContext.
  //
  // TODO This object isn't thread-safe but that's okay currently
  // because RowType is always created dynamically by the analyzer and used in
  // the scope of a single query only, not shared via the Catalog.
  mutable std::any table_scan_context_;

  friend class TypeFactory;
};

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_TYPES_ROW_TYPE_H_
