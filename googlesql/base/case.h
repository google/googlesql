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

#ifndef THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_CASE_H_
#define THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_CASE_H_

// -----------------------------------------------------------------------------
// File: case.h
// -----------------------------------------------------------------------------
//
// This package contains character classification functions for evaluating
// the case state of strings, and converting strings to uppercase, lowercase,
// etc.
//
// Unlike <ctype.h> (or absl/strings/ascii.h), the functions in this file
// are designed to operate on strings, not single characters.
//
// Except for those marked as "using the C/POSIX locale", these functions are
// for ASCII strings only.

#ifndef _MSC_VER
#include <strings.h>  // for strcasecmp, but msvc does not have this header
#endif

#include <cstring>
#include <functional>
#include <string>

#include "absl/base/macros.h"
#include "absl/strings/ascii.h"
#include "absl/strings/string_view.h"

namespace googlesql_base {

// StringCaseCompare()
//
// Performs a case-insensitive string comparison, using the C/POSIX locale.
// Returns:
//    less than 0:    if s1 < s2
//    equal to 0:     if s1 == s2
//    greater than 0: if s1 > s2
inline int StringCaseCompare(const std::string& s1, const std::string& s2) {
#ifdef _MSC_VER
  return _stricmp(s1.c_str(), s2.c_str());
#else
  return strcasecmp(s1.c_str(), s2.c_str());
#endif
}

// StringCaseEqual()
//
// Returns true if the two strings are equal, case-insensitively speaking, using
// the C/POSIX locale.
inline bool StringCaseEqual(const std::string& s1, const std::string& s2) {
  return StringCaseCompare(s1, s2) == 0;
}

// StringCaseLess()
//
// Performs a case-insensitive less-than string comparison, using the C/POSIX
// locale. This function object is useful as a template parameter for STL
// set/map of strings, if uniqueness of keys is case-insensitive.
struct StringCaseLess {
  bool operator()(const std::string& s1, const std::string& s2) const {
    return StringCaseCompare(s1, s2) < 0;
  }
};

// CaseCompare()
//
// Performs a case-insensitive absl::string_view comparison.
// Returns:
//    less than 0:    if s1 < s2
//    equal to 0:     if s1 == s2
//    greater than 0: if s1 > s2
int CaseCompare(absl::string_view s1, absl::string_view s2);

// CaseEqual()
//
// Returns true if the two absl::string_views are equal, case-insensitively
// speaking.
bool CaseEqual(absl::string_view s1, absl::string_view s2);

// CaseLess()
//
// Performs a case-insensitive less-than absl::string_view comparison. This
// function object is useful as a template parameter for STL set/map of
// absl::string_view-compatible types, if uniqueness of keys is
// case-insensitive.
struct CaseLess {
  bool operator()(absl::string_view s1, absl::string_view s2) const {
    return CaseCompare(s1, s2) < 0;
  }
};

struct StringViewCaseEqual {
  typedef void is_transparent;
  bool operator()(absl::string_view s1, absl::string_view s2) const {
    return CaseEqual(s1, s2);
  }
};

struct StringViewCaseHash {
  typedef void is_transparent;
  size_t operator()(absl::string_view s1) const {
    return std::hash<std::string>()(absl::AsciiStrToLower(s1));
  }
};

}  // namespace googlesql_base

#endif  // THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_CASE_H_
