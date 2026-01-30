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

#ifndef THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_STATUS_PAYLOAD_H_
#define THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_STATUS_PAYLOAD_H_

#include "absl/base/attributes.h"
#include "absl/status/status.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace googlesql_base {

extern const absl::string_view kGoogleSqlTypeUrlPrefix;

// Return the type_url for encoding a Status payload of type T.
template <class T>
std::string GetTypeUrl() {
  return absl::StrCat(kGoogleSqlTypeUrlPrefix, T::descriptor()->full_name());
}

// Attaches the given payload. This will overwrite any previous payload with
// the same type.
template <class T>
void AttachPayload(absl::Status* status, const T& payload) {
  absl::Cord serialized = absl::Cord(payload.SerializeAsString());
  status->SetPayload(GetTypeUrl<T>(), serialized);
}

}  // namespace googlesql_base

#endif  // THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_STATUS_PAYLOAD_H_
