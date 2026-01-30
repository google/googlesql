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

#ifndef GOOGLESQL_PUBLIC_FUNCTIONS_RANGE_H_
#define GOOGLESQL_PUBLIC_FUNCTIONS_RANGE_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>

#include "googlesql/common/errors.h"
#include "googlesql/public/civil_time.h"
#include "googlesql/public/functions/date_time_util.h"
#include "googlesql/public/functions/datetime.pb.h"
#include "googlesql/public/interval_value.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/range_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/value.h"
#include "absl/base/optimization.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "googlesql/base/endian.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

// Helper functions used for parsing range values from strings.
namespace googlesql {

struct StringRangeBoundaries {
  std::optional<absl::string_view> start;
  std::optional<absl::string_view> end;
};

// Extracts the "start" and "end" parts from a given range literal string
// with format "[<start>, <end>]", and returns it as a std::pair of optional
// "start" and "end" parts respectively. If the "start" and "end" parts are
// unbounded, then std::nullopt is returned in the corresponding part of the
// pair.
// If "strict_formatting" is set to true, then method will require
// <start> and <end> to be delimited by ", " (comma then space) with no extra
// trailing spaces, and will return error if it's not the case.
absl::StatusOr<StringRangeBoundaries> ParseRangeBoundaries(
    absl::string_view range_value, bool strict_formatting = true);

namespace functions {

// Utility class for GENERATE_RANGE_ARRAY function implementations
// that covers RANGE<TIMESTAMP>
//
// This class is structured to allow for more efficient computation when all,
// but first (input RANGE) function arguments are constant.
class TimestampRangeArrayGenerator {
 public:
  static absl::StatusOr<TimestampRangeArrayGenerator> Create(
      IntervalValue step, bool last_partial_range, TimestampScale scale);

  // Generates [start, end) subranges of the specified range (passed as
  // range_start and range_end arguments) and invokes the provided emitter Fn
  // for every subrange.
  //
  // Fn can be any invocable with the following signature:
  //   absl::Status(absl::Time, absl::Time)
  // Fn can terminate the process by returning a non-OK absl::Status.
  //
  // An error is returned if the input range has UNBOUNDED endpoints.
  template <typename Fn>
  absl::Status Generate(std::optional<absl::Time> range_start,
                        std::optional<absl::Time> range_end,
                        const Fn& emitter) const;

 private:
  static absl::Status ValidateStep(const IntervalValue& step,
                                   TimestampScale scale);
  static absl::Duration IntervalToDuration(const IntervalValue& step);

  TimestampRangeArrayGenerator(absl::Duration step, bool last_partial_range)
      : step_(step), last_partial_range_(last_partial_range) {}

  absl::Duration step_;
  bool last_partial_range_;
};

template <typename Fn>
absl::Status TimestampRangeArrayGenerator::Generate(
    std::optional<absl::Time> range_start, std::optional<absl::Time> range_end,
    const Fn& emitter) const {
  static_assert(
      std::is_invocable_r_v<absl::Status, Fn, absl::Time, absl::Time>,
      "emitter should be invocable as absl::Status(absl::Time, absl::Time)");
  if (!range_start || !range_end) {
    return MakeEvalError() << "input RANGE cannot have UNBOUNDED endpoints";
  }
  absl::Time start = range_start.value();
  absl::Time end = range_end.value();
  GOOGLESQL_RET_CHECK(IsValidTime(start));
  GOOGLESQL_RET_CHECK(IsValidTime(end));
  GOOGLESQL_RET_CHECK_LT(start, end) << "invalid input RANGE value";

  // Note that in the following code we don't need to check for overflows when
  // adding step_ to current_start, since the range of representable dates in
  // absl::Time (+/-100 billion years) is much wider than the range of allowed
  // dates in TIMESTAMP (1-10,000 years).

  absl::Time current_start;
  absl::Time current_end = start;
  for (;;) {
    current_start = current_end;
    current_end = current_start + step_;
    if (ABSL_PREDICT_FALSE(current_end >= end)) {
      if (current_end == end || last_partial_range_) {
        GOOGLESQL_RETURN_IF_ERROR(emitter(current_start, end));
      }
      break;
    }
    GOOGLESQL_RETURN_IF_ERROR(emitter(current_start, current_end));
  }

  return absl::OkStatus();
}

// Utility class for GENERATE_RANGE_ARRAY function implementations
// that covers RANGE<DATE>
//
// This class is structured to allow for more efficient computation when all,
// but first (input RANGE) function arguments are constant.
class DateRangeArrayGenerator {
 public:
  static absl::StatusOr<DateRangeArrayGenerator> Create(
      IntervalValue step, bool last_partial_range);

