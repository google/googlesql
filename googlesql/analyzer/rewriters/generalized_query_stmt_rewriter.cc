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

#include "googlesql/analyzer/rewriters/generalized_query_stmt_rewriter.h"

// This rewrites a ResolvedGeneralizedQueryStmt, replacing it with one
// statement node, which is either the Stmt for the terminal operator inside
// (e.g. ResolvedExportDataStmt, see (broken link)), or a
// ResolvedMultiStmt for cases where multiple statements are necessary
// like pipe FORK ((broken link)) or TEE.
//
// This assumes REWRITE_PIPE_IF has already been applied, so handling
// ResolvedPipeIfScan as a possibly-terminal operator isn't necessary.
// This gives an error if ResolvedPipeIfScan occurs.
//
// Most terminal operators can only occur as the outermost scan in a
// ResolvedGeneralizedQueryStmt or ResolvedGeneralizedQuerySubpipeline.
// However, this is not true for pipe TEE, which also returns a table, and
// thus can occur more deeply embedded, requiring the visitor to find it.
//
// For a query like this (using TEE, but FORK is similar, other than not
// returning a final output table):
//
//   <input_query>
//   |> TEE
//        (<subpipeline1>),
//        (<subpipeline2>)
//   |> <pipe_suffix>
//
// We need to make sure to produce statements in the right order so
// CTE definitions come before references and so statements producing query
// results or side-effects happen in the right order (syntax order).
//
// The rewrite visitor works as follows:
//
// 1. It maintains statement_list_stack_, where the top of the stack is
//    the list of additional statements (or nested lists of statements) created
//    for the current terminal operator.
//    - That gets added to while traversing scans under that operator.
//    - When we reach the end of a terminal operator, it adds a final statement
//      if necessary, and then pops the stack, move the current StatementList
//      to be a nested StatementList in the parent entry.
//
//  2. When reaching the end of a ResolvedGeneralizedQueryStmt, it flattens
//     out the list of produced statements, to produce either a single final
//     statement (like a CREATE TABLE), or a list of statements inside a
//     ResolvedMultiStmt (which will use ResolvedCreateWithEntryStmt to make
//     CTEs for intermediate result tables).
//
//  3. For FORK or TEE, there can be multiple child
//     ResolvedGeneralizedQuerySubpipelines.  The visitor converts each of
//     those to a single StatementList entry, so FORK/TEE with N subpipelines
//     will see N of these child entries.  PostVisit for these nodes converts
//     those into a full statement list for the FORK or TEE.
//
//  4. While inside FORK or TEE, subpipeline_context_stack_ is used to track
//     the outermost such node, so we can tell whether ResolvedSubpiplines
//     we encounter are children of those nodes (which should be rewritte)
//     or something else (which should be ignored).
//
//  5. subpipeline_input_scans_stack_ tracks the current ResolvedSubpipeline
//     we are resolving, and the replacement to apply (if any) when
//     we find its ResolvedSubpipelineInputScan.
//
//
// If the query has a ResolvedWithScan, we also need special handling to pivot
// that WITH to inside the new statement.
//
// For example, for a query like
//   WITH w AS (...)
//   FROM ...
//   |> EXPORT DATA ...
//
// we'll have this:
//   GeneralizedStmt(
//     WithScan(                              # The WITH outside the Scan.
//       with_entry_list=...
//       query=
//         PipeExportDataScan(                # EXPORT DATA is a Scan here.
//           export_data_stmt=
//             PipeExportDataStmt(            # With an EXPORT DATA Stmt inside.
//               query=<terminal_input_scan>
//               option_list=...
//               output_column_list=...
//             )
//         )
//     )
//   )
//
// and rewrite it to:
//   ResolvedExportDataStmt(          # Now we have an EXPORT DATA Stmt.
//     query=WithScan(                # The WITH is inside the new Stmt.
//       with_entry_list=...
//       query=<terminal_input_scan>  # The original `query` is now the final
//     )                              # scan inside the added WithScan.
//     option_list=...
//     output_column_list=...
//   )
//
// When we have ResolvedMultiStmt output, we might need to reference the CTE
// from multiple of those statements, so rather than moving it inside just
// one statement, we convert it to one or more ResolvedCreateWithEntryStmts
// at the start of the statement list.

