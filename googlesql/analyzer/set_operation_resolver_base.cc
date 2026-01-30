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

#include "googlesql/analyzer/set_operation_resolver_base.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "googlesql/analyzer/input_argument_type_resolver_helper.h"
#include "googlesql/analyzer/resolver_util.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parse_tree_errors.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/coercer.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/input_argument_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/resolved_ast/column_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_helper.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "googlesql/base/stl_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

SetOperationResolverBase::SetOperationResolverBase(
    const AnalyzerOptions& analyzer_options, Coercer& coercer,
    ColumnFactory& column_factory)
    : analyzer_options_(analyzer_options),
      coercer_(coercer),
      column_factory_(column_factory) {}

// static
absl::StatusOr<ResolvedSetOperationScan::SetOperationType>
SetOperationResolverBase::GetSetOperationType(
    const ASTSetOperationMetadata* metadata) {
  switch (metadata->op_type()->value()) {
    case ASTSetOperation::UNION:
      return metadata->all_or_distinct()->value() == ASTSetOperation::DISTINCT
                 ? ResolvedSetOperationScan::UNION_DISTINCT
                 : ResolvedSetOperationScan::UNION_ALL;

    case ASTSetOperation::EXCEPT:
      return metadata->all_or_distinct()->value() == ASTSetOperation::DISTINCT
                 ? ResolvedSetOperationScan::EXCEPT_DISTINCT
                 : ResolvedSetOperationScan::EXCEPT_ALL;

    case ASTSetOperation::INTERSECT:
      return metadata->all_or_distinct()->value() == ASTSetOperation::DISTINCT
                 ? ResolvedSetOperationScan::INTERSECT_DISTINCT
                 : ResolvedSetOperationScan::INTERSECT_ALL;

    case ASTSetOperation::NOT_SET:
      return MakeSqlErrorAtLocalNode(metadata) << "Invalid set operation type";
  }
}

// static
std::string SetOperationResolverBase::GetSQLForOperation(
    ResolvedSetOperationScan::SetOperationType op_type) {
  switch (op_type) {
    case ResolvedSetOperationScan::UNION_ALL:
      return "UNION ALL";
    case ResolvedSetOperationScan::UNION_DISTINCT:
      return "UNION DISTINCT";
    case ResolvedSetOperationScan::EXCEPT_ALL:
      return "EXCEPT ALL";
    case ResolvedSetOperationScan::EXCEPT_DISTINCT:
      return "EXCEPT DISTINCT";
    case ResolvedSetOperationScan::INTERSECT_ALL:
      return "INTERSECT ALL";
    case ResolvedSetOperationScan::INTERSECT_DISTINCT:
      return "INTERSECT DISTINCT";
    default:
      return "UNKNOWN";
  }
}

absl::StatusOr<InputArgumentType>
SetOperationResolverBase::GetColumnInputArgumentType(
    const ResolvedColumn& column, const ResolvedScan* resolved_scan) const {
  // If this column was computed, find the expr that computed it.
  const ResolvedExpr* expr = nullptr;
  if (resolved_scan->node_kind() == RESOLVED_PROJECT_SCAN) {
    expr = FindProjectExpr(resolved_scan->GetAs<ResolvedProjectScan>(), column);
  }
  if (expr != nullptr) {
    return GetInputArgumentTypeForExpr(
        expr, /*pick_default_type_for_untyped_expr=*/false, analyzer_options_);
  } else {
    return InputArgumentType(column.type());
  }
}

absl::StatusOr<std::vector<const Type*>>
SetOperationResolverBase::GetSuperTypesOfSetOperation(
    absl::Span<const std::vector<InputArgumentType>> column_type_lists,
    const ASTNode* error_location,
    ResolvedSetOperationScan::SetOperationType op_type,
    std::function<std::string(int)> column_identifier_in_error_string) const {
  std::vector<const Type*> supertypes;
  supertypes.reserve(column_type_lists.size());

  for (int i = 0; i < column_type_lists.size(); ++i) {
    InputArgumentTypeSet type_set;
    for (const InputArgumentType& type : column_type_lists[i]) {
      type_set.Insert(type);
    }
    const Type* supertype = nullptr;
    GOOGLESQL_RETURN_IF_ERROR(coercer_.GetCommonSuperType(type_set, &supertype));
    if (supertype == nullptr) {
      return MakeSqlErrorAt(error_location)
             << "Column " << column_identifier_in_error_string(i) << " in "
             << GetSQLForOperation(op_type) << " has incompatible types: "
             << InputArgumentType::ArgumentsToString(column_type_lists[i])
             << GetTypeCompatibilityExplanation(column_type_lists[i]);
    }

    std::string no_grouping_type;
    bool column_types_must_support_grouping =
        op_type != ResolvedSetOperationScan::UNION_ALL;
    if (column_types_must_support_grouping &&
        !supertype->SupportsGrouping(analyzer_options_.language(),
                                     &no_grouping_type)) {
      return MakeSqlErrorAt(error_location)
             << "Column " << column_identifier_in_error_string(i) << " in "
             << GetSQLForOperation(op_type)
             << " has type that does not support set operation comparisons: "
             << no_grouping_type;
    }
    supertypes.push_back(supertype);
  }
  return supertypes;
}

