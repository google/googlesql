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

#include "googlesql/resolved_ast/query_expression.h"

#include <cassert>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/analyzer/query_resolver_helper.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "re2/re2.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

using SQLAliasPairList = QueryExpression::SQLAliasPairList;
using TargetSyntaxMode = QueryExpression::TargetSyntaxMode;

absl::StatusOr<QueryExpression::QueryType> QueryExpression::GetQueryType()
    const {
  if (set_op_scan_list_.empty()) {
    GOOGLESQL_RET_CHECK(!select_list_.empty());
    GOOGLESQL_RET_CHECK(set_op_type_.empty());
    GOOGLESQL_RET_CHECK(set_op_column_match_mode_.empty());
    GOOGLESQL_RET_CHECK(set_op_column_propagation_mode_.empty());
    GOOGLESQL_RET_CHECK(with_depth_modifier_.empty());
    GOOGLESQL_RET_CHECK(corresponding_set_op_output_column_list_.empty());
    return QueryExpression::kDefaultQueryType;
  }

  GOOGLESQL_RET_CHECK(select_list_.empty());
  GOOGLESQL_RET_CHECK(!set_op_type_.empty());
  if (set_op_column_match_mode_.empty()) {
    GOOGLESQL_RET_CHECK(corresponding_set_op_output_column_list_.empty());
    return QueryExpression::kPositionalSetOpScan;
  }
  GOOGLESQL_RET_CHECK(!corresponding_set_op_output_column_list_.empty());
  return QueryExpression::kCorrespondenceSetOpScan;
}

// Joins entries present in <list> (with pairs as elements) separated by
// <delimiter>. While appending each pair we add the second element (if present)
// as an alias to the first element.
static std::string JoinListWithAliases(
    absl::Span<const std::pair<std::string, std::string>> list,
    absl::string_view delimiter) {
  std::string list_str;
  bool first = true;
  for (const auto& entry : list) {
    if (!first) absl::StrAppend(&list_str, delimiter);

    if (entry.second.empty()) {
      absl::StrAppend(&list_str, entry.first);
    } else {
      absl::StrAppend(&list_str, entry.first, " AS ", entry.second);
    }
    first = false;
  }
  return list_str;
}

std::string QueryExpression::GetWithClauseSQL() const {
  std::string sql;
  if (!with_list_.empty()) {
    absl::StrAppend(&sql, "WITH ");
    if (with_recursive_) {
      absl::StrAppend(&sql, "RECURSIVE ");
    }
    absl::StrAppend(&sql, JoinListWithAliases(with_list_, ", "), " ");
  }
  return sql;
}

bool QueryExpression::TryAppendSelectClause(std::string& sql) const {
  if (select_list_.empty()) {
    return false;
  }

  ABSL_DCHECK(set_op_type_.empty() && set_op_modifier_.empty() &&
         set_op_scan_list_.empty());
  absl::StrAppend(
      &sql, "SELECT ",
      anonymization_options_.empty()
          ? ""
          : absl::StrCat(anonymization_options_, " "),
      query_hints_.empty() ? "" : absl::StrCat(query_hints_, " "),
      select_as_modifier_.empty() ? "" : absl::StrCat(select_as_modifier_, " "),
      JoinListWithAliases(select_list_, ", "));
  return true;
}

bool QueryExpression::TryAppendSetOpClauses(
    std::string& sql, TargetSyntaxMode target_syntax_mode) const {
  if (set_op_scan_list_.empty()) {
    return false;
  }

  ABSL_DCHECK(!set_op_type_.empty());
  ABSL_DCHECK(!set_op_modifier_.empty());
  ABSL_DCHECK(select_list_.empty());
  ABSL_DCHECK(from_.empty() && where_.empty() && group_by_list_.empty());
  for (int i = 0; i < set_op_scan_list_.size(); ++i) {
    QueryExpression* qe = set_op_scan_list_[i].get();
    if (!select_as_modifier_.empty()) {
      if (qe->select_as_modifier_.empty()) {
        qe->SetSelectAsModifier(select_as_modifier_);
      } else {
        ABSL_DCHECK_EQ(qe->select_as_modifier_, select_as_modifier_);
      }
    }
    if (i > 0) {
      if (set_op_column_propagation_mode_ == "FULL" ||
          set_op_column_propagation_mode_ == "LEFT") {
        absl::StrAppend(&sql, " ", set_op_column_propagation_mode_);
      }
      absl::StrAppend(&sql, " ", set_op_type_);
      if (i == 1) {
        absl::StrAppend(&sql, " ", query_hints_);
      }
      absl::StrAppend(&sql, " ", set_op_modifier_);
      if (set_op_column_propagation_mode_ == "STRICT") {
        absl::StrAppend(&sql, " ", set_op_column_propagation_mode_);
      }
      if (!set_op_column_match_mode_.empty()) {
        absl::StrAppend(&sql, " ", set_op_column_match_mode_);
        if (set_op_column_match_mode_ == "CORRESPONDING BY") {
          absl::StrAppend(
              &sql, " (",
              absl::StrJoin(
                  corresponding_set_op_output_column_list_, ", ",
                  [](std::string* out,
                     const std::pair</*column path*/ std::string,
                                     /*column alias*/ std::string>& column) {
                    absl::StrAppend(out, column.second);
                  }),
              ")");
        }
      }
      if (!with_depth_modifier_.empty()) {
        absl::StrAppend(&sql, " ", with_depth_modifier_);
      }
    }
    absl::StrAppend(&sql, "(", qe->GetSQLQuery(target_syntax_mode), ")");
  }
  return true;
}

