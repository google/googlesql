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

#ifndef GOOGLESQL_PUBLIC_ANNOTATION_DEFAULT_ANNOTATION_SPEC_H_
#define GOOGLESQL_PUBLIC_ANNOTATION_DEFAULT_ANNOTATION_SPEC_H_

#include <memory>
#include <string>
#include <vector>

#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/type.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace googlesql {

// This class provides default implementations for most CheckAndPropagate
// methods. Most AnnotationSpecs should inherit from this and customize behavior
// as needed.
class DefaultAnnotationSpec : public AnnotationSpec {
 public:
  ~DefaultAnnotationSpec() override = default;

  // Returns the name of the annotation.
  //
  // Default implementation:
  // - If this is a built-in implementation, returns the annotation kind name.
  // - Otherwise, returns Annotation[<Id()>].
  virtual std::string Name() const;

  // Reconciles annotations for templated functions within each templated
  // argument type, e.g. T1 (and the related ARRAY<T1>, RANGE<T1>, etc.)
  // TODO custom functions which do not express template arg
  // correlation through the usual framework (e.g. some complex MAP or JSON
  // functions which rely on structs sharing some fields, etc), we should still
  // have specific annotations on the FunctionArgTypes and the
  // FunctionSignature, or conversely on the AnnotationSpec itself, to still
  // have structure in the way they merge annotations across each templated arg.
  absl::Status CheckAndPropagateForFunctionCallBase(
      const ResolvedFunctionCallBase& function_call,
      AnnotationMap* result_annotation_map) override;

  // These functions copy the annotation from their source to the result
  // annotation map. If the annotation is already set, they raise an error.
  // <result_annotation_map> may be nullptr for any of these functions, in which
  // case they simply return.
  // If <result_annotation_map> is non-null, it's an error if its structure
  // doesn't match the implied structure of the ResolvedNode. For example, in
  // the case of CheckAndPropagateForMakeStruct, <result_annotation_map> must
  // be a StructAnnotationMap.
  absl::Status CheckAndPropagateForColumnRef(
      const ResolvedColumnRef& column_ref,
      AnnotationMap* result_annotation_map) override;
  absl::Status CheckAndPropagateForGetStructField(
      const ResolvedGetStructField& get_struct_field,
      AnnotationMap* result_annotation_map) override;
  absl::Status CheckAndPropagateForMakeStruct(
      const ResolvedMakeStruct& make_struct,
      StructAnnotationMap* result_annotation_map) override;

  // Drops all annotations as we are casting to a new type. Subclasses can
  // override this behavior if they want to annotate the output.
  absl::Status CheckAndPropagateForCast(
      const ResolvedCast& cast, AnnotationMap* result_annotation_map) override;

  // If the subquery is an array subquery, copies the element type of its
  // annotation map to <result_annotation_map>. Otherwise, copies the annotation
  // map of the subquery's column to <result_annotation_map>.
  absl::Status CheckAndPropagateForSubqueryExpr(
      const ResolvedSubqueryExpr& subquery_expr,
      AnnotationMap* result_annotation_map) override;

  // Merges the AnnotationMaps for each column in the scan by successively
  // calling ScalarMergeIfCompatible on their components.
  absl::Status CheckAndPropagateForSetOperationScan(
      const ResolvedSetOperationScan& set_operation_scan,
      const std::vector<AnnotationMap*>& result_annotation_maps) override;

  // Merges the AnnotationMaps for each column in the recursive scan by
  // successively calling ScalarMergeIfCompatible on their components.
  absl::Status CheckAndPropagateForRecursiveScan(
      const ResolvedRecursiveScan& recursive_scan,
      const std::vector<AnnotationMap*>& result_annotation_maps) override;

 protected:
  // Merges two scalar annotations, placing the result in 'out'.
  // This function should not call AsStructMap or AsArrayMap on either 'in' or
  // 'out'. The work of traversing struct map or array map structures is
  // handled by MergeAnnotations, which calls this.
  //
  // Default behavior:
  // - Copies <in>'s setting for this annotation to <out>. If <in> is null or
  //   lacks an annotation for Id(), does nothing.
  // - Reports an error if both <out> and <in> have the annotation and the
  //   annotation value is different.
  virtual absl::Status ScalarMergeIfCompatible(const AnnotationMap* in,
                                               AnnotationMap& out) const;

  // Recursively calls ScalarMergeIfCompatible on the <left> and <right>.
  absl::Status MergeAnnotations(const AnnotationMap* left,
                                AnnotationMap& right) const;

  std::string AnnotationDebugString(const AnnotationMap* a) const {
    return a == nullptr ? "<null>" : a->DebugString(Id());
  }

  // Propagates the annotation from `annotation_map` across all contained types
  // to achieve the a merged annotation "supertype" per-templated arg kind,
  // e.g. ARG_TYPE_ANY_1, ARG_ARRAY_TYPE_ANY_1, etc, to reconcile annotations
  // of the related "group" scattered across the arguments.
  // `type` is used only to create AnnotationMaps as we build up the result.
  //
  // This function is the core of the templated propagation algorithm. It is
  // called twice, representing 2 passes:
  // 1. In the first pass, we merge annotation maps from all the arguments and
  //    into the appropriate argument kind slots (e.g. ARG_TYPE_ANY_1, etc.),
  //    based on the argument's original kind and its related component
  //    argument kinds (e.g. ARG_ARRAY_TYPE_ANY_3 needs to also merge its
  //    child annotation map into the slot for ARG_TYPE_ANY_3).
  // 2. In the second pass, we simply retrieve (and build up, as necessary)
  //    the AnnotationMap merged for the argument kind slot corresponding to
  //    the output's original argument kind.
  //
  //   Returns the merged AnnotationMap at the current level. This is used both
  //   to link parent StructAnnotationMap (e.g. that of ARG_ARRAY_ANY_1) to its
  //   child AnnotationMap (the one merged for ARG_TYPE_ANY_1).
  absl::StatusOr<const AnnotationMap*> PropagateThroughCompositeType(
      AnnotatedType annotated_type, SignatureArgumentKind original_kind,
      absl::flat_hash_map<SignatureArgumentKind,
                          std::unique_ptr<AnnotationMap>>& merging_map) const;
};

// Helper function to retrieve argument `i` from `function_call`, removing
// the need to distinguish between `argument_list` and
// `generic_argument_list`.
const ResolvedNode* GetFunctionCallArgument(
    const ResolvedFunctionCallBase& function_call, int i);

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_ANNOTATION_DEFAULT_ANNOTATION_SPEC_H_
