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

#ifndef GOOGLESQL_RESOLVED_AST_QUERY_EXPRESSION_H_
#define GOOGLESQL_RESOLVED_AST_QUERY_EXPRESSION_H_

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/analyzer/query_resolver_helper.h"
#include "gtest/gtest_prod.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/case.h"
#include "googlesql/base/map_util.h"

namespace googlesql {

// A struct to preserve a list of grouping set ids (for non-product case) and
// list of grouping set id lists (for multi grouping sets with product).
//
// When `is_product` is true, the `non_product_grouping_set_ids_list` will be
// empty and the `product_grouping_set_ids_list` will be populated with the
// list of grouping set ids.
//
// When `is_product` is false, the `non_product_grouping_set_ids_list` will be
// populated with the list of grouping set ids for each grouping set, and the
// `product_grouping_set_ids_list` will be empty.
struct GroupingSetIdsInfo {
  // Returns true when both `non_product_grouping_set_ids_list` and
  // `product_grouping_set_ids_list` are empty.
  bool IsEmpty() const {
    return non_product_grouping_set_ids_list.empty() &&
           product_grouping_set_ids_list.empty();
  }

  std::vector<GroupingSetIds> non_product_grouping_set_ids_list;
  std::vector<std::vector<GroupingSetIds>> product_grouping_set_ids_list;
  bool is_product;
};

// `SQLBuilder` representation of a SQL query. Holds internal state while
// traversing a ResolvedAST.
//
// `QueryExpression` (QE) is a class that stores the SQL clauses, parts of the
// final SQL query, in various fields. For example, it has fields to store the
// SELECT list (SELECT clauses and aliases), the GROUP BY list (GROUP BY
// columns), FROM and WHERE clauses, and so on. These clauses are stored without
// their leading keyword such as SELECT or WHERE, and such.
//
// QE provides a `GetSQLQuery` method that takes all the various clauses
// collected in QE, and combines them to create the final and correct SQL query.
//
// QE also maintains a state-machine to make sure that the clause fields are set
// in the right combination and order. `SQLBuilder` checks this state-machine to
// determine whether some clauses can be set at particular times, and to make
// some decisions about query generation.
//
// One of these decisions is to wrap the current clauses of QE as
// a subquery. `SQLBuilder` calls the `Wrap(alias)` method on QE for this
// purpose. Calling `Wrap` generates an SQL query for the current state of the
// QE object by calling `GetSQLQuery`, and sets it as the FROM clause of the QE
// object (with the given alias), resetting all other clauses. In other words,
// this makes the current to-be-generated query a subquery, and lets
// `SQLBuilder` set the QE clause fields again.
//
// `SQLBuilder` does this with QE again and again as it recursively traverses
// the ResolvedAST. After the traversal is over, it makes a final call to
// `GetSQLQuery` to get the final SQL query, with possibly many layers of nested
// subqueries.
//
// The `GetSQLQuery` method (indirectly) orchestrates the assembly of the final
// SQL query. It generates strings for individual clauses (e.g., SELECT, FROM,
// WHERE) using the fields filled in by `SQLBuilder` and combines them.
//
// The SQL generation logic for individual clauses is largely reusable for Pipe
// syntax due to significant overlap between Standard SQL syntax and Pipe
// syntax.
//
// `GetSQLQuery` delegates to the following methods for Standard and Pipe Syntax
// respectively:
//   - `GetStandardSQLQuery`: This method uses the clause-specific appenders to
//     construct a query in Standard SQL syntax.
//   - `GetPipeSQLQuery`: This method uses the same appenders but combines the
//     clause strings using the `|>` operator (pipe syntax). It also handles
//     cases where a query cannot be formed in Pipe syntax and falls back to
//     Standard SQL.
//
// `GetSQLQuery` acts as a dispatcher, invoking either
// `GetStandardSQLQuery` or `GetPipeSQLQuery` based on the desired syntax
// (controlled by `target_syntax_mode_`).
//
// The `WrapForPipeSyntaxMode` method, similar to the `Wrap` method,
// appends the `|>` operator to the current query instead of creating nested
// subqueries, to produce Pipe syntax SQL.
class QueryExpression {
 public:
  // A list of pairs of SQL string and alias.
  using SQLAliasPairList =
      std::vector<std::pair<std::string /*SQL*/, std::string /*alias*/>>;