std::string QueryExpression::GetFromClauseStandardSQL() const {
  if (from_.empty()) {
    return "";
  }
  return absl::StrCat(" FROM ", from_);
}

std::string QueryExpression::GetFromClausePipeSQL() const {
  if (from_.empty()) {
    return "";
  }

  if (StartsWithWith(from_)) {
    return absl::StrCat("FROM (", from_, ")");
  }

  // TODO: Instead of using string manipulation, capture the
  // information in data structures and use them for manipulation.
  if (StartsWithSelectOrFrom(from_)) {
    return from_;
  }

  return absl::StrCat("FROM ", from_);
}

std::string QueryExpression::GetFromClauseStandardWrappingPipeSQL() const {
  if (from_.empty()) {
    return "";
  }

  // The FROM clause of this point is in Pipe SQL syntax, and the returned
  // FROM clause must be usable in the Standard SQL syntax. So we wrap it.
  // If the FROM clause starts with WITH or SELECT or FROM, we wrap it with
  // FROM(...).
  if (StartsWithSelectOrFromOrWith(from_)) {
    return absl::StrCat(" FROM (", from_, ") ");
  }

  // If the FROM clause is a single word, we prefix it with FROM.
  if (!absl::StrContains(from_, " ")) {
    return absl::StrCat(" FROM ", from_);
  }

  // Else we wrap it with FROM (FROM ...).
  return absl::StrCat(" FROM (FROM ", from_, ") ");
}

bool QueryExpression::TryAppendPivotClause(std::string& sql) const {
  if (pivot_.empty()) {
    return false;
  }

  absl::StrAppend(&sql, pivot_);
  return true;
}

bool QueryExpression::TryAppendUnpivotClause(std::string& sql) const {
  if (unpivot_.empty()) {
    return false;
  }

  absl::StrAppend(&sql, unpivot_);
  return true;
}

bool QueryExpression::TryAppendMatchRecognizeClause(std::string& sql) const {
  if (match_recognize_.empty()) {
    return false;
  }

  absl::StrAppend(&sql, match_recognize_);
  return true;
}

bool QueryExpression::TryAppendWhereClause(std::string& sql) const {
  if (where_.empty()) {
    return false;
  }

  absl::StrAppend(&sql, " WHERE ", where_);
  return true;
}

static void ReplaceGroupingExpressionsWithAliases(
    SQLAliasPairList& aggregate_columns,
    const SQLAliasPairList& group_by_columns) {
  for (const auto& [group_by_column, group_by_alias] : group_by_columns) {
    for (auto& aggregate_column : aggregate_columns) {
      if (aggregate_column.first ==
          absl::StrCat("GROUPING(", group_by_column, ")")) {
        aggregate_column.first = absl::StrCat("GROUPING(", group_by_alias, ")");
      }
    }
  }
}

std::string ConvertToGroupingSetsStr(
    absl::Span<const GroupingSetIds> grouping_set_ids,
    std::function<void(std::string*, const std::vector<int>&)>
        append_column_list) {
  std::vector<std::string> grouping_set_strs;
  for (const GroupingSetIds& grouping_set_ids : grouping_set_ids) {
    std::string grouping_set_str = "";
    if (grouping_set_ids.kind == GroupingSetKind::kGroupingSet) {
      std::vector<int> column_id_list;
      for (const std::vector<int>& multi_column : grouping_set_ids.ids) {
        ABSL_DCHECK_EQ(multi_column.size(), 1);
        column_id_list.push_back(multi_column.front());
      }
      append_column_list(&grouping_set_str, column_id_list);
    } else if (grouping_set_ids.kind == GroupingSetKind::kRollup ||
               grouping_set_ids.kind == GroupingSetKind::kCube) {
      absl::StrAppend(
          &grouping_set_str,
          grouping_set_ids.kind == GroupingSetKind::kRollup ? "ROLLUP" : "CUBE",
          "(");
      std::vector<std::string> multi_column_strs;
      ABSL_DCHECK_GT(grouping_set_ids.ids.size(), 0);
      for (const std::vector<int>& multi_column : grouping_set_ids.ids) {
        ABSL_DCHECK_GT(multi_column.size(), 0);
        std::string multi_column_str = "";
        append_column_list(&multi_column_str, multi_column);
        multi_column_strs.push_back(multi_column_str);
      }
      absl::StrAppend(&grouping_set_str,
                      absl::StrJoin(multi_column_strs, ", "));
      absl::StrAppend(&grouping_set_str, ")");
    }
    grouping_set_strs.push_back(grouping_set_str);
  }
  // Wrap grouping sets strings to GROUPING SETS only when
  // 1. Multiple grouping sets, e.g. GROUPING SETS(x, ROLLUP(y)), OR
  // 2. A grouping set, but its kind is kGroupingSet, e.g. GROUPING SETS(x)
  // Otherwise for simplicity, we generate a top-level ROLLUP or CUBE
  // instead of wrapping it to GROUPING SETS. E.g. We generate ROLLUP(x, y)
  // rather than GROUPING SETS(ROLLUP(x, y)) where there is only a rollup.
  if (grouping_set_strs.size() > 1 ||
      grouping_set_ids.front().kind == GroupingSetKind::kGroupingSet) {
    return absl::StrCat("GROUPING SETS(",
                        absl::StrJoin(grouping_set_strs, ", "), ")");
  }
  return grouping_set_strs.front();
}

