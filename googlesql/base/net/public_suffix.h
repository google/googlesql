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

#ifndef THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_NET_PUBLIC_SUFFIX_H_
#define THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_NET_PUBLIC_SUFFIX_H_

// This file abstracts public suffix methods which has a different
// implementation in the open source version.  Methods are documented here as a
// convenience.

#include "googlesql/base/net/public_suffix_oss.h"

namespace googlesql::internal {

//
// GetPublicSuffix()
//
// Extract the public suffix from the given name, using the latest public
// suffix data. Returns the entire string if it is a public suffix. Returns the
// empty string when the name does not contain a public suffix. Examples:
//   "www.berkeley.edu"           -> "edu"
//   "www.clavering.essex.sch.uk" -> "essex.sch.uk"
//   "sch.uk"                     -> ""
// The name must be normalized (lower-cased, Punycoded) before calling this
// method. Use googlesql::internal::ToASCII to normalize the name. The returned
// absl::string_view will live as long as the input absl::string_view lives.
//
// absl::string_view GetPublicSuffix(absl::string_view name);

//
// GetTopPrivateDomain()
//
// Extract the top private domain from the given name, using the latest public
// suffix data. Returns the entire string if it is a top private domain.
// Returns the empty string when the name does not contain a top private
// domain. Examples:
//   "www.google.com"   -> "google.com"
//   "www.google.co.uk" -> "google.co.uk"
//   "co.uk"            -> ""
// The name must be normalized (lower-cased, Punycoded) before calling this
// method. Use googlesql::internal::ToASCII to normalize the name. The returned
// absl::string_view will live as long as the input absl::string_view lives.
//
// absl::string_view GetTopPrivateDomain(absl::string_view name);

}  // namespace googlesql::internal

#endif  // THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_NET_PUBLIC_SUFFIX_H_
