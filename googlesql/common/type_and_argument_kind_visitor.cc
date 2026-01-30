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
          {ARG_TYPE_ANY_1, {}},
          {ARG_TYPE_ANY_2, {}},
          {ARG_TYPE_ANY_3, {}},
          {ARG_TYPE_ANY_4, {}},
          {ARG_TYPE_ANY_5, {}},

          // These are just like ANY_i, but add a further restriction on the
          // particular type of the arg (e.g. proto, struct, enum).
          {ARG_STRUCT_ANY, {}},
          {ARG_ENUM_ANY, {}},
          {ARG_PROTO_ANY, {}},

          // Even though PROTO_MAP_ANY is related to PROTO_MAP_KEY_ANY and
          // PROTO_MAP_VALUE_ANY, the type itself is not modeled as a
          // composite type in the GoogleSQL type system proper (e.g., its
          // AnnotationMap will not have components.)
          {ARG_PROTO_MAP_ANY, {}},
          // Those, however, act just like ANY_i, and have no further component
          // themselves.
          {ARG_PROTO_MAP_KEY_ANY, {}},
          {ARG_PROTO_MAP_VALUE_ANY, {}},

          // ARRAY_TYPE_ANY_i => { ARRAY_TYPE_ANY_i }
          {ARG_ARRAY_TYPE_ANY_1, {ARG_TYPE_ANY_1}},
          {ARG_ARRAY_TYPE_ANY_2, {ARG_TYPE_ANY_2}},
          {ARG_ARRAY_TYPE_ANY_3, {ARG_TYPE_ANY_3}},
          {ARG_ARRAY_TYPE_ANY_4, {ARG_TYPE_ANY_4}},
          {ARG_ARRAY_TYPE_ANY_5, {ARG_TYPE_ANY_5}},

          // Similar to arrays..
          {ARG_MEASURE_TYPE_ANY_1, {ARG_TYPE_ANY_1}},
          {ARG_RANGE_TYPE_ANY_1, {ARG_TYPE_ANY_1}},

          // MAP_TYPE_ANY_1_2 relates to its indicated components.
          {ARG_MAP_TYPE_ANY_1_2, {ARG_TYPE_ANY_1, ARG_TYPE_ANY_2}},
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
    case ARG_MAP_TYPE_ANY_1_2:
      return true;
    case ARG_TYPE_FIXED:
    case ARG_TYPE_ARBITRARY:
    case ARG_TYPE_GRAPH_NODE:
    case ARG_TYPE_GRAPH_EDGE:
    case ARG_TYPE_GRAPH_ELEMENT:
    case ARG_TYPE_GRAPH_PATH:
      return false;
    // Non-scalars should never be passed to this function.
    case ARG_TYPE_LAMBDA:
    case ARG_TYPE_GRAPH:
    case ARG_TYPE_VOID:
    case ARG_TYPE_MODEL:
    case ARG_TYPE_CONNECTION:
    case ARG_TYPE_DESCRIPTOR:
    case ARG_TYPE_SEQUENCE:
    case ARG_TYPE_RELATION:
    default:
      // We should never hit this path. Any argument kind must be handled
      // explicitly above.
      GOOGLESQL_RET_CHECK_FAIL() << "Unexpected SignatureArgumentKind: "
                       << FunctionArgumentType::SignatureArgumentKindToString(
                              kind);
  }
}

}  // namespace googlesql