// Convert a GroupingSetIdsInfo to sql and append it to `sql`.
void AppendGroupingSetIdsInfoToSql(
    const GroupingSetIdsInfo& grouping_set_ids_info,
    std::function<void(std::string*, const std::vector<int>&)>
        append_column_list,
    std::string& sql) {
  if (grouping_set_ids_info.is_product) {
    for (int i = 0;
         i < grouping_set_ids_info.product_grouping_set_ids_list.size(); ++i) {
      absl::StrAppend(
          &sql, " ",
          ConvertToGroupingSetsStr(
              grouping_set_ids_info.product_grouping_set_ids_list.at(i),
              append_column_list));
      if (i != grouping_set_ids_info.product_grouping_set_ids_list.size() - 1) {
        absl::StrAppend(&sql, ",");
      }
    }
  } else {
    absl::StrAppend(&sql, " ",
                    ConvertToGroupingSetsStr(
                        grouping_set_ids_info.non_product_grouping_set_ids_list,
                        append_column_list));
  }
}

bool QueryExpression::TryAppendGroupByClause(
    std::string& sql, TargetSyntaxMode target_syntax_mode) const {
  // This function generates GROUP BY .. clause in Standard syntax mode
  // and AGGREGATE .. GROUP BY .. clause in Pipe syntax mode.

  // If there are no GROUP BY columns and no AGGREGATE columns, return.
  if (!HasGroupByClauseOrOnlyAggregateColumns()) {
    return false;
  }

  bool pipe_mode = target_syntax_mode == TargetSyntaxMode::kPipe;

  // In Pipe syntax mode, it is possible to have the AGGREGATE ... clause
  // without the GROUP BY .. part. This condition calculates if the GROUP BY ...
  // part is required or not.
  bool group_by_required =
      (group_by_all_ && !pipe_mode) || !group_by_list_.empty();

  bool appended = false;
  if (pipe_mode) {
    // In Pipe syntax mode, the GROUP BY ... part can refer to columns only by
    // aliases, not ordinal.
    // First, we separately get the group-by columns, and aggregate columns
    // along with their aliases.
    auto [group_by_columns, aggregate_columns] =
        GetGroupByAndAggregateColumns();

    // If there are group-by columns, we add them along with their aliases as
    // EXTEND clauses, so that we can refer to those aliases in the GROUP BY ...
    // part later.
    if (!group_by_columns.empty()) {
      absl::StrAppend(&sql, "EXTEND ",
                      JoinListWithAliases(group_by_columns, ", "), kPipe);
      appended = true;
    }

    // If there are aggregate columns, or the GROUP BY ... part is required, we
    // add the AGGREGATE ... part, with the aggregate columns and their aliases.
    if (group_by_required || !aggregate_columns.empty()) {
      ReplaceGroupingExpressionsWithAliases(aggregate_columns,
                                            group_by_columns);
      absl::StrAppend(&sql, "AGGREGATE ",
                      anonymization_options_.empty()
                          ? ""
                          : absl::StrCat(anonymization_options_, " "),
                      JoinListWithAliases(aggregate_columns, ", "),
                      aggregate_columns.empty() ? "" : " ");
      appended = true;
    }
  }

  // If the GROUP BY ... part is not required, we return at this point. The
  // return value indicates if we were able to append anything to the input SQL.
  if (!group_by_required) {
    return appended;
  }

  absl::StrAppend(
      &sql, " GROUP ",
      group_by_hints_.empty() ? "" : absl::StrCat(group_by_hints_, " "), "BY ");

  // In Pipe syntax mode, we must use the group-by column aliases, whereas
  // in Standard syntax mode, we are okay to use the group-by columns directly.
  auto get_column_or_alias = [this, pipe_mode](int column_id) {
    return pipe_mode ? GetGroupByColumnAliasOrDie(column_id)
                     : GetGroupByColumnOrDie(column_id);
  };

  // The Pipe syntax mode does not support GROUP BY ALL.
  if (!pipe_mode && group_by_all_) {
    absl::StrAppend(&sql, "ALL");
  } else if (!group_by_list_.empty()) {
    // Legacy ROLLUP
    if (!rollup_column_id_list_.empty()) {
      absl::StrAppend(
          &sql, "ROLLUP(",
          absl::StrJoin(rollup_column_id_list_, ", ",
                        [get_column_or_alias](std::string* out, int column_id) {
                          absl::StrAppend(out, get_column_or_alias(column_id));
                        }),
          ")");
    } else if (!grouping_set_ids_info_.IsEmpty()) {
      // A lambda expression to output a column list.
      auto append_column_list = [get_column_or_alias](
                                    std::string* output,
                                    const std::vector<int>& column_id_list) {
        if (column_id_list.empty()) {
          absl::StrAppend(output, "()");
          return;
        }
        if (column_id_list.size() > 1) {
          absl::StrAppend(output, "(");
        }
        absl::StrAppend(
            output, absl::StrJoin(
                        column_id_list, ", ",
                        [get_column_or_alias](std::string* out, int column_id) {
                          absl::StrAppend(out, get_column_or_alias(column_id));
                        }));
        if (column_id_list.size() > 1) {
          absl::StrAppend(output, ")");
        }
      };
      AppendGroupingSetIdsInfoToSql(grouping_set_ids_info_, append_column_list,
                                    sql);
    } else {
      // We assume while iterating the group_by_list_, the entries will be
      // sorted by the column id.
      absl::StrAppend(
          &sql, absl::StrJoin(
                    group_by_list_, ", ",
                    [this, pipe_mode](
                        std::string* out,
                        const std::pair<int, std::string>& column_id_and_sql) {
                      auto column_alias_or_sql =
                          pipe_mode ? GetGroupByColumnAliasOrDie(
                                          column_id_and_sql.first)
                                    : column_id_and_sql.second;
                      absl::StrAppend(out, column_alias_or_sql);
                    }));
    }
  }
  return true;
}

