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

#include "googlesql/analyzer/rewriters/like_any_all_rewriter.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/analyzer/substitute.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output_properties.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/rewriter_interface.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/column_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_deep_copy_visitor.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/rewrite_utils.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace {

// Contains helper functions for building the ResolvedAST for a
// 'search_value LIKE {{ANY|ALL}} <subquery>' expression.
// This builder is responsible for constructing the AST that corresponds to the
// conceptual SQL transformation, turning the expression into a scalar subquery
// that uses aggregation and a CASE statement to handle all semantics correctly.
class LikeAnyAllSubqueryScanBuilder {
 public:
  LikeAnyAllSubqueryScanBuilder(const AnalyzerOptions* analyzer_options,
                                Catalog* catalog, ColumnFactory* column_factory,
                                TypeFactory* type_factory)
      : analyzer_options_(analyzer_options),
        catalog_(catalog),
        fn_builder_(*analyzer_options, *catalog, *type_factory),
        column_factory_(column_factory) {}

 private:
  // Builds the AggregateScan of the ResolvedAST for a
  // <input> LIKE {{ANY|ALL}} <subquery>
  // expression as detailed at (broken link)
  // Maps to:
  // AggregateScan
  //   +-input_scan=SubqueryScan  // User input subquery
  //     +-pattern_col#2=subquery_column
  //   +-like_agg_col#3=AggregateFunctionCall(
  //         LOGICAL_OR/AND(input_expr#1 LIKE pattern_col#2) -> BOOL)
  //           // OR for ANY, AND for ALL
  //   +-null_agg_col#4=AggregateFunctionCall(
  //         LOGICAL_OR(pattern_col#2 IS NULL) -> BOOL)
  // in the ResolvedAST
  absl::StatusOr<std::unique_ptr<ResolvedAggregateScan>> BuildAggregateScan(
      const ResolvedColumn& input_column, const ResolvedColumn& subquery_column,
      std::unique_ptr<const ResolvedScan> input_scan,
      ResolvedSubqueryExpr::SubqueryType subquery_type);

  // Constructs a ResolvedAggregateFunctionCall for a LOGICAL_OR/AND function
  // for use in the LIKE ANY/ALL rewriter
  //
  // The signature for the built-in function "logical_or" or "logical_and" must
  // be available in <catalog> or an error status is returned
  absl::StatusOr<std::unique_ptr<const ResolvedAggregateFunctionCall>>
  AggregateLogicalOperation(FunctionSignatureId context_id,
                            std::unique_ptr<const ResolvedExpr> expression);

  const AnalyzerOptions* analyzer_options_;
  Catalog* catalog_;
  FunctionCallBuilder fn_builder_;
  ColumnFactory* column_factory_;
};

