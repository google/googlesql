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

#ifndef GOOGLESQL_PUBLIC_FUNCTIONS_LIKE_H_
#define GOOGLESQL_PUBLIC_FUNCTIONS_LIKE_H_

#include <memory>
#include <string>

#include "googlesql/public/type.pb.h"
#include "absl/base/attributes.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "re2/re2.h"
#include "googlesql/base/status.h"

namespace googlesql {
namespace functions {

// Returns true if <c> is a potentially meaningful regexp character.
bool IsRegexSpecialChar(char c);

// Generates regexp pattern from the pattern of LIKE function <pattern>.
absl::StatusOr<std::string> GetRePatternFromLikePattern(
    absl::string_view pattern);

// Creates a regexp that can be used to compute LIKE function with a given
// pattern. <type> must be either TYPE_STRING or TYPE_BYTES. In case of success
// the result is saved in *regexp. Caller should use returned regexp with
// RE2::FullMatch().
absl::Status CreateLikeRegexp(absl::string_view pattern, TypeKind type,
                              std::unique_ptr<RE2>* regexp);

// Creates a regexp that can be used to compute LIKE function with a given
// pattern. <options> can be used to specify the options for compiling
// the regexp. Caller should use returned regexp with RE2::FullMatch().
absl::Status CreateLikeRegexpWithOptions(absl::string_view pattern,
                                         const RE2::Options& options,
                                         std::unique_ptr<RE2>* regexp);

}  // namespace functions
}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_FUNCTIONS_LIKE_H_
