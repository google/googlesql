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

#include "googlesql/public/functions/range.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "googlesql/common/errors.h"
#include "googlesql/public/functions/date_time_util.h"
#include "googlesql/public/interval_value.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/range_type.h"
#include "googlesql/public/value.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "googlesql/base/case.h"  
#include "googlesql/base/status.h"                         
#include "googlesql/base/status_macros.h"

namespace googlesql {

namespace {

std::optional<absl::string_view> UnboundedOrValue(
    absl::string_view boundary_value) {
  if (googlesql_base::CaseEqual(boundary_value, "UNBOUNDED") ||
      googlesql_base::CaseEqual(boundary_value, "NULL")) {
    return std::nullopt;
  }
  return {boundary_value};
}

static Value RangeBoundaryAsDate(std::optional<int32_t> boundary) {
  return boundary ? Value::Date(*boundary) : Value::NullDate();
}

static Value RangeBoundaryAsDatetimeFromPacked64Micros(
    std::optional<int64_t> boundary) {
  return boundary.has_value() ? Value::DatetimeFromPacked64Micros(*boundary)
                              : Value::NullDatetime();
}

static Value RangeBoundaryAsTimestampFromUnixMicros(
    std::optional<int64_t> boundary) {
  return boundary.has_value() ? Value::TimestampFromUnixMicros(*boundary)
                              : Value::NullTimestamp();
}

// Creates a Value object of type 'range_type' from given 'boundaries'.
static absl::StatusOr<Value> MakeRange(const RangeType* range_type,
                                       RangeBoundaries<int32_t> boundaries) {
  if (!(range_type->element_type()->kind() == TypeKind::TYPE_DATE)) {
    return MakeEvalError() << "MakeRange is not implemented for "
                           << range_type->element_type()->kind();
  }
  return Value::MakeRange(RangeBoundaryAsDate(boundaries.start),
                          RangeBoundaryAsDate(boundaries.end));
}

// Creates a Value object of type 'range_type' from given 'boundaries'.
absl::StatusOr<Value> MakeRange(const RangeType* range_type,
                                RangeBoundaries<int64_t> boundaries) {
  switch (range_type->element_type()->kind()) {
    case TypeKind::TYPE_DATETIME: {
      return Value::MakeRange(
          RangeBoundaryAsDatetimeFromPacked64Micros(boundaries.start),
          RangeBoundaryAsDatetimeFromPacked64Micros(boundaries.end));
    }
    case TypeKind::TYPE_TIMESTAMP: {
      return Value::MakeRange(
          RangeBoundaryAsTimestampFromUnixMicros(boundaries.start),
          RangeBoundaryAsTimestampFromUnixMicros(boundaries.end));
    }
    default:
      return MakeEvalError() << "MakeRange is not implemented for "
                             << range_type->element_type()->kind();
  }
}

}  // namespace

absl::StatusOr<StringRangeBoundaries> ParseRangeBoundaries(
    absl::string_view range_value, bool strict_formatting) {
  // Check that range_value starts with "[" and ends with ")"
  if (range_value.empty() || range_value.front() != '[' ||
      range_value.back() != ')') {
    return MakeEvalError()
           << "Failed to parse range: range must be formatted exactly "
              "as [START, END) (found "
           << range_value << ")";
  }

  // Remove the "[" from the start and ")" from end
  range_value.remove_prefix(1);
  range_value.remove_suffix(1);
  // TODO: Update to handle FORMAT clause,
  // including dates with formats with "," such as "May 3rd, 2023"
  std::vector<absl::string_view> range_parts = absl::StrSplit(range_value, ',');
  if (range_parts.size() != 2 ||
      (strict_formatting && !absl::StartsWith(range_parts[1], " "))) {
    std::string separator = ", ";
    if (!strict_formatting) {
      separator = ",";
    }
    return MakeEvalError()
           << "Failed to parse range: range must be formatted exactly as "
              "[START, "
              "END) with two parts, START and END, divided with \""
           << separator << "\"";
  }

  absl::string_view start = absl::StripAsciiWhitespace(range_parts[0]);
  absl::string_view end = absl::StripAsciiWhitespace(range_parts[1]);
  if (strict_formatting) {
    if (start.length() != range_parts[0].length()) {
      return MakeEvalError()
             << "Failed to parse range: range must be formatted "
                "exactly as [START, END) with START having "
                "no leading or trailing spaces, but found: \""
             << range_parts[0] << "\"";
    }
    // range_parts[1] will have a leading space from delimiter ", "
    // So +1 to trimmed string length to account for it
    if (end.length() + 1 != range_parts[1].length()) {
      return MakeEvalError()
             << "Failed to parse range: range must be formatted "
                "exactly as [START, END) with END having "
                "no leading or trailing spaces, but found: \""
             << range_parts[1] << "\"";
    }
  }

  return StringRangeBoundaries{.start = UnboundedOrValue(start),
                               .end = UnboundedOrValue(end)};
}

absl::StatusOr<Value> DeserializeRangeValueFromBytes(
    const RangeType* range_type, absl::string_view bytes, size_t* bytes_read) {
  switch (range_type->element_type()->kind()) {
    case TypeKind::TYPE_DATE: {
      GOOGLESQL_ASSIGN_OR_RETURN(RangeBoundaries<int32_t> boundaries,
                       DeserializeRangeFromBytes<int32_t>(bytes, bytes_read));
      return MakeRange(range_type, boundaries);
    }
    case TypeKind::TYPE_DATETIME: {
      GOOGLESQL_ASSIGN_OR_RETURN(RangeBoundaries<int64_t> boundaries,
                       DeserializeRangeFromBytes<int64_t>(bytes, bytes_read));
      return MakeRange(range_type, boundaries);
    }
    case TypeKind::TYPE_TIMESTAMP: {
      GOOGLESQL_ASSIGN_OR_RETURN(RangeBoundaries<int64_t> boundaries,
                       DeserializeRangeFromBytes<int64_t>(bytes, bytes_read));
      return MakeRange(range_type, boundaries);
    }
    default:
      return MakeEvalError()
             << "DeserializeRangeFromBytes is not implemented for "
             << range_type->element_type()->kind();
  }
}

namespace functions {

absl::StatusOr<TimestampRangeArrayGenerator>
TimestampRangeArrayGenerator::Create(IntervalValue step,
                                     bool last_partial_range,
                                     TimestampScale scale) {
  GOOGLESQL_RETURN_IF_ERROR(ValidateTimeScale(scale));
  GOOGLESQL_RETURN_IF_ERROR(ValidateStep(step, scale));

  return TimestampRangeArrayGenerator(IntervalToDuration(step),
                                      last_partial_range);
}

absl::Status TimestampRangeArrayGenerator::ValidateStep(
    const IntervalValue& step, TimestampScale scale) {
  if (step.get_months() != 0) {
    return MakeEvalError()
           << "step with non-zero MONTH or YEAR part is not supported";
  }
  if (scale != kNanoseconds && step.get_nano_fractions() != 0) {
    return MakeEvalError()
           << "step with non-zero NANOSECOND part is not supported";
  }
  // Nano fractions can't be negative, so only checking months, days and
  // micros here.
  if (step.get_days() < 0 || step.get_micros() < 0) {
    return MakeEvalError() << "step cannot be negative";
  }
  // We rejected step with MONTH part, so we don't need to check it.
  // Note that Interval::get_nanos() returns both, MICROSECOND and NANOSECOND
  // parts converted to nanoseconds.
  if (step.get_days() == 0 && step.get_nanos() == 0) {
    return MakeEvalError() << "step cannot be 0";
  }
  return absl::OkStatus();
}

absl::Duration TimestampRangeArrayGenerator::IntervalToDuration(
    const IntervalValue& step) {
  return absl::Hours(step.get_days()) * 24 +
         absl::Microseconds(step.get_micros()) +
         absl::Nanoseconds(step.get_nano_fractions());
}

absl::StatusOr<DateRangeArrayGenerator> DateRangeArrayGenerator::Create(
    IntervalValue step, bool last_partial_range) {
  GOOGLESQL_RETURN_IF_ERROR(ValidateStep(step));
  return DateRangeArrayGenerator(step, last_partial_range);
}

absl::Status DateRangeArrayGenerator::ValidateStep(const IntervalValue& step) {
  // Note that Interval::get_nanos() returns both, MICROSECOND and NANOSECOND
  // parts converted to nanoseconds.
  if (step.get_nanos() != 0) {
    return MakeEvalError()
           << "step with non-zero (H:M:S[.F]) part is not supported";
  }
  // We rejected step with get_nanos() part, so we don't need to check it.
  // step for DateRangeArray cannot have both Y-M part and D part
  if (step.get_months() != 0 && step.get_days() != 0) {
    return MakeEvalError()
           << "step should either have the Y-M part or the DAY part";
  }
  if (step.get_months() < 0 || step.get_days() < 0) {
    return MakeEvalError() << "step cannot be negative";
  }
  if (step.get_months() == 0 && step.get_days() == 0) {
    return MakeEvalError() << "step cannot be 0";
  }

  return absl::OkStatus();
}

absl::StatusOr<DatetimeRangeArrayGenerator> DatetimeRangeArrayGenerator::Create(
    IntervalValue step, bool last_partial_range, TimestampScale scale) {
  GOOGLESQL_RETURN_IF_ERROR(ValidateTimeScale(scale));
  GOOGLESQL_RETURN_IF_ERROR(ValidateStep(step, scale));

  return DatetimeRangeArrayGenerator(step, last_partial_range);
}

absl::Status DatetimeRangeArrayGenerator::ValidateStep(
    const IntervalValue& step, TimestampScale scale) {
  if (scale != kNanoseconds && step.get_nano_fractions() != 0) {
    return MakeEvalError()
           << "step with non-zero NANOSECOND part is not supported";
  }
  // Note that Interval::get_nanos() returns both, MICROSECOND and NANOSECOND
  // parts converted to nanoseconds.
  if (step.get_months() != 0 &&
      (step.get_days() != 0 || step.get_nanos() != 0)) {
    return MakeEvalError()
           << "step should either have the Y-M part or the D (H:M:S[.F]) part";
  }
  // Nano fractions can't be negative, so only checking months, days and
  // micros here.
  if (step.get_months() < 0 || step.get_days() < 0 || step.get_micros() < 0) {
    return MakeEvalError() << "step cannot be negative";
  }
  if (step.get_months() == 0 && step.get_days() == 0 && step.get_nanos() == 0) {
    return MakeEvalError() << "step cannot be 0";
  }

  return absl::OkStatus();
}

}  // namespace functions

}  // namespace googlesql
