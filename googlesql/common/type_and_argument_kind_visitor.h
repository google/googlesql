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

#ifndef GOOGLESQL_COMMON_TYPE_AND_ARGUMENT_KIND_VISITOR_H_
#define GOOGLESQL_COMMON_TYPE_AND_ARGUMENT_KIND_VISITOR_H_

#include <vector>

#include "googlesql/public/function.pb.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/types/annotation.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

// Returns the component argument kinds related to a given argument kind,
// for example, returns {ARG_TYPE_ANY_1} for ARG_ARRAY_TYPE_ANY_1.
// This must be called only for arg kinds that indicate relationships across
// arguments of a function signature. So an ARG_TYPE_FIXED that is a struct
// in some invocation is still unrelated to any other arguments and does not
// appear in this map. Retrieval will result in a GOOGLESQL_RET_CHECK.
//
// The following kinds are *INTENTIONALLY* excluded from this map, as they
// should never be looked up here:
//  1. Argument kinds where the argument is unrelated to any others. They should
//     never be looked up as they do not merge annotations (compared to ANY_1,
//     for example). This exclusion protect against future bugs that may
//     mistakenly rely on the general path of templated/related kinds.
//     Note that these types may have component types, like GRAPH_NODE, but the
//     argument nevertheless acts like FIXED or ARBITRARY, just with the extra
//     requirement that it is a GRAPH_NODE.
//  2. LAMBDA, because the ARG_TYPE_LAMBDA kind itself does not statically
//     relate to other argument kinds. Instead, the propagation logic checks the
//     FunctionArgumentTypes of this particular invocation.
//  2. VOID is only ever a return type, and never a value, so nothing is ever
//     propagated.
//  3. The other TVF argument kinds, RELATION, MODEL, CONNECTION, DESCRIPTOR and
//     SEQUENCE are not values and cannot be annotated.
//
//  This approach generalizes really well and should be used in
//  FunctionSignatureMatcher, which would also help keep these consistent.
absl::StatusOr<absl::Span<const SignatureArgumentKind>>
GetComponentSignatureArgumentKinds(SignatureArgumentKind kind);

// This visitor recurses through the components of the given `annotated_type`
// according to the original templated SignatureArgumentKind.
// This is the reliable way to propagate information (e.g. annotation
// propagation) between related templated arguments, including their component
// arguments (e.g. ARG_MAP_TYPE_ANY_1_2 is related to ARG_TYPE_ANY_1 and
// ARG_TYPE_ANY_2).
// At each step, the visitor calls the virtual `Process` method.
template <typename T>
class AnnotatedTypeAndSignatureArgumentKindVisitor {
 public:
  virtual ~AnnotatedTypeAndSignatureArgumentKindVisitor() = default;

  // Called at each component type which could be related to other arguments,
  // including the top-level (the type at the entry point), before recursing
  // into any components.
  virtual absl::Status PreVisitChildren(
      AnnotatedType annotated_type, SignatureArgumentKind original_kind) = 0;

  // Called at each component type which could be related to other arguments,
  // including the top-level (the type at the entry point), after recursing
  // into any components.
  virtual absl::StatusOr<T> PostVisitChildren(
      AnnotatedType annotated_type, SignatureArgumentKind original_kind,
      std::vector<T> component_results) = 0;

