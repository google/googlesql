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

#ifndef GOOGLESQL_COMMON_REFLECTION_HELPER_H_
#define GOOGLESQL_COMMON_REFLECTION_HELPER_H_

#include <string>

#include "googlesql/common/reflection.pb.h"

namespace googlesql {
namespace reflection {

// Format `result_table` into a string.
// This produces the formatted output for pipe DESCRIBE.
// If `include_table_schema` is false, skip the columns and table aliases.
std::string FormatResultTable(const reflection::ResultTable& result_table,
                              bool include_table_schema = true);

}  // namespace reflection
}  // namespace googlesql

#endif  // GOOGLESQL_COMMON_REFLECTION_HELPER_H_
