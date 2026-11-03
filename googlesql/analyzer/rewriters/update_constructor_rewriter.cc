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

#include "googlesql/analyzer/rewriters/update_constructor_rewriter.h"

#include <cstddef>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/atomic_sequence_num.h"
#include "googlesql/analyzer/substitute.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output_properties.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/proto_util.h"
#include "googlesql/public/rewriter_interface.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/column_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_builder.h"
#include "googlesql/resolved_ast/resolved_ast_deep_copy_visitor.h"
#include "googlesql/resolved_ast/resolved_ast_rewrite_visitor.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "google/protobuf/descriptor.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace {

// Placeholder column ID for an update item whose value is not a nested
// ResolvedUpdateConstructor.
constexpr int kNoPlaceholderColumnId = -1;

// Helper visitor that replaces references to one column with references to
// another column, keeping `is_correlated`. Used to replace references to the
// placeholder column of a repeated field's element with references to the
// ARRAY_TRANSFORM lambda argument.
class ColumnRefReplacer : public ResolvedASTRewriteVisitor {
 public:
  ColumnRefReplacer(int replaced_column_id, const ResolvedColumn& replacement)
      : replaced_column_id_(replaced_column_id), replacement_(replacement) {}

  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedColumnRef(
      std::unique_ptr<const ResolvedColumnRef> node) override {
    if (node->column().column_id() == replaced_column_id_) {
      GOOGLESQL_RET_CHECK(node->type()->Equals(replacement_.type()));
      return MakeResolvedColumnRef(replacement_,
                                   /*is_correlated=*/node->is_correlated());
    }
    return node;
  }

 private:
  int replaced_column_id_;
  ResolvedColumn replacement_;
};

// UpdateConstructorRewriteVisitor rewrites an update constructor to
// REPLACE_FIELDS and ARRAY_TRANSFORM.
class UpdateConstructorRewriteVisitor : public ResolvedASTRewriteVisitor {
 public:
  UpdateConstructorRewriteVisitor(TypeFactory* type_factory, Catalog* catalog,
                                  const AnalyzerOptions& options,
                                  IdStringPool& id_string_pool,
                                  googlesql_base::SequenceNumber& sequence)
      : type_factory_(type_factory),
        catalog_(catalog),
        options_(options),
        column_factory_(/*max_seen_col_id=*/0, id_string_pool, sequence) {}

 private:
  // Store the loop column placeholder ID of child updates before they are
  // rewritten. This is needed because during Post-Visit execution, the children
  // have already been rewritten to their final shapes
  // (ARRAY_TRANSFORM/ReplaceField) and we can no longer access their original
  // loop column ID. We track placeholder IDs per update constructor using a
  // stack of vectors indexed by child item position.
  absl::Status PreVisitResolvedUpdateConstructor(
      const ResolvedUpdateConstructor& node) override {
    std::vector<int> placeholder_ids;
    placeholder_ids.reserve(node.update_field_item_list_size());
    for (const auto& update : node.update_field_item_list()) {
      if (update->expr()->Is<ResolvedUpdateConstructor>()) {
        placeholder_ids.push_back(update->expr()
                                      ->GetAs<ResolvedUpdateConstructor>()
                                      ->update_element_column()
                                      .column_id());
      } else {
        placeholder_ids.push_back(kNoPlaceholderColumnId);
      }
    }
    placeholder_ids_stack_.push_back(std::move(placeholder_ids));
    return absl::OkStatus();
  }

  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedUpdateConstructor(
      std::unique_ptr<const ResolvedUpdateConstructor> node) override {
    ResolvedUpdateConstructorBuilder update_builder =
        ToBuilder(std::move(node));
    std::vector<std::unique_ptr<const ResolvedReplaceFieldItem>>
        replace_field_items;

    const ResolvedExpr* root_expr = update_builder.expr();

    GOOGLESQL_RET_CHECK(!placeholder_ids_stack_.empty());
    std::vector<int> placeholder_ids = std::move(placeholder_ids_stack_.back());
    placeholder_ids_stack_.pop_back();
    std::vector<std::unique_ptr<const ResolvedUpdateFieldItem>> updates =
        update_builder.release_update_field_item_list();
    GOOGLESQL_RET_CHECK_EQ(placeholder_ids.size(), updates.size());

    for (size_t i = 0; i < updates.size(); ++i) {
      ResolvedUpdateFieldItemBuilder update_item_builder =
          ToBuilder(std::move(updates[i]));
      if (update_item_builder.operation() ==
          ResolvedUpdateFieldItem::UPDATE_MANY) {
        // Handle *: operator by rewriting to ARRAY_TRANSFORM.
        GOOGLESQL_ASSIGN_OR_RETURN(
            std::unique_ptr<const ResolvedReplaceFieldItem> replaced_item,
            BuildReplaceFieldItemForUpdateMany(
                root_expr, update_item_builder.proto_field_path(),
                update_item_builder.release_expr(), placeholder_ids[i]));
        replace_field_items.push_back(std::move(replaced_item));
      } else {
        // Standard UPDATE (single field replacement)
        replace_field_items.push_back(MakeResolvedReplaceFieldItem(
            update_item_builder.release_expr(), /*struct_index_path=*/{},
            update_item_builder.proto_field_path()));
      }
    }

    std::unique_ptr<ResolvedReplaceField> replace_field =
        MakeResolvedReplaceField(update_builder.type(),
                                 update_builder.release_expr(),
                                 std::move(replace_field_items));
    replace_field->set_type_annotation_map(
        update_builder.type_annotation_map());
    return replace_field;
  }