bool QueryExpression::TryAppendOrderByClause(std::string& sql) const {
  if (order_by_list_.empty()) {
    return false;
  }

  absl::StrAppend(
      &sql, " ORDER ",
      order_by_hints_.empty() ? "" : absl::StrCat(order_by_hints_, " "), "BY ",
      absl::StrJoin(order_by_list_, ", "));
  return true;
}

bool QueryExpression::TryAppendLimitClause(std::string& sql) const {
  if (limit_.empty()) {
    return false;
  }

  absl::StrAppend(&sql, " LIMIT ", limit_);
  return true;
}

bool QueryExpression::TryAppendOffsetClause(std::string& sql) const {
  if (offset_.empty()) {
    return false;
  }

  absl::StrAppend(&sql, " OFFSET ", offset_);
  return true;
}

bool QueryExpression::TryAppendLockModeClause(std::string& sql) const {
  if (lock_mode_.empty()) {
    return false;
  }

  absl::StrAppend(&sql, " FOR ", lock_mode_);
  return true;
}

std::string QueryExpression::GetSQLQuery(
    TargetSyntaxMode target_syntax_mode) const {
  switch (target_syntax_mode) {
    case TargetSyntaxMode::kStandard: {
      return GetStandardSQLQuery(TargetSyntaxMode::kStandard);
    }
    case TargetSyntaxMode::kPipe: {
      return GetPipeSQLQuery();
    }
  }
}

std::string QueryExpression::GetStandardSQLQuery(
    TargetSyntaxMode from_clause_syntax_mode) const {
  // The order of forming the query in Standard SQL syntax is:
  // WITH
  // SELECT
  // UNION/INTERSECT/EXCEPT
  // FROM
  // PIVOT/UNPIVOT
  // MATCH_RECOGNIZE
  // WHERE
  // GROUP BY
  // ORDER BY
  // LIMIT/OFFSET
  // FOR

  std::string from_clause_sql;
  switch (from_clause_syntax_mode) {
    case TargetSyntaxMode::kStandard: {
      from_clause_sql = GetFromClauseStandardSQL();
      break;
    }
    case TargetSyntaxMode::kPipe: {
      from_clause_sql = GetFromClauseStandardWrappingPipeSQL();
      break;
    }
  }

  std::string sql;
  TryAppendSelectClause(sql);
  TryAppendSetOpClauses(sql, TargetSyntaxMode::kStandard);

  if (!from_clause_sql.empty()) {
    absl::StrAppend(&sql, absl::StartsWith(from_clause_sql, " ") ? "" : " ",
                    from_clause_sql);
  }

  TryAppendPivotClause(sql);
  TryAppendUnpivotClause(sql);
  TryAppendMatchRecognizeClause(sql);
  TryAppendWhereClause(sql);
  TryAppendGroupByClause(sql, TargetSyntaxMode::kStandard);
  TryAppendOrderByClause(sql);
  TryAppendLimitClause(sql);
  TryAppendOffsetClause(sql);
  TryAppendLockModeClause(sql);
  absl::StripTrailingAsciiWhitespace(&sql);

  std::string with_clause_sql = GetWithClauseSQL();
  if (!with_clause_sql.empty()) {
    sql = absl::StrCat(with_clause_sql, sql);
  }
  return sql;
}