absl::StatusOr<std::unique_ptr<ResolvedAggregateScan>>
LikeAnyAllSubqueryScanBuilder::BuildAggregateScan(
    const ResolvedColumn& input_column, const ResolvedColumn& subquery_column,
    std::unique_ptr<const ResolvedScan> input_scan,
    ResolvedSubqueryExpr::SubqueryType subquery_type) {
  std::vector<std::unique_ptr<const ResolvedComputedColumn>> aggregate_list;
  std::vector<ResolvedColumn> column_list;

  // Create a LOGICAL_OR/AND(input LIKE pattern) function using ColumnRefs to
  // the input and subquery columns, and add it as a column to the
  // AggregateScan.
  // Maps to:
  // +-like_agg_col#3=AggregateFunctionCall(
  //       LOGICAL_OR/AND(input_expr#1 LIKE pattern_col#2) -> BOOL)
  //         // OR for ANY, AND for ALL
  // in the ResolvedAST.
  std::unique_ptr<ResolvedColumnRef> like_input_column_ref =
      MakeResolvedColumnRef(input_column,
                            /*is_correlated=*/true);
  std::unique_ptr<ResolvedColumnRef> subquery_column_ref_like =
      MakeResolvedColumnRef(subquery_column,
                            /*is_correlated=*/false);
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> like_fn,
                   fn_builder_.Like(std::move(like_input_column_ref),
                                    std::move(subquery_column_ref_like)));
  FunctionSignatureId context_id;
  std::string column_name;
  if (subquery_type == ResolvedSubqueryExpr::LIKE_ANY) {
    context_id = FN_LOGICAL_OR;
    column_name = "like_any";
  } else if (subquery_type == ResolvedSubqueryExpr::LIKE_ALL) {
    context_id = FN_LOGICAL_AND;
    column_name = "like_all";
  } else {
    GOOGLESQL_RET_CHECK_FAIL()
        << "Subquery type can only be LIKE_ANY or LIKE_ALL. Subquery type: "
        << ResolvedSubqueryExprEnums_SubqueryType_Name(subquery_type);
  }
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedAggregateFunctionCall>
                       logical_operation_like_fn,
                   AggregateLogicalOperation(context_id, std::move(like_fn)));
  ResolvedColumn like_column =
      column_factory_->MakeCol("aggregate", column_name, types::BoolType());
  std::unique_ptr<ResolvedComputedColumn> like_computed_column =
      MakeResolvedComputedColumn(like_column,
                                 std::move(logical_operation_like_fn));
  aggregate_list.push_back(std::move(like_computed_column));
  column_list.push_back(like_column);

  // Create a LOGICAL_OR(pattern IS NULL) function using ColumnRefs to the
  // subquery column, and add it as a column to the AggregateScan.
  // Maps to:
  // +-null_agg_col#4=AggregateFunctionCall(
  //       LOGICAL_OR(pattern_col#2 IS NULL) -> BOOL)
  // in the ResolvedAST.
  std::unique_ptr<ResolvedColumnRef> subquery_column_ref_contains_null =
      MakeResolvedColumnRef(subquery_column,
                            /*is_correlated=*/false);
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<const ResolvedExpr> is_null_fn,
      fn_builder_.IsNull(std::move(subquery_column_ref_contains_null)));
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<const ResolvedAggregateFunctionCall> contains_null_fn,
      AggregateLogicalOperation(FN_LOGICAL_OR, std::move(is_null_fn)));
  ResolvedColumn contains_null_column = column_factory_->MakeCol(
      "aggregate", "has_null_pattern", types::BoolType());
  std::unique_ptr<ResolvedComputedColumn> contains_null_computed_column =
      MakeResolvedComputedColumn(contains_null_column,
                                 std::move(contains_null_fn));
  aggregate_list.push_back(std::move(contains_null_computed_column));
  column_list.push_back(contains_null_column);

  // Maps to:
  // AggregateScan
  //   +-input_scan=SubqueryScan  // User input subquery
  //     +-pattern_col#2=subquery_column
  //   +-like_agg_col#3=AggregateFunctionCall(
  //         LOGICAL_OR/AND(input_expr#1 LIKE pattern_col#2) -> BOOL)
  //           // OR for ANY, AND for ALL
  //   +-null_agg_col#4=AggregateFunctionCall(
  //         LOGICAL_OR(pattern_col#2 IS NULL) -> BOOL)
  // in the ResolvedAST
  return MakeResolvedAggregateScan(column_list, std::move(input_scan),
                                   /*group_by_list=*/{},
                                   std::move(aggregate_list),
                                   /*grouping_set_list=*/{},
                                   /*rollup_column_list=*/{},
                                   /*grouping_call_list=*/{});
}

absl::StatusOr<std::unique_ptr<const ResolvedAggregateFunctionCall>>
LikeAnyAllSubqueryScanBuilder::AggregateLogicalOperation(
    FunctionSignatureId context_id,
    std::unique_ptr<const ResolvedExpr> expression) {
  GOOGLESQL_RET_CHECK_EQ(expression->type(), types::BoolType());

  std::string logical_fn;
  if (context_id == FN_LOGICAL_OR) {
    logical_fn = "logical_or";
  } else if (context_id == FN_LOGICAL_AND) {
    logical_fn = "logical_and";
  } else {
    GOOGLESQL_RET_CHECK_FAIL() << "Function context_id did not match LOGICAL_OR or "
                        "LOGICAL_AND. context_id: "
                     << FunctionSignatureId_Name(context_id);
  }

  const Function* logical_operation_fn;
  GOOGLESQL_RET_CHECK_OK(catalog_->FindFunction({logical_fn}, &logical_operation_fn,
                                      analyzer_options_->find_options()))
      << "Engine does not support " << logical_fn << " function";
  GOOGLESQL_RET_CHECK(logical_operation_fn->IsGoogleSQLBuiltin());
  GOOGLESQL_RET_CHECK_NE(logical_operation_fn, nullptr);

  FunctionSignature logical_operation_signature(
      {types::BoolType(), 1}, {{types::BoolType(), 1}}, context_id);
  std::vector<std::unique_ptr<const ResolvedExpr>> logical_operation_args;
  logical_operation_args.push_back(std::move(expression));

  return MakeResolvedAggregateFunctionCall(
      types::BoolType(), logical_operation_fn, logical_operation_signature,
      std::move(logical_operation_args),
      ResolvedFunctionCallBaseEnums::DEFAULT_ERROR_MODE, /*distinct=*/false,
      ResolvedNonScalarFunctionCallBaseEnums::DEFAULT_NULL_HANDLING,
      /*having_modifier=*/nullptr, /*order_by_item_list=*/{},
      /*limit=*/nullptr);
}

