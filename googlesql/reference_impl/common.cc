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

#include "googlesql/reference_impl/common.h"

#include <memory>
#include <string>
#include <utility>

#include "googlesql/public/collator.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_collation.h"
#include "googlesql/resolved_ast/serialization.pb.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

absl::StatusOr<std::string>
GetCollationNameFromResolvedCollation(
    const ResolvedCollation& resolved_collation) {
  GOOGLESQL_RET_CHECK(!resolved_collation.Empty())
      << "Cannot get collation name from empty ResolvedCollation.";
  // TODO: So far we only support to get collation name from
  // ResolvedCollation for String type. Will extend to get collation info for
  // other types such as Array and Struct.
  if (!resolved_collation.HasCompatibleStructure(types::StringType())) {
    return ::googlesql_base::InvalidArgumentErrorBuilder()
           << "Collation for non-String type is not supported: "
           << resolved_collation.DebugString();
  }
  GOOGLESQL_RET_CHECK(resolved_collation.HasCollation());
  return std::string(resolved_collation.CollationName());
}

absl::StatusOr<std::unique_ptr<const GoogleSqlCollator>>
GetCollatorFromResolvedCollation(const ResolvedCollation& resolved_collation) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::string collation_name,
                   GetCollationNameFromResolvedCollation(resolved_collation));
  return MakeSqlCollatorLite(collation_name);
}

absl::StatusOr<std::unique_ptr<const GoogleSqlCollator>>
GetCollatorFromResolvedCollationList(
    absl::Span<const ResolvedCollation> collation_list) {
  GOOGLESQL_RET_CHECK_LE(collation_list.size(), 1);
  if (collation_list.empty()) {
    return nullptr;
  }
  return GetCollatorFromResolvedCollation(collation_list[0]);
}

absl::StatusOr<std::unique_ptr<const GoogleSqlCollator>>
GetCollatorFromResolvedCollationValue(const Value& collation_value) {
  ResolvedCollationProto resolved_collation_proto;
  bool is_valid =
      resolved_collation_proto.ParsePartialFromCord(collation_value.ToCord());
  GOOGLESQL_RET_CHECK(is_valid)
      << "Failed to parse collation_value to ResolvedCollation proto: "
      << collation_value.ToCord();

  ResolvedCollation resolved_collation;
  GOOGLESQL_ASSIGN_OR_RETURN(resolved_collation,
                   ResolvedCollation::Deserialize(resolved_collation_proto));
  return GetCollatorFromResolvedCollation(resolved_collation);
}

absl::StatusOr<CollatorList> MakeCollatorList(
    absl::Span<const ResolvedCollation> collation_list) {
  CollatorList collator_list;

  if (collation_list.empty()) {
    return collator_list;
  }

  for (const ResolvedCollation& resolved_collation : collation_list) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const GoogleSqlCollator> collator,
                     GetCollatorFromResolvedCollation(resolved_collation));
    collator_list.push_back(std::move(collator));
  }

  return std::move(collator_list);
}

}  // namespace googlesql