absl::StatusOr<ResolvedColumnList>
SetOperationResolverBase::BuildFinalColumnList(
    absl::Span<const IdString> final_column_names,
    const std::vector<const Type*>& final_column_types, IdString table_name,
    std::function<void(const ResolvedColumn&)> record_column_access) {
  GOOGLESQL_RET_CHECK_EQ(final_column_names.size(), final_column_types.size());

  ResolvedColumnList column_list;
  column_list.reserve(final_column_types.size());
  for (int i = 0; i < final_column_names.size(); ++i) {
    column_list.emplace_back(column_factory_.AllocateColumnId(), table_name,
                             final_column_names[i], final_column_types[i]);
    record_column_access(column_list.back());
  }
  return column_list;
}

absl::StatusOr<std::optional<int>>
SetOperationResolverBase::IndexMapper::GetOutputColumnIndex(
    int query_idx, int final_column_idx) const {
  auto two_way_mapping = index_mapping_.find(query_idx);
  GOOGLESQL_RET_CHECK(two_way_mapping != index_mapping_.end());

  const auto& final_to_output = two_way_mapping->second.final_to_output;
  auto output_column_idx = final_to_output.find(final_column_idx);

  if (output_column_idx == final_to_output.end()) {
    return std::nullopt;
  }
  return output_column_idx->second;
}

absl::StatusOr<std::optional<int>>
SetOperationResolverBase::IndexMapper::GetFinalColumnIndex(
    int query_idx, int output_column_idx) const {
  auto two_way_mapping = index_mapping_.find(query_idx);
  GOOGLESQL_RET_CHECK(two_way_mapping != index_mapping_.end());

  const auto& output_to_final = two_way_mapping->second.output_to_final;
  auto final_column_idx = output_to_final.find(output_column_idx);

  if (final_column_idx == output_to_final.end()) {
    return std::nullopt;
  }
  return final_column_idx->second;
}

