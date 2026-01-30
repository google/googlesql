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

#include "googlesql/common/warning_sink.h"

#include <cstdint>
#include <string>

#include "googlesql/common/status_payload_utils.h"
#include "googlesql/proto/internal_error_location.pb.h"
#include "googlesql/public/deprecation_warning.pb.h"
#include "absl/status/status.h"
#include "absl/types/span.h"

namespace googlesql {

WarningSink::WarningSink(bool consider_location)
    : consider_location_(consider_location) {}

absl::Status WarningSink::AddWarning(DeprecationWarning::Kind kind,
                                     absl::Status warning) {
  int64_t byte_offset = -1;
  std::string filename = "";
  if (consider_location_) {
    // If the warning already exists, return early.
    InternalErrorLocation location;
    if (internal::HasPayloadWithType<InternalErrorLocation>(warning)) {
      location = internal::GetPayload<InternalErrorLocation>(warning);
    }
    byte_offset = location.byte_offset();
    filename = location.filename();
  }
  if (!unique_warnings_.emplace(kind, warning.message(), byte_offset, filename)
           .second) {
    return absl::OkStatus();
  }
  DeprecationWarning warning_proto;
  warning_proto.set_kind(kind);
  internal::AttachPayload(&warning, warning_proto);
  warnings_.push_back(warning);
  return absl::OkStatus();
}

absl::Span<const absl::Status> WarningSink::warnings() const {
  return absl::MakeSpan(warnings_);
}

void WarningSink::Reset() {
  warnings_.clear();
  unique_warnings_.clear();
}

}  // namespace googlesql
