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

#ifndef GOOGLESQL_ANALYZER_SET_OPERATION_RESOLVER_BASE_H_
#define GOOGLESQL_ANALYZER_SET_OPERATION_RESOLVER_BASE_H_

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "googlesql/parser/parse_tree.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/coercer.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/input_argument_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/resolved_ast/column_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"

namespace googlesql {

// This stateless class contains basic functionalities needed to resolve set
// operation.
class SetOperationResolverBase {
 public:
  SetOperationResolverBase(const AnalyzerOptions& analyzer_options,
                           Coercer& coercer, ColumnFactory& column_factory);

  // Returns the SQL string for the given set operation type enum.
  static std::string GetSQLForOperation(
      ResolvedSetOperationScan::SetOperationType op_type);

  // Returns the set operation type enum for the given `metadata`.
  static absl::StatusOr<ResolvedSetOperationScan::SetOperationType>
  GetSetOperationType(const ASTSetOperationMetadata* metadata);

  // Returns the input argument type for the given `column` in the context of
  // the `resolved_scan`.
  absl::StatusOr<InputArgumentType> GetColumnInputArgumentType(
      const ResolvedColumn& column, const ResolvedScan* resolved_scan) const;

  // Coerces the types given by `column_type_lists` into a list of common
  // super types. Returns an error if
  // - column type does not meet set operation constraint of `op_type`; or
  // - the type coercion for any columns is impossible.
  //
  // `column_identifier_in_error_string` is a function that takes in a
  // column_idx and returns its column identifier used in error strings.
  absl::StatusOr<std::vector<const Type*>> GetSuperTypesOfSetOperation(
      absl::Span<const std::vector<InputArgumentType>> column_type_lists,
      const ASTNode* error_location,
      ResolvedSetOperationScan::SetOperationType op_type,
      std::function<std::string(int)> column_identifier_in_error_string) const;

  // Returns a column list where column names are from `final_column_names`
  // and column types are from `final_column_types`.
  //
  // REQUIRES: `final_column_names` and `final_column_types` must have the same
  // length.
  absl::StatusOr<ResolvedColumnList> BuildFinalColumnList(
      absl::Span<const IdString> final_column_names,
      const std::vector<const Type*>& final_column_types, IdString table_name,
      std::function<void(const ResolvedColumn&)> record_column_access);

  // This class provides a two-way index mapping between the columns in the
  // `output_column_list` of each input and the final columns of the set
  // operation.
  class IndexMapper {
   public:
    explicit IndexMapper(size_t num_queries) {
      for (int query_idx = 0; query_idx < num_queries; ++query_idx) {
        index_mapping_[query_idx] = {};
      }
    }

    // Returns std::nullopt if the `final_column_idx` -th final column does
    // not have a corresponding column in the `output_column_list` of the
    // `query_idx` -th input.
    absl::StatusOr<std::optional<int>> GetOutputColumnIndex(
        int query_idx, int final_column_idx) const;

    // Returns std::nullopt if for the `query_idx` -th input, its
    // `output_column_idx` -th column in the `output_column_list` of does not
    // appear in the final columns of the set operation.
    absl::StatusOr<std::optional<int>> GetFinalColumnIndex(
        int query_idx, int output_column_idx) const;

    absl::Status AddMapping(int query_idx, int final_column_idx,
                            int output_column_idx);

   private:
    struct TwoWayMapping {
      absl::flat_hash_map</*final_column_idx*/ int, /*output_column_idx*/ int>
          final_to_output;
      absl::flat_hash_map</*output_column_idx*/ int, /*final_column_idx*/ int>
          output_to_final;
    };

    absl::flat_hash_map</*query_idx*/ int, TwoWayMapping> index_mapping_;
  };

  // IndexedColumnNames contains the index of the query and the column names
  // of the corresponding input.
  struct IndexedColumnNames {
    int query_idx;
    std::vector<IdString> column_names;
  };

  // Builds an index mapping between the columns names in each
  // `input_scan_column_names` and the names in `final_column_names`.
  absl::StatusOr<std::unique_ptr<IndexMapper>> BuildIndexMapping(
      absl::Span<const IndexedColumnNames> input_scan_column_names,
      absl::Span<const IdString> final_column_names) const;

