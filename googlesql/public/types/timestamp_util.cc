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

#include "googlesql/public/types/timestamp_util.h"

#include <type_traits>

namespace googlesql::types {

absl::Time TimestampMaxBaseTime() {
  static_assert(std::is_trivially_destructible_v<absl::Time>);
  static const absl::Time kBaseTimeMax(
      absl::FromUnixMicros(types::kTimestampMax) + absl::Nanoseconds(999));
  return kBaseTimeMax;
}

}  // namespace googlesql::types
