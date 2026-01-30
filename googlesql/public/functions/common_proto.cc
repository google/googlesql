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

#include "googlesql/public/functions/common_proto.h"

#include <cstdint>
#include <string>

#include "google/type/timeofday.pb.h"
#include "googlesql/common/errors.h"
#include "googlesql/public/civil_time.h"
#include "googlesql/public/functions/date_time_util.h"
#include "absl/strings/str_cat.h"
#include "googlesql/base/status.h"
#include "googlesql/base/status_builder.h"

namespace googlesql {
namespace functions {

namespace {

static const int64_t kNanosPerMicrosecond = 1000;

static bool IsValidTime(int hour, int minute, int second) {
  if (hour < 0 || hour > 23 ||
      minute < 0 || minute > 59 ||
      second < 0 || second > 59) {
    return false;
  }
  return true;
}

static bool IsValidProto3TimeOfDay(const google::type::TimeOfDay& time) {
  if (!IsValidTime(time.hours(), time.minutes(), time.seconds()) ||
      time.nanos() < 0 || time.nanos() > 999999999) {
    return false;
  }
  return true;
}

}  // namespace

absl::Status ConvertTimeToProto3TimeOfDay(TimeValue input,
                                          google::type::TimeOfDay* output) {
  if (!input.IsValid()) {
    return MakeEvalError()
           << "Input is outside of Proto3 TimeOfDay range: "
           << input.DebugString();
  }
  output->set_hours(input.Hour());
  output->set_minutes(input.Minute());
  output->set_seconds(input.Second());
  output->set_nanos(input.Nanoseconds());
  return absl::OkStatus();
}

absl::Status ConvertProto3TimeOfDayToTime(const google::type::TimeOfDay& input,
                                          TimestampScale scale,
                                          TimeValue* output) {
  if (!IsValidProto3TimeOfDay(input)) {
    return MakeEvalError() << "Invalid Proto3 TimeOfDay input: "
                           << absl::StrCat(input);
  }
  if (scale == kMicroseconds) {
    *output = TimeValue::FromHMSAndMicros(input.hours(), input.minutes(),
                                          input.seconds(),
                                          input.nanos() / kNanosPerMicrosecond);
  } else {
    *output = TimeValue::FromHMSAndNanos(input.hours(), input.minutes(),
                                         input.seconds(), input.nanos());
  }
  return absl::OkStatus();
}

}  // namespace functions
}  // namespace googlesql