// Appends <separator> to <sql> if <append> is true.
static void AppendSeparator(bool append, std::string& sql,
                            absl::string_view separator) {
  if (append) {
    absl::StrAppend(&sql, separator);
  }
}

bool QueryExpression::CanFormPipeSQLQuery() const {
  // If the query has SELECT hints, we cannot form it in Pipe SQL syntax
  // because Pipe SQL syntax does not support SELECT hints.
  if (HasQueryHints()) {
    return false;
  }

  // This query has GROUP BY clause.
  if (HasGroupByClauseOrOnlyAggregateColumns()) {
    // In Pipe SQL syntax, we must refer to group-by columns using their
    // aliases. If we have group-by columns without aliases, we cannot form the
    // query in Pipe SQL syntax.
    if (!AllGroupByColumnsHaveAliases()) {
      return false;
    } else {
      // If all group-by columns have aliases, we still cannot form the query in
      // Pipe SQL syntax if there are columns with duplicate aliases.
      if (!rollup_column_id_list_.empty()) {
        absl::flat_hash_map<int, std::string> aliases;
        int i = 0;
        for (int column_id : rollup_column_id_list_) {
          aliases[i++] = GetGroupByColumnAliasOrDie(column_id);
        }
        if (HasDuplicateAliases(aliases)) {
          return false;
        }
      }

      auto [group_by_columns, _] = GetGroupByAndAggregateColumns();
      if (!group_by_columns.empty()) {
        absl::flat_hash_map<int, absl::string_view> aliases;
        int i = 0;
        for (auto const& entry : group_by_columns) {
          aliases[i++] = entry.second;
        }
        if (HasDuplicateAliases(aliases)) {
          return false;
        }
      }
    }
  }

  return true;
}

std::string QueryExpression::GetPipeSQLQuery() const {
  // If the query cannot be formed in the Pipe SQL syntax, we must fall back
  // to the Standard SQL syntax. However, the FROM clause generated till now
  // will be in the Pipe SQL syntax. So we create a new FROM clauses from it
  // such that it works in the Standard SQL syntax.
  if (!CanFormPipeSQLQuery()) {
    return GetStandardSQLQuery(TargetSyntaxMode::kPipe);
  }

  // The order of forming the query in Pipe SQL syntax is:
  // WITH
  // FROM
  // FOR
  // |> PIVOT/UNPIVOT
  // |> MATCH_RECOGNIZE
  // |> WHERE
  // |> AGGREGATE GROUP BY
  // |> UNION/INTERSECT/EXCEPT
  // |> ORDER BY
  // |> SELECT
  // |> LIMIT/OFFSET

  std::string sql;

  std::string from_clause_sql = GetFromClausePipeSQL();
  if (!from_clause_sql.empty()) {
    absl::StrAppend(&sql, from_clause_sql);
    TryAppendLockModeClause(sql);
    AppendSeparator(true, sql, kPipe);
  }
  AppendSeparator(TryAppendPivotClause(sql), sql, kPipe);
  AppendSeparator(TryAppendUnpivotClause(sql), sql, kPipe);
  AppendSeparator(TryAppendMatchRecognizeClause(sql), sql, kPipe);
  AppendSeparator(TryAppendWhereClause(sql), sql, kPipe);

  bool group_by_clause_added =
      TryAppendGroupByClause(sql, TargetSyntaxMode::kPipe);
  AppendSeparator(group_by_clause_added, sql, kPipe);

  bool set_op_clause_added =
      TryAppendSetOpClauses(sql, TargetSyntaxMode::kPipe);
  AppendSeparator(set_op_clause_added, sql, kPipe);

  AppendSeparator(TryAppendOrderByClause(sql), sql, kPipe);

  // If the GROUP BY clause was added, there is no need to add the SELECT
  // clause because the aggregate columns are already in the GROUP BY clause.
  if (!group_by_clause_added) {
    AppendSeparator(TryAppendSelectClause(sql), sql, kPipe);
  }

  bool limit_clause_added = TryAppendLimitClause(sql);
  // There is no pipe between limit and offset clauses.
  AppendSeparator(limit_clause_added, sql, " ");
  auto offset_clause_added = TryAppendOffsetClause(sql);
  if (limit_clause_added || offset_clause_added) {
    AppendSeparator(true, sql, kPipe);
  }

  // Remove the trailing pipe if the query ends with a pipe.
  if (absl::EndsWith(sql, kPipe)) {
    sql.resize(sql.size() - 4);
  }

  // Prepend the WITH clause to the query.
  std::string with_clause_sql = GetWithClauseSQL();
  if (!with_clause_sql.empty()) {
    sql = absl::StrCat(with_clause_sql, " ", std::move(sql));
  }
  return sql;
}

