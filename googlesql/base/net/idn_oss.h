//
// Copyright 2020 Google LLC
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

#ifndef THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_NET_IDN_OSS_H_
#define THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_NET_IDN_OSS_H_

#include <string>

#include "absl/strings/string_view.h"

namespace googlesql::internal {

// Converts a host name to the standard encoding used in DNS. For example,
// café.fr is encoded as xn--caf-dma.fr. Returns true when the input is
// valid.
//
// This function does not alter inputs that are already ASCII but will
// reject names with unrecognized, reserved LDH labels. (I.e. where the 3rd
// and 4th characters in a label are hyphens but the first two letters aren't
// a recognized tag like "xn".)
//
// This function implements UTS #46 (Unicode Technical Standard #46), which
// includes an option to transition from IDNA 2003 (Internationalized Domain
// Names for Applications) to IDNA 2008.
bool ToASCII(absl::string_view utf8_host, std::string* ascii_host);

}  // namespace googlesql::internal

#endif  // THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_NET_IDN_OSS_H_