absl::Status SetOperationResolverBase::IndexMapper::AddMapping(
    int query_idx, int final_column_idx, int output_column_idx) {
  auto& two_way_mapping = index_mapping_[query_idx];
  GOOGLESQL_RET_CHECK(two_way_mapping.final_to_output
                .insert({final_column_idx, output_column_idx})
                .second);
  GOOGLESQL_RET_CHECK(two_way_mapping.output_to_final
                .insert({output_column_idx, final_column_idx})
                .second);
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<SetOperationResolverBase::IndexMapper>>
SetOperationResolverBase::BuildIndexMapping(
    absl::Span<const IndexedColumnNames> input_scan_column_names,
    absl::Span<const IdString> final_column_names) const {
  auto index_mapper =
      std::make_unique<IndexMapper>(input_scan_column_names.size());
  absl::flat_hash_set<IdString, IdStringCaseHash, IdStringCaseEqualFunc>
      final_column_names_set(final_column_names.begin(),
                             final_column_names.end());
  for (const auto& indexed_column_names : input_scan_column_names) {
    const std::vector<IdString>& curr_names = indexed_column_names.column_names;
    absl::flat_hash_map<IdString, int, IdStringCaseHash, IdStringCaseEqualFunc>
        name_to_output_index;
    for (int output_column_idx = 0; output_column_idx < curr_names.size();
         ++output_column_idx) {
      if (!final_column_names_set.contains(curr_names[output_column_idx])) {
        // This column does not show up in the final column list, skip it.
        continue;
      }
      GOOGLESQL_RET_CHECK(name_to_output_index
                    .insert({curr_names[output_column_idx], output_column_idx})
                    .second);
    }

    for (int final_column_idx = 0; final_column_idx < final_column_names.size();
         ++final_column_idx) {
      IdString final_column_name = final_column_names[final_column_idx];
      auto output_column_index = name_to_output_index.find(final_column_name);
      if (output_column_index == name_to_output_index.end()) {
        continue;
      }
      GOOGLESQL_RET_CHECK_OK(index_mapper->AddMapping(indexed_column_names.query_idx,
                                            final_column_idx,
                                            output_column_index->second));
    }
  }
  return index_mapper;
}

absl::StatusOr<std::vector<IdString>>
SetOperationResolverBase::CalculateFinalColumnNamesForInnerCorresponding(
    absl::Span<IndexedColumnNames> input_scan_column_names,
    std::function<absl::Status()> column_intersection_empty_error) const {
  absl::flat_hash_set<IdString, IdStringCaseHash, IdStringCaseEqualFunc>
      column_intersection;
  for (const IndexedColumnNames& indexed_column_names :
       input_scan_column_names) {
    absl::flat_hash_set<IdString, IdStringCaseHash, IdStringCaseEqualFunc>
        column_names;
    for (const IdString& column_name : indexed_column_names.column_names) {
      GOOGLESQL_RET_CHECK(column_names.insert(column_name).second);
    }
    if (indexed_column_names.query_idx > 0) {
      absl::erase_if(column_intersection, [&](const IdString& name) {
        return !column_names.contains(name);
      });
    } else {
      column_intersection = column_names;
    }
  }
  if (column_intersection.empty()) {
    return column_intersection_empty_error();
  }
  std::vector<IdString> matched_column_list;
  // The columns in `matched_column_list` are returned in the order they
  // appear in the first query input.
  for (const IdString& column_name :
       input_scan_column_names.front().column_names) {
    if (column_intersection.contains(column_name)) {
      matched_column_list.push_back(column_name);
    }
  }
  return matched_column_list;
}

absl::StatusOr<std::vector<IdString>>
SetOperationResolverBase::CalculateFinalColumnNamesForStrictCorresponding(
    absl::Span<IndexedColumnNames> input_scan_column_names,
    std::function<absl::Status(int, const std::vector<IdString>&,
                               const std::vector<IdString>&)>
        input_column_mismatch_error) const {
  absl::flat_hash_set<IdString, IdStringCaseHash, IdStringCaseEqualFunc>
      first_query_column_names(
          input_scan_column_names.front().column_names.begin(),
          input_scan_column_names.front().column_names.end());
  for (const IndexedColumnNames& indexed_column_names :
       input_scan_column_names) {
    if (indexed_column_names.query_idx == 0) {
      continue;
    }
    absl::flat_hash_set<IdString, IdStringCaseHash, IdStringCaseEqualFunc>
        column_names(indexed_column_names.column_names.begin(),
                     indexed_column_names.column_names.end());
    if (!googlesql_base::HashSetEquality(first_query_column_names, column_names)) {
      return input_column_mismatch_error(
          indexed_column_names.query_idx,
          input_scan_column_names.front().column_names,
          indexed_column_names.column_names);
    }
  }
  return input_scan_column_names.front().column_names;
}

absl::StatusOr<std::vector<IdString>>
SetOperationResolverBase::CalculateFinalColumnNamesForLeftCorresponding(
    absl::Span<IndexedColumnNames> input_scan_column_names,
    std::function<absl::Status(int)> no_common_columns_error) const {
  absl::flat_hash_set<IdString, IdStringCaseHash, IdStringCaseEqualFunc>
      first_query_column_names;
  for (const IdString& column_name :
       input_scan_column_names.front().column_names) {
    GOOGLESQL_RET_CHECK(first_query_column_names.insert(column_name).second);
  }
  for (const IndexedColumnNames& indexed_column_names :
       input_scan_column_names) {
    if (indexed_column_names.query_idx == 0) {
      continue;
    }
    bool has_common = false;
    for (const IdString& column_name : indexed_column_names.column_names) {
      if (first_query_column_names.contains(column_name)) {
        has_common = true;
        break;
      }
    }
    if (!has_common) {
      return no_common_columns_error(indexed_column_names.query_idx);
    }
  }
  return input_scan_column_names.front().column_names;
}

absl::StatusOr<std::vector<IdString>>
SetOperationResolverBase::CalculateFinalColumnNamesForFullCorresponding(
    absl::Span<IndexedColumnNames> input_scan_column_names) const {
  absl::flat_hash_set<IdString, IdStringCaseHash, IdStringCaseEqualFunc>
      seen_columns;
  std::vector<IdString> unique_column_names;
  unique_column_names.reserve(
      input_scan_column_names.front().column_names.size());
  for (const IndexedColumnNames& indexed_column_names :
       input_scan_column_names) {
    for (const IdString& column : indexed_column_names.column_names) {
      if (seen_columns.insert(column).second) {
        unique_column_names.push_back(column);
      }
    }
  }
  return unique_column_names;
}

absl::StatusOr<ResolvedColumnList>
SetOperationResolverBase::GetCorrespondingFinalColumns(
    const ResolvedColumnList& final_column_list,
    const ResolvedColumnList& output_column_list, int query_idx,
    const IndexMapper& index_mapper) const {
  ResolvedColumnList matched_final_columns;
  matched_final_columns.reserve(output_column_list.size());

  for (int i = 0; i < output_column_list.size(); ++i) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::optional<int> final_column_index,
                     index_mapper.GetFinalColumnIndex(query_idx, i));
    if (!final_column_index.has_value()) {
      // This column does not show up in the final_column_list, use itself as
      // the "final" column so that no type cast is needed.
      matched_final_columns.push_back(output_column_list[i]);
      continue;
    }
    GOOGLESQL_RET_CHECK_GE(*final_column_index, 0);
    GOOGLESQL_RET_CHECK_LT(*final_column_index, final_column_list.size());
    matched_final_columns.push_back(final_column_list[*final_column_index]);
  }
  return matched_final_columns;
}

}  // namespace googlesql
