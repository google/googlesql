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

// This file implements basic comparisons utility functions.

#ifndef GOOGLESQL_PUBLIC_FUNCTIONS_COMPARISON_H_
#define GOOGLESQL_PUBLIC_FUNCTIONS_COMPARISON_H_

#include <cstdint>
#include <limits>


namespace googlesql {
namespace functions {

// Returns a negative value if x < y, positive if x > y, and zero if x==y.
inline int64_t Compare64(int64_t x, uint64_t y) {
  if (x < 0 || y > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
    return -1;
  // Else, both are non-negative int64 values, subtraction result fits in int64.
  return x - static_cast<int64_t>(y);
}

}  // namespace functions
}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_FUNCTIONS_COMPARISON_H_
