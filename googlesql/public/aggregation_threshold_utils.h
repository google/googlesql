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

#ifndef GOOGLESQL_PUBLIC_AGGREGATION_THRESHOLD_UTILS_H_
#define GOOGLESQL_PUBLIC_AGGREGATION_THRESHOLD_UTILS_H_

#include <string>

#include "googlesql/public/analyzer_options.h"
#include "absl/container/flat_hash_map.h"
#include "googlesql/base/case.h"

namespace googlesql {
// Returns map of allowed options for ResolvedAggregationThresholdAggregateScan
// node. Make sure to update ResolvedAggregationThresholdAggregateScan when
// function name changes.
const absl::flat_hash_map<std::string, AllowedOptionProperties,
                          googlesql_base::StringViewCaseHash,
                          googlesql_base::StringViewCaseEqual>&
GetAllowedAggregationThresholdOptions();

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_AGGREGATION_THRESHOLD_UTILS_H_