// Template for rewriting LIKE ANY with null handling for cases:
//   SELECT <input> LIKE ANY UNNEST([]) -> FALSE
//   SELECT NULL LIKE ANY {{UNNEST(<patterns>)|(<patterns>)}} -> NULL
//   SELECT <input> LIKE ANY {{UNNEST([NULL, ...])|(NULL, ...)}}
//     -> TRUE (if other matches present), NULL (if no other matches
//     present)
// Arguments:
//   input - STRING or BYTES
//   patterns - ARRAY<STRING> or ARRAY<BYTES>
// Returns: BOOL
// Semantic Rules:
//   If patterns is empty or NULL, return FALSE
//   If input is NULL, return NULL
//   If LOGICAL_OR(input LIKE pattern) is TRUE, return TRUE
//   If patterns contains any NULL values, return NULL
//   Otherwise, return FALSE
constexpr absl::string_view kLikeAnyTemplate = R"(
(SELECT
  CASE
    WHEN patterns IS NULL OR ARRAY_LENGTH(patterns) = 0 THEN FALSE
    WHEN input IS NULL THEN NULL
    WHEN LOGICAL_OR(input LIKE pattern) THEN TRUE
    WHEN LOGICAL_OR(pattern IS NULL) THEN NULL
    ELSE FALSE
  END
FROM UNNEST(patterns) as pattern)
)";

// Template for rewriting NOT LIKE ANY
// This template is used for a newer implementation of LIKE ANY with NOT
// operator. Details in (broken link)
// Arguments:
//   input - STRING or BYTES
//   patterns - ARRAY<STRING> or ARRAY<BYTES>
// Returns: BOOL
// Semantic Rules:
//   If patterns is empty or NULL, return FALSE
//   If input is NULL, return NULL
//   If LOGICAL_OR(input NOT LIKE pattern) is TRUE, return TRUE
//   If patterns contains any NULL values, return NULL
//   Otherwise, return FALSE
constexpr absl::string_view kNotLikeAnyTemplate = R"(
(SELECT
  CASE
    WHEN patterns IS NULL OR ARRAY_LENGTH(patterns) = 0 THEN FALSE
    WHEN input IS NULL THEN NULL
    WHEN LOGICAL_OR(input NOT LIKE pattern) THEN TRUE
    WHEN LOGICAL_OR(pattern IS NULL) THEN NULL
    ELSE FALSE
  END
FROM UNNEST(patterns) as pattern)
)";

// Template for rewriting LIKE ALL with null handling for cases:
//   SELECT <input> LIKE ALL UNNEST([]) -> TRUE
//   SELECT NULL LIKE ALL {{UNNEST(<patterns>)|(<patterns>)}} -> NULL
//   SELECT <input> LIKE ALL {{UNNEST([NULL, ...])|(NULL, ...)}} -> NULL
// Arguments:
//   input - STRING or BYTES
//   patterns - ARRAY<STRING> or ARRAY<BYTES>
// Returns: BOOL
// Semantic Rules:
//   If patterns is empty or NULL, return TRUE
//   If input is NULL, return NULL
//   If LOGICAL_AND(input LIKE pattern) is FALSE, return FALSE
//   If patterns contains any NULL values, return NULL
//   Otherwise, return TRUE
constexpr absl::string_view kLikeAllTemplate = R"(
(SELECT
  CASE
    WHEN patterns IS NULL OR ARRAY_LENGTH(patterns) = 0 THEN TRUE
    WHEN input IS NULL THEN NULL
    WHEN NOT LOGICAL_AND(input LIKE pattern) THEN FALSE
    WHEN LOGICAL_OR(pattern IS NULL) THEN NULL
    ELSE TRUE
  END
  FROM UNNEST(patterns) as pattern)
)";