#include <cstddef>
#include <iterator>
#include <memory>
#include <stack>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "googlesql/public/rewriter_interface.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_builder.h"
#include "googlesql/resolved_ast/resolved_ast_rewrite_visitor.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "absl/container/flat_hash_set.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

// A list of elements, where each element can either be a value or a nested
// list.
//
// This is used to collect the side output statements for a pipe operator. It
// is a nested list so that it is easier to group the side output statements
// for each pipe operator.
template <typename T>
class NestedList {
 public:
  using ElementType = std::variant<T, NestedList<T>>;

  NestedList() = default;

  void push_back(ElementType element) {
    elements_.push_back(std::move(element));
  }

  ElementType pop_front() {
    ElementType element = std::move(elements_.front());
    elements_.erase(elements_.begin());
    return element;
  }

  bool empty() const { return elements_.empty(); }

  size_t size() const { return elements_.size(); }

  std::vector<T> flatten() && {
    std::vector<T> flattened;
    std::move(*this).flatten(flattened);
    return flattened;
  }

 private:
  explicit NestedList(std::vector<ElementType>&& elements)
      : elements_(std::move(elements)) {}

  void flatten(std::vector<T>& flattened) && {
    for (ElementType& element : elements_) {
      if (std::holds_alternative<T>(element)) {
        flattened.push_back(std::move(std::get<T>(element)));
      } else {
        std::move(std::get<NestedList<T>>(element)).flatten(flattened);
      }
    }
  }

  std::vector<ElementType> elements_;
};

using StatementList = NestedList<std::unique_ptr<const ResolvedStatement>>;

class GeneralizedQueryStmtRewriteVisitor : public ResolvedASTRewriteVisitor {
 public:
  absl::Status PreVisitResolvedGeneralizedQueryStmt(
      const ResolvedGeneralizedQueryStmt& node) override;

  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedGeneralizedQueryStmt(
      std::unique_ptr<const ResolvedGeneralizedQueryStmt> node) override;

  absl::Status PreVisitResolvedPipeExportDataScan(
      const ResolvedPipeExportDataScan& node) override;

  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedPipeExportDataScan(
      std::unique_ptr<const ResolvedPipeExportDataScan> scan) override;

  absl::Status PreVisitResolvedPipeCreateTableScan(
      const ResolvedPipeCreateTableScan& node) override;

  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedPipeCreateTableScan(
      std::unique_ptr<const ResolvedPipeCreateTableScan> scan) override;

  absl::Status PreVisitResolvedPipeInsertScan(
      const ResolvedPipeInsertScan& node) override;

  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedPipeInsertScan(
      std::unique_ptr<const ResolvedPipeInsertScan> scan) override;

  absl::Status PreVisitResolvedPipeTeeScan(
      const ResolvedPipeTeeScan& node) override;

  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedPipeTeeScan(
      std::unique_ptr<const ResolvedPipeTeeScan> node) override;

  absl::Status PreVisitResolvedPipeForkScan(
      const ResolvedPipeForkScan& node) override;

  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedPipeForkScan(
      std::unique_ptr<const ResolvedPipeForkScan> node) override;

  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedSubpipelineInputScan(
      std::unique_ptr<const ResolvedSubpipelineInputScan> node) override;

  absl::Status PreVisitResolvedGeneralizedQuerySubpipeline(
      const ResolvedGeneralizedQuerySubpipeline& node) override;

  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedGeneralizedQuerySubpipeline(
      std::unique_ptr<const ResolvedGeneralizedQuerySubpipeline> node) override;

