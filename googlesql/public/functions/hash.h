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

// A common interface for various hash algorithms.

#ifndef GOOGLESQL_PUBLIC_FUNCTIONS_HASH_H_
#define GOOGLESQL_PUBLIC_FUNCTIONS_HASH_H_

#include <cstdint>
#include <memory>
#include <string>

#include "absl/base/attributes.h"
#include "absl/strings/string_view.h"

namespace googlesql {
namespace functions {

// Hashes inputs using a particular algorithm.
class Hasher {
 public:
  enum Algorithm {
    kMd5 = 0,
    kSha1 = 1,
    kSha256 = 2,
    kSha512 = 3,
  };

  // Creates a new instance of the hasher for the given algorithm.
  static std::unique_ptr<Hasher> Create(Algorithm algorithm);

  virtual ~Hasher() = default;

  // Returns the hash of the input bytes. Calling this method concurrently
  // on the same object is not thread-safe.
  ABSL_MUST_USE_RESULT virtual std::string Hash(absl::string_view input) = 0;
};

// Computes the fingerprint of the input bytes using the farmhash::Fingerprint64
// function from the FarmHash library (https://github.com/google/farmhash).
int64_t FarmFingerprint(absl::string_view input);

}  // namespace functions
}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_FUNCTIONS_HASH_H_
