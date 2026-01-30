//
// Copyright 2018 Google LLC
// Copyright 2017 The Abseil Authors.
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

// This file contains string processing functions related to
// uppercase, lowercase, etc.

#include "googlesql/base/case.h"

#include <string>

#include "absl/strings/ascii.h"
#include "absl/strings/string_view.h"

namespace googlesql_base {

static int memcasecmp(const char* s1, const char* s2, size_t len) {
  const unsigned char* us1 = reinterpret_cast<const unsigned char*>(s1);
  const unsigned char* us2 = reinterpret_cast<const unsigned char*>(s2);

  for (size_t i = 0; i < len; i++) {
    const int diff =
        int{static_cast<unsigned char>(absl::ascii_tolower(us1[i]))} -
        int{static_cast<unsigned char>(absl::ascii_tolower(us2[i]))};
    if (diff != 0) return diff;
  }
  return 0;
}

bool CaseEqual(absl::string_view s1, absl::string_view s2) {
  if (s1.size() != s2.size()) return false;
  return memcasecmp(s1.data(), s2.data(), s1.size()) == 0;
}

int CaseCompare(absl::string_view s1, absl::string_view s2) {
  if (s1.size() == s2.size()) {
    return memcasecmp(s1.data(), s2.data(), s1.size());
  } else if (s1.size() < s2.size()) {
    int res = memcasecmp(s1.data(), s2.data(), s1.size());
    return (res == 0) ? -1 : res;
  } else {
    int res = memcasecmp(s1.data(), s2.data(), s2.size());
    return (res == 0) ? 1 : res;
  }
}

}  // namespace googlesql_base
