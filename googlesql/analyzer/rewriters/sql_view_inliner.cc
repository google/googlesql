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

#include "googlesql/analyzer/rewriters/sql_view_inliner.h"

#include <memory>
#include <string>
#include <utility>

#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output_properties.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/rewriter_interface.h"
#include "googlesql/public/sql_view.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/column_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_deep_copy_visitor.h"
#include "googlesql/resolved_ast/resolved_ast_enums.pb.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/rewrite_utils.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace {

// A visitor that replaces calls to SQL view scans with the resolved query.
class SqlViewInlineVistor : public ResolvedASTDeepCopyVisitor {
 public:
  explicit SqlViewInlineVistor(ColumnFactory* column_factory)
      : column_factory_(column_factory) {}

 private:
  ColumnFactory* column_factory_;

  absl::StatusOr<bool> IsScanInlinable(const ResolvedTableScan* scan) {
    const Table* table = scan->table();
    if (table == nullptr || !table->Is<SQLView>()) {
      return false;
    }
    const SQLView* view = table->GetAs<SQLView>();
    if (!view->enable_view_inline()) {
      return false;
    }
    if (view->sql_security() != SQLView::kSecurityInvoker &&
        view->sql_security() != SQLView::kSecurityDefiner) {
      // We make no assumption about unspecified SQL SECURITY. We cannot inline
      // this view invocation.
      return absl::InvalidArgumentError(absl::StrFormat(
          "View inlining not supported for unspecified SQL SECURITY views. "
          "View %s has %s, and the catalog should not report it as "
          "inlineable.",
          view->Name(),
          ResolvedCreateStatementEnums::SqlSecurity_Name(
              view->sql_security())));
    }
    if (scan->hint_list_size() > 0) {
      // View inlining leaves no place to hang table scan hints. It's not clear
      // that inlining a view scan with hints is even the right thing to do.
      return absl::UnimplementedError(
          "Hints are not supported on invocations of inlined views.");
    }
    return true;
  }

  absl::Status VisitResolvedTableScan(const ResolvedTableScan* node) override {
    GOOGLESQL_ASSIGN_OR_RETURN(bool is_inlinable, IsScanInlinable(node));
    if (is_inlinable) {
      return InlineSqlView(node, node->table()->GetAs<SQLView>());
    }
    return CopyVisitResolvedTableScan(node);
  }

  absl::Status InlineSqlView(const ResolvedTableScan* scan,
                             const SQLView* view) {
    GOOGLESQL_RET_CHECK_NE(column_factory_, nullptr);
    ABSL_DCHECK(scan->table()->Is<SQLView>());

    const ResolvedScan* const view_def = view->view_query();
    GOOGLESQL_RET_CHECK_NE(view_def, nullptr);

    // For definer-rights views, we introduce a ResolvedExecuteAsRole node to
    // mark the boundary between invoker and definer rights. In this case,
    // we remap the columns so that consumers of this view call do not reach
    // into its subtree and move things outside of the rights zone.
    if (scan->table()->GetAs<SQLView>()->sql_security() ==
        SQLView::kSecurityDefiner) {
      GOOGLESQL_ASSIGN_OR_RETURN(
          std::unique_ptr<ResolvedScan> view_query,
          ReplaceScanColumns(
              *column_factory_, *view_def, scan->column_index_list(),
              CreateReplacementColumns(*column_factory_, scan->column_list())));

      PushNodeToStack(MakeResolvedExecuteAsRoleScan(
          scan->column_list(), std::move(view_query), scan->table(),
          /*original_inlined_tvf=*/nullptr));
    } else {
      GOOGLESQL_ASSIGN_OR_RETURN(
          std::unique_ptr<ResolvedScan> view_query,
          ReplaceScanColumns(*column_factory_, *view_def,
                             scan->column_index_list(), scan->column_list()));
      PushNodeToStack(std::move(view_query));
    }

    return absl::OkStatus();
  }
};

class SqlViewScanInliner : public Rewriter {
 public:
  absl::StatusOr<std::unique_ptr<const ResolvedNode>> Rewrite(
      const AnalyzerOptions& options, const ResolvedNode& input,
      Catalog& catalog, TypeFactory& type_factory,
      AnalyzerOutputProperties& output_properties) const override {
    GOOGLESQL_RET_CHECK(options.column_id_sequence_number() != nullptr);
    ColumnFactory column_factory(0, options.id_string_pool().get(),
                                 options.column_id_sequence_number());
    SqlViewInlineVistor rewriter(&column_factory);
    GOOGLESQL_RETURN_IF_ERROR(input.Accept(&rewriter));
    return rewriter.ConsumeRootNode<ResolvedNode>();
  }

  std::string Name() const override { return "SqlViewScanInliner"; }
};

}  // namespace

const Rewriter* GetSqlViewInliner() {
  static const auto* const kRewriter = new SqlViewScanInliner;
  return kRewriter;
}

}  // namespace googlesql
