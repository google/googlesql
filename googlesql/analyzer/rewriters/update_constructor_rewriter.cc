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
#include "googlesql/resolved_ast/resolved_ast_helper.h"
#include "googlesql/resolved_ast/resolved_ast_rewrite_visitor.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/rewrite_utils.h"
#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "google/protobuf/descriptor.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace {

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

// Returns `WITH(<column> AS <value>, <body>)`, which evaluates `value` once and
// makes it the value of `column` inside `body`. See
// UpdateConstructorRewriteVisitor for why the rewrite binds columns this way.
std::unique_ptr<const ResolvedExpr> BindColumn(
    const ResolvedColumn& column, std::unique_ptr<const ResolvedExpr> value,
    std::unique_ptr<const ResolvedExpr> body) {
  std::vector<std::unique_ptr<const ResolvedComputedColumn>> assignments;
  assignments.push_back(MakeResolvedComputedColumn(column, std::move(value)));
  return MakeResolvedWithExpr(std::move(assignments), std::move(body));
}

// Returns whether the rewrite of `items` must bind `update_element_column` to
// `input`, so that `input` is evaluated only once. See
// UpdateConstructorRewriteVisitor.
absl::StatusOr<bool> ShouldBindElementColumn(
    absl::Span<const std::unique_ptr<const ResolvedUpdateFieldItem>> items,
    const ResolvedColumn& update_element_column, const ResolvedExpr& input) {
  for (const std::unique_ptr<const ResolvedUpdateFieldItem>& item : items) {
    // The outer REPLACE_FIELDS call already evaluates `input` once as its first
    // argument. If an item also needs the original value of a field (either to
    // bind `update_item_column` or to transform a repeated field for `*:`), the
    // rewritten expression must read from `input` a second time. When `input`
    // is a column reference, we can copy it directly; otherwise, we must bind
    // `input` to `update_element_column` so `input` is only evaluated once.
    // Copying it would also duplicate the columns it defines, e.g. in a
    // subquery.
    if (!input.Is<ResolvedColumnRef>() &&
        (item->update_item_column() != nullptr ||
         item->operation() == ResolvedUpdateFieldItem::UPDATE_MANY)) {
      return true;
    }
    // The update value reads the object.
    GOOGLESQL_ASSIGN_OR_RETURN(
        const absl::flat_hash_set<const ResolvedColumnRef*> free_refs,
        CollectFreeColumnRefs(*item->expr()));
    if (absl::c_any_of(free_refs,
                       [&update_element_column](const ResolvedColumnRef* ref) {
                         return ref->column() == update_element_column;
                       })) {
      return true;
    }
  }
  return false;
}

