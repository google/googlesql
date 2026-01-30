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

#ifndef GOOGLESQL_REFERENCE_IMPL_TUPLE_COMPARATOR_H_
#define GOOGLESQL_REFERENCE_IMPL_TUPLE_COMPARATOR_H_

#include <memory>
#include <vector>

#include "googlesql/reference_impl/common.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"

namespace googlesql {

class EvaluationContext;
class KeyArg;
struct Tuple;
class TupleData;

class TupleComparator {
 public:
  // Validates the collators in <keys> if non-nullptr, returns both the tuple
  // comparator and the collators upon success. 'slots_for_keys[i]' is the index
  // of 'keys[i]' in a TupleData passed to operator().
  static absl::StatusOr<std::unique_ptr<TupleComparator>> Create(
      absl::Span<const KeyArg* const> keys,
      absl::Span<const int> slots_for_keys,
      absl::Span<const TupleData* const> params, EvaluationContext* context);

  // Same with above, with an additional extra_sort_key_slots parameters.
  static absl::StatusOr<std::unique_ptr<TupleComparator>> Create(
      absl::Span<const KeyArg* const> keys,
      absl::Span<const int> slots_for_keys,
      absl::Span<const int> extra_sort_key_slots,
      absl::Span<const TupleData* const> params, EvaluationContext* context);

  // Returns true if t1 is less than t2.
  bool operator()(const TupleData& t1, const TupleData& t2) const;

  // t1 and t2  must not be NULL.
  bool operator()(const TupleData* t1, const TupleData* t2) const {
    return (*this)(*t1, *t2);
  }

  // t1 and t2 must not be NULL.
  bool operator()(const std::unique_ptr<TupleData>& t1,
                  const std::unique_ptr<TupleData>& t2) const {
    return (*this)(*t1, *t2);
  }

  // Returns true if for any two consecutive tuples a and b in 'tuples'
  // (which must be sorted), either a < b according to this object, or the
  // values corresponding to 'slots_for_values' in a and b are all equal. In
  // this case, not only is 'tuples' sorted according to this object, but that
  // sort order is unique.
  bool IsUniquelyOrdered(absl::Span<const TupleData* const> tuples,
                         absl::Span<const int> slot_idxs_for_values) const;

  // Returns true if any of the comparisons between individual values required
  // to determine `IsUniquelyOrdered` involve comparing arrays with uncertain
  // orders (if order_kind = kIgnoresOrder). Requires that IsUniquelyOrdered
  // is already true.
  bool InvolvesUncertainArrayComparisons(
      absl::Span<const TupleData* const> tuples) const;

  absl::Span<const KeyArg* const> keys() const { return keys_; }

 private:
  TupleComparator(absl::Span<const KeyArg* const> keys,
                  absl::Span<const int> slots_for_keys,
                  absl::Span<const int> extra_sort_key_slots,
                  std::shared_ptr<const CollatorList> collators)
      : keys_(keys.begin(), keys.end()),
        slots_for_keys_(slots_for_keys.begin(), slots_for_keys.end()),
        extra_sort_key_slots_(extra_sort_key_slots.begin(),
                              extra_sort_key_slots.end()),
        collators_(collators) {}

  // `compare_floating_point_approximately`, `has_approximate_comparison`, and
  // `collator_caused_equality` are used in `IsUniquelyOrdered` only. See
  // comments at `IsUniquelyOrdered` below.
  bool Compare(const TupleData& t1, const TupleData& t2,
               bool compare_floating_point_approximately,
               bool* has_approximate_comparison,
               bool* collator_caused_equality) const;

  const std::vector<const KeyArg*> keys_;
  const std::vector<int> slots_for_keys_;

  // This indicates the extra keys being used during sorting, usually it doesn't
  // have a key expression but just have a slot number. The sort specification
  // for extra sort-keys is ASC, and null-first by default. The tuples will be
  // sorted by regular keys at first, then extra keys.
  const std::vector<int> extra_sort_key_slots_;
  // <collators_> indicates the COLLATE specific rules to compare strings for
  // each sort key in <keys_>. This corresponds 1-1 with keys_.
  // NOTE: If any element of <collators_> is nullptr, then the strings are
  // compared based on their UTF-8 encoding.
  // We use std::shared_ptr<const ...> to allow the comparator to be copied.
  const std::shared_ptr<const CollatorList> collators_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_REFERENCE_IMPL_TUPLE_COMPARATOR_H_