  static constexpr absl::string_view kPipe = " |> ";

  // Controls the target syntax mode of the SQLBuilder. By default, the
  // SQLBuilder will output the Standard syntax with nested subqueries.
  // Setting to kPipe will output the Pipe syntax with flattened subqueries.
  enum class TargetSyntaxMode {
    kStandard,
    kPipe,
  };

  QueryExpression() = default;
  explicit QueryExpression(TargetSyntaxMode target_syntax_mode)
      : target_syntax_mode_(target_syntax_mode) {}
  QueryExpression(const QueryExpression&) = delete;
  QueryExpression& operator=(const QueryExpression&) = delete;
  ~QueryExpression() = default;

  enum QueryType {
    kDefaultQueryType = 0,
    kPositionalSetOpScan = 1,
    kCorrespondenceSetOpScan = 2
  };

  // Returns the query type of this query expression. Returns an error if the
  // internal state of the query expression is inconsistent.
  absl::StatusOr<QueryType> GetQueryType() const;

  // Returns the SQL query represented by this QueryExpression.
  std::string GetSQLQuery() const { return GetSQLQuery(target_syntax_mode_); };

  // Returns the SQL query represented by this QueryExpression in the given
  // target syntax mode.
  std::string GetSQLQuery(TargetSyntaxMode target_syntax_mode) const;

  // Mutates the QueryExpression, wrapping its previous form as a subquery in
  // the from_ clause, with the given `alias`.
  //
  // Example Usage:
  // Assume we have a QueryExpression representing the SQL query:
  // "SELECT Key FROM KeyValue"
  //
  // 1. After calling Wrap("alias", kStandard), FromClause()
  // will return: "(SELECT Key FROM KeyValue) AS alias"
  //
  // 2. After calling Wrap("alias", kPipe), FromClause() will return:
  // "KeyValue |> SELECT Key |> AS alias"
  void Wrap(absl::string_view alias) {
    WrapImpl(alias, target_syntax_mode_, target_syntax_mode_);
  }

  // Mutates the QueryExpression, wrapping its previous form as a subquery in
  // Pipe SQL syntax in the from_ clause, with the given `alias`.
  // Wraps the FROM clause to be usable in Standard syntax mode.
  //
  // Example Usage:
  // Assume we have a QueryExpression representing the SQL query:
  // "SELECT Key FROM KeyValue"
  //
  // After calling WrapForPipeSyntaxMode("alias"), FromClause() will
  // return: "(SELECT Key FROM KeyValue) |> AS alias"
  void WrapForPipeSyntaxMode(absl::string_view alias) {
    WrapImpl(alias, TargetSyntaxMode::kStandard, TargetSyntaxMode::kPipe);
  }

  // The below TrySet... methods return true if we are able to set the concerned
  // clause in QueryExpression successfully. Otherwise return false with the
  // QueryExpression left unchanged.
  bool TrySetWithClause(const SQLAliasPairList& with_list, bool recursive);
  bool TrySetSelectClause(const SQLAliasPairList& select_list,
                          absl::string_view select_hints);
  bool TrySetFromClause(absl::string_view from);
  bool TrySetWhereClause(absl::string_view where);
  bool TrySetSetOpScanList(
      std::vector<std::unique_ptr<QueryExpression>>* set_op_scan_list,
      absl::string_view set_op_type, absl::string_view set_op_modifier,
      absl::string_view set_op_column_match_mode,
      absl::string_view set_op_column_propagation_mode,
      absl::string_view query_hints, absl::string_view with_depth_modifier);
  bool TrySetGroupByClause(const std::map<int, std::string>& group_by_list,
                           absl::string_view group_by_hints,
                           const GroupingSetIdsInfo& grouping_set_ids_info,
                           const std::vector<int>& rollup_column_id_list);
  bool TrySetOrderByClause(const std::vector<std::string>& order_by_list,
                           absl::string_view order_by_hints);
  bool TrySetLimitClause(absl::string_view limit);
  bool TrySetOffsetClause(absl::string_view offset);
  bool TrySetWithAnonymizationClause(absl::string_view anonymization_options);
  bool TrySetPivotClause(absl::string_view pivot);
  bool TrySetUnpivotClause(absl::string_view unpivot);
  bool TrySetMatchRecognizeClause(absl::string_view match_recognize);
  bool TrySetLockModeClause(absl::string_view lock_mode);