  absl::Status PreVisitResolvedSubpipeline(
      const ResolvedSubpipeline& node) override;

  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedPipeIfScan(
      std::unique_ptr<const ResolvedPipeIfScan> scan) override;

  absl::Status CheckFinalState() const {
    GOOGLESQL_RET_CHECK(statement_list_stack_.empty());
    GOOGLESQL_RET_CHECK(subpipeline_context_stack_.empty());
    GOOGLESQL_RET_CHECK(subpipeline_input_scans_stack_.empty());
    return absl::OkStatus();
  }

 private:
  // Shared PreVisit logic for ResolvedPipeTeeScan and ResolvedPipeForkScan.
  template <typename T>
  void SetupForTeeOrFork(absl::string_view query_name, const T& node);

  // Shared PostVisit logic for ResolvedPipeTeeScan and ResolvedPipeForkScan.
  template <typename T>
  absl::Status CollectStatementsForTeeOrFork(absl::string_view query_name,
                                             std::unique_ptr<const T> scan);

  // Scans for some terminal operators (e.g. ResolvedPipeExportDataScan) do not
  // have outputs but the rewriter framework requires that we return a
  // ResolvedNode, so we return a fake scan. It doesn't matter what the fake
  // scan is, because the corresponding `output_schema` is always nullptr and
  // thus the scan will be ignored.
  //
  // We can't return nullptr because the ResolvedAST visitor framework does not
  // allow returning nullptr.
  std::unique_ptr<const ResolvedNode> fake_scan() {
    return MakeResolvedSingleRowScan();
  }

  StatementList& current_statement_list() {
    ABSL_DCHECK(!statement_list_stack_.empty());
    return statement_list_stack_.top();
  }

  // Pops the current statement list and adds it as a nested-list element
  // (not a single statement) to the previous statement list.
  absl::Status PopAndAddToPreviousStatementList() {
    GOOGLESQL_RET_CHECK_GE(statement_list_stack_.size(), 2);
    StatementList statement_list = std::move(current_statement_list());
    statement_list_stack_.pop();
    statement_list_stack_.top().push_back(std::move(statement_list));
    return absl::OkStatus();
  }

  // The top of the stack contains the side output statements for the current
  // node, if the current is inside a statement that can generate side outputs.
  // This can only happen inside a ResolvedGeneralizedQueryStmt, which sets up
  // the initial outermost stack entry.
  //
  // A new list will be pushed onto the stack when we PreVisit a ResolvedNode
  // which can potentially generate side statements, which happens for the
  // terminal operators that can occur inside ResolvedGeneralizedQueryStmt.
  //
  // In PostVisit, the top of the stack contains the list of side outputs
  // produced by that node. The PostVisit method will pop the stack, and add
  // a single entry into the previous StatementList (from its parent node)
  // containing a nested list of statements.
  //
  // Statements like FORK and TEE with multiple subpipelines will see a
  // StatementList containing one nested list of statements for each
  // subpipeline.
  std::stack<StatementList> statement_list_stack_;

  // A counter to generate unique names for WITH entries.
  int num_ctes_ = 0;

  // Context for the subpipelines of TEE and FORK scan.
  // We we encounter a ResolvedSubpipeline, we'll check to see if we have a
  // SubpipelineContext on the stack, and if its `subpipelines_to_rewrite`
  // contains that ResolvedSubpipeline.
  struct SubpipelineContext {
    // The fields needed to make a ResolvedWithRefScan for the subpipeline
    // input scans for TEE or FORK scans.  When visiting their subpipelines, we
    // replace the ResolvedSubpipelineInputScan with that ResolvedWithRefScan.
    ResolvedColumnList with_column_list;
    std::string with_query_name;

