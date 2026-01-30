//
// Copyright 2018 Google LLC
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
#include "googlesql/base/time_proto_util.h"

#include "google/protobuf/timestamp.pb.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"

namespace googlesql_base {

namespace {

// Validation requirements documented at:
// (broken link)
absl::Status Validate(const google::protobuf::Timestamp& t) {
  const auto sec = t.seconds();
  const auto ns = t.nanos();
  // sec must be [0001-01-01T00:00:00Z, 9999-12-31T23:59:59.999999999Z]
  if (sec < -62135596800 || sec > 253402300799) {
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        absl::StrCat("seconds=", sec));
  }
  if (ns < 0 || ns > 999999999) {
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        absl::StrCat("nanos=", ns));
  }
  return absl::OkStatus();
}

}  // namespace

absl::Status EncodeGoogleApiProto(absl::Time t,
                                  google::protobuf::Timestamp* proto) {
  const int64_t s = absl::ToUnixSeconds(t);
  proto->set_seconds(s);
  proto->set_nanos((t - absl::FromUnixSeconds(s)) / absl::Nanoseconds(1));
  return Validate(*proto);
}

absl::StatusOr<absl::Time> DecodeGoogleApiProto(
    const google::protobuf::Timestamp& proto) {
  absl::Status status = Validate(proto);
  if (!status.ok()) return status;
  return absl::FromUnixSeconds(proto.seconds()) +
         absl::Nanoseconds(proto.nanos());
}

}  // namespace googlesql_base
