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

#include "googlesql/public/annotation/collation.h"

#include <memory>
#include <string>
#include <vector>

#include "googlesql/common/errors.h"
#include "googlesql/parser/ast_node.h"
#include "googlesql/parser/parse_tree_errors.h"
#include "googlesql/public/annotation/default_annotation_spec.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/collation.h"
#include "googlesql/public/types/simple_value.h"
#include "googlesql/public/types/type.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_collation.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/substitute.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

namespace {

// Returns true if <type> supports collation.
bool SupportsCollation(const Type* type) {
  if (type->IsString()) {
    return true;
  } else if (type->IsArray()) {
    return SupportsCollation(type->AsArray()->element_type());
  } else if (type->IsStruct()) {
    for (int i = 0; i < type->AsStruct()->num_fields(); i++) {
      if (SupportsCollation(type->AsStruct()->field(i).type)) {
        return true;
      }
    }
    return false;
  }
  return false;
}

std::string GetArgumentNameOrIndex(const FunctionSignature& signature, int i) {
  if (signature.ConcreteArgument(i).options().has_argument_name()) {
    return signature.ConcreteArgument(i).options().argument_name();
  } else {
    return std::to_string(i + 1);
  }
}

}  // namespace

absl::Status CollationAnnotation::RejectsCollationOnFunctionArguments(
    const ResolvedFunctionCallBase& function_call) {
  const FunctionSignature& signature = function_call.signature();
  // Index or name of argument is kept for error message.
  for (int i = 0; i < signature.NumConcreteArguments(); i++) {
    const googlesql::ResolvedExpr* arg_i = nullptr;
    if (function_call.argument_list_size() > 0) {
      arg_i = function_call.argument_list(i);
      // If the input path expression of FLATTEN function is effectively a
      // single array and does not dot into fields of its elements, the first
      // argument of FLATTEN function is the input array. Otherwise, the first
      // argument of FLATTEN function is set to be a ResolvedFlatten node whose
      // <expr> contains the input array.
      if (function_call.function()->IsGoogleSQLBuiltin() &&
          signature.context_id() == FN_FLATTEN &&
          arg_i->node_kind() == RESOLVED_FLATTEN) {
        arg_i = arg_i->GetAs<ResolvedFlatten>()->expr();
      }
    } else if (function_call.generic_argument_list(i)->expr() != nullptr) {
      arg_i = function_call.generic_argument_list(i)->expr();
    } else {
      continue;
    }
    const AnnotationMap* argi_annotation_map = arg_i->type_annotation_map();
    if (CollationAnnotation::ExistsIn(argi_annotation_map)) {
      const std::string argument_name =
          (function_call.function()->IsGoogleSQLBuiltin() &&
           signature.context_id() == FN_FLATTEN)
              ? "input array to FLATTEN"
              : absl::StrCat("argument ", GetArgumentNameOrIndex(signature, i));
      return MakeSqlError() << absl::Substitute(
                 "Collation is not allowed on $0 ($1)$2",
                 argument_name,
                 argi_annotation_map->DebugString(GetId()),
                 arg_i->type()->IsString()
                     ? ". Use COLLATE(arg, '') to remove collation"
                     : "");
    }
  }
  return absl::OkStatus();
}

absl::Status CollationAnnotation::CheckAndPropagateForCast(
    const ResolvedCast& cast, AnnotationMap* result_annotation_map) {
  GOOGLESQL_RET_CHECK(!result_annotation_map->Has<CollationAnnotation>());
  const Collation& target_collation = cast.type_modifiers().collation();
  if (target_collation.Empty()) {
    return absl::OkStatus();
  }
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<AnnotationMap> annotation_map,
                   target_collation.ToAnnotationMap(cast.type()));
  return MergeAnnotations(annotation_map.get(), *result_annotation_map);
}

