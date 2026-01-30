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

#include "googlesql/public/annotation/default_annotation_spec.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "googlesql/common/errors.h"
#include "googlesql/common/type_and_argument_kind_visitor.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/simple_value.h"
#include "googlesql/public/types/type.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

namespace {

class AnnotationPropagationVisitor
    : public AnnotatedTypeAndSignatureArgumentKindVisitor<
          const AnnotationMap*> {
 public:
  AnnotationPropagationVisitor(
      absl::flat_hash_map<SignatureArgumentKind,
                          std::unique_ptr<AnnotationMap>>& merging_map,
      std::function<absl::Status(const AnnotationMap*, AnnotationMap&)>
          merge_callback)
      : merging_map_(merging_map), merge_callback_(merge_callback) {}

  absl::Status PreVisitChildren(AnnotatedType annotated_type,
                                SignatureArgumentKind original_kind) override {
    const auto& [type, input_annotation_map] = annotated_type;

    // Merge at the current level itself.
    auto it = merging_map_.find(original_kind);
    if (it == merging_map_.end()) {
      it = merging_map_.insert({original_kind, AnnotationMap::Create(type)})
               .first;
    }

    AnnotationMap* current_map = it->second.get();

    if (input_annotation_map != nullptr) {
      // We're propagating from the input map to the appropriate merging spot.
      GOOGLESQL_RETURN_IF_ERROR(merge_callback_(input_annotation_map, *current_map));
    }
    return absl::OkStatus();
  }

  absl::StatusOr<const AnnotationMap*> PostVisitChildren(
      AnnotatedType annotated_type, SignatureArgumentKind original_kind,
      std::vector<const AnnotationMap*> component_results) override {
    GOOGLESQL_ASSIGN_OR_RETURN(absl::Span<const SignatureArgumentKind> component_kinds,
                     GetComponentSignatureArgumentKinds(original_kind));

    auto it = merging_map_.find(original_kind);
    GOOGLESQL_RET_CHECK(it != merging_map_.end());
    AnnotationMap* current_map = it->second.get();

    if (component_results.empty()) {
      return current_map;
    }

    // This ArgKind is related to other component arguments.
    GOOGLESQL_RET_CHECK(current_map->IsStructMap());
    GOOGLESQL_RET_CHECK_EQ(current_map->AsStructMap()->num_fields(),
                 component_kinds.size());

    for (int i = 0; i < component_kinds.size(); ++i) {
      GOOGLESQL_RETURN_IF_ERROR(merge_callback_(
          component_results[i], *current_map->AsStructMap()->mutable_field(i)));
    }

    return current_map;
  }

 private:
  absl::flat_hash_map<SignatureArgumentKind, std::unique_ptr<AnnotationMap>>&
      merging_map_;
  std::function<absl::Status(const AnnotationMap*, AnnotationMap&)>
      merge_callback_;
};

}  // namespace

absl::StatusOr<const AnnotationMap*>
DefaultAnnotationSpec::PropagateThroughCompositeType(
    AnnotatedType annotated_type, SignatureArgumentKind original_kind,
    absl::flat_hash_map<SignatureArgumentKind, std::unique_ptr<AnnotationMap>>&
        merging_map) const {
  AnnotationPropagationVisitor visitor(
      merging_map, [this](const AnnotationMap* from, AnnotationMap& to) {
        return MergeAnnotations(from, to);
      });
  return visitor.Visit(annotated_type, original_kind);
}

// Returns the argument at the given index of the function call, or null if it
// is not an expression or a lambda (e.g. if it is a SEQUENCE).
//
// The <function_call> has exactly one of 'argument_list' or
// 'generic_argument_list' populated. Usually 'argument_list' is used, but
// the 'generic_argument_list' is used when there is a non-expression
// argument (such as a lambda). This function is just a convenient helper to
// hide the different lists when attempting to get the argument expression.
const ResolvedNode* GetFunctionCallArgument(
    const ResolvedFunctionCallBase& function_call, int i) {
  if (function_call.argument_list_size() > 0) {
    return function_call.argument_list(i);
  }

  const auto* generic_argument_i = function_call.generic_argument_list(i);
  if (generic_argument_i->expr() != nullptr) {
    return generic_argument_i->expr();
  }
  return generic_argument_i->inline_lambda();
}