  // Generates [start, end) subranges of the specified range (passed as
  // range_start and range_end arguments) and invokes the provided emitter Fn
  // for every subrange.
  //
  // Fn can be any invocable with the following signature:
  //   absl::Status(int32_t, int32_t)
  // Fn can terminate the process by returning a non-OK absl::Status.
  //
  // An error is returned if the input range has UNBOUNDED endpoints.
  template <typename Fn>
  absl::Status Generate(std::optional<int32_t> range_start,
                        std::optional<int32_t> range_end,
                        const Fn& emitter) const;

 private:
  static absl::Status ValidateStep(const IntervalValue& step);

  DateRangeArrayGenerator(IntervalValue step, bool last_partial_range)
      : step_(step), last_partial_range_(last_partial_range) {}

  IntervalValue step_;
  bool last_partial_range_;
};

template <typename Fn>
absl::Status DateRangeArrayGenerator::Generate(
    std::optional<int32_t> range_start, std::optional<int32_t> range_end,
    const Fn& emitter) const {
  static_assert(
      std::is_invocable_r_v<absl::Status, Fn, int32_t, int32_t>,
      "emitter should be invocable as absl::Status(int32_t, int32_t)");
  if (!range_start || !range_end) {
    return MakeEvalError() << "input RANGE cannot have UNBOUNDED endpoints";
  }
  int32_t start = range_start.value();
  int32_t end = range_end.value();
  GOOGLESQL_RET_CHECK(IsValidDate(start));
  GOOGLESQL_RET_CHECK(IsValidDate(end));
  GOOGLESQL_RET_CHECK_LT(start, end) << "invalid input RANGE value";

  int32_t current_start;
  int32_t current_end = start;
  absl::Status status;
  // Note: current_step would never overflow because MAX_MONTHS and MAX_DAYS
  // do not exceed the boundary of int64_t
  // Every iteration, we use the incremented step value to handle cases where
  // dates are near the end of the month consistent with the existing method
  int64_t current_step = 0;
  for (;;) {
    current_start = current_end;
    if (step_.get_months() > 0) {
      // Case 1. step is composed of Y-M
      current_step += step_.get_months();
      status = AddDate(start, MONTH, current_step, &current_end);
    } else {
      // Case 2. step is composed of D
      current_step += step_.get_days();
      status = AddDate(start, DAY, current_step, &current_end);
    }

    // We expect status to be not OK when current_end overflows date bound
    if (ABSL_PREDICT_FALSE(!status.ok() || current_end >= end)) {
      if (current_end == end || last_partial_range_) {
        GOOGLESQL_RETURN_IF_ERROR(emitter(current_start, end));
      }
      break;
    }
    GOOGLESQL_RETURN_IF_ERROR(emitter(current_start, current_end));
  }

  return absl::OkStatus();
}

// Utility class for GENERATE_RANGE_ARRAY function implementations
// that covers RANGE<DATETIME>
//
// This class is structured to allow for more efficient computation when all,
// but first (input RANGE) function arguments are constant.
class DatetimeRangeArrayGenerator {
 public:
  static absl::StatusOr<DatetimeRangeArrayGenerator> Create(
      IntervalValue step, bool last_partial_range, TimestampScale scale);

  // Generates [start, end) subranges of the specified range (passed as
  // range_start and range_end arguments) and invokes the provided emitter Fn
  // for every subrange.
  //
  // Fn can be any invocable with the following signature:
  //   absl::Status(DatetimeValue, DatetimeValue)
  // Fn can terminate the process by returning a non-OK absl::Status.
  //
  // An error is returned if the input range has UNBOUNDED endpoints.
  template <typename Fn>
  absl::Status Generate(std::optional<DatetimeValue> range_start,
                        std::optional<DatetimeValue> range_end,
                        const Fn& emitter) const;

