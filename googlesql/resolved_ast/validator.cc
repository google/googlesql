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

#include "googlesql/resolved_ast/validator.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/base/varsetter.h"
#include "googlesql/analyzer/expr_matching_helpers.h"
#include "googlesql/analyzer/filter_fields_path_validator.h"
#include "googlesql/common/graph_element_utils.h"
#include "googlesql/common/measure_utils.h"
#include "googlesql/common/thread_stack.h"
#include "googlesql/public/aggregation_threshold_utils.h"
#include "googlesql/public/annotation/collation.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/constant.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/property_graph.h"
#include "googlesql/public/sql_view.h"
#include "googlesql/public/strings.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/templated_sql_function.h"
#include "googlesql/public/templated_sql_tvf.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/collation.h"
#include "googlesql/public/types/graph_element_type.h"
#include "googlesql/public/types/measure_type.h"
#include "googlesql/public/value.h"
#include "googlesql/public/with_modifier_mode.h"
#include "googlesql/resolved_ast/node_sources.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_enums.pb.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "googlesql/resolved_ast/rewrite_utils.h"
#include "googlesql/base/case.h"
#include "absl/algorithm/container.h"
#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "googlesql/base/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/stl_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status.h"
#include "googlesql/base/status_builder.h"
#include "googlesql/base/status_macros.h"

// Replacements for GOOGLESQL_RET_CHECK()-related macros within the validator.
// These macros behave similarly to the originals, but also record the node at
// the top of the context stack so that it get emphasized in the tree dump.
//
// Note: These macros are only valid inside of the Validator class.
#define VALIDATOR_RET_CHECK(expr) GOOGLESQL_RET_CHECK(expr) << RecordContext()
#define VALIDATOR_RET_CHECK_EQ(a, b) GOOGLESQL_RET_CHECK_EQ(a, b) << RecordContext()
#define VALIDATOR_RET_CHECK_NE(a, b) GOOGLESQL_RET_CHECK_NE(a, b) << RecordContext()
#define VALIDATOR_RET_CHECK_GE(a, b) GOOGLESQL_RET_CHECK_GE(a, b) << RecordContext()
#define VALIDATOR_RET_CHECK_GT(a, b) GOOGLESQL_RET_CHECK_GT(a, b) << RecordContext()
#define VALIDATOR_RET_CHECK_LE(a, b) GOOGLESQL_RET_CHECK_LE(a, b) << RecordContext()
#define VALIDATOR_RET_CHECK_LT(a, b) GOOGLESQL_RET_CHECK_LT(a, b) << RecordContext()
#define VALIDATOR_RET_CHECK_FAIL() GOOGLESQL_RET_CHECK_FAIL() << RecordContext()
#define VALIDATOR_RET_CHECK_OK(s) GOOGLESQL_RET_CHECK_OK(s) << RecordContext()

namespace googlesql {

#ifndef __EMSCRIPTEN__
#define RETURN_ERROR_IF_OUT_OF_STACK_SPACE()                                   \
  GOOGLESQL_RETURN_IF_NOT_ENOUGH_STACK(                                        \
      "Out of stack space due to deeply nested query expression during query " \
      "validation")
#else
#define RETURN_ERROR_IF_OUT_OF_STACK_SPACE()
#endif

namespace {

// Visitor that traverses Resolved AST nodes automatically and collects the
// set of ResolvedColumns created and referenced, according to the
// `column_is_created` and `column_list_is_created_columns` annotations in
// gen_resolved_ast.py.
//
// Calling CheckCreatedColumns checks that any columns that are referenced
// were created exactly once somewhere in the Resolved AST.
//
// This only checks globally and doesn't check that the flow from column
// creation to references is valid.  We don't currently have enough metadata
// in the Resolved AST to do that.  For example, within a particular
// ResolvedScan, we'd need to know the order that child fields are processed,
// and which ResolvedExprs can see columns produced by which sibling fields.
class ValidateResolvedColumnsVisitor : public ResolvedASTVisitor {
 public:
  ValidateResolvedColumnsVisitor() = default;

  absl::Status DefaultVisit(const ResolvedNode* node) override {
    for (const ResolvedColumn& col : node->GetColumnsCreated()) {
      if (!created_columns_.insert(col).second) {
        return absl::InternalError(absl::StrCat(
            "Column ", col.DebugString(), " created more than once; ",
            "Column creation must be marked with `column_is_created` or "
            "`column_list_is_created_columns` in `gen_resolved_ast.py`"));
      }
    }
    for (const ResolvedColumn& col : node->GetColumnsReferenced()) {
      referenced_columns_.insert(col);
    }
    return node->ChildrenAccept(this);
  }

  // ResolvedMultiStmt acts like a list of statements and allow creating the
  // same ResolvedColumns in each sub-statement.
  // We check each sub-statement individually, using new column sets.
  absl::Status VisitResolvedMultiStmt(const ResolvedMultiStmt* node) override {
    GOOGLESQL_RET_CHECK(created_columns_.empty());
    GOOGLESQL_RET_CHECK(referenced_columns_.empty());

    for (const auto& stmt : node->statement_list()) {
      GOOGLESQL_RETURN_IF_ERROR(stmt->Accept(this));
      GOOGLESQL_RETURN_IF_ERROR(CheckCreatedColumns());
      created_columns_.clear();
      referenced_columns_.clear();
    }

    return absl::OkStatus();
  }

  absl::Status CheckCreatedColumns() {
    for (const ResolvedColumn& col : referenced_columns_) {
      if (!created_columns_.contains(col)) {
        return absl::InternalError(absl::StrCat(
            "Column ", col.DebugString(), " was referenced but not created; ",
            "Column creation must be marked with `column_is_created` or "
            "`column_list_is_created_columns` in `gen_resolved_ast.py`"));
      }
    }
    return absl::OkStatus();
  }

 private:
  absl::flat_hash_set<ResolvedColumn> created_columns_;
  absl::flat_hash_set<ResolvedColumn> referenced_columns_;
};

}  // namespace

// Prefer `AddColumnsFromComputedColumnList` over this function.
template <typename ResolvedComputedColumnType>
static void AddColumnsFromComputedColumnListWithoutValidation(
    absl::Span<const std::unique_ptr<const ResolvedComputedColumnType>>
        computed_column_list,
    std::set<ResolvedColumn>& visible_columns) {
  for (const auto& computed_column : computed_column_list) {
    visible_columns.insert(computed_column->column());
  }
}

Validator::Validator(const LanguageOptions& language_options,
                     ValidatorOptions validator_options)
    : options_(std::move(validator_options)),
      language_options_(language_options) {}

static bool IsEmptyWindowFrame(const ResolvedWindowFrame& window_frame) {
  const ResolvedWindowFrameExpr* frame_start_expr = window_frame.start_expr();
  const ResolvedWindowFrameExpr* frame_end_expr = window_frame.end_expr();

  switch (frame_start_expr->boundary_type()) {
    case ResolvedWindowFrameExpr::UNBOUNDED_FOLLOWING:
      return true;
    case ResolvedWindowFrameExpr::CURRENT_ROW:
      if (frame_end_expr->boundary_type() ==
          ResolvedWindowFrameExpr::OFFSET_PRECEDING) {
        return true;
      }
      break;
    case ResolvedWindowFrameExpr::OFFSET_FOLLOWING:
      switch (frame_end_expr->boundary_type()) {
        case ResolvedWindowFrameExpr::OFFSET_PRECEDING:
        case ResolvedWindowFrameExpr::CURRENT_ROW:
          return true;
        default:
          break;
      }
      break;
    default:
      break;
  }
  if (frame_end_expr->boundary_type() ==
      ResolvedWindowFrameExpr::UNBOUNDED_PRECEDING) {
    return true;
  }
  return false;
}

absl::Status Validator::ValidateResolvedParameter(
    const ResolvedParameter* resolved_param) {
  PushErrorContext push(this, resolved_param);
  VALIDATOR_RET_CHECK(nullptr != resolved_param);
  // If the parameter has a name, it must not have a position and vice versa.
  const bool has_name = !resolved_param->name().empty();
  const bool has_position = resolved_param->position() > 0;
  if (has_name == has_position) {
    return InternalErrorBuilder()
           << "Parameter is expected to have a name or a position but not "
              "both: "
           << resolved_param->DebugString();
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedArgumentRef(
    const googlesql::ResolvedArgumentRef* arg_ref) {
  VALIDATOR_RET_CHECK(
      allowed_argument_kinds_.contains(arg_ref->argument_kind()))
      << "ResolvedArgumentRef with unexpected kind:\n"
      << arg_ref->DebugString();
  if (disallow_aggregate_resolved_arg_refs_ &&
      arg_ref->argument_kind() == ResolvedArgumentDefEnums::AGGREGATE) {
    return InternalErrorBuilder()
           << "Incorrect reference to argument " << arg_ref->DebugString();
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedExpressionColumn(
    const ResolvedExpressionColumn* expression_column) {
  if (disallow_expression_columns_) {
    return InternalErrorBuilder() << "Incorrect reference to expression column "
                                  << expression_column->DebugString();
  }
  expression_column->MarkFieldsAccessed();
  return absl::OkStatus();
}

absl::Status Validator::CheckColumnIsPresentInColumnSet(
    const ResolvedColumn& column,
    const std::set<ResolvedColumn>& visible_columns) {
  if (!googlesql_base::ContainsKey(visible_columns, column)) {
    return InternalErrorBuilder()
           << "Incorrect reference to column " << column.DebugString();
  }
  return absl::OkStatus();
}

absl::Status Validator::CheckColumnList(
    const ResolvedScan* scan, const std::set<ResolvedColumn>& visible_columns) {
  PushErrorContext push(this, scan);
  VALIDATOR_RET_CHECK(nullptr != scan);
  for (const ResolvedColumn& column : scan->column_list()) {
    if (!googlesql_base::ContainsKey(visible_columns, column)) {
      return InternalErrorBuilder()
             << "Column list contains column " << column.DebugString()
             << " not visible in scan node\n"
             << scan->DebugString();
    }
  }
  return absl::OkStatus();
}

Validator::Validator() = default;

Validator::~Validator() = default;

absl::Status Validator::ValidateResolvedExprList(
    const std::set<ResolvedColumn>& visible_columns,
    const std::set<ResolvedColumn>& visible_parameters,
    absl::Span<const std::unique_ptr<const ResolvedExpr>> expr_list) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  for (const auto& expr_iter : expr_list) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(visible_columns, visible_parameters,
                                         expr_iter.get()));
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedFunctionArgumentList(
    const std::set<ResolvedColumn>& visible_columns,
    const std::set<ResolvedColumn>& visible_parameters,
    absl::Span<const std::unique_ptr<const ResolvedFunctionArgument>>
        expr_list) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  for (const auto& expr_iter : expr_list) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedFunctionArgument(
        visible_columns, visible_parameters, expr_iter.get()));
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedCast(
    const std::set<ResolvedColumn>& visible_columns,
    const std::set<ResolvedColumn>& visible_parameters,
    const ResolvedCast* resolved_cast) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, resolved_cast);
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(visible_columns, visible_parameters,
                                       resolved_cast->expr()));
  if (resolved_cast->format() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(visible_columns, visible_parameters,
                                         resolved_cast->format()));
    VALIDATOR_RET_CHECK(resolved_cast->format()->type()->IsString());
  }
  if (resolved_cast->time_zone() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(visible_columns, visible_parameters,
                                         resolved_cast->time_zone()));
    VALIDATOR_RET_CHECK(resolved_cast->time_zone()->type()->IsString());
  }
  const TypeParameters& type_params =
      resolved_cast->type_modifiers().type_parameters();
  const Collation& collation = resolved_cast->type_modifiers().collation();
  if (!type_params.IsEmpty()) {
    GOOGLESQL_RETURN_IF_ERROR(resolved_cast->type()->ValidateResolvedTypeParameters(
        type_params, language_options_.product_mode()));
  }

  if (language_options_.LanguageFeatureEnabled(FEATURE_COLLATION_SUPPORT)) {
    VALIDATOR_RET_CHECK(collation.HasCompatibleStructure(resolved_cast->type()))
        << "collation must have compatible structure with the target type";
    GOOGLESQL_ASSIGN_OR_RETURN(bool equals_to_annotation,
                     collation.EqualsCollationAnnotation(
                         resolved_cast->type_annotation_map()));
    std::string collation_annotations_debug_string =
        resolved_cast->type_annotation_map() == nullptr
            ? "null"
            : resolved_cast->type_annotation_map()->DebugString(
                  CollationAnnotation::GetId());
    VALIDATOR_RET_CHECK(equals_to_annotation)
        << "Collation must be semantically equal to collation annotations: "
        << collation.DebugString() << " vs. "
        << collation_annotations_debug_string;
  } else {
    VALIDATOR_RET_CHECK(collation.Empty());
  }

  resolved_cast->return_null_on_error();  // Mark field as visited.
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedConstant(
    const std::set<ResolvedColumn>& visible_columns,
    const std::set<ResolvedColumn>& visible_parameters,
    const ResolvedConstant* resolved_constant) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, resolved_constant);

  VALIDATOR_RET_CHECK(resolved_constant->constant() != nullptr)
      << "ResolvedConstant does not have a Constant:\n"
      << resolved_constant->DebugString();
  VALIDATOR_RET_CHECK(
      resolved_constant->constant()->type()->Equals(resolved_constant->type()))
      << "Expected ResolvedConstant of type "
      << resolved_constant->constant()->type()->DebugString() << ", found "
      << resolved_constant->type()->DebugString();

  return absl::OkStatus();
}

absl::Status Validator::ValidateGenericArgumentsAgainstConcreteArguments(
    const ResolvedFunctionCallBase* resolved_function_call,
    const FunctionSignature& signature) {
  PushErrorContext push(this, resolved_function_call);
  bool only_expr_is_used = true;
  for (int i = 0; i < resolved_function_call->generic_argument_list_size();
       i++) {
    const ResolvedFunctionArgument* generic_arg =
        resolved_function_call->generic_argument_list(i);
    const FunctionArgumentType& concrete_argument =
        signature.ConcreteArgument(i);
    if (generic_arg->expr() != nullptr) {
      VALIDATOR_RET_CHECK(
          concrete_argument.type() != nullptr &&
          generic_arg->expr()->type()->Equals(concrete_argument.type()))
          << "Arg index: " << i
          << " function: " << resolved_function_call->DebugString();
    } else if (generic_arg->inline_lambda() != nullptr) {
      only_expr_is_used = false;

      const ResolvedInlineLambda* lambda = generic_arg->inline_lambda();
      const FunctionArgumentType::ArgumentTypeLambda& concrete_lambda =
          concrete_argument.lambda();
      VALIDATOR_RET_CHECK_EQ(lambda->argument_list().size(),
                             concrete_lambda.argument_types().size());
      // Check lambda argument matches concrete argument. This shouldn't trigger
      // under known use cases. But still doing the check just in case.
      for (int i = 0; i < lambda->argument_list_size(); i++) {
        VALIDATOR_RET_CHECK(lambda->argument_list(i).type()->Equals(
            concrete_lambda.argument_types()[i].type()))
            << i << "lambda argument type: "
            << lambda->argument_list(i).type()->DebugString()
            << " concrete type: " << concrete_lambda.argument_types()[i].type();
      }
      VALIDATOR_RET_CHECK(
          lambda->body()->type()->Equals(concrete_lambda.body_type().type()))
          << " lambda body type: " << lambda->body()->type()->DebugString()
          << " concrete body type: "
          << concrete_lambda.body_type().type()->DebugString();
    } else if (generic_arg->sequence() != nullptr) {
      only_expr_is_used = false;
      VALIDATOR_RET_CHECK(concrete_argument.IsSequence())
          << " Found Sequence argument for concrete_argument: "
          << concrete_argument.DebugString();
    } else {
      only_expr_is_used = false;
      // For non expr arguments, we need to check argument type against the type
      // required by the signature. We should arrange to share the code with
      // TVFScan validation.
      VALIDATOR_RET_CHECK_FAIL()
          << "Unexpected function argument with index " << i
          << " of function call: " << resolved_function_call->DebugString();
    }
  }
  // `generic_argument_list` must be used if the signature supports argument
  // aliases, even if all the arguments are expressions.
  if (!SignatureSupportsArgumentAliases(signature)) {
    VALIDATOR_RET_CHECK(resolved_function_call->generic_argument_list_size() ==
                            0 ||
                        !only_expr_is_used)
        << "If all arguments are ResolvedExpressions and all argument aliases "
        << "are empty, `argument_list` should be used instead of "
        << "`generic_argument_list`";
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedFilterField(
    const std::set<ResolvedColumn>& visible_columns,
    const std::set<ResolvedColumn>& visible_parameters,
    const ResolvedFilterField* filter_field) {
  PushErrorContext push(this, filter_field);
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(visible_columns, visible_parameters,
                                       filter_field->expr()));

  const auto& expr_type = filter_field->expr()->type();
  VALIDATOR_RET_CHECK(expr_type->IsProto());

  const auto& filter_field_arg_list = filter_field->filter_field_arg_list();
  FilterFieldsPathValidator validator(expr_type->AsProto()->descriptor());
  for (const auto& filter_field_arg : filter_field_arg_list) {
    VALIDATOR_RET_CHECK(!filter_field_arg->field_descriptor_path().empty());
    VALIDATOR_RET_CHECK_OK(
        validator.ValidateFieldPath(filter_field_arg->include(),
                                    filter_field_arg->field_descriptor_path()));
  }
  VALIDATOR_RET_CHECK_OK(
      validator.FinalValidation(filter_field->reset_cleared_required_fields()));
  return absl::OkStatus();
}

absl::Status Validator::ValidateArgumentAliases(
    const FunctionSignature& signature,
    absl::Span<const std::unique_ptr<const ResolvedFunctionArgument>>
        arguments) {
  VALIDATOR_RET_CHECK_EQ(arguments.size(), signature.NumConcreteArguments());
  for (int i = 0; i < arguments.size(); i++) {
    FunctionEnums::ArgumentAliasKind alias_kind =
        signature.ConcreteArgument(i).options().argument_alias_kind();
    VALIDATOR_RET_CHECK_NE(alias_kind,
                           FunctionEnums::ARGUMENT_ALIAS_KIND_UNSPECIFIED);
    if (!arguments[i]->argument_alias().empty()) {
      // If the argument has an alias in the function call, it must support
      // aliases in the signature.
      //
      // Note we cannot validate the other direction: if the argument supports
      // aliases, its alias may still be empty because the resolver generates an
      // empty string as alias for it.
      VALIDATOR_RET_CHECK_EQ(alias_kind, FunctionEnums::ARGUMENT_ALIASED);
    }
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateTemplatedSqlFunctionCall(
    const TemplatedSQLFunctionCall& templated_info,
    const FunctionSignature& signature,
    const ArgumentKindSet& allowed_argument_kinds) {
  googlesql_base::VarSetter<ArgumentKindSet> allowed_argument_kinds_setter(
      &allowed_argument_kinds_, allowed_argument_kinds);
  // Column ids inside a templated function call only need to be unique within
  // the body of the function.
  googlesql_base::VarSetter<absl::flat_hash_set<int>> column_ids_seen_setter(&column_ids_seen_,
                                                             {});
  // Any context from an enclosing CREATE TABLE FUNCTION statement is not
  // relevant to the templated function call.
  googlesql_base::VarSetter<const ResolvedCreateTableFunctionStmt*>
      current_create_table_function_stmt_setter(
          &current_create_table_function_stmt_, nullptr);

  // Validate the output type.
  VALIDATOR_RET_CHECK(
      templated_info.expr()->type()->Equals(signature.result_type().type()));

  // For non-aggregates, no columns are visible.  For aggregates, columns
  // created by the aggregate expressions are visible.
  std::set<ResolvedColumn> visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(AddColumnsFromComputedColumnList(
      templated_info.aggregate_expression_list(), &visible_columns));

  // The body of a templated function can only refer to its own
  // arguments, not external columns.
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(
      visible_columns, /*visible_parameters=*/{}, templated_info.expr()));

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedFunctionCallBase(
    const std::set<ResolvedColumn>& visible_columns,
    const std::set<ResolvedColumn>& visible_parameters,
    const ResolvedFunctionCallBase* resolved_function_call) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, resolved_function_call);

  VALIDATOR_RET_CHECK(resolved_function_call->function() != nullptr)
      << "ResolvedFunctionCall does not have a Function:\n"
      << resolved_function_call->DebugString();

  VALIDATOR_RET_CHECK(resolved_function_call->argument_list_size() == 0 ||
                      resolved_function_call->generic_argument_list_size() == 0)
      << "Only one of argument_list and generic_argument_list can be "
         "non-empty. "
         "Function call: "
      << resolved_function_call->DebugString();

  GOOGLESQL_RETURN_IF_ERROR(
      ValidateResolvedExprList(visible_columns, visible_parameters,
                               resolved_function_call->argument_list()));

  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedFunctionArgumentList(
      visible_columns, visible_parameters,
      resolved_function_call->generic_argument_list()));

  const FunctionSignature& signature = resolved_function_call->signature();
  VALIDATOR_RET_CHECK(signature.IsConcrete())
      << "ResolvedFunctionCall must have a concrete signature:\n"
      << resolved_function_call->DebugString();
  VALIDATOR_RET_CHECK(
      resolved_function_call->type()->Equals(signature.result_type().type()))
      << "Resolved function call type: "
      << resolved_function_call->type()->DebugString()
      << ", signature result type: "
      << signature.result_type().type()->DebugString();

  const int num_concrete_args = signature.NumConcreteArguments();
  const int num_resolved_args =
      resolved_function_call->argument_list_size() == 0
          ? resolved_function_call->generic_argument_list_size()
          : resolved_function_call->argument_list_size();
  VALIDATOR_RET_CHECK_EQ(num_resolved_args, num_concrete_args)
      << resolved_function_call->DebugString()
      << "\nSignature: " << signature.DebugString();
  for (int i = 0; i < resolved_function_call->argument_list_size(); i++) {
    VALIDATOR_RET_CHECK(
        resolved_function_call->argument_list(i)->type()->Equals(
            signature.ConcreteArgumentType(i)))
        << resolved_function_call->argument_list(i)->type()->DebugString(true)
        << " vs " << signature.ConcreteArgumentType(i)->DebugString(true)
        << "arg: " << i;
  }

  GOOGLESQL_RETURN_IF_ERROR(ValidateGenericArgumentsAgainstConcreteArguments(
      resolved_function_call, signature));

  if (resolved_function_call->error_mode() ==
      ResolvedFunctionCallBase::SAFE_ERROR_MODE) {
    VALIDATOR_RET_CHECK(
        resolved_function_call->function()->SupportsSafeErrorMode())
        << "Function " << resolved_function_call->function()->FullName(false)
        << "does not support SAFE error mode";
  }

  if (resolved_function_call->node_kind() == RESOLVED_AGGREGATE_FUNCTION_CALL) {
    const ResolvedFunctionCallInfo* info =
        resolved_function_call->GetAs<ResolvedAggregateFunctionCall>()
            ->function_call_info()
            .get();
    if (info != nullptr && info->Is<TemplatedSQLFunctionCall>()) {
      std::set<ResolvedColumn> visible_columns;
      GOOGLESQL_RETURN_IF_ERROR(ValidateTemplatedSqlFunctionCall(
          *info->GetAs<TemplatedSQLFunctionCall>(),
          resolved_function_call->signature(),
          /*allowed_argument_kinds=*/
          {{ResolvedArgumentDefEnums::AGGREGATE,
            ResolvedArgumentDefEnums::NOT_AGGREGATE}}));
    }
  }

  if (resolved_function_call->node_kind() == RESOLVED_FUNCTION_CALL) {
    const ResolvedFunctionCallInfo* info =
        resolved_function_call->GetAs<ResolvedFunctionCall>()
            ->function_call_info()
            .get();
    if (info != nullptr && info->Is<TemplatedSQLFunctionCall>()) {
      GOOGLESQL_RETURN_IF_ERROR(ValidateTemplatedSqlFunctionCall(
          *info->GetAs<TemplatedSQLFunctionCall>(),
          resolved_function_call->signature(),
          /*allowed_argument_kinds=*/{ResolvedArgumentDefEnums::SCALAR}));
    }
    GOOGLESQL_RETURN_IF_ERROR(ValidateHintList(resolved_function_call->hint_list()));
  }

  VALIDATOR_RET_CHECK(resolved_function_call->collation_list().size() <= 1);

  if (!resolved_function_call->generic_argument_list().empty()) {
    // Only functions that use `generic_argument_list` can have argument
    // aliases.
    GOOGLESQL_RETURN_IF_ERROR(ValidateArgumentAliases(
        signature, resolved_function_call->generic_argument_list()));
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateFinalState(bool in_multi_stmt) {
  if (in_multi_stmt) {
    // When inside a ResolvedMultiStmt, we're finishing an inner statement,
    // while the stack still has the context for the outer ResolvedMultiStmt.
    VALIDATOR_RET_CHECK(!context_stack_.empty());
  } else {
    VALIDATOR_RET_CHECK(context_stack_.empty());
    VALIDATOR_RET_CHECK(visible_with_entries_.empty());
  }
  VALIDATOR_RET_CHECK(unconsumed_side_effect_columns_.empty())
      << "Unconsumed side effect columns: "
      << absl::StrJoin(unconsumed_side_effect_columns_, ", ");
  return absl::OkStatus();
}

absl::Status Validator::ValidateStandaloneResolvedExpr(
    const ResolvedExpr* expr) {
  Reset();
  absl::Status status = ValidateResolvedExpr(
      /*visible_columns=*/{}, /*visible_parameters=*/{}, expr);

  if (status.ok()) {
    ValidateResolvedColumnsVisitor column_visitor;
    status = expr->Accept(&column_visitor);
    if (status.ok()) {
      status = column_visitor.CheckCreatedColumns();
    }
  }

  if (!status.ok()) {
    if (status.code() == absl::StatusCode::kResourceExhausted) {
      // Don't wrap a resource exhausted status into internal error. This error
      // may still occur for a valid properly resolved expression (stack
      // exhaustion in case of deeply nested expression). There exist cases
      // where the validator uses more stack than parsing/analysis (b/65294961).
      return status;
    }
    // Note: This string is pattern-matched in
    // `tools/execute_query/execute_query_web_writer.cc`.  Keep that in sync.
    return InternalErrorBuilder()
           << "Resolved AST validation failed: " << status.message() << "\n"
           << expr->DebugString(ResolvedNode::DebugStringConfig{
                  {{error_context_, "(validation failed here)"}}, false});
  }

  return ValidateFinalState();
}

absl::Status Validator::ValidateResolvedExpr(
    const std::set<ResolvedColumn>& visible_columns,
    const std::set<ResolvedColumn>& visible_parameters,
    const ResolvedExpr* expr) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, expr);

  VALIDATOR_RET_CHECK(nullptr != expr);
  VALIDATOR_RET_CHECK(expr->type() != nullptr)
      << "ResolvedExpr does not have a Type:\n"
      << expr->DebugString();
  if (expr->type_annotation_map() != nullptr) {
    VALIDATOR_RET_CHECK(
        expr->type_annotation_map()->HasCompatibleStructure(expr->type()));
  }

  if (!language_options_.LanguageFeatureEnabled(FEATURE_COLLATION_SUPPORT)) {
    VALIDATOR_RET_CHECK(
        !CollationAnnotation::ExistsIn(expr->type_annotation_map()));
  }

  // This will fail if more child types are added. Add them to the switch below
  // before updating this.
  static_assert(ResolvedExpr::NUM_DESCENDANT_LEAF_TYPES == 31,
                "Missing case in switch on ResolvedExpr descendants");

  // Do not add new checks inline here because they increase stack space used in
  // this recursive function. Add a new function for each case instead. Also,
  // avoid creating any unnecessary temporary objects (e.g. by calling
  // GOOGLESQL_RETURN_IF_ERROR(Validate...) rather than return Validate...).
  switch (expr->node_kind()) {
    case RESOLVED_LITERAL:
    case RESOLVED_DMLDEFAULT:
    case RESOLVED_SYSTEM_VARIABLE:
      // No validation required.
      expr->MarkFieldsAccessed();
      break;
    case RESOLVED_EXPRESSION_COLUMN:
      return ValidateResolvedExpressionColumn(
          expr->GetAs<ResolvedExpressionColumn>());
    case RESOLVED_CATALOG_COLUMN_REF: {
      const ResolvedCatalogColumnRef* resolved_catalog_column_ref =
          expr->GetAs<ResolvedCatalogColumnRef>();
      const Column* ctlg_col = resolved_catalog_column_ref->column();
      if (ctlg_col != nullptr) {
        GOOGLESQL_RET_CHECK(resolved_catalog_column_ref->name().empty());
        GOOGLESQL_RET_CHECK(ctlg_col->GetType()->Equals(expr->type()));
      } else {
        GOOGLESQL_RET_CHECK(!resolved_catalog_column_ref->name().empty());
      }
      break;
    }
    case RESOLVED_PARAMETER:
      return ValidateResolvedParameter(expr->GetAs<ResolvedParameter>());
    case RESOLVED_COLUMN_REF: {
      const ResolvedColumnRef* column_ref = expr->GetAs<ResolvedColumnRef>();
      VALIDATOR_RET_CHECK(
          column_ref->type()->Equals(column_ref->column().type()))
          << "\nColumnRef type: " << column_ref->type()->DebugString()
          << "\nColumn type: " << column_ref->column().type()->DebugString();
      if (unconsumed_side_effect_columns_.contains(
              column_ref->column().column_id())) {
        unconsumed_side_effect_columns_.erase(column_ref->column().column_id());
      }
      return CheckColumnIsPresentInColumnSet(
          column_ref->column(),
          column_ref->is_correlated() ? visible_parameters : visible_columns);
    }
    case RESOLVED_ARGUMENT_REF:
      return ValidateResolvedArgumentRef(expr->GetAs<ResolvedArgumentRef>());
    case RESOLVED_CAST:
      return ValidateResolvedCast(visible_columns, visible_parameters,
                                  expr->GetAs<ResolvedCast>());
    case RESOLVED_CONSTANT:
      return ValidateResolvedConstant(visible_columns, visible_parameters,
                                      expr->GetAs<ResolvedConstant>());
    case RESOLVED_FUNCTION_CALL: {
      return ValidateResolvedFunctionCallBase(
          visible_columns, visible_parameters,
          expr->GetAs<ResolvedFunctionCall>());
    }
    case RESOLVED_AGGREGATE_FUNCTION_CALL: {
      return ValidateResolvedAggregateFunctionCall(
          visible_columns, visible_parameters,
          expr->GetAs<ResolvedAggregateFunctionCall>());
    }
    case RESOLVED_ANALYTIC_FUNCTION_CALL: {
      return ValidateResolvedAnalyticFunctionCall(
          visible_columns, visible_parameters,
          expr->GetAs<ResolvedAnalyticFunctionCall>());
    }
    case RESOLVED_MAKE_STRUCT: {
      return ValidateResolvedMakeStruct(visible_columns, visible_parameters,
                                        expr->GetAs<ResolvedMakeStruct>());
    }
    case RESOLVED_MAKE_PROTO: {
      for (const auto& resolved_make_proto_field :
           expr->GetAs<ResolvedMakeProto>()->field_list()) {
        VALIDATOR_RET_CHECK(nullptr != resolved_make_proto_field);
        GOOGLESQL_RETURN_IF_ERROR(
            ValidateResolvedExpr(visible_columns, visible_parameters,
                                 resolved_make_proto_field->expr()));
        resolved_make_proto_field->field_descriptor();  // Mark visited.
        resolved_make_proto_field->format();            // Mark visited.
      }
      break;
    }
    case RESOLVED_GET_STRUCT_FIELD: {
      const ResolvedGetStructField* get_struct_field =
          expr->GetAs<ResolvedGetStructField>();
      GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(visible_columns, visible_parameters,
                                           get_struct_field->expr()));
      VALIDATOR_RET_CHECK(get_struct_field->expr()->type()->IsStruct());
      VALIDATOR_RET_CHECK_GE(get_struct_field->field_idx(), 0);
      VALIDATOR_RET_CHECK_LT(
          get_struct_field->field_idx(),
          get_struct_field->expr()->type()->AsStruct()->num_fields());
      break;
    }
    case RESOLVED_GET_PROTO_FIELD:
      return ValidateResolvedGetProtoFieldExpr(
          visible_columns, visible_parameters,
          expr->GetAs<ResolvedGetProtoField>());
    case RESOLVED_GET_JSON_FIELD:
      return ValidateResolvedGetJsonFieldExpr(
          visible_columns, visible_parameters,
          expr->GetAs<ResolvedGetJsonField>());
    case RESOLVED_GET_ROW_FIELD:
      return ValidateResolvedGetRowFieldExpr(
          visible_columns, visible_parameters,
          expr->GetAs<ResolvedGetRowField>());
    case RESOLVED_FLATTEN:
      return ValidateResolvedFlatten(visible_columns, visible_parameters,
                                     expr->GetAs<ResolvedFlatten>());
    case RESOLVED_FLATTENED_ARG:
      return ValidateResolvedFlattenedArg(expr->GetAs<ResolvedFlattenedArg>());
    case RESOLVED_GET_PROTO_ONEOF:
      return ValidateResolvedGetProtoOneof(
          visible_columns, visible_parameters,
          expr->GetAs<ResolvedGetProtoOneof>());
    case RESOLVED_SUBQUERY_EXPR:
      return ValidateResolvedSubqueryExpr(visible_columns, visible_parameters,
                                          expr->GetAs<ResolvedSubqueryExpr>());
    case RESOLVED_WITH_EXPR:
      return ValidateResolvedWithExpr(visible_columns, visible_parameters,
                                      expr->GetAs<ResolvedWithExpr>());
    case RESOLVED_REPLACE_FIELD:
      return ValidateResolvedReplaceField(visible_columns, visible_parameters,
                                          expr->GetAs<ResolvedReplaceField>());
    case RESOLVED_FILTER_FIELD:
      return ValidateResolvedFilterField(visible_columns, visible_parameters,
                                         expr->GetAs<ResolvedFilterField>());
    case RESOLVED_GRAPH_GET_ELEMENT_PROPERTY:
      return ValidateResolvedGraphGetElementProperty(
          visible_columns, visible_parameters,
          expr->GetAs<ResolvedGraphGetElementProperty>());
    case RESOLVED_GRAPH_MAKE_ELEMENT:
      return ValidateResolvedGraphMakeElement(
          visible_columns, visible_parameters,
          expr->GetAs<ResolvedGraphMakeElement>());
    case RESOLVED_GRAPH_IS_LABELED_PREDICATE:
      return ValidateResolvedGraphIsLabeledPredicate(
          visible_columns, visible_parameters,
          expr->GetAs<ResolvedGraphIsLabeledPredicate>());
    case RESOLVED_ARRAY_AGGREGATE:
      return ValidateResolvedArrayAggregate(
          visible_columns, visible_parameters,
          expr->GetAs<ResolvedArrayAggregate>());
    case RESOLVED_UPDATE_CONSTRUCTOR:
      return ValidateResolvedUpdateConstructor(
          visible_columns, visible_parameters,
          expr->GetAs<ResolvedUpdateConstructor>());
    default:
      return ::googlesql_base::InternalErrorBuilder()
             << "Unhandled node kind: " << expr->node_kind_string()
             << " in ValidateResolvedExpr";
  }

  return absl::OkStatus();
}

static bool IsFirstOrLast(const Function* function) {
  if (!function->IsGoogleSQLBuiltin() || function->NumSignatures() != 1) {
    return false;
  }
  return function->GetSignature(0)->context_id() == FN_FIRST_AGG ||
         function->GetSignature(0)->context_id() == FN_LAST_AGG;
}

absl::Status Validator::ValidateOrderByAndLimitClausesOfAggregateFunctionCall(
    const std::set<ResolvedColumn>& input_scan_visible_columns,
    const std::set<ResolvedColumn>& visible_parameters,
    const ResolvedAggregateFunctionCall* aggregate_function_call) {
  PushErrorContext push(this, aggregate_function_call);
  const Function* aggregate_function = aggregate_function_call->function();
  const std::string& function_name = aggregate_function->Name();

  if (IsFirstOrLast(aggregate_function_call->function())) {
    VALIDATOR_RET_CHECK(match_recognize_state_.has_value());
    VALIDATOR_RET_CHECK(
        match_recognize_state_->match_row_number_column.IsInitialized());
    VALIDATOR_RET_CHECK_EQ(aggregate_function_call->order_by_item_list_size(),
                           1);
    VALIDATOR_RET_CHECK_EQ(
        aggregate_function_call->order_by_item_list(0)
            ->column_ref()
            ->column()
            .column_id(),
        match_recognize_state_->match_row_number_column.column_id());
  } else if (!aggregate_function->SupportsOrderingArguments() &&
             !aggregate_function_call->order_by_item_list().empty()) {
    return InternalErrorBuilder() << "Aggregate function " << function_name
                                  << " does not support ordering arguments,"
                                  << " but has an ORDER BY clause:\n"
                                  << aggregate_function->DebugString();
  }

  if (!aggregate_function->SupportsLimitArguments() &&
      aggregate_function_call->limit() != nullptr) {
    return InternalErrorBuilder() << "Aggregate function " << function_name
                                  << " does not support limiting arguments,"
                                  << " but has a LIMIT clause:\n"
                                  << aggregate_function->DebugString();
  }

  std::set<ResolvedColumn> order_by_visible_columns =
      input_scan_visible_columns;
  const bool is_multi_level_aggregate_function =
      !aggregate_function_call->group_by_list().empty();
  if (is_multi_level_aggregate_function) {
    order_by_visible_columns.clear();
    // `group_by_list` and `group_by_aggregate_list` columns are already seen
    // and validated in `ValidateResolvedAggregateFunctionCall`, so add them
    // to `order_by_visible_columns` without validation.
    AddColumnsFromComputedColumnListWithoutValidation<ResolvedComputedColumn>(
        aggregate_function_call->group_by_list(), order_by_visible_columns);
    AddColumnsFromComputedColumnListWithoutValidation<
        ResolvedComputedColumnBase>(
        aggregate_function_call->group_by_aggregate_list(),
        order_by_visible_columns);
  }

  googlesql_base::VarSetter<bool> disallow_aggregate_resolved_arg_refs_setter(
      &disallow_aggregate_resolved_arg_refs_,
      is_multi_level_aggregate_function);
  googlesql_base::VarSetter<bool> disallow_expression_columns_setter(
      &disallow_expression_columns_, is_multi_level_aggregate_function);
  for (const auto& order_by_item :
       aggregate_function_call->order_by_item_list()) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedOrderByItem(
        order_by_visible_columns, visible_parameters, order_by_item.get()));
  }

  if (aggregate_function_call->limit() != nullptr) {
    bool validate_constant_nonnegative =
        !language_options_.LanguageFeatureEnabled(
            FEATURE_LIMIT_OFFSET_EXPRESSIONS);
    GOOGLESQL_RETURN_IF_ERROR(ValidateArgumentIsInt64(
        aggregate_function_call->limit(), validate_constant_nonnegative,
        absl::StrCat("Limit in ", function_name)));
  }

  // If both DISTINCT and ORDER BY are present, the ORDER BY arguments
  // must be a subset of the DISTINCT arguments.
  if (aggregate_function_call->distinct()) {
    for (const auto& order_by_item :
         aggregate_function_call->order_by_item_list()) {
      VALIDATOR_RET_CHECK(absl::c_any_of(
          aggregate_function_call->argument_list(),
          [&](const std::unique_ptr<const ResolvedExpr>& argument) {
            return argument->Is<ResolvedColumnRef>() &&
                   argument->GetAs<ResolvedColumnRef>()->column() ==
                       order_by_item->column_ref()->column();
          }))
          << "ORDER BY argument: " << order_by_item->DebugString()
          << " is not an argument to the aggregate: "
          << absl::StrJoin(aggregate_function_call->argument_list(), ", ",
                           [](std::string* out,
                              const std::unique_ptr<const ResolvedExpr>& expr) {
                             absl::StrAppend(out, expr->DebugString());
                           });
    }
  }

  return absl::OkStatus();
}

absl::Status Validator::ValidateGroupingSetListAreEmpty(
    absl::Span<const std::unique_ptr<const ResolvedGroupingSetBase>>
        grouping_set_list,
    absl::Span<const std::unique_ptr<const ResolvedColumnRef>>
        rollup_column_list) {
  VALIDATOR_RET_CHECK(grouping_set_list.empty() && rollup_column_list.empty());
  return absl::OkStatus();
}

absl::Status Validator::ValidateAndExtractNonCompositeGroupingSetColumns(
    const std::set<ResolvedColumn>& group_by_columns,
    const ResolvedGroupingSetBase* grouping_set_base,
    std::set<ResolvedColumn>* grouping_sets_all_columns) {
  if (grouping_set_base->Is<ResolvedGroupingSet>()) {
    const ResolvedGroupingSet* grouping_set =
        grouping_set_base->GetAs<ResolvedGroupingSet>();
    // Columns should be unique within each grouping set.
    absl::flat_hash_set<ResolvedColumn> grouping_set_columns;
    for (const auto& column_ref : grouping_set->group_by_column_list()) {
      GOOGLESQL_RETURN_IF_ERROR(CheckColumnIsPresentInColumnSet(column_ref->column(),
                                                      group_by_columns));
      VALIDATOR_RET_CHECK(
          googlesql_base::InsertIfNotPresent(&grouping_set_columns, column_ref->column()));
    }
    grouping_sets_all_columns->insert(grouping_set_columns.begin(),
                                      grouping_set_columns.end());
  } else {
    // rollup or cube
    VALIDATOR_RET_CHECK(grouping_set_base->Is<ResolvedRollup>() ||
                        grouping_set_base->Is<ResolvedCube>());
    const auto& multi_column_list =
        grouping_set_base->Is<ResolvedRollup>()
            ? grouping_set_base->GetAs<ResolvedRollup>()->rollup_column_list()
            : grouping_set_base->GetAs<ResolvedCube>()->cube_column_list();
    for (const auto& multi_column : multi_column_list) {
      // Duplicated columns are not allowed in multi-column, while
      // rollup and cube are allowed to have duplicated multi-columns.
      absl::flat_hash_set<ResolvedColumn> multi_column_set;
      // Multi-column is not allowed to be empty.
      VALIDATOR_RET_CHECK(!multi_column->column_list().empty());
      for (const auto& column_ref : multi_column->column_list()) {
        GOOGLESQL_RETURN_IF_ERROR(CheckColumnIsPresentInColumnSet(column_ref->column(),
                                                        group_by_columns));
        VALIDATOR_RET_CHECK(
            googlesql_base::InsertIfNotPresent(&multi_column_set, column_ref->column()));
      }
      grouping_sets_all_columns->insert(multi_column_set.begin(),
                                        multi_column_set.end());
    }
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateAndExtractGroupingSetColumns(
    const std::set<ResolvedColumn>& group_by_columns,
    const ResolvedGroupingSetBase* grouping_set_base,
    std::set<ResolvedColumn>* grouping_sets_all_columns) {
  if (grouping_set_base->Is<ResolvedGroupingSet>() ||
      grouping_set_base->Is<ResolvedRollup>() ||
      grouping_set_base->Is<ResolvedCube>()) {
    return ValidateAndExtractNonCompositeGroupingSetColumns(
        group_by_columns, grouping_set_base, grouping_sets_all_columns);
  }
  VALIDATOR_RET_CHECK(grouping_set_base->Is<ResolvedGroupingSetList>());
  for (const auto& grouping_set_base :
       grouping_set_base->GetAs<ResolvedGroupingSetList>()->elem_list()) {
    VALIDATOR_RET_CHECK(!grouping_set_base->Is<ResolvedGroupingSetProduct>())
        << "Nested ResolvedGroupingSetProduct is invalid";
    VALIDATOR_RET_CHECK(!grouping_set_base->Is<ResolvedGroupingSetList>())
        << "Nested ResolvedGroupingSetList is invalid";
    GOOGLESQL_RETURN_IF_ERROR(ValidateAndExtractNonCompositeGroupingSetColumns(
        group_by_columns, grouping_set_base.get(), grouping_sets_all_columns));
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateGroupingSetList(
    absl::Span<const std::unique_ptr<const ResolvedGroupingSetBase>>
        grouping_set_list,
    absl::Span<const std::unique_ptr<const ResolvedColumnRef>>
        rollup_column_list,
    absl::Span<const std::unique_ptr<const ResolvedComputedColumn>>
        group_by_list) {
  if (!grouping_set_list.empty()) {
    // This validation is true when only ROLLUP exists in the query.
    // TODO: Add more validations when cube/rollup/grouping sets
    // can work together.
    if (!rollup_column_list.empty()) {
      // There should be a grouping set for every prefix of the rollup list,
      // including the empty one.
      VALIDATOR_RET_CHECK_EQ(grouping_set_list.size(),
                             rollup_column_list.size() + 1);
    }
    std::set<ResolvedColumn> group_by_columns;
    for (const auto& group_by_column : group_by_list) {
      group_by_columns.insert(group_by_column->column());
    }

    std::set<ResolvedColumn> rollup_columns;
    for (const auto& column_ref : rollup_column_list) {
      GOOGLESQL_RETURN_IF_ERROR(CheckColumnIsPresentInColumnSet(column_ref->column(),
                                                      group_by_columns));
      rollup_columns.insert(column_ref->column());
    }
    // This won't be true when grouping sets, rollup, and cube exist. Keeping
    // this check here to make sure that we don't break any contract before
    // grouping sets are ready. Once grouping sets are ready, we should remove
    // this check completely.
    // TODO: Remove this check once grouping sets are ready.
    if (!rollup_columns.empty()) {
      // All group by columns should also be rollup columns. We can't use
      // std::set_intersect because ResolvedColumn does not support assignment.
      for (const ResolvedColumn& group_by_column : group_by_columns) {
        GOOGLESQL_RETURN_IF_ERROR(
            CheckColumnIsPresentInColumnSet(group_by_column, rollup_columns));
      }
    }

    // All columns used in grouping sets, rollup and cube.
    std::set<ResolvedColumn> grouping_sets_all_columns;
    if (grouping_set_list.size() == 1 &&
        grouping_set_list[0]->Is<ResolvedGroupingSetProduct>()) {
      // The input query has multiple grouping sets and the effective grouping
      // set list is the cross product.
      VALIDATOR_RET_CHECK(
          language_options_.LanguageFeatureEnabled(FEATURE_MULTI_GROUPING_SETS))
          << "Multi-grouping sets are only supported when "
             "FEATURE_MULTI_GROUPING_SETS is enabled";

      const auto* grouping_set_product =
          grouping_set_list[0]->GetAs<ResolvedGroupingSetProduct>();
      for (const auto& grouping_set : grouping_set_product->input_list()) {
        GOOGLESQL_RETURN_IF_ERROR(ValidateAndExtractGroupingSetColumns(
            group_by_columns, grouping_set.get(), &grouping_sets_all_columns));
      }
    } else {
      // Concatenation of each grouping sets.
      for (const auto& grouping_set_base : grouping_set_list) {
        VALIDATOR_RET_CHECK(
            !(grouping_set_base->Is<ResolvedGroupingSetList>() ||
              grouping_set_base->Is<ResolvedGroupingSetProduct>()))
            << "Unexpected ResolvedGroupingSetList or "
               "ResolvedGroupingSetProduct in the grouping set list."
               "ResolvedGroupingSetList and ResolvedGroupingSetProduct are "
               "only expected when FEATURE_MULTI_GROUPING_SETS is "
               "enabled and the input has multiple grouping sets";
        GOOGLESQL_RETURN_IF_ERROR(ValidateAndExtractNonCompositeGroupingSetColumns(
            group_by_columns, grouping_set_base.get(),
            &grouping_sets_all_columns));
      }
    }
    // Verify that all group by columns are also in grouping sets/rollup/cube.
    for (const ResolvedColumn& column : group_by_columns) {
      GOOGLESQL_RETURN_IF_ERROR(
          CheckColumnIsPresentInColumnSet(column, grouping_sets_all_columns));
    }
  } else {
    // Presence of grouping sets should indicate that there is a rollup list.
    VALIDATOR_RET_CHECK(rollup_column_list.empty());
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedAggregateFunctionCall(
    const std::set<ResolvedColumn>& visible_columns,
    const std::set<ResolvedColumn>& visible_parameters,
    const ResolvedAggregateFunctionCall* aggregate_function_call) {
  PushErrorContext push(this, aggregate_function_call);
  VALIDATOR_RET_CHECK_EQ(aggregate_function_call->generic_argument_list_size(),
                         0)
      << "Aggregate functions do not support generic arguments yet";
  if (!aggregate_function_call->group_by_aggregate_list().empty()) {
    VALIDATOR_RET_CHECK(!aggregate_function_call->group_by_list().empty())
        << "Multi-level aggregation requires a non-empty group by list";
  }
  if (!aggregate_function_call->group_by_hint_list().empty()) {
    VALIDATOR_RET_CHECK(!aggregate_function_call->group_by_list().empty())
        << "Group by hints can only be specified with a non-empty group by "
           "list";
  }
  if (!aggregate_function_call->group_by_list().empty()) {
    VALIDATOR_RET_CHECK(language_options_.LanguageFeatureEnabled(
        FEATURE_MULTILEVEL_AGGREGATION))
        << "Aggregate functions can only have a group_by_list when "
           "FEATURE_MULTILEVEL_AGGREGATION is enabled";
  }

  if (aggregate_function_call->where_expr() != nullptr) {
    VALIDATOR_RET_CHECK(
        language_options_.LanguageFeatureEnabled(FEATURE_AGGREGATE_FILTERING))
        << "Aggregate functions can only have a where_expr when "
           "FEATURE_AGGREGATE_FILTERING is enabled";
    // Validate the WHERE expression against the current visible columns and
    // parameters. Since the WHERE expression operates on the pre-grouping
    // input, we do not need to consider columns provided by the `group_by_list`
    // or `group_by_aggregate_list`.
    GOOGLESQL_RETURN_IF_ERROR(ValidateBoolExpr(visible_columns, visible_parameters,
                                     aggregate_function_call->where_expr()));
  }

  if (aggregate_function_call->having_expr() != nullptr) {
    VALIDATOR_RET_CHECK(
        language_options_.LanguageFeatureEnabled(FEATURE_AGGREGATE_FILTERING))
        << "Aggregate functions can only have a having_expr when "
           "FEATURE_AGGREGATE_FILTERING is enabled";
    VALIDATOR_RET_CHECK(!aggregate_function_call->group_by_list().empty())
        << "Aggregate functions with having_expr must have a non-empty "
           "group_by_list";
  }

  std::set<ResolvedColumn> group_rows_columns;
  std::set<ResolvedColumn> multi_level_aggregation_columns;
  const std::set<ResolvedColumn>* function_visible_columns = &visible_columns;
  bool is_multi_level_aggregate_function = false;
  if (!aggregate_function_call->group_by_list().empty()) {
    is_multi_level_aggregate_function = true;
    // Validate the `group_by_hint_list`.
    GOOGLESQL_RETURN_IF_ERROR(
        ValidateHintList(aggregate_function_call->group_by_hint_list()));
    // Validate the `group_by_list`.
    for (const auto& new_grouping_column :
         aggregate_function_call->group_by_list()) {
      GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(*function_visible_columns,
                                           visible_parameters,
                                           new_grouping_column->expr()));
    }
    // Validate the `group_by_aggregate_list`.
    for (const auto& aggregate_computed_column :
         aggregate_function_call->group_by_aggregate_list()) {
      // Check that the aggregate column is an AggregateFunctionCall, and
      // recursively validate it.
      VALIDATOR_RET_CHECK(aggregate_computed_column->expr()
                              ->Is<ResolvedAggregateFunctionCall>());
      GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(*function_visible_columns,
                                           visible_parameters,
                                           aggregate_computed_column->expr()));
      std::set<ResolvedColumn> available_side_effect_columns;
      if (aggregate_computed_column->Is<ResolvedDeferredComputedColumn>()) {
        available_side_effect_columns.insert(
            aggregate_computed_column->GetAs<ResolvedDeferredComputedColumn>()
                ->side_effect_column());
      }
      GOOGLESQL_RET_CHECK(aggregate_computed_column->Is<ResolvedComputedColumnImpl>());
      GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedAggregateComputedColumn(
          aggregate_computed_column->GetAs<ResolvedComputedColumnImpl>(),
          *function_visible_columns, visible_parameters,
          available_side_effect_columns));
    }
    // The set of visible columns for any arguments to the multi-level aggregate
    // function is the union of the columns produced by the `group_by_list` and
    // the `group_by_aggregate_list`.
    GOOGLESQL_RETURN_IF_ERROR(AddColumnsFromComputedColumnList(
        aggregate_function_call->group_by_list(),
        &multi_level_aggregation_columns));
    GOOGLESQL_RETURN_IF_ERROR(AddColumnsFromComputedColumnList(
        aggregate_function_call->group_by_aggregate_list(),
        &multi_level_aggregation_columns));
    function_visible_columns = &multi_level_aggregation_columns;

    // Validate the having_expr if present using the updated
    // `function_visible_columns`. The having_expr operates on the post-grouping
    // input, so it should only see the columns produced by the `group_by_list`
    // and `group_by_aggregate_list`.
    if (aggregate_function_call->having_expr() != nullptr) {
      GOOGLESQL_RETURN_IF_ERROR(ValidateBoolExpr(*function_visible_columns,
                                       visible_parameters,
                                       aggregate_function_call->having_expr()));
    }
  }

  googlesql_base::VarSetter<bool> disallow_aggregate_resolved_arg_refs_setter(
      &disallow_aggregate_resolved_arg_refs_,
      is_multi_level_aggregate_function);
  googlesql_base::VarSetter<bool> disallow_expression_columns_setter(
      &disallow_expression_columns_, is_multi_level_aggregate_function);
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedFunctionCallBase(
      *function_visible_columns, visible_parameters, aggregate_function_call));

  // Mark fields as visited.
  aggregate_function_call->distinct();
  aggregate_function_call->null_handling_modifier();

  if (aggregate_function_call->having_modifier() != nullptr) {
    VALIDATOR_RET_CHECK_EQ(
        aggregate_function_call->group_by_aggregate_list_size(), 0)
        << "Multi-level aggregation does not support HAVING modifier";
    VALIDATOR_RET_CHECK_EQ(aggregate_function_call->group_by_list_size(), 0)
        << "Multi-level aggregation does not support HAVING modifier";
    auto* having_modifier = aggregate_function_call->having_modifier();
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(*function_visible_columns,
                                         visible_parameters,
                                         having_modifier->having_expr()));

    having_modifier->kind();  // Mark visited.
  }
  // Since some aggregate validations depends on the input scan,
  // the ORDER BY and LIMIT to the aggregate arguments are not validated
  // here, but in ValidateResolvedAggregateScan().
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedAnalyticFunctionCall(
    const std::set<ResolvedColumn>& visible_columns,
    const std::set<ResolvedColumn>& visible_parameters,
    const ResolvedAnalyticFunctionCall* call) {
  PushErrorContext push(this, call);
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedFunctionCallBase(visible_columns,
                                                   visible_parameters, call));

  if (call->where_expr() != nullptr) {
    VALIDATOR_RET_CHECK(
        language_options_.LanguageFeatureEnabled(FEATURE_AGGREGATE_FILTERING))
        << "Analytic functions can only have a where_expr when "
           "FEATURE_AGGREGATE_FILTERING is enabled";
    GOOGLESQL_RETURN_IF_ERROR(ValidateBoolExpr(visible_columns, visible_parameters,
                                     call->where_expr()));
  }

  // Mark fields as visited.
  call->distinct();
  call->null_handling_modifier();

  // Since some window frame validations depends on the window ORDER BY,
  // the window frame is not validated here, but in
  // ValidateResolvedWindow().
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedMakeStruct(
    const std::set<ResolvedColumn>& visible_columns,
    const std::set<ResolvedColumn>& visible_parameters,
    const ResolvedMakeStruct* expr) {
  PushErrorContext push(this, expr);
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExprList(
      visible_columns, visible_parameters,
      expr->GetAs<ResolvedMakeStruct>()->field_list()));
  VALIDATOR_RET_CHECK(expr->type()->IsStruct());
  const StructType* struct_type = expr->type()->AsStruct();
  for (int i = 0; i < struct_type->num_fields(); ++i) {
    VALIDATOR_RET_CHECK(
        struct_type->field(i).type->Equals(expr->field_list(i)->type()))
        << "Field type mismatch for index " << i
        << "; expected: " << struct_type->field(i).type->DebugString()
        << " but found " << expr->field_list(i)->type()->DebugString();
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedGetProtoFieldExpr(
    const std::set<ResolvedColumn>& visible_columns,
    const std::set<ResolvedColumn>& visible_parameters,
    const ResolvedGetProtoField* get_proto_field) {
  PushErrorContext push(this, get_proto_field);
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(visible_columns, visible_parameters,
                                       get_proto_field->expr()));
  VALIDATOR_RET_CHECK(get_proto_field->expr()->type()->IsProto());
  // Match proto full_name rather than using pointer equality because
  // the FieldDescriptor is allowed to be an extension, which may
  // come from a different DescriptorPool.
  VALIDATOR_RET_CHECK_EQ(
      get_proto_field->expr()->type()->AsProto()->descriptor()->full_name(),
      get_proto_field->field_descriptor()->containing_type()->full_name())
      << "Mismatched proto message "
      << get_proto_field->expr()->type()->DebugString() << " and field "
      << get_proto_field->field_descriptor()->full_name();
  if (get_proto_field->field_descriptor()->is_required() ||
      get_proto_field->get_has_bit()) {
    VALIDATOR_RET_CHECK(!get_proto_field->default_value().is_valid());
    VALIDATOR_RET_CHECK(!get_proto_field->return_default_value_when_unset());
  } else {
    if (get_proto_field->return_default_value_when_unset()) {
      VALIDATOR_RET_CHECK(!get_proto_field->type()->IsProto());
      VALIDATOR_RET_CHECK(ProtoType::GetUseDefaultsExtension(
                              get_proto_field->field_descriptor()) ||
                          !get_proto_field->field_descriptor()->has_presence());
    }
    VALIDATOR_RET_CHECK(get_proto_field->default_value().is_valid());
    VALIDATOR_RET_CHECK(get_proto_field->type()->Equals(
        get_proto_field->default_value().type()));
  }
  if (get_proto_field->get_has_bit()) {
    VALIDATOR_RET_CHECK(get_proto_field->type()->IsBool());
  }
  get_proto_field->format();  // Mark field as visited.
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedGetJsonFieldExpr(
    const std::set<ResolvedColumn>& visible_columns,
    const std::set<ResolvedColumn>& visible_parameters,
    const ResolvedGetJsonField* get_json_field) {
  PushErrorContext push(this, get_json_field);
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(visible_columns, visible_parameters,
                                       get_json_field->expr()));
  VALIDATOR_RET_CHECK(get_json_field->expr()->type()->IsJson());
  VALIDATOR_RET_CHECK(!get_json_field->field_name().empty());
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedGetRowFieldExpr(
    const std::set<ResolvedColumn>& visible_columns,
    const std::set<ResolvedColumn>& visible_parameters,
    const ResolvedGetRowField* get_row_field) {
  PushErrorContext push(this, get_row_field);
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(visible_columns, visible_parameters,
                                       get_row_field->expr()));
  VALIDATOR_RET_CHECK(get_row_field->expr()->type()->IsSingleRow());
  VALIDATOR_RET_CHECK(
      get_row_field->type()->Equals(get_row_field->column()->GetType()));
  // It would be nice to check that the Column came from the right Table
  // but Column doesn't have a method to give its Table.
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedFlatten(
    const std::set<ResolvedColumn>& visible_columns,
    const std::set<ResolvedColumn>& visible_parameters,
    const ResolvedFlatten* flatten) {
  PushErrorContext push(this, flatten);
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(visible_columns, visible_parameters,
                                       flatten->expr()));
  GOOGLESQL_ASSIGN_OR_RETURN(const Type* scalar_type,
                   flatten->expr()->type()->GetElementType());
  VALIDATOR_RET_CHECK(scalar_type->IsProto() || scalar_type->IsStruct() ||
                      scalar_type->IsJson() || scalar_type->IsGraphElement() ||
                      scalar_type->IsSingleRow());

  bool seen_proto = scalar_type->IsProto();
  bool seen_json = scalar_type->IsJson();
  bool seen_graph_element = scalar_type->IsGraphElement();
  for (const auto& get_field_expr : flatten->get_field_list()) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(visible_columns, visible_parameters,
                                         get_field_expr.get()));
    auto validate_get_field = [&](const ResolvedExpr& expr) -> absl::Status {
      if (expr.node_kind() == RESOLVED_GET_STRUCT_FIELD) {
        VALIDATOR_RET_CHECK(!seen_proto);
        VALIDATOR_RET_CHECK(!seen_json);
        VALIDATOR_RET_CHECK(!seen_graph_element);
        VALIDATOR_RET_CHECK_EQ(
            expr.GetAs<ResolvedGetStructField>()->expr()->node_kind(),
            RESOLVED_FLATTENED_ARG);
      } else if (expr.node_kind() == RESOLVED_GET_JSON_FIELD) {
        seen_json = true;
        VALIDATOR_RET_CHECK_EQ(
            expr.GetAs<ResolvedGetJsonField>()->expr()->node_kind(),
            RESOLVED_FLATTENED_ARG);
      } else if (expr.node_kind() == RESOLVED_GET_PROTO_FIELD) {
        VALIDATOR_RET_CHECK(!seen_json);
        seen_proto = true;
        VALIDATOR_RET_CHECK_EQ(
            expr.GetAs<ResolvedGetProtoField>()->expr()->node_kind(),
            RESOLVED_FLATTENED_ARG);
      } else if (expr.node_kind() == RESOLVED_GET_ROW_FIELD) {
        VALIDATOR_RET_CHECK(!seen_proto);
        VALIDATOR_RET_CHECK(!seen_json);
        VALIDATOR_RET_CHECK_EQ(
            expr.GetAs<ResolvedGetRowField>()->expr()->node_kind(),
            RESOLVED_FLATTENED_ARG);
      } else if (expr.node_kind() == RESOLVED_GRAPH_GET_ELEMENT_PROPERTY) {
        VALIDATOR_RET_CHECK(!seen_proto);
        VALIDATOR_RET_CHECK(!seen_json);
        seen_graph_element = true;
        VALIDATOR_RET_CHECK_EQ(
            expr.GetAs<ResolvedGraphGetElementProperty>()->expr()->node_kind(),
            RESOLVED_FLATTENED_ARG);
      } else {
        VALIDATOR_RET_CHECK_FAIL()
            << "Unexpected node kind: " << expr.DebugString();
      }
      return absl::OkStatus();
    };
    if (get_field_expr->Is<ResolvedFunctionCall>()) {
      // The get field calls can optionally have an array offset which always
      // take two arguments, the array and the offset.
      const ResolvedFunctionCall* resolved_function_call =
          get_field_expr->GetAs<ResolvedFunctionCall>();
      const std::string& function_name =
          resolved_function_call->function()->Name();
      VALIDATOR_RET_CHECK(function_name == "$array_at_offset" ||
                          function_name == "$array_at_ordinal" ||
                          function_name == "$safe_array_at_offset" ||
                          function_name == "$safe_array_at_ordinal")
          << function_name;
      VALIDATOR_RET_CHECK_EQ(2, resolved_function_call->argument_list_size());
      GOOGLESQL_RETURN_IF_ERROR(
          validate_get_field(*resolved_function_call->argument_list(0)));
    } else {
      GOOGLESQL_RETURN_IF_ERROR(validate_get_field(*get_field_expr));
    }
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedFlattenedArg(
    const ResolvedFlattenedArg* flattened_arg) {
  PushErrorContext push(this, flattened_arg);
  VALIDATOR_RET_CHECK(flattened_arg->type()->IsProto() ||
                      flattened_arg->type()->IsStruct() ||
                      flattened_arg->type()->IsJson() ||
                      flattened_arg->type()->IsGraphElement() ||
                      flattened_arg->type()->IsSingleRow())
      << flattened_arg->type()->DebugString();
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedReplaceField(
    const std::set<ResolvedColumn>& visible_columns,
    const std::set<ResolvedColumn>& visible_parameters,
    const ResolvedReplaceField* replace_field) {
  PushErrorContext push(this, replace_field);
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(visible_columns, visible_parameters,
                                       replace_field->expr()));

  for (const std::unique_ptr<const ResolvedReplaceFieldItem>&
           replace_field_item : replace_field->replace_field_item_list()) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(visible_columns, visible_parameters,
                                         replace_field_item->expr()));

    VALIDATOR_RET_CHECK(!replace_field_item->struct_index_path().empty() ||
                        !replace_field_item->proto_field_path().empty());
    const StructType* current_struct_type =
        replace_field->expr()->type()->AsStruct();
    const googlesql::Type* last_struct_field_type;
    for (const int field_index : replace_field_item->struct_index_path()) {
      // Ensure that the field path is valid by checking field containment.
      VALIDATOR_RET_CHECK(current_struct_type != nullptr);
      VALIDATOR_RET_CHECK_GE(field_index, 0);
      VALIDATOR_RET_CHECK_LT(field_index, current_struct_type->num_fields());
      last_struct_field_type = current_struct_type->field(field_index).type;
      current_struct_type = last_struct_field_type->AsStruct();
    }

    if (!replace_field_item->proto_field_path().empty()) {
      const absl::string_view base_proto_name =
          replace_field_item->struct_index_path().empty()
              ? replace_field->expr()
                    ->type()
                    ->AsProto()
                    ->descriptor()
                    ->full_name()
              : last_struct_field_type->AsProto()->descriptor()->full_name();
      GOOGLESQL_RETURN_IF_ERROR(ValidateProtoFieldPath(
          base_proto_name, replace_field_item->proto_field_path()));
    }
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedGetProtoOneof(
    const std::set<ResolvedColumn>& visible_columns,
    const std::set<ResolvedColumn>& visible_parameters,
    const ResolvedGetProtoOneof* get_proto_oneof) {
  PushErrorContext push(this, get_proto_oneof);
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(visible_columns, visible_parameters,
                                       get_proto_oneof->expr()));
  VALIDATOR_RET_CHECK(get_proto_oneof->expr()->type()->IsProto());
  VALIDATOR_RET_CHECK(get_proto_oneof->type()->IsString());
  VALIDATOR_RET_CHECK_EQ(
      get_proto_oneof->expr()->type()->AsProto()->descriptor()->full_name(),
      get_proto_oneof->oneof_descriptor()->containing_type()->full_name())
      << "Mismatched proto message "
      << get_proto_oneof->expr()->type()->DebugString() << " and oneof "
      << get_proto_oneof->oneof_descriptor()->full_name();
  VALIDATOR_RET_CHECK_NE(
      get_proto_oneof->expr()->type()->AsProto()->descriptor()->FindOneofByName(
          get_proto_oneof->oneof_descriptor()->name()),
      nullptr);
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedInlineLambda(
    const std::set<ResolvedColumn>& visible_columns,
    const std::set<ResolvedColumn>& visible_parameters,
    const ResolvedInlineLambda* resolved_lambda) {
  PushErrorContext push(this, resolved_lambda);
  // Build column set from argument list of lambda.
  std::set<ResolvedColumn> argument_columns;
  for (const ResolvedColumn& argument : resolved_lambda->argument_list()) {
    argument_columns.insert(argument);
  }

  // Build column set from parameter list of lambda.
  std::set<ResolvedColumn> body_visible_parameters;
  for (const std::unique_ptr<const ResolvedColumnRef>& column_ref :
       resolved_lambda->parameter_list()) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(visible_columns, visible_parameters,
                                         column_ref.get()));
    body_visible_parameters.insert(column_ref->column());
  }

  return ValidateResolvedExpr(argument_columns, body_visible_parameters,
                              resolved_lambda->body());
}

absl::Status Validator::ValidateResolvedSubqueryExpr(
    const std::set<ResolvedColumn>& visible_columns,
    const std::set<ResolvedColumn>& visible_parameters,
    const ResolvedSubqueryExpr* resolved_subquery_expr) {
  PushErrorContext push(this, resolved_subquery_expr);
  ResolvedSubqueryExpr::SubqueryType type =
      resolved_subquery_expr->subquery_type();
  VALIDATOR_RET_CHECK_EQ((type == ResolvedSubqueryExpr::IN) ||
                             (type == ResolvedSubqueryExpr::LIKE_ANY) ||
                             (type == ResolvedSubqueryExpr::LIKE_ALL) ||
                             (type == ResolvedSubqueryExpr::NOT_LIKE_ANY) ||
                             (type == ResolvedSubqueryExpr::NOT_LIKE_ALL),
                         resolved_subquery_expr->in_expr() != nullptr)
      << "Subquery expressions of "
      << ResolvedSubqueryExprEnums::SubqueryType_Name(type)
      << " should have <in_expr> populated";

  if (resolved_subquery_expr->in_expr() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(visible_columns, visible_parameters,
                                         resolved_subquery_expr->in_expr()));
  }

  if (!resolved_subquery_expr->in_collation().Empty()) {
    VALIDATOR_RET_CHECK_EQ(type, ResolvedSubqueryExpr::IN)
        << "<in_collation> should only be populated for Subquery expressions "
           "of IN type. Subquery expression type is "
        << ResolvedSubqueryExprEnums::SubqueryType_Name(type);
    VALIDATOR_RET_CHECK_NE(resolved_subquery_expr->in_expr(), nullptr);
    // We only check that <in_collation> is compatible with the type of
    // <in_expr> here given the type of subquery column should match that of
    // <in_expr> (which is checked afterwards).
    VALIDATOR_RET_CHECK(
        resolved_subquery_expr->in_collation().HasCompatibleStructure(
            resolved_subquery_expr->in_expr()->type()));
  }

  std::set<ResolvedColumn> subquery_parameters;
  for (const std::unique_ptr<const ResolvedColumnRef>& column_ref :
       resolved_subquery_expr->parameter_list()) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(visible_columns, visible_parameters,
                                         column_ref.get()));
    VALIDATOR_RET_CHECK(
        subquery_parameters.insert(column_ref->column()).second);
  }
  // The subquery sees only its own parameters, not the
  // visible_parameters that were passed in.
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedScan(resolved_subquery_expr->subquery(),
                                       subquery_parameters));

  if (options_.validate_no_unreferenced_subquery_params) {
    // Validate that the subquery does not include parameters that are
    // unreferenced.
    std::vector<std::unique_ptr<const ResolvedColumnRef>> refs;
    GOOGLESQL_RETURN_IF_ERROR(
        CollectColumnRefs(*resolved_subquery_expr->subquery(), &refs));
    absl::flat_hash_set<int> referenced_column_ids;
    for (auto& ref : refs) {
      if (ref->is_correlated()) {
        referenced_column_ids.insert(ref->column().column_id());
      }
    }
    for (auto& param : subquery_parameters) {
      VALIDATOR_RET_CHECK(referenced_column_ids.contains(param.column_id()))
          << "Expression subquery does not reference correlated column "
          << "parameter: " << param.DebugString()
      << " subquery: " << resolved_subquery_expr->DebugString();
    }
  }

  switch (resolved_subquery_expr->subquery_type()) {
    case ResolvedSubqueryExpr::SCALAR:
    case ResolvedSubqueryExpr::ARRAY:
    case ResolvedSubqueryExpr::IN:
      VALIDATOR_RET_CHECK_EQ(
          resolved_subquery_expr->subquery()->column_list_size(), 1)
          << "Expression subquery must produce exactly one column";
      if (resolved_subquery_expr->in_expr() != nullptr) {
        const Type* in_expr_type = resolved_subquery_expr->in_expr()->type();
        const Type* in_subquery_type =
            resolved_subquery_expr->subquery()->column_list(0).type();
        VALIDATOR_RET_CHECK(in_expr_type->SupportsEquality());
        VALIDATOR_RET_CHECK(in_subquery_type->SupportsEquality());

        const bool argument_types_equal =
            in_expr_type->Equals(in_subquery_type);
        const bool argument_types_int64_and_uint64 =
            (in_expr_type->IsInt64() && in_subquery_type->IsUint64()) ||
            (in_expr_type->IsUint64() && in_subquery_type->IsInt64());
        VALIDATOR_RET_CHECK(argument_types_equal ||
                            argument_types_int64_and_uint64);
      }
      break;
    case ResolvedSubqueryExpr::LIKE_ANY:
    case ResolvedSubqueryExpr::LIKE_ALL:
    case ResolvedSubqueryExpr::NOT_LIKE_ANY:
    case ResolvedSubqueryExpr::NOT_LIKE_ALL: {
      VALIDATOR_RET_CHECK(language_options_.LanguageFeatureEnabled(
          FEATURE_LIKE_ANY_SOME_ALL_SUBQUERY))
          << "LIKE ANY/ALL subquery is not enabled";
      VALIDATOR_RET_CHECK_EQ(
          resolved_subquery_expr->subquery()->column_list_size(), 1)
          << "Expression subquery must produce exactly one column";
      const Type* like_expr_type = resolved_subquery_expr->in_expr()->type();
      const Type* like_subquery_type =
          resolved_subquery_expr->subquery()->column_list(0).type();
      VALIDATOR_RET_CHECK(like_expr_type->Equivalent(like_subquery_type));
      VALIDATOR_RET_CHECK(like_expr_type->IsString() ||
                          like_expr_type->IsBytes());
      break;
    }
    case ResolvedSubqueryExpr::EXISTS:
      break;
  }

  GOOGLESQL_RETURN_IF_ERROR(ValidateHintList(resolved_subquery_expr->hint_list()));

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedWithExpr(
    const std::set<ResolvedColumn>& visible_columns,
    const std::set<ResolvedColumn>& visible_parameters,
    const ResolvedWithExpr* resolved_with_expr) {
  std::set<ResolvedColumn> local_visible_columns = visible_columns;
  for (const auto& assignment : resolved_with_expr->assignment_list()) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(
        local_visible_columns, visible_parameters, assignment->expr()));
    GOOGLESQL_RETURN_IF_ERROR(
        AddColumnFromComputedColumn(assignment.get(), &local_visible_columns));
  }
  return ValidateResolvedExpr(local_visible_columns, visible_parameters,
                              resolved_with_expr->expr());
}

absl::Status Validator::ValidateResolvedComputedColumn(
    const std::set<ResolvedColumn>& visible_columns,
    const std::set<ResolvedColumn>& visible_parameters,
    const ResolvedComputedColumnBase* computed_column) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  VALIDATOR_RET_CHECK(nullptr != computed_column);

  VALIDATOR_RET_CHECK(computed_column->Is<ResolvedComputedColumnImpl>());
  auto computed_col = computed_column->GetAs<ResolvedComputedColumnImpl>();
  PushErrorContext push(this, computed_column);

  const ResolvedExpr* expr = computed_col->expr();
  VALIDATOR_RET_CHECK(nullptr != expr);
  GOOGLESQL_RETURN_IF_ERROR(
      ValidateResolvedExpr(visible_columns, visible_parameters, expr));
  VALIDATOR_RET_CHECK(computed_col->column().type()->Equals(expr->type()))
      << computed_col->DebugString()  // includes newline
      << "column: " << computed_col->column().DebugString() << " with type "
      << computed_col->column().type()->DebugString()
      << "\nexpr type: " << expr->type()->DebugString();

  // TODO: Enable this check.
  // VALIDATOR_RET_CHECK(AnnotationMap::HasEqualAnnotations(
  //     computed_column->column().annotated_type().annotation_map,
  //     expr->annotated_type().annotation_map, CollationAnnotation::GetId()));
  // TODO: Add a more general check to handle any ResolvedExpr
  // (not just RESOLVED_COLUMN_REF).  The ResolvedExpr should not
  // reference the ResolvedColumn to be computed.
  if (computed_col->expr()->node_kind() == RESOLVED_COLUMN_REF &&
      computed_col->column() ==
          computed_col->expr()->GetAs<ResolvedColumnRef>()->column()) {
    return InternalErrorBuilder()
           << "ResolvedComputedColumn expression cannot reference itself: "
           << computed_col->DebugString();
  }

  std::set<ResolvedColumn> available_side_effect_columns;
  if (computed_column->Is<ResolvedDeferredComputedColumn>()) {
    VALIDATOR_RET_CHECK(language_options_.LanguageFeatureEnabled(
        FEATURE_ENFORCE_CONDITIONAL_EVALUATION))
        << "ResolvedDeferredComputedColumns is only used when "
           "FEATURE_ENFORCE_CONDITIONAL_EVALUATION is enabled.";
    const auto* deferred_computed_column =
        computed_column->GetAs<ResolvedDeferredComputedColumn>();
    available_side_effect_columns.insert(
        deferred_computed_column->side_effect_column());
    VALIDATOR_RET_CHECK_EQ(
        deferred_computed_column->side_effect_column().type()->kind(),
        TYPE_BYTES);

    if (deferred_computed_column->expr()->Is<ResolvedColumnRef>() &&
        deferred_computed_column->side_effect_column() ==
            computed_col->expr()->GetAs<ResolvedColumnRef>()->column()) {
      return InternalErrorBuilder()
             << "ResolvedDeferredComputedColumn expression cannot reference "
                "its own side effect column: "
             << computed_col->DebugString();
    }
  }

  if (expr->Is<ResolvedAggregateFunctionCall>()) {
    return ValidateResolvedAggregateComputedColumn(
        computed_col, visible_columns, visible_parameters,
        available_side_effect_columns);
  }

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedComputedColumnList(
    const std::set<ResolvedColumn>& visible_columns,
    const std::set<ResolvedColumn>& visible_parameters,
    absl::Span<const std::unique_ptr<const ResolvedComputedColumn>>
        computed_column_list) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  for (const auto& computed_column : computed_column_list) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedComputedColumn(
        visible_columns, visible_parameters, computed_column.get()));
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedComputedColumnList(
    const std::set<ResolvedColumn>& visible_columns,
    const std::set<ResolvedColumn>& visible_parameters,
    absl::Span<const std::unique_ptr<const ResolvedComputedColumnBase>>
        computed_column_list) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  for (const auto& computed_column : computed_column_list) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedComputedColumn(
        visible_columns, visible_parameters, computed_column.get()));
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateGroupingFunctionCallList(
    const std::set<ResolvedColumn>& visible_columns,
    absl::Span<const std::unique_ptr<const ResolvedGroupingCall>>
        grouping_call_list,
    const std::set<ResolvedColumn>& group_by_columns) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  for (const auto& grouping_call : grouping_call_list) {
    VALIDATOR_RET_CHECK(grouping_call->output_column().type()->IsInt64());
    GOOGLESQL_RETURN_IF_ERROR(CheckColumnIsPresentInColumnSet(
        grouping_call->output_column(), visible_columns));
    GOOGLESQL_RETURN_IF_ERROR(CheckColumnIsPresentInColumnSet(
        grouping_call->group_by_column()->column(), group_by_columns));
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedOutputColumn(
    const std::set<ResolvedColumn>& visible_columns,
    const ResolvedOutputColumn* output_column) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  VALIDATOR_RET_CHECK(nullptr != output_column);
  PushErrorContext push(this, output_column);
  GOOGLESQL_RETURN_IF_ERROR(CheckColumnIsPresentInColumnSet(output_column->column(),
                                                  visible_columns));
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedOutputColumnList(
    absl::Span<const ResolvedColumn> visible_columns,
    absl::Span<const std::unique_ptr<const ResolvedOutputColumn>>
        output_column_list,
    bool is_value_table) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  VALIDATOR_RET_CHECK(!output_column_list.empty())
      << "Statement must produce at least one output column";
  const std::set<ResolvedColumn> visible_columns_set(visible_columns.begin(),
                                                     visible_columns.end());
  for (const auto& output_column : output_column_list) {
    GOOGLESQL_RETURN_IF_ERROR(
        ValidateResolvedOutputColumn(visible_columns_set, output_column.get()));
  }
  if (is_value_table) {
    if (output_column_list.size() != 1) {
      return InternalErrorBuilder()
             << "Statement producing a value table must produce exactly one "
                "column; this one has "
             << output_column_list.size();
    }
    if (!in_templated_tvf_call_ &&
        !IsInternalAlias(output_column_list[0]->name())) {
      return InternalErrorBuilder()
             << "Statement producing a value table must produce an anonymous "
                "column; this one has name "
             << ToIdentifierLiteral(output_column_list[0]->name());
    }
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedOutputSchema(
    absl::Span<const ResolvedColumn> visible_columns,
    const ResolvedOutputSchema* output_schema) {
  return ValidateResolvedOutputColumnList(visible_columns,
                                          output_schema->output_column_list(),
                                          output_schema->is_value_table());
}

absl::Status Validator::AddColumnList(
    const ResolvedColumnList& column_list,
    std::set<ResolvedColumn>* visible_columns) {
  VALIDATOR_RET_CHECK_NE(visible_columns, nullptr);
  for (const ResolvedColumn& column : column_list) {
    GOOGLESQL_RETURN_IF_ERROR(CheckColumnIdWasCreated(column));
    visible_columns->insert(column);
  }
  return absl::OkStatus();
}

absl::Status Validator::AddColumnList(
    const ResolvedColumnList& column_list,
    absl::flat_hash_set<ResolvedColumn>* visible_columns) {
  VALIDATOR_RET_CHECK_NE(visible_columns, nullptr);
  for (const ResolvedColumn& column : column_list) {
    GOOGLESQL_RETURN_IF_ERROR(CheckColumnIdWasCreated(column));
    visible_columns->insert(column);
  }
  return absl::OkStatus();
}

absl::Status Validator::AddColumnFromComputedColumn(
    const ResolvedComputedColumnBase* computed_column,
    std::set<ResolvedColumn>* visible_columns) {
  VALIDATOR_RET_CHECK(nullptr != visible_columns && nullptr != computed_column);
  PushErrorContext push(this, computed_column);
  auto computed_col = computed_column->GetAs<ResolvedComputedColumnImpl>();
  GOOGLESQL_RETURN_IF_ERROR(CheckUniqueColumnId(computed_col->column()));
  visible_columns->insert(computed_col->column());
  if (computed_column->Is<ResolvedDeferredComputedColumn>()) {
    // This should have been already checked before, but repeating here for
    // future-proofing, in case someone calls this without calling
    // ValidateComputedColumn() first.
    VALIDATOR_RET_CHECK(language_options_.LanguageFeatureEnabled(
        FEATURE_ENFORCE_CONDITIONAL_EVALUATION))
        << "ResolvedDeferredComputedColumns is only used when "
           "FEATURE_ENFORCE_CONDITIONAL_EVALUATION is enabled.";

    const ResolvedColumn& side_effect_column =
        computed_column->GetAs<ResolvedDeferredComputedColumn>()
            ->side_effect_column();
    VALIDATOR_RET_CHECK(side_effect_column.type()->IsBytes());
    GOOGLESQL_RETURN_IF_ERROR(CheckUniqueColumnId(side_effect_column));
    visible_columns->insert(side_effect_column);
    unconsumed_side_effect_columns_.insert(side_effect_column.column_id());
  }
  return absl::OkStatus();
}

absl::Status Validator::AddGroupingFunctionCallColumn(
    const ResolvedColumn grouping_call_column,
    std::set<ResolvedColumn>* visible_columns) {
  VALIDATOR_RET_CHECK(nullptr != visible_columns);
  GOOGLESQL_RETURN_IF_ERROR(CheckUniqueColumnId(grouping_call_column));
  visible_columns->insert(grouping_call_column);
  return absl::OkStatus();
}

absl::Status Validator::AddColumnsFromComputedColumnList(
    absl::Span<const std::unique_ptr<const ResolvedComputedColumn>>
        computed_column_list,
    std::set<ResolvedColumn>* visible_columns) {
  VALIDATOR_RET_CHECK(nullptr != visible_columns);
  for (const auto& computed_column : computed_column_list) {
    GOOGLESQL_RETURN_IF_ERROR(
        AddColumnFromComputedColumn(computed_column.get(), visible_columns));
  }
  return absl::OkStatus();
}

absl::Status Validator::AddColumnsFromComputedColumnList(
    absl::Span<const std::unique_ptr<const ResolvedComputedColumnBase>>
        computed_column_list,
    std::set<ResolvedColumn>* visible_columns) {
  VALIDATOR_RET_CHECK(nullptr != visible_columns);
  for (const auto& computed_column : computed_column_list) {
    GOOGLESQL_RETURN_IF_ERROR(
        AddColumnFromComputedColumn(computed_column.get(), visible_columns));
  }
  return absl::OkStatus();
}

absl::Status Validator::AddColumnsFromGroupingCallList(
    absl::Span<const std::unique_ptr<const ResolvedGroupingCall>>
        grouping_call_list,
    std::set<ResolvedColumn>* visible_columns) {
  VALIDATOR_RET_CHECK(nullptr != visible_columns);
  for (const auto& grouping_call : grouping_call_list) {
    GOOGLESQL_RETURN_IF_ERROR(AddGroupingFunctionCallColumn(
        grouping_call->output_column(), visible_columns));
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateTableColumnTypeEquality(
    const Column& column, const ResolvedColumn& resolved_column,
    absl::string_view table_name, absl::string_view column_name) {
  VALIDATOR_RET_CHECK_EQ(column.GetType()->IsMeasureType(),
                         resolved_column.type()->IsMeasureType());

  if (column.GetType()->IsMeasureType()) {
    // Special case for MeasureType because each measure column
    // instantiation is a different MeasureType.
    //
    // TODO: b/460223066: Remove this special case once we finish formalizing
    // the MeasureType spec.
    const Type* column_result_type =
        column.GetType()->AsMeasure()->result_type();
    const Type* resolved_column_result_type =
        resolved_column.type()->AsMeasure()->result_type();
    VALIDATOR_RET_CHECK(
        column_result_type->Equals(resolved_column_result_type));
  } else {
    VALIDATOR_RET_CHECK(column.GetType()->Equals(resolved_column.type()))
        << "\nTable column " << column.FullName() << " type "
        << column.GetType()->DebugString() << "\nResolvedColumn "
        << resolved_column.DebugString() << " type "
        << resolved_column.type()->DebugString();
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateMeasureColumns(const ResolvedScan* scan,
                                               const Table* table) {
  VALIDATOR_RET_CHECK(
      language_options_.LanguageFeatureEnabled(FEATURE_ENABLE_MEASURES))
      << scan->node_kind_string()
      << "projects a measure column, but FEATURE_ENABLE_MEASURES is not "
         "enabled. Scan: "
      << scan->DebugString();
  // Measure tables must support NumColumns and GetColumn(i).
  VALIDATOR_RET_CHECK(table->HasColumnList());

  auto table_row_identity_columns = table->RowIdentityColumns();
  for (int i = 0; i < table->NumColumns(); ++i) {
    const Column* catalog_column = table->GetColumn(i);
    if (!catalog_column->GetType()->IsMeasureType()) {
      continue;
    }
    if (catalog_column->GetExpression().has_value() &&
        catalog_column->GetExpression()->RowIdentityColumns().has_value()) {
      continue;
    }
    VALIDATOR_RET_CHECK(table_row_identity_columns.has_value() &&
                        !table_row_identity_columns.value().empty())
        << "Table " << table->Name()
        << " contains measure column: " << catalog_column->Name()
        << ", but it does not have column-level row identity columns and "
           "the table also does not have table-level row identity columns.";
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedTableScan(
    const ResolvedTableScan* scan,
    const std::set<ResolvedColumn>& visible_parameters) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, scan);

  const Table* table = scan->table();
  VALIDATOR_RET_CHECK(nullptr != table);

  if (scan->for_system_time_expr() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr({}, visible_parameters,
                                         scan->for_system_time_expr()));
    VALIDATOR_RET_CHECK(scan->for_system_time_expr()->type()->IsTimestamp())
        << "TableScan has for_system_type_expr with non-TIMESTAMP type: "
        << scan->for_system_time_expr()->type()->DebugString();
  }

  // Make sure column ids are unique. If the table scan emits a measure column,
  // perform additional checks below.
  bool perform_measure_column_checks = false;
  for (const ResolvedColumn& column : scan->column_list()) {
    GOOGLESQL_RETURN_IF_ERROR(CheckUniqueColumnId(column));
    if (column.type() != nullptr && column.type()->IsMeasureType()) {
      perform_measure_column_checks = true;
    }
  }
  if (perform_measure_column_checks) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateMeasureColumns(scan, scan->table()));
  }

  VALIDATOR_RET_CHECK(scan->column_index_list().empty() ||
                      scan->table_column_list().empty())
      << "Cannot set both column_index_list and table_column_list";

  if (scan->read_as_row_type()) {
    // In `read_as_row_type` mode, we should have at most one column, and it
    // should have ROW type.
    VALIDATOR_RET_CHECK(table->GetColumnListMode() !=
                        Table::ColumnListMode::DEFAULT);
    VALIDATOR_RET_CHECK_LE(scan->column_list_size(), 1);
    VALIDATOR_RET_CHECK_EQ(scan->column_index_list_size(), 0);
    VALIDATOR_RET_CHECK_EQ(scan->table_column_list_size(), 0);

    for (const ResolvedColumn& column : scan->column_list()) {
      const Type* type = column.type();
      VALIDATOR_RET_CHECK(type->IsRow()) << type->DebugString();
      VALIDATOR_RET_CHECK_EQ(type->AsRow()->table(), table);
    }

  } else if (scan->table_column_list_size() != 0) {
    VALIDATOR_RET_CHECK_EQ(scan->column_index_list_size(), 0);
    VALIDATOR_RET_CHECK(!scan->read_as_row_type());
    VALIDATOR_RET_CHECK_EQ(scan->table_column_list_size(),
                           scan->column_list_size());
    for (int i = 0; i < scan->table_column_list_size(); ++i) {
      const Column* column = scan->table_column_list(i);
      const ResolvedColumn& resolved_column = scan->column_list(i);

      GOOGLESQL_RETURN_IF_ERROR(ValidateTableColumnTypeEquality(
          *column, resolved_column, table->Name(), column->Name()));
    }

  } else if (scan->column_index_list_size() != 0) {
    // Ideally, column_index_list should always be the same size as column_list.
    // However, for historical reasons, some clients don't provide a
    // column_index_list and they match the table column by ResolvedColumn name
    // (this violates the contract of ResolvedColumn name() which defines it as
    // semantically meaningless). Therefore there's an exception here to allow
    // an empty column_index_list. Once all the client migrations are done, this
    // exception should be removed.
    //
    // TODO: Remove the condition on this if.

    VALIDATOR_RET_CHECK_EQ(scan->table_column_list_size(), 0);
    VALIDATOR_RET_CHECK(!scan->read_as_row_type());
    VALIDATOR_RET_CHECK_EQ(scan->column_list_size(),
                           scan->column_index_list_size());
    const int num_columns = table->NumColumns();

    for (const int index : scan->column_index_list()) {
      VALIDATOR_RET_CHECK_GE(index, 0);
      VALIDATOR_RET_CHECK_LT(index, num_columns);
      VALIDATOR_RET_CHECK_NE(table->GetColumn(index), nullptr);
    }
  }

  VALIDATOR_RET_CHECK(scan->lock_mode() == nullptr ||
                      scan->lock_mode()->strength() >=
                          ResolvedLockMode::UPDATE);

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedJoinScan(
    const ResolvedJoinScan* scan,
    const std::set<ResolvedColumn>& visible_parameters) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, scan);

  VALIDATOR_RET_CHECK(nullptr != scan->left_scan());
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedScan(scan->left_scan(), visible_parameters));

  scan->join_type();  // Mark field as visited.

  std::set<ResolvedColumn> left_visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(
      AddColumnList(scan->left_scan()->column_list(), &left_visible_columns));

  std::set<ResolvedColumn> visible_paramerters_for_rhs;
  if (!scan->is_lateral()) {
    VALIDATOR_RET_CHECK(scan->parameter_list().empty());
    visible_paramerters_for_rhs = visible_parameters;
  } else {
    for (const auto& col_ref : scan->parameter_list()) {
      if (!col_ref->is_correlated()) {
        // If the ref is not already correlated, it must be available from the
        // left scan.
        VALIDATOR_RET_CHECK(
            googlesql_base::ContainsKey(left_visible_columns, col_ref->column()))
            << "Column must come from the left scan";
      } else {
        // If the ref is correlated, it cannot come from the left scan.
        VALIDATOR_RET_CHECK(
            !googlesql_base::ContainsKey(left_visible_columns, col_ref->column()))
            << "Column cannot come from the left scan";
      }
      visible_paramerters_for_rhs.insert(col_ref->column());
    }
  }

  VALIDATOR_RET_CHECK(nullptr != scan->right_scan());

  // For LATERAL joins, the RHS sees only the lateral list, not the outside
  // parameters.
  GOOGLESQL_RETURN_IF_ERROR(
      ValidateResolvedScan(scan->right_scan(), visible_paramerters_for_rhs));

  std::set<ResolvedColumn> right_visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(
      AddColumnList(scan->right_scan()->column_list(), &right_visible_columns));
  // Both left and right scans should not have any common column references
  // introduced in the visible set for the on_condition.
  VALIDATOR_RET_CHECK(!googlesql_base::SortedContainersHaveIntersection(
      left_visible_columns, right_visible_columns));

  const std::set<ResolvedColumn> visible_columns =
      googlesql_base::STLSetUnion(left_visible_columns, right_visible_columns);

  if (nullptr != scan->join_expr()) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(visible_columns, visible_parameters,
                                         scan->join_expr()));
    VALIDATOR_RET_CHECK(scan->join_expr()->type()->IsBool())
        << "JoinScan has join_expr with non-BOOL type: "
        << scan->join_expr()->type()->DebugString();
  } else {
    if (scan->join_type() == ResolvedJoinScan::LEFT) {
      VALIDATOR_RET_CHECK(scan->is_lateral())
          << "LEFT JOIN can omit the condition only if it is LATERAL";
    }
  }

  GOOGLESQL_RETURN_IF_ERROR(CheckColumnList(scan, visible_columns));

  return absl::OkStatus();
}

absl::Status Validator::ValidateTableNameArrayPathArrayScan(
    const ResolvedArrayScan* node) {
  VALIDATOR_RET_CHECK(node->input_scan() != nullptr);
  VALIDATOR_RET_CHECK(node->input_scan()->node_kind() == RESOLVED_TABLE_SCAN);
  VALIDATOR_RET_CHECK(!node->is_outer());
  VALIDATOR_RET_CHECK(node->array_offset_column() == nullptr);
  VALIDATOR_RET_CHECK(node->join_expr() == nullptr);

  // Check if the underlying column reference of array_expr is uncorrelated.
  // If we cannot find a column ref, it is not the shape we want.
  int unused_column_id;
  VALIDATOR_RET_CHECK(ContainsTableArrayNamePathWithFreeColumnRef(
      node->array_expr(), &unused_column_id));
  bool outputs_element_column = node->column_list_size() == 1 &&
                                node->column_list(0) == node->element_column();

  // ResolvedArrayScan might not have output column_list if array element
  // column does not show up in the final select list and gets pruned in
  // the resolved AST.
  VALIDATOR_RET_CHECK(node->column_list_size() == 0 || outputs_element_column);

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedArrayScan(
    const ResolvedArrayScan* scan,
    const std::set<ResolvedColumn>& visible_parameters) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, scan);

  if (scan->node_source() == kNodeSourceSingleTableArrayNamePath) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateTableNameArrayPathArrayScan(scan));
  }

  std::set<ResolvedColumn> visible_columns;
  if (nullptr != scan->input_scan()) {
    GOOGLESQL_RETURN_IF_ERROR(
        ValidateResolvedScan(scan->input_scan(), visible_parameters));
    GOOGLESQL_RETURN_IF_ERROR(
        AddColumnList(scan->input_scan()->column_list(), &visible_columns));
  }

  // Validate named argument `mode`.
  if (scan->array_zip_mode() != nullptr) {
    VALIDATOR_RET_CHECK(scan->array_zip_mode()->type()->IsEnum());
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(visible_columns, visible_parameters,
                                         scan->array_zip_mode()));
  }

  VALIDATOR_RET_CHECK_EQ(scan->array_expr_list_size(),
                         scan->element_column_list_size());
  for (int i = 0; i < scan->array_expr_list_size(); ++i) {
    VALIDATOR_RET_CHECK(scan->array_expr_list(i) != nullptr);
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(visible_columns, visible_parameters,
                                         scan->array_expr_list(i)));
    const Type* expr_type = scan->array_expr_list(i)->type();
    const Type* element_type = nullptr;
    if (expr_type->IsArray()) {
      element_type = expr_type->AsArray()->element_type();
    } else if (expr_type->IsRow()) {
      VALIDATOR_RET_CHECK(expr_type->AsRow()->IsJoin());
      element_type = expr_type->AsRow()->element_type();
      VALIDATOR_RET_CHECK_EQ(scan->array_expr_list_size(), 1);
      VALIDATOR_RET_CHECK(scan->array_offset_column() == nullptr)
          << "Can't read OFFSET when scanning a RowType";
    } else {
      VALIDATOR_RET_CHECK_FAIL()
          << "ArrayScan of non-ARRAY type: " << expr_type->DebugString();
    }
    const ResolvedColumn& element_column = scan->element_column_list(i);
    VALIDATOR_RET_CHECK(element_type->Equals(element_column.type()))
        << "Array element type " << element_type->DebugString()
        << " does not match column type "
        << element_column.type()->DebugString();

    GOOGLESQL_RETURN_IF_ERROR(CheckUniqueColumnId(element_column));
    visible_columns.insert(element_column);
  }

  if (nullptr != scan->array_offset_column()) {
    GOOGLESQL_RETURN_IF_ERROR(CheckUniqueColumnId(scan->array_offset_column()->column()));
    visible_columns.insert(scan->array_offset_column()->column());
  }
  if (nullptr != scan->join_expr()) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(visible_columns, visible_parameters,
                                         scan->join_expr()));
    VALIDATOR_RET_CHECK(scan->join_expr()->type()->IsBool())
        << "ArrayScan has join_expr with non-BOOL type: "
        << scan->join_expr()->type()->DebugString();
  }
  GOOGLESQL_RETURN_IF_ERROR(CheckColumnList(scan, visible_columns));
  scan->is_outer();  // Mark field as visited.

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedFilterScan(
    const ResolvedFilterScan* scan,
    const std::set<ResolvedColumn>& visible_parameters) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, scan);

  VALIDATOR_RET_CHECK(nullptr != scan->input_scan());
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedScan(scan->input_scan(), visible_parameters));

  std::set<ResolvedColumn> visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(
      AddColumnList(scan->input_scan()->column_list(), &visible_columns));
  VALIDATOR_RET_CHECK(nullptr != scan->filter_expr());
  GOOGLESQL_RETURN_IF_ERROR(ValidateBoolExpr(visible_columns, visible_parameters,
                                   scan->filter_expr()));
  GOOGLESQL_RETURN_IF_ERROR(CheckColumnList(scan, visible_columns));

  return absl::OkStatus();
}

template <typename T>
static bool Contains(const std::set<T> set, const T& element) {
  return set.find(element) != set.end();
}

absl::Status Validator::ValidateResolvedAggregateComputedColumn(
    const ResolvedComputedColumnImpl* computed_column,
    const std::set<ResolvedColumn>& input_scan_visible_columns,
    const std::set<ResolvedColumn>& visible_parameters,
    const std::set<ResolvedColumn>& available_side_effect_columns) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, computed_column);
  GOOGLESQL_RET_CHECK(
      !Contains(available_side_effect_columns, computed_column->column()));
  if (computed_column->Is<ResolvedDeferredComputedColumn>()) {
    VALIDATOR_RET_CHECK(language_options_.LanguageFeatureEnabled(
        FEATURE_ENFORCE_CONDITIONAL_EVALUATION))
        << "ResolvedDeferredComputedColumns is only used when "
           "FEATURE_ENFORCE_CONDITIONAL_EVALUATION is enabled.";

    const auto* deferred_computed_column =
        computed_column->GetAs<ResolvedDeferredComputedColumn>();
    // The referenced side effect column must exist.
    VALIDATOR_RET_CHECK(
        Contains(available_side_effect_columns,
                 deferred_computed_column->side_effect_column()));
    // This column itself cannot be a side-effects column
    VALIDATOR_RET_CHECK(
        !Contains(available_side_effect_columns, computed_column->column()));
  }
  VALIDATOR_RET_CHECK_EQ(computed_column->expr()->node_kind(),
                         RESOLVED_AGGREGATE_FUNCTION_CALL);
  const ResolvedAggregateFunctionCall* aggregate_function_call =
      computed_column->expr()->GetAs<ResolvedAggregateFunctionCall>();

  return ValidateOrderByAndLimitClausesOfAggregateFunctionCall(
      input_scan_visible_columns, visible_parameters, aggregate_function_call);
}

absl::Status Validator::ValidateResolvedAggregateScanBase(
    const ResolvedAggregateScanBase* scan,
    const std::set<ResolvedColumn>& visible_parameters,
    std::set<ResolvedColumn>* input_scan_visible_columns) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, scan);

  VALIDATOR_RET_CHECK(nullptr != scan->input_scan());
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedScan(scan->input_scan(), visible_parameters));

  GOOGLESQL_RETURN_IF_ERROR(AddColumnList(scan->input_scan()->column_list(),
                                input_scan_visible_columns));
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedComputedColumnList(
      *input_scan_visible_columns, visible_parameters, scan->group_by_list()));

  if (language_options_.LanguageFeatureEnabled(FEATURE_COLLATION_SUPPORT)) {
    if (scan->collation_list_size() != 0) {
      GOOGLESQL_RET_CHECK_EQ(scan->collation_list_size(), scan->group_by_list_size());
      for (int i = 0; i < scan->collation_list_size(); i++) {
        GOOGLESQL_RET_CHECK(scan->collation_list(i).HasCompatibleStructure(
            scan->group_by_list(i)->expr()->type()))
            << "Collation must have compatible structure with the type of "
               "the element in group_by_list with the same index";
      }
    }
  } else {
    GOOGLESQL_RET_CHECK_EQ(scan->collation_list_size(), 0);
  }
  // TODO: consider capturing input_columns_for_group_rows_ here and
  // not in ValidateResolvedAggregateFunctionCall(). This way we can avoid extra
  // copies when multiple WITH GROUP ROWS are used in the same aggregate scan;
  // also we could validate the case if for some way WITH GROUP ROWS is used
  // outside of aggregate scan.

  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedComputedColumnList(
      *input_scan_visible_columns, visible_parameters, scan->aggregate_list()));

  std::set<ResolvedColumn> generated_side_effect_columns;
  for (const auto& computed_column : scan->aggregate_list()) {
    if (computed_column->Is<ResolvedDeferredComputedColumn>()) {
      generated_side_effect_columns.insert(
          computed_column->GetAs<ResolvedDeferredComputedColumn>()
              ->side_effect_column());
    }
  }

  // Validates other constructs in aggregates such as ORDER BY and LIMIT.
  for (const auto& computed_column : scan->aggregate_list()) {
    GOOGLESQL_RET_CHECK(computed_column->Is<ResolvedComputedColumnImpl>());
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedAggregateComputedColumn(
        computed_column->GetAs<ResolvedComputedColumnImpl>(),
        *input_scan_visible_columns, visible_parameters,
        generated_side_effect_columns));
  }

  GOOGLESQL_RETURN_IF_ERROR(ValidateGroupingSetList(scan->grouping_set_list(),
                                          scan->rollup_column_list(),
                                          scan->group_by_list()));

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedAggregateScan(
    const ResolvedAggregateScan* scan,
    const std::set<ResolvedColumn>& visible_parameters) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, scan);

  std::set<ResolvedColumn> input_scan_visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedAggregateScanBase(
      scan, visible_parameters, &input_scan_visible_columns));

  std::set<ResolvedColumn> group_by_columns;
  for (const auto& group_by_column : scan->group_by_list()) {
    group_by_columns.insert(group_by_column->column());
  }

  std::set<ResolvedColumn> visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(AddColumnsFromComputedColumnList(scan->aggregate_list(),
                                                   &visible_columns));
  GOOGLESQL_RETURN_IF_ERROR(AddColumnsFromComputedColumnList(scan->group_by_list(),
                                                   &visible_columns));

  // Validate grouping_call_list after adding the group_by_list to visible
  // columns.
  if (!scan->grouping_call_list().empty()) {
    GOOGLESQL_RETURN_IF_ERROR(AddColumnsFromGroupingCallList(scan->grouping_call_list(),
                                                   &visible_columns));
    GOOGLESQL_RETURN_IF_ERROR(ValidateGroupingFunctionCallList(
        visible_columns, scan->grouping_call_list(), group_by_columns));
  }
  GOOGLESQL_RETURN_IF_ERROR(CheckColumnList(scan, visible_columns));

  return absl::OkStatus();
}

absl::Status
Validator::ValidateGroupSelectionThresholdExprWithMinPrivacyUnitsPerGroup(
    const ResolvedExpr* group_threshold_expr,
    const std::set<ResolvedColumn>& visible_columns,
    const std::set<ResolvedColumn>& visible_parameters,
    const std::unique_ptr<const ResolvedOption>&
        min_privacy_units_per_group_option,
    absl::string_view expression_name) {
  // If `min_privacy_units_per_group` != NULL, we expect `group_threshold_expr`
  // to be of the form IF(<ResolvedColumnRef> >= <ResolvedLiteral>,
  // (then) <ValidThresholdExpression> - <ResolvedLiteral>,
  // (else) <ResolvedLiteral>).
  VALIDATOR_RET_CHECK(group_threshold_expr != nullptr);
  VALIDATOR_RET_CHECK(group_threshold_expr->type()->IsNumerical());

  VALIDATOR_RET_CHECK(
      min_privacy_units_per_group_option->value()->Is<ResolvedLiteral>());
  if (min_privacy_units_per_group_option->value()
          ->GetAs<ResolvedLiteral>()
          ->value()
          .is_null()) {
    return ValidateGroupSelectionThresholdExprWithoutMinPrivacyUnitsPerGroup(
        group_threshold_expr, visible_columns, visible_parameters,
        expression_name);
  }

  // Check that the statement is an if condition and obtain the parameter
  // expressions.
  VALIDATOR_RET_CHECK(group_threshold_expr->Is<ResolvedFunctionCall>());
  const ResolvedFunctionCall* if_function =
      group_threshold_expr->GetAs<ResolvedFunctionCall>();
  VALIDATOR_RET_CHECK_EQ(if_function->signature().context_id(), FN_IF);
  VALIDATOR_RET_CHECK_EQ(if_function->argument_list_size(), 3);
  const ResolvedExpr* condition_expr = if_function->argument_list(0);
  const ResolvedExpr* then_expr = if_function->argument_list(1);
  const ResolvedExpr* else_expr = if_function->argument_list(2);

  // The if condition is supposed to check that
  // exact_count_distinct_privacy_units >= min_privacy_units_per_group.
  // exact_count_distinct_privacy_units should be computed in a column and
  // min_privacy_units_per_group should be a literal.
  VALIDATOR_RET_CHECK(condition_expr->Is<ResolvedFunctionCall>());
  const ResolvedFunctionCall* condition_function =
      condition_expr->GetAs<ResolvedFunctionCall>();
  VALIDATOR_RET_CHECK_EQ(condition_function->signature().context_id(),
                         FN_GREATER_OR_EQUAL);
  VALIDATOR_RET_CHECK_EQ(condition_function->argument_list_size(), 2);
  const ResolvedExpr* exact_count_distinct_privacy_units_col =
      condition_function->argument_list(0);
  const ResolvedExpr* min_privacy_units_per_group_expr =
      condition_function->argument_list(1);

  VALIDATOR_RET_CHECK(
      exact_count_distinct_privacy_units_col->Is<ResolvedColumnRef>());
  GOOGLESQL_RETURN_IF_ERROR(CheckColumnIsPresentInColumnSet(
      exact_count_distinct_privacy_units_col->GetAs<ResolvedColumnRef>()
          ->column(),
      visible_columns));
  VALIDATOR_RET_CHECK(min_privacy_units_per_group_expr->Is<ResolvedLiteral>());

  // The then expression should be noisy_count_distinct_privacy_units -
  // (min_privacy_units_per_group - 1). noisy_count_distinct_privacy_units
  // should be a valid group selection threshold expression and
  // (min_privacy_units_per_group - 1) is a literal.
  VALIDATOR_RET_CHECK(then_expr->Is<ResolvedFunctionCall>());
  const ResolvedFunctionCall* then_function =
      then_expr->GetAs<ResolvedFunctionCall>();
  VALIDATOR_RET_CHECK_EQ(then_function->signature().context_id(),
                         FN_SAFE_SUBTRACT_INT64);
  VALIDATOR_RET_CHECK_EQ(then_function->argument_list_size(), 2);
  const ResolvedExpr* noisy_count_distinct_privacy_units =
      then_function->argument_list(0);
  const ResolvedExpr* decrease_threshold_by_expr =
      then_function->argument_list(1);

  GOOGLESQL_RETURN_IF_ERROR(
      ValidateGroupSelectionThresholdExprWithoutMinPrivacyUnitsPerGroup(
          noisy_count_distinct_privacy_units, visible_columns,
          visible_parameters, expression_name));
  VALIDATOR_RET_CHECK(decrease_threshold_by_expr->Is<ResolvedLiteral>());
  const Value& decrease_threshold_value =
      decrease_threshold_by_expr->GetAs<ResolvedLiteral>()->value();
  VALIDATOR_RET_CHECK(!decrease_threshold_value.is_null());
  VALIDATOR_RET_CHECK(decrease_threshold_value.type()->IsInt64());
  int64_t decrease_threshold = decrease_threshold_value.int64_value();

  const Value& min_privacy_units_per_group_value =
      min_privacy_units_per_group_option->value()
          ->GetAs<ResolvedLiteral>()
          ->value();
  VALIDATOR_RET_CHECK(!min_privacy_units_per_group_value.is_null());
  VALIDATOR_RET_CHECK(min_privacy_units_per_group_value.type()->IsInt64());
  int64_t min_privacy_units_per_group =
      min_privacy_units_per_group_value.int64_value();
  VALIDATOR_RET_CHECK_EQ(decrease_threshold, min_privacy_units_per_group - 1);
  // The else expression should be a resolved literal which will always be 0.
  VALIDATOR_RET_CHECK(else_expr->Is<ResolvedLiteral>());
  const Value& else_value = else_expr->GetAs<ResolvedLiteral>()->value();
  VALIDATOR_RET_CHECK(else_value.type()->IsInt64());
  VALIDATOR_RET_CHECK(else_value.is_null());

  return absl::OkStatus();
}

absl::Status
Validator::ValidateGroupSelectionThresholdExprWithoutMinPrivacyUnitsPerGroup(
    const ResolvedExpr* group_threshold_expr,
    const std::set<ResolvedColumn>& visible_columns,
    const std::set<ResolvedColumn>& visible_parameters,
    absl::string_view expression_name) {
  VALIDATOR_RET_CHECK(group_threshold_expr != nullptr);
  VALIDATOR_RET_CHECK(group_threshold_expr->type()->IsNumerical());

  if (group_threshold_expr->Is<ResolvedColumnRef>()) {
    GOOGLESQL_RETURN_IF_ERROR(CheckColumnIsPresentInColumnSet(
        group_threshold_expr->GetAs<ResolvedColumnRef>()->column(),
        visible_columns));
  } else if (group_threshold_expr->Is<ResolvedGetProtoField>()) {
    // We expect group_threshold_expr be exactly of the following structure:
    // ResolvedGetProtoField -> ResolvedGetProtoField -> ResolvedColumnRef.
    // This structure is guaranteed because group_threshold_expr of this form is
    // being constructed in the anonymization_rewriter in the cases when
    // the user query contains unique users count in it and the output type is
    // report (format=PROTO).
    VALIDATOR_RET_CHECK(group_threshold_expr->GetAs<ResolvedGetProtoField>()
                            ->expr()
                            ->Is<ResolvedGetProtoField>());
    VALIDATOR_RET_CHECK(group_threshold_expr->GetAs<ResolvedGetProtoField>()
                            ->expr()
                            ->GetAs<ResolvedGetProtoField>()
                            ->expr()
                            ->Is<ResolvedColumnRef>());
    GOOGLESQL_RETURN_IF_ERROR(CheckColumnIsPresentInColumnSet(
        group_threshold_expr->GetAs<ResolvedGetProtoField>()
            ->expr()
            ->GetAs<ResolvedGetProtoField>()
            ->expr()
            ->GetAs<ResolvedColumnRef>()
            ->column(),
        visible_columns));
  } else if (group_threshold_expr->Is<ResolvedFunctionCall>()) {
    // We expect group_threshold_expr have exactly the following structure:
    // ResolvedFunctionCall -> ResolvedFunctionCall  -> ResolvedColumnRef.
    // This structure is guaranteed because group_threshold_expr of this form is
    // being constructed in the anonymization_rewriter in the cases when the
    // user query contains unique users count in it and the output type is
    // report (format=JSON).
    VALIDATOR_RET_CHECK(group_threshold_expr->GetAs<ResolvedFunctionCall>()
                            ->argument_list(0)
                            ->Is<ResolvedFunctionCall>());
    VALIDATOR_RET_CHECK(group_threshold_expr->GetAs<ResolvedFunctionCall>()
                            ->argument_list(0)
                            ->GetAs<ResolvedFunctionCall>()
                            ->argument_list(0)
                            ->Is<ResolvedColumnRef>());
    GOOGLESQL_RETURN_IF_ERROR(CheckColumnIsPresentInColumnSet(
        group_threshold_expr->GetAs<ResolvedFunctionCall>()
            ->argument_list(0)
            ->GetAs<ResolvedFunctionCall>()
            ->argument_list(0)
            ->GetAs<ResolvedColumnRef>()
            ->column(),
        visible_columns));
  } else {
    VALIDATOR_RET_CHECK_FAIL()
        << "Unexpected expression type for " << expression_name << ": "
        << group_threshold_expr->node_kind_string();
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateGroupSelectionThresholdExpr(
    const ResolvedExpr* group_threshold_expr,
    const std::set<ResolvedColumn>& visible_columns,
    const std::set<ResolvedColumn>& visible_parameters,
    absl::Span<const std::unique_ptr<const ResolvedOption>> scan_options,
    absl::string_view expression_name) {
  if (group_threshold_expr == nullptr) {
    return absl::OkStatus();
  }
  // The group threshold expression has a special form, which we validate below.
  // First and foremost, it has to be a valid expression, though.
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(visible_columns, visible_parameters,
                                       group_threshold_expr));
  if (language_options_.LanguageFeatureEnabled(
          FEATURE_DIFFERENTIAL_PRIVACY_MIN_PRIVACY_UNITS_PER_GROUP)) {
    auto is_min_privacy_units_per_group =
        [](const std::unique_ptr<const ResolvedOption>& option) {
          return absl::AsciiStrToLower(option->name()) ==
                 "min_privacy_units_per_group";
        };
    if (auto it = std::find_if(scan_options.begin(), scan_options.end(),
                               is_min_privacy_units_per_group);
        it != scan_options.end()) {
      // Found the `min_privacy_units_per_group` option.
      return ValidateGroupSelectionThresholdExprWithMinPrivacyUnitsPerGroup(
          group_threshold_expr, visible_columns, visible_parameters, *it,
          expression_name);
    }
  }
  return ValidateGroupSelectionThresholdExprWithoutMinPrivacyUnitsPerGroup(
      group_threshold_expr, visible_columns, visible_parameters,
      expression_name);
}

// Validates that the aggregate scan has no multi-level aggregates.
absl::Status Validator::ValidateAggregateScanHasNoMultiLevelAggregates(
    const ResolvedAggregateScanBase* scan) {
  for (const auto& resolved_computed_column : scan->aggregate_list()) {
    VALIDATOR_RET_CHECK(
        resolved_computed_column->expr()->Is<ResolvedAggregateFunctionCall>());
    const ResolvedAggregateFunctionCall* aggregate_function_call =
        resolved_computed_column->expr()
            ->GetAs<ResolvedAggregateFunctionCall>();
    VALIDATOR_RET_CHECK(aggregate_function_call->group_by_list().empty())
        << "Multi-level aggregation is not supported for this scan type";
  }
  return absl::OkStatus();
}

// Validates that the aggregate scan has no `AGG` function calls (`AGG` is used
// to aggregate measure typed columns).
absl::Status Validator::ValidateAggregateScanHasNoAggFunctionCall(
    const ResolvedAggregateScanBase* scan) {
  for (const auto& resolved_computed_column : scan->aggregate_list()) {
    VALIDATOR_RET_CHECK(
        resolved_computed_column->expr()->Is<ResolvedAggregateFunctionCall>());
    const ResolvedAggregateFunctionCall* aggregate_function_call =
        resolved_computed_column->expr()
            ->GetAs<ResolvedAggregateFunctionCall>();
    if (aggregate_function_call->function()->NumSignatures() == 1) {
      VALIDATOR_RET_CHECK(
          aggregate_function_call->function()->GetSignature(0)->context_id() !=
          FN_AGG)
          << aggregate_function_call->function()->Name()
          << " function is not supported for this scan type.";
    }
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateFeatureIsEnabledForWithModifier(
    LanguageFeature feature, WithModifierMode mode) {
  VALIDATOR_RET_CHECK(language_options_.LanguageFeatureEnabled(feature))
      << "WITH " << WithModifierModeToString(mode) << " is not supported";
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedAnonymizedAggregateScan(
    const ResolvedAnonymizedAggregateScan* scan,
    const std::set<ResolvedColumn>& visible_parameters) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, scan);

  GOOGLESQL_RETURN_IF_ERROR(ValidateFeatureIsEnabledForWithModifier(
      FEATURE_ANONYMIZATION, WithModifierMode::ANONYMIZATION));

  std::set<ResolvedColumn> input_scan_visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedAggregateScanBase(
      scan, visible_parameters, &input_scan_visible_columns));

  // Multi-level aggregation is currently not supported for Anonymized aggregate
  // scans.
  GOOGLESQL_RETURN_IF_ERROR(ValidateAggregateScanHasNoMultiLevelAggregates(scan));
  // `AGG` function calls are not supported for Anonymized aggregate scans.
  GOOGLESQL_RETURN_IF_ERROR(ValidateAggregateScanHasNoAggFunctionCall(scan));

  std::set<ResolvedColumn> visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(AddColumnsFromComputedColumnList(scan->aggregate_list(),
                                                   &visible_columns));
  GOOGLESQL_RETURN_IF_ERROR(ValidateGroupSelectionThresholdExpr(
      scan->k_threshold_expr(), visible_columns, visible_parameters,
      scan->anonymization_option_list(), "k_threshold_expr"));
  GOOGLESQL_RETURN_IF_ERROR(AddColumnsFromComputedColumnList(scan->group_by_list(),
                                                   &visible_columns));
  GOOGLESQL_RETURN_IF_ERROR(CheckColumnList(scan, visible_columns));

  GOOGLESQL_RETURN_IF_ERROR(ValidateGroupingSetListAreEmpty(scan->grouping_set_list(),
                                                  scan->rollup_column_list()));

  return ValidateOptionsList(
      scan->anonymization_option_list(),
      options_.allowed_hints_and_options.anonymization_options_lower,
      visible_columns, visible_parameters, "anonymization");
}

absl::Status Validator::ValidateResolvedDifferentialPrivacyAggregateScan(
    const ResolvedDifferentialPrivacyAggregateScan* scan,
    const std::set<ResolvedColumn>& visible_parameters) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, scan);

  GOOGLESQL_RETURN_IF_ERROR(ValidateFeatureIsEnabledForWithModifier(
      FEATURE_DIFFERENTIAL_PRIVACY, WithModifierMode::DIFFERENTIAL_PRIVACY));

  std::set<ResolvedColumn> input_scan_visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedAggregateScanBase(
      scan, visible_parameters, &input_scan_visible_columns));

  // Multi-level aggregation is currently not supported for Differential Privacy
  // aggregate scans.
  GOOGLESQL_RETURN_IF_ERROR(ValidateAggregateScanHasNoMultiLevelAggregates(scan));
  // `AGG` function calls are not supported for Differential Privacy aggregate
  // scans.
  GOOGLESQL_RETURN_IF_ERROR(ValidateAggregateScanHasNoAggFunctionCall(scan));

  std::set<ResolvedColumn> visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(AddColumnsFromComputedColumnList(scan->aggregate_list(),
                                                   &visible_columns));
  GOOGLESQL_RETURN_IF_ERROR(ValidateGroupSelectionThresholdExpr(
      scan->group_selection_threshold_expr(), visible_columns,
      visible_parameters, scan->option_list(),
      "group_selection_threshold_expr"));
  GOOGLESQL_RETURN_IF_ERROR(AddColumnsFromComputedColumnList(scan->group_by_list(),
                                                   &visible_columns));
  GOOGLESQL_RETURN_IF_ERROR(CheckColumnList(scan, visible_columns));

  GOOGLESQL_RETURN_IF_ERROR(ValidateGroupingSetListAreEmpty(scan->grouping_set_list(),
                                                  scan->rollup_column_list()));

  return ValidateOptionsList(
      scan->option_list(),
      options_.allowed_hints_and_options.differential_privacy_options_lower,
      input_scan_visible_columns, visible_parameters, "differential privacy");
}

absl::Status Validator::ValidateResolvedAggregationThresholdAggregateScan(
    const ResolvedAggregationThresholdAggregateScan* scan,
    const std::set<ResolvedColumn>& visible_parameters) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, scan);
  GOOGLESQL_RETURN_IF_ERROR(ValidateFeatureIsEnabledForWithModifier(
      FEATURE_AGGREGATION_THRESHOLD, WithModifierMode::AGGREGATION_THRESHOLD));

  std::set<ResolvedColumn> input_scan_visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedAggregateScanBase(
      scan, visible_parameters, &input_scan_visible_columns));

  // Multi-level aggregation is currently not supported for Aggregation
  // Threshold aggregate scans.
  GOOGLESQL_RETURN_IF_ERROR(ValidateAggregateScanHasNoMultiLevelAggregates(scan));
  // `AGG` function calls are not supported for Aggregation Threshold aggregate
  // scans.
  GOOGLESQL_RETURN_IF_ERROR(ValidateAggregateScanHasNoAggFunctionCall(scan));

  std::set<ResolvedColumn> visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(AddColumnsFromComputedColumnList(scan->aggregate_list(),
                                                   &visible_columns));
  GOOGLESQL_RETURN_IF_ERROR(AddColumnsFromComputedColumnList(scan->group_by_list(),
                                                   &visible_columns));
  GOOGLESQL_RETURN_IF_ERROR(CheckColumnList(scan, visible_columns));

  return ValidateOptionsList(
      scan->option_list(), GetAllowedAggregationThresholdOptions(),
      input_scan_visible_columns, visible_parameters, "aggregation_threshold");
}

absl::Status Validator::ValidatePercentArgument(const ResolvedExpr* expr) {
  VALIDATOR_RET_CHECK(expr != nullptr);
  PushErrorContext push(this, expr);
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(
      /*visible_columns=*/{}, /*visible_parameters=*/{}, expr));
  VALIDATOR_RET_CHECK(expr->node_kind() == RESOLVED_LITERAL ||
                      expr->node_kind() == RESOLVED_PARAMETER)
      << "PERCENT argument is of incorrect kind: " << expr->node_kind_string();

  VALIDATOR_RET_CHECK(expr->type()->IsInt64() || expr->type()->IsDouble())
      << "PERCENT argument must be either a double or an int64";

  if (expr->node_kind() == RESOLVED_LITERAL) {
    // If a literal, we validate its value.
    const Value value = expr->GetAs<ResolvedLiteral>()->value();
    bool is_valid = false;
    if (value.type()->IsInt64()) {
      is_valid = (!value.is_null() && value.int64_value() >= 0 &&
                  value.int64_value() <= 100);
    } else {
      VALIDATOR_RET_CHECK(value.type()->IsDouble());
      is_valid = (!value.is_null() && value.double_value() >= 0.0 &&
                  value.double_value() <= 100.0);
    }
    if (!is_valid) {
      return ::googlesql_base::UnknownErrorBuilder()
             << "PERCENT argument value must be in the range [0, 100]";
    }
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedSampleScan(
    const ResolvedSampleScan* scan,
    const std::set<ResolvedColumn>& visible_parameters) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, scan);

  VALIDATOR_RET_CHECK(nullptr != scan->input_scan());
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedScan(scan->input_scan(), visible_parameters));

  VALIDATOR_RET_CHECK(!scan->method().empty());
  VALIDATOR_RET_CHECK(nullptr != scan->size());

  const ResolvedSampleScan::SampleUnit unit = scan->unit();
  if (unit == ResolvedSampleScan::ROWS) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateArgumentIsInt64(
        scan->size(), /*validate_constant_nonnegative=*/true,
        /*context_msg=*/"Sample unit"));
  } else {
    VALIDATOR_RET_CHECK_EQ(unit, ResolvedSampleScan::PERCENT);
    GOOGLESQL_RETURN_IF_ERROR(ValidatePercentArgument(scan->size()));
  }

  VALIDATOR_RET_CHECK(scan->size() != nullptr);
  if (scan->repeatable_argument() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateArgumentIsInt64(
        scan->repeatable_argument(), /*validate_constant_nonnegative=*/true,
        /*context_msg=*/"Sample repeatable argument"));
  }

  std::set<ResolvedColumn> visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(
      AddColumnList(scan->input_scan()->column_list(), &visible_columns));
  if (nullptr != scan->weight_column()) {
    GOOGLESQL_RETURN_IF_ERROR(CheckUniqueColumnId(scan->weight_column()->column()));
    visible_columns.insert(scan->weight_column()->column());
  }
  GOOGLESQL_RETURN_IF_ERROR(CheckColumnList(scan, visible_columns));

  if (!scan->partition_by_list().empty()) {
    VALIDATOR_RET_CHECK_EQ(ResolvedSampleScan::ROWS, unit);
    VALIDATOR_RET_CHECK_EQ("RESERVOIR", absl::AsciiStrToUpper(scan->method()));
    for (const auto& partition_by_expr : scan->partition_by_list()) {
      GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(
          visible_columns, /*visible_parameters=*/{}, partition_by_expr.get()));
    }
  }

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedUnsetArgumentScan(
    const ResolvedUnsetArgumentScan* scan, bool allow_unset_argument_scan) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, scan);

  VALIDATOR_RET_CHECK(allow_unset_argument_scan)
      << "UnsetArgumentScan is only allowed as an argument to a TVFScan";
  VALIDATOR_RET_CHECK_EQ(scan->column_list_size(), 0)
      << "UnsetArgumentScan should not have any columns";
  return absl::OkStatus();
}

absl::Status Validator::CheckUniqueColumnId(const ResolvedColumn& column) {
  VALIDATOR_RET_CHECK(
      googlesql_base::InsertIfNotPresent(&column_ids_seen_, column.column_id()))
      << "Duplicate column id " << column.column_id() << " in column "
      << column.DebugString();
  return absl::OkStatus();
}

absl::Status Validator::CheckColumnIdWasCreated(const ResolvedColumn& column) {
  VALIDATOR_RET_CHECK(column_ids_seen_.contains(column.column_id()))
      << "Column was never created with CheckUniqueColumnId: "
      << column.DebugString();
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedAnalyticScan(
    const ResolvedAnalyticScan* scan,
    const std::set<ResolvedColumn>& visible_parameters) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, scan);

  VALIDATOR_RET_CHECK(nullptr != scan->input_scan());
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedScan(scan->input_scan(), visible_parameters));

  std::set<ResolvedColumn> visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(
      AddColumnList(scan->input_scan()->column_list(), &visible_columns));

  for (const std::unique_ptr<const ResolvedAnalyticFunctionGroup>& group :
       scan->function_group_list()) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedAnalyticFunctionGroup(
        group.get(), visible_columns, visible_parameters));
  }

  for (const std::unique_ptr<const ResolvedAnalyticFunctionGroup>& group :
       scan->function_group_list()) {
    GOOGLESQL_RETURN_IF_ERROR(AddColumnsFromComputedColumnList(
        group->analytic_function_list(), &visible_columns));
  }
  GOOGLESQL_RETURN_IF_ERROR(CheckColumnList(scan, visible_columns));

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedAnalyticFunctionGroup(
    const ResolvedAnalyticFunctionGroup* group,
    const std::set<ResolvedColumn>& input_visible_columns,
    const std::set<ResolvedColumn>& visible_parameters) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, group);

  for (const auto& computed_column : group->analytic_function_list()) {
    VALIDATOR_RET_CHECK(computed_column->Is<ResolvedComputedColumn>());
    const ResolvedAnalyticFunctionCall* analytic_function_call =
        computed_column->expr()->GetAs<ResolvedAnalyticFunctionCall>();
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(
        input_visible_columns, visible_parameters, analytic_function_call));

    const Function* analytic_function = analytic_function_call->function();
    const std::string function_name = analytic_function->Name();
    if (!analytic_function->SupportsOverClause()) {
      return InternalErrorBuilder()
             << "Function " << function_name
             << " cannot be used in an analytic function call, since it does "
                "not"
                " support an OVER clause";
    }
    if (analytic_function_call->distinct()) {
      if (!analytic_function_call->function()->IsAggregate()) {
        return InternalErrorBuilder()
               << "Cannot specify DISTINCT for a non-aggregate analytic "
                  "function:\n"
               << analytic_function_call->DebugString();
      }
      VALIDATOR_RET_CHECK_EQ(
          analytic_function_call->generic_argument_list_size(), 0)
          << "Analytic functions do not support generic arguments yet";
      if (analytic_function_call->argument_list_size() == 0) {
        return InternalErrorBuilder()
               << "DISTINCT function call " << function_name
               << " does not have an argument:\n"
               << analytic_function_call->DebugString();
      }
      if (group->order_by() != nullptr) {
        return InternalErrorBuilder()
               << "Cannot specify a window ORDER BY clause in a DISTINCT "
                  "analytic "
                  "function call:\n"
               << analytic_function_call->DebugString();
      }
      if (analytic_function_call->window_frame() != nullptr) {
        const ResolvedWindowFrame* window_frame =
            analytic_function_call->window_frame();
        if (window_frame->start_expr()->boundary_type() !=
                ResolvedWindowFrameExpr::UNBOUNDED_PRECEDING ||
            window_frame->end_expr()->boundary_type() !=
                ResolvedWindowFrameExpr::UNBOUNDED_FOLLOWING) {
          return InternalErrorBuilder()
                 << "The window frame for a DISTINCT analytic function call "
                    "must "
                    "be UNBOUNDED PRECEDING to UNBOUNDED FOLLOWING:\n"
                 << analytic_function_call->DebugString();
        }
      }
    }

    if (!analytic_function->SupportsWindowFraming() &&
        analytic_function_call->window_frame() != nullptr) {
      return InternalErrorBuilder()
             << "Analytic function " << function_name
             << " does not support framing, but has a window framing clause:\n"
             << analytic_function_call->DebugString();
    }
    if (analytic_function->RequiresWindowOrdering() &&
        group->order_by() == nullptr) {
      return InternalErrorBuilder() << "Analytic function " << function_name
                                    << " must have a window ORDER BY clause:\n"
                                    << group->DebugString();
    }
    if (!analytic_function->SupportsWindowOrdering() &&
        group->order_by() != nullptr) {
      return InternalErrorBuilder()
             << "Analytic function " << function_name
             << " does not support a window ORDER BY clause:\n"
             << analytic_function_call->DebugString();
    }

    if (analytic_function_call->window_frame() != nullptr) {
      GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedWindowFrame(
          input_visible_columns, visible_parameters, group->order_by(),
          analytic_function_call->window_frame()));
    }
  }

  if (group->partition_by() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedWindowPartitioning(
        group->partition_by(), input_visible_columns, visible_parameters));
  }

  if (group->order_by() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedWindowOrdering(
        group->order_by(), input_visible_columns, visible_parameters));
  }

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedWindowPartitioning(
    const ResolvedWindowPartitioning* partition_by,
    const std::set<ResolvedColumn>& visible_columns,
    const std::set<ResolvedColumn>& visible_parameters) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  for (const auto& column_ref : partition_by->partition_by_list()) {
    std::string no_partitioning_type;
    if (!column_ref->type()->SupportsPartitioning(language_options_,
                                                  &no_partitioning_type)) {
      return InternalErrorBuilder()
             << "Type of PARTITIONING expressions " << no_partitioning_type
             << " does not support partitioning:\n"
             << column_ref->DebugString();
    }
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(visible_columns, visible_parameters,
                                         column_ref.get()));
  }
  if (language_options_.LanguageFeatureEnabled(FEATURE_COLLATION_SUPPORT)) {
    if (!partition_by->collation_list().empty()) {
      VALIDATOR_RET_CHECK_EQ(partition_by->collation_list().size(),
                             partition_by->partition_by_list().size());
      for (int i = 0; i < partition_by->collation_list().size(); ++i) {
        VALIDATOR_RET_CHECK(
            partition_by->collation_list()[i].HasCompatibleStructure(
                partition_by->partition_by_list()[i]->type()))
            << "Collation must have compatible structure with the type of "
               "the element in partition_by_list with the same index";
      }
    }
  } else {
    VALIDATOR_RET_CHECK(partition_by->collation_list().empty());
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedWindowOrdering(
    const ResolvedWindowOrdering* order_by,
    const std::set<ResolvedColumn>& visible_columns,
    const std::set<ResolvedColumn>& visible_parameters) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  for (const auto& order_by_item : order_by->order_by_item_list()) {
    if (!order_by_item->column_ref()->type()->SupportsOrdering(
            language_options_, /*type_description=*/nullptr)) {
      return InternalErrorBuilder()
             << "Type of ORDERING expressions "
             << order_by_item->column_ref()->type()->DebugString()
             << " does not support ordering:\n"
             << order_by_item->column_ref()->DebugString();
    }
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedOrderByItem(
        visible_columns, visible_parameters, order_by_item.get()));
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedWindowFrame(
    const std::set<ResolvedColumn>& visible_columns,
    const std::set<ResolvedColumn>& visible_parameters,
    const ResolvedWindowOrdering* window_ordering,
    const ResolvedWindowFrame* window_frame) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, window_frame);
  VALIDATOR_RET_CHECK(window_frame->start_expr() != nullptr &&
                      window_frame->end_expr() != nullptr)
      << "Window frame must specify both the starting and the ending boundary"
         ":\n"
      << window_frame->DebugString();
  VALIDATOR_RET_CHECK(window_frame->frame_unit() == ResolvedWindowFrame::ROWS ||
                      window_frame->frame_unit() == ResolvedWindowFrame::RANGE)
      << "Unhandled window frame unit " << window_frame->GetFrameUnitString()
      << ":\n"
      << window_frame->DebugString();

  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedWindowFrameExpr(
      visible_columns, visible_parameters, window_ordering,
      window_frame->frame_unit(), window_frame->start_expr()));
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedWindowFrameExpr(
      visible_columns, visible_parameters, window_ordering,
      window_frame->frame_unit(), window_frame->end_expr()));

  if (IsEmptyWindowFrame(*window_frame)) {
    return InternalErrorBuilder() << "Window frame must be non-empty";
  }

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedWindowFrameExpr(
    const std::set<ResolvedColumn>& visible_columns,
    const std::set<ResolvedColumn>& visible_parameters,
    const ResolvedWindowOrdering* window_ordering,
    const ResolvedWindowFrame::FrameUnit& frame_unit,
    const ResolvedWindowFrameExpr* window_frame_expr) {
  PushErrorContext push(this, window_frame_expr);
  switch (window_frame_expr->boundary_type()) {
    case ResolvedWindowFrameExpr::UNBOUNDED_PRECEDING:
    case ResolvedWindowFrameExpr::CURRENT_ROW:
    case ResolvedWindowFrameExpr::UNBOUNDED_FOLLOWING:
      if (window_frame_expr->expression() != nullptr) {
        return InternalErrorBuilder()
               << "Window frame boundary of type "
               << window_frame_expr->GetBoundaryTypeString()
               << " cannot have an offset expression:\n"
               << window_frame_expr->DebugString();
      }
      break;
    case ResolvedWindowFrameExpr::OFFSET_PRECEDING:
    case ResolvedWindowFrameExpr::OFFSET_FOLLOWING: {
      if (window_frame_expr->expression() == nullptr) {
        return InternalErrorBuilder()
               << "Window frame boundary of type "
               << window_frame_expr->GetBoundaryTypeString()
               << " must specify an offset expression:\n"
               << window_frame_expr->DebugString();
      }
      GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(visible_columns, visible_parameters,
                                           window_frame_expr->expression()));

      const ResolvedExpr* order_expr = nullptr;
      if (frame_unit == ResolvedWindowFrame::RANGE) {
        if (window_ordering == nullptr ||
            window_ordering->order_by_item_list_size() != 1) {
          return InternalErrorBuilder()
                 << "Must have exactly one ordering key for a RANGE-based "
                    "window"
                 << " with an offset boundary:\n"
                 << window_ordering->DebugString();
        }
        order_expr = window_ordering->order_by_item_list(0)->column_ref();
      }

      GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedWindowFrameExprType(
          frame_unit, order_expr, *window_frame_expr->expression()));
      break;
    }
    default:
      return InternalErrorBuilder()
             << "Unhandled window boundary type:\n"
             << window_frame_expr->GetBoundaryTypeString();
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedWindowFrameExprType(
    const ResolvedWindowFrame::FrameUnit& frame_unit,
    const ResolvedExpr* window_ordering_expr,
    const ResolvedExpr& window_frame_expr) {
  switch (frame_unit) {
    case ResolvedWindowFrame::ROWS: {
      PushErrorContext push(this, &window_frame_expr);
      if (!window_frame_expr.type()->IsInt64()) {
        return InternalErrorBuilder()
               << "ROWS-based window boundary expression must be INT64 type, "
                  "but "
                  "has type "
               << window_frame_expr.type()->DebugString() << ":\n"
               << window_frame_expr.DebugString();
      }
      break;
    }
    case ResolvedWindowFrame::RANGE: {
      VALIDATOR_RET_CHECK(window_ordering_expr != nullptr);
      PushErrorContext push(this, window_ordering_expr);
      if (!window_ordering_expr->type()->IsNumerical()) {
        return InternalErrorBuilder()
               << "Ordering expression must be numeric type in a RANGE-based "
                  "window, but has type "
               << window_ordering_expr->type()->DebugString() << ":\n"
               << window_ordering_expr->DebugString();
      }
      if (!window_ordering_expr->type()->Equals(window_frame_expr.type())) {
        return InternalErrorBuilder()
               << "RANGE-based window boundary expression has a different type "
                  "with the ordering expression ("
               << window_frame_expr.type()->DebugString() << " vs. "
               << window_ordering_expr->type()->DebugString() << "):\n"
               << window_frame_expr.DebugString();
      }
      break;
    }
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedSetOperationItem(
    const ResolvedSetOperationItem* input_item,
    const ResolvedColumnList& output_column_list,
    const std::set<ResolvedColumn>& visible_parameters) {
  PushErrorContext push(this, input_item);
  const ResolvedScan* input_scan = input_item->scan();
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedScan(input_scan, visible_parameters));

  const std::set<ResolvedColumn> produced_columns(
      input_scan->column_list().begin(), input_scan->column_list().end());

  // <output_column_list>, which corresponds with the column list of the
  // enclosing set operation, matches 1:1 with the <output_column_list> from the
  // input item, but not necessarily with the input item's <input_scan>'s
  // <column_list>.
  VALIDATOR_RET_CHECK_EQ(input_item->output_column_list_size(),
                         output_column_list.size());
  for (int i = 0; i < output_column_list.size(); ++i) {
    const ResolvedColumn& input_column = input_item->output_column_list(i);
    VALIDATOR_RET_CHECK(
        output_column_list.at(i).type()->Equals(input_column.type()))
        << "The " << i
        << "-th SetOperation input column type does not match output type. "
        << "Input column " << input_column.DebugString()
        << " with type: " << input_column.type()->DebugString()
        << " Output column " << output_column_list.at(i).DebugString()
        << " with type: " << output_column_list.at(i).type()->DebugString();

    VALIDATOR_RET_CHECK(googlesql_base::ContainsKey(produced_columns, input_column))
        << "SetOperation input scan does not produce column referenced in "
           "output_column_list: "
        << input_column.DebugString();
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedSetOperationScan(
    const ResolvedSetOperationScan* set_op_scan,
    const std::set<ResolvedColumn>& visible_parameters) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, set_op_scan);
  VALIDATOR_RET_CHECK_GE(set_op_scan->input_item_list_size(), 2);

  for (const auto& input_item : set_op_scan->input_item_list()) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedSetOperationItem(
        input_item.get(), set_op_scan->column_list(), visible_parameters));
  }

  set_op_scan->op_type();  // Mark field as visited.

  switch (set_op_scan->column_match_mode()) {
    case ResolvedSetOperationScan::BY_POSITION: {
      // STRICT is the default and only-allowed column propagation mode for
      // BY_POSITION.
      VALIDATOR_RET_CHECK_EQ(set_op_scan->column_propagation_mode(),
                             ResolvedSetOperationScan::STRICT);
      break;
    }
    case ResolvedSetOperationScan::CORRESPONDING:
    case ResolvedSetOperationScan::CORRESPONDING_BY: {
      absl::flat_hash_set<IdString, IdStringCaseHash, IdStringCaseEqualFunc>
          column_names;
      for (const auto& column : set_op_scan->column_list()) {
        // No duplicate names are allowed.
        VALIDATOR_RET_CHECK(
            googlesql_base::InsertIfNotPresent(&column_names, column.name_id()));
        // No anonymous columns are allowed.
        VALIDATOR_RET_CHECK(!IsInternalAlias(column.name_id()));
      }
      break;
    }
  }

  // Each output column should be unique. In particular, set operation scans
  // should not reuse column ids from their inputs.
  for (const ResolvedColumn& column : set_op_scan->column_list()) {
    GOOGLESQL_RETURN_IF_ERROR(CheckUniqueColumnId(column));
  }

  return absl::OkStatus();
}
static bool IsResolvedLiteralOrParameter(ResolvedNodeKind kind) {
  return kind == RESOLVED_LITERAL || kind == RESOLVED_PARAMETER;
}

absl::Status Validator::ValidateArgumentIsInt64(
    const ResolvedExpr* expr, bool validate_constant_nonnegative,
    absl::string_view context_msg) {
  VALIDATOR_RET_CHECK(expr != nullptr);
  PushErrorContext push(this, expr);
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(
      /*visible_columns=*/{}, /*visible_parameters=*/{}, expr));

  VALIDATOR_RET_CHECK(expr->type()->IsInt64())
      << context_msg << " must take INT64";

  if (!validate_constant_nonnegative) {
    return absl::OkStatus();
  }

  VALIDATOR_RET_CHECK(IsResolvedLiteralOrParameter(expr->node_kind()) ||
                      (expr->node_kind() == RESOLVED_CAST &&
                       expr->type()->IsInt64() &&
                       IsResolvedLiteralOrParameter(
                           expr->GetAs<ResolvedCast>()->expr()->node_kind())))
      << context_msg
      << " arg is of incorrect node kind: " << expr->node_kind_string();

  if (expr->node_kind() == RESOLVED_LITERAL) {
    // If a literal, we can also validate its value.
    const Value value = expr->GetAs<ResolvedLiteral>()->value();
    VALIDATOR_RET_CHECK(value.type()->IsInt64());
    VALIDATOR_RET_CHECK(!value.is_null())
        << "Unexpected literal with null value: " << value.DebugString();
    VALIDATOR_RET_CHECK_GE(value.int64_value(), 0);
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateTableValuedFunction(
    const TableValuedFunction* tvf) {
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedLimitOffsetScan(
    const ResolvedLimitOffsetScan* scan,
    const std::set<ResolvedColumn>& visible_parameters) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, scan);

  if (scan->limit() == nullptr) {
    VALIDATOR_RET_CHECK(
        language_options_.LanguageFeatureEnabled(FEATURE_LIMIT_ALL))
        << "Limit cannot be null when FEATURE_LIMIT_ALL is disabled";
  }

  const bool validate_constant_nonnegative =
      !language_options_.LanguageFeatureEnabled(
          FEATURE_LIMIT_OFFSET_EXPRESSIONS);

  constexpr absl::string_view kClauseName = "LIMIT ... OFFSET ...";
  if (scan->limit() != nullptr) {
    // LIMIT is optional when FEATURE_LIMIT_ALL is enabled.
    GOOGLESQL_RETURN_IF_ERROR(ValidateArgumentIsInt64(
        scan->limit(), validate_constant_nonnegative, kClauseName));
  }
  if (scan->offset() != nullptr) {
    // OFFSET is optional.
    GOOGLESQL_RETURN_IF_ERROR(ValidateArgumentIsInt64(
        scan->offset(), validate_constant_nonnegative, kClauseName));
  }

  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedScan(scan->input_scan(), visible_parameters));
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedProjectScan(
    const ResolvedProjectScan* scan,
    const std::set<ResolvedColumn>& visible_parameters) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, scan);

  VALIDATOR_RET_CHECK(nullptr != scan->input_scan());
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedScan(scan->input_scan(), visible_parameters));

  std::set<ResolvedColumn> visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(
      AddColumnList(scan->input_scan()->column_list(), &visible_columns));
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedComputedColumnList(
      visible_columns, visible_parameters, scan->expr_list()));
  GOOGLESQL_RETURN_IF_ERROR(
      AddColumnsFromComputedColumnList(scan->expr_list(), &visible_columns));
  GOOGLESQL_RETURN_IF_ERROR(CheckColumnList(scan, visible_columns));

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedTVFScan(
    const ResolvedTVFScan* resolved_tvf_scan,
    const std::set<ResolvedColumn>& visible_parameters) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, resolved_tvf_scan);

  VALIDATOR_RET_CHECK(resolved_tvf_scan->signature() != nullptr);
  VALIDATOR_RET_CHECK(resolved_tvf_scan->function_call_signature() != nullptr);
  VALIDATOR_RET_CHECK_EQ(
      resolved_tvf_scan->argument_list_size(),
      resolved_tvf_scan->signature()->input_arguments().size());
  const FunctionSignature* function_call_signature =
      resolved_tvf_scan->function_call_signature().get();
  // It is possible the <function_call_signature> field is not set.
  if (function_call_signature != nullptr) {
    VALIDATOR_RET_CHECK_EQ(resolved_tvf_scan->argument_list_size(),
                           function_call_signature->NumConcreteArguments());
  }

  std::vector<int> table_argument_offsets;
  std::vector<int> descriptor_offsets;
  for (int arg_idx = 0; arg_idx < resolved_tvf_scan->argument_list_size();
       ++arg_idx) {
    const ResolvedFunctionArgument* resolved_arg =
        resolved_tvf_scan->argument_list(arg_idx);

    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedFunctionArgument(
        /*visible_columns=*/{}, visible_parameters, resolved_arg,
        /*allow_unset_argument_scan=*/true));

    if (resolved_arg->scan() != nullptr) {
      table_argument_offsets.push_back(arg_idx);
    }

    if (resolved_arg->descriptor_arg() != nullptr &&
        resolved_arg->descriptor_arg()->descriptor_column_list_size() > 0) {
      descriptor_offsets.push_back(arg_idx);
    }

    const FunctionArgumentType* concrete_arg =
        (function_call_signature == nullptr
             ? nullptr
             : &function_call_signature->ConcreteArgument(arg_idx));

    if (concrete_arg != nullptr) {
      // This block should eventually move to ValidateResolvedFunctionArgument,
      // once ResolvedFunctionCall supports more of these kinds.
      const SignatureArgumentKind kind = concrete_arg->kind();
      switch (kind) {
        case ARG_TYPE_RELATION:
          VALIDATOR_RET_CHECK(resolved_arg->scan() != nullptr);
          break;
        case ARG_TYPE_MODEL:
          VALIDATOR_RET_CHECK(resolved_arg->model() != nullptr);
          break;
        case ARG_TYPE_CONNECTION:
          VALIDATOR_RET_CHECK(resolved_arg->connection() != nullptr);
          break;
        case ARG_TYPE_DESCRIPTOR:
          VALIDATOR_RET_CHECK(resolved_arg->descriptor_arg() != nullptr);
          break;
        case ARG_TYPE_LAMBDA:
          VALIDATOR_RET_CHECK(resolved_arg->inline_lambda() != nullptr);
          break;
        case ARG_TYPE_SEQUENCE:
          VALIDATOR_RET_CHECK(resolved_arg->sequence() != nullptr);
          break;
        case ARG_TYPE_GRAPH:
          VALIDATOR_RET_CHECK(resolved_arg->graph() != nullptr);
          break;
        case ARG_TYPE_FIXED:
          // Descriptor argument types can resolve to be an integer offset so
          // we allow either an expr or a descriptor arg here.
          VALIDATOR_RET_CHECK(resolved_arg->expr() != nullptr ||
                              resolved_arg->descriptor_arg() != nullptr);
          break;
        default:
          VALIDATOR_RET_CHECK(resolved_arg->expr() != nullptr);
          break;
      }
    }

    // If the function signature specifies a required schema for the relation
    // argument, check that the column names and types are a superset of those
    // from the scan.
    if (resolved_arg->scan() != nullptr) {
      const TableValuedFunction* tvf = resolved_tvf_scan->tvf();
      GOOGLESQL_RETURN_IF_ERROR(ValidateTableValuedFunction(tvf));
      // This check is only done if the argument is a fixed relation where the
      // relation input schema is available. Table valued functions can have
      // templated argument types (e.g. ANY_TABLE) but those will not be
      // checked here.
      if (!tvf->GetSignature(0)->argument(arg_idx).IsFixedRelation()) {
        continue;
      }
      const FunctionArgumentTypeOptions& options =
          tvf->GetSignature(0)->argument(arg_idx).options();
      const TVFRelation& required_input_schema =
          options.relation_input_schema();

      VALIDATOR_RET_CHECK_LT(
          arg_idx, resolved_tvf_scan->signature()->input_arguments().size());
      const TVFInputArgumentType& tvf_signature_arg =
          resolved_tvf_scan->signature()->argument(arg_idx);
      VALIDATOR_RET_CHECK(tvf_signature_arg.is_relation());
      const TVFRelation& input_relation = tvf_signature_arg.relation();
      GOOGLESQL_RETURN_IF_ERROR(ValidateRelationSchemaInResolvedFunctionArgument(
          required_input_schema, input_relation, resolved_arg));
    } else if (resolved_arg->model() != nullptr) {
      if (concrete_arg != nullptr) {
        VALIDATOR_RET_CHECK(concrete_arg->IsModel());
      }
      GOOGLESQL_RETURN_IF_ERROR(ValidateTableValuedFunction(resolved_tvf_scan->tvf()));
    } else if (resolved_arg->expr() != nullptr) {
      VALIDATOR_RET_CHECK(resolved_arg->expr()->type() != nullptr);
      if (concrete_arg != nullptr) {
        VALIDATOR_RET_CHECK(concrete_arg->type() != nullptr);
        VALIDATOR_RET_CHECK(
            resolved_arg->expr()->type()->Equals(concrete_arg->type()));
      }
    }
  }

  // If there is any descriptor in tvf call with resolved column names, should
  // check table arguments to validate that those descriptor ResolvedColumns can
  // be projected from one of the table arguments.
  for (int descriptor_arg_index : descriptor_offsets) {
    const ResolvedFunctionArgument* resolved_descriptor_arg =
        resolved_tvf_scan->argument_list(descriptor_arg_index);
    const std::vector<ResolvedColumn>& descriptor_column_vector =
        resolved_descriptor_arg->argument_column_list();
    bool validationPassed = false;
    for (int table_arg_index : table_argument_offsets) {
      if (validationPassed) {
        break;
      }
      const ResolvedFunctionArgument* resolved_scan_arg =
          resolved_tvf_scan->argument_list(table_arg_index);
      const std::vector<ResolvedColumn>& table_argument_column_vector =
          resolved_scan_arg->argument_column_list();
      int descriptor_column_index;
      for (descriptor_column_index = 0;
           descriptor_column_index < descriptor_column_vector.size();
           descriptor_column_index++) {
        if (std::find(table_argument_column_vector.begin(),
                      table_argument_column_vector.end(),
                      descriptor_column_vector[descriptor_column_index]) !=
            table_argument_column_vector.end()) {
          break;
        }
      }

      if (descriptor_column_index == descriptor_column_vector.size()) {
        validationPassed = true;
      }
    }

    VALIDATOR_RET_CHECK(validationPassed)
        << "ResolvedDescriptor has at least one resolved column in "
           "descriptor_column_list that cannot be projected in any table "
           "argument in the same TVF call"
        << resolved_descriptor_arg->descriptor_arg()->DebugString();
  }

  // Verify the result schema is valid. Also verify the result schema in
  // `signature()` and `matched_signature` match.
  const TVFRelation& result_schema =
      resolved_tvf_scan->signature()->result_schema();
  if (result_schema.is_value_table()) {
    int64_t num_pseudo_columns = std::count_if(
        result_schema.columns().begin(), result_schema.columns().end(),
        [](const TVFSchemaColumn& column) { return column.is_pseudo_column; });
    VALIDATOR_RET_CHECK_EQ(1, result_schema.num_columns() - num_pseudo_columns);
    VALIDATOR_RET_CHECK(!result_schema.column(0).is_pseudo_column);
  } else {
    VALIDATOR_RET_CHECK_NE(0, result_schema.num_columns());
  }

  // Each output column should have a unique column id. Mark the column id
  // as seen and verify that we haven't already seen it.
  bool has_measure_columns = false;
  for (const ResolvedColumn& column : resolved_tvf_scan->column_list()) {
    GOOGLESQL_RETURN_IF_ERROR(CheckUniqueColumnId(column));
    if (column.type() != nullptr && column.type()->IsMeasureType()) {
      has_measure_columns = true;
    }
  }
  if (has_measure_columns) {
    VALIDATOR_RET_CHECK(
        language_options_.LanguageFeatureEnabled(FEATURE_MEASURES_TVF))
        << "TvfScan projects a measure column, but FEATURE_MEASURES_TVF is not "
           "enabled. Scan: "
        << resolved_tvf_scan->DebugString();
    const Table* result_table_schema =
        resolved_tvf_scan->signature()->result_table_schema();
    VALIDATOR_RET_CHECK_NE(result_table_schema, nullptr);
    GOOGLESQL_RETURN_IF_ERROR(
        ValidateMeasureColumns(resolved_tvf_scan, result_table_schema));
  }

  // Ideally, column_index_list should always be the same size as column_list.
  // However, for historical reasons, some clients don't provide a
  // column_index_list and they match the table column by ResolvedColumn name
  // instead (this violates the contract of ResolvedColumn name()). Therefore
  // there's an exception here to allow an empty column_index_list. Once all
  // the client migrations are done, this exception should be removed.
  //
  // TODO: Remove this exception.
  if (resolved_tvf_scan->column_index_list_size() > 0) {
    // Check that all columns have corresponding indexes.
    VALIDATOR_RET_CHECK_EQ(resolved_tvf_scan->column_list_size(),
                           resolved_tvf_scan->column_index_list_size());

    VALIDATOR_RET_CHECK_NE(resolved_tvf_scan->signature(), nullptr);
    const TVFRelation result_schema =
        resolved_tvf_scan->signature()->result_schema();

    // Check that all indexes are valid indexes into the TVF's result schema,
    // and that the type of the corresponding columns matches.
    for (int column_index = 0;
         column_index < resolved_tvf_scan->column_list_size(); ++column_index) {
      const int tvf_schema_column_index =
          resolved_tvf_scan->column_index_list()[column_index];
      VALIDATOR_RET_CHECK_GE(tvf_schema_column_index, 0);
      VALIDATOR_RET_CHECK_LT(tvf_schema_column_index,
                             result_schema.num_columns());
      VALIDATOR_RET_CHECK(
          resolved_tvf_scan->column_list(column_index)
              .type()
              ->Equals(result_schema.column(tvf_schema_column_index).type));
    }
  }

  if (resolved_tvf_scan->signature()->Is<TemplatedSQLTVFSignature>()) {
    auto* templated_tvf_signature =
        resolved_tvf_scan->signature()->GetAs<TemplatedSQLTVFSignature>();
    VALIDATOR_RET_CHECK(templated_tvf_signature->resolved_templated_query() !=
                        nullptr);
    googlesql_base::VarSetter<ArgumentKindSet> setter(&allowed_argument_kinds_,
                                      {ResolvedArgumentDefEnums::SCALAR});
    // Column ids inside a templated function call only need to be unique within
    // the body of the function.
    googlesql_base::VarSetter<absl::flat_hash_set<int>> column_ids_seen_setter(
        &column_ids_seen_, {});
    googlesql_base::VarSetter<bool> templated_function_call_setter(&in_templated_tvf_call_,
                                                   true);
    // Any context from an enclosing CREATE TABLE FUNCTION statement is not
    // relevant to the templated TVF call.
    googlesql_base::VarSetter<const ResolvedCreateTableFunctionStmt*>
        current_create_table_function_stmt_setter(
            &current_create_table_function_stmt_, nullptr);
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedQueryStmt(
        templated_tvf_signature->resolved_templated_query()));
  }

  GOOGLESQL_RETURN_IF_ERROR(ValidateHintList(resolved_tvf_scan->hint_list()));
  resolved_tvf_scan->tvf();  // Mark field as visited.
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedExecuteAsRoleScan(
    const ResolvedExecuteAsRoleScan* scan,
    const std::set<ResolvedColumn>& visible_parameters) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, scan);

  VALIDATOR_RET_CHECK(scan->input_scan() != nullptr);
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedScan(scan->input_scan(), visible_parameters));

  VALIDATOR_RET_CHECK_EQ(scan->column_list_size(),
                         scan->input_scan()->column_list().size());

  for (int i = 0; i < scan->column_list_size(); ++i) {
    const ResolvedColumn& output_column = scan->column_list(i);
    const ResolvedColumn& input_column = scan->input_scan()->column_list(i);

    VALIDATOR_RET_CHECK_EQ(output_column.type(), input_column.type());

    // We are checking name equality only as a sanity check, since our code
    // copies the names over. Names are used mainly in readability of tests and
    // sanity checks, not the query semantics. For query semantics, we rely on
    // column IDs. We validate column ID uniqueness immediately after in
    // CheckUniqueColumnId().
    VALIDATOR_RET_CHECK_EQ(output_column.name(), input_column.name());
  }

  // Make sure column ids are unique
  for (const ResolvedColumn& column : scan->column_list()) {
    GOOGLESQL_RETURN_IF_ERROR(CheckUniqueColumnId(column));
  }

  GOOGLESQL_RETURN_IF_ERROR(ValidateHintList(scan->hint_list()));

  VALIDATOR_RET_CHECK_NE(scan->original_inlined_view() == nullptr,
                         scan->original_inlined_tvf() == nullptr)
      << "Exactly one of original_inlined_view and original_inlined_tvf must "
         "be not null";

  if (scan->original_inlined_view() != nullptr) {
    VALIDATOR_RET_CHECK(scan->original_inlined_view()->Is<SQLView>());
  }

  if (scan->original_inlined_tvf() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateTableValuedFunction(scan->original_inlined_tvf()));
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedRelationArgumentScan(
    const ResolvedRelationArgumentScan* arg_ref,
    const std::set<ResolvedColumn>& visible_parameters) {
  PushErrorContext push(this, arg_ref);

  // If we're currently validating a ResolvedCreateTableFunctionStmt, find the
  // argument in the current CREATE TABLE FUNCTION statement with the same name
  // as 'arg_ref'.
  if (current_create_table_function_stmt_ != nullptr) {
    VALIDATOR_RET_CHECK(std::any_of(
        current_create_table_function_stmt_->argument_name_list().begin(),
        current_create_table_function_stmt_->argument_name_list().end(),
        [arg_ref](const std::string& arg_name) {
          return googlesql_base::CaseEqual(arg_ref->name(), arg_name);
        }));
  }

  // Make sure the produced columns are unique and mark them as seen.
  for (const ResolvedColumn& column : arg_ref->column_list()) {
    GOOGLESQL_RETURN_IF_ERROR(CheckUniqueColumnId(column));
  }

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedWithScan(
    const ResolvedWithScan* scan,
    const std::set<ResolvedColumn>& visible_parameters) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, scan);

  VALIDATOR_RET_CHECK_NE(scan->query(), nullptr);

  const int old_num_visible_with_entries = visible_with_entries_.size();

  for (const auto& with_entry : scan->with_entry_list()) {
    VALIDATOR_RET_CHECK(nullptr != with_entry);
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedScan(with_entry->with_subquery(),
                                         /*visible_parameters=*/{}));
    with_entry->with_query_name();  // Mark visited.

    visible_with_entries_.push_back(with_entry.get());
  }

  // The main query can be correlated. The aliased subqueries cannot.
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedScan(scan->query(), visible_parameters));

  // Remove added WithEntries. They aren't visible after this scan.
  GOOGLESQL_RET_CHECK_EQ(visible_with_entries_.size(),
               old_num_visible_with_entries + scan->with_entry_list().size());
  visible_with_entries_.resize(old_num_visible_with_entries);

  std::set<ResolvedColumn> visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(
      AddColumnList(scan->query()->column_list(), &visible_columns));
  GOOGLESQL_RETURN_IF_ERROR(CheckColumnList(scan, visible_columns));

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedWithRefScan(
    const ResolvedWithRefScan* scan) {
  scan->MarkFieldsAccessed();
  // Make sure column ids are unique
  for (const ResolvedColumn& column : scan->column_list()) {
    GOOGLESQL_RETURN_IF_ERROR(CheckUniqueColumnId(column));
  }
  const std::string& query_name = scan->with_query_name();
  // Find the referenced CTE in visible_with_entries_.
  const ResolvedWithEntry* with_entry = nullptr;
  for (int i = visible_with_entries_.size() - 1; i >= 0; --i) {
    if (visible_with_entries_[i]->with_query_name() == query_name) {
      with_entry = visible_with_entries_[i];
      break;
    }
  }
  VALIDATOR_RET_CHECK_NE(with_entry, nullptr)
      << "Validator failed to find ResolvedWithEntry for query name '"
      << query_name << "'.";

  VALIDATOR_RET_CHECK_EQ(with_entry->with_subquery()->column_list_size(),
                         scan->column_list_size())
      << "ResolvedWithRefScan must scan exactly the columns projected from the "
      << "with query";
  absl::flat_hash_set<int> seen_column_ids;
  for (int i = 0; i < scan->column_list_size(); ++i) {
    const ResolvedColumn& scan_column = scan->column_list(i);
    const ResolvedColumn& subquery_column =
        with_entry->with_subquery()->column_list(i);

    // Ensure that the union all between the enty columns and the scan columns
    // is distinct.
    VALIDATOR_RET_CHECK(seen_column_ids.insert(scan_column.column_id()).second)
        << "ResolvedWithRefScan must rename columns. Found duplicate column "
        << scan_column.DebugString();
    VALIDATOR_RET_CHECK(scan_column.type()->Equals(subquery_column.type()))
        << "Type mismatch between ResolvedWithRefScan and with query for "
        << "column " << scan_column.DebugString() << " has type "
        << scan_column.type()->DebugString() << " but expected "
        << subquery_column.type()->DebugString();
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedReturningClause(
    const ResolvedReturningClause* returning,
    std::set<ResolvedColumn>& visible_columns) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, returning);

  for (const auto& computed_column : returning->expr_list()) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(visible_columns,
                                         /*visible_parameters=*/{},
                                         computed_column->expr()));
  }
  GOOGLESQL_RETURN_IF_ERROR(AddColumnsFromComputedColumnList(returning->expr_list(),
                                                   &visible_columns));
  int output_size = returning->output_column_list_size();
  if (returning->action_column() != nullptr) {
    VALIDATOR_RET_CHECK_GE(output_size, 1);

    const ResolvedOutputColumn* action_column =
        returning->output_column_list().at(output_size - 1).get();
    VALIDATOR_RET_CHECK(action_column->column().type()->IsString());
    bool is_internal_name =
        IsInternalAlias(action_column->column().table_name());
    VALIDATOR_RET_CHECK_EQ(is_internal_name, true);
    output_size -= 1;
  }

  // Checks output columns other than the generated action column
  for (int i = 0; i < output_size; i++) {
    const auto& resolved_output_column = returning->output_column_list().at(i);
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedOutputColumn(visible_columns,
                                                 resolved_output_column.get()));
  }
  return absl::OkStatus();
}

void Validator::Reset(bool in_multi_stmt) {
  in_generalized_query_stmt_ = false;
  nested_recursive_scans_.clear();
  column_ids_seen_.clear();
  error_context_ = nullptr;
  unconsumed_side_effect_columns_.clear();
  current_input_scan_in_graph_linear_scan_stack_.clear();

  // These don't get reset between statements in a ResolvedMultiStmt, since
  // this state carries over across sub-statements.
  if (!in_multi_stmt) {
    context_stack_.clear();
    visible_with_entries_.clear();
  }
}

absl::Status Validator::ValidateResolvedStatement(
    const ResolvedStatement* statement) {
  return ValidateResolvedStatement(statement, /*in_multi_stmt=*/false);
}

absl::Status Validator::ValidateResolvedStatement(
    const ResolvedStatement* statement, bool in_multi_stmt) {
  Reset(in_multi_stmt);
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedStatementInternal(statement));
  return ValidateFinalState(in_multi_stmt);
}

absl::Status Validator::ValidateResolvedStatementInternal(
    const ResolvedStatement* statement) {
  VALIDATOR_RET_CHECK(nullptr != statement);
  PushErrorContext push(this, statement);

  in_generalized_query_stmt_ = false;
  absl::Status status;
  switch (statement->node_kind()) {
    case RESOLVED_QUERY_STMT:
      status = ValidateResolvedQueryStmt(statement->GetAs<ResolvedQueryStmt>());
      break;
    case RESOLVED_GENERALIZED_QUERY_STMT:
      in_generalized_query_stmt_ = true;
      status = ValidateResolvedGeneralizedQueryStmt(
          statement->GetAs<ResolvedGeneralizedQueryStmt>());
      break;
    case RESOLVED_MULTI_STMT:
      status = ValidateResolvedMultiStmt(statement->GetAs<ResolvedMultiStmt>());
      break;
    case RESOLVED_CREATE_WITH_ENTRY_STMT:
      status = ValidateCreateWithEntryStmt(
          statement->GetAs<ResolvedCreateWithEntryStmt>());
      break;
    case RESOLVED_EXPLAIN_STMT:
      status = ValidateResolvedStatementInternal(
          statement->GetAs<ResolvedExplainStmt>()->statement());
      break;
    case RESOLVED_CREATE_CONNECTION_STMT:
      status = ValidateResolvedCreateConnectionStmt(
          statement->GetAs<ResolvedCreateConnectionStmt>());
      break;
    case RESOLVED_CREATE_DATABASE_STMT:
      status = ValidateResolvedCreateDatabaseStmt(
          statement->GetAs<ResolvedCreateDatabaseStmt>());
      break;
    case RESOLVED_CREATE_INDEX_STMT:
      status = ValidateResolvedIndexStmt(
          statement->GetAs<ResolvedCreateIndexStmt>());
      break;
    case RESOLVED_CREATE_SCHEMA_STMT:
      status = ValidateResolvedCreateSchemaStmt(
          statement->GetAs<ResolvedCreateSchemaStmt>());
      break;
    case RESOLVED_CREATE_EXTERNAL_SCHEMA_STMT:
      status = ValidateResolvedCreateExternalSchemaStmt(
          statement->GetAs<ResolvedCreateExternalSchemaStmt>());
      break;
    case RESOLVED_CREATE_SNAPSHOT_TABLE_STMT:
      status = ValidateResolvedCreateSnapshotTableStmt(
          statement->GetAs<ResolvedCreateSnapshotTableStmt>());
      break;
    case RESOLVED_CREATE_TABLE_STMT:
      status = ValidateResolvedCreateTableStmt(
          statement->GetAs<ResolvedCreateTableStmt>());
      break;
    case RESOLVED_CREATE_TABLE_AS_SELECT_STMT:
      status = ValidateResolvedCreateTableAsSelectStmt(
          statement->GetAs<ResolvedCreateTableAsSelectStmt>());
      break;
    case RESOLVED_CREATE_MODEL_STMT:
      status = ValidateResolvedCreateModelStmt(
          statement->GetAs<ResolvedCreateModelStmt>());
      break;
    case RESOLVED_CREATE_VIEW_STMT:
      status = ValidateResolvedCreateViewStmt(
          statement->GetAs<ResolvedCreateViewStmt>());
      break;
    case RESOLVED_CREATE_MATERIALIZED_VIEW_STMT:
      status = ValidateResolvedCreateMaterializedViewStmt(
          statement->GetAs<ResolvedCreateMaterializedViewStmt>());
      break;
    case RESOLVED_CREATE_APPROX_VIEW_STMT:
      status = ValidateResolvedCreateApproxViewStmt(
          statement->GetAs<ResolvedCreateApproxViewStmt>());
      break;
    case RESOLVED_CREATE_EXTERNAL_TABLE_STMT:
      status = ValidateResolvedCreateExternalTableStmt(
          statement->GetAs<ResolvedCreateExternalTableStmt>());
      break;
    case RESOLVED_CREATE_PRIVILEGE_RESTRICTION_STMT:
      status = ValidateResolvedCreatePrivilegeRestrictionStmt(
          statement->GetAs<ResolvedCreatePrivilegeRestrictionStmt>());
      break;
    case RESOLVED_ALTER_PRIVILEGE_RESTRICTION_STMT:
      status = ValidateResolvedAlterPrivilegeRestrictionStmt(
          statement->GetAs<ResolvedAlterPrivilegeRestrictionStmt>());
      break;
    case RESOLVED_CREATE_ROW_ACCESS_POLICY_STMT:
      status = ValidateResolvedCreateRowAccessPolicyStmt(
          statement->GetAs<ResolvedCreateRowAccessPolicyStmt>());
      break;
    case RESOLVED_CREATE_CONSTANT_STMT:
      status = ValidateResolvedCreateConstantStmt(
          statement->GetAs<ResolvedCreateConstantStmt>());
      break;
    case RESOLVED_CREATE_FUNCTION_STMT:
      status = ValidateResolvedCreateFunctionStmt(
          statement->GetAs<ResolvedCreateFunctionStmt>());
      break;
    case RESOLVED_CREATE_TABLE_FUNCTION_STMT:
      status = ValidateResolvedCreateTableFunctionStmt(
          statement->GetAs<ResolvedCreateTableFunctionStmt>());
      break;
    case RESOLVED_CREATE_PROCEDURE_STMT:
      status = ValidateResolvedCreateProcedureStmt(
          statement->GetAs<ResolvedCreateProcedureStmt>());
      break;
    case RESOLVED_CREATE_SEQUENCE_STMT:
      status = ValidateResolvedCreateSequenceStmt(
          statement->GetAs<ResolvedCreateSequenceStmt>());
      break;
    case RESOLVED_CLONE_DATA_STMT:
      status = ValidateResolvedCloneDataStmt(
          statement->GetAs<ResolvedCloneDataStmt>());
      break;
    case RESOLVED_EXPORT_DATA_STMT:
      status = ValidateResolvedExportDataStmt(
          statement->GetAs<ResolvedExportDataStmt>());
      break;
    case RESOLVED_EXPORT_MODEL_STMT:
      status = ValidateResolvedExportModelStmt(
          statement->GetAs<ResolvedExportModelStmt>());
      break;
    case RESOLVED_EXPORT_METADATA_STMT:
      status = ValidateResolvedExportMetadataStmt(
          statement->GetAs<ResolvedExportMetadataStmt>());
      break;
    case RESOLVED_CALL_STMT:
      status = ValidateResolvedCallStmt(statement->GetAs<ResolvedCallStmt>());
      break;
    case RESOLVED_DEFINE_TABLE_STMT:
      status = ValidateResolvedDefineTableStmt(
          statement->GetAs<ResolvedDefineTableStmt>());
      break;
    case RESOLVED_DESCRIBE_STMT:
      status = ValidateResolvedDescribeStmt(
          statement->GetAs<ResolvedDescribeStmt>());
      break;
    case RESOLVED_SHOW_STMT:
      status = ValidateResolvedShowStmt(statement->GetAs<ResolvedShowStmt>());
      break;
    case RESOLVED_BEGIN_STMT:
      status = ValidateResolvedBeginStmt(statement->GetAs<ResolvedBeginStmt>());
      break;
    case RESOLVED_SET_TRANSACTION_STMT:
      status = ValidateResolvedSetTransactionStmt(
          statement->GetAs<ResolvedSetTransactionStmt>());
      break;
    case RESOLVED_COMMIT_STMT:
      status =
          ValidateResolvedCommitStmt(statement->GetAs<ResolvedCommitStmt>());
      break;
    case RESOLVED_ROLLBACK_STMT:
      status = ValidateResolvedRollbackStmt(
          statement->GetAs<ResolvedRollbackStmt>());
      break;
    case RESOLVED_START_BATCH_STMT:
      status = ValidateResolvedStartBatchStmt(
          statement->GetAs<ResolvedStartBatchStmt>());
      break;
    case RESOLVED_RUN_BATCH_STMT:
      status = ValidateResolvedRunBatchStmt(
          statement->GetAs<ResolvedRunBatchStmt>());
      break;
    case RESOLVED_ABORT_BATCH_STMT:
      status = ValidateResolvedAbortBatchStmt(
          statement->GetAs<ResolvedAbortBatchStmt>());
      break;
    case RESOLVED_UNDROP_STMT:
      status =
          ValidateResolvedUndropStmt(statement->GetAs<ResolvedUndropStmt>());
      break;
    case RESOLVED_DROP_STMT:
      status = ValidateResolvedDropStmt(statement->GetAs<ResolvedDropStmt>());
      break;
    case RESOLVED_DROP_MATERIALIZED_VIEW_STMT:
      status = ValidateResolvedDropMaterializedViewStmt(
          statement->GetAs<ResolvedDropMaterializedViewStmt>());
      break;
    case RESOLVED_DROP_FUNCTION_STMT:
      status = ValidateResolvedDropFunctionStmt(
          statement->GetAs<ResolvedDropFunctionStmt>());
      break;
    case RESOLVED_DROP_SNAPSHOT_TABLE_STMT:
      status = ValidateResolvedDropSnapshotTableStmt(
          statement->GetAs<ResolvedDropSnapshotTableStmt>());
      break;
    case RESOLVED_DROP_TABLE_FUNCTION_STMT:
      status = ValidateResolvedDropTableFunctionStmt(
          statement->GetAs<ResolvedDropTableFunctionStmt>());
      break;
    case RESOLVED_DROP_PRIVILEGE_RESTRICTION_STMT:
      status = ValidateResolvedDropPrivilegeRestrictionStmt(
          statement->GetAs<ResolvedDropPrivilegeRestrictionStmt>());
      break;
    case RESOLVED_DROP_ROW_ACCESS_POLICY_STMT:
      status = ValidateResolvedDropRowAccessPolicyStmt(
          statement->GetAs<ResolvedDropRowAccessPolicyStmt>());
      break;
    case RESOLVED_DROP_INDEX_STMT:
      status = ValidateResolvedDropIndexStmt(
          statement->GetAs<ResolvedDropIndexStmt>());
      break;
    case RESOLVED_GRANT_STMT:
      status = ValidateResolvedGrantStmt(statement->GetAs<ResolvedGrantStmt>());
      break;
    case RESOLVED_REVOKE_STMT:
      status =
          ValidateResolvedRevokeStmt(statement->GetAs<ResolvedRevokeStmt>());
      break;
    case RESOLVED_INSERT_STMT:
      status =
          ValidateResolvedInsertStmt(statement->GetAs<ResolvedInsertStmt>());
      break;
    case RESOLVED_DELETE_STMT:
      status =
          ValidateResolvedDeleteStmt(statement->GetAs<ResolvedDeleteStmt>());
      break;
    case RESOLVED_UPDATE_STMT:
      status =
          ValidateResolvedUpdateStmt(statement->GetAs<ResolvedUpdateStmt>());
      break;
    case RESOLVED_MERGE_STMT:
      status = ValidateResolvedMergeStmt(statement->GetAs<ResolvedMergeStmt>());
      break;
    case RESOLVED_TRUNCATE_STMT:
      status = ValidateResolvedTruncateStmt(
          statement->GetAs<ResolvedTruncateStmt>());
      break;
    case RESOLVED_ALTER_ROW_ACCESS_POLICY_STMT:
      status = ValidateResolvedAlterRowAccessPolicyStmt(
          statement->GetAs<ResolvedAlterRowAccessPolicyStmt>());
      break;
    case RESOLVED_ALTER_ALL_ROW_ACCESS_POLICIES_STMT:
      status = ValidateResolvedAlterAllRowAccessPoliciesStmt(
          statement->GetAs<ResolvedAlterAllRowAccessPoliciesStmt>());
      break;
    case RESOLVED_ALTER_INDEX_STMT:
      status = ValidateResolvedAlterIndexStmt(
          statement->GetAs<ResolvedAlterIndexStmt>());
      break;
    case RESOLVED_ALTER_MATERIALIZED_VIEW_STMT:
      status = ValidateResolvedAlterObjectStmt(
          statement->GetAs<ResolvedAlterMaterializedViewStmt>());
      break;
    case RESOLVED_ALTER_APPROX_VIEW_STMT:
      status = ValidateResolvedAlterObjectStmt(
          statement->GetAs<ResolvedAlterApproxViewStmt>());
      break;
    case RESOLVED_ALTER_MODEL_STMT:
      status = ValidateResolvedAlterObjectStmt(
          statement->GetAs<ResolvedAlterModelStmt>());
      break;
    case RESOLVED_ALTER_TABLE_SET_OPTIONS_STMT:
      status = ValidateResolvedAlterTableSetOptionsStmt(
          statement->GetAs<ResolvedAlterTableSetOptionsStmt>());
      break;
    case RESOLVED_ALTER_DATABASE_STMT:
      status = ValidateResolvedAlterObjectStmt(
          statement->GetAs<ResolvedAlterDatabaseStmt>());
      break;
    case RESOLVED_ALTER_SCHEMA_STMT:
      status = ValidateResolvedAlterObjectStmt(
          statement->GetAs<ResolvedAlterSchemaStmt>());
      break;
    case RESOLVED_ALTER_EXTERNAL_SCHEMA_STMT:
      status = ValidateResolvedAlterObjectStmt(
          statement->GetAs<ResolvedAlterExternalSchemaStmt>());
      break;
    case RESOLVED_ALTER_CONNECTION_STMT:
      status = ValidateResolvedAlterObjectStmt(
          statement->GetAs<ResolvedAlterConnectionStmt>());
      break;
    case RESOLVED_ALTER_SEQUENCE_STMT:
      status = ValidateResolvedAlterObjectStmt(
          statement->GetAs<ResolvedAlterSequenceStmt>());
      break;
    case RESOLVED_ALTER_TABLE_STMT:
      status = ValidateResolvedAlterObjectStmt(
          statement->GetAs<ResolvedAlterTableStmt>());
      break;
    case RESOLVED_ALTER_VIEW_STMT:
      status = ValidateResolvedAlterObjectStmt(
          statement->GetAs<ResolvedAlterViewStmt>());
      break;
    case RESOLVED_RENAME_STMT:
      status =
          ValidateResolvedRenameStmt(statement->GetAs<ResolvedRenameStmt>());
      break;
    case RESOLVED_IMPORT_STMT:
      status =
          ValidateResolvedImportStmt(statement->GetAs<ResolvedImportStmt>());
      break;
    case RESOLVED_MODULE_STMT:
      status =
          ValidateResolvedModuleStmt(statement->GetAs<ResolvedModuleStmt>());
      break;
    case RESOLVED_ANALYZE_STMT:
      status =
          ValidateResolvedAnalyzeStmt(statement->GetAs<ResolvedAnalyzeStmt>());
      break;
    case RESOLVED_ASSERT_STMT:
      status =
          ValidateResolvedAssertStmt(statement->GetAs<ResolvedAssertStmt>());
      break;
    case RESOLVED_ASSIGNMENT_STMT:
      status = ValidateResolvedAssignmentStmt(
          statement->GetAs<ResolvedAssignmentStmt>());
      break;
    case RESOLVED_EXECUTE_IMMEDIATE_STMT:
      status = ValidateResolvedExecuteImmediateStmt(
          statement->GetAs<ResolvedExecuteImmediateStmt>());
      break;
    case RESOLVED_CREATE_ENTITY_STMT:
      status = ValidateResolvedCreateEntityStmt(
          statement->GetAs<ResolvedCreateEntityStmt>());
      break;
    case RESOLVED_ALTER_ENTITY_STMT:
      status = ValidateResolvedAlterEntityStmt(
          statement->GetAs<ResolvedAlterEntityStmt>());
      break;
    case RESOLVED_AUX_LOAD_DATA_STMT:
      status = ValidateResolvedAuxLoadDataStmt(
          statement->GetAs<ResolvedAuxLoadDataStmt>());
      break;
    case RESOLVED_CREATE_PROPERTY_GRAPH_STMT:
      status = ValidateResolvedCreatePropertyGraphStmt(
          statement->GetAs<ResolvedCreatePropertyGraphStmt>());
      break;
    case RESOLVED_SUBPIPELINE_STMT:
      // ResolvedSubpipelineStmt allows generalized pipe operators.
      in_generalized_query_stmt_ = true;
      status = ValidateResolvedSubpipelineStmt(
          statement->GetAs<ResolvedSubpipelineStmt>());
      break;
    case RESOLVED_STATEMENT_WITH_PIPE_OPERATORS_STMT:
      status = ValidateResolvedStatementWithPipeOperatorsStmt(
          statement->GetAs<ResolvedStatementWithPipeOperatorsStmt>());
      break;
    default:
      VALIDATOR_RET_CHECK_FAIL() << "Cannot validate statement of type "
                                 << statement->node_kind_string();
  }

  if (status.ok()) {
    ValidateResolvedColumnsVisitor column_visitor;
    status = statement->Accept(&column_visitor);
    if (status.ok()) {
      // TODO ResolvedCloneDataStmt doesn't preserve columns
      // correctly so it triggers errors about referencing uncreated columns.
      if (statement->node_kind() != RESOLVED_CLONE_DATA_STMT) {
        status = column_visitor.CheckCreatedColumns();
      }
    }
  }

  if (!status.ok()) {
    if (status.code() == absl::StatusCode::kResourceExhausted) {
      // Don't wrap a resource exhausted status into internal error. This error
      // may still occur for a valid properly resolved expression (stack
      // exhaustion in case of deeply nested expression). There exist cases
      // where the validator uses more stack than parsing/analysis (b/65294961).
      return status;
    }
    return ::googlesql_base::InternalErrorBuilder()
           << "Resolved AST validation failed: " << status.message() << "\n"
           << statement->DebugString(ResolvedNode::DebugStringConfig{
                  {{error_context_, "(validation failed here)"}}, false});
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedCreateDatabaseStmt(
    const ResolvedCreateDatabaseStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);
  GOOGLESQL_RETURN_IF_ERROR(ValidateOptionsList(stmt->option_list()));
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedIndexStmt(
    const ResolvedCreateIndexStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);
  GOOGLESQL_RETURN_IF_ERROR(ValidateOptionsList(stmt->option_list()));

  VALIDATOR_RET_CHECK(stmt->table_scan() != nullptr);
  GOOGLESQL_RETURN_IF_ERROR(
      ValidateResolvedTableScan(stmt->table_scan(), /*visible_parameters=*/{}));
  std::set<ResolvedColumn> visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(
      AddColumnList(stmt->table_scan()->column_list(), &visible_columns));

  for (const auto& index_unnest_column : stmt->unnest_expressions_list()) {
    VALIDATOR_RET_CHECK(index_unnest_column->array_expr() != nullptr);
    VALIDATOR_RET_CHECK(index_unnest_column->array_expr()->type()->IsArray())
        << "CREATE INDEX Unnest non-ARRAY type: "
        << index_unnest_column->array_expr()->type()->DebugString();
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(visible_columns,
                                         /*visible_parameters=*/{},
                                         index_unnest_column->array_expr()));

    visible_columns.insert(index_unnest_column->element_column());
    if (index_unnest_column->array_offset_column() != nullptr) {
      visible_columns.insert(
          index_unnest_column->array_offset_column()->column());
    }
  }

  // is_search and is_vector are mutually exclusive.
  VALIDATOR_RET_CHECK(!stmt->is_search() || !stmt->is_vector());

  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedComputedColumnList(
      visible_columns,
      /*visible_parameters=*/{}, stmt->computed_columns_list()));
  GOOGLESQL_RETURN_IF_ERROR(AddColumnsFromComputedColumnList(
      stmt->computed_columns_list(), &visible_columns));

  if (stmt->index_all_columns()) {
    VALIDATOR_RET_CHECK(stmt->is_search());
    // When ALL_COLUMNS is used, the index_item_list is either empty, or
    // contains items with non-empty option_list.
    for (const auto& item : stmt->index_item_list()) {
      VALIDATOR_RET_CHECK(!item->option_list().empty());
    }
  }

  for (const auto& item : stmt->index_item_list()) {
    GOOGLESQL_RETURN_IF_ERROR(CheckColumnIsPresentInColumnSet(
        item->column_ref()->column(), visible_columns));
  }

  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExprList(visible_columns,
                                           /*visible_parameters=*/{},
                                           stmt->storing_expression_list()));
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExprList(visible_columns,
                                           /*visible_parameters=*/{},
                                           stmt->partition_by_list()));
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedCreateConnectionStmt(
    const ResolvedCreateConnectionStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);
  GOOGLESQL_RETURN_IF_ERROR(ValidateOptionsList(stmt->option_list()));
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedCreateSchemaStmt(
    const ResolvedCreateSchemaStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);
  if (stmt->collation_name() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateCollateExpr(stmt->collation_name()));
  }
  GOOGLESQL_RETURN_IF_ERROR(ValidateOptionsList(stmt->option_list()));
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedCreateExternalSchemaStmt(
    const ResolvedCreateExternalSchemaStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);
  GOOGLESQL_RETURN_IF_ERROR(ValidateOptionsList(stmt->option_list()));
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedForeignKey(
    const ResolvedForeignKey* foreign_key,
    const std::vector<const Type*> column_types,
    absl::flat_hash_set<std::string>* constraint_names) {
  PushErrorContext push(this, foreign_key);
  if (!foreign_key->constraint_name().empty()) {
    VALIDATOR_RET_CHECK(
        constraint_names->insert(foreign_key->constraint_name()).second)
        << "Duplicate constraint name: " << foreign_key->constraint_name();
  }
  const auto& referencing_offsets =
      foreign_key->referencing_column_offset_list();
  VALIDATOR_RET_CHECK(!referencing_offsets.empty())
      << "Missing foreign key column offsets";
  const auto& referenced_offsets = foreign_key->referenced_column_offset_list();
  VALIDATOR_RET_CHECK_EQ(referencing_offsets.size(), referenced_offsets.size())
      << "Size of " << referencing_offsets.size()
      << " for the foreign key referencing column offset list is not the "
      << "same as the size of " << referenced_offsets.size()
      << " for the referenced column offset list";
  absl::flat_hash_set<int> referencing_set;
  for (int offset : referencing_offsets) {
    VALIDATOR_RET_CHECK(offset >= 0 && offset < column_types.size())
        << "Invalid foreign key referencing column at offset " << offset;
    VALIDATOR_RET_CHECK(referencing_set.insert(offset).second)
        << "Duplicate foreign key referencing column at offset " << offset;
    VALIDATOR_RET_CHECK(
        column_types[offset]->SupportsEquality(language_options_))
        << "Foreign key referencing column at offset" << offset
        << " does not support equality";
  }
  const auto* referenced_table = foreign_key->referenced_table();
  VALIDATOR_RET_CHECK_NE(referenced_table, nullptr)
      << "Missing foreign key referenced table";
  absl::flat_hash_set<int> referenced_set;
  for (int offset : referenced_offsets) {
    VALIDATOR_RET_CHECK(offset >= 0 && offset < referenced_table->NumColumns())
        << "Invalid foreign key referenced column at offset " << offset;
    VALIDATOR_RET_CHECK(referenced_set.insert(offset).second)
        << "Duplicate foreign key referenced column at offset " << offset;
    const auto* type = referenced_table->GetColumn(offset)->GetType();
    VALIDATOR_RET_CHECK(type->SupportsEquality(language_options_))
        << "Foreign key referenced column at offset" << offset
        << " does not support equality";
  }

  return ValidateOptionsList(foreign_key->option_list());
}

absl::Status Validator::ValidateResolvedPrimaryKey(
    const std::vector<const Type*>& resolved_column_types,
    const ResolvedPrimaryKey* primary_key,
    absl::flat_hash_set<std::string>* seen_constraint_names) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, primary_key);

  if (!primary_key->constraint_name().empty()) {
    auto result = seen_constraint_names->insert(primary_key->constraint_name());
    VALIDATOR_RET_CHECK(result.second)
        << "Duplicate constraint name: " << primary_key->constraint_name();
  }

  if (resolved_column_types.empty()) {
    VALIDATOR_RET_CHECK_EQ(primary_key->column_offset_list_size(), 0);
  } else {
    VALIDATOR_RET_CHECK_EQ(primary_key->column_offset_list_size(),
                           primary_key->column_name_list_size());

    std::set<int> column_indexes;
    for (const int i : primary_key->column_offset_list()) {
      if (i >= resolved_column_types.size() || i < 0) {
        return InternalErrorBuilder()
               << "Invalid column index " << i << " in PRIMARY KEY";
      }
      if (googlesql_base::ContainsKey(column_indexes, i)) {
        return InternalErrorBuilder()
               << "Duplicate column index " << i << " in PRIMARY KEY";
      }
      column_indexes.insert(i);
    }
  }

  return ValidateOptionsList(primary_key->option_list());
}

absl::Status Validator::ValidateColumnDefinitions(
    absl::Span<const std::unique_ptr<const ResolvedColumnDefinition>>
        column_definitions,
    std::set<ResolvedColumn>* visible_columns) {
  for (const auto& column_definition : column_definitions) {
    if (!googlesql_base::InsertIfNotPresent(visible_columns,
                                 column_definition->column())) {
      return InternalErrorBuilder()
             << "Column already used: "
             << column_definition->column().DebugString();
    }
  }
  for (const auto& column_definition : column_definitions) {
    if (column_definition->annotations() != nullptr) {
      GOOGLESQL_RETURN_IF_ERROR(
          ValidateColumnAnnotations(column_definition->annotations()));
      GOOGLESQL_ASSIGN_OR_RETURN(TypeParameters full_type_parameters,
                       column_definition->GetFullTypeParameters());
      GOOGLESQL_RETURN_IF_ERROR(column_definition->type()->ValidateResolvedTypeParameters(
          full_type_parameters, language_options_.product_mode()));
    }
    VALIDATOR_RET_CHECK(column_definition->type() != nullptr);
    if (column_definition->generated_column_info() != nullptr) {
      VALIDATOR_RET_CHECK(column_definition->default_value() == nullptr);
      GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedGeneratedColumnInfo(
          column_definition.get(), *visible_columns));
    }

    if (column_definition->default_value() != nullptr) {
      VALIDATOR_RET_CHECK(column_definition->generated_column_info() ==
                          nullptr);
      GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedColumnDefaultValue(
          column_definition->default_value(), column_definition->type()));
    }
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedCreateTableStmtBase(
    const ResolvedCreateTableStmtBase* stmt,
    std::set<ResolvedColumn>* visible_columns) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);
  GOOGLESQL_RETURN_IF_ERROR(ValidateOptionsList(stmt->option_list()));
  GOOGLESQL_RETURN_IF_ERROR(ValidateColumnDefinitions(stmt->column_definition_list(),
                                            visible_columns));

  const Table* like_table = stmt->like_table();
  if (like_table != nullptr) {
    // Column information in the like_table will be extracted into column
    // definition list.
    VALIDATOR_RET_CHECK_EQ(like_table->NumColumns(), visible_columns->size())
        << "Number of columns does not match in like_table and column "
           "definition list";
  }
  for (const auto& pseudo_column : stmt->pseudo_column_list()) {
    if (!googlesql_base::InsertIfNotPresent(visible_columns, pseudo_column)) {
      return InternalErrorBuilder()
             << "Column already used: " << pseudo_column.DebugString();
    }
  }

  // Used to detect duplicate constraint names.
  absl::flat_hash_set<std::string> constraint_names;

  if (stmt->primary_key() != nullptr) {
    std::vector<const Type*> column_types;
    for (const auto& column_definition : stmt->column_definition_list()) {
      column_types.push_back(column_definition->type());
    }
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedPrimaryKey(
        column_types, stmt->primary_key(), &constraint_names));
  }

  // Validate foreign keys.
  VALIDATOR_RET_CHECK(
      stmt->foreign_key_list().empty() ||
      language_options_.LanguageFeatureEnabled(FEATURE_FOREIGN_KEYS))
      << "Foreign keys are not supported";
  const auto& column_definitions = stmt->column_definition_list();
  std::vector<const Type*> column_types;
  column_types.reserve(column_definitions.size());
  for (const auto& column_definition : column_definitions) {
    column_types.push_back(column_definition->type());
  }
  for (const auto& foreign_key : stmt->foreign_key_list()) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedForeignKey(foreign_key.get(), column_types,
                                               &constraint_names));
  }

  // Validate check constraints.
  VALIDATOR_RET_CHECK(
      stmt->check_constraint_list().empty() ||
      language_options_.LanguageFeatureEnabled(FEATURE_CHECK_CONSTRAINT));
  for (const auto& check_constraint : stmt->check_constraint_list()) {
    if (!check_constraint->constraint_name().empty()) {
      VALIDATOR_RET_CHECK(
          constraint_names.insert(check_constraint->constraint_name()).second)
          << "Duplicate constraint name: "
          << check_constraint->constraint_name();
    }
    VALIDATOR_RET_CHECK(check_constraint->expression() != nullptr)
        << "Missing expression in ABSL_CHECK constraint";
    VALIDATOR_RET_CHECK(check_constraint->expression()->type()->IsBool())
        << "CHECK constraint expects a boolean expression; got "
        << check_constraint->expression()->type()->ShortTypeName(
               language_options_.product_mode());
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(*visible_columns,
                                         /*visible_parameters=*/{},
                                         check_constraint->expression()));
  }

  // Validate collation.
  if (stmt->collation_name() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateCollateExpr(stmt->collation_name()));
  }

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedCreateTableStmt(
    const ResolvedCreateTableStmt* stmt) {
  PushErrorContext push(this, stmt);
  VALIDATOR_RET_CHECK(stmt->like_table() == nullptr ||
                      stmt->clone_from() == nullptr)
      << "CLONE and LIKE cannot both be used for CREATE TABLE";
  VALIDATOR_RET_CHECK(stmt->like_table() == nullptr ||
                      stmt->copy_from() == nullptr)
      << "COPY and LIKE cannot both be used for CREATE TABLE";
  VALIDATOR_RET_CHECK(stmt->clone_from() == nullptr ||
                      stmt->copy_from() == nullptr)
      << "COPY and CLONE cannot both be used for CREATE TABLE";
  if (stmt->clone_from() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedCloneDataSource(stmt->clone_from()));
  }
  if (stmt->copy_from() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedCloneDataSource(stmt->copy_from()));
  }
  // Build and store the list of visible_columns.
  std::set<ResolvedColumn> visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedCreateTableStmtBase(stmt, &visible_columns));
  for (const auto& partition_by_expr : stmt->partition_by_list()) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(
        visible_columns, /*visible_parameters=*/{}, partition_by_expr.get()));
  }
  for (const auto& cluster_by_expr : stmt->cluster_by_list()) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(
        visible_columns, /*visible_parameters=*/{}, cluster_by_expr.get()));
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedCreateSnapshotTableStmt(
    const ResolvedCreateSnapshotTableStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);

  VALIDATOR_RET_CHECK(stmt->clone_from() != nullptr)
      << "CLONE must be provided for CREATE SNAPSHOT TABLE";
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedCloneDataSource(stmt->clone_from()));

  GOOGLESQL_RETURN_IF_ERROR(ValidateOptionsList(stmt->option_list()));

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedIdentityColumnInfo(
    const Type* column_type,
    const ResolvedIdentityColumnInfo* identity_column_info,
    bool skip_check_type_match) {
  // Check that identity column attributes are not null.
  VALIDATOR_RET_CHECK(!identity_column_info->start_with_value().is_null());
  VALIDATOR_RET_CHECK(!identity_column_info->increment_by_value().is_null());
  VALIDATOR_RET_CHECK(!identity_column_info->min_value().is_null());
  VALIDATOR_RET_CHECK(!identity_column_info->max_value().is_null());

  // Check that the types of identity column attributes match the column type.
  if (!skip_check_type_match) {
    VALIDATOR_RET_CHECK(column_type != nullptr);
    VALIDATOR_RET_CHECK(column_type->IsInteger());
    VALIDATOR_RET_CHECK(
        column_type->Equals(identity_column_info->start_with_value().type()));
    VALIDATOR_RET_CHECK(
        column_type->Equals(identity_column_info->increment_by_value().type()));
    VALIDATOR_RET_CHECK(
        column_type->Equals(identity_column_info->max_value().type()));
    VALIDATOR_RET_CHECK(
        column_type->Equals(identity_column_info->min_value().type()));
  }

  // Check that MINVALUE <= START WITH <= MAXVALUE.
  VALIDATOR_RET_CHECK(!identity_column_info->start_with_value().LessThan(
      identity_column_info->min_value()));
  VALIDATOR_RET_CHECK(!identity_column_info->max_value().LessThan(
      identity_column_info->start_with_value()));
  // Check that INCREMENT BY is not 0.
  VALIDATOR_RET_CHECK(
      !identity_column_info->increment_by_value().Equals(Value::Int64(0)));
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedGeneratedColumnInfo(
    const ResolvedColumnDefinition* column_definition,
    const std::set<ResolvedColumn>& visible_columns) {
  PushErrorContext push(this, column_definition);
  const ResolvedGeneratedColumnInfo* generated_column_info =
      column_definition->generated_column_info();

  // Validate when the generated column is an identity column.
  if (const ResolvedIdentityColumnInfo* identity_col =
          generated_column_info->identity_column_info();
      identity_col != nullptr) {
    VALIDATOR_RET_CHECK(generated_column_info->expression() == nullptr);
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedIdentityColumnInfo(
        column_definition->type(), identity_col));
  } else {
    // Validate when the generated column is an expression.
    VALIDATOR_RET_CHECK(generated_column_info->expression() != nullptr);
    VALIDATOR_RET_CHECK(generated_column_info->GetParseLocationRangeOrNULL() !=
                        nullptr);
    VALIDATOR_RET_CHECK(generated_column_info->identity_column_info() ==
                        nullptr);

    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(visible_columns,
                                         /*visible_parameters=*/{},
                                         generated_column_info->expression()));
    VALIDATOR_RET_CHECK(generated_column_info->expression()->type() != nullptr);
    VALIDATOR_RET_CHECK(column_definition->type() != nullptr);
    VALIDATOR_RET_CHECK(generated_column_info->expression()->type()->Equals(
        column_definition->type()));
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedCreateTableAsSelectStmt(
    const ResolvedCreateTableAsSelectStmt* stmt,
    const std::set<ResolvedColumn>& pipe_visible_parameters) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);

  VALIDATOR_RET_CHECK(stmt->query() != nullptr);
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedScan(stmt->query(), pipe_visible_parameters));

  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedOutputColumnList(stmt->query()->column_list(),
                                                   stmt->output_column_list(),
                                                   stmt->is_value_table()));
  const int num_columns = stmt->column_definition_list_size();
  if (num_columns != stmt->output_column_list_size()) {
    return InternalErrorBuilder()
           << "Inconsistent length between column definition list ("
           << stmt->column_definition_list_size()
           << ") and output column list (" << stmt->output_column_list_size()
           << ")";
  }
  for (int i = 0; i < num_columns; ++i) {
    const ResolvedOutputColumn* output_column = stmt->output_column_list(i);
    const ResolvedColumnDefinition* column_def =
        stmt->column_definition_list(i);
    const std::string& output_column_name = output_column->name();
    const std::string& column_def_name = column_def->name();
    if (output_column_name != column_def_name) {
      return InternalErrorBuilder()
             << "Output column name '" << output_column_name
             << "' is different from column definition name '"
             << column_def_name << "' for column " << (i + 1);
    }
    const Type* output_type = output_column->column().type();
    const Type* defined_type = column_def->type();
    if (!output_type->Equals(defined_type)) {
      return InternalErrorBuilder()
             << "Output column type " << output_type->DebugString()
             << " is different from column definition type "
             << defined_type->DebugString() << " for column " << (i + 1) << " ("
             << column_def_name << ")";
    }
  }
  std::set<ResolvedColumn> visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedCreateTableStmtBase(stmt, &visible_columns));
  for (const auto& partition_by_expr : stmt->partition_by_list()) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(
        visible_columns, /*visible_parameters=*/{}, partition_by_expr.get()));
  }
  for (const auto& cluster_by_expr : stmt->cluster_by_list()) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(
        visible_columns, /*visible_parameters=*/{}, cluster_by_expr.get()));
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedCreateModelStmt(
    const ResolvedCreateModelStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);

  auto validate_input_output_columns = [this, stmt]() -> absl::Status {
    std::set<ResolvedColumn> visible_input_columns;
    GOOGLESQL_RETURN_IF_ERROR(ValidateColumnDefinitions(
        stmt->input_column_definition_list(), &visible_input_columns));
    std::set<ResolvedColumn> visible_output_columns;
    GOOGLESQL_RETURN_IF_ERROR(ValidateColumnDefinitions(
        stmt->output_column_definition_list(), &visible_output_columns));
    return absl::OkStatus();
  };

  bool enable_aliased_query_list = language_options_.LanguageFeatureEnabled(
      FEATURE_CREATE_MODEL_WITH_ALIASED_QUERY_LIST);
  bool enable_remote_model =
      language_options_.LanguageFeatureEnabled(FEATURE_REMOTE_MODEL);
  if (stmt->is_remote()) {
    // Remote model.
    VALIDATOR_RET_CHECK(enable_remote_model) << "Remote model is not supported";
    VALIDATOR_RET_CHECK(stmt->transform_list().empty())
        << "Remote model training with transform list is not supported";
    VALIDATOR_RET_CHECK(stmt->transform_input_column_list().empty());
    VALIDATOR_RET_CHECK(stmt->transform_output_column_list().empty());
    VALIDATOR_RET_CHECK(stmt->transform_analytic_function_group_list().empty());
    VALIDATOR_RET_CHECK(stmt->aliased_query_list().empty())
        << "Remote model training with aliased query list is not supported";

    if (stmt->query() == nullptr) {
      // External model.
      // Input & output are optional.
      GOOGLESQL_RETURN_IF_ERROR(validate_input_output_columns());

      // The rest must be empty.
      VALIDATOR_RET_CHECK(stmt->output_column_list().empty());
    } else {
      // Remotely trained model with AS SELECT clause.
      GOOGLESQL_RETURN_IF_ERROR(
          ValidateResolvedScan(stmt->query(), /*visible_parameters=*/{}));
      GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedOutputColumnList(
          stmt->query()->column_list(), stmt->output_column_list(),
          /*is_value_table=*/false));
    }
  } else {
    // Local model.
    VALIDATOR_RET_CHECK_EQ(stmt->connection(), nullptr);

    if (!enable_remote_model) {
      // If aliased query list is not allowed, then as select clause must
      // present and aliased query list must be empty.
      if (!enable_aliased_query_list) {
        VALIDATOR_RET_CHECK_NE(stmt->query(), nullptr);
        VALIDATOR_RET_CHECK(stmt->aliased_query_list().empty());
      } else {
        // Otherwise either as select clause or aliased query must present.
        VALIDATOR_RET_CHECK(stmt->query() != nullptr ||
                            !stmt->aliased_query_list().empty());
      }
    }

    if (stmt->query() == nullptr && stmt->aliased_query_list().empty()) {
      // Imported model.

      // Input & output are optional.
      GOOGLESQL_RETURN_IF_ERROR(validate_input_output_columns());

      // The rest must be empty.
      VALIDATOR_RET_CHECK(stmt->output_column_list().empty());
      VALIDATOR_RET_CHECK(stmt->transform_list().empty());
      VALIDATOR_RET_CHECK(stmt->transform_input_column_list().empty());
      VALIDATOR_RET_CHECK(stmt->transform_output_column_list().empty());
      VALIDATOR_RET_CHECK(
          stmt->transform_analytic_function_group_list().empty());
    }
    if (stmt->query() != nullptr && stmt->aliased_query_list().empty()) {
      // Locally trained model with AS SELECT clause.
      GOOGLESQL_RETURN_IF_ERROR(
          ValidateResolvedScan(stmt->query(), /*visible_parameters=*/{}));
      GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedOutputColumnList(
          stmt->query()->column_list(), stmt->output_column_list(),
          /*is_value_table=*/false));

      if (!stmt->transform_list().empty()) {
        // Validate transform_input_column_list is used properly in
        // transform_list.
        std::set<ResolvedColumn> transform_input_cols;
        for (const auto& transform_input_col :
             stmt->transform_input_column_list()) {
          transform_input_cols.insert(transform_input_col->column());
        }
        for (const auto& group :
             stmt->transform_analytic_function_group_list()) {
          GOOGLESQL_RETURN_IF_ERROR(AddColumnsFromComputedColumnList(
              group->analytic_function_list(), &transform_input_cols));
        }
        GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedComputedColumnList(
            transform_input_cols, {}, stmt->transform_list()));

        // Validate transform_list is used properly in
        // transform_output_column_list.
        std::vector<ResolvedColumn> transform_resolved_cols;
        for (const auto& transform_col : stmt->transform_list()) {
          transform_resolved_cols.push_back(transform_col->column());
        }
        GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedOutputColumnList(
            transform_resolved_cols, stmt->transform_output_column_list(),
            /*is_value_table=*/false));
      } else {
        VALIDATOR_RET_CHECK(stmt->transform_input_column_list().empty());
        VALIDATOR_RET_CHECK(stmt->transform_output_column_list().empty());
        VALIDATOR_RET_CHECK(
            stmt->transform_analytic_function_group_list().empty());
      }

      // The rest must be empty.
      VALIDATOR_RET_CHECK(stmt->input_column_definition_list().empty());
      VALIDATOR_RET_CHECK(stmt->output_column_definition_list().empty());
    }
    if (stmt->query() == nullptr && !stmt->aliased_query_list().empty()) {
      // Locally trained model with aliased query list.
      VALIDATOR_RET_CHECK(enable_aliased_query_list)
          << "Aliased query list is not supported";
      std::set<std::string> alias_names;
      for (const auto& aliased_query : stmt->aliased_query_list()) {
        const std::string alias_string = aliased_query->alias();
        // Checks for duplicate aliases.
        if (!googlesql_base::InsertIfNotPresent(&alias_names, alias_string)) {
          VALIDATOR_RET_CHECK_FAIL() << "Duplicate alias " << alias_string
                                     << " for aliased query list";
        }
        GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedScan(aliased_query->query(),
                                             /*visible_parameters=*/{}));
        GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedOutputColumnList(
            aliased_query->query()->column_list(),
            aliased_query->output_column_list(),
            /*is_value_table=*/false));
      }
      // TODO: lift these restrictions once TRANSFORM is supported
      // on aliased query list.
      VALIDATOR_RET_CHECK(stmt->transform_list().empty());
      VALIDATOR_RET_CHECK(stmt->transform_input_column_list().empty());
      VALIDATOR_RET_CHECK(stmt->transform_output_column_list().empty());
      VALIDATOR_RET_CHECK(
          stmt->transform_analytic_function_group_list().empty());

      // The rest must be empty.
      VALIDATOR_RET_CHECK(stmt->input_column_definition_list().empty());
      VALIDATOR_RET_CHECK(stmt->output_column_definition_list().empty());
    }
    if (stmt->query() != nullptr && !stmt->aliased_query_list().empty()) {
      VALIDATOR_RET_CHECK_FAIL()
          << "Query and aliased query list cannot coexist";
    }
  }

  GOOGLESQL_RETURN_IF_ERROR(ValidateOptionsList(stmt->option_list()));
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedCreateViewBase(
    const ResolvedCreateViewBase* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);
  GOOGLESQL_RETURN_IF_ERROR(
      ValidateResolvedScan(stmt->query(), /*visible_parameters=*/{}));
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedOutputColumnList(stmt->query()->column_list(),
                                                   stmt->output_column_list(),
                                                   stmt->is_value_table()));
  GOOGLESQL_RETURN_IF_ERROR(ValidateOptionsList(stmt->option_list()));
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedCreateViewStmt(
    const ResolvedCreateViewStmt* stmt) {
  return ValidateResolvedCreateViewBase(stmt);
}

absl::Status Validator::ValidateResolvedCreateMaterializedViewStmt(
    const ResolvedCreateMaterializedViewStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);
  if (stmt->query() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(
        ValidateResolvedScan(stmt->query(), /*visible_parameters=*/{}));
    VALIDATOR_RET_CHECK_EQ(stmt->replica_source(), nullptr)
        << "Query cannot be specified with AS REPLICA OF";
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedOutputColumnList(
        stmt->query()->column_list(), stmt->output_column_list(),
        stmt->is_value_table()));
  }
  if (stmt->replica_source() != nullptr) {
    VALIDATOR_RET_CHECK_EQ(stmt->query(), nullptr)
        << "Query cannot be specified with AS REPLICA OF";
    VALIDATOR_RET_CHECK_EQ(stmt->sql_security(),
                           ResolvedCreateStatement::SQL_SECURITY_UNSPECIFIED)
        << "SQL SECURITY options cannot be specified with AS REPLICA OF";
  }
  GOOGLESQL_RETURN_IF_ERROR(ValidateOptionsList(stmt->option_list()));
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedCreateApproxViewStmt(
    const ResolvedCreateApproxViewStmt* stmt) {
  return ValidateResolvedCreateViewBase(stmt);
}

absl::Status Validator::ValidateResolvedWithPartitionColumns(
    const ResolvedWithPartitionColumns* with_partition_columns,
    std::set<ResolvedColumn>* visible_columns) {
  PushErrorContext push(this, with_partition_columns);
  if (with_partition_columns != nullptr) {
    for (const auto& column_definition :
         with_partition_columns->column_definition_list()) {
      if (!googlesql_base::InsertIfNotPresent(visible_columns,
                                   column_definition->column())) {
        return InternalErrorBuilder()
               << "Column already used: "
               << column_definition->column().DebugString();
      }
    }

    for (const auto& column_definition :
         with_partition_columns->column_definition_list()) {
      // column annotation are not supported for column definition in
      // with_partition_columns.
      VALIDATOR_RET_CHECK(column_definition->annotations() == nullptr);
      VALIDATOR_RET_CHECK(column_definition->type() != nullptr);
    }
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedCreateExternalTableStmt(
    const ResolvedCreateExternalTableStmt* stmt) {
  PushErrorContext push(this, stmt);
  // Build the visible_columns.
  std::set<ResolvedColumn> visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedCreateTableStmtBase(stmt, &visible_columns));
  return ValidateResolvedWithPartitionColumns(stmt->with_partition_columns(),
                                              &visible_columns);
}

absl::Status Validator::ValidateResolvedCreatePrivilegeRestrictionStmt(
    const ResolvedCreatePrivilegeRestrictionStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);
  VALIDATOR_RET_CHECK(!stmt->column_privilege_list().empty());
  VALIDATOR_RET_CHECK(!stmt->name_path().empty());
  VALIDATOR_RET_CHECK(absl::AsciiStrToLower(stmt->object_type()) == "table" ||
                      absl::AsciiStrToLower(stmt->object_type()) == "view");
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedCreateRowAccessPolicyStmt(
    const ResolvedCreateRowAccessPolicyStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);

  VALIDATOR_RET_CHECK(stmt->table_scan() != nullptr);
  GOOGLESQL_RETURN_IF_ERROR(
      ValidateResolvedTableScan(stmt->table_scan(), /*visible_parameters=*/{}));
  std::set<ResolvedColumn> visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(
      AddColumnList(stmt->table_scan()->column_list(), &visible_columns));

  const ResolvedExpr* predicate = stmt->predicate();
  VALIDATOR_RET_CHECK(predicate != nullptr);
  VALIDATOR_RET_CHECK(predicate->type()->IsBool())
      << "CreateRowAccessPolicyStmt has predicate with non-BOOL type: "
      << predicate->type()->DebugString();
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(visible_columns,
                                       /*visible_parameters=*/{}, predicate));

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedCreateConstantStmt(
    const ResolvedCreateConstantStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);
  VALIDATOR_RET_CHECK(stmt->expr() != nullptr);
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(
      /*visible_columns=*/{}, /*visible_parameters=*/{}, stmt->expr()));
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedCreateFunctionStmt(
    const ResolvedCreateFunctionStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);
  bool contains_templated_args =
      std::any_of(stmt->signature().arguments().begin(),
                  stmt->signature().arguments().end(),
                  [](const FunctionArgumentType& arg_type) {
                    return arg_type.IsTemplated();
                  });
  if (contains_templated_args) {
    // This function declaration contained one or more templated argument types.
    // In this case the resolver sets the 'language' field to SQL and the 'code'
    // field to contain the original string contents of the SQL expression body.
    // The 'arguments' are empty in this case since the templated type
    // information is present in the function signature instead.
    if (stmt->signature().result_type().IsTemplated()) {
      VALIDATOR_RET_CHECK_EQ("SQL", stmt->language());
    }
    VALIDATOR_RET_CHECK(stmt->function_expression() == nullptr);
    if (!stmt->is_remote()) {
      VALIDATOR_RET_CHECK(!stmt->code().empty());
    }
  } else {
    // This function declaration did not contain any templated argument types.
    // In this case the function should have a concrete return type.
    VALIDATOR_RET_CHECK(
        stmt->return_type()->Equals(stmt->signature().result_type().type()));
  }
  VALIDATOR_RET_CHECK(stmt->return_type() != nullptr);

  // For non-aggregates, no columns are visible.  For aggregates, columns
  // created by the aggregate expressions are visible.
  std::set<ResolvedColumn> visible_columns;

  if (!stmt->aggregate_expression_list().empty()) {
    VALIDATOR_RET_CHECK(stmt->is_aggregate());
    VALIDATOR_RET_CHECK(stmt->function_expression() != nullptr);

    googlesql_base::VarSetter<ArgumentKindSet> setter(
        &allowed_argument_kinds_, {ResolvedArgumentDefEnums::AGGREGATE,
                                   ResolvedArgumentDefEnums::NOT_AGGREGATE});

    for (const auto& computed_column : stmt->aggregate_expression_list()) {
      VALIDATOR_RET_CHECK(
          computed_column->expr()->Is<ResolvedAggregateFunctionCall>());
      GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedAggregateFunctionCall(
          /*visible_columns=*/{}, /*visible_parameters=*/{},
          computed_column->expr()->GetAs<ResolvedAggregateFunctionCall>()));
      GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedAggregateComputedColumn(
          computed_column.get(),
          /*input_scan_visible_columns=*/{}, /*visible_parameters=*/{},
          /*available_side_effect_columns=*/{}));
    }

    GOOGLESQL_RETURN_IF_ERROR(AddColumnsFromComputedColumnList(
        stmt->aggregate_expression_list(), &visible_columns));
  }

  if (stmt->function_expression() != nullptr) {
    googlesql_base::VarSetter<ArgumentKindSet> setter(
        &allowed_argument_kinds_,
        {stmt->is_aggregate() ? ResolvedArgumentDefEnums::NOT_AGGREGATE
                              : ResolvedArgumentDefEnums::SCALAR});

    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(visible_columns,
                                         /*visible_parameters=*/{},
                                         stmt->function_expression()));
    VALIDATOR_RET_CHECK(
        stmt->function_expression()->type()->Equals(stmt->return_type()));

    // Check no query parameter in function body.
    std::vector<const ResolvedNode*> parameter_nodes;
    stmt->function_expression()->GetDescendantsWithKinds({RESOLVED_PARAMETER},
                                                         &parameter_nodes);
    VALIDATOR_RET_CHECK(parameter_nodes.empty());
  }

  // Check that is_remote is true iff language is "REMOTE".
  VALIDATOR_RET_CHECK_EQ(googlesql_base::CaseEqual(stmt->language(), "REMOTE"),
                         stmt->is_remote())
      << "is_remote is true iff language is \"REMOTE\"";

  if (stmt->is_remote() &&
      language_options_.LanguageFeatureEnabled(FEATURE_REMOTE_FUNCTION)) {
    // For a remote function, its code should be empty.
    VALIDATOR_RET_CHECK(stmt->code().empty());
  }

  if (stmt->connection() != nullptr) {
    VALIDATOR_RET_CHECK(
        stmt->is_remote() ||
        (language_options_.LanguageFeatureEnabled(
             FEATURE_CREATE_FUNCTION_LANGUAGE_WITH_CONNECTION) &&
         !stmt->language().empty()));
  }

  GOOGLESQL_RETURN_IF_ERROR(ValidateOptionsList(stmt->option_list()));
  return absl::OkStatus();
}

// Check that the function signature does not contain unsupported templated
// argument types.
absl::Status Validator::CheckFunctionArgumentType(
    const FunctionArgumentTypeList& argument_type_list,
    absl::string_view statement_type) {
  for (const FunctionArgumentType& arg_type : argument_type_list) {
    switch (arg_type.kind()) {
      case ARG_TYPE_FIXED:
      case ARG_TYPE_ARBITRARY:
      case ARG_TYPE_RELATION:
        continue;
      default:
        VALIDATOR_RET_CHECK_FAIL()
            << "Unexpected " << statement_type
            << " argument type: " << arg_type.DebugString();
    }
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateProtoFieldPath(
    absl::string_view base_proto_name,
    const std::vector<const google::protobuf::FieldDescriptor*>& proto_field_path) {
  std::string containing_proto_name = std::string(base_proto_name);
  for (const google::protobuf::FieldDescriptor* field : proto_field_path) {
    // Ensure that the field path is valid by checking field containment.
    VALIDATOR_RET_CHECK(!containing_proto_name.empty())
        << "Unable to identify parent message of field: " << field->full_name();
    VALIDATOR_RET_CHECK_EQ(containing_proto_name,
                           field->containing_type()->full_name())
        << "Mismatched proto message " << containing_proto_name << " and field "
        << field->full_name();
    if (field->message_type() != nullptr) {
      containing_proto_name = field->message_type()->full_name();
    } else {
      containing_proto_name.clear();
    }
  }

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedCreateTableFunctionStmt(
    const ResolvedCreateTableFunctionStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);
  VALIDATOR_RET_CHECK_EQ(stmt->argument_name_list().size(),
                         stmt->signature().arguments().size());
  GOOGLESQL_RETURN_IF_ERROR(CheckFunctionArgumentType(stmt->signature().arguments(),
                                            "CREATE TABLE FUNCTION"));
  if (stmt->query() != nullptr) {
    VALIDATOR_RET_CHECK(!stmt->language().empty());

    googlesql_base::VarSetter<ArgumentKindSet> allowed_arg_kinds_setter(
        &allowed_argument_kinds_, {ResolvedArgumentDefEnums::SCALAR});
    googlesql_base::VarSetter<const ResolvedCreateTableFunctionStmt*> stmt_setter(
        &current_create_table_function_stmt_, stmt);

    GOOGLESQL_RETURN_IF_ERROR(
        ValidateResolvedScan(stmt->query(), /*visible_parameters=*/{}));
    VALIDATOR_RET_CHECK(!stmt->output_column_list().empty());

    // Check no query parameter in table function body.
    std::vector<const ResolvedNode*> parameter_nodes;
    stmt->query()->GetDescendantsWithKinds({RESOLVED_PARAMETER},
                                           &parameter_nodes);
    VALIDATOR_RET_CHECK(parameter_nodes.empty());

    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedOutputColumnList(
        stmt->query()->column_list(), stmt->output_column_list(),
        stmt->is_value_table()));
  } else {
    GOOGLESQL_RET_CHECK(stmt->output_column_list().empty());
  }

  GOOGLESQL_RETURN_IF_ERROR(ValidateOptionsList(stmt->option_list()));
  if (stmt->signature().IsTemplated()) {
    VALIDATOR_RET_CHECK(stmt->output_column_list().empty());
    VALIDATOR_RET_CHECK(stmt->query() == nullptr);
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedCreateProcedureStmt(
    const ResolvedCreateProcedureStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);
  GOOGLESQL_RETURN_IF_ERROR(ValidateOptionsList(stmt->option_list()));
  VALIDATOR_RET_CHECK_EQ(stmt->argument_name_list().size(),
                         stmt->signature().arguments().size());
  GOOGLESQL_RETURN_IF_ERROR(CheckFunctionArgumentType(stmt->signature().arguments(),
                                            "CREATE PROCEDURE"));
  if (!language_options_.LanguageFeatureEnabled(
          FEATURE_EXTERNAL_SECURITY_PROCEDURE)) {
    VALIDATOR_RET_CHECK(stmt->external_security() ==
                        ResolvedCreateStatementEnums::SQL_SECURITY_UNSPECIFIED);
  }
  if (language_options_.LanguageFeatureEnabled(FEATURE_NON_SQL_PROCEDURE)) {
    if (!stmt->procedure_body().empty()) {
      VALIDATOR_RET_CHECK(stmt->language().empty());
      VALIDATOR_RET_CHECK(stmt->code().empty());
    } else {
      VALIDATOR_RET_CHECK(!stmt->language().empty());
    }
  } else {
    VALIDATOR_RET_CHECK(stmt->connection() == nullptr);
    VALIDATOR_RET_CHECK(stmt->language().empty());
    VALIDATOR_RET_CHECK(stmt->code().empty());
    VALIDATOR_RET_CHECK(!stmt->procedure_body().empty());
  }

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedCreateEntityStmt(
    const ResolvedCreateEntityStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);
  VALIDATOR_RET_CHECK(!stmt->entity_type().empty());
  VALIDATOR_RET_CHECK(stmt->entity_body_json().empty() ||
                      stmt->entity_body_text().empty())
      << "At most one of JSON or TEXT literals can be non-empty";
  GOOGLESQL_RETURN_IF_ERROR(ValidateOptionsList(stmt->option_list()));
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedAlterEntityStmt(
    const ResolvedAlterEntityStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);
  VALIDATOR_RET_CHECK(!stmt->entity_type().empty());
  for (const auto& action : stmt->alter_action_list()) {
    if (action->node_kind() == RESOLVED_SET_AS_ACTION) {
      auto* set_as_action = action->GetAs<ResolvedSetAsAction>();
      VALIDATOR_RET_CHECK(set_as_action->entity_body_json().empty() !=
                          set_as_action->entity_body_text().empty())
          << "Exactly one of JSON or TEXT literals should be non-empty";
    }
  }

  return absl::OkStatus();
}

absl::Status Validator::ValidateCompatibleSchemaForClone(const Table* source,
                                                         const Table* target) {
  VALIDATOR_RET_CHECK_EQ(source->NumColumns(), target->NumColumns());
  for (int i = 0; i < source->NumColumns(); i++) {
    const Column* from = source->GetColumn(i);
    const Column* to = target->GetColumn(i);
    VALIDATOR_RET_CHECK(from->GetType()->Equals(to->GetType()))
        << "Incompatible column type: " << from->GetType()->DebugString()
        << " vs. " << to->GetType()->DebugString();
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateSingleCloneDataSource(
    const ResolvedScan* source, const Table* target) {
  PushErrorContext push(this, source);
  switch (source->node_kind()) {
    case RESOLVED_TABLE_SCAN:
      if (target != nullptr) {
        GOOGLESQL_RETURN_IF_ERROR(ValidateCompatibleSchemaForClone(
            source->GetAs<ResolvedTableScan>()->table(), target));
      }
      break;
    case RESOLVED_FILTER_SCAN:
      VALIDATOR_RET_CHECK(
          source->GetAs<ResolvedFilterScan>()->input_scan()->node_kind() ==
          RESOLVED_TABLE_SCAN)
          << "Bad scan type: "
          << source->GetAs<ResolvedFilterScan>()->node_kind_string();
      break;
    default:
      return InternalErrorBuilder()
             << "Bad scan type: " << source->node_kind_string();
  }
  return ValidateResolvedScan(source, /*visible_parameters=*/{});
}

absl::Status Validator::ValidateResolvedCloneDataSource(
    const ResolvedScan* source, const Table* target) {
  PushErrorContext push(this, source);
  if (source->node_kind() == RESOLVED_SET_OPERATION_SCAN) {
    const ResolvedSetOperationScan* scan =
        source->GetAs<ResolvedSetOperationScan>();
    VALIDATOR_RET_CHECK_EQ(scan->op_type(),
                           ResolvedSetOperationScan::UNION_ALL);
    for (const auto& input : scan->input_item_list()) {
      GOOGLESQL_RETURN_IF_ERROR(ValidateSingleCloneDataSource(input->scan(), target));
    }
    return absl::OkStatus();
  }
  return ValidateSingleCloneDataSource(source, target);
}

absl::Status Validator::ValidateResolvedCloneDataStmt(
    const ResolvedCloneDataStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedTableScan(stmt->target_table(),
                                            /*visible_parameters=*/{}));
  return ValidateResolvedCloneDataSource(stmt->clone_from(),
                                         stmt->target_table()->table());
}

absl::Status Validator::ValidateResolvedExportDataStmt(
    const ResolvedExportDataStmt* stmt,
    const std::set<ResolvedColumn>& pipe_visible_parameters) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);

  VALIDATOR_RET_CHECK(stmt->query() != nullptr);
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedScan(stmt->query(), pipe_visible_parameters));
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedOutputColumnList(stmt->query()->column_list(),
                                                   stmt->output_column_list(),
                                                   stmt->is_value_table()));
  GOOGLESQL_RETURN_IF_ERROR(ValidateOptionsList(stmt->option_list()));
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedExportModelStmt(
    const ResolvedExportModelStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);
  GOOGLESQL_RETURN_IF_ERROR(ValidateOptionsList(stmt->option_list()));
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedExportMetadataStmt(
    const ResolvedExportMetadataStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);
  VALIDATOR_RET_CHECK(!stmt->name_path().empty());
  VALIDATOR_RET_CHECK(absl::AsciiStrToLower(stmt->schema_object_kind()) ==
                      "table");
  return ValidateOptionsList(stmt->option_list());
}

absl::Status Validator::ValidateResolvedCallStmt(const ResolvedCallStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);

  VALIDATOR_RET_CHECK(stmt->procedure() != nullptr)
      << "ResolvedCallStmt does not have a Procedure:\n"
      << stmt->DebugString();

  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExprList({}, {}, stmt->argument_list()));

  VALIDATOR_RET_CHECK(stmt->signature().IsConcrete())
      << "ResolvedCallStmt must have a concrete signature:\n"
      << stmt->DebugString();
  const int num_args = stmt->signature().NumConcreteArguments();
  VALIDATOR_RET_CHECK_EQ(stmt->argument_list_size(), num_args);
  for (int i = 0; i < num_args; ++i) {
    VALIDATOR_RET_CHECK(!stmt->signature().ConcreteArgument(i).IsVoid())
        << "Procedures invoked by CALL cannot have VOID arguments, but found "
           "at position "
        << i << ", signature: " << stmt->signature().DebugString();
    VALIDATOR_RET_CHECK(stmt->argument_list(i)->type()->Equals(
        stmt->signature().ConcreteArgumentType(i)));
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedDefineTableStmt(
    const ResolvedDefineTableStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);
  GOOGLESQL_RETURN_IF_ERROR(ValidateOptionsList(stmt->option_list()));
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedDescribeStmt(
    const ResolvedDescribeStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedShowStmt(const ResolvedShowStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedBeginStmt(
    const ResolvedBeginStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedSetTransactionStmt(
    const ResolvedSetTransactionStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedCommitStmt(
    const ResolvedCommitStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedRollbackStmt(
    const ResolvedRollbackStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedStartBatchStmt(
    const ResolvedStartBatchStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedRunBatchStmt(
    const ResolvedRunBatchStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedAbortBatchStmt(
    const ResolvedAbortBatchStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedUndropStmt(
    const ResolvedUndropStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedDropStmt(const ResolvedDropStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedDropFunctionStmt(
    const ResolvedDropFunctionStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);
  VALIDATOR_RET_CHECK_EQ(stmt->signature() == nullptr,
                         stmt->arguments() == nullptr);
  if (stmt->signature() != nullptr) {
    const bool has_relation_args =
        std::any_of(stmt->signature()->signature().arguments().begin(),
                    stmt->signature()->signature().arguments().end(),
                    [](const FunctionArgumentType& arg_type) {
                      return arg_type.IsRelation();
                    });
    if (has_relation_args) {
      VALIDATOR_RET_CHECK_EQ(0, stmt->arguments()->arg_list_size());
    } else {
      VALIDATOR_RET_CHECK_EQ(stmt->signature()->signature().arguments().size(),
                             stmt->arguments()->arg_list_size());
    }
    VALIDATOR_RET_CHECK(stmt->signature()->signature().result_type().IsVoid());
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedDropTableFunctionStmt(
    const ResolvedDropTableFunctionStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedDropSnapshotTableStmt(
    const ResolvedDropSnapshotTableStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedDropMaterializedViewStmt(
    const ResolvedDropMaterializedViewStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedDropPrivilegeRestrictionStmt(
    const ResolvedDropPrivilegeRestrictionStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  VALIDATOR_RET_CHECK(!stmt->column_privilege_list().empty());
  VALIDATOR_RET_CHECK(absl::AsciiStrToLower(stmt->object_type()) == "table" ||
                      absl::AsciiStrToLower(stmt->object_type()) == "view");
  VALIDATOR_RET_CHECK(!stmt->name_path().empty());
  PushErrorContext push(this, stmt);
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedDropRowAccessPolicyStmt(
    const ResolvedDropRowAccessPolicyStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);
  VALIDATOR_RET_CHECK(!(stmt->is_drop_all() && stmt->is_if_exists()));
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedDropIndexStmt(
    const ResolvedDropIndexStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  return absl::OkStatus();
}
absl::Status Validator::ValidateResolvedGrantStmt(
    const ResolvedGrantStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedRevokeStmt(
    const ResolvedRevokeStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedOrderByScan(
    const ResolvedOrderByScan* scan,
    const std::set<ResolvedColumn>& visible_parameters) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, scan);

  VALIDATOR_RET_CHECK(nullptr != scan->input_scan());
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedScan(scan->input_scan(), visible_parameters));

  std::set<ResolvedColumn> visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(
      AddColumnList(scan->input_scan()->column_list(), &visible_columns));
  for (const auto& order_by_item : scan->order_by_item_list()) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedOrderByItem(
        visible_columns, visible_parameters, order_by_item.get()));
  }

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedOrderByItem(
    const std::set<ResolvedColumn>& visible_columns,
    const std::set<ResolvedColumn>& visible_parameters,
    const ResolvedOrderByItem* item) {
  PushErrorContext push(this, item);
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(visible_columns, visible_parameters,
                                       item->column_ref()));
  if (item->collation_name() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(visible_columns, visible_parameters,
                                         item->collation_name()));
    VALIDATOR_RET_CHECK(item->collation_name()->type()->IsString())
        << "collation_name must have type STRING";
    if (language_options_.LanguageFeatureEnabled(FEATURE_COLLATION_SUPPORT)) {
      if (item->collation_name()->Is<ResolvedLiteral>()) {
        VALIDATOR_RET_CHECK(!item->collation().Empty());
        VALIDATOR_RET_CHECK_EQ(item->collation_name()
                                   ->GetAs<ResolvedLiteral>()
                                   ->value()
                                   .string_value(),
                               item->collation().CollationName());
      } else {
        VALIDATOR_RET_CHECK(item->collation_name()->Is<ResolvedParameter>());
        VALIDATOR_RET_CHECK(item->collation().Empty());
      }
    }
  }
  item->is_descending();  // Mark field as visited.
  item->null_order();     // Mark field as visited.
  if (!item->collation().Empty()) {
    VALIDATOR_RET_CHECK(
        item->collation().HasCompatibleStructure(item->column_ref()->type()))
        << "collation must have compatible structure with the type of "
           "column_ref";
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedQueryStmt(
    const ResolvedQueryStmt* query) {
  VALIDATOR_RET_CHECK(nullptr != query);
  PushErrorContext push(this, query);
  GOOGLESQL_RETURN_IF_ERROR(
      ValidateResolvedScan(query->query(), /*visible_parameters=*/{}));
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedOutputColumnList(
      query->query()->column_list(), query->output_column_list(),
      query->is_value_table()));
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedGeneralizedQueryStmt(
    const ResolvedGeneralizedQueryStmt* query) {
  VALIDATOR_RET_CHECK(nullptr != query);
  PushErrorContext push(this, query);
  GOOGLESQL_RETURN_IF_ERROR(
      ValidateResolvedScan(query->query(), /*visible_parameters=*/{}));
  if (query->output_schema() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedOutputSchema(query->query()->column_list(),
                                                 query->output_schema()));
  } else {
    VALIDATOR_RET_CHECK(query->query()->column_list().empty());
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedGeneralizedQuerySubpipeline(
    const ResolvedGeneralizedQuerySubpipeline* subpipeline,
    const ResolvedScan* input_scan,
    const std::set<ResolvedColumn>& visible_parameters) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, subpipeline);

  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedSubpipeline(
      subpipeline->subpipeline(), input_scan->column_list(),
      input_scan->is_ordered(), visible_parameters));

  if (subpipeline->output_schema() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedOutputSchema(
        subpipeline->subpipeline()->scan()->column_list(),
        subpipeline->output_schema()));
  } else {
    VALIDATOR_RET_CHECK(
        subpipeline->subpipeline()->scan()->column_list().empty());
  }

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedMultiStmt(
    const ResolvedMultiStmt* multi) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, multi);

  // This can be relaxed if there's a reason to support size 1.
  VALIDATOR_RET_CHECK_GE(multi->statement_list().size(), 2);

  // We shouldn't have any WITH definitions at the start of a statement.
  // We'll create some for the sub-statements that are visible util the end
  // of this ResolvedMultiStmt, when we'll remove them.
  VALIDATOR_RET_CHECK(visible_with_entries_.empty());

  for (const auto& stmt : multi->statement_list()) {
    VALIDATOR_RET_CHECK_NE(stmt->node_kind(), RESOLVED_MULTI_STMT)
        << "ResolvedMultiStmt contains nested ResolvedMultiStmt";

    // With in_multi_stmt, this does a partial Reset(), so it validates the
    // statements as self-contained, except for the specific state that's
    // intentionally carried over, like visible_with_entries_.
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedStatement(stmt.get(),
                                              /*in_multi_stmt=*/true));
  }

  visible_with_entries_.clear();
  return absl::OkStatus();
}

absl::Status Validator::ValidateCreateWithEntryStmt(
    const ResolvedCreateWithEntryStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);

  // CreateWithEntryStmt can only occur in a ResolvedMultiStmt.
  // Check that by looking for ResolvedMultiStmt as the parent node in
  // the context stack.  It is the third node up because this node is pushed
  // twice, in this method and in ValidateResolvedStatementInternal.
  VALIDATOR_RET_CHECK_GE(context_stack_.size(), 3);
  const ResolvedNode* parent_node = context_stack_[context_stack_.size() - 3];
  VALIDATOR_RET_CHECK_EQ(parent_node->node_kind(), RESOLVED_MULTI_STMT)
      << "ResolvedCreateWithEntryStmt can only occur inside a "
         "ResolvedMultiStmt";

  const ResolvedWithEntry* with_entry = stmt->with_entry();
  VALIDATOR_RET_CHECK(nullptr != with_entry);

  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedScan(with_entry->with_subquery(),
                                       /*visible_parameters=*/{}));
  with_entry->with_query_name();  // Mark visited.

  visible_with_entries_.push_back(with_entry);
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedScan(
    const ResolvedScan* scan,
    const std::set<ResolvedColumn>& visible_parameters) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  VALIDATOR_RET_CHECK(nullptr != scan);
  PushErrorContext push(this, scan);

  GOOGLESQL_RETURN_IF_ERROR(
      googlesql::ValidateScanCanEmitMeasureColumns(scan, language_options_));

  // We assign the status in each switch branch and call GOOGLESQL_RETURN_IF_ERROR only
  // once. This is because GOOGLESQL_RETURN_IF_ERROR introduces temporary variables on
  // each call, which are not eliminated between the switch branches. This
  // substantially increases this stack size of this function, causing stack
  // exhaustion issues in some compilation modes.
  absl::Status scan_subtype_status;
  switch (scan->node_kind()) {
    case RESOLVED_SINGLE_ROW_SCAN:
      // Single row scan has no fields to validate.
      break;
    case RESOLVED_WITH_REF_SCAN:
      scan_subtype_status =
          ValidateResolvedWithRefScan(scan->GetAs<ResolvedWithRefScan>());
      break;
    case RESOLVED_TABLE_SCAN:
      scan_subtype_status = ValidateResolvedTableScan(
          scan->GetAs<ResolvedTableScan>(), visible_parameters);
      break;
    case RESOLVED_JOIN_SCAN:
      scan_subtype_status = ValidateResolvedJoinScan(
          scan->GetAs<ResolvedJoinScan>(), visible_parameters);
      break;
    case RESOLVED_ARRAY_SCAN:
      scan_subtype_status = ValidateResolvedArrayScan(
          scan->GetAs<ResolvedArrayScan>(), visible_parameters);
      break;
    case RESOLVED_FILTER_SCAN:
      scan_subtype_status = ValidateResolvedFilterScan(
          scan->GetAs<ResolvedFilterScan>(), visible_parameters);
      break;
    case RESOLVED_AGGREGATE_SCAN:
      scan_subtype_status = ValidateResolvedAggregateScan(
          scan->GetAs<ResolvedAggregateScan>(), visible_parameters);
      break;
    case RESOLVED_ANONYMIZED_AGGREGATE_SCAN:
      scan_subtype_status = ValidateResolvedAnonymizedAggregateScan(
          scan->GetAs<ResolvedAnonymizedAggregateScan>(), visible_parameters);
      break;
    case RESOLVED_DIFFERENTIAL_PRIVACY_AGGREGATE_SCAN:
      scan_subtype_status = ValidateResolvedDifferentialPrivacyAggregateScan(
          scan->GetAs<ResolvedDifferentialPrivacyAggregateScan>(),
          visible_parameters);
      break;
    case RESOLVED_AGGREGATION_THRESHOLD_AGGREGATE_SCAN:
      scan_subtype_status = ValidateResolvedAggregationThresholdAggregateScan(
          scan->GetAs<ResolvedAggregationThresholdAggregateScan>(),
          visible_parameters);
      break;
    case RESOLVED_SET_OPERATION_SCAN:
      scan_subtype_status = ValidateResolvedSetOperationScan(
          scan->GetAs<ResolvedSetOperationScan>(), visible_parameters);
      break;
    case RESOLVED_PROJECT_SCAN:
      scan_subtype_status = ValidateResolvedProjectScan(
          scan->GetAs<ResolvedProjectScan>(), visible_parameters);
      break;
    case RESOLVED_ORDER_BY_SCAN:
      scan_subtype_status = ValidateResolvedOrderByScan(
          scan->GetAs<ResolvedOrderByScan>(), visible_parameters);
      break;
    case RESOLVED_LIMIT_OFFSET_SCAN:
      scan_subtype_status = ValidateResolvedLimitOffsetScan(
          scan->GetAs<ResolvedLimitOffsetScan>(), visible_parameters);
      break;
    case RESOLVED_WITH_SCAN:
      scan_subtype_status = ValidateResolvedWithScan(
          scan->GetAs<ResolvedWithScan>(), visible_parameters);
      break;
    case RESOLVED_ANALYTIC_SCAN:
      scan_subtype_status = ValidateResolvedAnalyticScan(
          scan->GetAs<ResolvedAnalyticScan>(), visible_parameters);
      break;
    case RESOLVED_SAMPLE_SCAN:
      scan_subtype_status = ValidateResolvedSampleScan(
          scan->GetAs<ResolvedSampleScan>(), visible_parameters);
      break;
    case RESOLVED_TVFSCAN:
      scan_subtype_status = ValidateResolvedTVFScan(
          scan->GetAs<ResolvedTVFScan>(), visible_parameters);
      break;
    case RESOLVED_EXECUTE_AS_ROLE_SCAN:
      scan_subtype_status = ValidateResolvedExecuteAsRoleScan(
          scan->GetAs<ResolvedExecuteAsRoleScan>(), visible_parameters);
      break;
    case RESOLVED_RELATION_ARGUMENT_SCAN:
      scan_subtype_status = ValidateResolvedRelationArgumentScan(
          scan->GetAs<ResolvedRelationArgumentScan>(), visible_parameters);
      break;
    case RESOLVED_RECURSIVE_SCAN:
      scan_subtype_status = ValidateResolvedRecursiveScan(
          scan->GetAs<ResolvedRecursiveScan>(), visible_parameters);
      break;
    case RESOLVED_RECURSIVE_REF_SCAN:
      scan_subtype_status = ValidateResolvedRecursiveRefScan(
          scan->GetAs<ResolvedRecursiveRefScan>());
      break;
    case RESOLVED_MATCH_RECOGNIZE_SCAN:
      scan_subtype_status = ValidateResolvedMatchRecognizeScan(
          scan->GetAs<ResolvedMatchRecognizeScan>(), visible_parameters);
      break;
    case RESOLVED_PIVOT_SCAN:
      scan_subtype_status = ValidateResolvedPivotScan(
          scan->GetAs<ResolvedPivotScan>(), visible_parameters);
      break;
    case RESOLVED_UNPIVOT_SCAN:
      scan_subtype_status = ValidateResolvedUnpivotScan(
          scan->GetAs<ResolvedUnpivotScan>(), visible_parameters);
      break;
    case RESOLVED_GROUP_ROWS_SCAN:
      scan_subtype_status =
          ValidateGroupRowsScan(scan->GetAs<ResolvedGroupRowsScan>());
      break;
    case RESOLVED_GRAPH_TABLE_SCAN:
      scan_subtype_status = ValidateResolvedGraphTableScan(
          scan->GetAs<ResolvedGraphTableScan>(), visible_parameters);
      break;
    case RESOLVED_GRAPH_NODE_SCAN:
      scan_subtype_status = ValidateResolvedGraphNodeScan(
          scan->GetAs<ResolvedGraphNodeScan>(), visible_parameters);
      break;
    case RESOLVED_GRAPH_EDGE_SCAN:
      scan_subtype_status = ValidateResolvedGraphEdgeScan(
          scan->GetAs<ResolvedGraphEdgeScan>(), visible_parameters);
      break;
    case RESOLVED_GRAPH_PATH_SCAN:
      scan_subtype_status = ValidateResolvedGraphPathScan(
          scan->GetAs<ResolvedGraphPathScan>(), visible_parameters);
      break;
    case RESOLVED_GRAPH_SCAN:
      scan_subtype_status = ValidateResolvedGraphScan(
          scan->GetAs<ResolvedGraphScan>(), visible_parameters);
      break;
    case RESOLVED_GRAPH_LINEAR_SCAN:
      scan_subtype_status = ValidateResolvedGraphLinearScan(
          scan->GetAs<ResolvedGraphLinearScan>(), visible_parameters);
      break;
    case RESOLVED_GRAPH_REF_SCAN:
      scan_subtype_status = ValidateResolvedGraphRefScan(
          scan->GetAs<ResolvedGraphRefScan>(), visible_parameters);
      break;
    case RESOLVED_GRAPH_CALL_SCAN:
      scan_subtype_status = ValidateResolvedGraphCallScan(
          scan->GetAs<ResolvedGraphCallScan>(), visible_parameters);
      break;
    case RESOLVED_DESCRIBE_SCAN:
      scan_subtype_status = ValidateResolvedDescribeScan(
          scan->GetAs<ResolvedDescribeScan>(), visible_parameters);
      break;
    case RESOLVED_STATIC_DESCRIBE_SCAN:
      scan_subtype_status = ValidateResolvedStaticDescribeScan(
          scan->GetAs<ResolvedStaticDescribeScan>(), visible_parameters);
      break;
    case RESOLVED_ASSERT_SCAN:
      scan_subtype_status = ValidateResolvedAssertScan(
          scan->GetAs<ResolvedAssertScan>(), visible_parameters);
      break;
    case RESOLVED_LOG_SCAN:
      scan_subtype_status = ValidateResolvedLogScan(
          scan->GetAs<ResolvedLogScan>(), visible_parameters);
      break;
    case RESOLVED_PIPE_IF_SCAN:
      scan_subtype_status = ValidateResolvedPipeIfScan(
          scan->GetAs<ResolvedPipeIfScan>(), visible_parameters);
      break;
    case RESOLVED_PIPE_FORK_SCAN:
      scan_subtype_status = ValidateResolvedPipeForkScan(
          scan->GetAs<ResolvedPipeForkScan>(), visible_parameters);
      break;
    case RESOLVED_PIPE_TEE_SCAN:
      scan_subtype_status = ValidateResolvedPipeTeeScan(
          scan->GetAs<ResolvedPipeTeeScan>(), visible_parameters);
      break;
    case RESOLVED_PIPE_EXPORT_DATA_SCAN:
      scan_subtype_status = ValidateResolvedPipeExportDataScan(
          scan->GetAs<ResolvedPipeExportDataScan>(), visible_parameters);
      break;
    case RESOLVED_PIPE_CREATE_TABLE_SCAN:
      scan_subtype_status = ValidateResolvedPipeCreateTableScan(
          scan->GetAs<ResolvedPipeCreateTableScan>(), visible_parameters);
      break;
    case RESOLVED_PIPE_INSERT_SCAN:
      scan_subtype_status = ValidateResolvedPipeInsertScan(
          scan->GetAs<ResolvedPipeInsertScan>(), visible_parameters);
      break;
    case RESOLVED_SUBPIPELINE_INPUT_SCAN:
      scan_subtype_status = ValidateResolvedSubpipelineInputScan(
          scan->GetAs<ResolvedSubpipelineInputScan>(), visible_parameters);
      break;
    case RESOLVED_BARRIER_SCAN:
      scan_subtype_status = ValidateResolvedBarrierScan(
          scan->GetAs<ResolvedBarrierScan>(), visible_parameters);
      break;
    case RESOLVED_UNSET_ARGUMENT_SCAN:
      return absl::InternalError(
          "UnsetArgumentScan is only allowed as an argument to a TVFScan");
    default:
      return InternalErrorBuilder()
             << "Unhandled node kind: " << scan->node_kind_string()
             << " in ValidateResolvedScan";
  }
  GOOGLESQL_RETURN_IF_ERROR(scan_subtype_status);

  if (scan->is_ordered()) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedScanOrdering(scan));
  }

  GOOGLESQL_RETURN_IF_ERROR(ValidateHintList(scan->hint_list()));

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedRecursiveScan(
    const ResolvedRecursiveScan* scan,
    const std::set<ResolvedColumn>& visible_parameters) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, scan);
  VALIDATOR_RET_CHECK(
      language_options_.LanguageFeatureEnabled(FEATURE_WITH_RECURSIVE) ||
      language_options_.LanguageFeatureEnabled(
          FEATURE_SQL_GRAPH_BOUNDED_PATH_QUANTIFICATION) ||
      language_options_.LanguageFeatureEnabled(FEATURE_PIPE_RECURSIVE_UNION))
      << "Found recursive scan, but WITH RECURSIVE and pipe RECURSIVE UNION "
         "are both disabled in language features";
  VALIDATOR_RET_CHECK(scan->non_recursive_term() != nullptr);
  VALIDATOR_RET_CHECK(scan->recursive_term() != nullptr);

  ResolvedColumn depth_column;
  if (scan->recursion_depth_modifier() != nullptr) {
    VALIDATOR_RET_CHECK_OK(ValidateResolvedRecursionDepthModifier(
        scan->recursion_depth_modifier(), scan->column_list()));
    depth_column =
        scan->recursion_depth_modifier()->recursion_depth_column()->column();
  }

  ResolvedColumnList columns_excluding_depth;
  for (const auto& col : scan->column_list()) {
    if (col != depth_column) {
      columns_excluding_depth.push_back(col);
    }
  }

  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedSetOperationItem(
      scan->non_recursive_term(), columns_excluding_depth, visible_parameters));
  nested_recursive_scans_.push_back(RecursiveScanInfo(scan));
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedSetOperationItem(
      scan->recursive_term(), columns_excluding_depth, visible_parameters));
  VALIDATOR_RET_CHECK_EQ(nested_recursive_scans_.back().scan, scan);
  VALIDATOR_RET_CHECK(nested_recursive_scans_.back().saw_recursive_ref)
      << "Recursive scan generated without a recursive reference in the "
         "recursive term:\n"
      << scan->DebugString();
  nested_recursive_scans_.pop_back();
  scan->op_type();  // Mark field as visited.

  for (const ResolvedColumn& column : scan->column_list()) {
    GOOGLESQL_RETURN_IF_ERROR(CheckUniqueColumnId(column));
  }
  return absl::Status();
}

absl::Status Validator::ValidateResolvedRecursionDepthModifier(
    const ResolvedRecursionDepthModifier* modifier,
    const ResolvedColumnList& recursion_column_list) {
  if (modifier->lower_bound() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateArgumentIsInt64(
        modifier->lower_bound(), /*validate_constant_nonnegative=*/true,
        /*context_msg=*/"Recursion lower bound"));
  }
  if (modifier->upper_bound() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateArgumentIsInt64(
        modifier->upper_bound(), /*validate_constant_nonnegative=*/true,
        /*context_msg=*/"Recursion upper bound"));
  }
  if (modifier->lower_bound() != nullptr &&
      modifier->upper_bound() != nullptr &&
      modifier->lower_bound()->Is<ResolvedLiteral>() &&
      modifier->upper_bound()->Is<ResolvedLiteral>()) {
    const auto& lower_value =
        modifier->lower_bound()->GetAs<ResolvedLiteral>()->value();
    VALIDATOR_RET_CHECK(lower_value.is_valid());
    VALIDATOR_RET_CHECK(!lower_value.is_null());
    VALIDATOR_RET_CHECK(lower_value.type()->IsInt64());

    const auto& upper_value =
        modifier->upper_bound()->GetAs<ResolvedLiteral>()->value();
    VALIDATOR_RET_CHECK(upper_value.is_valid());
    VALIDATOR_RET_CHECK(!upper_value.is_null());
    VALIDATOR_RET_CHECK(upper_value.type()->IsInt64());

    VALIDATOR_RET_CHECK_LE(lower_value.int64_value(),
                           upper_value.int64_value());
  }
  VALIDATOR_RET_CHECK(modifier->recursion_depth_column() != nullptr);
  VALIDATOR_RET_CHECK(absl::c_linear_search(
      recursion_column_list, modifier->recursion_depth_column()->column()))
      << "Recursion depth column is not in the output column list";
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedRecursiveRefScan(
    const ResolvedRecursiveRefScan* scan) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, scan);
  VALIDATOR_RET_CHECK(!nested_recursive_scans_.empty())
      << "ResolvedRecursiveRefScan() detected outside a recursive UNION term";
  VALIDATOR_RET_CHECK(language_options_.LanguageFeatureEnabled(
                          FEATURE_RELAXED_WITH_RECURSIVE) ||
                      !nested_recursive_scans_.back().saw_recursive_ref)
      << "Recursive scan contains multiple recursive references in its "
         "recursive term:\n"
      << nested_recursive_scans_.back().scan->DebugString();
  nested_recursive_scans_.back().saw_recursive_ref = true;

  for (const ResolvedColumn& column : scan->column_list()) {
    GOOGLESQL_RETURN_IF_ERROR(CheckUniqueColumnId(column));
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedScanOrdering(const ResolvedScan* scan) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  VALIDATOR_RET_CHECK(nullptr != scan);
  PushErrorContext push(this, scan);

  const ResolvedScan* input_scan = nullptr;

  switch (scan->node_kind()) {
    // OrderByScan can always produce an ordered result.
    case RESOLVED_ORDER_BY_SCAN:
      return absl::OkStatus();

    // ResolvedSubpipelineInputScan gets its ordering from the subpipeline's
    // input, which is recorded on the subpipeline stack.
    case RESOLVED_SUBPIPELINE_INPUT_SCAN:
      VALIDATOR_RET_CHECK_GE(subpipeline_info_stack_.size(), 1);
      VALIDATOR_RET_CHECK(subpipeline_info_stack_.back().is_ordered);
      return absl::OkStatus();

    // These scans can produce an ordered result if their input was ordered.
    // These cases fill in <input_scan>, which is checked below the switch.
    case RESOLVED_PROJECT_SCAN:
      input_scan = scan->GetAs<ResolvedProjectScan>()->input_scan();
      break;
    case RESOLVED_LIMIT_OFFSET_SCAN:
      input_scan = scan->GetAs<ResolvedLimitOffsetScan>()->input_scan();
      break;
    case RESOLVED_WITH_SCAN:
      input_scan = scan->GetAs<ResolvedWithScan>()->query();
      break;
    case RESOLVED_EXECUTE_AS_ROLE_SCAN:
      input_scan = scan->GetAs<ResolvedExecuteAsRoleScan>()->input_scan();
      break;
    case RESOLVED_STATIC_DESCRIBE_SCAN:
      input_scan = scan->GetAs<ResolvedStaticDescribeScan>()->input_scan();
      break;
    case RESOLVED_LOG_SCAN:
      input_scan = scan->GetAs<ResolvedLogScan>()->input_scan();
      break;
    case RESOLVED_PIPE_IF_SCAN:
      input_scan = scan->GetAs<ResolvedPipeIfScan>()->GetSelectedCaseScan();
      break;
    case RESOLVED_BARRIER_SCAN:
      input_scan = scan->GetAs<ResolvedBarrierScan>()->input_scan();
      break;

    // For all other scan types, is_ordered is not allowed.
    default:
      return InternalErrorBuilder() << "Node kind: " << scan->node_kind_string()
                                    << " cannot have is_ordered=true:\n"
                                    << scan->DebugString();
  }

  VALIDATOR_RET_CHECK(input_scan != nullptr);
  if (!input_scan->is_ordered()) {
    return InternalErrorBuilder()
           << "Node has is_ordered=true but its input does not:\n"
           << scan->DebugString();
  }

  return absl::OkStatus();
}

absl::Status Validator::ValidateGroupRowsScan(
    const ResolvedGroupRowsScan* scan) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  VALIDATOR_RET_CHECK_NE(nullptr, scan);
  PushErrorContext push(this, scan);

  // A group rows scan can only appear in a group rows subquery associated with
  // an aggregate function and scan, so the input columns must be set.
  VALIDATOR_RET_CHECK(input_columns_for_group_rows_.has_value());
  VALIDATOR_RET_CHECK(!input_columns_for_group_rows_.value().empty());

  // Validate that columns defined in GROUP_ROWS() are all column references
  // pointing to some columns that are actually available from the input of
  // aggregation scan where WITH GROUP ROWS was used.
  std::set<ResolvedColumn> refs;
  absl::flat_hash_set<ResolvedColumn> resolved_columns;
  for (const auto& column : scan->input_column_list()) {
    GOOGLESQL_RETURN_IF_ERROR(CheckUniqueColumnId(column->column()));
    resolved_columns.insert(column->column());
    VALIDATOR_RET_CHECK(column->expr()->Is<ResolvedColumnRef>());
    const ResolvedColumnRef* ref = column->expr()->GetAs<ResolvedColumnRef>();
    VALIDATOR_RET_CHECK(
        googlesql_base::ContainsKey(input_columns_for_group_rows_.value(), ref->column()));
  }

  // Validate that all columns produced by GROUP_ROWS() scan are in fact defined
  // in it.
  for (const ResolvedColumn& column : scan->column_list()) {
    VALIDATOR_RET_CHECK(resolved_columns.contains(column));
  }

  return absl::OkStatus();
}

absl::Status Validator::ValidateHintList(
    absl::Span<const std::unique_ptr<const ResolvedOption>> hint_list) {
  for (const std::unique_ptr<const ResolvedOption>& hint : hint_list) {
    // The value in a Hint must be a constant so we don't pass any visible
    // column names.
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr({}, {}, hint->value()));
    hint->name();       // Mark field as visited without checking it.
    hint->qualifier();  // Mark field as visited without checking it.
  }

  return absl::OkStatus();
}

absl::Status Validator::ValidateOptionsList(
    absl::Span<const std::unique_ptr<const ResolvedOption>> hint_list) {
  for (const std::unique_ptr<const ResolvedOption>& hint : hint_list) {
    // The value in a Hint must be a constant so we don't pass any visible
    // column names.
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr({}, {}, hint->value()));
    hint->name();  // Mark field as visited without checking it.
    VALIDATOR_RET_CHECK(hint->qualifier().empty())
        << "Qualifiers should not exist in options (only hints)\n";
  }

  return absl::OkStatus();
}

template <class MapType>
absl::Status Validator::ValidateOptionsList(
    absl::Span<const std::unique_ptr<const ResolvedOption>> list,
    const MapType& allowed_options,
    const std::set<ResolvedColumn>& visible_columns,
    const std::set<ResolvedColumn>& visible_parameters,
    absl::string_view option_type) {
  const std::string upper_case_option_type = [option_type]() {
    std::string result(option_type.data(), option_type.size());
    if (!result.empty()) {
      result[0] = absl::ascii_toupper(option_type[0]);
    }
    return result;
  }();
  for (const auto& option : list) {
    VALIDATOR_RET_CHECK(!option_type.empty());
    VALIDATOR_RET_CHECK(option->qualifier().empty())
        << upper_case_option_type
        << " options must not have a qualifier, but found "
        << option->qualifier();
    auto iter = allowed_options.find(absl::AsciiStrToLower(option->name()));
    if (iter == allowed_options.end()) {
      VALIDATOR_RET_CHECK_FAIL()
          << "Invalid " << option_type << " option name: " << option->name();
    }
    const Type* expected_option_type = iter->second.type;
    if (expected_option_type != nullptr) {
      VALIDATOR_RET_CHECK(
          option->value()->type()->Equals(expected_option_type));
    }
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(visible_columns, visible_parameters,
                                         option->value()));
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedTableAndColumnInfo(
    const ResolvedTableAndColumnInfo* table_and_column_info) {
  PushErrorContext push(this, table_and_column_info);
  if (!table_and_column_info->column_index_list().empty()) {
    // Verifying that the column index is in the valid range for the table, and
    // that there are no duplicates.
    std::set<int> column_indexes;
    for (const int column_index : table_and_column_info->column_index_list()) {
      VALIDATOR_RET_CHECK_LT(column_index,
                             table_and_column_info->table()->NumColumns());
      VALIDATOR_RET_CHECK_GE(column_index, 0);
      VALIDATOR_RET_CHECK(column_indexes.insert(column_index).second);
    }
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedTableAndColumnInfoList(
    absl::Span<const std::unique_ptr<const ResolvedTableAndColumnInfo>>
        table_and_column_info_list) {
  std::set<std::string, googlesql_base::CaseLess> table_names;
  for (const std::unique_ptr<const ResolvedTableAndColumnInfo>&
           table_and_column_info : table_and_column_info_list) {
    const std::string table_name = table_and_column_info->table()->FullName();
    VALIDATOR_RET_CHECK(table_names.insert(table_name).second) << table_name;
    GOOGLESQL_RETURN_IF_ERROR(
        ValidateResolvedTableAndColumnInfo(table_and_column_info.get()));
  }

  return absl::OkStatus();
}

absl::Status Validator::ValidateCollateExpr(
    const ResolvedExpr* resolved_collate) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, resolved_collate);
  VALIDATOR_RET_CHECK(resolved_collate != nullptr);
  VALIDATOR_RET_CHECK(resolved_collate->node_kind() == RESOLVED_LITERAL)
      << "COLLATE must be followed by a string literal";
  VALIDATOR_RET_CHECK(resolved_collate->type()->IsString())
      << "COLLATE must be applied to type STRING";
  return absl::OkStatus();
}

absl::Status Validator::ValidateColumnAnnotations(
    const ResolvedColumnAnnotations* annotations) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, annotations);
  VALIDATOR_RET_CHECK(annotations != nullptr);
  if (annotations->collation_name() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateCollateExpr(annotations->collation_name()));
  }
  for (const std::unique_ptr<const ResolvedColumnAnnotations>& child :
       annotations->child_list()) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateColumnAnnotations(child.get()));
  }
  return ValidateOptionsList(annotations->option_list());
}

absl::Status Validator::ValidateUpdatedAnnotations(
    const ResolvedColumnAnnotations* annotations) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, annotations);
  VALIDATOR_RET_CHECK(annotations != nullptr);
  VALIDATOR_RET_CHECK_EQ(annotations->option_list_size(), 0);
  VALIDATOR_RET_CHECK(!annotations->not_null());
  if (annotations->collation_name() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateCollateExpr(annotations->collation_name()));
  }
  for (const std::unique_ptr<const ResolvedColumnAnnotations>& child :
       annotations->child_list()) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateColumnAnnotations(child.get()));
  }
  return ValidateOptionsList(annotations->option_list());
}

template <class STMT>
absl::Status Validator::ValidateResolvedDMLStmt(
    const STMT* stmt, const ResolvedColumn* array_element_column,
    std::set<ResolvedColumn>* visible_columns) {
  PushErrorContext push(this, stmt);
  visible_columns->clear();
  GOOGLESQL_RETURN_IF_ERROR(ValidateHintList(stmt->hint_list()));

  if (array_element_column == nullptr) {
    // Non-nested DML.
    VALIDATOR_RET_CHECK(stmt->table_scan() != nullptr);
    GOOGLESQL_RETURN_IF_ERROR(
        ValidateResolvedScan(stmt->table_scan(), /*visible_parameters=*/{}));

    GOOGLESQL_RETURN_IF_ERROR(
        AddColumnList(stmt->table_scan()->column_list(), visible_columns));
  } else {
    // Nested DML.
    VALIDATOR_RET_CHECK(stmt->table_scan() == nullptr);
    // The array element is not visible in nested INSERTs.
    if (!std::is_same<STMT, ResolvedInsertStmt>::value) {
      visible_columns->insert(*array_element_column);
    }
  }

  if (stmt->assert_rows_modified() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(
        ValidateArgumentIsInt64(stmt->assert_rows_modified()->rows(),
                                /*validate_constant_nonnegative=*/true,
                                /*context_msg=*/"Assert rows modified"));
  }

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedInsertStmt(
    const ResolvedInsertStmt* stmt,
    const std::set<ResolvedColumn>* outer_visible_columns,
    const ResolvedColumn* array_element_column,
    const std::set<ResolvedColumn>& pipe_visible_parameters) {
  PushErrorContext push(this, stmt);
  std::set<ResolvedColumn> visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(
      ValidateResolvedDMLStmt(stmt, array_element_column, &visible_columns));

  std::vector<ResolvedColumn> inserted_columns;

  VALIDATOR_RET_CHECK_EQ(array_element_column == nullptr,
                         outer_visible_columns == nullptr);
  VALIDATOR_RET_CHECK_EQ(
      stmt->topologically_sorted_generated_column_id_list_size(),
      stmt->generated_column_expr_list_size());
  if (array_element_column == nullptr) {
    // Non-nested INSERTs.
    VALIDATOR_RET_CHECK_GT(stmt->insert_column_list_size(), 0);
    for (const ResolvedColumn& column : stmt->insert_column_list()) {
      GOOGLESQL_RETURN_IF_ERROR(CheckColumnIsPresentInColumnSet(column, visible_columns));
    }
    inserted_columns = stmt->insert_column_list();

    VALIDATOR_RET_CHECK_EQ(stmt->table_scan()->column_index_list().size(),
                           stmt->column_access_list().size());
  } else {
    // Nested INSERTs.
    VALIDATOR_RET_CHECK(stmt->table_scan() == nullptr);
    VALIDATOR_RET_CHECK_EQ(stmt->insert_column_list_size(), 0)
        << "insert_column_list not supported on nested INSERTs";
    VALIDATOR_RET_CHECK_EQ(stmt->insert_mode(), ResolvedInsertStmt::OR_ERROR)
        << "insert_mode not supported on nested INSERTs";

    VALIDATOR_RET_CHECK(visible_columns.empty());
    inserted_columns.push_back(*array_element_column);

    visible_columns.insert(outer_visible_columns->begin(),
                           outer_visible_columns->end());

    VALIDATOR_RET_CHECK_EQ(stmt->column_access_list().size(), 0);
  }

  VALIDATOR_RET_CHECK_EQ(stmt->query() != nullptr,
                         stmt->query_output_column_list_size() > 0);

  if (!pipe_visible_parameters.empty()) {
    GOOGLESQL_RET_CHECK(stmt->query() != nullptr);
    GOOGLESQL_RET_CHECK(stmt->returning() == nullptr)
        << "THEN RETURN not supported on pipe INSERT yet";
  }

  if (stmt->query() != nullptr) {
    VALIDATOR_RET_CHECK_EQ(stmt->row_list_size(), 0)
        << "INSERT has both query and VALUES";

    // With pipe input, the query sees the pipe query's visible columns.
    // With nested INSERTs inside UPDATES, query_parameter_list has columns
    // that are visible from the outer structure.
    std::set<ResolvedColumn> visible_parameters = pipe_visible_parameters;
    for (const std::unique_ptr<const ResolvedColumnRef>& parameter :
         stmt->query_parameter_list()) {
      GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(visible_columns,
                                           /*visible_parameters=*/{},
                                           parameter.get()));
      visible_parameters.insert(parameter->column());
    }

    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedScan(stmt->query(), visible_parameters));

    const std::set<ResolvedColumn> produced_columns(
        stmt->query()->column_list().begin(),
        stmt->query()->column_list().end());

    const ResolvedColumnList& query_output_column_list =
        stmt->query_output_column_list();

    VALIDATOR_RET_CHECK_EQ(query_output_column_list.size(),
                           inserted_columns.size());
    for (int i = 0; i < query_output_column_list.size(); ++i) {
      VALIDATOR_RET_CHECK(query_output_column_list[i].type()->Equals(
          inserted_columns[i].type()));
      VALIDATOR_RET_CHECK(
          googlesql_base::ContainsKey(produced_columns, query_output_column_list[i]))
          << "InsertStmt query does not produce column referenced in "
             "query_output_column_list: "
          << query_output_column_list[i].DebugString();
    }
  } else {
    VALIDATOR_RET_CHECK_GT(stmt->row_list_size(), 0)
        << "INSERT has neither query nor VALUES";
    for (const std::unique_ptr<const ResolvedInsertRow>& insert_row :
         stmt->row_list()) {
      VALIDATOR_RET_CHECK_EQ(insert_row->value_list_size(),
                             inserted_columns.size());
      for (int i = 0; i < insert_row->value_list_size(); ++i) {
        const ResolvedDMLValue* dml_value = insert_row->value_list(i);
        VALIDATOR_RET_CHECK(dml_value->value() != nullptr);
        const ResolvedExpr* expr = dml_value->value();
        GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(visible_columns,
                                             /*visible_parameters=*/{}, expr));
        VALIDATOR_RET_CHECK(expr->type()->Equals(inserted_columns[i].type()));
      }
    }
  }

  if (stmt->returning() != nullptr) {
    // Returning clause is only valid on top-level UPDATE.
    VALIDATOR_RET_CHECK_EQ(array_element_column, nullptr);
    GOOGLESQL_RETURN_IF_ERROR(
        ValidateResolvedReturningClause(stmt->returning(), visible_columns));
  }

  if (stmt->on_conflict_clause() != nullptr) {
    // ON CONFLICT is allowed on top-level INSERT only.
    VALIDATOR_RET_CHECK_EQ(array_element_column, nullptr)
        << "Nested INSERT cannot have ON CONFLICT clause";
    VALIDATOR_RET_CHECK(stmt->table_scan() != nullptr);
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedOnConflictClause(stmt->on_conflict_clause(),
                                                     visible_columns));
  }

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedOnConflictClause(
    const ResolvedOnConflictClause* on_conflict_clause,
    const std::set<ResolvedColumn>& visible_columns) {
  PushErrorContext push(this, on_conflict_clause);

  for (const auto& column : on_conflict_clause->conflict_target_column_list()) {
    GOOGLESQL_RETURN_IF_ERROR(CheckColumnIsPresentInColumnSet(column, visible_columns));
  }

  if (on_conflict_clause->conflict_action() ==
      ResolvedOnConflictClauseEnums::NOTHING) {
    return absl::OkStatus();
  }

  // DO UPDATE must have a conflict target or unique constraint name.
  VALIDATOR_RET_CHECK(on_conflict_clause->conflict_target_column_list_size() !=
                          0 ||
                      !on_conflict_clause->unique_constraint_name().empty());

  std::set<ResolvedColumn> visible_columns_with_insert_row_scan;
  visible_columns_with_insert_row_scan.insert(visible_columns.begin(),
                                              visible_columns.end());
  if (on_conflict_clause->insert_row_scan() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedScan(on_conflict_clause->insert_row_scan(),
                                         /*visible_parameters=*/{}));
    GOOGLESQL_RETURN_IF_ERROR(
        AddColumnList(on_conflict_clause->insert_row_scan()->column_list(),
                      &visible_columns_with_insert_row_scan));
  }

  VALIDATOR_RET_CHECK_GT(on_conflict_clause->update_item_list_size(), 0);
  for (const std::unique_ptr<const ResolvedUpdateItem>& item :
       on_conflict_clause->update_item_list()) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedUpdateItem(
        item.get(), /*allow_nested_statements=*/false,
        /*element_column=*/nullptr, visible_columns,
        visible_columns_with_insert_row_scan));
  }

  if (on_conflict_clause->update_where_expression() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(
        visible_columns_with_insert_row_scan, /*visible parameters=*/{},
        on_conflict_clause->update_where_expression()));
    VALIDATOR_RET_CHECK(
        on_conflict_clause->update_where_expression()->type()->IsBool())
        << "UpdateStmt has WHERE expression with non-BOOL type: "
        << on_conflict_clause->update_where_expression()->type()->DebugString();
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedDeleteStmt(
    const ResolvedDeleteStmt* stmt,
    const std::set<ResolvedColumn>* outer_visible_columns,
    const ResolvedColumn* array_element_column) {
  PushErrorContext push(this, stmt);
  std::set<ResolvedColumn> visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(
      ValidateResolvedDMLStmt(stmt, array_element_column, &visible_columns));
  if (outer_visible_columns != nullptr) {
    visible_columns.insert(outer_visible_columns->begin(),
                           outer_visible_columns->end());
  }

  if (array_element_column == nullptr) {
    // Top-level DELETE.
    VALIDATOR_RET_CHECK(stmt->array_offset_column() == nullptr);
    VALIDATOR_RET_CHECK_EQ(stmt->table_scan()->column_index_list().size(),
                           stmt->column_access_list().size());
  } else {
    // Nested DELETE.
    VALIDATOR_RET_CHECK(stmt->table_scan() == nullptr);
    VALIDATOR_RET_CHECK_EQ(stmt->column_access_list().size(), 0);
  }

  if (stmt->array_offset_column() != nullptr) {
    visible_columns.insert(stmt->array_offset_column()->column());
  }

  VALIDATOR_RET_CHECK(stmt->where_expr() != nullptr);
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(
      visible_columns, /*visible_parameters=*/{}, stmt->where_expr()));
  VALIDATOR_RET_CHECK(stmt->where_expr()->type()->IsBool())
      << "DeleteStmt has WHERE expression with non-BOOL type: "
      << stmt->where_expr()->type()->DebugString();
  if (stmt->returning() != nullptr) {
    // Returning clause is only valid on top-level DELETE.
    VALIDATOR_RET_CHECK_EQ(array_element_column, nullptr);
    GOOGLESQL_RETURN_IF_ERROR(
        ValidateResolvedReturningClause(stmt->returning(), visible_columns));
  }
  return absl::OkStatus();
}

absl::Status Validator::CheckExprIsPath(const ResolvedExpr* expr,
                                        const ResolvedColumnRef** ref) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, expr);
  switch (expr->node_kind()) {
    case RESOLVED_COLUMN_REF:
      *ref = expr->GetAs<ResolvedColumnRef>();
      return absl::OkStatus();
    case RESOLVED_GET_PROTO_FIELD:
      return CheckExprIsPath(expr->GetAs<ResolvedGetProtoField>()->expr(), ref);
    case RESOLVED_GET_STRUCT_FIELD:
      return CheckExprIsPath(expr->GetAs<ResolvedGetStructField>()->expr(),
                             ref);
    case RESOLVED_GET_JSON_FIELD:
      return CheckExprIsPath(expr->GetAs<ResolvedGetJsonField>()->expr(), ref);
    default:
      VALIDATOR_RET_CHECK_FAIL()
          << "Expression is not a path: " << expr->node_kind_string();
  }
}

absl::Status Validator::ValidateResolvedUpdateStmt(
    const ResolvedUpdateStmt* stmt,
    const std::set<ResolvedColumn>* outer_visible_columns,
    const ResolvedColumn* array_element_column) {
  PushErrorContext push(this, stmt);
  std::set<ResolvedColumn> target_visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedDMLStmt(stmt, array_element_column,
                                          &target_visible_columns));

  VALIDATOR_RET_CHECK_EQ(
      stmt->topologically_sorted_generated_column_id_list_size(),
      stmt->generated_column_expr_list_size());
  if (array_element_column == nullptr) {
    // Top-level UPDATE.
    VALIDATOR_RET_CHECK(stmt->array_offset_column() == nullptr);
    VALIDATOR_RET_CHECK_EQ(stmt->table_scan()->column_index_list().size(),
                           stmt->column_access_list().size());
  } else {
    // Nested UPDATE.
    VALIDATOR_RET_CHECK(stmt->table_scan() == nullptr);
    VALIDATOR_RET_CHECK(stmt->from_scan() == nullptr);
    VALIDATOR_RET_CHECK_EQ(stmt->column_access_list().size(), 0);
  }

  std::set<ResolvedColumn> all_visible_columns(target_visible_columns);
  if (stmt->from_scan() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(
        ValidateResolvedScan(stmt->from_scan(), /*visible_parameters=*/{}));
    GOOGLESQL_RETURN_IF_ERROR(
        AddColumnList(stmt->from_scan()->column_list(), &all_visible_columns));
  }
  if (stmt->array_offset_column() != nullptr) {
    all_visible_columns.insert(stmt->array_offset_column()->column());
  }
  if (outer_visible_columns != nullptr) {
    all_visible_columns.insert(outer_visible_columns->begin(),
                               outer_visible_columns->end());
  }

  VALIDATOR_RET_CHECK_GT(stmt->update_item_list_size(), 0);
  for (const std::unique_ptr<const ResolvedUpdateItem>& item :
       stmt->update_item_list()) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedUpdateItem(
        item.get(), /*allow_nested_statements=*/true,
        /*element_column=*/nullptr, target_visible_columns,
        all_visible_columns));
  }

  VALIDATOR_RET_CHECK(stmt->where_expr() != nullptr);
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(
      all_visible_columns, /*visible parameters=*/{}, stmt->where_expr()));
  VALIDATOR_RET_CHECK(stmt->where_expr()->type()->IsBool())
      << "UpdateStmt has WHERE expression with non-BOOL type: "
      << stmt->where_expr()->type()->DebugString();

  if (stmt->returning() != nullptr) {
    // Returning clause is only valid on top-level UPDATE.
    VALIDATOR_RET_CHECK_EQ(array_element_column, nullptr);
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedReturningClause(stmt->returning(),
                                                    target_visible_columns));
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedUpdateItem(
    const ResolvedUpdateItem* item, bool allow_nested_statements,
    const ResolvedColumn* element_column,
    const std::set<ResolvedColumn>& target_visible_columns,
    const std::set<ResolvedColumn>& offset_and_where_visible_columns) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, item);

  VALIDATOR_RET_CHECK(item->target() != nullptr);
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(
      target_visible_columns, /*visible_parameters=*/{}, item->target()));

  const ResolvedColumnRef* expr_column_ref = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(CheckExprIsPath(item->target(), &expr_column_ref));
  VALIDATOR_RET_CHECK(expr_column_ref);
  if (element_column != nullptr) {
    VALIDATOR_RET_CHECK(
        googlesql_base::ContainsKey(target_visible_columns, *element_column));
    VALIDATOR_RET_CHECK(
        !googlesql_base::ContainsKey(offset_and_where_visible_columns, *element_column));
    VALIDATOR_RET_CHECK_EQ(element_column->column_id(),
                           expr_column_ref->column().column_id());
  }

  const Type* target_type = item->target()->type();
  if (item->set_value() != nullptr) {
    // Flat SET {target} = {value} clause.
    VALIDATOR_RET_CHECK(item->element_column() == nullptr);
    VALIDATOR_RET_CHECK_EQ(item->update_item_element_list_size(), 0);
    VALIDATOR_RET_CHECK_EQ(item->delete_list_size(), 0);
    VALIDATOR_RET_CHECK_EQ(item->update_list_size(), 0);
    VALIDATOR_RET_CHECK_EQ(item->insert_list_size(), 0);

    VALIDATOR_RET_CHECK(item->set_value()->value() != nullptr);
    VALIDATOR_RET_CHECK(
        target_type->Equals(item->set_value()->value()->type()));
  } else {
    // Two Cases:
    // 1) SET {target}[<expr>]{optional_remainder} = {value} clause.
    // 2) Nested DML statement.
    VALIDATOR_RET_CHECK(target_type->IsArray() || target_type->IsJson() ||
                        target_type->IsMap());
    VALIDATOR_RET_CHECK(item->element_column() != nullptr);
    const ResolvedColumn& element_column = item->element_column()->column();
    VALIDATOR_RET_CHECK(element_column.IsInitialized());
    if (target_type->IsArray()) {
      VALIDATOR_RET_CHECK(element_column.type()->Equals(
          target_type->AsArray()->element_type()));
    } else if (target_type->IsMap()) {
      VALIDATOR_RET_CHECK(
          element_column.type()->Equals(target_type->AsMap()->value_type()));
    } else if (target_type->IsJson()) {
      VALIDATOR_RET_CHECK(element_column.type()->IsJson());
    } else {
      VALIDATOR_RET_CHECK_FAIL()
          << "Unexpected type in ValidateResolvedUpdateItem: "
          << target_type->DebugString();
    }

    if (item->update_item_element_list_size() > 0) {
      // Array element modification.
      VALIDATOR_RET_CHECK_EQ(item->delete_list_size(), 0);
      VALIDATOR_RET_CHECK_EQ(item->update_list_size(), 0);
      VALIDATOR_RET_CHECK_EQ(item->insert_list_size(), 0);

      for (const auto& update_item_element : item->update_item_element_list()) {
        GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedUpdateItemElement(
            update_item_element.get(), element_column, target_visible_columns,
            offset_and_where_visible_columns, target_type));
      }
    } else {
      // Nested DML statement.
      VALIDATOR_RET_CHECK_GT(item->delete_list_size() +
                                 item->update_list_size() +
                                 item->insert_list_size(),
                             0);
      VALIDATOR_RET_CHECK(allow_nested_statements)
          << "nested deletes: " << item->delete_list_size()
          << " nested updates: " << item->update_list_size()
          << " nested inserts: " << item->insert_list_size();
      for (const auto& delete_stmt : item->delete_list()) {
        GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedDeleteStmt(
            delete_stmt.get(), &offset_and_where_visible_columns,
            &element_column));
      }
      for (const auto& update_stmt : item->update_list()) {
        GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedUpdateStmt(
            update_stmt.get(), &offset_and_where_visible_columns,
            &element_column));
      }
      for (const auto& insert_stmt : item->insert_list()) {
        GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedInsertStmt(
            insert_stmt.get(), &offset_and_where_visible_columns,
            &element_column));
      }
    }
  }

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedUpdateItemElement(
    const ResolvedUpdateItemElement* item, const ResolvedColumn& element_column,
    const std::set<ResolvedColumn>& target_visible_columns,
    const std::set<ResolvedColumn>& offset_and_where_visible_columns,
    const Type* subscripted_target_type) {
  PushErrorContext push(this, item);
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(offset_and_where_visible_columns,
                                       /*visible_parameters=*/{},
                                       item->subscript()));

  if (subscripted_target_type->IsArray()) {
    VALIDATOR_RET_CHECK_EQ(item->subscript()->type()->kind(), TYPE_INT64);
  } else if (subscripted_target_type->IsJson()) {
    VALIDATOR_RET_CHECK(item->subscript()->type()->kind() == TYPE_INT64 ||
                        item->subscript()->type()->kind() == TYPE_STRING)
        << item->subscript()->type()->DebugString();
  } else if (subscripted_target_type->IsMap()) {
    VALIDATOR_RET_CHECK_EQ(item->subscript()->type(),
                           subscripted_target_type->AsMap()->key_type());
  } else {
    GOOGLESQL_RET_CHECK_FAIL() << "Unexpected type in ValidateResolvedUpdateItemElement: "
                     << subscripted_target_type->DebugString();
  }

  if (item->update_item_mode() ==
      ResolvedUpdateItemElementEnums::ELEMENT_PRESENCE_REQUIRED) {
    VALIDATOR_RET_CHECK(subscripted_target_type->IsArray() ||
                        subscripted_target_type->IsMap())
        << subscripted_target_type->DebugString();
  } else if (item->update_item_mode() ==
             ResolvedUpdateItemElementEnums::ELEMENT_PRESENCE_OPTIONAL) {
    VALIDATOR_RET_CHECK(subscripted_target_type->IsJson() ||
                        subscripted_target_type->IsMap())
        << subscripted_target_type->DebugString();
  } else {
    GOOGLESQL_RET_CHECK_FAIL() << "Unexpected update item mode: "
                     << item->update_item_mode();
  }

  std::set<ResolvedColumn> child_target_visible_columns(target_visible_columns);
  child_target_visible_columns.insert(element_column);
  // We don't allow [] in the target of a nested DML statement, and
  // gen_resolved_ast.py documents that a ResolvedUpdateItem child of a
  // ResolvedUpdateItemElement node cannot have nested statements.
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedUpdateItem(
      item->update_item(), /*allow_nested_statements=*/false, &element_column,
      child_target_visible_columns, offset_and_where_visible_columns));

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedMergeWhen(
    const ResolvedMergeWhen* merge_when,
    const std::set<ResolvedColumn>& all_visible_columns,
    const std::set<ResolvedColumn>& source_visible_columns,
    const std::set<ResolvedColumn>& target_visible_columns) {
  PushErrorContext push(this, merge_when);
  const std::set<ResolvedColumn>* visible_columns = nullptr;
  switch (merge_when->match_type()) {
    // For WHEN MATCHED and WHEN NOT MATCHED BY SOURCE clauses, only UPDATE and
    // DELETE are allowed.
    case ResolvedMergeWhen::MATCHED:
      visible_columns = &all_visible_columns;
      VALIDATOR_RET_CHECK_NE(ResolvedMergeWhen::INSERT,
                             merge_when->action_type());
      break;
    case ResolvedMergeWhen::NOT_MATCHED_BY_SOURCE:
      visible_columns = &target_visible_columns;
      VALIDATOR_RET_CHECK_NE(ResolvedMergeWhen::INSERT,
                             merge_when->action_type());
      break;
    // For WHEN NOT MATCHED BY TARGET merge_when, only INSERT is allowed.
    case ResolvedMergeWhen::NOT_MATCHED_BY_TARGET:
      visible_columns = &source_visible_columns;
      VALIDATOR_RET_CHECK_EQ(ResolvedMergeWhen::INSERT,
                             merge_when->action_type());
      break;
  }

  if (merge_when->match_expr() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(*visible_columns,
                                         /*visible_parameters=*/{},
                                         merge_when->match_expr()));
  }

  switch (merge_when->action_type()) {
    case ResolvedMergeWhen::INSERT:
      VALIDATOR_RET_CHECK(!merge_when->insert_column_list().empty());
      VALIDATOR_RET_CHECK_NE(nullptr, merge_when->insert_row());
      VALIDATOR_RET_CHECK_EQ(merge_when->insert_column_list_size(),
                             merge_when->insert_row()->value_list_size());
      VALIDATOR_RET_CHECK(merge_when->update_item_list().empty());

      for (const ResolvedColumn& column : merge_when->insert_column_list()) {
        GOOGLESQL_RETURN_IF_ERROR(
            CheckColumnIsPresentInColumnSet(column, target_visible_columns));
      }
      for (int i = 0; i < merge_when->insert_row()->value_list_size(); ++i) {
        const ResolvedDMLValue* dml_value =
            merge_when->insert_row()->value_list(i);
        VALIDATOR_RET_CHECK_NE(nullptr, dml_value->value());
        const ResolvedExpr* expr = dml_value->value();
        GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(*visible_columns,
                                             /*visible_parameters=*/{}, expr));
        VALIDATOR_RET_CHECK(
            expr->type()->Equals(merge_when->insert_column_list(i).type()));
      }
      break;
    case ResolvedMergeWhen::UPDATE:
      VALIDATOR_RET_CHECK(!merge_when->update_item_list().empty());
      VALIDATOR_RET_CHECK(merge_when->insert_column_list().empty());
      VALIDATOR_RET_CHECK_EQ(nullptr, merge_when->insert_row());

      for (const std::unique_ptr<const ResolvedUpdateItem>& item :
           merge_when->update_item_list()) {
        GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedUpdateItem(
            item.get(), /*allow_nested_statements=*/false,
            /*element_column=*/nullptr, target_visible_columns,
            all_visible_columns));
      }
      break;
    case ResolvedMergeWhen::DELETE:
      VALIDATOR_RET_CHECK(merge_when->update_item_list().empty());
      VALIDATOR_RET_CHECK(merge_when->insert_column_list().empty());
      VALIDATOR_RET_CHECK_EQ(nullptr, merge_when->insert_row());
      break;
  }

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedMergeStmt(
    const ResolvedMergeStmt* stmt) {
  PushErrorContext push(this, stmt);
  VALIDATOR_RET_CHECK_NE(nullptr, stmt->table_scan());
  GOOGLESQL_RETURN_IF_ERROR(
      ValidateResolvedScan(stmt->table_scan(), /*visible_parameters=*/{}));
  std::set<ResolvedColumn> target_visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(AddColumnList(stmt->table_scan()->column_list(),
                                &target_visible_columns));
  VALIDATOR_RET_CHECK_EQ(stmt->table_scan()->column_index_list().size(),
                         stmt->column_access_list().size());

  VALIDATOR_RET_CHECK_NE(nullptr, stmt->from_scan());
  std::set<ResolvedColumn> source_visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(
      ValidateResolvedScan(stmt->from_scan(), /*visible_parameters=*/{}));
  GOOGLESQL_RETURN_IF_ERROR(
      AddColumnList(stmt->from_scan()->column_list(), &source_visible_columns));

  std::set<ResolvedColumn> all_visible_columns =
      googlesql_base::STLSetUnion(source_visible_columns, target_visible_columns);
  if (nullptr != stmt->merge_expr()) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(
        all_visible_columns, /*visible_parameters=*/{}, stmt->merge_expr()));
  }

  VALIDATOR_RET_CHECK(!stmt->when_clause_list().empty());
  for (const auto& when_clause : stmt->when_clause_list()) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedMergeWhen(
        when_clause.get(), all_visible_columns, source_visible_columns,
        target_visible_columns));
  }
  return absl::OkStatus();
}

// Truncate statement is not supported in nested-DML, however, this is
// enforced by the parser.
absl::Status Validator::ValidateResolvedTruncateStmt(
    const ResolvedTruncateStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);
  VALIDATOR_RET_CHECK_NE(nullptr, stmt->table_scan());
  GOOGLESQL_RETURN_IF_ERROR(
      ValidateResolvedScan(stmt->table_scan(), /*visible_parameters=*/{}));
  std::set<ResolvedColumn> target_visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(AddColumnList(stmt->table_scan()->column_list(),
                                &target_visible_columns));

  if (stmt->where_expr() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(target_visible_columns,
                                         /*visible_parameters=*/{},
                                         stmt->where_expr()));
    VALIDATOR_RET_CHECK(stmt->where_expr()->type()->IsBool())
        << "TruncateStmt has WHERE expression with non-BOOL type: "
        << stmt->where_expr()->type()->DebugString();
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedAlterTableSetOptionsStmt(
    const ResolvedAlterTableSetOptionsStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);
  GOOGLESQL_RETURN_IF_ERROR(ValidateOptionsList(stmt->option_list()));
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedAlterPrivilegeRestrictionStmt(
    const ResolvedAlterPrivilegeRestrictionStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);
  VALIDATOR_RET_CHECK(!stmt->column_privilege_list().empty());
  VALIDATOR_RET_CHECK(absl::AsciiStrToLower(stmt->object_type()) == "table" ||
                      absl::AsciiStrToLower(stmt->object_type()) == "view");
  VALIDATOR_RET_CHECK(!stmt->name_path().empty());
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedAlterObjectStmt(stmt));
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedAlterRowAccessPolicyStmt(
    const ResolvedAlterRowAccessPolicyStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);

  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedTableScan(stmt->table_scan(),
                                            /*visible_parameters=*/{}));

  VALIDATOR_RET_CHECK(!stmt->name().empty());
  VALIDATOR_RET_CHECK(stmt->table_scan() != nullptr);
  // Check that the last element of the name path, which should be the table
  // name, matches the table scan name.
  VALIDATOR_RET_CHECK(googlesql_base::CaseEqual(
      stmt->name_path().back(), stmt->table_scan()->table()->Name()));
  std::set<ResolvedColumn> visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(
      AddColumnList(stmt->table_scan()->column_list(), &visible_columns));
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedAlterObjectStmt(stmt));

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedAlterAllRowAccessPoliciesStmt(
    const ResolvedAlterAllRowAccessPoliciesStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);

  VALIDATOR_RET_CHECK(!stmt->name_path().empty());
  VALIDATOR_RET_CHECK(stmt->table_scan() != nullptr);

  VALIDATOR_RET_CHECK_EQ(1, stmt->alter_action_list_size())
      << "ALTER ALL ROW ACCESS POLICIES expects exactly one revoke action";

  const ResolvedAlterAction* action = stmt->alter_action_list(0);
  VALIDATOR_RET_CHECK_EQ(RESOLVED_REVOKE_FROM_ACTION, action->node_kind())
      << "Currently only REVOKE action is supported in ALTER ALL ROW ACCESS "
         "POLICIES statement";
  auto* revoke_from = action->GetAs<ResolvedRevokeFromAction>();
  if (revoke_from->is_revoke_from_all()) {
    VALIDATOR_RET_CHECK(revoke_from->revokee_expr_list().empty());
  } else {
    VALIDATOR_RET_CHECK(!revoke_from->revokee_expr_list().empty());
  }

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedAlterIndexStmt(
    const ResolvedAlterIndexStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);
  VALIDATOR_RET_CHECK(!stmt->name_path().empty());
  VALIDATOR_RET_CHECK(!stmt->alter_action_list().empty());

  if (stmt->table_scan() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedTableScan(stmt->table_scan(),
                                              /*visible_parameters=*/{}));
  }

  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedAlterObjectStmt(stmt));
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedRenameStmt(
    const ResolvedRenameStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedImportStmt(
    const ResolvedImportStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);

  if (stmt->import_kind() == ResolvedImportStmt::MODULE) {
    // In the ResolvedAST, both the name_path and alias_path should be
    // set (with implicit aliases added if necessary). But file_path
    // should not be set.
    VALIDATOR_RET_CHECK(!stmt->name_path().empty()) << stmt->DebugString();
    VALIDATOR_RET_CHECK(!stmt->alias_path().empty()) << stmt->DebugString();

    VALIDATOR_RET_CHECK(stmt->file_path().empty()) << stmt->DebugString();
    VALIDATOR_RET_CHECK(stmt->into_alias_path().empty()) << stmt->DebugString();
  } else if (stmt->import_kind() == ResolvedImportStmt::PROTO) {
    // In the ResolvedAST, the file_path should be set while name_path and
    // alias_path should not be set.  The into_alias_path may or may not
    // be set.
    VALIDATOR_RET_CHECK(!stmt->file_path().empty()) << stmt->DebugString();

    VALIDATOR_RET_CHECK(stmt->name_path().empty()) << stmt->DebugString();
    VALIDATOR_RET_CHECK(stmt->alias_path().empty()) << stmt->DebugString();
  }

  GOOGLESQL_RETURN_IF_ERROR(ValidateOptionsList(stmt->option_list()));

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedModuleStmt(
    const ResolvedModuleStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);

  VALIDATOR_RET_CHECK(!stmt->name_path().empty());
  GOOGLESQL_RETURN_IF_ERROR(ValidateOptionsList(stmt->option_list()));

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedAssignmentStmt(
    const ResolvedAssignmentStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);
  VALIDATOR_RET_CHECK(stmt->target() != nullptr);
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(
      /*visible_columns=*/{}, /*visible_parameters=*/{}, stmt->target()));

  // Verify that the target is an l-value.  Currently, only system variables
  // and fields of system variables are supported.
  const ResolvedNode* node = stmt->target();
  while (node->node_kind() != RESOLVED_SYSTEM_VARIABLE) {
    switch (node->node_kind()) {
      case RESOLVED_GET_STRUCT_FIELD:
        node = node->GetAs<ResolvedGetStructField>()->expr();
        break;
      case RESOLVED_GET_PROTO_FIELD:
        node = node->GetAs<ResolvedGetProtoField>()->expr();
        break;
      default:
        VALIDATOR_RET_CHECK_FAIL()
            << "Expected l-value; got " << node->node_kind_string();
    }
  }

  VALIDATOR_RET_CHECK(stmt->expr() != nullptr);
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(
      /*visible_columns=*/{}, /*visible_parameters=*/{}, stmt->expr()));
  VALIDATOR_RET_CHECK(stmt->expr()->type()->Equals(stmt->target()->type()));
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedAnalyzeStmt(
    const ResolvedAnalyzeStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);
  if (!stmt->option_list().empty()) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateOptionsList(stmt->option_list()));
  }
  if (!stmt->table_and_column_index_list().empty()) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedTableAndColumnInfoList(
        stmt->table_and_column_index_list()));
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedAssertStmt(
    const ResolvedAssertStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);
  VALIDATOR_RET_CHECK(stmt->expression() != nullptr);
  VALIDATOR_RET_CHECK(stmt->expression()->type()->IsBool());
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(
      /*visible_columns=*/{}, /*visible_parameters=*/{}, stmt->expression()));
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedFunctionArgument(
    const std::set<ResolvedColumn>& visible_columns,
    const std::set<ResolvedColumn>& visible_parameters,
    const ResolvedFunctionArgument* resolved_arg,
    bool allow_unset_argument_scan) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, resolved_arg);
  VALIDATOR_RET_CHECK(resolved_arg != nullptr);
  int fields_set = 0;
  if (resolved_arg->expr() != nullptr) {
    ++fields_set;
    // This is a TVF scalar argument. Validate the input expression,
    // and pass through the 'visible_parameters' because the
    // expression may be correlated.
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(visible_columns, visible_parameters,
                                         resolved_arg->expr()));
  }
  if (resolved_arg->model() != nullptr) {
    ++fields_set;
    resolved_arg->model()->model();  // Mark field as visited.
  }
  if (resolved_arg->connection() != nullptr) {
    ++fields_set;
    resolved_arg->connection()->connection();  // Mark field as visited.
  }
  if (resolved_arg->sequence() != nullptr) {
    ++fields_set;
    resolved_arg->sequence()->sequence();  // Mark field as visited.
  }
  if (resolved_arg->descriptor_arg() != nullptr) {
    ++fields_set;
    const ResolvedDescriptor* descriptor = resolved_arg->descriptor_arg();
    if (descriptor->descriptor_column_list_size() > 0) {
      VALIDATOR_RET_CHECK_EQ(descriptor->descriptor_column_list_size(),
                             descriptor->descriptor_column_name_list_size());
    }
    // We just mark the column list as visited without validating.
    // The columns that are referenceable come from the sibling argument
    // with relation type, which we don't have easy access to.
    descriptor->descriptor_column_list();  // Mark field as visited.

    descriptor->descriptor_column_name_list();  // Mark field as visited.
  }
  if (resolved_arg->scan() != nullptr) {
    ++fields_set;
    // This is a TVF relation argument. Validate the input relation,
    // passing through the 'visible_parameters' because correlation references
    // may be present in the scan.
    if (resolved_arg->scan()->Is<ResolvedUnsetArgumentScan>()) {
      GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedUnsetArgumentScan(
          resolved_arg->scan()->GetAs<ResolvedUnsetArgumentScan>(),
          allow_unset_argument_scan));
    } else {
      VALIDATOR_RET_CHECK_GT(resolved_arg->argument_column_list_size(), 0);
      GOOGLESQL_RETURN_IF_ERROR(
          ValidateResolvedScan(resolved_arg->scan(), visible_parameters));
    }
    // Verify that columns in <argument_column_list> are actually available
    // in <scan>.
    const std::set<ResolvedColumn> produced_columns(
        resolved_arg->scan()->column_list().begin(),
        resolved_arg->scan()->column_list().end());
    for (const ResolvedColumn& argument_column :
         resolved_arg->argument_column_list()) {
      VALIDATOR_RET_CHECK(googlesql_base::ContainsKey(produced_columns, argument_column))
          << "TVFArgument scan does not produce column referenced in "
             "argument_column_list: "
          << argument_column.DebugString();
    }
  } else {
    VALIDATOR_RET_CHECK_EQ(0, resolved_arg->argument_column_list_size());
  }
  if (resolved_arg->inline_lambda() != nullptr) {
    ++fields_set;
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedInlineLambda(
        visible_columns, visible_parameters, resolved_arg->inline_lambda()));
  }
  if (resolved_arg->graph() != nullptr) {
    ++fields_set;
    resolved_arg->graph();  // Mark field as visited.
  }
  if (!resolved_arg->argument_alias().empty()) {
    // For now only `expr`-typed arguments can have aliases.
    VALIDATOR_RET_CHECK(resolved_arg->expr() != nullptr);
  }
  VALIDATOR_RET_CHECK_EQ(1, fields_set)
      << "ResolvedTVFArgument should have exactly one field set";
  return absl::OkStatus();
}

absl::Status Validator::ValidateRelationSchemaInResolvedFunctionArgument(
    const TVFRelation& required_input_schema, const TVFRelation& input_relation,
    const ResolvedFunctionArgument* resolved_arg) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, resolved_arg);
  VALIDATOR_RET_CHECK(resolved_arg != nullptr);
  VALIDATOR_RET_CHECK(resolved_arg->scan() != nullptr);
  // Check that the provided columns match position-wise with the required
  // columns. The provided input relation must have exactly the same number
  // of columns as the required input relation, and each (required,
  // provided) column pair must have the same names and types.
  VALIDATOR_RET_CHECK_EQ(input_relation.num_columns(),
                         required_input_schema.num_columns());
  VALIDATOR_RET_CHECK_EQ(input_relation.num_columns(),
                         resolved_arg->argument_column_list_size());
  if (required_input_schema.is_value_table()) {
    VALIDATOR_RET_CHECK_EQ(1, input_relation.num_columns());
    VALIDATOR_RET_CHECK_EQ(1, resolved_arg->argument_column_list_size());
    VALIDATOR_RET_CHECK(input_relation.column(0).type->Equals(
        resolved_arg->argument_column_list(0).type()));
  } else {
    for (int col_idx = 0; col_idx < input_relation.columns().size();
         ++col_idx) {
      VALIDATOR_RET_CHECK(input_relation.column(col_idx).type->Equals(
          required_input_schema.column(col_idx).type))
          << "col_idx" << col_idx << " input_type: "
          << input_relation.column(col_idx).type->DebugString(true)
          << " required_type: "
          << required_input_schema.column(col_idx).type->DebugString(true);

      VALIDATOR_RET_CHECK(
          googlesql_base::CaseEqual(input_relation.column(col_idx).name,
                                 required_input_schema.column(col_idx).name))
          << "input relation column name: "
          << input_relation.column(col_idx).name
          << ", required relation column name: "
          << required_input_schema.column(col_idx).name;
      VALIDATOR_RET_CHECK(input_relation.column(col_idx).type->Equals(
          resolved_arg->argument_column_list(col_idx).type()));
    }
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateAlterIndexActions(
    const ResolvedAlterObjectStmt* stmt) {
  bool has_rebuild_action = false;
  for (const auto& alter_action : stmt->alter_action_list()) {
    if (has_rebuild_action) {
      VALIDATOR_RET_CHECK_FAIL()
          << "REBUILD action must be the last action in the list.";
    }
    switch (alter_action->node_kind()) {
      case RESOLVED_REBUILD_ACTION:
        has_rebuild_action = true;
        VALIDATOR_RET_CHECK_EQ(stmt->node_kind(), RESOLVED_ALTER_INDEX_STMT)
            << "Rebuild action is only supported in ALTER VECTOR|SEARCH INDEX "
               "statement, found in "
            << stmt->node_kind_string();
        break;
      case RESOLVED_ADD_COLUMN_IDENTIFIER_ACTION:
        VALIDATOR_RET_CHECK_EQ(stmt->node_kind(), RESOLVED_ALTER_INDEX_STMT)
            << "AddColumnIdentifier action is only supported in ALTER "
               "VECTOR|SEARCH INDEX statement, found in "
            << stmt->node_kind_string();
        break;
      case RESOLVED_DROP_COLUMN_ACTION:
      case RESOLVED_ALTER_COLUMN_OPTIONS_ACTION:
      case RESOLVED_SET_OPTIONS_ACTION:
        break;
      default:
        VALIDATOR_RET_CHECK_NE(stmt->node_kind(), RESOLVED_ALTER_INDEX_STMT)
            << "Unsupported alter action in ALTER VECTOR|SEARCH INDEX "
               "statement: "
            << alter_action->node_kind_string();
        break;
    }
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedAlterObjectStmt(
    const ResolvedAlterObjectStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);

  GOOGLESQL_RETURN_IF_ERROR(ValidateAlterIndexActions(stmt));

  std::set<ResolvedColumn> visible_columns;
  if (stmt->node_kind() == RESOLVED_ALTER_TABLE_STMT) {
    bool has_add_generated_column = false;
    for (const auto& action : stmt->alter_action_list()) {
      if (action->node_kind() == RESOLVED_ADD_COLUMN_ACTION &&
          action->GetAs<ResolvedAddColumnAction>()
                  ->column_definition()
                  ->generated_column_info() != nullptr) {
        has_add_generated_column = true;
        break;
      }
    }
    const ResolvedTableScan* table_scan =
        stmt->GetAs<ResolvedAlterTableStmt>()->table_scan();
    if (table_scan != nullptr) {
      VALIDATOR_RET_CHECK(has_add_generated_column);
      VALIDATOR_RET_CHECK(language_options_.LanguageFeatureEnabled(
          FEATURE_ENABLE_ALTER_TABLE_ADD_COLUMN_GENERATED_EXPR));
      for (const ResolvedColumn& column : table_scan->column_list()) {
        visible_columns.insert(column);
      }
    }
  }
  for (const auto& alter_action : stmt->alter_action_list()) {
    GOOGLESQL_RETURN_IF_ERROR(
        ValidateResolvedAlterAction(alter_action.get(), &visible_columns));
  }

  // Validate we don't drop or create any column twice.
  std::set<std::string, googlesql_base::CaseLess> new_columns,
      columns_to_drop;

  // Validate there exists either rename column actions only or other alter
  // actions only in this alter statement.
  bool has_rename_column = false;
  bool has_non_rename_column = false;
  for (const auto& action : stmt->alter_action_list()) {
    if (action->node_kind() == RESOLVED_ADD_COLUMN_ACTION) {
      const std::string name =
          action->GetAs<ResolvedAddColumnAction>()->column_definition()->name();
      VALIDATOR_RET_CHECK(new_columns.insert(name).second)
          << "Column added twice: " << name;
    } else if (action->node_kind() == RESOLVED_ADD_COLUMN_IDENTIFIER_ACTION) {
      const std::string name =
          action->GetAs<ResolvedAddColumnIdentifierAction>()->name();
      VALIDATOR_RET_CHECK(new_columns.insert(name).second)
          << "Column added twice: " << name;
    } else if (action->node_kind() == RESOLVED_DROP_COLUMN_ACTION) {
      const std::string name =
          action->GetAs<ResolvedDropColumnAction>()->name();
      VALIDATOR_RET_CHECK(columns_to_drop.insert(name).second)
          << "Column dropped twice: " << name;
      VALIDATOR_RET_CHECK(new_columns.find(name) == new_columns.end())
          << "Newly added column is being dropped: " << name;
    }
    if (action->node_kind() == RESOLVED_RENAME_COLUMN_ACTION) {
      has_rename_column = true;
    } else {
      has_non_rename_column = true;
    }
    VALIDATOR_RET_CHECK(!has_rename_column || !has_non_rename_column)
        << "RENAME COLUMN cannot be used with other alter statement.";
  }

  return absl::OkStatus();
}

absl::Status Validator::ValidateAddForeignKeyAction(
    const ResolvedAddConstraintAction* action,
    absl::flat_hash_set<std::string>* constraint_names) {
  GOOGLESQL_RET_CHECK(action->constraint()->node_kind() == RESOLVED_FOREIGN_KEY &&
            action->table() != nullptr);

  const auto* foreign_key = action->constraint()->GetAs<ResolvedForeignKey>();
  const Table* referencing_table = action->table();
  std::vector<const Type*> column_types;
  column_types.reserve(referencing_table->NumColumns());
  for (int i = 0; i < referencing_table->NumColumns(); i++) {
    column_types.push_back(referencing_table->GetColumn(i)->GetType());
  }
  GOOGLESQL_RETURN_IF_ERROR(
      ValidateResolvedForeignKey(foreign_key, column_types, constraint_names));

  VALIDATOR_RET_CHECK(foreign_key->referencing_column_list().size() ==
                      foreign_key->referencing_column_offset_list().size());
  for (int i = 0; i < foreign_key->referencing_column_list().size(); i++) {
    absl::string_view name = foreign_key->referencing_column_list(i);
    int offset = foreign_key->referencing_column_offset_list(i);
    const Column* referencing_column = referencing_table->GetColumn(offset);
    VALIDATOR_RET_CHECK(referencing_column != nullptr);
    VALIDATOR_RET_CHECK(referencing_column->Name() == name);
  }

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedAlterAction(
    const ResolvedAlterAction* action,
    std::set<ResolvedColumn>* visible_columns) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, action);
  absl::flat_hash_set<std::string> seen_constraints;
  switch (action->node_kind()) {
    case RESOLVED_SET_OPTIONS_ACTION:
      GOOGLESQL_RETURN_IF_ERROR(ValidateOptionsList(
          action->GetAs<ResolvedSetOptionsAction>()->option_list()));
      break;
    case RESOLVED_ADD_COLUMN_ACTION: {
      auto* column_definition =
          action->GetAs<ResolvedAddColumnAction>()->column_definition();
      if (column_definition->annotations() != nullptr) {
        GOOGLESQL_RETURN_IF_ERROR(
            ValidateColumnAnnotations(column_definition->annotations()));
        GOOGLESQL_ASSIGN_OR_RETURN(TypeParameters full_type_parameters,
                         column_definition->GetFullTypeParameters());
        GOOGLESQL_RETURN_IF_ERROR(
            column_definition->type()->ValidateResolvedTypeParameters(
                full_type_parameters, language_options_.product_mode()));
      }
      VALIDATOR_RET_CHECK(column_definition->type() != nullptr);
      VALIDATOR_RET_CHECK(!column_definition->name().empty());
      if (column_definition->default_value() != nullptr) {
        VALIDATOR_RET_CHECK_EQ(column_definition->generated_column_info(),
                               nullptr);
        GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedColumnDefaultValue(
            column_definition->default_value(), column_definition->type()));
      }
      if (column_definition->generated_column_info() != nullptr) {
        VALIDATOR_RET_CHECK_EQ(column_definition->default_value(), nullptr);
        const std::set<ResolvedColumn>& current_visible_columns =
            (visible_columns != nullptr) ? *visible_columns
                                         : std::set<ResolvedColumn>();
        GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedGeneratedColumnInfo(
            column_definition, current_visible_columns));
      }
      if (visible_columns != nullptr) {
        visible_columns->insert(column_definition->column());
      }
    } break;
    case RESOLVED_ADD_COLUMN_IDENTIFIER_ACTION: {
      auto* add_column_identifier_action =
          action->GetAs<ResolvedAddColumnIdentifierAction>();
      VALIDATOR_RET_CHECK(!add_column_identifier_action->name().empty());
      GOOGLESQL_RETURN_IF_ERROR(
          ValidateOptionsList(add_column_identifier_action->options_list()));
    } break;
    case RESOLVED_ADD_TO_RESTRICTEE_LIST_ACTION:
      // Nothing to do
      break;
    case RESOLVED_DROP_COLUMN_ACTION:
      VALIDATOR_RET_CHECK(
          !action->GetAs<ResolvedDropColumnAction>()->name().empty());
      break;
    case RESOLVED_RENAME_COLUMN_ACTION: {
      GOOGLESQL_RET_CHECK(!action->GetAs<ResolvedRenameColumnAction>()->name().empty());
      GOOGLESQL_RET_CHECK(
          !action->GetAs<ResolvedRenameColumnAction>()->new_name().empty());
    } break;
    case RESOLVED_GRANT_TO_ACTION:
      VALIDATOR_RET_CHECK(
          action->GetAs<ResolvedGrantToAction>()->grantee_expr_list_size() > 0);
      break;
    case RESOLVED_RESTRICT_TO_ACTION:
      // Nothing to do
      break;
    case RESOLVED_REMOVE_FROM_RESTRICTEE_LIST_ACTION:
      // Nothing to do
      break;
    case RESOLVED_FILTER_USING_ACTION: {
      auto* filter_using = action->GetAs<ResolvedFilterUsingAction>();
      VALIDATOR_RET_CHECK(filter_using->predicate() != nullptr);
      VALIDATOR_RET_CHECK(filter_using->predicate()->type()->IsBool())
          << "FilterUsing AlterRowAccessPolicy action has predicate with "
             "non-BOOL type: "
          << filter_using->predicate()->type()->DebugString();
    } break;
    case RESOLVED_REVOKE_FROM_ACTION: {
      auto* revoke_from = action->GetAs<ResolvedRevokeFromAction>();
      if (revoke_from->is_revoke_from_all()) {
        VALIDATOR_RET_CHECK(revoke_from->revokee_expr_list().empty());
      } else {
        VALIDATOR_RET_CHECK(!revoke_from->revokee_expr_list().empty());
      }
    } break;
    case RESOLVED_RENAME_TO_ACTION:
      VALIDATOR_RET_CHECK(
          !action->GetAs<ResolvedRenameToAction>()->new_path().empty());
      break;
    case RESOLVED_ADD_CONSTRAINT_ACTION: {
      const auto* add_constraint = action->GetAs<ResolvedAddConstraintAction>();
      if (add_constraint->constraint()->node_kind() == RESOLVED_FOREIGN_KEY &&
          add_constraint->table() != nullptr) {
        return ValidateAddForeignKeyAction(add_constraint, &seen_constraints);
      } else if (add_constraint->constraint()->node_kind() ==
                 RESOLVED_PRIMARY_KEY) {
        const auto* primary_key =
            add_constraint->constraint()->GetAs<ResolvedPrimaryKey>();
        std::vector<const Type*> column_types;
        if (add_constraint->table() != nullptr) {
          for (int i = 0; i < add_constraint->table()->NumColumns(); i++) {
            column_types.push_back(
                add_constraint->table()->GetColumn(i)->GetType());
          }
        }
        return ValidateResolvedPrimaryKey(column_types, primary_key,
                                          &seen_constraints);
      }
    } break;
    case RESOLVED_DROP_CONSTRAINT_ACTION:
      VALIDATOR_RET_CHECK(
          !action->GetAs<ResolvedDropConstraintAction>()->name().empty());
      break;
    case RESOLVED_DROP_PRIMARY_KEY_ACTION:
      // Nothing to do
      break;
    case RESOLVED_ALTER_COLUMN_OPTIONS_ACTION:
      VALIDATOR_RET_CHECK(
          !action->GetAs<ResolvedAlterColumnOptionsAction>()->column().empty());
      GOOGLESQL_RETURN_IF_ERROR(ValidateOptionsList(
          action->GetAs<ResolvedAlterColumnOptionsAction>()->option_list()));
      break;
    case RESOLVED_ALTER_COLUMN_SET_DATA_TYPE_ACTION: {
      const auto* set_data_type =
          action->GetAs<ResolvedAlterColumnSetDataTypeAction>();
      VALIDATOR_RET_CHECK(!set_data_type->column().empty());
      VALIDATOR_RET_CHECK(set_data_type->updated_type() != nullptr);
      GOOGLESQL_RETURN_IF_ERROR(
          set_data_type->updated_type()->ValidateResolvedTypeParameters(
              set_data_type->updated_type_parameters(),
              language_options_.product_mode()));
      if (set_data_type->updated_annotations() != nullptr) {
        VALIDATOR_RET_CHECK(set_data_type->updated_annotations());
        // Validate collation annotations if exist.
        GOOGLESQL_RETURN_IF_ERROR(
            ValidateUpdatedAnnotations(set_data_type->updated_annotations()));
      }
    } break;
    case RESOLVED_ALTER_COLUMN_DROP_NOT_NULL_ACTION:
      VALIDATOR_RET_CHECK(!action->GetAs<ResolvedAlterColumnDropNotNullAction>()
                               ->column()
                               .empty());
      break;
    case RESOLVED_ALTER_COLUMN_SET_DEFAULT_ACTION: {
      const auto* set_default =
          action->GetAs<ResolvedAlterColumnSetDefaultAction>();
      VALIDATOR_RET_CHECK(!set_default->column().empty());
      const ResolvedColumnDefaultValue* default_value =
          set_default->default_value();
      VALIDATOR_RET_CHECK(default_value != nullptr);
      GOOGLESQL_RETURN_IF_ERROR(
          ValidateResolvedColumnDefaultValue(default_value,
                                             /*column_type=*/nullptr,
                                             /*skip_check_type_match=*/true));
    } break;
    case RESOLVED_ALTER_COLUMN_DROP_DEFAULT_ACTION:
      VALIDATOR_RET_CHECK(!action->GetAs<ResolvedAlterColumnDropDefaultAction>()
                               ->column()
                               .empty());
      break;
    case RESOLVED_ALTER_COLUMN_DROP_GENERATED_ACTION:
      VALIDATOR_RET_CHECK(
          !action->GetAs<ResolvedAlterColumnDropGeneratedAction>()
               ->column()
               .empty());
      break;
    case RESOLVED_SET_COLLATE_CLAUSE:
      VALIDATOR_RET_CHECK(
          action->GetAs<ResolvedSetCollateClause>()->collation_name() !=
          nullptr);
      GOOGLESQL_RETURN_IF_ERROR(ValidateCollateExpr(
          action->GetAs<ResolvedSetCollateClause>()->collation_name()));
      break;
    case RESOLVED_ALTER_SUB_ENTITY_ACTION: {
      const auto* alter_sub_entity_action =
          action->GetAs<ResolvedAlterSubEntityAction>();
      VALIDATOR_RET_CHECK(!alter_sub_entity_action->entity_type().empty());
      VALIDATOR_RET_CHECK(!alter_sub_entity_action->name().empty());
      GOOGLESQL_RETURN_IF_ERROR(
          ValidateResolvedAlterAction(alter_sub_entity_action->alter_action(),
                                      /*visible_columns=*/nullptr));
    } break;
    case RESOLVED_ADD_SUB_ENTITY_ACTION: {
      const auto* add_sub_entity_action =
          action->GetAs<ResolvedAddSubEntityAction>();
      VALIDATOR_RET_CHECK(!add_sub_entity_action->entity_type().empty());
      VALIDATOR_RET_CHECK(!add_sub_entity_action->name().empty());
      GOOGLESQL_RETURN_IF_ERROR(
          ValidateOptionsList(add_sub_entity_action->options_list()));
    } break;
    case RESOLVED_DROP_SUB_ENTITY_ACTION: {
      const auto* drop_sub_entity_action =
          action->GetAs<ResolvedDropSubEntityAction>();
      VALIDATOR_RET_CHECK(!drop_sub_entity_action->entity_type().empty());
      VALIDATOR_RET_CHECK(!drop_sub_entity_action->name().empty());
    } break;
    case RESOLVED_REBUILD_ACTION:
      // Nothing to do
      break;
    case RESOLVED_ALTER_COLUMN_SET_GENERATED_ACTION: {
      const auto* alter_column_set_generated_action =
          action->GetAs<ResolvedAlterColumnSetGeneratedAction>();
      VALIDATOR_RET_CHECK(!alter_column_set_generated_action->column().empty());
      VALIDATOR_RET_CHECK(
          alter_column_set_generated_action->generated_column_info() !=
          nullptr);
      VALIDATOR_RET_CHECK(
          alter_column_set_generated_action->generated_column_info()
              ->expression() == nullptr);
      VALIDATOR_RET_CHECK(
          alter_column_set_generated_action->generated_column_info()
              ->identity_column_info() != nullptr);
      GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedIdentityColumnInfo(
          /*column_type=*/nullptr,
          alter_column_set_generated_action->generated_column_info()
              ->identity_column_info(),
          /*skip_check_type_match=*/true));
    } break;
    default:
      return InternalErrorBuilder()
             << "Unhandled node kind: " << action->node_kind_string()
             << " in ValidateResolvedAlterAction";
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedColumnDefaultValue(
    const ResolvedColumnDefaultValue* default_value,
    const googlesql::Type* column_type, bool skip_check_type_match) {
  VALIDATOR_RET_CHECK_NE(default_value->expression(), nullptr);
  VALIDATOR_RET_CHECK(!default_value->sql().empty());
  if (!skip_check_type_match) {
    VALIDATOR_RET_CHECK(
        default_value->expression()->type()->Equals(column_type));
  }
  return ValidateResolvedExpr(
      /*visible_columns=*/{}, /*visible_parameters=*/{},
      default_value->expression());
}

absl::Status Validator::ValidateResolvedExecuteImmediateStmt(
    const ResolvedExecuteImmediateStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);

  VALIDATOR_RET_CHECK(stmt->sql()->type()->IsString())
      << "SQL must evaluate to type STRING";
  absl::flat_hash_set<std::string> seen;
  for (const std::string& into_identifier : stmt->into_identifier_list()) {
    VALIDATOR_RET_CHECK(
        seen.insert(absl::AsciiStrToLower(into_identifier)).second)
        << "The same parameter cannot be assigned multiple times in an INTO "
           "clause";
  }

  seen.clear();
  bool expecting_names = false;
  for (int i = 0; i < stmt->using_argument_list_size(); i++) {
    const ResolvedExecuteImmediateArgument* expr = stmt->using_argument_list(i);
    bool has_name = !expr->name().empty();
    if (has_name) {
      VALIDATOR_RET_CHECK(
          seen.insert(absl::AsciiStrToLower(expr->name())).second)
          << "The same parameter cannot be assigned multiple times in an INTO "
             "clause";
    }
    if (i == 0) {
      expecting_names = has_name;
    } else {
      VALIDATOR_RET_CHECK(expecting_names == has_name)
          << "Cannot mix named and positional parameters";
    }
  }

  return absl::OkStatus();
}

absl::Status Validator::ValidateMatchRecognizeVariableName(
    absl::string_view name) {
  VALIDATOR_RET_CHECK(!name.empty()) << "Pattern variable name cannot be empty";
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedMatchRecognizePatternOperation(
    const ResolvedMatchRecognizePatternOperation* operation,
    const absl::flat_hash_set<absl::string_view>& available_variable_names) {
  switch (operation->op_type()) {
    case ResolvedMatchRecognizePatternOperation::CONCAT:
    case ResolvedMatchRecognizePatternOperation::ALTERNATE: {
      VALIDATOR_RET_CHECK_GE(operation->operand_list_size(), 2)
          << "Pattern operation "
          << ResolvedMatchRecognizePatternOperationEnums::
                 MatchRecognizePatternOperationType_Name(operation->op_type())
          << " requires at least two operands.";
      for (const auto& argument : operation->operand_list()) {
        GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedMatchRecognizePatternExpr(
            argument->GetAs<ResolvedMatchRecognizePatternExpr>(),
            available_variable_names));
      }
      return absl::OkStatus();
    }
    default:
      VALIDATOR_RET_CHECK_FAIL()
          << "Unsupported row pattern operation type: "
          << ResolvedMatchRecognizePatternOperationEnums::
                 MatchRecognizePatternOperationType_Name(operation->op_type());
  }
  GOOGLESQL_RET_CHECK_FAIL() << "Should have returned from the switch statement";
}

absl::Status Validator::ValidateResolvedMatchRecognizePatternQuantification(
    const ResolvedMatchRecognizePatternQuantification* quantification,
    const absl::flat_hash_set<absl::string_view>& available_variable_names) {
  // Recursively validate the operand.
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedMatchRecognizePatternExpr(
      quantification->operand(), available_variable_names));

  VALIDATOR_RET_CHECK(quantification->lower_bound() != nullptr);
  GOOGLESQL_RETURN_IF_ERROR(ValidateArgumentIsInt64(
      quantification->lower_bound(), /*validate_constant_nonnegative=*/true,
      "Quantifier lower bound"));

  if (quantification->upper_bound() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateArgumentIsInt64(
        quantification->upper_bound(), /*validate_constant_nonnegative=*/true,
        "Quantifier upper bound"));
  }

  // Dummy access to the "is_reluctant" bit, since it's valid either way.
  quantification->is_reluctant();

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedMatchRecognizePatternExpr(
    const ResolvedMatchRecognizePatternExpr* pattern,
    const absl::flat_hash_set<absl::string_view>& available_variable_names) {
  switch (pattern->node_kind()) {
    case RESOLVED_MATCH_RECOGNIZE_PATTERN_EMPTY: {
      // Nothing to validate.
      return absl::OkStatus();
    }
    case RESOLVED_MATCH_RECOGNIZE_PATTERN_ANCHOR: {
      VALIDATOR_RET_CHECK_NE(
          pattern->GetAs<ResolvedMatchRecognizePatternAnchor>()->mode(),
          ResolvedMatchRecognizePatternAnchorEnums::MODE_UNSPECIFIED);
      return absl::OkStatus();
    }
    case RESOLVED_MATCH_RECOGNIZE_PATTERN_VARIABLE_REF: {
      absl::string_view name =
          pattern->GetAs<ResolvedMatchRecognizePatternVariableRef>()->name();
      GOOGLESQL_RETURN_IF_ERROR(ValidateMatchRecognizeVariableName(name));
      VALIDATOR_RET_CHECK(available_variable_names.contains(name))
          << "Pattern variable " << name << " has no definition.";
      return absl::OkStatus();
    }
    case RESOLVED_MATCH_RECOGNIZE_PATTERN_OPERATION:
      return ValidateResolvedMatchRecognizePatternOperation(
          pattern->GetAs<ResolvedMatchRecognizePatternOperation>(),
          available_variable_names);
    case RESOLVED_MATCH_RECOGNIZE_PATTERN_QUANTIFICATION:
      return ValidateResolvedMatchRecognizePatternQuantification(
          pattern->GetAs<ResolvedMatchRecognizePatternQuantification>(),
          available_variable_names);
    default:
      VALIDATOR_RET_CHECK_FAIL()
          << "Unexpected row pattern type: " << pattern->node_kind_string();
  }
  GOOGLESQL_RET_CHECK_FAIL() << "Should have returned from the switch statement"
                   << pattern->node_kind();
}

absl::Status Validator::ValidateResolvedMatchRecognizeScan(
    const ResolvedMatchRecognizeScan* scan,
    const std::set<ResolvedColumn>& visible_parameters) {
  PushErrorContext push(this, scan);
  VALIDATOR_RET_CHECK(
      language_options_.LanguageFeatureEnabled(FEATURE_MATCH_RECOGNIZE))
      << "MATCH_RECOGNIZE is not supported";
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedScan(scan->input_scan(), visible_parameters));

  std::set<ResolvedColumn> visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(
      AddColumnList(scan->input_scan()->column_list(), &visible_columns));

  if (!scan->option_list().empty()) {
    VALIDATOR_RET_CHECK_EQ(scan->option_list_size(), 1);
    const ResolvedOption* option = scan->option_list(0);
    VALIDATOR_RET_CHECK(
        googlesql_base::CaseEqual(option->name(), "use_longest_match"))
        << "The only supported option for MATCH_RECOGNIZE is "
           "`use_longest_match`, but found: "
        << option->name();
    VALIDATOR_RET_CHECK(option->value() != nullptr);
    VALIDATOR_RET_CHECK(
        IsResolvedLiteralOrParameter(option->value()->node_kind()));
    VALIDATOR_RET_CHECK(option->value()->type()->IsBool());
    if (option->value()->Is<ResolvedLiteral>()) {
      // The value itself doesn't matter, so just access it.
      option->value()->GetAs<ResolvedLiteral>()->value();
    } else {
      GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedParameter(
          option->value()->GetAs<ResolvedParameter>()));
    }
  }

  VALIDATOR_RET_CHECK_EQ(scan->analytic_function_group_list_size(), 1);
  const ResolvedAnalyticFunctionGroup* group =
      scan->analytic_function_group_list(0);

  std::set<ResolvedColumn> visible_columns_for_def = visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedAnalyticFunctionGroup(group, visible_columns,
                                                        visible_parameters));
  for (const auto& col : group->analytic_function_list()) {
    VALIDATOR_RET_CHECK(col->expr()->Is<ResolvedAnalyticFunctionCall>());
    const FunctionSignature& signature =
        col->expr()->GetAs<ResolvedAnalyticFunctionCall>()->signature();
    VALIDATOR_RET_CHECK(signature.context_id() == FN_LEAD ||
                        signature.context_id() == FN_LAG);
    VALIDATOR_RET_CHECK(visible_columns_for_def.insert(col->column()).second);
  }

  if (scan->partition_by() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedWindowPartitioning(
        scan->partition_by(), visible_columns, visible_parameters));

    // Partitioning columns can never be duplicates nor correlated, and must
    // come from the input scan directly (not from the DML context)
    absl::flat_hash_set<ResolvedColumn> partitioning_columns_seen;
    for (const auto& partition_by_item :
         scan->partition_by()->partition_by_list()) {
      VALIDATOR_RET_CHECK(!partition_by_item->is_correlated())
          << "Partitioning column cannot be correlated: "
          << partition_by_item->DebugString();
      VALIDATOR_RET_CHECK(
          partitioning_columns_seen.insert(partition_by_item->column()).second)
          << "Duplicate partitioning column "
          << partition_by_item->DebugString();
    }
  }

  VALIDATOR_RET_CHECK(scan->order_by() != nullptr);
  VALIDATOR_RET_CHECK(!scan->order_by()->order_by_item_list().empty())
      << "ORDER BY item list cannot be empty.";

  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedWindowOrdering(
      scan->order_by(), visible_columns, visible_parameters));

  // Validate pattern variable names & predicates in the DEFINE clause.
  VALIDATOR_RET_CHECK(!scan->pattern_variable_definition_list().empty())
      << "Pattern variable definition list cannot be empty.";

  absl::flat_hash_set<absl::string_view> pattern_variable_names_defined;

  std::set<absl::string_view, googlesql_base::CaseLess>
      pattern_variable_names_defined_case_insensitive;
  for (const auto& definition : scan->pattern_variable_definition_list()) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(
        visible_columns_for_def, visible_parameters, definition->predicate()));
    absl::string_view name = definition->name();
    GOOGLESQL_RETURN_IF_ERROR(ValidateMatchRecognizeVariableName(name));
    VALIDATOR_RET_CHECK(pattern_variable_names_defined_case_insensitive
                            .insert(definition->name())
                            .second)
        << "Pattern variable names are supposed to be unique, but found "
           "duplicate name: "
        << definition->name();
    VALIDATOR_RET_CHECK(
        pattern_variable_names_defined.insert(definition->name()).second)
        << "Pattern variable names are supposed to be unique, but found "
           "duplicate name: "
        << definition->name();
  }

  // Validate the pattern expression.
  VALIDATOR_RET_CHECK(scan->pattern() != nullptr);
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedMatchRecognizePatternExpr(
      scan->pattern(), pattern_variable_names_defined));

  VALIDATOR_RET_CHECK(
      scan->after_match_skip_mode() !=
      ResolvedMatchRecognizeScanEnums::AFTER_MATCH_SKIP_MODE_UNSPECIFIED)
      << "After match skip mode must be specified";

  VALIDATOR_RET_CHECK(!match_recognize_state_.has_value())
      << "Nested MATCH_RECOGNIZE clause";

  VALIDATOR_RET_CHECK(scan->match_number_column().IsInitialized());
  VALIDATOR_RET_CHECK(scan->match_row_number_column().IsInitialized());
  VALIDATOR_RET_CHECK(scan->classifier_column().IsInitialized());

  googlesql_base::VarSetter<std::optional<MatchRecognizeState>> mr_setter(
      &match_recognize_state_,
      MatchRecognizeState{scan->match_number_column(),
                          scan->match_row_number_column(),
                          scan->classifier_column()});

  GOOGLESQL_RETURN_IF_ERROR(CheckUniqueColumnId(scan->match_number_column()));
  GOOGLESQL_RETURN_IF_ERROR(CheckUniqueColumnId(scan->match_row_number_column()));
  GOOGLESQL_RETURN_IF_ERROR(CheckUniqueColumnId(scan->classifier_column()));

  VALIDATOR_RET_CHECK(
      visible_columns.insert(scan->match_number_column()).second);
  VALIDATOR_RET_CHECK(
      visible_columns.insert(scan->match_row_number_column()).second);
  VALIDATOR_RET_CHECK(visible_columns.insert(scan->classifier_column()).second);

  absl::flat_hash_set<ResolvedColumn> output_columns_created;
  absl::flat_hash_set<absl::string_view> measure_group_var_names_seen;
  bool universal_group_seen = false;
  for (const auto& measure_group : scan->measure_group_list()) {
    VALIDATOR_RET_CHECK_GT(measure_group->aggregate_list_size(), 0);
    if (measure_group->pattern_variable_ref() == nullptr) {
      VALIDATOR_RET_CHECK(!universal_group_seen);
      universal_group_seen = true;
    } else {
      absl::string_view name = measure_group->pattern_variable_ref()->name();
      GOOGLESQL_RETURN_IF_ERROR(ValidateMatchRecognizeVariableName(name));
      VALIDATOR_RET_CHECK(pattern_variable_names_defined.contains(name))
          << "Pattern variable " << name << " has no definition.";
      VALIDATOR_RET_CHECK(measure_group_var_names_seen.insert(name).second)
          << "Pattern variable " << name << " has multiple measure groups.";
    }

    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedComputedColumnList(
        visible_columns, visible_parameters, measure_group->aggregate_list()));
    for (const auto& measure : measure_group->aggregate_list()) {
      VALIDATOR_RET_CHECK(measure->Is<ResolvedComputedColumn>())
          << "Conditional evaluation is not supported in MEASURES";
      VALIDATOR_RET_CHECK(
          output_columns_created.insert(measure->column()).second)
          << "Output column appears multiple times in MEASURES lists";
      GOOGLESQL_RETURN_IF_ERROR(CheckUniqueColumnId(measure->column()));
    }
  }

  int num_partitioning_columns =
      scan->partition_by() == nullptr
          ? 0
          : scan->partition_by()->partition_by_list_size();

  for (int i = 0; i < num_partitioning_columns; ++i) {
    VALIDATOR_RET_CHECK_EQ(
        scan->column_list(i).column_id(),
        scan->partition_by()->partition_by_list(i)->column().column_id());
  }
  // TODO: side effect columns
  int idx = num_partitioning_columns;
  for (const auto& measure_group : scan->measure_group_list()) {
    for (const auto& measure : measure_group->aggregate_list()) {
      VALIDATOR_RET_CHECK_LT(idx, scan->column_list_size());
      VALIDATOR_RET_CHECK_EQ(scan->column_list(idx).column_id(),
                             measure->column().column_id());
      idx++;
    }
  }

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedPivotScan(
    const ResolvedPivotScan* scan,
    const std::set<ResolvedColumn>& visible_parameters) {
  PushErrorContext push(this, scan);
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedScan(scan->input_scan(), visible_parameters));

  std::set<ResolvedColumn> input_column_set;
  GOOGLESQL_RETURN_IF_ERROR(
      AddColumnList(scan->input_scan()->column_list(), &input_column_set));

  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExprList(input_column_set, visible_parameters,
                                           scan->pivot_expr_list()));

  // ValidateResolvedExprList() does not cover ORDER BY or LIMIT clauses inside
  // of an aggregate function call; they must be validated separately.
  for (const auto& expr : scan->pivot_expr_list()) {
    VALIDATOR_RET_CHECK_EQ(expr->node_kind(), RESOLVED_AGGREGATE_FUNCTION_CALL);
    const ResolvedAggregateFunctionCall* aggregate_function_call =
        expr->GetAs<ResolvedAggregateFunctionCall>();
    GOOGLESQL_RETURN_IF_ERROR(ValidateOrderByAndLimitClausesOfAggregateFunctionCall(
        input_column_set, visible_parameters, aggregate_function_call));
  }

  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(input_column_set, visible_parameters,
                                       scan->for_expr()));
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExprList(/*visible_columns=*/{},
                                           /*visible_parameters=*/{},
                                           scan->pivot_value_list()));
  for (const ResolvedColumn& column : scan->column_list()) {
    GOOGLESQL_RETURN_IF_ERROR(CheckUniqueColumnId(column));
  }

  absl::flat_hash_set<ResolvedColumn> output_columns_created;

  // Validate constant pivot values
  for (const auto& pivot_value : scan->pivot_value_list()) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(input_column_set, visible_parameters,
                                         pivot_value.get()));
  }
  // Validate groupby columns
  for (const auto& column : scan->group_by_list()) {
    VALIDATOR_RET_CHECK_EQ(column->expr()->node_kind(), RESOLVED_COLUMN_REF)
        << "ComputedColumn in group-by column of pivot scan should be a "
           "ResolvedColumnRef";
    const ResolvedColumn& input_column =
        column->expr()->GetAs<ResolvedColumnRef>()->column();

    VALIDATOR_RET_CHECK(googlesql_base::ContainsKey(input_column_set, input_column))
        << "ResolvedPivotScan's groupby input column not in input column list: "
        << input_column.DebugString();

    VALIDATOR_RET_CHECK(
        googlesql_base::InsertIfNotPresent(&output_columns_created, column->column()))
        << "Output column appears multiple times in group-by/pivot lists";
  }

  // Validate pivot columns.
  absl::flat_hash_set<std::pair<int, int>> pivot_column_indexes_seen;
  VALIDATOR_RET_CHECK_LE(scan->pivot_column_list_size(),
                         scan->column_list_size());
  for (const auto& column : scan->pivot_column_list()) {
    VALIDATOR_RET_CHECK_GE(column->pivot_expr_index(), 0);
    VALIDATOR_RET_CHECK_LT(column->pivot_expr_index(),
                           scan->pivot_expr_list_size());
    GOOGLESQL_RET_CHECK_GE(column->pivot_value_index(), 0);
    VALIDATOR_RET_CHECK_LT(column->pivot_value_index(),
                           scan->pivot_value_list_size());

    VALIDATOR_RET_CHECK(
        googlesql_base::InsertIfNotPresent(&pivot_column_indexes_seen,
                                std::make_pair(column->pivot_expr_index(),
                                               column->pivot_value_index())))
        << "Multiple output columns referring to pivot expr index "
        << column->pivot_expr_index() << ", pivot value index "
        << column->pivot_value_index();

    VALIDATOR_RET_CHECK(
        googlesql_base::InsertIfNotPresent(&output_columns_created, column->column()))
        << "Output column appears multiple times in group-by/pivot lists";
  }

  // Make sure all output columns are either pivot columns or group-by columns.
  for (const ResolvedColumn& column : scan->column_list()) {
    VALIDATOR_RET_CHECK(output_columns_created.contains(column));
  }

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedUnpivotScan(
    const ResolvedUnpivotScan* scan,
    const std::set<ResolvedColumn>& visible_parameters) {
  VALIDATOR_RET_CHECK_NE(scan, nullptr);
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, scan);
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedScan(scan->input_scan(), visible_parameters));

  // Insert all columns in this set. This includes input_scan columns, unpivot
  // value columns, unpivot label columns and additional columns generated for
  // output like 'SELECT 1'.
  absl::flat_hash_set<ResolvedColumn> all_columns;
  absl::flat_hash_set<ResolvedColumn> input_source_columns;
  absl::flat_hash_set<ResolvedColumn> output_columns;
  GOOGLESQL_RETURN_IF_ERROR(
      AddColumnList(scan->input_scan()->column_list(), &all_columns));
  input_source_columns.insert(scan->input_scan()->column_list().begin(),
                              scan->input_scan()->column_list().end());
  for (auto& column : scan->column_list()) {
    // Columns from input scan are already checked for uniqueness in the
    // ValidateResolvedScan above.
    if (!all_columns.contains(column)) {
      GOOGLESQL_RETURN_IF_ERROR(CheckUniqueColumnId(column));
      all_columns.insert(column);
    }
  }
  for (auto& column : scan->value_column_list()) {
    if (!all_columns.contains(column)) {
      GOOGLESQL_RETURN_IF_ERROR(CheckUniqueColumnId(column));
      all_columns.insert(column);
    }
  }
  if (!all_columns.contains(scan->label_column())) {
    GOOGLESQL_RETURN_IF_ERROR(CheckUniqueColumnId(scan->label_column()));
    all_columns.insert(scan->label_column());
  }

  for (auto& column : scan->column_list()) {
    VALIDATOR_RET_CHECK(all_columns.contains(column))
        << "The output columns must be a subset of input columns and the newly "
           "generated unpivot columns.";
    output_columns.insert(column);
  }

  absl::flat_hash_set<ResolvedColumn> input_columns_in_output;
  for (const std::unique_ptr<const ResolvedComputedColumn>& column :
       scan->projected_input_column_list()) {
    GOOGLESQL_RET_CHECK_EQ(column->expr()->node_kind(), RESOLVED_COLUMN_REF)
        << "ComputedColumn in projected input columns of unpivot scan should be"
           "a ResolvedColumnRef";
    const ResolvedColumn& input_column =
        column->expr()->GetAs<ResolvedColumnRef>()->column();
    input_columns_in_output.insert(input_column);
    const ResolvedColumn col = column->column();
    GOOGLESQL_RET_CHECK(col.column_id() > 0);
    GOOGLESQL_RET_CHECK(input_source_columns.contains(input_column))
        << "The expressions for the columns in the projected_input_column_list "
           "in unpivot output should be a reference to columns in the "
           "input_scan";
  }

  // Check that the order of columns in the output is input columns in order,
  // value column in order, and label column. Not all input columns are present
  // in the output and hence we aren't directly matching the list here.
  std::vector<ResolvedColumn> all_columns_list;
  for (const std::unique_ptr<const ResolvedComputedColumn>& input_col :
       scan->projected_input_column_list()) {
    all_columns_list.push_back(input_col->column());
  }
  all_columns_list.insert(std::end(all_columns_list),
                          std::begin(scan->value_column_list()),
                          std::end(scan->value_column_list()));
  all_columns_list.push_back(scan->label_column());
  int ordered_column_position = 0;
  for (auto& column : scan->column_list()) {
    while (ordered_column_position < all_columns_list.size() &&
           column.column_id() !=
               all_columns_list.at(ordered_column_position).column_id()) {
      ordered_column_position++;
    }
    VALIDATOR_RET_CHECK_LT(ordered_column_position, all_columns_list.size())
        << "The output columns must be in the order of input columns(in the "
           "order that they appear in input_scan), value columns (in the "
           "order that they are defined in UNPIVOT) and then label column.";
  }

  absl::flat_hash_set<ResolvedColumn> input_columns_seen;
  for (const auto& input_column_set : scan->unpivot_arg_list()) {
    VALIDATOR_RET_CHECK_EQ(scan->value_column_list().size(),
                           input_column_set->column_list_size())
        << "Each column group should be of the same size as value_column_list";
    for (const auto& input_column : input_column_set->column_list()) {
      ResolvedColumn input_column_res = input_column->column();
      VALIDATOR_RET_CHECK_EQ(input_column->node_kind(), RESOLVED_COLUMN_REF)
          << "Column in unpivot_input_column_list of unpivot scan should be a "
             "ResolvedColumnRef";
      VALIDATOR_RET_CHECK(
          googlesql_base::InsertIfNotPresent(&input_columns_seen, input_column_res))
          << "Duplicate input column " << input_column_res.DebugString()
          << " in ResolvedUnpivotScan's input_column_list";
      GOOGLESQL_RET_CHECK(!input_columns_in_output.contains(input_column_res))
          << "Columns from the unpivot_arg_list (inside UNPIVOT IN clause) are "
             "not present in the unpivot output";
    }
  }

  VALIDATOR_RET_CHECK_EQ(scan->label_list_size(),
                         scan->unpivot_arg_list_size());
  for (const auto& val : scan->label_list()) {
    VALIDATOR_RET_CHECK_EQ(val->value().type(), scan->label_column().type());
  }
  // Simply visit this boolean value node for validation purposes.
  scan->include_nulls();

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedDescribeScan(
    const ResolvedDescribeScan* scan,
    const std::set<ResolvedColumn>& visible_parameters) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, scan);

  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedScan(scan->input_scan(), visible_parameters));

  // No input columns are visible when computing the expression.
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedComputedColumn(
      /*visible_columns=*/{}, /*visible_parameters=*/{},
      scan->describe_expr()));

  VALIDATOR_RET_CHECK(scan->describe_expr()->column().type()->IsString());
  VALIDATOR_RET_CHECK_EQ(scan->describe_expr()->expr()->node_kind(),
                         RESOLVED_LITERAL);

  std::set<ResolvedColumn> visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(
      AddColumnFromComputedColumn(scan->describe_expr(), &visible_columns));
  GOOGLESQL_RETURN_IF_ERROR(CheckColumnList(scan, visible_columns));

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedStaticDescribeScan(
    const ResolvedStaticDescribeScan* scan,
    const std::set<ResolvedColumn>& visible_parameters) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, scan);

  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedScan(scan->input_scan(), visible_parameters));

  std::set<ResolvedColumn> visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(
      AddColumnList(scan->input_scan()->column_list(), &visible_columns));

  GOOGLESQL_RETURN_IF_ERROR(CheckColumnList(scan, visible_columns));

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedAssertScan(
    const ResolvedAssertScan* scan,
    const std::set<ResolvedColumn>& visible_parameters) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, scan);

  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedScan(scan->input_scan(), visible_parameters));

  std::set<ResolvedColumn> visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(
      AddColumnList(scan->input_scan()->column_list(), &visible_columns));

  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(visible_columns, visible_parameters,
                                       scan->condition()));
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(visible_columns, visible_parameters,
                                       scan->message()));

  VALIDATOR_RET_CHECK(scan->condition()->type()->IsBool());
  VALIDATOR_RET_CHECK(scan->message()->type()->IsString());

  GOOGLESQL_RETURN_IF_ERROR(CheckColumnList(scan, visible_columns));

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedLogScan(
    const ResolvedLogScan* scan,
    const std::set<ResolvedColumn>& visible_parameters) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, scan);

  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedScan(scan->input_scan(), visible_parameters));

  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedSubpipeline(
      scan->subpipeline(), scan->input_scan()->column_list(),
      scan->input_scan()->is_ordered(), visible_parameters));

  VALIDATOR_RET_CHECK(scan->output_schema() != nullptr);
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedOutputSchema(
      scan->subpipeline()->scan()->column_list(), scan->output_schema()));

  std::set<ResolvedColumn> visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(
      AddColumnList(scan->input_scan()->column_list(), &visible_columns));

  GOOGLESQL_RETURN_IF_ERROR(CheckColumnList(scan, visible_columns));

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedPipeIfScan(
    const ResolvedPipeIfScan* scan,
    const std::set<ResolvedColumn>& visible_parameters) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, scan);

  VALIDATOR_RET_CHECK(nullptr != scan->input_scan());
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedScan(scan->input_scan(), visible_parameters));

  std::set<ResolvedColumn> visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(
      AddColumnList(scan->input_scan()->column_list(), &visible_columns));

  VALIDATOR_RET_CHECK_GE(scan->if_case_list().size(), 1);
  size_t num_cases = scan->if_case_list().size();

  if (scan->selected_case() != -1) {
    VALIDATOR_RET_CHECK_GE(scan->selected_case(), 0);
    VALIDATOR_RET_CHECK_LT(scan->selected_case(), num_cases);
  }

  std::set<ResolvedColumn> output_visible_columns;

  for (int i = 0; i < num_cases; ++i) {
    const ResolvedPipeIfCase* if_case = scan->if_case_list(i);
    bool is_selected_case = (i == scan->selected_case());

    if (!if_case->IsElse()) {
      GOOGLESQL_RETURN_IF_ERROR(ValidateBoolExpr(visible_columns, visible_parameters,
                                       if_case->condition()));
    } else {
      VALIDATOR_RET_CHECK_EQ(i, num_cases - 1);
      VALIDATOR_RET_CHECK_GE(num_cases, 2);
    }

    if (if_case->subpipeline() != nullptr) {
      GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedSubpipeline(
          if_case->subpipeline(), scan->input_scan()->column_list(),
          scan->input_scan()->is_ordered(), visible_parameters));
    }

    if (is_selected_case) {
      VALIDATOR_RET_CHECK(if_case->subpipeline() != nullptr);

      GOOGLESQL_RETURN_IF_ERROR(
          AddColumnList(if_case->subpipeline()->scan()->column_list(),
                        &output_visible_columns));
    }

    // `subpipeline_sql` is required for all cases, even the selected case,
    // since the SQLBuilder currently relies on it.
    VALIDATOR_RET_CHECK(!if_case->subpipeline_sql().empty());
  }

  if (scan->selected_case() == -1) {
    GOOGLESQL_RETURN_IF_ERROR(AddColumnList(scan->input_scan()->column_list(),
                                  &output_visible_columns));
  }

  GOOGLESQL_RETURN_IF_ERROR(CheckColumnList(scan, output_visible_columns));

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedPipeForkScan(
    const ResolvedPipeForkScan* scan,
    const std::set<ResolvedColumn>& visible_parameters) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, scan);

  // FORK can only occur in GeneralizedQueryStmts.
  VALIDATOR_RET_CHECK(in_generalized_query_stmt_);

  VALIDATOR_RET_CHECK(scan->input_scan() != nullptr);
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedScan(scan->input_scan(), visible_parameters));

  std::set<ResolvedColumn> visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(
      AddColumnList(scan->input_scan()->column_list(), &visible_columns));

  VALIDATOR_RET_CHECK_GE(scan->subpipeline_list().size(), 1);

  for (const auto& subpipeline : scan->subpipeline_list()) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedGeneralizedQuerySubpipeline(
        subpipeline.get(), scan->input_scan(), visible_parameters));
  }

  VALIDATOR_RET_CHECK(scan->column_list().empty());

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedPipeTeeScan(
    const ResolvedPipeTeeScan* scan,
    const std::set<ResolvedColumn>& visible_parameters) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, scan);

  // TEE can only occur in GeneralizedQueryStmts.
  VALIDATOR_RET_CHECK(in_generalized_query_stmt_);

  VALIDATOR_RET_CHECK(scan->input_scan() != nullptr);
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedScan(scan->input_scan(), visible_parameters));

  std::set<ResolvedColumn> visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(
      AddColumnList(scan->input_scan()->column_list(), &visible_columns));

  VALIDATOR_RET_CHECK_GE(scan->subpipeline_list().size(), 1);

  for (const auto& subpipeline : scan->subpipeline_list()) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedGeneralizedQuerySubpipeline(
        subpipeline.get(), scan->input_scan(), visible_parameters));
  }

  GOOGLESQL_RETURN_IF_ERROR(CheckColumnList(scan, visible_columns));

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedPipeExportDataScan(
    const ResolvedPipeExportDataScan* scan,
    const std::set<ResolvedColumn>& visible_parameters) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, scan);

  // EXPORT DATA can only occur in GeneralizedQueryStmts.
  VALIDATOR_RET_CHECK(in_generalized_query_stmt_);

  VALIDATOR_RET_CHECK(scan->export_data_stmt()->query() != nullptr);

  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExportDataStmt(scan->export_data_stmt(),
                                                 visible_parameters));

  VALIDATOR_RET_CHECK(scan->column_list().empty());

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedPipeCreateTableScan(
    const ResolvedPipeCreateTableScan* scan,
    const std::set<ResolvedColumn>& visible_parameters) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, scan);

  VALIDATOR_RET_CHECK(nullptr != scan->create_table_as_select_stmt());
  VALIDATOR_RET_CHECK(nullptr != scan->create_table_as_select_stmt()->query());
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedCreateTableAsSelectStmt(
      scan->create_table_as_select_stmt(), visible_parameters));

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedPipeInsertScan(
    const ResolvedPipeInsertScan* scan,
    const std::set<ResolvedColumn>& visible_parameters) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, scan);

  // INSERT can only occur in GeneralizedQueryStmts.
  VALIDATOR_RET_CHECK(in_generalized_query_stmt_);

  VALIDATOR_RET_CHECK(scan->insert_stmt()->query() != nullptr);

  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedInsertStmt(scan->insert_stmt(),
                                             /*outer_visible_columns=*/nullptr,
                                             /*array_element_column=*/nullptr,
                                             visible_parameters));

  VALIDATOR_RET_CHECK(scan->column_list().empty());

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedSubpipeline(
    const ResolvedSubpipeline* subpipeline,
    absl::Span<const ResolvedColumn> visible_columns, bool input_is_ordered,
    const std::set<ResolvedColumn>& visible_parameters) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, subpipeline);

  subpipeline_info_stack_.emplace_back(visible_columns, input_is_ordered);
  const int stack_size = subpipeline_info_stack_.size();

  GOOGLESQL_RETURN_IF_ERROR(
      ValidateResolvedScan(subpipeline->scan(), visible_parameters));

  VALIDATOR_RET_CHECK_EQ(stack_size, subpipeline_info_stack_.size());
  VALIDATOR_RET_CHECK(subpipeline_info_stack_.back().saw_subpipeline_input_scan)
      << "ResolvedSubpipeline does not contain a ResolvedSubpipelineInputScan";
  subpipeline_info_stack_.pop_back();

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedSubpipelineStmt(
    const ResolvedSubpipelineStmt* subpipeline_stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, subpipeline_stmt);

  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedScan(subpipeline_stmt->table_scan(),
                                       /*visible_parameters=*/{}));

  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedSubpipeline(
      subpipeline_stmt->subpipeline(),
      subpipeline_stmt->table_scan()->column_list(),
      /*input_is_ordered=*/false, /*visible_parameters=*/{}));

  if (subpipeline_stmt->output_schema() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedOutputSchema(
        subpipeline_stmt->subpipeline()->scan()->column_list(),
        subpipeline_stmt->output_schema()));
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedStatementWithPipeOperatorsStmt(
    const ResolvedStatementWithPipeOperatorsStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);

  switch (stmt->statement()->node_kind()) {
    // These statements support pipe operator suffixes in the parser, in the
    // `sql_statement_body_maybe_pipe_suffix` production.  Keep this in sync.
    case RESOLVED_CALL_STMT:
    case RESOLVED_DESCRIBE_STMT:
    case RESOLVED_EXECUTE_IMMEDIATE_STMT:
    // This is parsed but doesn't exist in the Resolved AST yet.
    // case RESOLVED_RUN_STMT:
    case RESOLVED_SHOW_STMT:
      break;
    default:
      VALIDATOR_RET_CHECK_FAIL() << "Statement not supported in "
                                    "ResolvedStatementWithPipeOperatorsStmt: "
                                 << stmt->statement()->node_kind_string();
  }

  VALIDATOR_RET_CHECK(stmt->suffix_subpipeline_sql() != nullptr);
  VALIDATOR_RET_CHECK(!stmt->suffix_subpipeline_sql()->value().empty());
  return ValidateResolvedStatementInternal(stmt->statement());
}

absl::Status Validator::ValidateResolvedSubpipelineInputScan(
    const ResolvedSubpipelineInputScan* scan,
    const std::set<ResolvedColumn>& visible_parameters) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, scan);

  VALIDATOR_RET_CHECK_GE(subpipeline_info_stack_.size(), 1);
  VALIDATOR_RET_CHECK(
      !subpipeline_info_stack_.back().saw_subpipeline_input_scan)
      << "ResolvedSubpipeline contains multiple ResolvedSubpipelineInputScans";

  subpipeline_info_stack_.back().saw_subpipeline_input_scan = true;

  std::set<ResolvedColumn> visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(AddColumnList(subpipeline_info_stack_.back().column_list,
                                &visible_columns));

  GOOGLESQL_RETURN_IF_ERROR(CheckColumnList(scan, visible_columns));

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedAuxLoadDataPartitionFilter(
    const std::set<ResolvedColumn>& visible_columns,
    const ResolvedAuxLoadDataPartitionFilter* partition_filter) {
  VALIDATOR_RET_CHECK(partition_filter->filter()->type()->IsBool())
      << "PARTITIONS expects a boolean expression";
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(visible_columns,
                                       /*visible_parameters=*/{},
                                       partition_filter->filter()));
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedAuxLoadDataStmt(
    const ResolvedAuxLoadDataStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);
  GOOGLESQL_RETURN_IF_ERROR(ValidateOptionsList(stmt->option_list()));
  std::set<ResolvedColumn> visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(ValidateColumnDefinitions(stmt->column_definition_list(),
                                            &visible_columns));
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedWithPartitionColumns(
      stmt->with_partition_columns(), &visible_columns));

  // Add the output columns in case the table schema is empty.
  for (const auto& c : stmt->output_column_list()) {
    visible_columns.insert(c->column());
  }

  // Pseudo columns shouldn't be in output_column_list. But they are still
  // visible for CLUSTER BY and PARTITION BY.
  for (const auto& pseudo_column : stmt->pseudo_column_list()) {
    if (!googlesql_base::InsertIfNotPresent(&visible_columns, pseudo_column)) {
      return InternalErrorBuilder()
             << "Column already used: " << pseudo_column.DebugString();
    }
  }
  if (stmt->partition_filter() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedAuxLoadDataPartitionFilter(
        visible_columns, stmt->partition_filter()));
  }
  for (const auto& partition_by_expr : stmt->partition_by_list()) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(
        visible_columns, /*visible_parameters=*/{}, partition_by_expr.get()));
  }
  for (const auto& cluster_by_expr : stmt->cluster_by_list()) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(
        visible_columns, /*visible_parameters=*/{}, cluster_by_expr.get()));
  }
  return ValidateOptionsList(stmt->from_files_option_list());
}

std::string Validator::RecordContext() {
  if (!context_stack_.empty()) {
    error_context_ = context_stack_.back();
  }
  return "";
}

absl::Status Validator::ValidateBoolExpr(
    const std::set<ResolvedColumn>& visible_columns,
    const std::set<ResolvedColumn>& visible_parameters,
    const ResolvedExpr* expr) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  GOOGLESQL_RETURN_IF_ERROR(
      ValidateResolvedExpr(visible_columns, visible_parameters, expr));
  VALIDATOR_RET_CHECK(expr->type()->IsBool())
      << "Expects BOOL found: " << expr->type()->DebugString();
  return absl::OkStatus();
}

template <typename T>
using CaseInsensitiveMap =
    absl::flat_hash_map<absl::string_view, T, googlesql_base::StringViewCaseHash,
                        googlesql_base::StringViewCaseEqual>;
using CaseInsensitiveSet =
    absl::flat_hash_set<absl::string_view, googlesql_base::StringViewCaseHash,
                        googlesql_base::StringViewCaseEqual>;

absl::Status Validator::ValidateResolvedCreatePropertyGraphStmt(
    const ResolvedCreatePropertyGraphStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);
  ABSL_CHECK(stmt != nullptr);  // Crash OK
  VALIDATOR_RET_CHECK(!stmt->name_path().empty());
  GOOGLESQL_RETURN_IF_ERROR(ValidateOptionsList(stmt->option_list()));

  CaseInsensitiveMap<AnnotatedType> property_dcl_name_map;
  for (const std::unique_ptr<const ResolvedGraphPropertyDeclaration>&
           property_dcl : stmt->property_declaration_list()) {
    VALIDATOR_RET_CHECK(!property_dcl->name().empty());
    ABSL_CHECK(property_dcl->type() != nullptr);  // Crash OK
    VALIDATOR_RET_CHECK(
        property_dcl_name_map
            .insert({property_dcl->name(),
                     AnnotatedType(property_dcl->type(),
                                   property_dcl->type_annotation_map())})
            .second);
  }

  CaseInsensitiveSet label_name_set;
  for (const std::unique_ptr<const ResolvedGraphElementLabel>& label :
       stmt->label_list()) {
    VALIDATOR_RET_CHECK(!label->name().empty());
    VALIDATOR_RET_CHECK(label_name_set.insert(label->name()).second);
    GOOGLESQL_RETURN_IF_ERROR(
        ValidateResolvedGraphElementLabel(label.get(), property_dcl_name_map));
  }

  CaseInsensitiveMap<const ResolvedScan*> node_table_scan_map;
  CaseInsensitiveSet element_table_name_set;
  for (const std::unique_ptr<const ResolvedGraphElementTable>& node_table :
       stmt->node_table_list()) {
    VALIDATOR_RET_CHECK(!node_table->alias().empty());
    VALIDATOR_RET_CHECK(
        element_table_name_set.insert(node_table->alias()).second);
    node_table_scan_map.emplace(node_table->alias(), node_table->input_scan());
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedGraphElementTable(
        node_table.get(),
        /*node_table_scan_map=*/{}, label_name_set, property_dcl_name_map));
  }
  for (const std::unique_ptr<const ResolvedGraphElementTable>& edge_table :
       stmt->edge_table_list()) {
    VALIDATOR_RET_CHECK(!edge_table->alias().empty());
    VALIDATOR_RET_CHECK(
        element_table_name_set.insert(edge_table->alias()).second);
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedGraphElementTable(
        edge_table.get(), node_table_scan_map, label_name_set,
        property_dcl_name_map));
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedGraphElementTable(
    const ResolvedGraphElementTable* element_table,
    const CaseInsensitiveMap<const ResolvedScan*>& node_table_scan_map,
    const CaseInsensitiveSet& all_label_name_set,
    const CaseInsensitiveMap<AnnotatedType>& all_property_name_set) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, element_table);
  VALIDATOR_RET_CHECK(!element_table->alias().empty());
  ABSL_CHECK(element_table->input_scan() != nullptr);  // Crash OK
  VALIDATOR_RET_CHECK(!element_table->key_list().empty());

  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedScan(element_table->input_scan(),
                                       /*visible_parameters=*/{}));
  std::set<ResolvedColumn> visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(AddColumnList(element_table->input_scan()->column_list(),
                                &visible_columns));

  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExprList(
      visible_columns, /*visible_parameters=*/{}, element_table->key_list()));

  // Node table doesn't have source/dest reference
  // Edge table need to validate both source&dest references
  bool is_node_table = false;
  if (element_table->source_node_reference() == nullptr) {
    VALIDATOR_RET_CHECK_EQ(element_table->dest_node_reference(), nullptr);
    is_node_table = true;
  } else {
    VALIDATOR_RET_CHECK_NE(element_table->dest_node_reference(), nullptr);

    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedGraphNodeTableReference(
        element_table->source_node_reference(), visible_columns,
        node_table_scan_map));
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedGraphNodeTableReference(
        element_table->dest_node_reference(), visible_columns,
        node_table_scan_map));
  }

  VALIDATOR_RET_CHECK(!element_table->label_name_list().empty());
  CaseInsensitiveSet label_name_set;
  for (const std::string& label_name : element_table->label_name_list()) {
    VALIDATOR_RET_CHECK(!label_name.empty());
    VALIDATOR_RET_CHECK(label_name_set.insert(label_name).second);
    VALIDATOR_RET_CHECK(all_label_name_set.find(label_name) !=
                        all_label_name_set.end());
  }

  for (const std::unique_ptr<const ResolvedGraphPropertyDefinition>&
           property_definition : element_table->property_definition_list()) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedGraphPropertyDefinition(
        property_definition.get(), visible_columns, all_property_name_set));
  }

  if (language_options_.LanguageFeatureEnabled(
          FEATURE_SQL_GRAPH_DYNAMIC_LABEL_PROPERTIES_IN_DDL) &&
      element_table->dynamic_label() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedGraphDynamicLabelSpecification(
        element_table->dynamic_label(), is_node_table, visible_columns));
  }
  if (language_options_.LanguageFeatureEnabled(
          FEATURE_SQL_GRAPH_DYNAMIC_LABEL_PROPERTIES_IN_DDL) &&
      element_table->dynamic_properties() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedGraphDynamicPropertiesSpecification(
        element_table->dynamic_properties(), visible_columns));
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedGraphNodeTableReference(
    const ResolvedGraphNodeTableReference* node_reference,
    const std::set<ResolvedColumn>& edge_visible_columns,
    const CaseInsensitiveMap<const ResolvedScan*>& node_table_scan_map) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, node_reference);

  auto found =
      node_table_scan_map.find(node_reference->node_table_identifier());
  VALIDATOR_RET_CHECK(found != node_table_scan_map.end())
      << "Failed to validate node reference "
      << node_reference->node_table_identifier();

  VALIDATOR_RET_CHECK(!node_reference->node_table_column_list().empty())
      << "Node reference " << node_reference->node_table_identifier()
      << " has no column list";
  VALIDATOR_RET_CHECK(!node_reference->edge_table_column_list().empty())
      << "Node reference " << node_reference->node_table_identifier()
      << " has no edge column list";

  std::set<ResolvedColumn> node_visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(
      AddColumnList(found->second->column_list(), &node_visible_columns));

  for (const std::unique_ptr<const ResolvedExpr>& column_expr :
       node_reference->node_table_column_list()) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(
        node_visible_columns, /*visible_parameters=*/{}, column_expr.get()));
  }

  for (const std::unique_ptr<const ResolvedExpr>& column_expr :
       node_reference->edge_table_column_list()) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(
        edge_visible_columns, /*visible_parameters=*/{}, column_expr.get()));
  }

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedGraphElementLabel(
    const ResolvedGraphElementLabel* label,
    const CaseInsensitiveMap<AnnotatedType>& property_dcl_name_map) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, label);

  VALIDATOR_RET_CHECK(!label->name().empty());
  if (!label->options_list().empty()) {
    VALIDATOR_RET_CHECK(language_options_.LanguageFeatureEnabled(
        FEATURE_SQL_GRAPH_DEFAULT_LABEL_AND_PROPERTY_DEFINITION_OPTIONS))
        << "Found options list in label, but "
           "SQL_GRAPH_DEFAULT_LABEL_AND_PROPERTY_DEFINITION_OPTIONS is not "
           "enabled";
    GOOGLESQL_RETURN_IF_ERROR(ValidateOptionsList(label->options_list()));
  }

  for (absl::string_view property_name :
       label->property_declaration_name_list()) {
    VALIDATOR_RET_CHECK(property_dcl_name_map.find(property_name) !=
                        property_dcl_name_map.end());
  }

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedGraphPropertyDefinition(
    const ResolvedGraphPropertyDefinition* property_definition,
    const std::set<ResolvedColumn>& visible_columns,
    const CaseInsensitiveMap<AnnotatedType>& all_property_name_set) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, property_definition);
  VALIDATOR_RET_CHECK(
      !property_definition->property_declaration_name().empty());
  VALIDATOR_RET_CHECK(!property_definition->sql().empty());

  auto it = all_property_name_set.find(
      property_definition->property_declaration_name());
  VALIDATOR_RET_CHECK(it != all_property_name_set.end());
  VALIDATOR_RET_CHECK(
      property_definition->expr()->type()->Equals(it->second.type))
      << property_definition->DebugString() << " does not match declared type "
      << it->second.type->TypeName(language_options_.product_mode());
  VALIDATOR_RET_CHECK(
      AnnotationMap::Equals(property_definition->expr()->type_annotation_map(),
                            it->second.annotation_map))
      << " property definition annotations: "
      << (property_definition->expr()->type_annotation_map() == nullptr
              ? "<nullptr>"
              : property_definition->expr()
                    ->type_annotation_map()
                    ->DebugString())
      << ", declared annotations: "
      << (it->second.annotation_map == nullptr
              ? "<nullptr>"
              : it->second.annotation_map->DebugString());

  if (!property_definition->options_list().empty()) {
    VALIDATOR_RET_CHECK(language_options_.LanguageFeatureEnabled(
        FEATURE_SQL_GRAPH_DEFAULT_LABEL_AND_PROPERTY_DEFINITION_OPTIONS))
        << "Found options list in property definition, but "
           "SQL_GRAPH_DEFAULT_LABEL_AND_PROPERTY_DEFINITION_OPTIONS is "
           "not enabled";
    GOOGLESQL_RETURN_IF_ERROR(ValidateOptionsList(property_definition->options_list()));
  }

  // TODO: Implementation validation when is_measure is true.

  return ValidateResolvedExpr(visible_columns, /*visible_parameters=*/{},
                              property_definition->expr());
}

bool IsStringArray(const Type* type) {
  return type->IsArray() && type->AsArray()->element_type()->IsString();
}

absl::Status Validator::ValidateResolvedGraphDynamicLabelSpecification(
    const ResolvedGraphDynamicLabelSpecification* label_specification,
    bool is_node_table, const std::set<ResolvedColumn>& visible_columns) {
  VALIDATOR_RET_CHECK_NE(label_specification->label_expr(), nullptr);
  if (is_node_table) {
    VALIDATOR_RET_CHECK(
        label_specification->label_expr()->type()->IsString() ||
        IsStringArray(label_specification->label_expr()->type()));
  } else {
    VALIDATOR_RET_CHECK(label_specification->label_expr()->type()->IsString());
  }
  // A dynamic label can only be a column-ref or a catalog-column-ref.
  VALIDATOR_RET_CHECK(
      label_specification->label_expr()->Is<ResolvedColumnRef>() ||
      label_specification->label_expr()->Is<ResolvedCatalogColumnRef>());
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(visible_columns,
                                       /*visible_parameters=*/{},
                                       label_specification->label_expr()));
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedGraphDynamicPropertiesSpecification(
    const ResolvedGraphDynamicPropertiesSpecification* property_specification,
    const std::set<ResolvedColumn>& visible_columns) {
  VALIDATOR_RET_CHECK_NE(property_specification->property_expr(), nullptr);
  VALIDATOR_RET_CHECK(
      property_specification->property_expr()->type()->IsJson());
  // A dynamic property can only be a column-ref or a catalog-column-ref.
  VALIDATOR_RET_CHECK(
      property_specification->property_expr()->Is<ResolvedColumnRef>() ||
      property_specification->property_expr()->Is<ResolvedCatalogColumnRef>());
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(
      visible_columns,
      /*visible_parameters=*/{}, property_specification->property_expr()));
  return absl::OkStatus();
}

absl::Status Validator::ValidateGraphReturnOperator(const ResolvedScan* scan) {
  VALIDATOR_RET_CHECK_NE(scan, nullptr);

  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, scan);

  const ResolvedScan* curr_scan = scan;
  VALIDATOR_RET_CHECK(curr_scan->Is<ResolvedProjectScan>() ||
                      curr_scan->Is<ResolvedAggregateScan>() ||
                      curr_scan->Is<ResolvedOrderByScan>() ||
                      curr_scan->Is<ResolvedLimitOffsetScan>());
  // Check that the resolved RETURN consists of nested ResolvedAggregateScan
  // and ResolvedProjectScan, the innermost scan should contain an
  // input scan that is either a GraphRefScan or SingleRowScan.
  while (curr_scan->Is<ResolvedAggregateScan>() ||
         curr_scan->Is<ResolvedProjectScan>()) {
    if (curr_scan->Is<ResolvedAggregateScan>()) {
      curr_scan = curr_scan->GetAs<ResolvedAggregateScan>()->input_scan();
    } else if (curr_scan->Is<ResolvedProjectScan>()) {
      curr_scan = curr_scan->GetAs<ResolvedProjectScan>()->input_scan();
    }
  }
  VALIDATOR_RET_CHECK_NE(curr_scan, nullptr);
  VALIDATOR_RET_CHECK(curr_scan->Is<ResolvedSingleRowScan>() ||
                      curr_scan->Is<ResolvedGraphRefScan>() ||
                      curr_scan->Is<ResolvedOrderByScan>() ||
                      curr_scan->Is<ResolvedLimitOffsetScan>() ||
                      curr_scan->Is<ResolvedAnalyticScan>());
  return absl::OkStatus();
}

absl::Status Validator::ValidateGraphSetOperationScanStructure(
    const ResolvedSetOperationScan* scan) {
  VALIDATOR_RET_CHECK_EQ(scan->column_match_mode(),
                         ResolvedSetOperationScan::CORRESPONDING);
  if (!language_options_.LanguageFeatureEnabled(
          FEATURE_SQL_GRAPH_SET_OPERATION_PROPAGATION_MODE)) {
    VALIDATOR_RET_CHECK_EQ(scan->column_propagation_mode(),
                           ResolvedSetOperationScan::STRICT);
  }
  VALIDATOR_RET_CHECK_GT(scan->input_item_list_size(), 0);
  for (const auto& item : scan->input_item_list()) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateInnerGraphLinearScanStructure(
        item->scan()->GetAs<ResolvedGraphLinearScan>()));
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateInnerGraphLinearScanStructure(
    const ResolvedGraphLinearScan* scan) {
  const std::vector<std::unique_ptr<const ResolvedScan>>& primitive_ops =
      scan->scan_list();
  VALIDATOR_RET_CHECK(!primitive_ops.empty());
  for (int i = 0; i < primitive_ops.size(); ++i) {
    // Check that all primitive ops has a non-empty input scan.
    const ResolvedScan* input_scan = nullptr;
    if (i == primitive_ops.size() - 1) {
      GOOGLESQL_RETURN_IF_ERROR(ValidateGraphReturnOperator(primitive_ops[i].get()));
      switch (primitive_ops[i]->node_kind()) {
        case RESOLVED_AGGREGATE_SCAN:
          input_scan =
              primitive_ops[i]->GetAs<ResolvedAggregateScan>()->input_scan();
          break;
        case RESOLVED_PROJECT_SCAN:
          input_scan =
              primitive_ops[i]->GetAs<ResolvedProjectScan>()->input_scan();
          break;
        case RESOLVED_ORDER_BY_SCAN:
          input_scan =
              primitive_ops[i]->GetAs<ResolvedOrderByScan>()->input_scan();
          break;
        case RESOLVED_LIMIT_OFFSET_SCAN:
          input_scan =
              primitive_ops[i]->GetAs<ResolvedLimitOffsetScan>()->input_scan();
          break;
        default:
          VALIDATOR_RET_CHECK_FAIL();
          break;
      }
    } else {
      switch (primitive_ops[i]->node_kind()) {
        case RESOLVED_GRAPH_SCAN:
          input_scan =
              primitive_ops[i]->GetAs<ResolvedGraphScan>()->input_scan();
          break;
        case RESOLVED_FILTER_SCAN:
          input_scan =
              primitive_ops[i]->GetAs<ResolvedFilterScan>()->input_scan();
          break;
        case RESOLVED_AGGREGATE_SCAN:
          input_scan =
              primitive_ops[i]->GetAs<ResolvedAggregateScan>()->input_scan();
          break;
        case RESOLVED_ANALYTIC_SCAN:
          input_scan =
              primitive_ops[i]->GetAs<ResolvedAnalyticScan>()->input_scan();
          break;
        case RESOLVED_PROJECT_SCAN:
          input_scan =
              primitive_ops[i]->GetAs<ResolvedProjectScan>()->input_scan();
          break;
        case RESOLVED_ORDER_BY_SCAN:
          input_scan =
              primitive_ops[i]->GetAs<ResolvedOrderByScan>()->input_scan();
          break;
        case RESOLVED_LIMIT_OFFSET_SCAN:
          input_scan =
              primitive_ops[i]->GetAs<ResolvedLimitOffsetScan>()->input_scan();
          break;
        case RESOLVED_ARRAY_SCAN:
          input_scan =
              primitive_ops[i]->GetAs<ResolvedArrayScan>()->input_scan();
          break;
        case RESOLVED_GRAPH_CALL_SCAN: {
          const ResolvedGraphCallScan* call_scan =
              primitive_ops[i]->GetAs<ResolvedGraphCallScan>();
          VALIDATOR_RET_CHECK(
              call_scan->input_scan()->Is<ResolvedGraphRefScan>() ||
              call_scan->input_scan()->Is<ResolvedSingleRowScan>());
          input_scan = call_scan->subquery();
          break;
        }
        case RESOLVED_SAMPLE_SCAN:
          input_scan =
              primitive_ops[i]->GetAs<ResolvedSampleScan>()->input_scan();
          break;
        case RESOLVED_TVFSCAN:
          // The first table arg must be the GraphRefScan.
          for (const auto& arg :
               primitive_ops[i]->GetAs<ResolvedTVFScan>()->argument_list()) {
            if (arg->scan() != nullptr) {
              input_scan = arg->scan();
            }
          }
          break;
        default:
          VALIDATOR_RET_CHECK_FAIL();
          break;
      }
    }
    VALIDATOR_RET_CHECK_NE(input_scan, nullptr);
  }
  return absl::OkStatus();
}

static bool IsGraphCompositeQuery(const ResolvedScan* scan) {
  return scan->Is<ResolvedGraphLinearScan>() ||
         scan->Is<ResolvedSetOperationScan>();
}

absl::Status Validator::ValidateTopLevelGraphLinearScanStructure(
    const ResolvedGraphLinearScan* scan) {
  const std::vector<std::unique_ptr<const ResolvedScan>>& composite_queries =
      scan->scan_list();
  VALIDATOR_RET_CHECK(!composite_queries.empty());
  for (const auto& composite_query : composite_queries) {
    VALIDATOR_RET_CHECK(IsGraphCompositeQuery(composite_query.get()));

    if (composite_query->Is<ResolvedSetOperationScan>()) {
      GOOGLESQL_RETURN_IF_ERROR(ValidateGraphSetOperationScanStructure(
          composite_query->GetAs<ResolvedSetOperationScan>()));
    } else {
      GOOGLESQL_RETURN_IF_ERROR(ValidateInnerGraphLinearScanStructure(
          composite_query->GetAs<ResolvedGraphLinearScan>()));
    }
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedGraphTableScan(
    const ResolvedGraphTableScan* scan,
    const std::set<ResolvedColumn>& visible_parameters) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, scan);
  VALIDATOR_RET_CHECK(scan->property_graph() != nullptr);
  VALIDATOR_RET_CHECK(scan->input_scan() != nullptr);
  if (scan->input_scan()->Is<ResolvedGraphLinearScan>()) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateTopLevelGraphLinearScanStructure(
        scan->input_scan()->GetAs<ResolvedGraphLinearScan>()));
  }
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedScan(scan->input_scan(), visible_parameters));
  std::set<ResolvedColumn> visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(
      AddColumnList(scan->input_scan()->column_list(), &visible_columns));
  VALIDATOR_RET_CHECK(!visible_columns.empty());
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedComputedColumnList(
      visible_columns, visible_parameters, scan->shape_expr_list()));
  GOOGLESQL_RETURN_IF_ERROR(AddColumnsFromComputedColumnList(scan->shape_expr_list(),
                                                   &visible_columns));
  GOOGLESQL_RETURN_IF_ERROR(CheckColumnList(scan, visible_columns));

  // Verify that scan only outputs non-graph SQL type columns, unless
  // FEATURE_SQL_GRAPH_EXPOSE_GRAPH_ELEMENT is enabled.
  if (!language_options_.LanguageFeatureEnabled(
          FEATURE_SQL_GRAPH_EXPOSE_GRAPH_ELEMENT)) {
    for (const ResolvedColumn& column : scan->column_list()) {
      VALIDATOR_RET_CHECK(!TypeIsOrContainsGraphElement(column.type()));
    }
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedGraphPathPatternQuantifier(
    const ResolvedGraphPathPatternQuantifier* quantifier,
    const std::set<ResolvedColumn>& visible_parameters) {
  if (quantifier == nullptr) return absl::OkStatus();

  GOOGLESQL_RETURN_IF_ERROR(ValidateArgumentIsInt64(quantifier->lower_bound(), true,
                                          "Quantifier lower bound"));
  if (!language_options_.LanguageFeatureEnabled(
          FEATURE_SQL_GRAPH_UNBOUNDED_PATH_QUANTIFICATION)) {
    VALIDATOR_RET_CHECK_NE(quantifier->upper_bound(), nullptr)
        << "Upper bound of graph path pattern quantifier must be set";
  }
  if (quantifier->upper_bound() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateArgumentIsInt64(quantifier->upper_bound(), true,
                                            "Quantifier upper bound"));
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedGraphElementScan(
    const ResolvedGraphElementScan* scan,
    const std::set<ResolvedColumn>& visible_parameters) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, scan);
  // A NodePattern should create 1 unique output column of graph node type.
  VALIDATOR_RET_CHECK_EQ(scan->column_list_size(), 1);
  const auto& output_column = scan->column_list(0);
  GOOGLESQL_RETURN_IF_ERROR(CheckUniqueColumnId(output_column));
  VALIDATOR_RET_CHECK(output_column.type()->IsGraphElement());
  VALIDATOR_RET_CHECK_EQ(output_column.type()->AsGraphElement()->IsNode(),
                         scan->Is<ResolvedGraphNodeScan>());

  std::set<ResolvedColumn> visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(AddColumnList(scan->column_list(), &visible_columns));
  if (scan->filter_expr() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateBoolExpr(visible_columns, visible_parameters,
                                     scan->filter_expr()));
  }
  VALIDATOR_RET_CHECK_NE(scan->label_expr(), nullptr);
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedGraphLabelExpr(scan->label_expr()));
  for (const GraphElementTable* const& table :
       scan->target_element_table_list()) {
    VALIDATOR_RET_CHECK_EQ(table->kind() == GraphElementTable::Kind::kNode,
                           scan->Is<ResolvedGraphNodeScan>());
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedGraphLabelExpr(
    const ResolvedGraphLabelExpr* expr) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  VALIDATOR_RET_CHECK_NE(expr, nullptr);
  PushErrorContext push(this, expr);

  switch (expr->node_kind()) {
    case RESOLVED_GRAPH_LABEL:
      return ValidateResolvedGraphLabel(expr->GetAs<ResolvedGraphLabel>());

    case RESOLVED_GRAPH_WILD_CARD_LABEL:
      return ValidateResolvedGraphWildCardLabel(
          expr->GetAs<ResolvedGraphWildCardLabel>());

    case RESOLVED_GRAPH_LABEL_NARY_EXPR:
      return ValidateResolvedGraphLabelNaryExpr(
          expr->GetAs<ResolvedGraphLabelNaryExpr>());

    default:
      return InternalErrorBuilder()
             << "Unhandled node kind: " << expr->node_kind_string()
             << " in ValidateResolvedGraphLabelExpr";
  }

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedGraphLabel(
    const ResolvedGraphLabel* expr) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, expr);
  VALIDATOR_RET_CHECK_NE(expr, nullptr);

  const ResolvedExpr* label_name = expr->label_name();
  if (!language_options_.LanguageFeatureEnabled(
          FEATURE_SQL_GRAPH_DYNAMIC_ELEMENT_TYPE)) {
    VALIDATOR_RET_CHECK_NE(expr->label(), nullptr) << "label must be set";
    VALIDATOR_RET_CHECK_EQ(label_name, nullptr);
  } else {
    VALIDATOR_RET_CHECK_NE(label_name, nullptr);
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(
        /*visible_columns=*/{}, /*visible_parameters=*/{}, label_name));
    VALIDATOR_RET_CHECK(label_name->Is<ResolvedLiteral>());
    VALIDATOR_RET_CHECK(label_name->type()->IsString());

    if (expr->label() != nullptr) {
      VALIDATOR_RET_CHECK(googlesql_base::CaseEqual(
          label_name->GetAs<ResolvedLiteral>()->value().string_value(),
          expr->label()->Name()))
          << "label_name and label must match case-insensitively";
    }
  }
  return absl::OkStatus();
}
absl::Status Validator::ValidateResolvedGraphWildCardLabel(
    const ResolvedGraphWildCardLabel* expr) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, expr);
  VALIDATOR_RET_CHECK_NE(expr, nullptr);

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedGraphNodeScan(
    const ResolvedGraphNodeScan* scan,
    const std::set<ResolvedColumn>& visible_parameters) {
  return ValidateResolvedGraphElementScan(scan, visible_parameters);
}

absl::Status Validator::ValidateResolvedGraphEdgeScan(
    const ResolvedGraphEdgeScan* scan,
    const std::set<ResolvedColumn>& visible_parameters) {
  scan->MarkFieldsAccessed();  // Mark field as visited.
  GOOGLESQL_RETURN_IF_ERROR(ValidateHintList(scan->lhs_hint_list()));
  GOOGLESQL_RETURN_IF_ERROR(ValidateHintList(scan->rhs_hint_list()));
  if (scan->cost_expr() != nullptr) {
    VALIDATOR_RET_CHECK(scan->cost_expr()->type()->IsNumerical());
  }
  return ValidateResolvedGraphElementScan(scan, visible_parameters);
}

absl::Status Validator::ValidateResolvedGraphPathSearchPrefix(
    const ResolvedGraphPathSearchPrefix* search_prefix) {
  if (search_prefix == nullptr) {
    return absl::OkStatus();
  }
  const ResolvedExpr* path_count = search_prefix->path_count();
  if (path_count != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(
        ValidateArgumentIsInt64(path_count, true, "Search prefix path count"));
  }
  VALIDATOR_RET_CHECK_NE(
      search_prefix->type(),
      ResolvedGraphPathSearchPrefix::PATH_SEARCH_PREFIX_TYPE_UNSPECIFIED);

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedGraphPathScan(
    const ResolvedGraphPathScan* scan,
    const std::set<ResolvedColumn>& visible_parameters) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  GOOGLESQL_RETURN_IF_ERROR(ValidateGraphPathMode(scan->path_mode()));
  VALIDATOR_RET_CHECK(!scan->input_scan_list().empty());

  GOOGLESQL_RETURN_IF_ERROR(ValidateHintList(scan->path_hint_list()));
  VALIDATOR_RET_CHECK(scan->head().type()->IsGraphElement());
  VALIDATOR_RET_CHECK(scan->head().type()->AsGraphElement()->IsNode());
  VALIDATOR_RET_CHECK(scan->tail().type()->IsGraphElement());
  VALIDATOR_RET_CHECK(scan->tail().type()->AsGraphElement()->IsNode());
  // Check that `head` and `tail` are set correctly.
  if (scan->quantifier() == nullptr) {
    if (language_options_.LanguageFeatureEnabled(
            FEATURE_SQL_GRAPH_BOUNDED_PATH_QUANTIFICATION)) {
      VALIDATOR_RET_CHECK(scan->group_variable_list_size() == 0)
          << "If group variables cannot be accessed then group_variable_list "
             "must be empty";
    }
    // Non-quantified paths expose the first graph element as 'head' and the
    // last graph element as 'tail'.
    VALIDATOR_RET_CHECK(scan->head() ==
                        scan->input_scan_list().front()->column_list().front());
    std::optional<ResolvedColumn> last_graph_element;
    for (auto it = scan->input_scan_list().back()->column_list().rbegin();
         it != scan->input_scan_list().back()->column_list().rend(); ++it) {
      if (it->type()->IsGraphElement()) {
        last_graph_element = *it;
        break;
      }
    }
    VALIDATOR_RET_CHECK(last_graph_element.has_value())
        << "Last input scan list has no graph elements";
    VALIDATOR_RET_CHECK(scan->tail() == *last_graph_element);
  } else {
    // A quantifier exposes a 'head' and a 'tail' at a minimum.
    VALIDATOR_RET_CHECK(scan->column_list_size() >= 2);
    VALIDATOR_RET_CHECK(scan->head() == scan->column_list(0));
    std::optional<ResolvedColumn> last_graph_element;
    for (auto it = scan->column_list().rbegin();
         it != scan->column_list().rend(); ++it) {
      if (it->type()->IsGraphElement()) {
        last_graph_element = *it;
        break;
      }
    }
    VALIDATOR_RET_CHECK(last_graph_element.has_value())
        << "Column list has no graph elements";
    VALIDATOR_RET_CHECK(scan->tail() == *last_graph_element);
    // Each new column should have a unique column id. Mark the column id
    // as seen and verify that we haven't already seen it.
    GOOGLESQL_RETURN_IF_ERROR(CheckUniqueColumnId(scan->head()));
    GOOGLESQL_RETURN_IF_ERROR(CheckUniqueColumnId(scan->tail()));

    // Validate the group variables.
    if (language_options_.LanguageFeatureEnabled(
            FEATURE_SQL_GRAPH_BOUNDED_PATH_QUANTIFICATION)) {
      VALIDATOR_RET_CHECK(scan->group_variable_list_size() != 0)
          << "Quantifier and group_variable_list must be set together";
      for (auto& make_array : scan->group_variable_list()) {
        GOOGLESQL_RETURN_IF_ERROR(CheckUniqueColumnId(make_array->array()));
      }
    }
  }

  std::set<ResolvedColumn> visible_columns;
  for (const std::unique_ptr<const ResolvedGraphPathScanBase>& input_scan :
       scan->input_scan_list()) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedScan(input_scan.get(), visible_parameters));
    GOOGLESQL_RETURN_IF_ERROR(AddColumnList(input_scan->column_list(), &visible_columns));
  }
  if (scan->path() != nullptr) {
    VALIDATOR_RET_CHECK(scan->path()->column().type()->IsGraphPath());
    const GraphPathType* path_type =
        scan->path()->column().type()->AsGraphPath();
    std::vector<ResolvedColumn> columns_to_check = scan->column_list();
    columns_to_check.push_back(scan->head());
    columns_to_check.push_back(scan->tail());
    for (const ResolvedColumn& column : columns_to_check) {
      const Type* type = column.type();
      if (type->IsGraphPath()) {
        VALIDATOR_RET_CHECK(column == scan->path()->column())
            << "The only path column must be the named path column. The "
               "path-typed column: "
            << column.DebugString()
            << "; the path column: " << scan->path()->column().DebugString();
      } else {
        if (type->IsArray()) {
          type = type->AsArray()->element_type();
        }
        VALIDATOR_RET_CHECK(type->IsGraphElement())
            << "Unexpected type in column list: " << column.DebugString();
        const GraphElementType* graph_element_type = type->AsGraphElement();
        if (graph_element_type->IsNode()) {
          VALIDATOR_RET_CHECK(
              graph_element_type->CoercibleTo(path_type->node_type()));
        } else {
          VALIDATOR_RET_CHECK(
              graph_element_type->CoercibleTo(path_type->edge_type()));
        }
      }
    }
    VALIDATOR_RET_CHECK(scan->path()->column() == scan->column_list().back())
        << "The path column must be the last column in the column "
           "list. Path: "
        << scan->path()->column().DebugString();
    GOOGLESQL_RETURN_IF_ERROR(CheckUniqueColumnId(scan->path()->column()));
  }
  // There is at most one path column in <column_list>.
  absl::flat_hash_set<ResolvedColumn> path_typed_columns;
  for (const ResolvedColumn& column : scan->column_list()) {
    if (column.type()->IsGraphPath()) {
      path_typed_columns.insert(column);
    }
  }
  VALIDATOR_RET_CHECK_LE(path_typed_columns.size(), 1)
      << "Found more than one path column in column list: "
      << absl::StrJoin(path_typed_columns, ", ",
                       [](std::string* out, const ResolvedColumn& column) {
                         absl::StrAppend(out, column.DebugString());
                       });

  for (const std::unique_ptr<const ResolvedGraphMakeArrayVariable>& make_array :
       scan->group_variable_list()) {
    VALIDATOR_RET_CHECK(visible_columns.find(make_array->element()) !=
                        visible_columns.end())
        << "Element " << make_array->element().DebugString()
        << " not found in visible columns";
  }
  if (scan->filter_expr() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateBoolExpr(visible_columns, visible_parameters,
                                     scan->filter_expr()));
  }
  if (scan->path_cost() != nullptr) {
    VALIDATOR_RET_CHECK(scan->path_cost()->cost_supertype() != nullptr);
    VALIDATOR_RET_CHECK(scan->path_cost()->cost_supertype()->IsNumerical());
  }
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedGraphPathPatternQuantifier(
      scan->quantifier(), visible_parameters));
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedGraphPathSearchPrefix(scan->search_prefix()));
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedGraphScan(
    const ResolvedGraphScan* scan,
    const std::set<ResolvedColumn>& visible_parameters) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  VALIDATOR_RET_CHECK(!scan->input_scan_list().empty());
  std::set<ResolvedColumn> visible_columns;
  for (const std::unique_ptr<const ResolvedGraphPathScan>& input_scan :
       scan->input_scan_list()) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedScan(input_scan.get(), visible_parameters));
    GOOGLESQL_RETURN_IF_ERROR(AddColumnList(input_scan->column_list(), &visible_columns));
  }
  if (scan->optional()) {
    VALIDATOR_RET_CHECK_NE(scan->input_scan(), nullptr)
        << "OPTIONAL MATCH needs an input scan in order to do a left join";
  }
  if (scan->input_scan() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(
        ValidateResolvedScan(scan->input_scan(), visible_parameters));

    std::set<ResolvedColumn> left_visible_columns;
    GOOGLESQL_RETURN_IF_ERROR(AddColumnList(scan->input_scan()->column_list(),
                                  &left_visible_columns));

    VALIDATOR_RET_CHECK(!googlesql_base::SortedContainersHaveIntersection(
        left_visible_columns, visible_columns))
        << "Input scan and graph pattern scan should not have any common "
           "column references";

    size_t input_table_size = scan->input_scan()->column_list().size();
    VALIDATOR_RET_CHECK(scan->column_list().size() > input_table_size)
        << "Graph pattern in MATCH should produce at least 1 column";
    for (size_t i = 0; i < input_table_size; ++i) {
      VALIDATOR_RET_CHECK(scan->column_list()[i] ==
                          scan->input_scan()->column_list()[i])
          << "Input scan's columns should be the prefix of GraphScan's output "
             "columns";
    }
    GOOGLESQL_RETURN_IF_ERROR(
        AddColumnList(scan->input_scan()->column_list(), &visible_columns));
  }

  if (scan->filter_expr() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateBoolExpr(visible_columns, visible_parameters,
                                     scan->filter_expr()));
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedGraphLinearScan(
    const ResolvedGraphLinearScan* scan,
    const std::set<ResolvedColumn>& visible_parameters) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, scan);
  VALIDATOR_RET_CHECK(!scan->scan_list().empty());

  for (int i = 0; i < scan->scan_list_size(); ++i) {
    const ResolvedScan* child_scan = scan->scan_list(i);
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedScan(child_scan, visible_parameters));
    // After validating the first child scan, push a new layer of output working
    // table onto the stack. For other child scans, simply update the top of the
    // stack.
    if (i == 0) {
      current_input_scan_in_graph_linear_scan_stack_.push_back(child_scan);
    } else {
      current_input_scan_in_graph_linear_scan_stack_.back() = child_scan;
    }
  }
  std::set<ResolvedColumn> visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(AddColumnList(
      current_input_scan_in_graph_linear_scan_stack_.back()->column_list(),
      &visible_columns));
  GOOGLESQL_RETURN_IF_ERROR(CheckColumnList(scan, visible_columns));

  // Pop output working table produced by a linear query block from the stack.
  current_input_scan_in_graph_linear_scan_stack_.pop_back();
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedGraphRefScan(
    const ResolvedGraphRefScan* scan,
    const std::set<ResolvedColumn>& visible_parameters) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, scan);
  VALIDATOR_RET_CHECK(!current_input_scan_in_graph_linear_scan_stack_.empty());
  VALIDATOR_RET_CHECK(current_input_scan_in_graph_linear_scan_stack_.back() !=
                      nullptr);
  std::set<ResolvedColumn> visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(AddColumnList(
      current_input_scan_in_graph_linear_scan_stack_.back()->column_list(),
      &visible_columns));
  return CheckColumnList(scan, visible_columns);
}

absl::Status Validator::ValidateResolvedGraphCallScan(
    const ResolvedGraphCallScan* scan,
    const std::set<ResolvedColumn>& visible_parameters) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, scan);

  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedScan(scan->input_scan(), visible_parameters));

  if (scan->optional()) {
    absl::string_view err_text =
        "OPTIONAL CALL needs an input scan in order to do a left join";
    VALIDATOR_RET_CHECK(scan->input_scan() != nullptr) << err_text;
    VALIDATOR_RET_CHECK(scan->input_scan()->Is<ResolvedGraphRefScan>())
        << err_text << "; found " << scan->input_scan()->node_kind_string();
  }

  std::set<ResolvedColumn> left_visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(
      AddColumnList(scan->input_scan()->column_list(), &left_visible_columns));

  std::set<ResolvedColumn> visible_paramerters_for_rhs;
  for (const auto& col_ref : scan->parameter_list()) {
    if (!col_ref->is_correlated()) {
      // If the ref is not already correlated, it must be available from the
      // left scan.
      VALIDATOR_RET_CHECK(
          googlesql_base::ContainsKey(left_visible_columns, col_ref->column()))
          << "Column must come from the left scan";
    } else {
      // If the ref is correlated, it cannot come from the left scan.
      VALIDATOR_RET_CHECK(
          !googlesql_base::ContainsKey(left_visible_columns, col_ref->column()))
          << "Column cannot come from the left scan";
    }
    visible_paramerters_for_rhs.insert(col_ref->column());
  }

  // CALL subqueries are just like LATERAL joins, the RHS sees only the lateral
  // list, not the outside parameters.
  VALIDATOR_RET_CHECK(scan->subquery() != nullptr);

  if (scan->subquery()->Is<ResolvedGraphTableScan>()) {
    const auto* subquery = scan->subquery()->GetAs<ResolvedGraphTableScan>();
    // We should check that this is the same as the enclosing property graph.
    VALIDATOR_RET_CHECK(subquery->shape_expr_list().empty());
  } else {
    VALIDATOR_RET_CHECK(scan->subquery()->Is<ResolvedTVFScan>());
  }

  GOOGLESQL_RETURN_IF_ERROR(
      ValidateResolvedScan(scan->subquery(), visible_paramerters_for_rhs));

  std::set<ResolvedColumn> right_visible_columns;
  GOOGLESQL_RETURN_IF_ERROR(
      AddColumnList(scan->subquery()->column_list(), &right_visible_columns));
  // The input and subquery scans should not have any common columns.
  VALIDATOR_RET_CHECK(!googlesql_base::SortedContainersHaveIntersection(
      left_visible_columns, right_visible_columns));

  const std::set<ResolvedColumn> visible_columns =
      googlesql_base::STLSetUnion(left_visible_columns, right_visible_columns);

  GOOGLESQL_RETURN_IF_ERROR(CheckColumnList(scan, visible_columns));

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedGraphGetElementProperty(
    const std::set<ResolvedColumn>& visible_columns,
    const std::set<ResolvedColumn>& visible_parameters,
    const ResolvedGraphGetElementProperty* get_element_prop_expr) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  VALIDATOR_RET_CHECK(get_element_prop_expr->expr()->type()->IsGraphElement());

  const ResolvedExpr* property_name = get_element_prop_expr->property_name();
  if (!language_options_.LanguageFeatureEnabled(
          FEATURE_SQL_GRAPH_DYNAMIC_ELEMENT_TYPE)) {
    VALIDATOR_RET_CHECK_NE(get_element_prop_expr->property(), nullptr)
        << "property must be set";
    VALIDATOR_RET_CHECK_EQ(property_name, nullptr);
  } else {
    VALIDATOR_RET_CHECK_NE(property_name, nullptr);
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(
        /*visible_columns=*/{}, /*visible_parameters=*/{}, property_name));
    VALIDATOR_RET_CHECK(property_name->Is<ResolvedLiteral>());
    VALIDATOR_RET_CHECK(property_name->type()->IsString());

    if (get_element_prop_expr->property() != nullptr) {
      VALIDATOR_RET_CHECK(googlesql_base::CaseEqual(
          property_name->GetAs<ResolvedLiteral>()->value().string_value(),
          get_element_prop_expr->property()->Name()))
          << "property_name and property must match case-insensitively";
    }
  }

  return ValidateResolvedExpr(visible_columns, visible_parameters,
                              get_element_prop_expr->expr());
}

absl::Status Validator::ValidateResolvedGraphLabelNaryExpr(
    const ResolvedGraphLabelNaryExpr* expr) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, expr);
  VALIDATOR_RET_CHECK_NE(expr, nullptr);

  VALIDATOR_RET_CHECK_GT(expr->operand_list_size(), 0);

  // Check that unary operator ! has one argument.
  // Check that n-ary operators &, | have >1 argument.
  switch (expr->op()) {
    case ResolvedGraphLabelNaryExpr::NOT:
      VALIDATOR_RET_CHECK_EQ(expr->operand_list_size(), 1);
      break;
    case ResolvedGraphLabelNaryExpr::AND:
    case ResolvedGraphLabelNaryExpr::OR:
      VALIDATOR_RET_CHECK_GT(expr->operand_list_size(), 1);
      break;
    default:
      return InternalErrorBuilder() << "Unrecognized graph label operation type"
                                    << " in ValidateResolvedGraphLabelNaryExpr";
  }

  // For each operand, perform validation recursively
  for (const auto& operand : expr->operand_list()) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedGraphLabelExpr(operand.get()));
  }

  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedGraphElementIdentifier(
    const std::set<ResolvedColumn>& visible_columns,
    const std::set<ResolvedColumn>& visible_parameters,
    const ResolvedGraphElementIdentifier* argument, bool is_edge) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  VALIDATOR_RET_CHECK_NE(argument->element_table(), nullptr);
  for (const auto& key : argument->key_list()) {
    GOOGLESQL_RETURN_IF_ERROR(
        ValidateResolvedExpr(visible_columns, visible_parameters, key.get()));
  }

  VALIDATOR_RET_CHECK_EQ(argument->source_node_identifier() != nullptr,
                         is_edge);
  VALIDATOR_RET_CHECK_EQ(argument->dest_node_identifier() != nullptr, is_edge);
  if (is_edge) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedGraphElementIdentifier(
        visible_columns, visible_parameters, argument->source_node_identifier(),
        /*is_edge=*/false));
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedGraphElementIdentifier(
        visible_columns, visible_parameters, argument->dest_node_identifier(),
        /*is_edge=*/false));
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedGraphElementProperty(
    const std::set<ResolvedColumn>& visible_columns,
    const std::set<ResolvedColumn>& visible_parameters,
    const ResolvedGraphElementProperty* argument) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  VALIDATOR_RET_CHECK_NE(argument->declaration(), nullptr);
  GOOGLESQL_RET_CHECK(argument->declaration()->Type()->Equals(argument->expr()->type()));
  return ValidateResolvedExpr(visible_columns, visible_parameters,
                              argument->expr());
}

absl::Status Validator::ValidateResolvedGraphMakeElement(
    const std::set<ResolvedColumn>& visible_columns,
    const std::set<ResolvedColumn>& visible_parameters,
    const ResolvedGraphMakeElement* make_graph_element) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  VALIDATOR_RET_CHECK(make_graph_element->type()->IsGraphElement());

  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedGraphElementIdentifier(
      visible_columns, visible_parameters, make_graph_element->identifier(),
      make_graph_element->type()->AsGraphElement()->IsEdge()));
  for (const auto& property : make_graph_element->property_list()) {
    GOOGLESQL_RET_CHECK_OK(ValidateResolvedGraphElementProperty(
        visible_columns, visible_parameters, property.get()));
  }

  if (!language_options_.LanguageFeatureEnabled(
          FEATURE_SQL_GRAPH_DYNAMIC_ELEMENT_TYPE)) {
    VALIDATOR_RET_CHECK_EQ(make_graph_element->dynamic_labels(), nullptr);
    VALIDATOR_RET_CHECK_EQ(make_graph_element->dynamic_properties(), nullptr);
  } else {
    if (make_graph_element->dynamic_labels() != nullptr) {
      // If the dynamic labels are of type ARRAY<STRING>, the feature flag
      // must be enabled and the element must be a node type. Otherwise, the
      // dynamic labels must be of type STRING.
      if (IsStringArray(make_graph_element->dynamic_labels()->type())) {
        VALIDATOR_RET_CHECK(language_options_.LanguageFeatureEnabled(
            FEATURE_SQL_GRAPH_DYNAMIC_MULTI_LABEL_NODES));
        // For now, we only support dynamic multi labels for nodes.
        VALIDATOR_RET_CHECK(
            make_graph_element->type()->AsGraphElement()->IsNode());
      } else {
        VALIDATOR_RET_CHECK(
            make_graph_element->dynamic_labels()->type()->IsString());
      }
    }
    if (make_graph_element->dynamic_properties() != nullptr) {
      VALIDATOR_RET_CHECK(
          make_graph_element->dynamic_properties()->type()->IsJson());
    }
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedArrayAggregate(
    const std::set<ResolvedColumn>& visible_columns,
    const std::set<ResolvedColumn>& visible_parameters,
    const ResolvedArrayAggregate* array_aggregate) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(visible_columns, visible_parameters,
                                       array_aggregate->array()));
  // Everywhere else, the element_column is also visible
  std::set<ResolvedColumn> columns = visible_columns;
  columns.insert(array_aggregate->element_column());
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedComputedColumnList(
      columns, visible_parameters,
      array_aggregate->pre_aggregate_computed_column_list()));

  VALIDATOR_RET_CHECK(array_aggregate->array()->type()->IsArray())
      << "Array expression must have an array type";
  VALIDATOR_RET_CHECK(array_aggregate->element_column().type()->Equals(
      array_aggregate->array()->type()->AsArray()->element_type()))
      << "Element must have the same type as an array element";

  GOOGLESQL_RETURN_IF_ERROR(AddColumnsFromComputedColumnList(
      array_aggregate->pre_aggregate_computed_column_list(), &columns));
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedAggregateFunctionCall(
      columns, visible_parameters, array_aggregate->aggregate()));
  GOOGLESQL_RETURN_IF_ERROR(ValidateOrderByAndLimitClausesOfAggregateFunctionCall(
      columns, visible_parameters, array_aggregate->aggregate()));

  // If element_column isn't in the visible columns, validation should fail.
  absl::Status status = ValidateResolvedAggregateFunctionCall(
      visible_columns, visible_parameters, array_aggregate->aggregate());
  VALIDATOR_RET_CHECK(!status.ok())
      << "Aggregate must depend on the element_column: "
      << array_aggregate->element_column().DebugString();

  return absl::OkStatus();
}

absl::Status Validator::ValidateGraphPathMode(
    const ResolvedGraphPathMode* path_mode) {
  GOOGLESQL_RET_CHECK(path_mode == nullptr ||
            path_mode->path_mode() !=
                ResolvedGraphPathMode::PATH_MODE_UNSPECIFIED)
      << "Path mode must be specified";
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedGraphIsLabeledPredicate(
    const std::set<ResolvedColumn>& visible_columns,
    const std::set<ResolvedColumn>& visible_parameters,
    const ResolvedGraphIsLabeledPredicate* predicate) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  // Mark accessed
  predicate->is_not();
  VALIDATOR_RET_CHECK(predicate->type()->IsBool());
  VALIDATOR_RET_CHECK_NE(predicate->expr(), nullptr);
  VALIDATOR_RET_CHECK(predicate->expr()->type()->IsGraphElement());
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(visible_columns, visible_parameters,
                                       predicate->expr()));
  VALIDATOR_RET_CHECK_NE(predicate->label_expr(), nullptr);
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedGraphLabelExpr(predicate->label_expr()));
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedBarrierScan(
    const ResolvedBarrierScan* scan,
    const std::set<ResolvedColumn>& visible_parameters) {
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedScan(scan->input_scan(), visible_parameters));

  // BarrierScan can only reference columns from the input scan.
  std::set<ResolvedColumn> input_scan_columns;
  GOOGLESQL_RETURN_IF_ERROR(
      AddColumnList(scan->input_scan()->column_list(), &input_scan_columns));
  GOOGLESQL_RETURN_IF_ERROR(CheckColumnList(scan, input_scan_columns));
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedUpdateConstructor(
    const std::set<ResolvedColumn>& visible_columns,
    const std::set<ResolvedColumn>& visible_parameters,
    const ResolvedUpdateConstructor* update_constructor) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, update_constructor);
  GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(visible_columns, visible_parameters,
                                       update_constructor->expr()));

  for (const std::unique_ptr<const ResolvedUpdateFieldItem>& update_field_item :
       update_constructor->update_field_item_list()) {
    GOOGLESQL_RETURN_IF_ERROR(ValidateResolvedExpr(visible_columns, visible_parameters,
                                         update_field_item->expr()));
    VALIDATOR_RET_CHECK(!update_field_item->proto_field_path().empty());
    const absl::string_view base_proto_name = update_constructor->expr()
                                                  ->type()
                                                  ->AsProto()
                                                  ->descriptor()
                                                  ->full_name();
    GOOGLESQL_RETURN_IF_ERROR(ValidateProtoFieldPath(
        base_proto_name, update_field_item->proto_field_path()));
    update_field_item->operation();  // Mark field as visited.
  }
  return absl::OkStatus();
}

absl::Status Validator::ValidateResolvedCreateSequenceStmt(
    const ResolvedCreateSequenceStmt* stmt) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  PushErrorContext push(this, stmt);
  VALIDATOR_RET_CHECK(!stmt->name_path().empty());
  GOOGLESQL_RETURN_IF_ERROR(ValidateOptionsList(stmt->option_list()));
  return absl::OkStatus();
}

}  // namespace googlesql
