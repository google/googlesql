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

#ifndef GOOGLESQL_COMMON_WARNING_SINK_H_
#define GOOGLESQL_COMMON_WARNING_SINK_H_

#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

#include "googlesql/public/deprecation_warning.pb.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/types/span.h"

namespace googlesql {

// A "sink" or collector object for SQL warnings. This class also provides
// de-duplication of warnings so that the same warning only occurs once per
// sql statement.
class WarningSink {
 public:
  // `consider_location` controls whether the warning location is considered
  // when deduplicating. When true, two warnings with the same code and message
  // but different locations will be considered different warnings and not
  // de-duplicated. This is useful when warnings will be displayed in a
  // graphical user interface or when warnings have fix suggestions attached.
  explicit WarningSink(bool consider_location);

  WarningSink(const WarningSink&) = delete;
  WarningSink& operator=(const WarningSink&) = delete;
  WarningSink(WarningSink&&) = default;
  WarningSink& operator=(WarningSink&&) = default;

  // Add a warning. If a warning with the same `kind` and `warning.message()` is
  // already added, this is a no-op.
  //
  // Returns an error status if adding the warning fails for some reason. The
  // returned status is notably not the warning itself.
  //
  // Usage:
  //
  //   GOOGLESQL_RETURN_IF_ERROR(warning_sink.AddWarning(
  //      DeprecationWarning::DONT_TOUCH_THE_HOB,
  //      MakeSqlErrorAt(ast_location) << "Touching hot things causes burns"));
  absl::Status AddWarning(DeprecationWarning::Kind kind, absl::Status warning);

  // Access the unique warnings added so far in no particular order.
  absl::Span<const absl::Status> warnings() const;

  // Clear the sink so that `warnings()` is empty and the next warning added
  // is guaranteed not to be a duplicate.
  void Reset();

 private:
  // Unique warnings set is keyed on kind, message, and location to
  // de-duplicate warnings.
  absl::flat_hash_set<
      std::tuple<DeprecationWarning::Kind, std::string, int64_t, std::string>>
      unique_warnings_;

  // Whether to include location in the uniqueness key.
  bool consider_location_;

  // Storage of record for warning statuses.
  std::vector<absl::Status> warnings_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_COMMON_WARNING_SINK_H_
