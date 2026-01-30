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

#include "googlesql/analyzer/rewriters/insert_dml_values_rewriter.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output_properties.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/rewriter_interface.h"
#include "googlesql/public/sql_view.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/column_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_builder.h"
#include "googlesql/resolved_ast/resolved_ast_deep_copy_visitor.h"
#include "googlesql/resolved_ast/resolved_ast_rewrite_visitor.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "absl/status/statusor.h"
#include "googlesql/base/no_destructor.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_builder.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

class InsertDmlValuesRewriteVisitor : public ResolvedASTRewriteVisitor {
 public:
  explicit InsertDmlValuesRewriteVisitor(ColumnFactory& column_factory)
      : column_factory_(column_factory) {}

 private:
  // This api takes a ResolvedInsertRow of a ResolvedInsertStatement and returns
  // the equivalent query represented by a ResolvedProjectScan.
  // For example, if insert statement is "INSERT INTO T VALUES (1)", the
  // ResolvedInsertRow will have Literals representing value 1. This api will
  // return equivalent query for this row which is "SELECT 1".
  absl::StatusOr<std::unique_ptr<const ResolvedProjectScan>>
  GetRowAsProjectScan(const ResolvedInsertRow* row,
                      const ResolvedInsertStmt* node) {
    std::vector<ResolvedColumn> columns;
    std::vector<std::unique_ptr<const ResolvedComputedColumn>> expr_list;
    std::vector<std::unique_ptr<const ResolvedDMLValue>> values =
        const_cast<ResolvedInsertRow*>(row)->release_value_list();
    for (int i = 0; i < values.size(); ++i) {
      std::unique_ptr<const ResolvedExpr> expr =
          const_cast<ResolvedDMLValue*>(values[i].get())->release_value();
      if (expr->node_kind() == RESOLVED_DMLDEFAULT) {
        // If a column is being inserted with explicit DEFAULT keyword used to
        // specify, the default value of the column, inline the default
        // expression of the column while rewrites.
        const Table* table = node->table_scan()->table();
        const Column* column =
            table->FindColumnByName(node->insert_column_list()[i].name());
        GOOGLESQL_RET_CHECK(column != nullptr);
        GOOGLESQL_RET_CHECK(column->HasDefaultExpression())
            << "No default expression exists for the column specified with "
               "DEFAULT keyword in insert dml";
        // For default expressions we are inlining the default expression
        // owned by the catalog in the insert statement. Hence, creating a copy
        // of the catalog owned expression before inlining.
        GOOGLESQL_ASSIGN_OR_RETURN(expr,
                         ResolvedASTDeepCopyVisitor::Copy(
                             column->GetExpression()->GetResolvedExpression()));
      }
      ResolvedColumn select_column = column_factory_.MakeCol(
          node->table_scan()->table()->Name(), "$col", expr->annotated_type());
      columns.push_back(select_column);
      GOOGLESQL_ASSIGN_OR_RETURN(auto computed_column, ResolvedComputedColumnBuilder()
                                                 .set_column(select_column)
                                                 .set_expr(std::move(expr))
                                                 .Build());
      expr_list.push_back(std::move(computed_column));
    }
    return ResolvedProjectScanBuilder()
        .set_column_list(columns)
        .set_expr_list(std::move(expr_list))
        .set_input_scan(ResolvedSingleRowScanBuilder())
        .Build();
  }

  // This api takes in the resolved insert statement node which represents
  // literals being inserted , for example "INSERT INTO T VALUES (1),(2)" and
  // returns a rewritten resolved insert statement node rewriting the literals
  // to corresponding select... union all queries, for example for above case,
  // "INSERT INTO T SELECT 1 UNION ALL SELECT 2"

  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedInsertStmt(
      std::unique_ptr<const ResolvedInsertStmt> node) override {
    // We takes in the resolved insert statement node which represents
    // literals being inserted , for example "INSERT INTO T VALUES (1),(2)" and
    // return a rewritten resolved insert statement node rewriting the literals
    // to corresponding select... union all queries, for example for above case,
    // "INSERT INTO T SELECT 1 UNION ALL SELECT 2"
    GOOGLESQL_RET_CHECK(!node->row_list().empty());
    ResolvedSetOperationScanBuilder union_all_builder;
    for (const std::unique_ptr<const ResolvedInsertRow>& row :
         node->row_list()) {
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedProjectScan> scan,
                       GetRowAsProjectScan(row.get(), node.get()));
      ResolvedColumnList column_list = scan->column_list();
      union_all_builder.add_input_item_list(
          ResolvedSetOperationItemBuilder()
              .set_output_column_list(std::move(column_list))
              .set_scan(std::move(scan)));
    }

    GOOGLESQL_RET_CHECK(!union_all_builder.input_item_list().empty());

    std::vector<ResolvedColumn> output_columns;
    for (const ResolvedColumn& column :
         union_all_builder.input_item_list()[0]->scan()->column_list()) {
      output_columns.push_back(column_factory_.MakeCol(
          node->table_scan()->table()->Name(), column.name(), column.type()));
    }
    ResolvedInsertStmtBuilder insert_stmt_builder =
        ToBuilder(std::move(node))
            .set_row_list(
                std::vector<std::unique_ptr<const ResolvedInsertRow>>{});

    if (union_all_builder.input_item_list().size() == 1) {
      ResolvedColumnList query_output_column_list =
          union_all_builder.input_item_list()[0]->scan()->column_list();
      return std::move(insert_stmt_builder)
          .set_query(
              ToBuilder(
                  std::move(
                      union_all_builder.release_input_item_list().front()))
                  .release_scan())
          .set_query_output_column_list(std::move(query_output_column_list))
          .Build();
    }

    ResolvedColumnList query_output_column_list(output_columns);
    return std::move(insert_stmt_builder)
        .set_query(
            union_all_builder.set_op_type(ResolvedSetOperationScan::UNION_ALL)
                .set_column_list(std::move(output_columns)))
        .set_query_output_column_list(std::move(query_output_column_list))
        .Build();
  }

  ColumnFactory& column_factory_;
};

class InsertDmlValuesRewriter : public Rewriter {
 public:
  absl::StatusOr<std::unique_ptr<const ResolvedNode>> Rewrite(
      const AnalyzerOptions& options, const ResolvedNode& input,
      Catalog& catalog, TypeFactory& type_factory,
      AnalyzerOutputProperties& output_properties) const override {
    GOOGLESQL_RET_CHECK(options.column_id_sequence_number() != nullptr);
    ColumnFactory column_factory(0, options.id_string_pool().get(),
                                 options.column_id_sequence_number());
    InsertDmlValuesRewriteVisitor rewriter(column_factory);
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedNode> copied_node,
                     ResolvedASTDeepCopyVisitor::Copy(&input));
    return rewriter.VisitAll<ResolvedInsertStmt>(std::move(copied_node));
  }

  std::string Name() const override { return "InsertDmlValuesRewriter"; }
};

const Rewriter* GetInsertDmlValuesRewriter() {
  static const googlesql_base::NoDestructor<InsertDmlValuesRewriter> dmlValuesRewriter;
  return dmlValuesRewriter.get();
}

}  // namespace googlesql