// Template for rewriting NOT LIKE ALL
// This template is used for a newer implementation of LIKE ALL with NOT
// operator. Details in (broken link)
// Arguments:
//   input - STRING or BYTES
//   patterns - ARRAY<STRING> or ARRAY<BYTES>
// Returns: BOOL
// Semantic Rules:
//   If patterns is empty or NULL, return TRUE
//   If input is NULL, return NULL
//   If input not like pattern is FALSE, return FALSE
//   If patterns contains any NULL values, return NULL
//   Otherwise, return TRUE
constexpr absl::string_view kNotLikeAllTemplate = R"(
(SELECT
  CASE
    WHEN patterns IS NULL OR ARRAY_LENGTH(patterns) = 0 THEN TRUE
    WHEN input IS NULL THEN NULL
    WHEN NOT LOGICAL_AND(input NOT LIKE pattern) THEN FALSE
    WHEN LOGICAL_OR(pattern IS NULL) THEN NULL
    ELSE TRUE
  END
  FROM UNNEST(patterns) as pattern)
)";

class LikeAnyAllRewriteVisitor : public ResolvedASTDeepCopyVisitor {
 public:
  LikeAnyAllRewriteVisitor(const AnalyzerOptions* analyzer_options,
                           Catalog* catalog, TypeFactory* type_factory)
      : analyzer_options_(analyzer_options),
        catalog_(catalog),
        fn_builder_(*analyzer_options, *catalog, *type_factory),
        type_factory_(type_factory) {}

 private:
  absl::Status VisitResolvedFunctionCall(
      const ResolvedFunctionCall* node) override;

  // Rewrites a function of the form:
  //   input [NOT] LIKE {{ANY|ALL}} (pattern1, [...])
  // to use the LOGICAL_OR aggregation function with the LIKE operator
  absl::Status RewriteLikeAnyAll(const ResolvedFunctionCall* node,
                                 absl::string_view rewrite_template);

  // Rewrites a function of the form:
  //   input [NOT] LIKE {{ANY|ALL}} UNNEST(<array-expression>)
  // to use the LOGICAL_OR aggregation function with the LIKE operator
  absl::Status RewriteLikeAnyAllArray(const ResolvedFunctionCall* node,
                                      absl::string_view rewrite_template);

  absl::Status RewriteLikeAnyAllArrayWithAggregate(
      std::unique_ptr<const ResolvedExpr> input_expr,
      std::unique_ptr<const ResolvedExpr> patterns_array_expr,
      absl::string_view template_string);

  const AnalyzerOptions* analyzer_options_;
  Catalog* catalog_;
  FunctionCallBuilder fn_builder_;
  TypeFactory* type_factory_;
};

struct LikeAnyAllRewriterConfig {
  enum RewriterVariant { kLikeAnyAll, kLikeAnyAllArray };

  RewriterVariant rewriter_variant;
  const absl::string_view rewrite_template;
};

static bool IsLikeAnyFunctionNode(const ResolvedFunctionCall* node) {
  return IsBuiltInFunctionIdEq(node, FN_STRING_LIKE_ANY) ||
         IsBuiltInFunctionIdEq(node, FN_BYTE_LIKE_ANY);
}

static bool IsNotLikeAnyFunctionNode(const ResolvedFunctionCall* node) {
  return IsBuiltInFunctionIdEq(node, FN_STRING_NOT_LIKE_ANY) ||
         IsBuiltInFunctionIdEq(node, FN_BYTE_NOT_LIKE_ANY);
}

static bool IsLikeAllFunctionNode(const ResolvedFunctionCall* node) {
  return IsBuiltInFunctionIdEq(node, FN_STRING_LIKE_ALL) ||
         IsBuiltInFunctionIdEq(node, FN_BYTE_LIKE_ALL);
}

static bool IsNotLikeAllFunctionNode(const ResolvedFunctionCall* node) {
  return IsBuiltInFunctionIdEq(node, FN_STRING_NOT_LIKE_ALL) ||
         IsBuiltInFunctionIdEq(node, FN_BYTE_NOT_LIKE_ALL);
}