  // Returns true if the clauses necessary to form a SQL query, i.e. select_list
  // or set_op_scan_list, are filled in QueryExpression. Otherwise false.
  bool CanFormSQLQuery() const;

  // Returns true if the conditions necessary to form a SQL query in Pipe
  // syntax are met.
  bool CanFormPipeSQLQuery() const;

  // The below CanSet... methods return true if filling in the concerned clause
  // in the QueryExpression will succeed (without mutating it or wrapping it as
  // a subquery). Otherwise false.
  bool CanSetWithClause() const;
  bool CanSetSelectClause() const;
  bool CanSetFromClause() const;
  bool CanSetWhereClause() const;
  bool CanSetSetOpScanList() const;
  bool CanSetGroupByClause() const;
  bool CanSetOrderByClause() const;
  bool CanSetLimitClause() const;
  bool CanSetOffsetClause() const;
  bool CanSetWithAnonymizationClause() const;
  bool CanSetPivotClause() const;
  bool CanSetUnpivotClause() const;
  bool CanSetMatchRecognizeClause() const;
  bool CanSetLockModeClause() const;

  // The below Has... methods return true if the concerned clause is present
  // inside the QueryExpression. Otherwise false.
  bool HasWithClause() const { return !with_list_.empty(); }
  bool HasSelectClause() const { return !select_list_.empty(); }
  bool HasFromClause() const { return !from_.empty(); }
  bool HasWhereClause() const { return !where_.empty(); }
  bool HasSetOpScanList() const { return !set_op_scan_list_.empty(); }
  bool HasGroupByClause() const {
    return !group_by_list_.empty() || group_by_all_;
  }
  bool HasGroupByClauseOrOnlyAggregateColumns() const {
    return HasGroupByClause() || group_by_only_aggregate_columns_;
  }
  bool HasOrderByClause() const { return !order_by_list_.empty(); }
  bool HasLimitClause() const { return !limit_.empty(); }
  bool HasOffsetClause() const { return !offset_.empty(); }
  bool HasPivotClause() const { return !pivot_.empty(); }
  bool HasUnpivotClause() const { return !unpivot_.empty(); }
  bool HasMatchRecognizeClause() const { return !match_recognize_.empty(); }
  bool HasWithAnonymizationClause() const {
    return !anonymization_options_.empty();
  }
  bool HasQueryHints() const { return !query_hints_.empty(); }

  bool HasGroupByColumn(int column_id) const {
    return googlesql_base::ContainsKey(group_by_list_, column_id);
  }
  bool HasAnyGroupByColumn() const { return !group_by_list_.empty(); }

  bool HasLockModeClause() const { return !lock_mode_.empty(); }

  absl::string_view FromClause() const { return from_; }

  // Returns an immutable reference to select_list_. For QueryExpression built
  // from a SetOp scan, it returns the select_list_ of its first subquery.
  const SQLAliasPairList& SelectList() const;

  const std::map<int, std::string>& GroupByList() const {
    return group_by_list_;
  }

  // Returns the column SQL of the group-by column by its column id.
  std::string GetGroupByColumnOrDie(int column_id) const {
    return googlesql_base::FindOrDie(group_by_list_, column_id);
  }

  // Returns the alias of the group-by column by its column id.
  std::string GetGroupByColumnAliasOrDie(int column_id) const;

  // Returns true if all group-by columns have aliases.
  bool AllGroupByColumnsHaveAliases() const;

  // Returns the group-by and aggregate columns.
  std::pair<SQLAliasPairList, SQLAliasPairList> GetGroupByAndAggregateColumns()
      const;

  // Updates the aliases of the output columns if their indexes appear in
  // `aliases`. If this query_expression corresponds to a set operation with
  // CORRESPONDING, each of its query_expression(s) corresponding to its set
  // operation items will also be updated.
  //
  // `aliases`: a map from column index to new alias. For set operations with
  // CORRESPONDING, the given aliases should not contain duplicates.
  absl::Status SetAliasesForSelectList(
      const absl::flat_hash_map<int, absl::string_view>& aliases);

  // Set the AS modifier for the SELECT.  e.g. "AS VALUE".
  void SetSelectAsModifier(absl::string_view modifier);

  void SetGroupByColumn(int column_id, std::string column_sql) {
    group_by_list_.insert_or_assign(column_id, column_sql);
  }

