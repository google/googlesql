//
// Copyright 2005 Google LLC
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

// Author: enge@google.com (Lars Engebretsen)
//
// Functions that compute various variations of the so called
// Levenshtein distance, aka edit distance, between two sequences
// (first considered by Levenshtein in Doklady Akademii Nauk SSSR,
// 163(4):845--848, 1965).
//
// All implementations use unit costs for insertions, deletions
// and substitutions. Transpositions are not allowed in Levenshtein
// distance.
//
////////////////////////////////////////////////////////////////////////

#ifndef THIRD_PARTY_FILE_BASED_TEST_DRIVER_BASE_EDIT_DISTANCE_H__
#define THIRD_PARTY_FILE_BASED_TEST_DRIVER_BASE_EDIT_DISTANCE_H__

#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

#include "absl/container/fixed_array.h"

namespace googlesql_base {

// Returns a "capped" Levenshtein distance between sequences.
//
// The parameters seq1_begin, seq1_end, seq2_begin, and seq2_end must
// be random access iterators. Although seq1_begin and seq1_end must
// have the same type, as must seq2_begin and seq2_end, it is not
// necessary that all four have the same type. The fifth parameter,
// equals, must be either a pointer to a function with signature
// The sixth parameter is the capping value.
//
// The algorithm is an optimization of the classic Levenshtein Distance.
// It can be used in cases where it is only important to know the exact
// distance between the supplied distance when this distance is small.
// This makes it possible to optimize the implementation so that the
// space complexity is still O(|seq2|) but the time complexity is improved
// to O(capping_value * |seq1|).
//
// If you think about using this function, make sure to actually time
// it. It probably only makes sense to use it when the capping_value
// is much smaller than |seq2|.
//
// The main idea in this optimized implementation is: Since we only
// have to compute the edit distance exactly if it is less than the
// capping value, it is enough to compute the values that are in a
// stripe of width 2*capping_value around the diagonal in the table d
// defined in <http://en.wikipedia.org/wiki/Levenshtein_distance>.
//
template<typename Ran1, typename Ran2, typename Cmp>
int CappedLevenshteinDistance(const Ran1& seq1_begin, const Ran1& seq1_end,
                              const Ran2& seq2_begin, const Ran2& seq2_end,
                              const Cmp& equals,
                              const int capping_value) {
  const typename std::iterator_traits<Ran1>::difference_type seq1_size
    = seq1_end - seq1_begin;
  const typename std::iterator_traits<Ran2>::difference_type seq2_size
    = seq2_end - seq2_begin;

  if ( seq1_size - seq2_size >= capping_value
       ||
       seq2_size - seq1_size >= capping_value ) {
    return capping_value;
  }

  absl::FixedArray<int> scratch1(seq2_size + 1);
  absl::FixedArray<int> scratch2(seq2_size + 1);

  // Invariant: Right after iteration i (i>0), (*current)[j] contains
  // the edit distance between seq1[0..i) and seq2[0..j). Here [a..b)
  // denotes an interval where the left end is inclusive and the right
  // end non-inclusive. In particular seq1[0..0) is the empty string.
  int* current = scratch1.data();
  int* previous = scratch2.data();

  // Special case for i=0: Distance between empty string and
  // string of length j is always j.
  for ( int j = 0; j <= std::min<int>(seq2_size, capping_value); ++j ) {
    current[j] = j;
  }

  for ( int i = 1; i <= seq1_size; ++i ) {
    // During iteration i, current and previous are swapped (as
    // pointer values) and then the contents of *previous is used
    // to compute new values in *current.
    std::swap(current, previous);

    // Special case for j == 0.
    if ( i <= capping_value ) {
      current[0] = i;  // Deletion is only possibility.
    }
    for ( int j = std::max(1, i - capping_value);
          j <= std::min<int>(seq2_size, i + capping_value);
          ++j ) {
      // We need to be particularly careful about border cases here.
      // When we are at the ends of the stripe, only some types of
      // edits can be beneficial (and the non-beneficial ones cannot
      // be computed).
      int partial_cost = capping_value;
      if ( j > i - capping_value ) {
        partial_cost = std::min(partial_cost,
                                current[j - 1] + 1);  // Insert.
      }
      if ( j < i + capping_value ) {
        partial_cost = std::min(partial_cost,
                                previous[j] + 1);  // Delete.
      }
      // If positions agree, there is no cost associated
      // with copying from i to j; otherwise there is a
      // cost, that of substituting contents of position j
      // with constents of position i.
      const int cost = equals(seq1_begin[i - 1], seq2_begin[j - 1]) ? 0 : 1;
      const int substitution_cost = previous[j - 1] + cost;
      current[j] = std::min(partial_cost, substitution_cost);
    }
  }

  return std::min(current[seq2_size], capping_value);
}

}  // namespace googlesql_base

#endif  // THIRD_PARTY_FILE_BASED_TEST_DRIVER_BASE_EDIT_DISTANCE_H__
