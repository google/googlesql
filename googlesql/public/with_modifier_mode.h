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

#ifndef GOOGLESQL_PUBLIC_WITH_MODIFIER_MODE_H_
#define GOOGLESQL_PUBLIC_WITH_MODIFIER_MODE_H_

#include "absl/strings/string_view.h"

namespace googlesql {

// This enum describes the mode introduced by SELECT WITH <mode> and the pipe
// AGGREGATE WITH <mode> clauses.
enum class WithModifierMode {
  // Represents regular query.
  NONE,
  // Represents SELECT WITH ANONYMIZATION query - Differential Privacy query.
  // Although ANONYMIZATION and DIFFERENTIAL_PRIVACY are semantically similar in
  // most ways, there are a couple of differences e.g. list of valid options are
  // different and so we need to be able to distinguish between them.
  // The SELECT WITH ANONYMIZATION clause is deprecated and will hopefully be
  // removed in the future. As such, ANONYMIZATION is not supported in the pipe
  // syntax and should not support any new features.
  ANONYMIZATION,
  // Represents SELECT WITH DIFFERENTIAL_PRIVACY query and pipe AGGREGATE WITH
  // DIFFERENTIAL_PRIVACY operator - Differential Privacy query. See comment
  // above for why both ANONYMIZATION and DIFFERENTIAL_PRIVACY exist.
  DIFFERENTIAL_PRIVACY,
  // Represents SELECT WITH AGGREGATION_THRESHOLD query.
  AGGREGATION_THRESHOLD,
};

inline constexpr absl::string_view WithModifierModeToString(
    WithModifierMode with_modifier_mode) {
  switch (with_modifier_mode) {
    case WithModifierMode::NONE:
      return "NONE";
    case WithModifierMode::ANONYMIZATION:
      return "ANONYMIZATION";
    case WithModifierMode::DIFFERENTIAL_PRIVACY:
      return "DIFFERENTIAL_PRIVACY";
    case WithModifierMode::AGGREGATION_THRESHOLD:
      return "AGGREGATION_THRESHOLD";
    default:
      return "UNKNOWN";
  }
}

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_WITH_MODIFIER_MODE_H_