void QueryExpression::WrapImpl(absl::string_view alias,
                               TargetSyntaxMode subquery_target_syntax_mode,
                               TargetSyntaxMode target_syntax_mode) {
  ABSL_DCHECK(CanFormSQLQuery());
  ABSL_DCHECK(!alias.empty());

  std::string sql;

  switch (subquery_target_syntax_mode) {
    case TargetSyntaxMode::kStandard: {
      sql = GetStandardSQLQuery(target_syntax_mode_);
      break;
    }
    case TargetSyntaxMode::kPipe: {
      sql = GetPipeSQLQuery();
      break;
    }
  }

  ClearAllClauses();

  switch (target_syntax_mode) {
    case TargetSyntaxMode::kStandard:
      from_ = absl::StrCat("(", sql, ") AS ", alias);
      break;
    case TargetSyntaxMode::kPipe: {
      if (absl::StartsWith(sql, "FROM ")) {
        sql.erase(0, 5);
      }
      from_ = absl::StrCat(sql, kPipe, "AS ", alias);
      break;
    }
  }

  target_syntax_mode_ = target_syntax_mode;
}

bool QueryExpression::TrySetWithClause(const SQLAliasPairList& with_list,
                                       bool recursive) {
  if (!CanSetWithClause()) {
    return false;
  }
  with_list_ = with_list;
  with_recursive_ = recursive;
  return true;
}

bool QueryExpression::TrySetSelectClause(const SQLAliasPairList& select_list,
                                         absl::string_view select_hints) {
  if (!CanSetSelectClause()) {
    return false;
  }
  select_list_ = select_list;
  ABSL_DCHECK(query_hints_.empty());
  query_hints_ = select_hints;
  return true;
}

bool QueryExpression::TrySetFromClause(absl::string_view from) {
  if (!CanSetFromClause()) {
    return false;
  }
  from_ = from;
  return true;
}

bool QueryExpression::TrySetWhereClause(absl::string_view where) {
  if (!CanSetWhereClause()) {
    return false;
  }
  where_ = where;
  return true;
}

bool QueryExpression::TrySetSetOpScanList(
    std::vector<std::unique_ptr<QueryExpression>>* set_op_scan_list,
    absl::string_view set_op_type, absl::string_view set_op_modifier,
    absl::string_view set_op_column_match_mode,
    absl::string_view set_op_column_propagation_mode,
    absl::string_view query_hints, absl::string_view with_depth_modifier) {
  if (!CanSetSetOpScanList()) {
    return false;
  }
  ABSL_DCHECK(set_op_scan_list != nullptr);
  set_op_scan_list_ = std::move(*set_op_scan_list);
  set_op_scan_list->clear();
  ABSL_DCHECK(set_op_type_.empty());
  ABSL_DCHECK(set_op_modifier_.empty());
  set_op_type_ = set_op_type;
  set_op_modifier_ = set_op_modifier;
  set_op_column_match_mode_ = set_op_column_match_mode;
  set_op_column_propagation_mode_ = set_op_column_propagation_mode;
  query_hints_ = query_hints;
  with_depth_modifier_ = with_depth_modifier;
  return true;
}

bool QueryExpression::TrySetGroupByClause(
    const std::map<int, std::string>& group_by_list,
    absl::string_view group_by_hints,
    const GroupingSetIdsInfo& grouping_set_ids_info,
    const std::vector<int>& rollup_column_id_list) {
  if (!CanSetGroupByClause()) {
    return false;
  }
  group_by_list_ = group_by_list;
  ABSL_DCHECK(group_by_hints_.empty());
  group_by_hints_ = group_by_hints;
  grouping_set_ids_info_ = grouping_set_ids_info;
  rollup_column_id_list_ = rollup_column_id_list;
  return true;
}

bool QueryExpression::TrySetOrderByClause(
    const std::vector<std::string>& order_by_list,
    absl::string_view order_by_hints) {
  if (!CanSetOrderByClause()) {
    return false;
  }
  order_by_list_ = order_by_list;
  ABSL_DCHECK(order_by_hints_.empty());
  order_by_hints_ = order_by_hints;
  return true;
}

bool QueryExpression::TrySetLimitClause(absl::string_view limit) {
  if (!CanSetLimitClause()) {
    return false;
  }
  limit_ = limit;
  return true;
}

bool QueryExpression::TrySetOffsetClause(absl::string_view offset) {
  if (!CanSetOffsetClause()) {
    return false;
  }
  offset_ = offset;
  return true;
}

bool QueryExpression::TrySetWithAnonymizationClause(
    absl::string_view anonymization_options) {
  if (!CanSetWithAnonymizationClause()) {
    return false;
  }
  anonymization_options_ = anonymization_options;
  return true;
}

bool QueryExpression::TrySetPivotClause(absl::string_view pivot) {
  if (!CanSetPivotClause()) {
    return false;
  }
  pivot_ = pivot;
  return true;
}