// UpdateConstructorRewriteVisitor rewrites an update constructor to
// REPLACE_FIELDS and ARRAY_TRANSFORM.
//
// Binding the columns of the UPDATE constructor
// ---------------------------------------------
// The update values can read two columns that the UPDATE constructor defines
// ((broken link)), both holding values from before the
// update:
//   - `update_element_column`: the object being updated, read through its
//     alias or its unqualified field names.
//   - `update_item_column`: the original value of the field being updated,
//     read through the field's name or by a nested braced constructor.
//
// REPLACE_FIELDS cannot define columns, so the rewrite has to give these
// columns their values in another way. Replacing each reference with the value
// does not work in general:
//   - The value is usually an expression (`<input>`, `<input>.a.b`), but a
//     reference in the parameter list of a subquery or lambda must stay a
//     column reference.
//   - Copying `<input>` into each reference would evaluate it more than once.
// So each of these columns that is read is bound with a WITH expression, which
// defines the column and evaluates its value once:
//
//   UPDATE (t.ks AS o) { int64_key_1: int64_key_1 + o.int64_val }
//   =>
//   WITH($element AS t.ks,
//        REPLACE_FIELDS($element,
//                       WITH($item AS $element.int64_key_1,
//                            $item + $element.int64_val) AS int64_key_1))
//
// The exception is `*:` on a path that ends with the repeated field. There
// `update_item_column` is the array element, which is the ARRAY_TRANSFORM
// lambda argument, so its references are replaced by references to the lambda
// argument instead.
//
// A column that no update value reads is not bound, so an UPDATE constructor
// whose update values read neither column rewrites to a plain REPLACE_FIELDS.
// The exception is `update_element_column` when the rewrite itself reads the
// object more than once, for `*:` items and for values that read
// `update_item_column`: it is then bound to `<input>`, unless `<input>` is a
// column reference, which can be copied.
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
  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedUpdateConstructor(
      std::unique_ptr<const ResolvedUpdateConstructor> node) override {
    ResolvedUpdateConstructorBuilder update_builder =
        ToBuilder(std::move(node));
    std::vector<std::unique_ptr<const ResolvedReplaceFieldItem>>
        replace_field_items;

    std::vector<std::unique_ptr<const ResolvedUpdateFieldItem>> items =
        update_builder.release_update_field_item_list();
    replace_field_items.reserve(items.size());

    // Bind `update_element_column` only if some update value reads it, or if
    // the rewrite would otherwise evaluate `input` more than once. See the
    // class comment.
    const ResolvedColumn& update_element_column =
        update_builder.update_element_column();
    std::unique_ptr<const ResolvedExpr> input = update_builder.release_expr();
    GOOGLESQL_ASSIGN_OR_RETURN(
        const bool bind_update_element_column,
        ShouldBindElementColumn(items, update_element_column, *input));
    GOOGLESQL_RET_CHECK(!bind_update_element_column ||
              update_element_column.IsInitialized());

    // `object` is how the rewritten expression reads the object being updated:
    // through `update_element_column` if it is bound, so that `input` is
    // evaluated only once, and otherwise as `input` itself.
    std::unique_ptr<const ResolvedExpr> object;
    if (bind_update_element_column) {
      object =
          MakeResolvedColumnRef(update_element_column, /*is_correlated=*/false);
    } else {
      object = std::move(input);
    }

    // Rewrite each ResolvedUpdateFieldItem into a ResolvedReplaceFieldItem of
    // the outer REPLACE_FIELDS: a `*:` item into ARRAY_TRANSFORM over the
    // repeated field, and any other item into its value, with
    // `update_item_column` bound to the original value of the field if read.
    for (std::unique_ptr<const ResolvedUpdateFieldItem>& update : items) {
      ResolvedUpdateFieldItemBuilder update_item_builder =
          ToBuilder(std::move(update));
      ResolvedColumn item_column;
      if (update_item_builder.update_item_column() != nullptr) {
        item_column = update_item_builder.update_item_column()->column();
      }
      if (update_item_builder.operation() ==
          ResolvedUpdateFieldItem::UPDATE_MANY) {
        // Handle *: operator by rewriting to ARRAY_TRANSFORM.
        GOOGLESQL_ASSIGN_OR_RETURN(
            std::unique_ptr<const ResolvedReplaceFieldItem> replaced_item,
            BuildReplaceFieldItemForUpdateMany(
                object.get(), update_item_builder.proto_field_path(),
                update_item_builder.release_expr(), item_column));
        replace_field_items.push_back(std::move(replaced_item));
      } else {
        // Standard UPDATE (single field replacement).
        GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> value_expr,
                         BindItemColumn(item_column, object.get(),
                                        update_item_builder.proto_field_path(),
                                        update_item_builder.release_expr()));
        replace_field_items.push_back(MakeResolvedReplaceFieldItem(
            std::move(value_expr), /*struct_index_path=*/{},
            update_item_builder.proto_field_path()));
      }
    }

    // REPLACE_FIELDS(<object>, ...)
    std::unique_ptr<ResolvedReplaceField> replace_field =
        MakeResolvedReplaceField(update_builder.type(), std::move(object),
                                 std::move(replace_field_items));
    replace_field->set_type_annotation_map(
        update_builder.type_annotation_map());
    if (!bind_update_element_column) {
      return replace_field;
    }
    // `replace_field` reads the object through `update_element_column`, so
    // define that column as the input:
    //   WITH(<update_element_column> AS <input>,
    //        REPLACE_FIELDS(<update_element_column>, ...))
    return BindColumn(/*column=*/update_element_column,
                      /*value=*/std::move(input),
                      /*body=*/std::move(replace_field));
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

  // Returns `value` with `item_column`, if it is initialized, bound to the
  // original value `root.<path>` of the field being updated:
  // `WITH(<item_column> AS root.<path>, <value>)`.
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>> BindItemColumn(
      const ResolvedColumn& item_column, const ResolvedExpr* root,
      absl::Span<const google::protobuf::FieldDescriptor* const> path,
      std::unique_ptr<const ResolvedExpr> value) {
    if (!item_column.IsInitialized()) {
      return value;
    }
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> original_value,
                     BuildFieldPathAccess(root, path));
    return BindColumn(item_column, std::move(original_value), std::move(value));
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
      std::unique_ptr<const ResolvedExpr> value_expr,
      const ResolvedColumn& item_column) {
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

    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> array_transform,
                     BuildArrayTransform(root_expr, prefix, suffix,
                                         std::move(value_expr), item_column));

    return MakeResolvedReplaceFieldItem(
        std::move(array_transform), /*struct_index_path=*/{},
        std::vector<const google::protobuf::FieldDescriptor*>(prefix.begin(),
                                                    prefix.end()));
  }

  // Helper to build the body of the ARRAY_TRANSFORM lambda.
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>> BuildUpdateExpr(
      const ResolvedExpr* root_expr,
      absl::Span<const google::protobuf::FieldDescriptor* const> path,
      std::unique_ptr<const ResolvedExpr> value_expr,
      const ResolvedColumn& item_column) {
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
                    root_expr, path, std::move(value_expr), item_column));
      return BuildReplaceField(root_expr, std::move(item));
    }

    // Base Case: No repeated fields in `path`. `root_expr` is the lambda
    // argument of the innermost ARRAY_TRANSFORM, i.e. the array element being
    // updated.
    if (item_column.IsInitialized() && path.empty()) {
      // The value being updated is the element itself, e.g. in
      // `rep *: rep + 1`, so references to `item_column` become references to
      // the lambda argument. No binding is needed.
      GOOGLESQL_RET_CHECK(root_expr->node_kind() == RESOLVED_COLUMN_REF);
      ColumnRefReplacer replacer(
          item_column.column_id(),
          root_expr->GetAs<ResolvedColumnRef>()->column());
      GOOGLESQL_ASSIGN_OR_RETURN(value_expr,
                       replacer.VisitAll<ResolvedExpr>(std::move(value_expr)));
    } else {
      // The value being updated is a field of the element, e.g. in
      // `rep.f *: f + 1`, so bind `item_column` to it.
      GOOGLESQL_ASSIGN_OR_RETURN(value_expr, BindItemColumn(item_column, root_expr, path,
                                                  std::move(value_expr)));
    }

    if (path.empty()) {
      return value_expr;
    }
    item = MakeResolvedReplaceFieldItem(
        std::move(value_expr), /*struct_index_path=*/{},
        std::vector<const google::protobuf::FieldDescriptor*>(path.begin(), path.end()));
    return BuildReplaceField(root_expr, std::move(item));
  }

  // Constructs the ARRAY_TRANSFORM function call.
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>> BuildArrayTransform(
      const ResolvedExpr* root_expr,
      absl::Span<const google::protobuf::FieldDescriptor* const> prefix_path,
      absl::Span<const google::protobuf::FieldDescriptor* const> suffix_path,
      std::unique_ptr<const ResolvedExpr> value_expr,
      const ResolvedColumn& item_column) {
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
                                     std::move(value_expr), item_column));

    // The update value can reference columns defined outside the lambda, e.g.
    // the object being updated in `UPDATE (p) { rep *: rep + p.int64_key_1 }`,
    // or the lambda argument of an enclosing ARRAY_TRANSFORM.
    // ResolvedInlineLambda requires those references to be correlated and
    // listed in its parameter list.
    std::vector<std::unique_ptr<const ResolvedColumnRef>> parameter_list;
    GOOGLESQL_RETURN_IF_ERROR(CollectSortUniqueColumnRefs(*lambda_body, parameter_list));
    std::erase_if(
        parameter_list,
        [&lambda_arg](const std::unique_ptr<const ResolvedColumnRef>& ref) {
          return ref->column() == lambda_arg;
        });
    GOOGLESQL_ASSIGN_OR_RETURN(lambda_body,
                     CorrelateColumnRefs(*lambda_body, {lambda_arg}));
    std::unique_ptr<const ResolvedInlineLambda> lambda =
        MakeResolvedInlineLambda({lambda_arg}, std::move(parameter_list),
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