absl::Status CollationAnnotation::CheckAndPropagateForFunctionCallBase(
    const ResolvedFunctionCallBase& function_call,
    AnnotationMap* result_annotation_map) {
  // TODO: add non-default propapation logic for functions.
  const FunctionSignature& signature = function_call.signature();
  if (signature.options().rejects_collation()) {
    return RejectsCollationOnFunctionArguments(function_call);
  }
  // Default propagation rules.
  if (signature.options().propagates_collation() && signature.IsConcrete() &&
      SupportsCollation(signature.result_type().type())) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<AnnotationMap> collation_to_propagate,
                     GetCollationFromFunctionArguments(
                         /*error_location=*/nullptr, function_call,
                         FunctionEnums::AFFECTS_PROPAGATION));
    // If the result_type has option uses_array_element_for_collation enabled,
    // propagates the collation annotation to array element.
    if (signature.result_type().options().uses_array_element_for_collation()) {
      GOOGLESQL_RET_CHECK(result_annotation_map->IsArrayMap());
      result_annotation_map =
          result_annotation_map->AsStructMap()->mutable_field(0);
    }
    GOOGLESQL_RETURN_IF_ERROR(
        MergeAnnotations(collation_to_propagate.get(), *result_annotation_map));
  }
  return absl::OkStatus();
}

// TODO: Rewrite the util function
// GetCollationFromFunctionArguments below with this function to avoid logic
// duplication. Need to return some information from this function for proper
// error message in the caller.
absl::StatusOr<std::unique_ptr<AnnotationMap>>
CollationAnnotation::GetCollationFromAnnotationMaps(
    const Type* type,
    const std::vector<const AnnotationMap*>& annotation_maps) {
  std::unique_ptr<AnnotationMap> result_collation = nullptr;
  // Shortcut for the case where the input type is not supported with collation.
  if (!SupportsCollation(type)) {
    return result_collation;
  }
  for (const AnnotationMap* annotation_map : annotation_maps) {
    if (!CollationAnnotation::ExistsIn(annotation_map)) {
      continue;
    }
    GOOGLESQL_RET_CHECK(annotation_map != nullptr &&
              annotation_map->HasCompatibleStructure(type));
    if (result_collation == nullptr) {
      result_collation = AnnotationMap::Create(type);
      // When merging the argument collation with the empty annotation map, we
      // should not have compatible issue. Return an internal error rather than
      // a user-facing error when this happens.
      GOOGLESQL_RET_CHECK(MergeAnnotations(annotation_map, *result_collation).ok());
    } else {
      GOOGLESQL_RETURN_IF_ERROR(MergeAnnotations(annotation_map, *result_collation));
    }
  }
  return result_collation;
}