    // The subpipelines that are part of the TEE or FORK scan that should be
    // rewritten. This is used to avoid rewriting other subpipelines this
    // rewriter isn't handling.
    absl::flat_hash_set<const ResolvedSubpipeline*> subpipelines_to_rewrite;
  };

  // The stack contains the context for the subpipelines of the current pipe
  // operator (e.g. TEE or FORK) to rewrite.
  std::stack<SubpipelineContext> subpipeline_context_stack_;

  // The top of the stack contains the scan to replace the
  // ResolvedSubpipelineInputScan for the current subpipeline. If the top is
  // nullptr, it means this subpipeline is not being rewritten, and thus the
  // the ResolvedSubpipelineInputScan should not be replaced.
  std::stack<std::unique_ptr<const ResolvedScan>>
      subpipeline_input_scans_stack_;
};

template <typename T>
static absl::StatusOr<std::unique_ptr<const ResolvedStatement>>
AddWithScanToInnerScan(ResolvedWithScanBuilder&& with_builder,
                       std::unique_ptr<const T> stmt) {
  auto builder = ToBuilder(std::move(stmt));
  with_builder.set_column_list(builder.query()->column_list());
  with_builder.set_query(builder.release_query());
  builder.set_query(with_builder);
  return std::move(builder).Build();
}

// This implements pivoting a ResolvedWithScan to the query inside
// a single statement of a supported kind, as described in the comment
// at the top of this file.
static absl::StatusOr<std::unique_ptr<const ResolvedStatement>>
AddWithScanToStmt(ResolvedWithScanBuilder&& with_builder,
                  std::unique_ptr<const ResolvedStatement> stmt) {
  switch (stmt->node_kind()) {
    case RESOLVED_EXPORT_DATA_STMT: {
      return AddWithScanToInnerScan(
          std::move(with_builder),
          GetAsResolvedNode<ResolvedExportDataStmt>(std::move(stmt)));
    }
    case RESOLVED_CREATE_TABLE_AS_SELECT_STMT: {
      return AddWithScanToInnerScan(
          std::move(with_builder),
          GetAsResolvedNode<ResolvedCreateTableAsSelectStmt>(std::move(stmt)));
    }
    case RESOLVED_INSERT_STMT: {
      return AddWithScanToInnerScan(
          std::move(with_builder),
          GetAsResolvedNode<ResolvedInsertStmt>(std::move(stmt)));
    }
    default:
      GOOGLESQL_RET_CHECK_FAIL() << "Unhandled node kind: " << stmt->node_kind_string();
  }
}

