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

#include <string>

#include "googlesql/base/testing/status_matchers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"

namespace googlesql {
namespace functions {
namespace {

using ::testing::HasSubstr;
using ::absl_testing::StatusIs;

TEST(ZstdCompress, InvalidLevel) {
  EXPECT_THAT(
      ZstdCompress("test", -6),
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr("ZSTD compression level must be between -5 and "
                         "22, but was -6")));
  EXPECT_THAT(
      ZstdCompress("test", 23),
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr("ZSTD compression level must be between -5 and "
                         "22, but was 23")));
}

TEST(ZstdDecompress, InvalidSizeLimit) {
  EXPECT_THAT(
      ZstdDecompress("test", /*check_utf8=*/false, /*size_limit=*/0),
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr("ZSTD size limit must be positive, but was 0")));
  EXPECT_THAT(
      ZstdDecompress("test", /*check_utf8=*/false, /*size_limit=*/-1),
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr("ZSTD size limit must be positive, but was -1")));
}

}  // namespace
}  // namespace functions
}  // namespace googlesql
