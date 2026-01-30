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

#include "googlesql/common/constant_utils.h"

#include <algorithm>
#include <ostream>

#include "googlesql/public/types/annotation.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "absl/status/statusor.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

// The constness level of an expression.
enum class ConstnessLevel {
  kForeverConst,
  kAnalysisConst,
  kImmutableConst,
  kStableConst,
  kQueryConst,
  kNotConst,
};

// Prints the name of a ConstnessLevel enum value.
std::ostream& operator<<(std::ostream& os,
                         const ConstnessLevel& constness_level) {
  switch (constness_level) {
    case ConstnessLevel::kForeverConst:
      return os << "FOREVER_CONST";
    case ConstnessLevel::kAnalysisConst:
      return os << "ANALYSIS_CONST";
    case ConstnessLevel::kImmutableConst:
      return os << "IMMUTABLE_CONST";
    case ConstnessLevel::kStableConst:
      return os << "STABLE_CONST";
    case ConstnessLevel::kQueryConst:
      return os << "QUERY_CONST";
    case ConstnessLevel::kNotConst:
      return os << "NOT_CONST";
    default:
      return os << "INVALID";
  }
}

// Returns the constness level of `node` if it is an expression. Otherwise,
// returns the maximum constness level of all the expressions referenced in
// `node`.
//
// A functional constant is a recursive definition of constness, in which the
// constness level of an expression is the maximum constness level of all the
// expressions it contains.
//
// TODO: b/277365877 - Handle GROUPING_CONST and WINDOW_CONST separately, as
// they can only be understood by the resolver and GoogleSQL rewriter.
absl::StatusOr<ConstnessLevel> GetConstnessLevel(const ResolvedNode* node) {

  // Statements are not allowed to exist when deciding the constness level of
  // an expression.
  GOOGLESQL_RET_CHECK(!node->IsStatement());

  switch (node->node_kind()) {
    case RESOLVED_LITERAL: {
      // CAVEAT: ARRAY and STRUCT literal syntax might resolve to a literal (as
      // a result of constant folding) or a constructor (`$make_array` function
      // call or `ResolvedMakeStruct` node). A composite type constructor is an
      // ANALYSIS_CONST. We don't have a way to distinguish the original syntax
      // here.
      return ConstnessLevel::kForeverConst;
    }
    case RESOLVED_CONSTANT: {
      return ConstnessLevel::kAnalysisConst;
    }

    case RESOLVED_FLATTENED_ARG: {
      // TODO: b/277365877 - Implement IMMUTABLE_CONST.
      return ConstnessLevel::kNotConst;
    }

    case RESOLVED_FUNCTION_CALL: {
      const ResolvedFunctionCall* function_call =
          node->GetAs<ResolvedFunctionCall>();
      FunctionEnums::Volatility volatility =
          function_call->function()->function_options().volatility;

      if (volatility == FunctionEnums::VOLATILE) {
        return ConstnessLevel::kNotConst;
      }

      ConstnessLevel max_constness_level;
      if (volatility == FunctionEnums::STABLE) {
        max_constness_level = ConstnessLevel::kStableConst;
      } else {
        GOOGLESQL_RET_CHECK(volatility == FunctionEnums::IMMUTABLE);
        max_constness_level = ConstnessLevel::kImmutableConst;

        if (function_call->function()->IsGoogleSQLBuiltin() &&
            function_call->function()->Name() == "$make_array") {
          // A collapsed array literal is an ANALYSIS_CONST. It's a
          // `$make_array` function call with all arguments satisfying
          // ANALYSIS_CONST constraint. So an ARRAY constructor is at least an
          // ANALYSIS_CONST, even though `$make_array` is generally IMMUTABLE.
          max_constness_level = ConstnessLevel::kAnalysisConst;
        }
      }

      for (const auto& arg : function_call->argument_list()) {
        GOOGLESQL_ASSIGN_OR_RETURN(ConstnessLevel arg_constness_level,
                         GetConstnessLevel(arg.get()));
        max_constness_level =
            std::max(max_constness_level, arg_constness_level);
      }
      // TODO: b/277365877 - For full constness propagation correctness,
      // iterate over function_call->generic_argument_list() and handle
      // RESOLVED_FUNCTION_ARGUMENT for lambdas and other generic arguments.
      return max_constness_level;
    }
    case RESOLVED_MAKE_STRUCT: {
      auto* make_struct = node->GetAs<ResolvedMakeStruct>();
      // A collapsed struct literal is an ANALYSIS_CONST. It's a
      // ResolvedMakeStruct with all fields being analysis const.
      ConstnessLevel max_constness_level = ConstnessLevel::kAnalysisConst;
      for (auto& field : make_struct->field_list()) {
        GOOGLESQL_ASSIGN_OR_RETURN(ConstnessLevel arg_constness_level,
                         GetConstnessLevel(field.get()));
        max_constness_level =
            std::max(max_constness_level, arg_constness_level);
      }
      return max_constness_level;
    }
    case RESOLVED_MAKE_PROTO: {
      // A collapsed proto literal is an ANALYSIS_CONST. It's a
      // ResolvedMakeProto with all fields being analysis const.
      ConstnessLevel max_constness_level = ConstnessLevel::kAnalysisConst;
      for (auto& field : node->GetAs<ResolvedMakeProto>()->field_list()) {
        GOOGLESQL_ASSIGN_OR_RETURN(ConstnessLevel arg_constness_level,
                         GetConstnessLevel(field->expr()));
        max_constness_level =
            std::max(max_constness_level, arg_constness_level);
      }
      return max_constness_level;
    }

    case RESOLVED_CAST:
      return GetConstnessLevel(node->GetAs<ResolvedCast>()->expr());
    case RESOLVED_GET_STRUCT_FIELD:
      return GetConstnessLevel(node->GetAs<ResolvedGetStructField>()->expr());
    case RESOLVED_GET_PROTO_FIELD:
      return GetConstnessLevel(node->GetAs<ResolvedGetProtoField>()->expr());
    case RESOLVED_GET_JSON_FIELD:
      return GetConstnessLevel(node->GetAs<ResolvedGetJsonField>()->expr());

    case RESOLVED_SUBQUERY_EXPR: {
      const ResolvedSubqueryExpr* subquery_expr =
          node->GetAs<ResolvedSubqueryExpr>();
      GOOGLESQL_ASSIGN_OR_RETURN(ConstnessLevel subquery_constness_level,
                       GetConstnessLevel(subquery_expr->subquery()));
      ConstnessLevel max_constness_level =
          std::max(ConstnessLevel::kImmutableConst, subquery_constness_level);
      for (const auto& param : subquery_expr->parameter_list()) {
        GOOGLESQL_ASSIGN_OR_RETURN(ConstnessLevel param_constness_level,
                         GetConstnessLevel(param.get()));
        max_constness_level =
            std::max(max_constness_level, param_constness_level);
      }
      return max_constness_level;
    }
    // Constness of the with expr is determined by the assignment_list as well
    // as the output expr.
    case RESOLVED_WITH_EXPR: {
      const ResolvedWithExpr* with_expr = node->GetAs<ResolvedWithExpr>();
      GOOGLESQL_ASSIGN_OR_RETURN(ConstnessLevel expr_constness_level,
                       GetConstnessLevel(with_expr->expr()));
      ConstnessLevel max_constness_level =
          std::max(ConstnessLevel::kImmutableConst, expr_constness_level);
      for (const auto& assignment : with_expr->assignment_list()) {
        GOOGLESQL_ASSIGN_OR_RETURN(ConstnessLevel assignment_constness_level,
                         GetConstnessLevel(assignment->expr()));
        max_constness_level =
            std::max(max_constness_level, assignment_constness_level);
      }
      return max_constness_level;
    }

    // TODO: b/277365877 - The following cases are not explicitly discussed in
    // the design doc.
    case RESOLVED_EXPRESSION_COLUMN:
    case RESOLVED_CATALOG_COLUMN_REF:
    case RESOLVED_DMLDEFAULT:
    case RESOLVED_GRAPH_GET_ELEMENT_PROPERTY:
    case RESOLVED_GRAPH_MAKE_ELEMENT: {
      return ConstnessLevel::kNotConst;
    }

    case RESOLVED_PARAMETER:
    case RESOLVED_SYSTEM_VARIABLE: {
      return ConstnessLevel::kStableConst;
    }
    case RESOLVED_ARGUMENT_REF: {
      return ConstnessLevel::kQueryConst;
    }
    case RESOLVED_COLUMN_REF: {
      // Correlated column references are constant within the subquery scope,
      // hence QUERY_CONST.
      if (node->GetAs<ResolvedColumnRef>()->is_correlated()) {
        return ConstnessLevel::kQueryConst;
      }
      // Otherwise, the constness level of a column reference is determined by
      // the source table it comes from. It's impossible to tell from the
      // ResolvedExpr without additional context. We have to return NOT_CONST
      // here. However, since we don't allow table scans (ResolvedTableScan,
      // GraphNodeScan and GraphEdgeScan) that require catalog lookup to be
      // constant, it's safe to assume that non-correlated column references can
      // not be constant.
      return ConstnessLevel::kNotConst;
    }

    // A single row scan is a constant operator.
    case RESOLVED_SINGLE_ROW_SCAN: {
      return ConstnessLevel::kAnalysisConst;
    }

    // Project scan is the only scan operator that is allowed to propagate
    // constness. Its constness is determined by its input scan and expr_list.
    case RESOLVED_PROJECT_SCAN: {
      const ResolvedProjectScan* project_scan =
          node->GetAs<ResolvedProjectScan>();

      GOOGLESQL_ASSIGN_OR_RETURN(ConstnessLevel max_constness_level,
                       GetConstnessLevel(project_scan->input_scan()));
      for (const auto& expr : project_scan->expr_list()) {
        GOOGLESQL_ASSIGN_OR_RETURN(ConstnessLevel expr_constness_level,
                         GetConstnessLevel(expr->expr()));
        max_constness_level =
            std::max(max_constness_level, expr_constness_level);
      }
      return max_constness_level;
    }

    // Table scans that require catalog lookup are not constant.
    case RESOLVED_TABLE_SCAN:
    case RESOLVED_GRAPH_NODE_SCAN:
    case RESOLVED_GRAPH_EDGE_SCAN: {
      return ConstnessLevel::kNotConst;
    }

    // TODO: b/277365877 - The following cases are not explicitly discussed in
    // the design doc.
    // Aggregate and analytic scans are not constant because they might be
    // non-deterministic.
    case RESOLVED_AGGREGATE_FUNCTION_CALL:
    case RESOLVED_ANALYTIC_FUNCTION_CALL:
    case RESOLVED_ARRAY_AGGREGATE:
    case RESOLVED_AGGREGATE_SCAN:
    case RESOLVED_ANALYTIC_SCAN: {
      return ConstnessLevel::kNotConst;
    }

    // TODO: b/277365877 - Relax this.
    // For all the other case, defaults to being non-constant.
    default: {
      return ConstnessLevel::kNotConst;
    }
  }
}

bool IsAnalysisConstant(const ResolvedNode* node) {
  absl::StatusOr<ConstnessLevel> constness_level = GetConstnessLevel(node);
  if (!constness_level.ok()) {
    return false;
  }
  return constness_level.value() <= ConstnessLevel::kAnalysisConst;
}

bool IsImmutableConstant(const ResolvedNode* node) {
  absl::StatusOr<ConstnessLevel> constness_level = GetConstnessLevel(node);
  if (!constness_level.ok()) {
    return false;
  }
  return constness_level.value() <= ConstnessLevel::kImmutableConst;
}

bool IsStableConstant(const ResolvedNode* node) {
  absl::StatusOr<ConstnessLevel> constness_level = GetConstnessLevel(node);
  if (!constness_level.ok()) {
    return false;
  }
  return constness_level.value() <= ConstnessLevel::kStableConst;
}

bool IsQueryConstant(const ResolvedNode* node) {
  absl::StatusOr<ConstnessLevel> constness_level = GetConstnessLevel(node);
  if (!constness_level.ok()) {
    return false;
  }
  return constness_level.value() <= ConstnessLevel::kQueryConst;
}

}  // namespace googlesql