absl::Status
GeneralizedQueryStmtRewriteVisitor::PreVisitResolvedGeneralizedQueryStmt(
    const ResolvedGeneralizedQueryStmt& node) {
  statement_list_stack_.emplace();
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<const ResolvedNode>>
GeneralizedQueryStmtRewriteVisitor::PostVisitResolvedGeneralizedQueryStmt(
    std::unique_ptr<const ResolvedGeneralizedQueryStmt> node) {
  GOOGLESQL_RET_CHECK_EQ(statement_list_stack_.size(), 1);
  // This can be empty if we have a ResolvedGeneralizdQueryStmt with no side
  // outputs.  This doesn't normally happen, except for ResolvedSubpipelineStmt,
  // which always gets rewritten to ResolvedGeneralizdQueryStmt because it
  // doesn't track whether the subpipeline had generalized operators or not.
  GOOGLESQL_RET_CHECK_LE(current_statement_list().size(), 1);
  // First collects all the side output statements.
  std::vector<std::unique_ptr<const ResolvedStatement>> statement_list =
      std::move(current_statement_list()).flatten();
  statement_list_stack_.pop();
  auto builder = ToBuilder(std::move(node));

  std::unique_ptr<const ResolvedScan> query = builder.release_query();

  // If the original generalized query had a ResolvedWithScan, then we need to
  // rewrite it when we generate statements.  If we have a single statement,
  // we can just move the ResolvedWithScan to the inner query inside that
  // statement.  Otherwise, we rewrite it into ResolvedCreateWithEntryStmts.
  if (query->Is<ResolvedWithScan>()) {
    ResolvedWithScanBuilder with_builder =
        ToBuilder(GetAsResolvedNode<ResolvedWithScan>(std::move(query)));

    // Special case: When there is only one statement, we can try adding the
    // WITH entries directly to the statement.
    if (statement_list.size() == 1) {
      switch (statement_list[0]->node_kind()) {
        case RESOLVED_EXPORT_DATA_STMT:
        case RESOLVED_CREATE_TABLE_AS_SELECT_STMT:
        case RESOLVED_INSERT_STMT:
          // Terminal operators don't have an output schema.
          GOOGLESQL_RET_CHECK(builder.output_schema() == nullptr);
          return AddWithScanToStmt(std::move(with_builder),
                                   std::move(statement_list[0]));
        default:
          break;
      }
    }

    // General case: Turn the WITH into a sequence of
    // ResolvedCreateWithEntryStmts, and add them to the front of the statement
    // list.
    std::vector<std::unique_ptr<const ResolvedStatement>> with_statement_list;
    for (auto& with_entry : with_builder.release_with_entry_list()) {
      with_statement_list.push_back(
          MakeResolvedCreateWithEntryStmt(std::move(with_entry)));
    }
    GOOGLESQL_RET_CHECK(!with_statement_list.empty());
    statement_list.insert(statement_list.begin(),
                          std::make_move_iterator(with_statement_list.begin()),
                          std::make_move_iterator(with_statement_list.end()));
    query = with_builder.release_query();
  }

  // If there is a main output, add it to the end of the statement list
  // as a ResolvedQueryStmt.
  if (builder.output_schema() != nullptr) {
    ResolvedOutputSchemaBuilder output_schema =
        ToBuilder(builder.release_output_schema());

    statement_list.push_back(MakeResolvedQueryStmt(
        output_schema.release_output_column_list(),
        output_schema.is_value_table(), std::move(query)));
  }

  std::unique_ptr<const ResolvedStatement> new_statement;
  GOOGLESQL_RET_CHECK(!statement_list.empty());
  if (statement_list.size() == 1) {
    new_statement = std::move(statement_list[0]);
  } else {
    new_statement = MakeResolvedMultiStmt(std::move(statement_list));
  }
  // Propagate statement-level hints from the outer ResolvedGeneralizedQueryStmt
  // to the outer ResolvedMultiStmt.
  const_cast<ResolvedStatement*>(new_statement.get())
      ->set_hint_list(builder.release_hint_list());
  return new_statement;
}

absl::Status
GeneralizedQueryStmtRewriteVisitor::PreVisitResolvedPipeExportDataScan(
    const ResolvedPipeExportDataScan& node) {
  statement_list_stack_.emplace();
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<const ResolvedNode>>
GeneralizedQueryStmtRewriteVisitor::PostVisitResolvedPipeExportDataScan(
    std::unique_ptr<const ResolvedPipeExportDataScan> scan) {
  ResolvedPipeExportDataScanBuilder scan_builder = ToBuilder(std::move(scan));

  // EXPORT DATA does not accept any hints in the parser.
  GOOGLESQL_RET_CHECK(scan_builder.hint_list().empty());

  GOOGLESQL_RET_CHECK(scan_builder.export_data_stmt()->query() != nullptr);
  current_statement_list().push_back(scan_builder.release_export_data_stmt());

  GOOGLESQL_RETURN_IF_ERROR(PopAndAddToPreviousStatementList());
  return fake_scan();
}

absl::Status GeneralizedQueryStmtRewriteVisitor::PreVisitResolvedPipeInsertScan(
    const ResolvedPipeInsertScan& node) {
  statement_list_stack_.emplace();
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<const ResolvedNode>>
GeneralizedQueryStmtRewriteVisitor::PostVisitResolvedPipeInsertScan(
    std::unique_ptr<const ResolvedPipeInsertScan> scan) {
  ResolvedPipeInsertScanBuilder scan_builder = ToBuilder(std::move(scan));

  // INSERT does not accept any hints in the parser.
  GOOGLESQL_RET_CHECK(scan_builder.hint_list().empty());

  GOOGLESQL_RET_CHECK(scan_builder.insert_stmt()->query() != nullptr);
  current_statement_list().push_back(scan_builder.release_insert_stmt());

  GOOGLESQL_RETURN_IF_ERROR(PopAndAddToPreviousStatementList());
  return fake_scan();
}

absl::Status
GeneralizedQueryStmtRewriteVisitor::PreVisitResolvedPipeCreateTableScan(
    const ResolvedPipeCreateTableScan& node) {
  statement_list_stack_.emplace();
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<const ResolvedNode>>
GeneralizedQueryStmtRewriteVisitor::PostVisitResolvedPipeCreateTableScan(
    std::unique_ptr<const ResolvedPipeCreateTableScan> scan) {
  ResolvedPipeCreateTableScanBuilder scan_builder = ToBuilder(std::move(scan));

  // CREATE TABLE does not accept any hints in the parser.
  GOOGLESQL_RET_CHECK(scan_builder.hint_list().empty());

  GOOGLESQL_RET_CHECK(scan_builder.create_table_as_select_stmt()->query() != nullptr);
  current_statement_list().push_back(
      scan_builder.release_create_table_as_select_stmt());

  GOOGLESQL_RETURN_IF_ERROR(PopAndAddToPreviousStatementList());
  return fake_scan();
}

template <typename T>
void GeneralizedQueryStmtRewriteVisitor::SetupForTeeOrFork(
    absl::string_view query_name, const T& node) {
  // Pipe FORK and TEE scans can generate side outputs, so we push a new side
  // statement list onto the stack.
  statement_list_stack_.emplace();

  absl::flat_hash_set<const ResolvedSubpipeline*> subpipelines_to_rewrite;
  for (const auto& subpipeline : node.subpipeline_list()) {
    subpipelines_to_rewrite.insert(subpipeline->subpipeline());
  }

  // Store the information needed to rewrite the subpipelines under this node.
  subpipeline_context_stack_.push({
      // The subpipeline input scan for a TEE or FORK scan is always a WITH
      // reference scan, constructed using these fields.
      .with_column_list = node.input_scan()->column_list(),
      .with_query_name = std::string(query_name),

      // Only rewrite these subpipelines, which are the direct children of this
      // FORK or TEE, not other subpipelines.
      .subpipelines_to_rewrite = std::move(subpipelines_to_rewrite),
  });
}

template <typename T>
absl::Status GeneralizedQueryStmtRewriteVisitor::CollectStatementsForTeeOrFork(
    absl::string_view query_name, std::unique_ptr<const T> scan) {
  auto builder = ToBuilder(std::move(scan));
  ResolvedColumnList input_column_list = builder.input_scan()->column_list();

  // `current_statement_list()` contains all the side output statements
  // collected from `input_scan` and all subpipelines.
  StatementList collected_statements = std::move(current_statement_list());

  // The final statement list `all_statements` will contain the following
  // statements in order:
  //
  // 1. The side output statements for the input_scan.
  // 2. The WITH entry for the input_scan.
  // 3. For each subpipeline,
  //    a. The side output statements for the subpipeline.
  //    b. The main output statement for the subpipeline.
  StatementList all_statements;

  if (collected_statements.size() == builder.subpipeline_list().size() + 1) {
    // The input_scan also outputs side statements. Add them.
    all_statements.push_back(collected_statements.pop_front());
  }
  GOOGLESQL_RET_CHECK_EQ(collected_statements.size(), builder.subpipeline_list().size());

  // Add ResolvedCreateWithEntryStmt for the input_scan.
  all_statements.push_back(MakeResolvedCreateWithEntryStmt(
      MakeResolvedWithEntry(query_name, builder.release_input_scan())));

  // Side and main outputs for each subpipeline.
  for (auto& subpipeline : builder.release_subpipeline_list()) {
    // Side statements come before the main output of the subpipeline.
    all_statements.push_back(collected_statements.pop_front());

    auto subpipeline_builder = ToBuilder(std::move(subpipeline));
    if (subpipeline_builder.output_schema() == nullptr) {
      // This subpipeline does not have any main outputs.
      continue;
    }

    // Add a ResolvedQueryStmt for the main output of the subpipeline if it
    // has one.
    auto output_schema = ToBuilder(subpipeline_builder.release_output_schema());
    all_statements.push_back(MakeResolvedQueryStmt(
        output_schema.release_output_column_list(),
        output_schema.is_value_table(),
        ToBuilder(subpipeline_builder.release_subpipeline()).release_scan()));
  }
  GOOGLESQL_RET_CHECK(collected_statements.empty());

  // All side output statements for the TEE or FORK scan are collected.
  // Add them to the previous statement list.
  statement_list_stack_.pop();
  GOOGLESQL_RET_CHECK(!statement_list_stack_.empty());
  statement_list_stack_.top().push_back(std::move(all_statements));
  return absl::OkStatus();
}

absl::Status GeneralizedQueryStmtRewriteVisitor::PreVisitResolvedPipeTeeScan(
    const ResolvedPipeTeeScan& node) {
  std::string query_name = absl::StrCat("$tee_cte_", ++num_ctes_);
  SetupForTeeOrFork(query_name, node);
  return absl::OkStatus();
}

absl::Status GeneralizedQueryStmtRewriteVisitor::PreVisitResolvedPipeForkScan(
    const ResolvedPipeForkScan& node) {
  std::string query_name = absl::StrCat("$fork_cte_", ++num_ctes_);
  SetupForTeeOrFork(query_name, node);
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<const ResolvedNode>>
GeneralizedQueryStmtRewriteVisitor::PostVisitResolvedPipeTeeScan(
    std::unique_ptr<const ResolvedPipeTeeScan> node) {
  GOOGLESQL_RET_CHECK(!subpipeline_context_stack_.empty());
  std::string query_name = subpipeline_context_stack_.top().with_query_name;

  ResolvedColumnList input_column_list = node->input_scan()->column_list();
  GOOGLESQL_RETURN_IF_ERROR(CollectStatementsForTeeOrFork(query_name, std::move(node)));
  subpipeline_context_stack_.pop();
  return MakeResolvedWithRefScan(input_column_list, query_name);
}

absl::StatusOr<std::unique_ptr<const ResolvedNode>>
GeneralizedQueryStmtRewriteVisitor::PostVisitResolvedPipeForkScan(
    std::unique_ptr<const ResolvedPipeForkScan> node) {
  GOOGLESQL_RET_CHECK(!subpipeline_context_stack_.empty());
  std::string query_name = subpipeline_context_stack_.top().with_query_name;
  GOOGLESQL_RETURN_IF_ERROR(CollectStatementsForTeeOrFork(query_name, std::move(node)));
  subpipeline_context_stack_.pop();
  return fake_scan();
}

// Every time we see a ResolvedGeneralizedQuerySubpipeline, we generate a
// StatementList for it, and then add that into the parent's StatementList
// as one nested StatementList with statements for this subpipeline.
// Operators like FORK and TEE can have multiple of these subpipelines.
absl::Status
GeneralizedQueryStmtRewriteVisitor::PreVisitResolvedGeneralizedQuerySubpipeline(
    const ResolvedGeneralizedQuerySubpipeline& node) {
  statement_list_stack_.emplace();
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<const ResolvedNode>>
GeneralizedQueryStmtRewriteVisitor::
    PostVisitResolvedGeneralizedQuerySubpipeline(
        std::unique_ptr<const ResolvedGeneralizedQuerySubpipeline> node) {
  GOOGLESQL_RETURN_IF_ERROR(PopAndAddToPreviousStatementList());
  return std::move(node);
}

// When visiting a subpipeline, we might be doing a similar expansion to inline
// it as rewrite_subpipeline.h, replacing its ResolvedSubpipelineInputScan.
// We need to duplicate that here as part of this larger visitor.
absl::Status GeneralizedQueryStmtRewriteVisitor::PreVisitResolvedSubpipeline(
    const ResolvedSubpipeline& node) {
  if (!subpipeline_context_stack_.empty() &&
      subpipeline_context_stack_.top().subpipelines_to_rewrite.contains(
          &node)) {
    // We're rewriting this ResolvedSubpipeline.  When we find the
    // ResolvedSubpipelineInputScan for it, we'll replace it with this
    // ResolvedWithRefScan.
    subpipeline_input_scans_stack_.push(MakeResolvedWithRefScan(
        subpipeline_context_stack_.top().with_column_list,
        subpipeline_context_stack_.top().with_query_name));
  } else {
    // This is a ResolvedSubpipeline that we aren't rewriting.  When we find the
    // ResolvedSubpipelineInputScan for it, we'll just leave it.
    subpipeline_input_scans_stack_.push(nullptr);
  }
  // We expect to find exactly one ResolvedSubpipelineInputScan under this node
  // if this is a valid resolved AST, and it will be consumed from
  // subpipeline_input_scans_stack_.  If we don't find exactly one, we'll
  // GOOGLESQL_RET_CHECK either in PostVisitResolvedSubpipelineInputScan or
  // CheckFinalState.
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<const ResolvedNode>>
GeneralizedQueryStmtRewriteVisitor::PostVisitResolvedSubpipelineInputScan(
    std::unique_ptr<const ResolvedSubpipelineInputScan> node) {
  GOOGLESQL_RET_CHECK(!subpipeline_input_scans_stack_.empty());
  std::unique_ptr<const ResolvedScan> subpipeline_input_scan =
      std::move(subpipeline_input_scans_stack_.top());
  subpipeline_input_scans_stack_.pop();
  if (subpipeline_input_scan == nullptr) {
    return std::move(node);
  }
  return subpipeline_input_scan;
}

absl::StatusOr<std::unique_ptr<const ResolvedNode>>
GeneralizedQueryStmtRewriteVisitor::PostVisitResolvedPipeIfScan(
    std::unique_ptr<const ResolvedPipeIfScan> scan) {
  GOOGLESQL_RET_CHECK_FAIL() << "REWRITE_PIPE_IF needs to be applied before "
                      "REWRITE_GENERALIZED_QUERY_STMT";
}

class VisitorBasedGeneralizedQueryStmtRewriter : public Rewriter {
 public:
  absl::StatusOr<std::unique_ptr<const ResolvedNode>> Rewrite(
      const AnalyzerOptions& options, std::unique_ptr<const ResolvedNode> input,
      Catalog& catalog, TypeFactory& type_factory,
      AnalyzerOutputProperties& output_properties) const override {
    auto visitor = GeneralizedQueryStmtRewriteVisitor();
    GOOGLESQL_ASSIGN_OR_RETURN(auto rewritten, visitor.VisitAll(std::move(input)));
    GOOGLESQL_RETURN_IF_ERROR(visitor.CheckFinalState());
    return rewritten;
  }

  std::string Name() const override { return "GeneralizedQueryStmtRewriter"; }
};

const Rewriter* GetGeneralizedQueryStmtRewriter() {
  static const auto* const kRewriter =
      new VisitorBasedGeneralizedQueryStmtRewriter();
  return kRewriter;
}

}  // namespace googlesql
