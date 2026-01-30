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

#ifndef GOOGLESQL_PUBLIC_TYPES_INTERNAL_UTILS_H_
#define GOOGLESQL_PUBLIC_TYPES_INTERNAL_UTILS_H_

#include <stddef.h>

#include <string>

#include <vector>

#include "googlesql/public/options.pb.h"
#include "googlesql/public/types/type.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/node_hash_map.h"
#include "absl/container/node_hash_set.h"
#include "absl/strings/str_cat.h"

namespace googlesql {
namespace internal {  //   For internal use only

// This file contains several utility functions used by Type classes and
// TypeFactory. This functions are intended for internal use only and not meant
// for public use.

// Some implementations of some stl container (especially, strings), when their
// content is smaller than container size, may store it within its own memory
// without using any additional heap allocations. Function checks for such case.
template <typename ContainerT>
bool IsContentEmbedded(const ContainerT& container) {
  const uint8_t* container_ptr = reinterpret_cast<const uint8_t*>(&container);
  const uint8_t* data = reinterpret_cast<const uint8_t*>(container.data());

  return data >= container_ptr && data < container_ptr + sizeof(container);
}

// Function returns the estimate (in bytes) of amount of memory that vector
// could allocate externally to store its data content
template <typename T>
int64_t GetExternallyAllocatedMemoryEstimate(const std::vector<T>& container) {
  return IsContentEmbedded(container) ? 0 : container.capacity() * sizeof(T);
}

// Function returns the estimate (in bytes) of amount of memory that string
// could allocate externally to store its data content
inline int64_t GetExternallyAllocatedMemoryEstimate(const std::string& str) {
  return IsContentEmbedded(str) ? 0 : str.capacity() + 1;
}

template <typename ElementT>
static size_t GetArrayAllocationMemoryEstimate(size_t elements_count) {
  size_t result = elements_count * sizeof(ElementT);

  //  Allocation most probably will be aligned, so round up result to alignment
  constexpr size_t alignment = sizeof(void*);
  constexpr size_t max_unaligned_mod = alignment - 1;
  constexpr bool is_alignment_power_of_two =
      (alignment != 0) && ((alignment & max_unaligned_mod) == 0);
  static_assert(is_alignment_power_of_two);

  return (result + max_unaligned_mod) & (~max_unaligned_mod);
}

// Rounds up the capacity to the next power of 2
inline int64_t RoundUpToNextPowerOfTwo(int64_t n) {
  if (n < 0 || (n & (1L << 62)) != 0) {
    ABSL_LOG(ERROR) << "Out of range: " << n;
    // Restrict to the valid range.
    return n < 0 ? 1 : 1L << 62;
  }

  int64_t power = 1;
  while (power <= n) {
    power = power << 1;
  }
  return power;
}

// Capacity is increased by factor of 2: to allocate N elements, smallest
// power of 2 - 1, which is bigger or equal to N elements will be found.
// We must also account for a 7/8 load factor on organically grown maps.
inline int64_t GetRawHashSetCapacityEstimateFromExpectedSize(
    int64_t expected_size) {
  int64_t capacity = RoundUpToNextPowerOfTwo(expected_size) - 1;
  return expected_size <= capacity - capacity / 8
             ? capacity
             : RoundUpToNextPowerOfTwo(capacity + 1) - 1;
}

// Estimate memory allocation of raw_hash_set, which is a base class for
// absl::flat_hash_map, flat_hash_set and node_hash_map
template <typename SetT>
int64_t GetRawHashSetExternallyAllocatedMemoryEstimate(
    const SetT& set, int64_t count_of_expected_items_to_add) {
  // If we know the capacity we should just use it. Otherwise we have to
  // estimate what it will be after the expected number of items are added.
  int64_t capacity = count_of_expected_items_to_add == 0
                         ? set.capacity()
                         : GetRawHashSetCapacityEstimateFromExpectedSize(
                               count_of_expected_items_to_add + set.size());

  if (capacity == 0) {
    return 0;
  }

  // Abseil raw_hash_set, uses two tables: first one is hash slots array, which
  // size is equal to capacity. The second one is array of control state bytes.
  // Control state is kept for each slot. Last table is spit into groups of 16
  // control bytes, table is padded with group size + 1 byte.
  constexpr int control_state_padding = 17;
  return GetArrayAllocationMemoryEstimate<typename SetT::slot_type>(capacity) +
         GetArrayAllocationMemoryEstimate<uint8_t>(capacity +
                                                   control_state_padding);
}

template <typename... Types>
int64_t GetExternallyAllocatedMemoryEstimate(
    const absl::flat_hash_map<Types...>& map,
    int64_t count_of_expected_items_to_add = 0) {
  return GetRawHashSetExternallyAllocatedMemoryEstimate<
      absl::flat_hash_map<Types...>>(map, count_of_expected_items_to_add);
}

template <typename... Types>
int64_t GetExternallyAllocatedMemoryEstimate(
    const absl::flat_hash_set<Types...>& set,
    int64_t count_of_expected_items_to_add = 0) {
  return GetRawHashSetExternallyAllocatedMemoryEstimate<
      absl::flat_hash_set<Types...>>(set, count_of_expected_items_to_add);
}

template <typename... Types>
int64_t GetExternallyAllocatedMemoryEstimate(
    const absl::node_hash_map<Types...>& map,
    int64_t count_of_expected_items_to_add = 0) {
  return GetRawHashSetExternallyAllocatedMemoryEstimate<
      absl::node_hash_map<Types...>>(map, count_of_expected_items_to_add);
}

// Generates a SQL cast expression that casts the literal represented by given
// value (which can have any type V supported by absl::StrCat) to the given
// GoogleSQL type.
// Setting the optional parameter `use_external_float32` to true will return
// FLOAT32 as the type name for TYPE_FLOAT.
// TODO: Remove `use_external_float32` once all engines are
// updated.
template <typename V>
std::string GetCastExpressionString(const V& value, const Type* type,
                                    ProductMode mode,
                                    bool use_external_float32) {
  return absl::StrCat("CAST(", value, " AS ",
                      type->TypeName(mode, use_external_float32), ")");
}
template <typename V>
std::string GetCastExpressionString(const V& value, const Type* type,
                                    ProductMode mode) {
  return GetCastExpressionString(value, type, mode,
                                 /*use_external_float32=*/false);
}

}  // namespace internal
}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_TYPES_INTERNAL_UTILS_H_
