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

#ifndef GOOGLESQL_PUBLIC_ANNOTATION_COLLATION_H_
#define GOOGLESQL_PUBLIC_ANNOTATION_COLLATION_H_

#include <memory>
#include <vector>

#include "googlesql/parser/ast_node.h"
#include "googlesql/public/annotation/default_annotation_spec.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/type.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace googlesql {

// Collation allows users to specify alternative rules for comparing strings.
// This class defines rules to propagate collation annotation for ResolvedAst
// nodes. Check comments on each method for propagation behavior on each kind of
// node.
class CollationAnnotation : public DefaultAnnotationSpec {
 public:
  CollationAnnotation() = default;

  static int GetId() { return static_cast<int>(AnnotationKind::kCollation); }

  int Id() const override { return GetId(); }

  // Collation assigns an annotation to the output of a cast if the target type
  // has collation. It drops the input's collation.
  absl::Status CheckAndPropagateForCast(
      const ResolvedCast& cast, AnnotationMap* result_annotation_map) override;

  // Determines whether collation should be propagated to the function's result,
  // as defined by the function call's signature.  If appropriate, propagates
  // collation from the function argument(s) to <result_annotation_map>.
  // Currently only supports default collation propagation, which returns an
  // error if multiple arguments have different collation, or returns the
  // common collation otherwise.
  absl::Status CheckAndPropagateForFunctionCallBase(
      const ResolvedFunctionCallBase& function_call,
      AnnotationMap* result_annotation_map) override;

  // Returns false when <map> is nullptr or CollationAnnotation is not
  // present in <map> or any of its nested AnnotationMaps.
  static bool ExistsIn(const AnnotationMap* map) {
    return map != nullptr && map->Has<CollationAnnotation>();
  }

  // Create an AnnotationMap with the input <type> and then set its
  // collation annotation by merging collation annotations from input
  // <annotation_maps>. Returns the result annotation map on success, and
  // returns error otherwise.
  //
  // Note that the output annotation map only contains collation annotation and
  // it is not normalized.
  absl::StatusOr<std::unique_ptr<AnnotationMap>> GetCollationFromAnnotationMaps(
      const Type* type,
      const std::vector<const AnnotationMap*>& annotation_maps);

  // Validates that all collations present on function arguments (if any) are
  // compatible, and returns that collation. Only function arguments which have
  // the option argument_collation_mode matching the <collation_mode_mask> are
  // considered. Returns nullptr to indicate no (non-default) collation. Throws
  // an error if function arguments have different collations.  If non-null,
  // <error_location> is used for error messages.
  absl::StatusOr<std::unique_ptr<AnnotationMap>>
  GetCollationFromFunctionArguments(
      const ASTNode* error_location,
      const ResolvedFunctionCallBase& function_call,
      FunctionEnums::ArgumentCollationMode collation_mode_mask);

  // Resolves the collation for ORDER BY item.
  static absl::Status ResolveCollationForResolvedOrderByItem(
      ResolvedOrderByItem* resolved_order_by_item);

  // Throws error if any function argument has collation annotation.
  static absl::Status RejectsCollationOnFunctionArguments(
      const ResolvedFunctionCallBase& function_call);

  // Similar to the implementation in the parent class, except it's not an error
  // if 'in' is NULL when 'out' has our annotation.
  absl::Status ScalarMergeIfCompatible(const AnnotationMap* in,
                                       AnnotationMap& out) const override;
};

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_ANNOTATION_COLLATION_H_