 private:
  static absl::Status ValidateStep(const IntervalValue& step,
                                   TimestampScale scale);

  DatetimeRangeArrayGenerator(IntervalValue step, bool last_partial_range)
      : step_(step), last_partial_range_(last_partial_range) {}

  IntervalValue step_;
  bool last_partial_range_;
};

template <typename Fn>
absl::Status DatetimeRangeArrayGenerator::Generate(
    std::optional<DatetimeValue> range_start,
    std::optional<DatetimeValue> range_end, const Fn& emitter) const {
  static_assert(
      std::is_invocable_r_v<absl::Status, Fn, DatetimeValue, DatetimeValue>,
      "emitter should be invocable as absl::Status(DatetimeValue, "
      "DatetimeValue)");
  if (!range_start || !range_end) {
    return MakeEvalError() << "input RANGE cannot have UNBOUNDED endpoints";
  }
  DatetimeValue start = range_start.value();
  DatetimeValue end = range_end.value();
  GOOGLESQL_RET_CHECK(start.IsValid());
  GOOGLESQL_RET_CHECK(end.IsValid());
  GOOGLESQL_RET_CHECK(IntervalDiffDatetimes(start, end)->GetAsNanos() <= 0)
      << "invalid input RANGE value";

  DatetimeValue current_start;
  DatetimeValue current_end = start;
  absl::Status status;
  // Every iteration, we use the incremented step value to handle cases where
  // dates are near the end of the month consistent with the existing method
  IntervalValue current_step;
  for (;;) {
    current_start = current_end;
    absl::StatusOr<IntervalValue> next_step = current_step + step_;
    // We expect status to be not OK when next_step overflowed IntervalValue
    if (ABSL_PREDICT_FALSE(!next_step.ok())) {
      // If the next_step has overflown, then it means the next_step is over
      // 10000 years (which exceeds the bound of Datetime).
      // Therefore, it is safe to exit the loop once the IntervalValue overflows
      if (last_partial_range_) {
        GOOGLESQL_RETURN_IF_ERROR(emitter(current_start, end));
      }
      break;
    }

    current_step = next_step.value();
    status = AddDatetime(start, current_step, &current_end);
    GOOGLESQL_ASSIGN_OR_RETURN(IntervalValue diff,
                     IntervalDiffDatetimes(current_end, end));

    // We expect status to be not OK when current_end overflows datetime bound
    if (ABSL_PREDICT_FALSE(!status.ok() || diff.GetAsNanos() >= 0)) {
      if (diff.GetAsNanos() == 0 || last_partial_range_) {
        GOOGLESQL_RETURN_IF_ERROR(emitter(current_start, end));
      }
      break;
    }

    GOOGLESQL_RETURN_IF_ERROR(emitter(current_start, current_end));
  }

  return absl::OkStatus();
}

}  // namespace functions

template <typename T>
struct RangeBoundaries {
  std::optional<T> start;
  std::optional<T> end;
};

// Serializes RANGE value with element type T and appends the result to the
// provided bytes argument.
template <typename T>
void SerializeRangeAndAppendToBytes(const RangeBoundaries<T>& range_boundaries,
                                    std::string* bytes);

template <typename T>
std::string SerializeRange(const RangeBoundaries<T>& range_boundaries) {
  std::string bytes;
  SerializeRangeAndAppendToBytes(range_boundaries, &bytes);
  return bytes;
}

// Deserializes RANGE<T> from bytes and returns range boundaries.
// The number of bytes would be written to "bytes_read" if it is not null
// Size of "bytes" can be longer than the size of the encoded range value
template <typename T>
absl::StatusOr<RangeBoundaries<T>> DeserializeRangeFromBytes(
    absl::string_view bytes, size_t* bytes_read = nullptr);

// Given a string_view that starts with encoded range, returns how many bytes
// the encoded range occupies. It's ok for provided string_view to be longer
// than encoded range
template <typename T>
absl::StatusOr<size_t> GetEncodedRangeSize(absl::string_view bytes);

namespace internal {

constexpr uint8_t kHasBoundedStartMask = 0x01;
constexpr uint8_t kHasBoundedEndMask = 0x02;

template <typename T>
size_t GetEncodedRangeSizeFromHeader(uint8_t header) {
  size_t num_bounds = (header & internal::kHasBoundedStartMask) +
                      ((header & internal::kHasBoundedEndMask) >> 1);
  return sizeof(header) + num_bounds * sizeof(T);
}

}  // namespace internal

template <typename T>
void SerializeRangeAndAppendToBytes(const RangeBoundaries<T>& range_boundaries,
                                    std::string* bytes) {
  // Currently serialized format is only defined for int32_t and int64_t.
  static_assert(std::is_same_v<T, int32_t> || std::is_same_v<T, int64_t>);

  uint8_t header = 0x00;
  if (range_boundaries.start) {
    header |= internal::kHasBoundedStartMask;
  }
  if (range_boundaries.end) {
    header |= internal::kHasBoundedEndMask;
  }

  // Allocate a buffer that can fit any of the serialized values.
  char buf[std::max(sizeof(T), sizeof(uint8_t))];

  googlesql_base::LittleEndian::Store<uint8_t>(header, buf);
  bytes->append(buf, sizeof(uint8_t));
  if (range_boundaries.start) {
    googlesql_base::LittleEndian::Store<T>(*range_boundaries.start, buf);
    bytes->append(buf, sizeof(T));
  }
  if (range_boundaries.end) {
    googlesql_base::LittleEndian::Store<T>(*range_boundaries.end, buf);
    bytes->append(buf, sizeof(T));
  }
}

template <typename T>
absl::StatusOr<RangeBoundaries<T>> DeserializeRangeFromBytes(
    absl::string_view bytes, size_t* bytes_read) {
  // Currently serialized format is only defined for int32_t and int64_t.
  static_assert(std::is_same_v<T, int32_t> || std::is_same_v<T, int64_t>);

  const char* data = reinterpret_cast<const char*>(bytes.data());
  // Read the header.
  if (ABSL_PREDICT_FALSE(bytes.size() < sizeof(uint8_t))) {
    return absl::InvalidArgumentError(
        "Too few bytes to read RANGE content (needed at least 1)");
  }
  uint8_t header = googlesql_base::LittleEndian::Load<uint8_t>(bytes.data());
  data += sizeof(header);
  const size_t encoded_range_size =
      internal::GetEncodedRangeSizeFromHeader<T>(header);
  if (ABSL_PREDICT_FALSE(bytes.size() < encoded_range_size)) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Too few bytes to read RANGE content (needed %d; got %d)",
        encoded_range_size, bytes.size()));
  }

  std::optional<T> start, end;
  if (header & internal::kHasBoundedStartMask) {
    start = googlesql_base::LittleEndian::Load<T>(data);
    data += sizeof(T);
  }
  if (header & internal::kHasBoundedEndMask) {
    end = googlesql_base::LittleEndian::Load<T>(data);
    data += sizeof(T);
  }

  if (bytes_read != nullptr) {
    *bytes_read = encoded_range_size;
  }
  return RangeBoundaries<T>{start, end};
}

absl::StatusOr<Value> DeserializeRangeValueFromBytes(
    const RangeType* range_type, absl::string_view bytes,
    size_t* bytes_read = nullptr);

template <typename T>
absl::StatusOr<size_t> GetEncodedRangeSize(absl::string_view bytes) {
  if (ABSL_PREDICT_FALSE(bytes.size() < sizeof(uint8_t))) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Too few bytes to determine encoded RANGE size (needed %d; got "
        "%d)",
        sizeof(uint8_t), bytes.size()));
  }
  uint8_t header = googlesql_base::LittleEndian::Load<uint8_t>(bytes.data());
  return internal::GetEncodedRangeSizeFromHeader<T>(header);
}

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_FUNCTIONS_RANGE_H_