  // This method has the core dispatch logic. It is intentionally marked as
  // non-virtual. All custom logic has to be in the virtual methods.
  absl::StatusOr<T> Visit(AnnotatedType annotated_type,
                          SignatureArgumentKind original_kind) {
    const auto& [type, input_annotation_map] = annotated_type;

    switch (original_kind) {
      // These kinds are unrelated to any other arguments, there's nothing to
      // propagate or merge.
      case ARG_TYPE_FIXED:
      case ARG_TYPE_ARBITRARY:

      // Similarly, graph element args are not related to other args.
      case ARG_TYPE_GRAPH_NODE:
      case ARG_TYPE_GRAPH_EDGE:
      case ARG_TYPE_GRAPH_ELEMENT:
      case ARG_TYPE_GRAPH_PATH:

      // These are not values and do not have annotations to propagate.
      case ARG_TYPE_VOID:
      case ARG_TYPE_MODEL:
      case ARG_TYPE_CONNECTION:
      case ARG_TYPE_DESCRIPTOR:
      case ARG_TYPE_SEQUENCE:
      case ARG_TYPE_RELATION:
        // Nothing to do or propagate, return directly.
        return nullptr;

      // ARG_TYPE_LAMBDA is not directly related to other argument kinds.
      // Instead, the propagation logic needs to check the concrete
      // FunctionArgumentTypes of the lambda for this invocation.
      case ARG_TYPE_LAMBDA:
        GOOGLESQL_RET_CHECK_FAIL() << "ARG_TYPE_LAMBDA should not be passed here. The "
                            "caller should instead use the concrete "
                            "FunctionArgumentTypes on the lambda argument";

      // All of these can relate to other arguments.
      case ARG_STRUCT_ANY:
      case ARG_PROTO_MAP_ANY:
      case ARG_PROTO_MAP_KEY_ANY:
      case ARG_PROTO_MAP_VALUE_ANY:
      case ARG_PROTO_ANY:
      case ARG_ENUM_ANY:

      case ARG_TYPE_ANY_1:
      case ARG_TYPE_ANY_2:
      case ARG_TYPE_ANY_3:
      case ARG_TYPE_ANY_4:
      case ARG_TYPE_ANY_5:

      case ARG_ARRAY_TYPE_ANY_1:
      case ARG_ARRAY_TYPE_ANY_2:
      case ARG_ARRAY_TYPE_ANY_3:
      case ARG_ARRAY_TYPE_ANY_4:
      case ARG_ARRAY_TYPE_ANY_5:

      case ARG_RANGE_TYPE_ANY_1:
      case ARG_MEASURE_TYPE_ANY_1:
      case ARG_MAP_TYPE_ANY_1_2: {
        // Call the logic at the current level.
        GOOGLESQL_RETURN_IF_ERROR(PreVisitChildren(annotated_type, original_kind));

        // Recurse on any component arguments, if any.
        GOOGLESQL_ASSIGN_OR_RETURN(
            absl::Span<const SignatureArgumentKind> component_kinds,
            GetComponentSignatureArgumentKinds(original_kind));
        // Less-than-or-equal because ANY_i itself may be a component type, like
        // an ARRAY type. For example:
        // IFNULL(ANY_1, ANY_1) -> ANY_1 may be called on some ARRAY or a STRUCT
        // so ANY_1 itself is is a composite type in this case.
        GOOGLESQL_RET_CHECK_LE(component_kinds.size(), type->ComponentTypes().size())
            << "Original kind: "
            << FunctionArgumentType::SignatureArgumentKindToString(
                   original_kind)
            << ", type: " << type->DebugString();

        // This ArgKind is related to other component arguments.
        std::vector<T> component_results;
        component_results.reserve(component_kinds.size());
        for (int i = 0; i < component_kinds.size(); ++i) {
          GOOGLESQL_ASSIGN_OR_RETURN(
              T component_result,
              Visit(AnnotatedType(
                        type->ComponentTypes()[i],
                        input_annotation_map == nullptr
                            ? nullptr
                            : input_annotation_map->AsStructMap()->field(i)),
                    component_kinds[i]));
          component_results.push_back(std::move(component_result));
        }

        return PostVisitChildren(annotated_type, original_kind,
                                 std::move(component_results));
      }
      default:
        // We should never hit this path. Any argument kind must be handled
        // explicitly above.
        GOOGLESQL_RET_CHECK_FAIL() << "Unexpected SignatureArgumentKind: "
                         << FunctionArgumentType::SignatureArgumentKindToString(
                                original_kind);
    }
  }
};

// Returns true if the given argument kind is related to other arguments,
// for example, ANY_1 relates to other arguments of the same kind, as well as
// ARRAY_ANY_1 and MAP_ANY_1_2. By contrast, ARBITRARY and FIXED are not
// related to other arguments.
// REQUIRES: the kind must be for scalar arguments. LAMBDA, RELATION, MODEL, etc
// should never be passed to this function.
absl::StatusOr<bool> IsRelatedToOtherArguments(SignatureArgumentKind kind);

}  // namespace googlesql

#endif  // GOOGLESQL_COMMON_TYPE_AND_ARGUMENT_KIND_VISITOR_H_
