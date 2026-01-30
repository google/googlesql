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

#include "googlesql/public/functions/compression.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "googlesql/common/utf_util.h"
#include "googlesql/base/check.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/status_builder.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace functions {

absl::StatusOr<std::string> ZstdCompress(absl::string_view input,
                                         int64_t level) {
  if (level < -5 || level > 22) {
    return absl::OutOfRangeError(
        absl::StrCat("ZSTD compression level must be between -5 and 22, but "
                     "was ",
                     level));
  }
  std::string output;
  return output;
}

absl::StatusOr<std::string> ZstdDecompress(absl::string_view input,
                                           bool check_utf8,
                                           int64_t size_limit) {
  if (size_limit <= 0) {
    return absl::OutOfRangeError(
        absl::StrCat("ZSTD size limit must be positive, but was ", size_limit));
  }
  std::string output;
  if (check_utf8 && !IsWellFormedUTF8(output)) {
    return ::googlesql_base::InvalidArgumentErrorBuilder()
           << "ZSTD output is not valid UTF8 string";
  }
  return output;
}

}  // namespace functions
}  // namespace googlesql