  absl::Status SetGroupByAllClause(
      const std::map<int, std::string>& group_by_list,
      absl::string_view group_by_hints);

  absl::Status SetGroupByOnlyAggregateColumns(
      bool group_by_only_aggregate_columns);

  void SetFromClause(std::string from) { from_ = from; }

  void AppendSelectColumn(std::string column, std::string alias) {
    select_list_.emplace_back(column, alias);
  }

  // Set the `corresponding_set_op_output_column_list` field for set operations
  // with column_match_mode = CORRESPONDING.
  void SetCorrespondingSetOpOutputColumnList(SQLAliasPairList select_list) {
    corresponding_set_op_output_column_list_ = std::move(select_list);
  }

  void ResetSelectClause() { select_list_.clear(); }

 protected:
  // Returns the WITH clause SQL.
  std::string GetWithClauseSQL() const;

  // Returns the FROM clause SQL in the Standard syntax.
  std::string GetFromClauseStandardSQL() const;

  // Returns the FROM clause SQL in the Pipe syntax.
  std::string GetFromClausePipeSQL() const;

  // Returns the FROM clause SQL where the subquery is in the Pipe syntax, but
  // FROM clause is usable in the Standard syntax:
  // `FROM (<subquery in pipe syntax>)`
  std::string GetFromClauseStandardWrappingPipeSQL() const;

  // Appenders for each clause. If the clause exists, they create the SQL for
  // the clause and append it to <sql>. They return true if the clause SQL is
  // appended, false otherwise.
  bool TryAppendSelectClause(std::string& sql) const;
  bool TryAppendSetOpClauses(std::string& sql,
                             TargetSyntaxMode target_syntax_mode) const;
  bool TryAppendPivotClause(std::string& sql) const;
  bool TryAppendUnpivotClause(std::string& sql) const;
  bool TryAppendMatchRecognizeClause(std::string& sql) const;
  bool TryAppendWhereClause(std::string& sql) const;
  bool TryAppendGroupByClause(std::string& sql,
                              TargetSyntaxMode target_syntax_mode) const;
  bool TryAppendOrderByClause(std::string& sql) const;
  bool TryAppendLimitClause(std::string& sql) const;
  bool TryAppendOffsetClause(std::string& sql) const;
  bool TryAppendLockModeClause(std::string& sql) const;

  // Returns the SQL query in the Standard syntax.
  std::string GetStandardSQLQuery(
      TargetSyntaxMode from_clause_syntax_mode) const;

  // Returns the SQL query in the Pipe syntax.
  std::string GetPipeSQLQuery() const;

 private:
  FRIEND_TEST(QueryExpressionWrapTest1, TestWrapImpl1);
  FRIEND_TEST(QueryExpressionWrapTest2, TestWrapImpl2);
  // Mutates the QueryExpression, wrapping its previous form as a subquery in
  // the given target syntax, in the from_ clause, with the given `alias`.
  // The subquery is formatted in the `subquery_target_syntax_mode`, whereas
  // the final wrapped query is formatted in the `target_syntax_mode`.
  //
  // Example Usage:
  // Assume we have a QueryExpression representing the SQL query:
  // "SELECT Key FROM KeyValue"
  //
  // 1. Wrap with all modes as Standard SQL:
  // After calling WrapImpl("alias", kStandard, kStandard), FromClause()
  // will return: "(SELECT Key FROM KeyValue) AS alias"
  //
  // 2. Wrap with all modes as Pipe SQL:
  // After calling WrapImpl("alias", kPipe, kPipe), FromClause() will return:
  // "KeyValue |> SELECT Key |> AS alias"
  //
  // 3. Wrap a Pipe SQL subquery in Standard SQL query:
  // After calling WrapImpl("alias", kPipe, kStandard), FromClause() will
  // return: "(FROM KeyValue |> SELECT Key) AS alias"
  //
  // 4. Wrap a Standard SQL subquery in Pipe SQL query:
  // After calling WrapImpl("alias", kStandard, kPipe), FromClause() will
  // return: "(SELECT Key FROM KeyValue) |> AS alias"
  void WrapImpl(absl::string_view alias,
                TargetSyntaxMode subquery_target_syntax_mode,
                TargetSyntaxMode target_syntax_mode);

