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

#ifndef GOOGLESQL_PUBLIC_LENIENT_FORMATTER_H_
#define GOOGLESQL_PUBLIC_LENIENT_FORMATTER_H_

#include <string>
#include <vector>

#include "googlesql/public/formatter_options.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace googlesql {

// Formats one or more GoogleSQL statements preserving all comments. The lenient
// formatter will try to format any string, even if it does not parse as a valid
// GoogleSQL statement.
//
// The results are deterministic and stable as long as the input does not
// change. If the input does change, for example if formatting the statements as
// the user is typing, it will try to minimize the change but gives no
// guarantees. Especially if the input switches from invalid to valid GoogleSQL.
absl::Status LenientFormatSql(
    absl::string_view sql, std::string* formatted_sql,
    const FormatterOptions& options = FormatterOptions());

// Same as above but uses StatusOr.
absl::StatusOr<std::string> LenientFormatSql(
    absl::string_view sql,
    const FormatterOptions& options = FormatterOptions());

// Formats only selected `byte_ranges` in `sql` leaving the other parts intact.
// Each range represents an interval [start, end) to be formatted, where start
// and end are 0-based byte offsets. Formats entire input if `byte_ranges` is
// empty.
// See also `FormatterOptions::SetExpandRangesToFullStatements`.
// Note: If expanding ranges to full statements is not enabled and any range
// starts/ends in the middle of a token, the result may be malformed.
absl::StatusOr<std::string> LenientFormatSqlByteRanges(
    absl::string_view sql, const std::vector<FormatterRange>& byte_ranges,
    const FormatterOptions& options = FormatterOptions());

// Formats only selected `line_ranges` in `sql` leaving the other parts intact.
// Each range represents an interval [start, end], where `start` and `end` are
// 1-based line numbers. Formats entire input if `line_ranges` is empty.
// See also `FormatterOptions::SetExpandRangesToFullStatements`.
absl::StatusOr<std::string> LenientFormatSqlLines(
    absl::string_view sql, const std::vector<FormatterRange>& line_ranges,
    const FormatterOptions& options = FormatterOptions());

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_LENIENT_FORMATTER_H_
