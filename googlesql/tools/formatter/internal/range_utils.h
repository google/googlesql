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

#ifndef GOOGLESQL_TOOLS_FORMATTER_INTERNAL_RANGE_UTILS_H_
#define GOOGLESQL_TOOLS_FORMATTER_INTERNAL_RANGE_UTILS_H_

#include <set>
#include <vector>

#include "googlesql/public/formatter_options.h"
#include "googlesql/public/parse_location.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace googlesql::formatter::internal {

// Adjusts the given sorted byte ranges if needed, so that each range starts
// from a statement start and ends at a statement end. Merges ranges together if
// they appear inside the same statement. If a range is in the middle, exclude
// leading and trailing blank lines from the range to make sure they are
// preserved in the output. If the resulting range includes all statements in
// `sql`, expands the range to the entire input (including leading and trailing
// spaces).
// If allowed_ranges are provided, a range is only expanded if it's contained in
// one of the allowed ranges, and it's never expanded beyond the boundaries of
// that allowed range. This is especially relevant for sql_test components.
absl::Status ExpandByteRangesToFullStatements(
    std::vector<FormatterRange>* sorted_ranges, absl::string_view sql,
    std::set<FormatterRange> allowed_ranges = {});

// Converts the given `line_ranges` into sorted byte ranges and makes sure that:
// * each range is not empty and within `sql`;
// * there are no overlapping ranges.
absl::StatusOr<std::vector<FormatterRange>> ConvertLineRangesToSortedByteRanges(
    const std::vector<FormatterRange>& line_ranges, absl::string_view sql,
    const ParseLocationTranslator& location_translator);

}  // namespace googlesql::formatter::internal

#endif  // GOOGLESQL_TOOLS_FORMATTER_INTERNAL_RANGE_UTILS_H_
