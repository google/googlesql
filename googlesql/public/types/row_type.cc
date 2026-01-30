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

#include "googlesql/public/types/row_type.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/language_options.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_modifiers.h"
#include "absl/hash/hash.h"
#include "googlesql/base/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/ret_check.h"

namespace googlesql {

RowType::RowType(const TypeFactoryBase* type_factory, const Table* table,
                 const std::string& table_name)
    : Type(type_factory, TYPE_ROW),
      table_(table),
      table_name_(table_name),
      multi_row_(false) {}

RowType::RowType(const TypeFactoryBase* type_factory, const Table* table,
                 const std::string& table_name, bool multi_row,
                 std::vector<const Column*> bound_columns,
                 const Table* bound_source_table,
                 std::vector<const Column*> bound_source_columns,
                 const RowType* element_type)
    : Type(type_factory, TYPE_ROW),
      table_(table),
      bound_columns_(std::move(bound_columns)),
      bound_source_table_(bound_source_table),
      bound_source_columns_(std::move(bound_source_columns)),
      table_name_(table_name),
      multi_row_(multi_row),
      element_type_(element_type) {
  ABSL_DCHECK(bound_source_table_ != nullptr);
  ABSL_DCHECK(!bound_columns_.empty())
      << "bound_columns must be set for multi_row RowTypes";
  ABSL_DCHECK_EQ(bound_columns_.size(), bound_source_columns_.size());
  // ArrayScans are allowed for all join columns, both ROW and MULTIROW.
  // The element type is the corresponding non-join ROW<T> type.
  // It's created in TypeFactory::MakeRowType rather than here because the
  // :types library doesn't depend on :type_factory.
  ABSL_DCHECK(element_type_ != nullptr);
  ABSL_DCHECK(!element_type_->IsJoin());
}

RowType::~RowType() = default;

absl::string_view RowType::GetBaseTypeName() const {
  return multi_row_ ? "MULTIROW" : "ROW";
}

void RowType::DebugStringImpl(bool details, TypeOrStringVector* stack,
                              std::string* debug_string) const {
  absl::StrAppend(debug_string, GetBaseTypeName(), "<", table_name_);
  if (IsJoin()) {
    // Including "join from TableName" would be nice but we'd need to
    // bind in the `bound_source_table_->FullName()` (or use a callback)
    // because of the circular dependency.
    absl::StrAppend(debug_string, " (join)");
  }
  absl::StrAppend(debug_string, ">");
}

absl::HashState RowType::HashTypeParameter(absl::HashState state) const {
  // Hash based on the table name
  return absl::HashState::combine(std::move(state), table_name_);
}

std::string RowType::TypeName(ProductMode mode) const {
  return absl::StrCat(GetBaseTypeName(), "<", table_name_, ">");
}

absl::StatusOr<std::string> RowType::TypeNameWithModifiers(
    const TypeModifiers& type_modifiers, ProductMode mode) const {
  GOOGLESQL_RET_CHECK(type_modifiers.IsEmpty());
  return TypeName(mode);
}

std::string RowType::CapitalizedName() const {
  return absl::StrCat(multi_row_ ? "Multirow<" : "Row<", table_name_, ">");
}

bool RowType::IsSupportedType(const LanguageOptions& language_options) const {
  return language_options.LanguageFeatureEnabled(FEATURE_ROW_TYPE);
}

std::string RowType::FormatValueContent(
    const ValueContent& value, const FormatValueContentOptions& options) const {
  return absl::StrCat("UNIMPLEMENTED ROW VALUE: ", DebugString());
}

absl::Status RowType::SerializeValueContent(const ValueContent& value,
                                            ValueProto* value_proto) const {
  return absl::UnimplementedError(
      "SerializeValueContent not implemented for RowType");
}

absl::Status RowType::DeserializeValueContent(const ValueProto& value_proto,
                                              ValueContent* value) const {
  return absl::UnimplementedError(
      "DeserializeValueContent not implemented for RowType");
}

absl::Status RowType::SerializeToProtoAndDistinctFileDescriptorsImpl(
    const BuildFileDescriptorMapOptions& options, TypeProto* type_proto,
    FileDescriptorSetMap* file_descriptor_set_map) const {
  type_proto->set_type_kind(TYPE_ROW);
  // TODO: Add RowTypeProto and serialize table reference.
  return absl::UnimplementedError("Serialize not implemented for RowType");
}

bool RowType::HasAnyFields() const {
  // Assume we always have fields under a ROW.
  // This would be false if we have ROW for a value table with scalar type,
  // but we don't allow value tables in ROW yet.
  return true;
}

// This stores a callback that implements HasFieldImpl using
// Table::FindColumn methods.
using HasColumnCallbackType =
    std::function<Type::HasFieldResult(const Table*, const std::string&)>;
static HasColumnCallbackType* row_type_has_column_callback = nullptr;
// This setter is called from `:type_with_catalog_impl` (if it's linked in)
// to install the callback.
void SetRowTypeHasColumnColumnCallback(HasColumnCallbackType callback) {
  ABSL_DCHECK(row_type_has_column_callback == nullptr);
  row_type_has_column_callback = new HasColumnCallbackType(callback);
}

Type::HasFieldResult RowType::HasFieldImpl(const std::string& name,
                                           int* field_id,
                                           bool include_pseudo_fields) const {
  if (field_id != nullptr) {
    *field_id = -1;
  }
  if (row_type_has_column_callback == nullptr) {
    // This shouldn't be reachable.  HasField will only be called by analyzer
    // code, which always has `:type_with_catalog_impl` linked in, so the
    // callback is registered.
    // TODO This function will get updated to support returning an
    // error and then this can be GOOGLESQL_RET_CHECK.
    ABSL_LOG(ERROR)
        << "Called HasFieldImpl without linking in :type_with_catalog_impl";
    return HAS_NO_FIELD;
  }
  return (*row_type_has_column_callback)(table_, name);
}

}  // namespace googlesql
