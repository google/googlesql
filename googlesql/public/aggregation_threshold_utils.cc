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

#include "googlesql/public/aggregation_threshold_utils.h"

#include <string>

#include "googlesql/proto/options.pb.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/types/type_factory.h"
#include "absl/container/flat_hash_map.h"
#include "googlesql/base/case.h"
#include "googlesql/base/no_destructor.h"

namespace googlesql {
const absl::flat_hash_map<std::string, AllowedOptionProperties,
                          googlesql_base::StringViewCaseHash,
                          googlesql_base::StringViewCaseEqual>&
GetAllowedAggregationThresholdOptions() {
  static const googlesql_base::NoDestructor<absl::flat_hash_map<
      std::string, AllowedOptionProperties, googlesql_base::StringViewCaseHash,
      googlesql_base::StringViewCaseEqual>>
      aggregation_threshold_options(
          {{"threshold", {types::Int64Type()}},
           {"max_groups_contributed", {types::Int64Type()}},
           {"max_rows_contributed", {types::Int64Type()}},
           {"privacy_unit_column",
            {nullptr, AllowedHintsAndOptionsProto::OptionProto::
                          FROM_NAME_SCOPE_IDENTIFIER}}});
  return *aggregation_threshold_options;
}
}  // namespace googlesql