  // Returns `root.<path>`, as SQL `root.<path>` resolves it.
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>> BuildFieldPathAccess(
      const ResolvedExpr* root,
      absl::Span<const google::protobuf::FieldDescriptor* const> path) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> current_expr,
                     ResolvedASTDeepCopyVisitor::Copy(root));
    for (const google::protobuf::FieldDescriptor* field : path) {
      GOOGLESQL_RET_CHECK(current_expr->type()->IsProto());
      const Type* field_type;
      Value default_value;
      GOOGLESQL_RETURN_IF_ERROR(GetProtoFieldTypeAndDefault(
          ProtoFieldDefaultOptions::FromFieldAndLanguage(field,
                                                         options_.language()),
          field, current_expr->type()->AsProto()->CatalogNamePath(),
          type_factory_, &field_type, &default_value));
      current_expr = MakeResolvedGetProtoField(
          field_type, std::move(current_expr), field, default_value,
          /*get_has_bit=*/false, ProtoType::GetFormatAnnotation(field),
          /*return_default_value_when_unset=*/false);
    }
    return current_expr;
  }

  // Returns `REPLACE_FIELDS(<root_expr>, <item>)`. `root_expr` is copied
  // because the new node takes ownership of its children.
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>> BuildReplaceField(
      const ResolvedExpr* root_expr,
      std::unique_ptr<const ResolvedReplaceFieldItem> item) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> root_copy,
                     ResolvedASTDeepCopyVisitor::Copy(root_expr));
    std::vector<std::unique_ptr<const ResolvedReplaceFieldItem>> items;
    items.push_back(std::move(item));
    std::unique_ptr<ResolvedReplaceField> replace_field =
        MakeResolvedReplaceField(root_expr->type(), std::move(root_copy),
                                 std::move(items));
    replace_field->set_type_annotation_map(root_expr->type_annotation_map());
    return replace_field;
  }

  // This function (and its helpers) implements the core algorithm for rewriting
  // repeated field updates (`*:` operator). Since standard `REPLACE_FIELDS`
  // cannot modify fields inside array elements, we must rewrite this into a
  // combination of `REPLACE_FIELDS` and `ARRAY_TRANSFORM`.
  //
  // Conceptual Explanation:
  // -----------------------
  // To update fields inside an array, we split the target path at the first
  // repeated field encountered. The path up to this field is the `prefix`, and
  // the remaining path is the `suffix`. We extract the array located at the
  // `prefix` and apply `ARRAY_TRANSFORM` to it. The lambda of the
  // transformation receives each element of the array and updates it by
  // recursively calling this algorithm with the remaining `suffix` path.
  // This handles multiple nested repeated fields automatically. Finally, the
  // newly transformed array is placed back into the original structure at the
  // `prefix` location using `REPLACE_FIELDS`.
  //
  // Examples:
  //   a.b.c.rep *: 5
  //   a.b.c.rep.d.e *: 5
  //
  // Recursive Pseudocode:
  // ---------------------
  // func(root, path, new_value) {
  //     // 1. Identify the split point: path = prefix (up to array) + suffix
  //     // e.g. path=a.b.c.rep.d.e -> prefix = a.b.c.rep, suffix = d.e
  //     prefix, suffix = find_repeated_field(path)
  //
  //     // Base Case: No more nested arrays in the remaining path
  //     if (no repeated field in path) {
  //          if path is empty
  //              return new_value
  //          else
  //              return REPLACE_FIELDS(root, path, new_value)
  //     }
  //
  //     // Recursive Case: We found a repeated field (array) at 'prefix'
  //     array = root.prefix
  //
  //     // Create the transformation: for each element 'e' in the array,
  //     // recursively update 'e' using the remaining 'suffix' path.
  //     transformed_array = ARRAY_TRANSFORM(array, e -> func(e, suffix,
  //     new_value))
  //
  //     // We place the transformed array back into the root.
  //     return REPLACE_FIELDS(root, prefix, transformed_array)
  // }
  //
  // Note: The actual implementation is mutually recursive across this function
  // (`BuildReplaceFieldItemForUpdateMany`), `BuildUpdateExpr`, and
  // `BuildArrayTransform`.
  // Helper to build a ReplaceFieldItem for repeated field updates
  // (`*:` operator).
  absl::StatusOr<std::unique_ptr<const ResolvedReplaceFieldItem>>
  BuildReplaceFieldItemForUpdateMany(
      const ResolvedExpr* root_expr,
      absl::Span<const google::protobuf::FieldDescriptor* const> path,
      std::unique_ptr<const ResolvedExpr> value_expr, int placeholder_id) {
    auto it = absl::c_find_if(path, [](const google::protobuf::FieldDescriptor* fd) {
      return fd->is_repeated();
    });
    GOOGLESQL_RET_CHECK(it != path.end());

    const size_t prefix_len =
        static_cast<size_t>(std::distance(path.begin(), it)) + 1;
    absl::Span<const google::protobuf::FieldDescriptor* const> prefix =
        path.subspan(0, prefix_len);
    absl::Span<const google::protobuf::FieldDescriptor* const> suffix =
        path.subspan(prefix_len);

    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedExpr> array_transform,
        BuildArrayTransform(root_expr, prefix, suffix, std::move(value_expr),
                            placeholder_id));

    return MakeResolvedReplaceFieldItem(
        std::move(array_transform), /*struct_index_path=*/{},
        std::vector<const google::protobuf::FieldDescriptor*>(prefix.begin(),
                                                    prefix.end()));
  }

  // Helper to build the body of the ARRAY_TRANSFORM lambda.
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>> BuildUpdateExpr(
      const ResolvedExpr* root_expr,
      absl::Span<const google::protobuf::FieldDescriptor* const> path,
      std::unique_ptr<const ResolvedExpr> value_expr, int placeholder_id) {
    GOOGLESQL_RET_CHECK(root_expr != nullptr);
    // Identify the first repeated field in the path.
    auto it = absl::c_find_if(path, [](const google::protobuf::FieldDescriptor* fd) {
      return fd->is_repeated();
    });

    std::unique_ptr<const ResolvedReplaceFieldItem> item;
    if (it != path.end()) {
      // Recursive Case: There are more repeated fields in the path.
      GOOGLESQL_ASSIGN_OR_RETURN(
          item, BuildReplaceFieldItemForUpdateMany(
                    root_expr, path, std::move(value_expr), placeholder_id));
    } else {
      // Base Case: No repeated fields in `path`.
      // We are at the leaf of the nested loops.
      // The `value_expr` might contain a placeholder (ResolvedColumnRef)
      // that corresponds to the element of the innermost array we are
      // updating. Since we are now at the leaf, `root_expr` *is* (or contains)
      // that element.
      // `root_expr` is derived from the lambda argument of the enclosing
      // ARRAY_TRANSFORM.
      // So we must replace the placeholder ColumnRef in `value_expr` with
      // a ColumnRef to the lambda argument (root_col).
      GOOGLESQL_RET_CHECK(root_expr->node_kind() == RESOLVED_COLUMN_REF);
      const ResolvedColumn& root_col =
          root_expr->GetAs<ResolvedColumnRef>()->column();
      if (placeholder_id != kNoPlaceholderColumnId) {
        ColumnRefReplacer replacer(placeholder_id, root_col);
        GOOGLESQL_ASSIGN_OR_RETURN(
            value_expr, replacer.VisitAll<ResolvedExpr>(std::move(value_expr)));
      }

      if (path.empty()) {
        return value_expr;
      }
      item = MakeResolvedReplaceFieldItem(
          std::move(value_expr), /*struct_index_path=*/{},
          std::vector<const google::protobuf::FieldDescriptor*>(path.begin(),
                                                      path.end()));
    }
    return BuildReplaceField(root_expr, std::move(item));
  }

  // Constructs the ARRAY_TRANSFORM function call.
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>> BuildArrayTransform(
      const ResolvedExpr* root_expr,
      absl::Span<const google::protobuf::FieldDescriptor* const> prefix_path,
      absl::Span<const google::protobuf::FieldDescriptor* const> suffix_path,
      std::unique_ptr<const ResolvedExpr> value_expr, int placeholder_id) {
    GOOGLESQL_RET_CHECK(root_expr != nullptr);
    // Construct access to the array: root_expr.prefix
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> current_expr,
                     BuildFieldPathAccess(root_expr, prefix_path));
    const Type* current_type = current_expr->type();

    GOOGLESQL_RET_CHECK(current_type->IsArray());
    const Type* element_type = current_type->AsArray()->element_type();
    const AnnotationMap* element_annotation_map = nullptr;
    AnnotatedType array_annotated_type = current_expr->annotated_type();
    if (array_annotated_type.annotation_map != nullptr) {
      GOOGLESQL_RET_CHECK(array_annotated_type.annotation_map->IsStructMap())
          << array_annotated_type.annotation_map->DebugString();
      GOOGLESQL_ASSIGN_OR_RETURN(
          element_annotation_map,
          array_annotated_type.annotation_map->AsStructMap()->child(0));
    }
    AnnotatedType element_annotated_type(element_type, element_annotation_map);

    // Construct Lambda.
    ResolvedColumn lambda_arg =
        column_factory_.MakeCol("$lambda", "e", element_annotated_type);
    auto lambda_arg_ref =
        MakeResolvedColumnRef(lambda_arg, /*is_correlated=*/false);

    // Recursively build body.
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> lambda_body,
                     BuildUpdateExpr(lambda_arg_ref.get(), suffix_path,
                                     std::move(value_expr), placeholder_id));

    // TODO: b/475073854 - Support RHS referencing columns defined outside the
    // lambda (e.g. `repeated_int64_val *: t.key`) by passing them via
    // `parameter_list` and correlating their references in `lambda_body`.
    std::unique_ptr<const ResolvedInlineLambda> lambda =
        MakeResolvedInlineLambda({lambda_arg}, /*parameter_list=*/{},
                                 std::move(lambda_body));

    absl::flat_hash_map<std::string, const ResolvedExpr*> variables;
    variables["arr"] = current_expr.get();

    absl::flat_hash_map<std::string, const ResolvedInlineLambda*> lambdas;
    lambdas["my_lambda"] = lambda.get();

    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<ResolvedExpr> result,
        AnalyzeSubstitute(options_, *catalog_, *type_factory_,
                          "ARRAY_TRANSFORM(arr, e -> my_lambda(e))", variables,
                          lambdas),
        _.With(ExpectAnalyzeSubstituteSuccess));

    return result;
  }

  TypeFactory* type_factory_;
  Catalog* catalog_;
  const AnalyzerOptions& options_;
  ColumnFactory column_factory_;

  // Stack of child placeholder column IDs per ResolvedUpdateConstructor,
  // indexed by child item position in update_field_item_list.
  std::vector<std::vector<int>> placeholder_ids_stack_;
};

}  // namespace

class UpdateConstructorRewriter : public Rewriter {
 public:
  absl::StatusOr<std::unique_ptr<const ResolvedNode>> Rewrite(
      const AnalyzerOptions& options, std::unique_ptr<const ResolvedNode> input,
      Catalog& catalog, TypeFactory& type_factory,
      AnalyzerOutputProperties& output_properties) const override {
    GOOGLESQL_RET_CHECK(options.id_string_pool() != nullptr);
    GOOGLESQL_RET_CHECK(options.column_id_sequence_number() != nullptr);
    UpdateConstructorRewriteVisitor visitor(
        &type_factory, &catalog, options, *options.id_string_pool(),
        *options.column_id_sequence_number());
    return visitor.VisitAll(std::move(input));
  }

  std::string Name() const override { return "UpdateConstructorRewriter"; }
};

const Rewriter* GetUpdateConstructorRewriter() {
  static const auto* const kRewriter = new UpdateConstructorRewriter;
  return kRewriter;
}

}  // namespace googlesql