absl::Status DefaultAnnotationSpec::CheckAndPropagateForFunctionCallBase(
    const ResolvedFunctionCallBase& function_call,
    AnnotationMap* result_annotation_map) {
  const FunctionSignature& signature = function_call.signature();
  GOOGLESQL_RET_CHECK(signature.IsConcrete());

  // TODO: Replace this with the actual propagation logic for each
  // function.
  switch (signature.context_id()) {
    // Graph functions:
    case FN_PATH_NODES:
    case FN_PATH_EDGES:
    case FN_PATH_FIRST:
    case FN_PATH_LAST:
    case FN_PATH_CREATE:
    case FN_UNCHECKED_PATH_CREATE:
    case FN_CONCAT_PATH:
    case FN_UNCHECKED_CONCAT_PATH: {
      for (const auto& arg : function_call.argument_list()) {
        if (arg->IsExpression() && arg->type_annotation_map() != nullptr &&
            !arg->type_annotation_map()->Empty()) {
          return MakeSqlError()
                 << "Annotation propagation is not supported for function "
                 << function_call.function()->Name();
        }
      }
      return absl::OkStatus();
    }
    default:
      break;
  }

  // This map contains only the root templated kinds, (ARG_TYPE_ANY_1,
  // ARG_TYPE_ANY_2, ..etc) but not the related kinds like ARG_ARRAY_TYPE_ANY_1.
  // Nor ARBITRARY either, since arbitrary types are unrelated to each other.
  absl::flat_hash_map<SignatureArgumentKind, std::unique_ptr<AnnotationMap>>
      merging_map;

  // First pass, we collect annotations from all expression arguments.
  for (int i = 0; i < signature.NumConcreteArguments(); ++i) {
    GOOGLESQL_RET_CHECK(!signature.ConcreteArgument(i).IsRelation())
        << "TABLE arguments with annotations are not supported.";
    if (signature.ConcreteArgument(i).IsScalar()) {
      // Propagate through any nested type to the "root" template kinds.
      const ResolvedNode* arg_i = GetFunctionCallArgument(function_call, i);

      GOOGLESQL_RET_CHECK(arg_i->IsExpression());
      GOOGLESQL_RETURN_IF_ERROR(PropagateThroughCompositeType(
                          arg_i->GetAs<ResolvedExpr>()->annotated_type(),
                          signature.ConcreteArgument(i).original_kind(),
                          merging_map)
                          .status());
    }
  }

  // Now that we collected annotations from all expr arguments, process lambda
  // arguments. For each lambda, we need to propagate from its own arguments
  // into its body. Once we have the body annotations, we can then merge it
  // just like any other expression argument.
  for (int i = 0; i < signature.NumConcreteArguments(); ++i) {
    if (signature.ConcreteArgument(i).IsLambda()) {
      const ResolvedNode* arg_i = GetFunctionCallArgument(function_call, i);
      if (arg_i->Is<ResolvedInlineLambda>()) {
        GOOGLESQL_RET_CHECK(signature.ConcreteArgument(i).IsLambda());
        const auto& concrete_lambda = signature.ConcreteArgument(i).lambda();
        const auto* lambda = arg_i->GetAs<ResolvedInlineLambda>();

        GOOGLESQL_RET_CHECK_EQ(lambda->argument_list_size(),
                     concrete_lambda.argument_types().size());

        // The body has been re-resolved if necessary, and fully annotated.
        // We only need to look at the body's return type as it may affect the
        // function's result annotations.
        // Propagate from the lambda's return type into the corresponding slots.
        // For example, ARRAY_TRANSFORM(arr1, e -> x), we need to propagate the
        // annotations from `x` into the output array.
        GOOGLESQL_RETURN_IF_ERROR(PropagateThroughCompositeType(
                            lambda->body()->annotated_type(),
                            concrete_lambda.body_type().original_kind(),
                            merging_map)
                            .status());
      }
    }
  }

  // Third pass, build from the merged annotations into the output type's map.
  GOOGLESQL_ASSIGN_OR_RETURN(const AnnotationMap* merged_result_type_annotations,
                   PropagateThroughCompositeType(
                       AnnotatedType(signature.result_type().type(),
                                     /*annotation_map=*/nullptr),
                       signature.result_type().original_kind(), merging_map));

  return MergeAnnotations(merged_result_type_annotations,
                          *result_annotation_map);
}