bool QueryExpression::TrySetUnpivotClause(absl::string_view unpivot) {
  if (!CanSetUnpivotClause()) {
    return false;
  }
  unpivot_ = unpivot;
  return true;
}

bool QueryExpression::TrySetMatchRecognizeClause(
    absl::string_view match_recognize) {
  if (!CanSetMatchRecognizeClause()) {
    return false;
  }
  match_recognize_ = match_recognize;
  return true;
}

bool QueryExpression::TrySetLockModeClause(absl::string_view lock_mode) {
  if (!CanSetLockModeClause()) {
    return false;
  }
  lock_mode_ = lock_mode;
  return true;
}

bool QueryExpression::CanFormSQLQuery() const { return !CanSetSelectClause(); }

bool QueryExpression::CanSetWithClause() const { return !HasWithClause(); }
bool QueryExpression::CanSetSelectClause() const {
  return !HasSelectClause() && !HasSetOpScanList();
}
bool QueryExpression::CanSetFromClause() const {
  return !HasFromClause() && CanSetSelectClause();
}
bool QueryExpression::CanSetWhereClause() const {
  return !HasWhereClause() && HasFromClause() && CanSetSelectClause();
}
bool QueryExpression::CanSetSetOpScanList() const {
  return !HasSetOpScanList() && !HasSelectClause() && !HasFromClause() &&
         !HasWhereClause() && !HasGroupByClause();
}
bool QueryExpression::CanSetGroupByClause() const {
  return !HasGroupByClause() && HasFromClause() && CanSetSelectClause();
}
bool QueryExpression::CanSetOrderByClause() const {
  return !HasOrderByClause() && !HasLimitClause() && !HasOffsetClause() &&
         HasFromClause();
}
bool QueryExpression::CanSetLimitClause() const {
  return !HasLimitClause() && !HasOffsetClause();
}
bool QueryExpression::CanSetOffsetClause() const { return !HasOffsetClause(); }
bool QueryExpression::CanSetWithAnonymizationClause() const {
  return !HasWithAnonymizationClause();
}
bool QueryExpression::CanSetPivotClause() const {
  return !HasMatchRecognizeClause() && !HasPivotClause() && !HasUnpivotClause();
}
bool QueryExpression::CanSetUnpivotClause() const {
  return !HasMatchRecognizeClause() && !HasPivotClause() && !HasUnpivotClause();
}
bool QueryExpression::CanSetMatchRecognizeClause() const {
  return !HasMatchRecognizeClause() && !HasPivotClause() && !HasUnpivotClause();
}
bool QueryExpression::CanSetLockModeClause() const {
  return HasFromClause() && !HasLockModeClause();
}

const SQLAliasPairList& QueryExpression::SelectList() const {
  if (!set_op_scan_list_.empty()) {
    ABSL_DCHECK(select_list_.empty());
    if (!set_op_column_match_mode_.empty()) {
      return corresponding_set_op_output_column_list_;
    }
    return set_op_scan_list_[0]->SelectList();
  }

  return select_list_;
}

static std::optional<int> GetGroupByColumnOrdinal(
    absl::string_view column_ordinal_or_sql) {
  int ordinal;
  if (absl::SimpleAtoi(column_ordinal_or_sql, &ordinal) && ordinal > 0) {
    return ordinal - 1;
  }
  return std::nullopt;
}

std::string QueryExpression::GetGroupByColumnAliasOrDie(int column_id) const {
  std::string column_ordinal_or_sql = GetGroupByColumnOrDie(column_id);
  std::optional<int> ordinal = GetGroupByColumnOrdinal(column_ordinal_or_sql);
  ABSL_CHECK(ordinal.has_value());  // Crash OK. Follows the same pattern as
                               // GetGroupByColumnOrDie.
  return select_list_.at(*ordinal).second;
}

bool QueryExpression::AllGroupByColumnsHaveAliases() const {
  for (const auto& [column_id, column_sql] : group_by_list_) {
    std::optional<int> ordinal = GetGroupByColumnOrdinal(column_sql);
    if (!ordinal.has_value()) {
      return false;
    }
  }
  return true;
}

std::pair<SQLAliasPairList, SQLAliasPairList>
QueryExpression::GetGroupByAndAggregateColumns() const {
  absl::flat_hash_set<int> alias_column_indices;
  for (const auto& [column_id, column_sql] : group_by_list_) {
    std::optional<int> ordinal = GetGroupByColumnOrdinal(column_sql);
    if (ordinal.has_value()) {
      alias_column_indices.insert(*ordinal);
    }
  }

  SQLAliasPairList aggregate_columns;
  aggregate_columns.reserve(select_list_.size());

  SQLAliasPairList group_by_columns;
  group_by_columns.reserve(select_list_.size());

  for (int i = 0; i < select_list_.size(); ++i) {
    if (alias_column_indices.contains(i)) {
      group_by_columns.push_back(select_list_.at(i));
      continue;
    }

    // Don't include NULL columns in the aggregate columns.
    auto [column, alias] = select_list_.at(i);
    if (column == "NULL") {
      continue;
    }
    aggregate_columns.push_back(select_list_.at(i));
  }

  return {group_by_columns, aggregate_columns};
}