  // The output columns are the intersection of column names among
  // `input_scan_column_names`. If the intersection is empty, an SQL error
  // generated by `column_intersection_empty_error` is returned.
  //
  // The order of the output columns follows the order of column names in the
  // first scan.
  //
  // For example, consider the following statement in GoogleSQL (GQL uses a
  // similar syntax without the 'CORRESPONDING' keyword):
  //
  // ```SQL
  // SELECT 1 AS A, 2 AS B, 3 AS C
  // INNER UNION ALL CORRESPONDING
  // SELECT 4 AS B, 5 AS A, 6 AS D
  // ```
  //
  // The output columns will be [A, B]. The columns C, D are dropped, and the
  // order of A and B follows the order in the first scan.
  absl::StatusOr<std::vector<IdString>>
  CalculateFinalColumnNamesForInnerCorresponding(
      absl::Span<IndexedColumnNames> input_scan_column_names,
      std::function<absl::Status()> column_intersection_empty_error) const;

  // The output columns are identical to the columns in the first scan.
  // Subsequent scans must share the same set of column names (column order
  // can be different), otherwise an SQL error generated by
  // `input_column_mismatch_error` is returned.
  //
  // If the column names are not strictly the same, an SQL error generated by
  // `input_column_mismatch_error` is returned. The three arguments of the error
  // function are the query index of the mismatched scan, the column names in
  // the first scan, and the column names in the mismatched scan.
  absl::StatusOr<std::vector<IdString>>
  CalculateFinalColumnNamesForStrictCorresponding(
      absl::Span<IndexedColumnNames> input_scan_column_names,
      std::function<absl::Status(int, const std::vector<IdString>&,
                                 const std::vector<IdString>&)>
          input_column_mismatch_error) const;

  // The output columns are identical to the columns in the first scan.
  // Subsequent scans need at least one common column name with the first
  // scan, otherwise an SQL error generated by `no_common_columns_error` is
  // returned. Columns from these scans not found in the first scan are
  // excluded.
  //
  // For example, consider the following statement in GoogleSQL (GQL uses a
  // similar syntax without the 'CORRESPONDING' keyword):
  //
  // ```SQL
  // SELECT 1 AS A, 2 AS B
  // LEFT UNION ALL CORRESPONDING
  // SELECT 3 AS B, 4 AS A, 5 AS D
  // ```
  //
  // The output columns will be [A, B]. The column D of the second scan is
  // dropped, and the order of A and B follows the order in the first scan.
  absl::StatusOr<std::vector<IdString>>
  CalculateFinalColumnNamesForLeftCorresponding(
      absl::Span<IndexedColumnNames> input_scan_column_names,
      std::function<absl::Status(int)> no_common_columns_error) const;

  // The output columns is a union of all input scan columns ensuring no
  // duplicate aliases. The column order reflects the sequence of input
  // scans, and within each scan, the left-to-right order of first
  // appearance of each unique alias.
  //
  // For example, consider the following statement in GoogleSQL (GQL uses a
  // similar syntax without the 'CORRESPONDING' keyword):
  //
  // ```SQL
  // SELECT 1 AS A, 2 AS B
  // FULL UNION ALL CORRESPONDING
  // SELECT 3 AS C, 4 AS A, 5 AS D
  // ```
  //
  // The output columns will be [A, B, C, D]. The column A of the second
  // scan does not show up twice in the list, and its order in the list
  // is determined by its first appearance, i.e. the column A of the first
  // scan.
  absl::StatusOr<std::vector<IdString>>
  CalculateFinalColumnNamesForFullCorresponding(
      absl::Span<IndexedColumnNames> input_scan_column_names) const;

  // Returns a column list of final columns that match positionally with the
  // columns in `output_column_list`. If a column in `output_column_list` does
  // not have a corresponding column in `final_column_list` (mapping is
  // provided by `index_mapper`), the column itself is used as the "final
  // column" (to guarantee no type cast is added for those columns).
  absl::StatusOr<ResolvedColumnList> GetCorrespondingFinalColumns(
      const ResolvedColumnList& final_column_list,
      const ResolvedColumnList& output_column_list, int query_idx,
      const IndexMapper& index_mapper) const;

 private:
  const AnalyzerOptions& analyzer_options_;
  Coercer& coercer_;
  ColumnFactory& column_factory_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_ANALYZER_SET_OPERATION_RESOLVER_BASE_H_