absl::Status DefaultAnnotationSpec::CheckAndPropagateForColumnRef(
    const ResolvedColumnRef& column_ref, AnnotationMap* result_annotation_map) {
  if (result_annotation_map == nullptr) return absl::OkStatus();
  return MergeAnnotations(column_ref.column().type_annotation_map(),
                          *result_annotation_map);
}

absl::Status DefaultAnnotationSpec::CheckAndPropagateForGetStructField(
    const ResolvedGetStructField& get_struct_field,
    AnnotationMap* result_annotation_map) {
  if (result_annotation_map == nullptr) return absl::OkStatus();
  const AnnotationMap* struct_annotation_map =
      get_struct_field.expr()->type_annotation_map();
  if (struct_annotation_map != nullptr) {
    GOOGLESQL_RET_CHECK(struct_annotation_map->IsStructMap());
    int field_idx = get_struct_field.field_idx();
    GOOGLESQL_RET_CHECK_LT(field_idx, struct_annotation_map->AsStructMap()->num_fields());
    GOOGLESQL_RETURN_IF_ERROR(
        MergeAnnotations(struct_annotation_map->AsStructMap()->field(field_idx),
                         *result_annotation_map));
  }
  return absl::OkStatus();
}

absl::Status DefaultAnnotationSpec::CheckAndPropagateForMakeStruct(
    const ResolvedMakeStruct& make_struct,
    StructAnnotationMap* result_annotation_map) {
  if (result_annotation_map == nullptr) return absl::OkStatus();
  GOOGLESQL_RET_CHECK_EQ(result_annotation_map->num_fields(),
               make_struct.field_list_size());
  for (int i = 0; i < make_struct.field_list_size(); i++) {
    GOOGLESQL_RETURN_IF_ERROR(
        MergeAnnotations(make_struct.field_list(i)->type_annotation_map(),
                         *result_annotation_map->mutable_field(i)));
  }
  return absl::OkStatus();
}

absl::Status DefaultAnnotationSpec::CheckAndPropagateForSubqueryExpr(
    const ResolvedSubqueryExpr& subquery_expr,
    AnnotationMap* result_annotation_map) {
  if (result_annotation_map == nullptr) return absl::OkStatus();
  const ResolvedScan* subquery_scan = subquery_expr.subquery();
  GOOGLESQL_RET_CHECK_NE(subquery_scan, nullptr);
  if (subquery_expr.subquery_type() == ResolvedSubqueryExpr::ARRAY) {
    GOOGLESQL_RET_CHECK_EQ(subquery_scan->column_list_size(), 1);
    GOOGLESQL_RET_CHECK(result_annotation_map->IsArrayMap());
    GOOGLESQL_RET_CHECK(subquery_scan->column_list(0).type()->Equivalent(
        subquery_expr.type()->AsArray()->element_type()));
    result_annotation_map =
        result_annotation_map->AsStructMap()->mutable_field(0);
  } else if (subquery_expr.subquery_type() == ResolvedSubqueryExpr::SCALAR) {
    GOOGLESQL_RET_CHECK_EQ(subquery_scan->column_list_size(), 1);
    GOOGLESQL_RET_CHECK(
        subquery_scan->column_list(0).type()->Equivalent(subquery_expr.type()));
  } else {
    // No way to propagate annotations by default for existence/in/like
    // subqueries.
    return absl::OkStatus();
  }
  auto* annotation = subquery_scan->column_list(0).type_annotation_map();
  return MergeAnnotations(annotation, *result_annotation_map);
}