absl::Status QueryExpression::SetAliasesForSelectList(
    const absl::flat_hash_map<int, absl::string_view>& aliases) {
  GOOGLESQL_ASSIGN_OR_RETURN(QueryType type, GetQueryType());
  switch (type) {
    case kDefaultQueryType:
      for (const auto [index, alias] : aliases) {
        GOOGLESQL_RET_CHECK_LT(index, select_list_.size());
        select_list_[index].second = alias;
      }
      break;
    case kPositionalSetOpScan:
      GOOGLESQL_RETURN_IF_ERROR(set_op_scan_list_[0]->SetAliasesForSelectList(aliases));
      break;
    case kCorrespondenceSetOpScan:
      GOOGLESQL_RET_CHECK(!HasDuplicateAliases(aliases));
      absl::flat_hash_map<absl::string_view, absl::string_view>
          old_to_new_alias;
      for (const auto [index, new_alias] : aliases) {
        GOOGLESQL_RET_CHECK_LT(index, corresponding_set_op_output_column_list_.size());
        GOOGLESQL_RET_CHECK(
            old_to_new_alias
                .insert({corresponding_set_op_output_column_list_[index].second,
                         new_alias})
                .second);
      }
      // Recursively set aliases for each set operation item.
      for (int item_index = 0; item_index < set_op_scan_list_.size();
           ++item_index) {
        absl::flat_hash_map<int, absl::string_view> item_aliases;
        for (int col_idx = 0;
             col_idx < set_op_scan_list_[item_index]->SelectList().size();
             ++col_idx) {
          absl::string_view old_alias =
              set_op_scan_list_[item_index]->SelectList()[col_idx].second;
          const auto new_alias = old_to_new_alias.find(old_alias);
          if (new_alias != old_to_new_alias.end()) {
            item_aliases[col_idx] = new_alias->second;
          }
        }
        GOOGLESQL_RETURN_IF_ERROR(set_op_scan_list_[item_index]->SetAliasesForSelectList(
            item_aliases));
      }
      // Update the select list for CORRESPONDING.
      for (const auto [index, new_alias] : aliases) {
        corresponding_set_op_output_column_list_[index].second = new_alias;
      }
      break;
  }
  return absl::OkStatus();
}

void QueryExpression::SetSelectAsModifier(absl::string_view modifier) {
  ABSL_DCHECK(select_as_modifier_.empty());
  select_as_modifier_ = modifier;
}

absl::Status QueryExpression::SetGroupByAllClause(
    const std::map<int, std::string>& group_by_list,
    absl::string_view group_by_hints) {
  GOOGLESQL_RET_CHECK(CanSetGroupByClause());
  group_by_all_ = true;
  group_by_list_ = group_by_list;
  ABSL_DCHECK(group_by_hints_.empty());
  group_by_hints_ = group_by_hints;
  return absl::OkStatus();
}

absl::Status QueryExpression::SetGroupByOnlyAggregateColumns(
    bool group_by_only_aggregate_columns) {
  GOOGLESQL_RET_CHECK(HasSelectClause());
  group_by_only_aggregate_columns_ = true;
  return absl::OkStatus();
}

void QueryExpression::ClearAllClauses() {
  with_list_.clear();
  select_list_.clear();
  select_as_modifier_.clear();
  query_hints_.clear();
  from_.clear();
  where_.clear();
  set_op_type_.clear();
  set_op_modifier_.clear();
  set_op_column_match_mode_.clear();
  set_op_column_propagation_mode_.clear();
  with_depth_modifier_.clear();
  set_op_scan_list_.clear();
  corresponding_set_op_output_column_list_.clear();
  group_by_all_ = false;
  group_by_only_aggregate_columns_ = false;
  group_by_list_.clear();
  group_by_hints_.clear();
  order_by_list_.clear();
  order_by_hints_.clear();
  limit_.clear();
  offset_.clear();
  anonymization_options_.clear();
  with_recursive_ = false;
  pivot_.clear();
  unpivot_.clear();
  match_recognize_.clear();
  lock_mode_.clear();
}

bool StartsWithSelectOrFromOrWith(absl::string_view sql) {
  static const LazyRE2 kRegex = {"^\\s*\\(*\\s*(WITH|SELECT|FROM)\\s+"};
  return RE2::PartialMatch(sql, *kRegex);
}

bool StartsWithSelectOrFrom(absl::string_view sql) {
  static const LazyRE2 kRegex = {"^\\s*\\(*\\s*(SELECT|FROM)\\s+"};
  return RE2::PartialMatch(sql, *kRegex);
}

bool StartsWithWith(absl::string_view sql) {
  static const LazyRE2 kRegex = {"^\\s*\\(*\\s*(WITH)\\s+"};
  return RE2::PartialMatch(sql, *kRegex);
}

}  // namespace googlesql
