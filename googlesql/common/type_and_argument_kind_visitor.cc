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

#include "googlesql/common/type_and_argument_kind_visitor.h"

#include <vector>

#include "googlesql/public/function_signature.h"
#include "absl/base/no_destructor.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"

namespace googlesql {

namespace {

// This map largely mirrors Type::ComponentTypes, but for SignatureArgumentKind
// but only for arg kinds that indicate relationships across arguments of a
// function signature.
// The following kinds are *INTENTIONALLY* excluded from this map, as they
// should never be looked up here:
//  1. Argument kinds where the argument is unrelated to any others. They should
//     never be looked up as they do not merge annotations (compared to ANY_1,
//     for example). This exclusion protect against future bugs that may
//     mistakenly rely on the general path of templated/related kinds.
//     Note that these types may have component types, like GRAPH_NODE, but the
//     argument nevertheless acts like FIXED or ARBITRARY, just with the extra
//     requirement that it is a GRAPH_NODE.
//  2. LAMBDA and TABLE should *not* be looked up here, because they *do* have
//     component argument kinds, but they are not static and are described on
//     the FunctionArgumentType of the signature. For some signature.
//  3. VOID is only ever a return type, and never a value, so nothing is ever
//     propagated.
//  4. MODEL, CONNECTION, DESCRIPTOR and SEQUENCE are not values and cannot be
//     annotated.
//
//  This approach generalizes really well and should be used in
//  FunctionSignatureMatcher, which would also help keep these consistent.
static const absl::flat_hash_map<SignatureArgumentKind,
                                 std::vector<SignatureArgumentKind>>&
ComponentSignatureArgumentKindsMap() {
  static const absl::NoDestructor<absl::flat_hash_map<
      SignatureArgumentKind, std::vector<SignatureArgumentKind>>>
      component_signature_argument_kinds_map({
          // These arg kinds relate only to themselves and do not relate to
          // other
          // arg kinds.
          {ARG_KIND_EXPR_ANY_1, {}},
          {ARG_KIND_EXPR_ANY_2, {}},
          {ARG_KIND_EXPR_ANY_3, {}},
          {ARG_KIND_EXPR_ANY_4, {}},
          {ARG_KIND_EXPR_ANY_5, {}},

          // These are just like ANY_i, but add a further restriction on the
          // particular type of the arg (e.g. proto, struct, enum).
          {ARG_KIND_EXPR_STRUCT_ANY, {}},
          {ARG_KIND_EXPR_ENUM_ANY, {}},
          {ARG_KIND_EXPR_PROTO_ANY, {}},
          {ARG_KIND_EXPR_STRING_ANY, {}},
          {ARG_KIND_EXPR_ROW_ANY, {}},

          // Even though PROTO_MAP_ANY is related to PROTO_MAP_KEY_ANY and
          // PROTO_MAP_VALUE_ANY, the type itself is not modeled as a
          // composite type in the GoogleSQL type system proper (e.g., its
          // AnnotationMap will not have components.)
          {ARG_KIND_EXPR_PROTO_MAP_ANY, {}},
          // Those, however, act just like ANY_i, and have no further component
          // themselves.
          {ARG_KIND_EXPR_PROTO_MAP_KEY_ANY, {}},
          {ARG_KIND_EXPR_PROTO_MAP_VALUE_ANY, {}},

          // ARRAY_TYPE_ANY_i => { ARRAY_TYPE_ANY_i }
          {ARG_KIND_EXPR_ARRAY_ANY_1, {ARG_KIND_EXPR_ANY_1}},
          {ARG_KIND_EXPR_ARRAY_ANY_2, {ARG_KIND_EXPR_ANY_2}},
          {ARG_KIND_EXPR_ARRAY_ANY_3, {ARG_KIND_EXPR_ANY_3}},
          {ARG_KIND_EXPR_ARRAY_ANY_4, {ARG_KIND_EXPR_ANY_4}},
          {ARG_KIND_EXPR_ARRAY_ANY_5, {ARG_KIND_EXPR_ANY_5}},

          // Similar to arrays..
          {ARG_KIND_EXPR_MEASURE_ANY_1, {ARG_KIND_EXPR_ANY_1}},
          {ARG_KIND_EXPR_RANGE_ANY_1, {ARG_KIND_EXPR_ANY_1}},

          // MAP_TYPE_ANY_1_2 relates to its indicated components.
          {ARG_KIND_EXPR_MAP_ANY_1_2,
           {ARG_KIND_EXPR_ANY_1, ARG_KIND_EXPR_ANY_2}},
      });
  return *component_signature_argument_kinds_map;
}

}  // namespace

absl::StatusOr<absl::Span<const SignatureArgumentKind>>
GetComponentSignatureArgumentKinds(SignatureArgumentKind kind) {
  auto it = ComponentSignatureArgumentKindsMap().find(kind);
  GOOGLESQL_RET_CHECK(it != ComponentSignatureArgumentKindsMap().end());
  return it->second;
}

absl::StatusOr<bool> IsRelatedToOtherArguments(SignatureArgumentKind kind) {
  switch (kind) {
    case ARG_KIND_EXPR_STRUCT_ANY:
    case ARG_KIND_EXPR_PROTO_MAP_ANY:
    case ARG_KIND_EXPR_PROTO_MAP_KEY_ANY:
    case ARG_KIND_EXPR_PROTO_MAP_VALUE_ANY:
    case ARG_KIND_EXPR_PROTO_ANY:
    case ARG_KIND_EXPR_ENUM_ANY:
    case ARG_KIND_EXPR_ANY_1:
    case ARG_KIND_EXPR_ANY_2:
    case ARG_KIND_EXPR_ANY_3:
    case ARG_KIND_EXPR_ANY_4:
    case ARG_KIND_EXPR_ANY_5:
    case ARG_KIND_EXPR_ARRAY_ANY_1:
    case ARG_KIND_EXPR_ARRAY_ANY_2:
    case ARG_KIND_EXPR_ARRAY_ANY_3:
    case ARG_KIND_EXPR_ARRAY_ANY_4:
    case ARG_KIND_EXPR_ARRAY_ANY_5:
    case ARG_KIND_EXPR_RANGE_ANY_1:
    case ARG_KIND_EXPR_MEASURE_ANY_1:
    case ARG_KIND_EXPR_MAP_ANY_1_2:
    case ARG_KIND_EXPR_ROW_ANY:
      return true;
    case ARG_KIND_EXPR_FIXED:
    case ARG_KIND_EXPR_ARBITRARY:
    case ARG_KIND_EXPR_GRAPH_NODE:
    case ARG_KIND_EXPR_GRAPH_EDGE:
    case ARG_KIND_EXPR_GRAPH_ELEMENT:
    case ARG_KIND_EXPR_GRAPH_PATH:
    case ARG_KIND_EXPR_STRING_ANY:
      return false;
    // Non-scalars should never be passed to this function.
    case ARG_KIND_LAMBDA:
    case ARG_KIND_GRAPH:
    case ARG_KIND_VOID:
    case ARG_KIND_MODEL:
    case ARG_KIND_CONNECTION:
    case ARG_KIND_DESCRIPTOR:
    case ARG_KIND_SEQUENCE:
    case ARG_KIND_RELATION:
    default:
      // We should never hit this path. Any argument kind must be handled
      // explicitly above.
      GOOGLESQL_RET_CHECK_FAIL() << "Unexpected SignatureArgumentKind: "
                       << FunctionArgumentType::SignatureArgumentKindToString(
                              kind);
  }
}

}  // namespace googlesql