absl::Status DefaultAnnotationSpec::CheckAndPropagateForSetOperationScan(
    const ResolvedSetOperationScan& set_operation_scan,
    const std::vector<AnnotationMap*>& result_annotation_maps) {
  const int column_list_size = set_operation_scan.column_list_size();
  GOOGLESQL_RET_CHECK_EQ(column_list_size, result_annotation_maps.size());
  for (int item_index = 0;
       item_index < set_operation_scan.input_item_list_size(); ++item_index) {
    const auto* item = set_operation_scan.input_item_list(item_index);
    GOOGLESQL_RET_CHECK_EQ(item->output_column_list_size(), column_list_size);
    for (int i = 0; i < column_list_size; i++) {
      if (result_annotation_maps[i] == nullptr) continue;
      GOOGLESQL_RETURN_IF_ERROR(
          MergeAnnotations(item->output_column_list(i).type_annotation_map(),
                           *result_annotation_maps[i]))
          << "in column " << i + 1 << ", item " << item_index + 1
          << " of set operation scan";
    }
  }
  return absl::OkStatus();
}

absl::Status DefaultAnnotationSpec::CheckAndPropagateForRecursiveScan(
    const ResolvedRecursiveScan& recursive_scan,
    const std::vector<AnnotationMap*>& result_annotation_maps) {
  const int column_list_size = recursive_scan.column_list_size();
  GOOGLESQL_RET_CHECK_EQ(column_list_size, result_annotation_maps.size());
  int item_index = 0;
  for (const auto& item :
       {recursive_scan.non_recursive_term(), recursive_scan.recursive_term()}) {
    if (item == nullptr) {
      continue;
    }
    GOOGLESQL_RET_CHECK_EQ(item->output_column_list_size(), column_list_size);
    for (int i = 0; i < column_list_size; i++) {
      if (result_annotation_maps[i] == nullptr) continue;
      GOOGLESQL_RETURN_IF_ERROR(
          MergeAnnotations(item->output_column_list(i).type_annotation_map(),
                           *result_annotation_maps[i]))
          << "in column " << i + 1 << ", item " << item_index + 1
          << " of recursive scan";
    }
    item_index++;
  }
  return absl::OkStatus();
}

absl::Status DefaultAnnotationSpec::CheckAndPropagateForCast(
    const ResolvedCast& cast, AnnotationMap* result_annotation_map) {
  return absl::OkStatus();
}

absl::Status DefaultAnnotationSpec::ScalarMergeIfCompatible(
    const AnnotationMap* in, AnnotationMap& out) const {
  const SimpleValue* value_before = out.GetAnnotation(Id());
  const SimpleValue* value_in =
      in != nullptr ? in->GetAnnotation(Id()) : nullptr;

  if (value_before == nullptr) {
    // If <out> doesn't already have a value for Id(), copy the one that's in
    // <in>, if <in> does contain a value.
    if (value_in == nullptr) return absl::OkStatus();
    out.SetAnnotation(Id(), *value_in);
    return absl::OkStatus();
  }

  if (value_in == nullptr || !value_in->Equals(*value_before)) {
    // If both <out> and <in> have a value and they are not the same, it's an
    // error.
    return MakeSqlError() << Name()
                          << " conflict: " << AnnotationDebugString(in)
                          << " vs. " << AnnotationDebugString(&out);
  }
  return absl::OkStatus();
}

absl::Status DefaultAnnotationSpec::MergeAnnotations(
    const AnnotationMap* left, AnnotationMap& right) const {
  GOOGLESQL_RETURN_IF_ERROR(ScalarMergeIfCompatible(left, right));
  if (left == nullptr || !left->IsStructMap()) {
    return absl::OkStatus();
  }

  GOOGLESQL_RET_CHECK(right.IsStructMap()) << right.DebugString(Id());
  GOOGLESQL_RET_CHECK_EQ(left->AsStructMap()->num_fields(),
               right.AsStructMap()->num_fields());
  for (int i = 0; i < left->AsStructMap()->num_fields(); i++) {
    GOOGLESQL_RETURN_IF_ERROR(MergeAnnotations(left->AsStructMap()->field(i),
                                     *right.AsStructMap()->mutable_field(i)));
  }
  return absl::OkStatus();
}

std::string DefaultAnnotationSpec::Name() const {
  if (Id() <= static_cast<int>(AnnotationKind::kMaxBuiltinAnnotationKind)) {
    return GetAnnotationKindName(static_cast<AnnotationKind>(Id()));
  } else {
    return absl::StrCat("Annotation[", std::to_string(Id()), "]");
  }
}
}  // namespace googlesql
