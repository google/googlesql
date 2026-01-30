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

#ifndef GOOGLESQL_REFERENCE_IMPL_COMMON_H_
#define GOOGLESQL_REFERENCE_IMPL_COMMON_H_

#include <memory>
#include <string>
#include <vector>

#include "googlesql/public/collator.h"
#include "googlesql/public/type.h"
#include "googlesql/resolved_ast/resolved_collation.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"

namespace googlesql {

// Returns a collation name from input <resolved_collation>.
absl::StatusOr<std::string>
GetCollationNameFromResolvedCollation(
    const ResolvedCollation& resolved_collation);

// Returns a GoogleSqlCollator from input <resolved_collation>.
absl::StatusOr<std::unique_ptr<const GoogleSqlCollator>>
GetCollatorFromResolvedCollation(const ResolvedCollation& resolved_collation);

// Returns a GoogleSqlCollator with the collation name from the input collation
// list with one collation. Returns nullptr when the collation_list is empty.
// Returns error when the collation list has more than one collation.
absl::StatusOr<std::unique_ptr<const GoogleSqlCollator>>
GetCollatorFromResolvedCollationList(
    absl::Span<const ResolvedCollation> collation_list);

// Returns a collator from a value representing a ResolvedCollation object.
// An error will be returned if the input <collation_value> cannot be converted
// to a ResolvedCollation object.
absl::StatusOr<std::unique_ptr<const GoogleSqlCollator>>
GetCollatorFromResolvedCollationValue(const Value& collation_value);

using CollatorList = std::vector<std::unique_ptr<const GoogleSqlCollator>>;

// Returns a list of GoogleSqlCollator based on collation information obtained
// from resolved function call.
absl::StatusOr<CollatorList> MakeCollatorList(
    absl::Span<const ResolvedCollation> collation_list);

}  // namespace googlesql

#endif  // GOOGLESQL_REFERENCE_IMPL_COMMON_H_