absl::StatusOr<std::unique_ptr<AnnotationMap>>
CollationAnnotation::GetCollationFromFunctionArguments(
    const ASTNode* error_location,
    const ResolvedFunctionCallBase& function_call,
    FunctionEnums::ArgumentCollationMode collation_mode_mask) {
  const FunctionSignature& signature = function_call.signature();
  std::unique_ptr<AnnotationMap> result_collation = nullptr;
  for (int i = 0; i < signature.NumConcreteArguments(); i++) {
    const googlesql::ResolvedExpr* arg_i = nullptr;
    // The <function_call> has exactly one of 'argument_list' or
    // 'generic_argument_list' populated. Usually 'argument_list' is used, but
    // the 'generic_argument_list' is used when there is a non-expression
    // argument (such as a lambda). Only expressions can have collation, so we
    // only look for arguments that are expression or lambda function body.
    if (function_call.argument_list_size() > 0) {
      arg_i = function_call.argument_list(i);
    } else {
      const auto* generic_argument_i = function_call.generic_argument_list(i);
      if (generic_argument_i->expr() != nullptr) {
        arg_i = generic_argument_i->expr();
      } else if (generic_argument_i->inline_lambda() != nullptr) {
        arg_i = generic_argument_i->inline_lambda()->body();
      }
    }
    if (arg_i == nullptr) continue;

    const AnnotationMap* argi_annotation_map = arg_i->type_annotation_map();
    const Type* argi_type = arg_i->type();
    // If an argument has option uses_array_element_for_collation enabled,
    // uses the collation annotation on array element.
    if (signature.ConcreteArgument(i)
            .options()
            .uses_array_element_for_collation()) {
      GOOGLESQL_RET_CHECK(argi_type->IsArray());
      argi_type = argi_type->AsArray()->element_type();
      if (argi_annotation_map != nullptr) {
        GOOGLESQL_RET_CHECK(argi_annotation_map->IsArrayMap());
        argi_annotation_map = argi_annotation_map->AsStructMap()->field(0);
      }
    }

    const bool mode_matches =
        (signature.ConcreteArgument(i).options().argument_collation_mode() &
         collation_mode_mask) != 0;

    if (!mode_matches || !ExistsIn(argi_annotation_map)) {
      continue;
    }

    if (result_collation == nullptr) {
      result_collation = AnnotationMap::Create(argi_type);
      // When merging the argument collation with the empty annotation map, we
      // should not have compatible issue. Return an internal error rather than
      // a user-facing error when this happens.
      GOOGLESQL_RET_CHECK(MergeAnnotations(argi_annotation_map, *result_collation).ok());
    } else {
      absl::Status merge_status =
          MergeAnnotations(argi_annotation_map, *result_collation);
      if (merge_status.ok()) {
        continue;
      }
      // TODO: Use a better mechanism to detect the collation
      // mismatch error from the result of MergeAnnotations function.
      if (merge_status.code() == absl::StatusCode::kInternal) {
        return merge_status;
      }
      // TODO: Add function to googlesql::Type class to output
      // collation within type like ARRAY<STRING COLLATE 'und:ci'>. Also make
      // the error message less confusing for functions that take two arguments.
      ::googlesql_base::StatusBuilder error =
          MakeSqlError() << absl::Substitute(
              "$0. Collation on argument $1 ($2) in function $3 is not "
              "compatible with other arguments",
              merge_status.message(), GetArgumentNameOrIndex(signature, i),
              argi_annotation_map->DebugString(GetId()),
              function_call.function()->SQLName());
      if (error_location != nullptr) {
        error.AttachPayload(
            GetErrorLocationPoint(error_location,
                                  /*include_leftmost_child=*/true)
                .ToInternalErrorLocation());
      }
      return error;
    }
  }
  return result_collation;
}

absl::Status CollationAnnotation::ResolveCollationForResolvedOrderByItem(
    ResolvedOrderByItem* resolved_order_by_item) {
  const ResolvedExpr* collation_name = resolved_order_by_item->collation_name();
  ResolvedCollation resolved_collation;
  if (collation_name != nullptr) {
    GOOGLESQL_RET_CHECK(collation_name->type()->IsString());
    if (collation_name->Is<ResolvedLiteral>()) {
      resolved_collation = ResolvedCollation::MakeScalar(
          collation_name->GetAs<ResolvedLiteral>()->value().string_value());
    }
  } else if (resolved_order_by_item->column_ref()->type_annotation_map() !=
             nullptr) {
    // There is collation to be propagated from the column_ref.
    GOOGLESQL_ASSIGN_OR_RETURN(
        resolved_collation,
        ResolvedCollation::MakeResolvedCollation(
            *resolved_order_by_item->column_ref()->type_annotation_map()));
  }
  resolved_order_by_item->set_collation(resolved_collation);
  return absl::OkStatus();
}

absl::Status CollationAnnotation::ScalarMergeIfCompatible(
    const AnnotationMap* in, AnnotationMap& out) const {
  if (!CollationAnnotation::ExistsIn(in)) {
    return absl::OkStatus();
  }
  return DefaultAnnotationSpec::ScalarMergeIfCompatible(in, out);
}

}  // namespace googlesql