  void ClearAllClauses();

  // Fields below define the text associated with different clauses of a SQL
  // query. Some principles:
  // * The text does not include the keyword corresponding to the clause.
  // * If any clause is not present inside the query its corresponding text
  //   would be empty.
  // * GetSQLQuery() will combine these fields into a single SQL query.
  std::vector<
      std::pair<std::string /* with_alias */, std::string /* with_query */>>
      with_list_;

  std::vector<std::pair<std::string /* select column */,
                        std::string /* select alias */>>
      select_list_;

  // The output columns of the set operations with column_match_mode =
  // CORRESPONDING or CORRESPONDING_BY. This field is needed because for those
  // set operations, the columns that can be "selected" are not the columns in
  // the select statement of the first query.
  SQLAliasPairList corresponding_set_op_output_column_list_;

  std::string select_as_modifier_;  // "AS TypeName", "AS STRUCT", or "AS VALUE"
  std::string query_hints_;

  std::string from_;
  std::string where_;

  // Contains the keyword corresponding to the set operation (UNION | INTERSECT
  // | EXCEPT).
  std::string set_op_type_;
  // For a set operation, contains either ALL or DISTINCT.
  std::string set_op_modifier_;
  // For a set operation, contains one of ["", "CORRESPONDING",
  // "CORRESPONDING BY"]; for non set operations it is "".
  std::string set_op_column_match_mode_;
  // For a set operation, contains one of "", "FULL", "LEFT", "STRICT"; for
  // non set operations it is "".
  std::string set_op_column_propagation_mode_;
  // For a pipe recursive union query, contains the sql text of the depth
  // modifier, e.g. the "WITH DEPTH" in "|> RECURSIVE UNION ALL WITH DEPTH".
  std::string with_depth_modifier_;
  // For QueryExpression of a SetOperationScan, the set_op_scan_list will
  // contain QueryExpression for each of the input queries in the set
  // operation.
  std::vector<std::unique_ptr<QueryExpression>> set_op_scan_list_;

  // We populate the <group_by_list> in two places (in ProjectScan and
  // AggregateScan) based on where the group_by column was computed.
  // This map stores the sql text of the computed column for group_by, i.e.
  // ordinal position of select clause as text if the column was computed in
  // select list, otherwise the text form of the expression if the column was
  // computed in group_by.
  //
  // NOTE: The map is keyed by column_id of the columns created by the
  // AggregateScan. In resolver output, these columns are always allocated
  // sequentially, so this map will preserve the order of the group_by from the
  // initial query.
  std::map<int, std::string> group_by_list_;
  // Column IDs of group by keys in the ROLLUP list. group_by_list_ stores the
  // string representations of these columns. Will be non-empty only if the
  // query used ROLLUP.
  std::vector<int> rollup_column_id_list_;
  // Column IDs of group by keys.
  GroupingSetIdsInfo grouping_set_ids_info_;

  std::string group_by_hints_;

  std::vector<std::string> order_by_list_;
  std::string order_by_hints_;

  std::string limit_;
  std::string offset_;

  std::string anonymization_options_;
  std::string pivot_;
  std::string unpivot_;
  std::string match_recognize_;

  std::string lock_mode_;

  TargetSyntaxMode target_syntax_mode_ = TargetSyntaxMode::kStandard;

  bool with_recursive_ = false;

  // If true, the group-by clause is "GROUP BY ALL".
  bool group_by_all_ = false;

  // If true, the group-by clause contains only aggregate columns.
  bool group_by_only_aggregate_columns_ = false;
};

// Returns true if the aliases, which are the values of the given map are not
// unique.
template <typename T>
bool HasDuplicateAliases(const absl::flat_hash_map<int, T>& aliases) {
  absl::flat_hash_set<absl::string_view, googlesql_base::StringViewCaseHash,
                      googlesql_base::StringViewCaseEqual>
      seen_aliases;
  for (const auto& [_, alias] : aliases) {
    if (!seen_aliases.insert(alias).second) {
      return true;
    }
  }
  return false;
}

bool StartsWithSelectOrFromOrWith(absl::string_view sql);
bool StartsWithSelectOrFrom(absl::string_view sql);
bool StartsWithWith(absl::string_view sql);

}  // namespace googlesql

#endif  // GOOGLESQL_RESOLVED_AST_QUERY_EXPRESSION_H_
