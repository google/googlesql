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

#ifndef GOOGLESQL_PUBLIC_FUNCTIONS_COMPRESSION_H_
#define GOOGLESQL_PUBLIC_FUNCTIONS_COMPRESSION_H_

#include <cstdint>
#include <string>

#include "googlesql/base/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace googlesql {
namespace functions {

absl::StatusOr<std::string> ZstdCompress(absl::string_view input,
                                         int64_t level = 3);
absl::StatusOr<std::string> ZstdDecompress(absl::string_view input,
                                           bool check_utf8,
                                           int64_t size_limit = 1 << 30);

}  // namespace functions
}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_FUNCTIONS_COMPRESSION_H_