static bool IsLikeAnyArrayFunctionNode(const ResolvedFunctionCall* node) {
  return IsBuiltInFunctionIdEq(node, FN_STRING_ARRAY_LIKE_ANY) ||
         IsBuiltInFunctionIdEq(node, FN_BYTE_ARRAY_LIKE_ANY);
}

static bool IsNotLikeAnyArrayFunctionNode(const ResolvedFunctionCall* node) {
  return IsBuiltInFunctionIdEq(node, FN_STRING_ARRAY_NOT_LIKE_ANY) ||
         IsBuiltInFunctionIdEq(node, FN_BYTE_ARRAY_NOT_LIKE_ANY);
}

static bool IsLikeAllArrayFunctionNode(const ResolvedFunctionCall* node) {
  return IsBuiltInFunctionIdEq(node, FN_STRING_ARRAY_LIKE_ALL) ||
         IsBuiltInFunctionIdEq(node, FN_BYTE_ARRAY_LIKE_ALL);
}

static bool IsNotLikeAllArrayFunctionNode(const ResolvedFunctionCall* node) {
  return IsBuiltInFunctionIdEq(node, FN_STRING_ARRAY_NOT_LIKE_ALL) ||
         IsBuiltInFunctionIdEq(node, FN_BYTE_ARRAY_NOT_LIKE_ALL);
}

static std::optional<LikeAnyAllRewriterConfig> GetRewriterConfig(
    const ResolvedFunctionCall* node) {
  if (IsLikeAnyFunctionNode(node)) {
    return LikeAnyAllRewriterConfig{LikeAnyAllRewriterConfig::kLikeAnyAll,
                                    kLikeAnyTemplate};
  } else if (IsNotLikeAnyFunctionNode(node)) {
    return LikeAnyAllRewriterConfig{LikeAnyAllRewriterConfig::kLikeAnyAll,
                                    kNotLikeAnyTemplate};
  } else if (IsLikeAllFunctionNode(node)) {
    return LikeAnyAllRewriterConfig{LikeAnyAllRewriterConfig::kLikeAnyAll,
                                    kLikeAllTemplate};
  } else if (IsNotLikeAllFunctionNode(node)) {
    return LikeAnyAllRewriterConfig{LikeAnyAllRewriterConfig::kLikeAnyAll,
                                    kNotLikeAllTemplate};
  } else if (IsLikeAnyArrayFunctionNode(node)) {
    return LikeAnyAllRewriterConfig{LikeAnyAllRewriterConfig::kLikeAnyAllArray,
                                    kLikeAnyTemplate};
  } else if (IsNotLikeAnyArrayFunctionNode(node)) {
    return LikeAnyAllRewriterConfig{LikeAnyAllRewriterConfig::kLikeAnyAllArray,
                                    kNotLikeAnyTemplate};
  } else if (IsLikeAllArrayFunctionNode(node)) {
    return LikeAnyAllRewriterConfig{LikeAnyAllRewriterConfig::kLikeAnyAllArray,
                                    kLikeAllTemplate};
  } else if (IsNotLikeAllArrayFunctionNode(node)) {
    return LikeAnyAllRewriterConfig{LikeAnyAllRewriterConfig::kLikeAnyAllArray,
                                    kNotLikeAllTemplate};
  }
  return std::nullopt;
}

absl::Status LikeAnyAllRewriteVisitor::VisitResolvedFunctionCall(
    const ResolvedFunctionCall* node) {
  std::optional<LikeAnyAllRewriterConfig> optional_rewriter_info =
      GetRewriterConfig(node);
  if (!optional_rewriter_info.has_value()) {
    return CopyVisitResolvedFunctionCall(node);
  }

  LikeAnyAllRewriterConfig& rewriter_info = optional_rewriter_info.value();
  switch (rewriter_info.rewriter_variant) {
    case LikeAnyAllRewriterConfig::kLikeAnyAll:
      return RewriteLikeAnyAll(node, rewriter_info.rewrite_template);
    case LikeAnyAllRewriterConfig::kLikeAnyAllArray:
      return RewriteLikeAnyAllArray(node, rewriter_info.rewrite_template);
    default:
      GOOGLESQL_RET_CHECK_FAIL()
          << "All enum value are covered above, should never reach here.";
  }
}

absl::Status LikeAnyAllRewriteVisitor::RewriteLikeAnyAll(
    const ResolvedFunctionCall* node, absl::string_view rewrite_template) {
  GOOGLESQL_RET_CHECK_GE(node->argument_list_size(), 2)
      << "LIKE ANY should have at least 2 arguments. Got: "
      << node->DebugString();

  // Extract and process the input
  const ResolvedExpr* input_expr = node->argument_list(0);
  GOOGLESQL_RET_CHECK_NE(input_expr, nullptr);
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedExpr> rewritten_input_expr,
                   ProcessNode(input_expr));
  const Type* input_type = rewritten_input_expr->type();
  GOOGLESQL_RET_CHECK(input_type->IsString() || input_type->IsBytes());

  // Extract and process the list of pattern arguments and use a helper function
  // to create a $make_array function to pack the patterns together
  std::vector<std::unique_ptr<const ResolvedExpr>> pattern_elements_list;
  for (int i = 1; i < node->argument_list_size(); ++i) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedExpr> rewritten_pattern_expr,
                     ProcessNode(node->argument_list(i)));
    pattern_elements_list.push_back(std::move(rewritten_pattern_expr));
  }

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<const ResolvedExpr> patterns_array_expr,
      fn_builder_.MakeArray(input_type, std::move(pattern_elements_list)));

  return RewriteLikeAnyAllArrayWithAggregate(std::move(rewritten_input_expr),
                                             std::move(patterns_array_expr),
                                             rewrite_template);
}

absl::Status LikeAnyAllRewriteVisitor::RewriteLikeAnyAllArray(
    const ResolvedFunctionCall* node, absl::string_view rewrite_template) {
  // Extract LIKE ANY arguments when given an array of patterns
  GOOGLESQL_RET_CHECK_EQ(node->argument_list_size(), 2)
      << "LIKE ANY with UNNEST has exactly 2 arguments. Got: "
      << node->DebugString();
  const ResolvedExpr* input_expr = node->argument_list(0);
  GOOGLESQL_RET_CHECK_NE(input_expr, nullptr);
  const ResolvedExpr* patterns_array_expr = node->argument_list(1);
  GOOGLESQL_RET_CHECK_NE(patterns_array_expr, nullptr);

  // Process child nodes first, so any nested rewrites are handled
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedExpr> rewritten_input_expr,
                   ProcessNode(input_expr));
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedExpr> rewritten_patterns_array_expr,
                   ProcessNode(patterns_array_expr));
  return RewriteLikeAnyAllArrayWithAggregate(
      std::move(rewritten_input_expr), std::move(rewritten_patterns_array_expr),
      rewrite_template);
}

absl::Status LikeAnyAllRewriteVisitor::RewriteLikeAnyAllArrayWithAggregate(
    std::unique_ptr<const ResolvedExpr> input_expr,
    std::unique_ptr<const ResolvedExpr> patterns_array_expr,
    absl::string_view template_string) {
  // Takes in the input expression, pattern array, and a template string that
  // differs between LIKE ANY and LIKE ALL expressions. AnalyzeSubstitute is
  // used with the function arguments to create the rewritten resolved AST.
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<ResolvedExpr> result,
      AnalyzeSubstitute(*analyzer_options_, *catalog_, *type_factory_,
                        template_string,
                        {{"input", input_expr.get()},
                         {"patterns", patterns_array_expr.get()}}));
  PushNodeToStack(std::move(result));
  return absl::OkStatus();
}

}  // namespace

class LikeAnyAllRewriter : public Rewriter {
 public:
  absl::StatusOr<std::unique_ptr<const ResolvedNode>> Rewrite(
      const AnalyzerOptions& options, const ResolvedNode& input,
      Catalog& catalog, TypeFactory& type_factory,
      AnalyzerOutputProperties& output_properties) const override {
    GOOGLESQL_RET_CHECK(options.id_string_pool() != nullptr);
    GOOGLESQL_RET_CHECK(options.column_id_sequence_number() != nullptr);
    LikeAnyAllRewriteVisitor rewriter(&options, &catalog, &type_factory);
    GOOGLESQL_RETURN_IF_ERROR(input.Accept(&rewriter));
    return rewriter.ConsumeRootNode<ResolvedNode>();
  }

  std::string Name() const override { return "LikeAnyAllRewriter"; }
};

const Rewriter* GetLikeAnyAllRewriter() {
  static const auto* kRewriter = new LikeAnyAllRewriter;
  return kRewriter;
}

}  // namespace googlesql
