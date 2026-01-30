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

#include <cstddef>
#include <optional>
#include <vector>

#include "googlesql/compliance/functions_testlib_common.h"
#include "googlesql/public/civil_time.h"
#include "googlesql/public/functions/date_time_util.h"
#include "googlesql/public/functions/range.h"
#include "googlesql/public/interval_value.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/testing/test_function.h"
#include "googlesql/testing/test_value.h"
#include "googlesql/testing/using_test_value.cc"  // NOLINT (build/include)
#include "gmock/gmock.h"
#include "googlesql/base/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "absl/types/span.h"

namespace googlesql {

using googlesql::Value;

namespace {

bool HasDatetimeElementType(Value range) {
  return range.type()->IsRangeType() &&
         range.type()->AsRange()->element_type()->kind() ==
             TypeKind::TYPE_DATETIME;
}

void CommonInitialCheck(absl::Span<const Value> values, const Value& unbounded,
                        const Value& null_range) {
  // Verify provided values are of the same type
  // All values must be of the same type, coercion is not allowed
  ABSL_CHECK_EQ(values[0].type_kind(), unbounded.type_kind());
  for (size_t i = 1; i < values.size(); i++) {
    ABSL_CHECK_EQ(values[i - 1].type_kind(), values[i].type_kind());
    ABSL_CHECK(values[i - 1].LessThan(values[i]));
  }

  // Check null_range's type and its range element type
  ABSL_CHECK_EQ(null_range.type_kind(), TypeKind::TYPE_RANGE);
  ABSL_CHECK_EQ(unbounded.type_kind(),
           null_range.type()->AsRange()->element_type()->kind());

  // Check nulls are nulls indeed
  ABSL_CHECK(unbounded.is_null());
  ABSL_CHECK(null_range.is_null());
}

std::vector<FunctionTestCall> WrapFeatures(
    absl::Span<const FunctionTestCall> tests) {
  std::vector<FunctionTestCall> wrapped_tests;
  wrapped_tests.reserve(tests.size());
  for (auto call : tests) {
    call.params = call.params.WrapWithFeature(FEATURE_RANGE_TYPE);
    if (HasDatetimeElementType(call.params.params().front()) ||
        HasDatetimeElementType(call.params.params().back())) {
      call.params = call.params.WrapWithFeature(FEATURE_CIVIL_TIME);
    }
    wrapped_tests.emplace_back(call);
  }
  return wrapped_tests;
}

std::vector<FunctionTestCall> EqualityTests(absl::Span<const Value> values,
                                            const Value& unbounded,
                                            const Value& null_range) {
  // Verify t1 < t2 < t3 and provided values are of the same type
  CommonInitialCheck(values, unbounded, null_range);
  const Value &t1 = values[0], &t2 = values[1], &t3 = values[2];

  return {
      // Same ranges
      {"RangeEquals", {Range(t1, t2), Range(t1, t2)}, Bool(true)},
      // Different ends
      {"RangeEquals", {Range(t1, t3), Range(t1, t2)}, Bool(false)},
      // Different starts
      {"RangeEquals", {Range(t1, t3), Range(t1, t2)}, Bool(false)},
      // Different starts and ends
      {"RangeEquals", {Range(t1, t2), Range(t2, t3)}, Bool(false)},
      // Same ranges with unbounded start
      {"RangeEquals", {Range(unbounded, t2), Range(unbounded, t2)}, Bool(true)},
      // Different ranges, one with unbounded start, same end date
      {"RangeEquals", {Range(unbounded, t2), Range(t1, t2)}, Bool(false)},
      // Different ranges, both with unbounded start, different end date
      {"RangeEquals",
       {Range(unbounded, t2), Range(unbounded, t3)},
       Bool(false)},
      // Same ranges with unbounded end
      {"RangeEquals", {Range(t1, unbounded), Range(t1, unbounded)}, Bool(true)},
      // Different ranges, same start date, one with unbounded end
      {"RangeEquals", {Range(t1, unbounded), Range(t1, t2)}, Bool(false)},
      // Different ranges, different start date, both with unbounded end
      {"RangeEquals",
       {Range(t1, unbounded), Range(t2, unbounded)},
       Bool(false)},
      // Same ranges, unbounded start and end
      {"RangeEquals",
       {Range(unbounded, unbounded), Range(unbounded, unbounded)},
       Bool(true)},
      // Null compared to non-null
      {"RangeEquals", {null_range, Range(t1, t2)}, NullBool()},
  };
}

std::vector<FunctionTestCall> ComparisonTests(absl::Span<const Value> values,
                                              const Value& unbounded,
                                              const Value& null_range) {
  // Verify t1 < t2 < t3 < t4 and provided values are of the same type
  CommonInitialCheck(values, unbounded, null_range);
  const Value &t1 = values[0], &t2 = values[1], &t3 = values[2],
              &t4 = values[3];

  // We only add tests for "=" and "<", because the test driver automatically
  // generates all comparison functions for every test case.
  return {
      // Regular ranges
      // [___)........
      // ........[___)
      {"RangeLessThan", {Range(t1, t2), Range(t3, t4)}, Bool(true)},
      // [___)........
      // ....[_______)
      {"RangeLessThan", {Range(t1, t2), Range(t2, t4)}, Bool(true)},
      // [_______)....
      // ....[_______)
      {"RangeLessThan", {Range(t1, t3), Range(t2, t4)}, Bool(true)},
      // [_______)....
      // ....[___)....
      {"RangeLessThan", {Range(t1, t3), Range(t2, t3)}, Bool(true)},
      // [___________)
      // ....[___)....
      {"RangeLessThan", {Range(t1, t4), Range(t2, t3)}, Bool(true)},
      // [___________)
      // ....[___)....
      {"RangeLessThan", {Range(t1, t4), Range(t2, t3)}, Bool(true)},
      // ....[_______)
      // ....[___)....
      {"RangeLessThan", {Range(t2, t4), Range(t2, t3)}, Bool(false)},
      // ....[___)....
      // ....[___)....
      {"RangeLessThan", {Range(t2, t3), Range(t2, t3)}, Bool(false)},
      // ....[_______)
      // [_______)....
      {"RangeLessThan", {Range(t2, t4), Range(t1, t3)}, Bool(false)},
      // ........[___)
      // [_______)....
      {"RangeLessThan", {Range(t3, t4), Range(t1, t3)}, Bool(false)},
      // ........[___)
      // [___)........
      {"RangeLessThan", {Range(t3, t4), Range(t1, t2)}, Bool(false)},
      // Ranges with unbounded start
      // ____)........
      // ........[___)
      {"RangeLessThan", {Range(unbounded, t2), Range(t3, t4)}, Bool(true)},
      // ____)........
      // ....[___)....
      {"RangeLessThan", {Range(unbounded, t2), Range(t2, t3)}, Bool(true)},
      // ________)....
      // ....[_______)
      {"RangeLessThan", {Range(unbounded, t3), Range(t2, t4)}, Bool(true)},
      // ________)....
      // ....[___)....
      {"RangeLessThan", {Range(unbounded, t3), Range(t2, t3)}, Bool(true)},
      // ____________)
      // ....[___)....
      {"RangeLessThan", {Range(unbounded, t4), Range(t2, t3)}, Bool(true)},
      // Ranges with unbounded end
      // [____________
      // ....[___)....
      {"RangeLessThan", {Range(t1, unbounded), Range(t2, t3)}, Bool(true)},
      // ....[________
      // ....[___)....
      {"RangeLessThan", {Range(t2, unbounded), Range(t2, t3)}, Bool(false)},
      // ........[____
      // ....[_______)
      {"RangeLessThan", {Range(t3, unbounded), Range(t2, t4)}, Bool(false)},
      // ........[____
      // ....[___)....
      {"RangeLessThan", {Range(t3, unbounded), Range(t2, t3)}, Bool(false)},
      // ........[____
      // [___)........
      {"RangeLessThan", {Range(t3, unbounded), Range(t1, t2)}, Bool(false)},
      // Ranges with unbounded start and end
      // _____________
      // ....[___)....
      {"RangeLessThan",
       {Range(unbounded, unbounded), Range(t2, t3)},
       Bool(true)},
      // _____________
      // ....[________
      {"RangeLessThan",
       {Range(unbounded, unbounded), Range(t2, unbounded)},
       Bool(true)},
      // _____________
      // ___).........
      {"RangeLessThan",
       {Range(unbounded, unbounded), Range(unbounded, t2)},
       Bool(false)},
      // _____________
      // _____________
      {"RangeLessThan",
       {Range(unbounded, unbounded), Range(unbounded, unbounded)},
       Bool(false)},
      // Null range
      {"RangeLessThan", {null_range, Range(t2, t3)}, NullBool()},
  };
}

std::vector<FunctionTestCall> RangeOverlapsTests(absl::Span<const Value> values,
                                                 const Value& unbounded,
                                                 const Value& null_range) {
  // Verify t1 < t2 < t3 < t4 and provided values are of the same type
  CommonInitialCheck(values, unbounded, null_range);
  const Value &t1 = values[0], &t2 = values[1], &t3 = values[2],
              &t4 = values[3];

  return {
      // Regular ranges
      // [___)........
      // ........[___)
      {"range_overlaps", {Range(t1, t2), Range(t3, t4)}, Bool(false)},
      // [___)........
      // ....[_______)
      {"range_overlaps", {Range(t1, t2), Range(t2, t4)}, Bool(false)},
      // [_______)....
      // ....[_______)
      {"range_overlaps", {Range(t1, t3), Range(t2, t4)}, Bool(true)},
      // [_______)....
      // ....[___)....
      {"range_overlaps", {Range(t1, t3), Range(t2, t3)}, Bool(true)},
      // [___________)
      // ....[___)....
      {"range_overlaps", {Range(t1, t4), Range(t2, t3)}, Bool(true)},
      // ....[_______)
      // ....[___)....
      {"range_overlaps", {Range(t2, t4), Range(t2, t3)}, Bool(true)},
      // ....[___)....
      // ....[___)....
      {"range_overlaps", {Range(t2, t3), Range(t2, t3)}, Bool(true)},
      // ....[_______)
      // [_______)....
      {"range_overlaps", {Range(t2, t4), Range(t1, t3)}, Bool(true)},
      // ........[___)
      // [_______)....
      {"range_overlaps", {Range(t3, t4), Range(t1, t3)}, Bool(false)},
      // ........[___)
      // [___)........
      {"range_overlaps", {Range(t3, t4), Range(t1, t2)}, Bool(false)},
      // First Range with unbounded start
      // ___)........
      // ........[___)
      {"range_overlaps", {Range(unbounded, t2), Range(t3, t4)}, Bool(false)},
      // ____)........
      // ....[_______)
      {"range_overlaps", {Range(unbounded, t2), Range(t2, t4)}, Bool(false)},
      // _______)....
      // ....[_______)
      {"range_overlaps", {Range(unbounded, t3), Range(t2, t4)}, Bool(true)},
      // ________)....
      // ....[___)....
      {"range_overlaps", {Range(unbounded, t3), Range(t2, t3)}, Bool(true)},
      // ___________)
      // ....[___)....
      {"range_overlaps", {Range(unbounded, t4), Range(t2, t3)}, Bool(true)},
      // ____________)
      // [_______)....
      {"range_overlaps", {Range(unbounded, t4), Range(t1, t3)}, Bool(true)},
      // ____________)
      // [___)........
      {"range_overlaps", {Range(unbounded, t4), Range(t1, t2)}, Bool(true)},
      // First Range with unbounded end
      // [___________
      // ........[___)
      {"range_overlaps", {Range(t1, unbounded), Range(t3, t4)}, Bool(true)},
      // ....[________
      // ....[___)....
      {"range_overlaps", {Range(t2, unbounded), Range(t2, t3)}, Bool(true)},
      // ....[________
      // [_______)....
      {"range_overlaps", {Range(t2, unbounded), Range(t1, t3)}, Bool(true)},
      // ........[____
      // [_______)....
      {"range_overlaps", {Range(t3, unbounded), Range(t1, t3)}, Bool(false)},
      // ........[____
      // [___)........
      {"range_overlaps", {Range(t3, unbounded), Range(t1, t2)}, Bool(false)},
      // Second Range with unbounded start
      // [___)........
      // ____________)
      {"range_overlaps", {Range(t1, t2), Range(unbounded, t4)}, Bool(true)},
      // [_______)....
      //  _______)....
      {"range_overlaps", {Range(t1, t3), Range(unbounded, t3)}, Bool(true)},
      // [___________)
      //  ________)...
      {"range_overlaps", {Range(t1, t4), Range(unbounded, t3)}, Bool(true)},
      // ....[_______)
      // ________)....
      {"range_overlaps", {Range(t2, t4), Range(unbounded, t3)}, Bool(true)},
      // ........[___)
      // ________)....
      {"range_overlaps", {Range(t3, t4), Range(unbounded, t3)}, Bool(false)},
      // ........[___)
      // ____)........
      {"range_overlaps", {Range(t3, t4), Range(unbounded, t2)}, Bool(false)},
      // Second Range with unbounded end
      // [___)........
      // ........[____
      {"range_overlaps", {Range(t1, t2), Range(t3, unbounded)}, Bool(false)},
      // [___)........
      // ....[________
      {"range_overlaps", {Range(t1, t2), Range(t2, unbounded)}, Bool(false)},
      // [_______)....
      // ....[________
      {"range_overlaps", {Range(t1, t3), Range(t2, unbounded)}, Bool(true)},
      // ....[_______)
      // ....[_______
      {"range_overlaps", {Range(t2, t4), Range(t2, unbounded)}, Bool(true)},
      // ....[_______)
      // [___________
      {"range_overlaps", {Range(t2, t4), Range(t1, unbounded)}, Bool(true)},
      // First Range with unbounded start and Second Range with unbounded start
      // ___)........
      // ___________)
      {"range_overlaps",
       {Range(unbounded, t2), Range(unbounded, t4)},
       Bool(true)},
      // ________)....
      // ________)....
      {"range_overlaps",
       {Range(unbounded, t3), Range(unbounded, t3)},
       Bool(true)},
      // ____________)
      // _______)....
      {"range_overlaps",
       {Range(unbounded, t4), Range(unbounded, t3)},
       Bool(true)},
      // First Range with unbounded start and Second Range with unbounded end
      // ____)........
      // ........[____
      {"range_overlaps",
       {Range(unbounded, t2), Range(t3, unbounded)},
       Bool(false)},
      // ____)........
      // ....[________
      {"range_overlaps",
       {Range(unbounded, t2), Range(t2, unbounded)},
       Bool(false)},
      // ________)....
      // ....[________
      {"range_overlaps",
       {Range(unbounded, t3), Range(t2, unbounded)},
       Bool(true)},
      // ____________)
      // [___________
      {"range_overlaps",
       {Range(unbounded, t4), Range(t1, unbounded)},
       Bool(true)},
      // First Range with unbounded end and Second Range with unbounded start
      // [___________
      //  ___________)
      {"range_overlaps",
       {Range(t1, unbounded), Range(unbounded, t4)},
       Bool(true)},
      // ....[________
      // _______)....
      {"range_overlaps",
       {Range(t2, unbounded), Range(unbounded, t3)},
       Bool(true)},
      // ........[____
      // ________)....
      {"range_overlaps",
       {Range(t3, unbounded), Range(unbounded, t3)},
       Bool(false)},
      // ........[____
      // ____)........
      {"range_overlaps",
       {Range(t3, unbounded), Range(unbounded, t2)},
       Bool(false)},
      // First Range with unbounded end and Second Range with unbounded end
      // [___________
      // ........[___
      {"range_overlaps",
       {Range(t1, unbounded), Range(t3, unbounded)},
       Bool(true)},
      // ....[________
      // ....[________
      {"range_overlaps",
       {Range(t2, unbounded), Range(t2, unbounded)},
       Bool(true)},
      // ....[________
      // [____________
      {"range_overlaps",
       {Range(t2, unbounded), Range(t1, unbounded)},
       Bool(true)},
      // First Range with unbounded start and end
      // _____________
      // ....[___)....
      {"range_overlaps",
       {Range(unbounded, unbounded), Range(t2, t3)},
       Bool(true)},
      // _____________
      // ....[________
      {"range_overlaps",
       {Range(unbounded, unbounded), Range(t2, unbounded)},
       Bool(true)},
      // _____________
      // ___).........
      {"range_overlaps",
       {Range(unbounded, unbounded), Range(unbounded, t2)},
       Bool(true)},
      // Second Range with unbounded start and end
      // ....[___)....
      // _____________
      {"range_overlaps",
       {Range(t2, t3), Range(unbounded, unbounded)},
       Bool(true)},
      // ....[________
      // _____________
      {"range_overlaps",
       {Range(t2, unbounded), Range(unbounded, unbounded)},
       Bool(true)},
      // ___).........
      // _____________
      {"range_overlaps",
       {Range(unbounded, t2), Range(unbounded, unbounded)},
       Bool(true)},
      // Both Range have unbounded start and end
      // _____________
      // _____________
      {"range_overlaps",
       {Range(unbounded, unbounded), Range(unbounded, unbounded)},
       Bool(true)},
      // Null Ranges
      {"range_overlaps", {null_range, Range(t2, t3)}, NullBool()},
      {"range_overlaps", {null_range, Range(unbounded, t3)}, NullBool()},
      {"range_overlaps", {null_range, Range(t2, unbounded)}, NullBool()},
      {"range_overlaps", {null_range, Range(unbounded, unbounded)}, NullBool()},
      {"range_overlaps", {Range(t2, t3), null_range}, NullBool()},
      {"range_overlaps", {Range(unbounded, t3), null_range}, NullBool()},
      {"range_overlaps", {Range(t2, unbounded), null_range}, NullBool()},
      {"range_overlaps", {Range(unbounded, unbounded), null_range}, NullBool()},
      {"range_overlaps", {null_range, null_range}, NullBool()},
  };
}

std::vector<FunctionTestCall> RangeIntersectTests(
    absl::Span<const Value> values, const Value& unbounded,
    const Value& null_range) {
  // Verify t1 < t2 < t3 < t4 and provided values are of the same type
  CommonInitialCheck(values, unbounded, null_range);
  const Value &t1 = values[0], &t2 = values[1], &t3 = values[2],
              &t4 = values[3];

  return {
      // Regular ranges
      // [_______)....
      // ....[_______)
      {"range_intersect", {Range(t1, t3), Range(t2, t4)}, Range(t2, t3)},
      // [_______)....
      // ....[___)....
      {"range_intersect", {Range(t1, t3), Range(t2, t3)}, Range(t2, t3)},
      // [___________)
      // ....[___)....
      {"range_intersect", {Range(t1, t4), Range(t2, t3)}, Range(t2, t3)},
      // ....[_______)
      // ....[___)....
      {"range_intersect", {Range(t2, t4), Range(t2, t3)}, Range(t2, t3)},
      // ....[___)....
      // ....[___)....
      {"range_intersect", {Range(t2, t3), Range(t2, t3)}, Range(t2, t3)},
      // ....[_______)
      // [_______)....
      {"range_intersect", {Range(t2, t4), Range(t1, t3)}, Range(t2, t3)},
      // First Range with unbounded start
      // _______)....
      // ....[_______)
      {"range_intersect", {Range(unbounded, t3), Range(t2, t4)}, Range(t2, t3)},
      // ________)....
      // ....[___)....
      {"range_intersect", {Range(unbounded, t3), Range(t2, t3)}, Range(t2, t3)},
      // ___________)
      // ....[___)....
      {"range_intersect", {Range(unbounded, t4), Range(t2, t3)}, Range(t2, t3)},
      // ____________)
      // [_______)....
      {"range_intersect", {Range(unbounded, t4), Range(t1, t3)}, Range(t1, t3)},
      // ____________)
      // [___)........
      {"range_intersect", {Range(unbounded, t4), Range(t1, t2)}, Range(t1, t2)},
      // First Range with unbounded end
      // [___________
      // ........[___)
      {"range_intersect", {Range(t1, unbounded), Range(t3, t4)}, Range(t3, t4)},
      // ....[________
      // ....[___)....
      {"range_intersect", {Range(t2, unbounded), Range(t2, t3)}, Range(t2, t3)},
      // ....[________
      // [_______)....
      {"range_intersect", {Range(t2, unbounded), Range(t1, t3)}, Range(t2, t3)},
      // Second Range with unbounded start
      // [___)........
      // ____________)
      {"range_intersect", {Range(t1, t2), Range(unbounded, t4)}, Range(t1, t2)},
      // [_______)....
      //  _______)....
      {"range_intersect", {Range(t1, t3), Range(unbounded, t3)}, Range(t1, t3)},
      // [___________)
      //  ________)...
      {"range_intersect", {Range(t1, t4), Range(unbounded, t3)}, Range(t1, t3)},
      // ....[_______)
      // ________)....
      {"range_intersect", {Range(t2, t4), Range(unbounded, t3)}, Range(t2, t3)},
      // Second Range with unbounded end
      // [_______)....
      // ....[________
      {"range_intersect", {Range(t1, t3), Range(t2, unbounded)}, Range(t2, t3)},
      // ....[_______)
      // ....[_______
      {"range_intersect", {Range(t2, t4), Range(t2, unbounded)}, Range(t2, t4)},
      // ....[_______)
      // [___________
      {"range_intersect", {Range(t2, t4), Range(t1, unbounded)}, Range(t2, t4)},
      // First Range with unbounded start and Second Range with unbounded start
      // ___)........
      // ___________)
      {"range_intersect",
       {Range(unbounded, t2), Range(unbounded, t4)},
       Range(unbounded, t2)},
      // ________)....
      // ________)....
      {"range_intersect",
       {Range(unbounded, t3), Range(unbounded, t3)},
       Range(unbounded, t3)},
      // ____________)
      // _______)....
      {"range_intersect",
       {Range(unbounded, t4), Range(unbounded, t3)},
       Range(unbounded, t3)},
      // First Range with unbounded start and Second Range with unbounded end
      // ________)....
      // ....[________
      {"range_intersect",
       {Range(unbounded, t3), Range(t2, unbounded)},
       Range(t2, t3)},
      // ____________)
      // [___________
      {"range_intersect",
       {Range(unbounded, t4), Range(t1, unbounded)},
       Range(t1, t4)},
      // First Range with unbounded end and Second Range with unbounded start
      // [___________
      //  ___________)
      {"range_intersect",
       {Range(t1, unbounded), Range(unbounded, t4)},
       Range(t1, t4)},
      // ....[________
      // _______)....
      {"range_intersect",
       {Range(t2, unbounded), Range(unbounded, t3)},
       Range(t2, t3)},
      // First Range with unbounded end and Second Range with unbounded end
      // [___________
      // ........[___
      {"range_intersect",
       {Range(t1, unbounded), Range(t3, unbounded)},
       Range(t3, unbounded)},
      // ....[________
      // ....[________
      {"range_intersect",
       {Range(t2, unbounded), Range(t2, unbounded)},
       Range(t2, unbounded)},
      // ....[________
      // [____________
      {"range_intersect",
       {Range(t2, unbounded), Range(t1, unbounded)},
       Range(t2, unbounded)},
      // First Range with unbounded start and end
      // _____________
      // ....[___)....
      {"range_intersect",
       {Range(unbounded, unbounded), Range(t2, t3)},
       Range(t2, t3)},
      // _____________
      // ....[________
      {"range_intersect",
       {Range(unbounded, unbounded), Range(t2, unbounded)},
       Range(t2, unbounded)},
      // _____________
      // ___).........
      {"range_intersect",
       {Range(unbounded, unbounded), Range(unbounded, t2)},
       Range(unbounded, t2)},
      // Second Range with unbounded start and end
      // ....[___)....
      // _____________
      {"range_intersect",
       {Range(t2, t3), Range(unbounded, unbounded)},
       Range(t2, t3)},
      // ....[________
      // _____________
      {"range_intersect",
       {Range(t2, unbounded), Range(unbounded, unbounded)},
       Range(t2, unbounded)},
      // ___).........
      // _____________
      {"range_intersect",
       {Range(unbounded, t2), Range(unbounded, unbounded)},
       Range(unbounded, t2)},
      // Both Ranges have unbounded start and end
      // _____________
      // _____________
      {"range_intersect",
       {Range(unbounded, unbounded), Range(unbounded, unbounded)},
       Range(unbounded, unbounded)},
      // Null Ranges
      {"range_intersect", {null_range, Range(t2, t3)}, null_range},
      {"range_intersect", {null_range, Range(unbounded, t3)}, null_range},
      {"range_intersect", {null_range, Range(t2, unbounded)}, null_range},
      {"range_intersect",
       {null_range, Range(unbounded, unbounded)},
       null_range},
      {"range_intersect", {Range(t2, t3), null_range}, null_range},
      {"range_intersect", {Range(unbounded, t3), null_range}, null_range},
      {"range_intersect", {Range(t2, unbounded), null_range}, null_range},
      {"range_intersect",
       {Range(unbounded, unbounded), null_range},
       null_range},
      {"range_intersect", {null_range, null_range}, null_range},
  };
}

std::vector<FunctionTestCall> RangeContainsTests(absl::Span<const Value> values,
                                                 const Value& unbounded,
                                                 const Value& null_range) {
  // Verify t1 < t2 < t3 < t4 < t5 and provided values are of the same type
  CommonInitialCheck(values, unbounded, null_range);
  const Value &t1 = values[0], &t2 = values[1], &t3 = values[2],
              &t4 = values[3], &t5 = values[4];

  std::vector<FunctionTestCall> range_contains_tests;
  std::vector<FunctionTestCall> range_contains_tests_with_range = {
      // Regular ranges
      // [___)........
      // ........[___)
      {"range_contains", {Range(t1, t2), Range(t3, t4)}, Bool(false)},
      // [___)........
      // ....[_______)
      {"range_contains", {Range(t1, t2), Range(t2, t4)}, Bool(false)},
      // [_______)....
      // ....[_______)
      {"range_contains", {Range(t1, t3), Range(t2, t4)}, Bool(false)},
      // [_______)....
      // ....[___)....
      {"range_contains", {Range(t1, t3), Range(t2, t3)}, Bool(true)},
      // [___________)
      // ....[___)....
      {"range_contains", {Range(t1, t4), Range(t2, t3)}, Bool(true)},
      // ....[_______)
      // ....[___)....
      {"range_contains", {Range(t2, t4), Range(t2, t3)}, Bool(true)},
      // ....[___)....
      // ....[___)....
      {"range_contains", {Range(t2, t3), Range(t2, t3)}, Bool(true)},
      // ....[_______)
      // [_______)....
      {"range_contains", {Range(t2, t4), Range(t1, t3)}, Bool(false)},
      // ........[___)
      // [_______)....
      {"range_contains", {Range(t3, t4), Range(t1, t3)}, Bool(false)},
      // ........[___)
      // [___)........
      {"range_contains", {Range(t3, t4), Range(t1, t2)}, Bool(false)},
      // First Range with unbounded start
      // ___)........
      // ........[___)
      {"range_contains", {Range(unbounded, t2), Range(t3, t4)}, Bool(false)},
      // ____)........
      // ....[_______)
      {"range_contains", {Range(unbounded, t2), Range(t2, t4)}, Bool(false)},
      // _______)....
      // ....[_______)
      {"range_contains", {Range(unbounded, t3), Range(t2, t4)}, Bool(false)},
      // ________)....
      // ....[___)....
      {"range_contains", {Range(unbounded, t3), Range(t2, t3)}, Bool(true)},
      // ___________)
      // ....[___)....
      {"range_contains", {Range(unbounded, t4), Range(t2, t3)}, Bool(true)},
      // ____________)
      // [_______)....
      {"range_contains", {Range(unbounded, t4), Range(t1, t3)}, Bool(true)},
      // First Range with unbounded end
      // [___________
      // ........[___)
      {"range_contains", {Range(t1, unbounded), Range(t3, t4)}, Bool(true)},
      // ....[________
      // ....[___)....
      {"range_contains", {Range(t2, unbounded), Range(t2, t3)}, Bool(true)},
      // ....[________
      // [_______)....
      {"range_contains", {Range(t2, unbounded), Range(t1, t3)}, Bool(false)},
      // ........[____
      // [_______)....
      {"range_contains", {Range(t3, unbounded), Range(t1, t3)}, Bool(false)},
      // ........[____
      // [___)........
      {"range_contains", {Range(t3, unbounded), Range(t1, t2)}, Bool(false)},
      // Second Range with unbounded start
      // [___)........
      // ____________)
      {"range_contains", {Range(t1, t2), Range(unbounded, t4)}, Bool(false)},
      // [_______)....
      //  _______)....
      {"range_contains", {Range(t1, t3), Range(unbounded, t3)}, Bool(false)},
      // [___________)
      //  ________)...
      {"range_contains", {Range(t1, t4), Range(unbounded, t3)}, Bool(false)},
      // ....[_______)
      // ________)....
      {"range_contains", {Range(t2, t4), Range(unbounded, t3)}, Bool(false)},
      // ........[___)
      // ________)....
      {"range_contains", {Range(t3, t4), Range(unbounded, t3)}, Bool(false)},
      // ........[___)
      // ____)........
      {"range_contains", {Range(t3, t4), Range(unbounded, t2)}, Bool(false)},
      // Second Range with unbounded end
      // [___)........
      // ........[____
      {"range_contains", {Range(t1, t2), Range(t3, unbounded)}, Bool(false)},
      // [___)........
      // ....[________
      {"range_contains", {Range(t1, t2), Range(t2, unbounded)}, Bool(false)},
      // [_______)....
      // ....[________
      {"range_contains", {Range(t1, t3), Range(t2, unbounded)}, Bool(false)},
      // ....[_______)
      // ....[_______
      {"range_contains", {Range(t2, t4), Range(t2, unbounded)}, Bool(false)},
      // ....[_______)
      // [___________
      {"range_contains", {Range(t2, t4), Range(t1, unbounded)}, Bool(false)},
      // First Range with unbounded start and Second Range with unbounded start
      // ___)........
      // ___________)
      {"range_contains",
       {Range(unbounded, t2), Range(unbounded, t4)},
       Bool(false)},
      // ________)....
      // ________)....
      {"range_contains",
       {Range(unbounded, t3), Range(unbounded, t3)},
       Bool(true)},
      // ____________)
      // _______)....
      {"range_contains",
       {Range(unbounded, t4), Range(unbounded, t3)},
       Bool(true)},
      // First Range with unbounded start and Second Range with unbounded end
      // ____)........
      // ........[____
      {"range_contains",
       {Range(unbounded, t2), Range(t3, unbounded)},
       Bool(false)},
      // ____)........
      // ....[________
      {"range_contains",
       {Range(unbounded, t2), Range(t2, unbounded)},
       Bool(false)},
      // ________)....
      // ....[________
      {"range_contains",
       {Range(unbounded, t3), Range(t2, unbounded)},
       Bool(false)},
      // ____________)
      // [___________
      {"range_contains",
       {Range(unbounded, t4), Range(t1, unbounded)},
       Bool(false)},
      // First Range with unbounded end and Second Range with unbounded start
      // [___________
      //  ___________)
      {"range_contains",
       {Range(t1, unbounded), Range(unbounded, t4)},
       Bool(false)},
      // ....[________
      // _______)....
      {"range_contains",
       {Range(t2, unbounded), Range(unbounded, t3)},
       Bool(false)},
      // ........[____
      // ________)....
      {"range_contains",
       {Range(t3, unbounded), Range(unbounded, t3)},
       Bool(false)},
      // ........[____
      // ____)........
      {"range_contains",
       {Range(t3, unbounded), Range(unbounded, t2)},
       Bool(false)},
      // First Range with unbounded end and Second Range with unbounded end
      // [___________
      // ........[___
      {"range_contains",
       {Range(t1, unbounded), Range(t3, unbounded)},
       Bool(true)},
      // ....[________
      // ....[________
      {"range_contains",
       {Range(t2, unbounded), Range(t2, unbounded)},
       Bool(true)},
      // ....[________
      // [____________
      {"range_contains",
       {Range(t2, unbounded), Range(t1, unbounded)},
       Bool(false)},
      // First Range with unbounded start and end
      // _____________
      // ....[___)....
      {"range_contains",
       {Range(unbounded, unbounded), Range(t2, t3)},
       Bool(true)},
      // _____________
      // ....[________
      {"range_contains",
       {Range(unbounded, unbounded), Range(t2, unbounded)},
       Bool(true)},
      // _____________
      // ___).........
      {"range_contains",
       {Range(unbounded, unbounded), Range(unbounded, t2)},
       Bool(true)},
      // Second Range with unbounded start and end
      // ....[___)....
      // _____________
      {"range_contains",
       {Range(t2, t3), Range(unbounded, unbounded)},
       Bool(false)},
      // ....[________
      // _____________
      {"range_contains",
       {Range(t2, unbounded), Range(unbounded, unbounded)},
       Bool(false)},
      // ___).........
      // _____________
      {"range_contains",
       {Range(unbounded, t2), Range(unbounded, unbounded)},
       Bool(false)},
      // Both Range have unbounded start and end
      // _____________
      // _____________
      {"range_contains",
       {Range(unbounded, unbounded), Range(unbounded, unbounded)},
       Bool(true)},
      // Null Ranges
      {"range_contains", {null_range, Range(t2, t3)}, NullBool()},
      {"range_contains", {null_range, Range(unbounded, t3)}, NullBool()},
      {"range_contains", {null_range, Range(t2, unbounded)}, NullBool()},
      {"range_contains", {null_range, Range(unbounded, unbounded)}, NullBool()},
      {"range_contains", {Range(t2, t3), null_range}, NullBool()},
      {"range_contains", {Range(unbounded, t3), null_range}, NullBool()},
      {"range_contains", {Range(t2, unbounded), null_range}, NullBool()},
      {"range_contains", {Range(unbounded, unbounded), null_range}, NullBool()},
      {"range_contains", {null_range, null_range}, NullBool()},
  };

  std::vector<FunctionTestCall> range_contains_tests_with_value = {
      // Regular range
      // ...[____)....
      // .|...........
      {"range_contains", {Range(t2, t4), t1}, Bool(false)},
      // ...[____)....
      // ...|.........
      {"range_contains", {Range(t2, t4), t2}, Bool(true)},
      // ...[____)....
      // ......|......
      {"range_contains", {Range(t2, t4), t3}, Bool(true)},
      // ...[____)....
      // ........|....
      {"range_contains", {Range(t2, t4), t4}, Bool(false)},
      // ...[____)....
      // ...........|.
      {"range_contains", {Range(t2, t4), t5}, Bool(false)},
      // Range with unbounded start
      // ___)........
      // .|..........
      {"range_contains", {Range(unbounded, t2), t1}, Bool(true)},
      // ___)........
      // ...|........
      {"range_contains", {Range(unbounded, t2), t2}, Bool(false)},
      // ___)........
      // ......|.....
      {"range_contains", {Range(unbounded, t2), t3}, Bool(false)},
      // Range with unbounded end
      // ..[_________
      // |...........
      {"range_contains", {Range(t2, unbounded), t1}, Bool(false)},
      // ..[_________
      // ..|..........
      {"range_contains", {Range(t2, unbounded), t2}, Bool(true)},
      // ..[_________
      // ......|......
      {"range_contains", {Range(t2, unbounded), t3}, Bool(true)},
      // Range with unbounded start and end
      // _____________
      // ...|..........
      {"range_contains", {Range(unbounded, unbounded), t2}, Bool(true)},
      // Null parameters
      {"range_contains", {null_range, t1}, NullBool()},
      {"range_contains", {Range(t2, t3), unbounded}, NullBool()},
      {"range_contains", {null_range, unbounded}, NullBool()},
  };

  range_contains_tests.insert(range_contains_tests.end(),
                              range_contains_tests_with_range.begin(),
                              range_contains_tests_with_range.end());
  range_contains_tests.insert(range_contains_tests.end(),
                              range_contains_tests_with_value.begin(),
                              range_contains_tests_with_value.end());
  return range_contains_tests;
}

Value RangeElementFromStr(std::optional<absl::string_view> element_string,
                          const Type* element_type,
                          functions::TimestampScale scale) {
  if (!element_string.has_value()) {
    return Value::Null(element_type);
  }

  Value value;
  switch (element_type->kind()) {
    case TypeKind::TYPE_DATE:
      return DateFromStr(*element_string);
    case TypeKind::TYPE_DATETIME:
      return DatetimeFromStr(*element_string, scale);
    case TypeKind::TYPE_TIMESTAMP:
      return TimestampFromStr(*element_string, scale);
    default:
      ABSL_LOG(FATAL) << "Unsupported RANGE element type: "
                 << TypeKind_Name(element_type->kind());
  }
}

Value RangeFromStr(absl::string_view range_string, const Type* range_type,
                   functions::TimestampScale scale) {
  absl::StatusOr<StringRangeBoundaries> boundaries =
      ParseRangeBoundaries(range_string);
  GOOGLESQL_CHECK_OK(boundaries) << range_string;
  const Type* element_type = range_type->AsRange()->element_type();
  Value start = RangeElementFromStr(boundaries->start, element_type, scale);
  Value end = RangeElementFromStr(boundaries->end, element_type, scale);
  absl::StatusOr<Value> range = Value::MakeRange(start, end);
  GOOGLESQL_CHECK_OK(range) << range_string;
  return *range;
}

}  // namespace

// Helper factory methods for RANGE tests
std::vector<Value> GetIncreasingDateValues() {
  return {Value::Date(1), Value::Date(2), Value::Date(3), Value::Date(4),
          Value::Date(5)};
}

// TODO: Add test cases with nanoseconds precision.
std::vector<Value> GetIncreasingDatetimeValues() {
  return {
      Value::Datetime(DatetimeValue::FromYMDHMSAndMicros(1, 1, 1, 1, 1, 1, 1)),
      Value::Datetime(DatetimeValue::FromYMDHMSAndMicros(1, 1, 2, 1, 1, 1, 1)),
      Value::Datetime(DatetimeValue::FromYMDHMSAndMicros(1, 1, 3, 1, 1, 1, 1)),
      Value::Datetime(DatetimeValue::FromYMDHMSAndMicros(1, 1, 4, 1, 1, 1, 1)),
      Value::Datetime(DatetimeValue::FromYMDHMSAndMicros(1, 1, 5, 1, 1, 1, 1)),
  };
}

// TODO: Add test cases with nanoseconds precision.
std::vector<Value> GetIncreasingTimestampValues() {
  const absl::TimeZone utc = absl::UTCTimeZone();
  return {Value::Timestamp(absl::FromCivil(
              absl::CivilSecond(1111, 01, 01, 01, 01, 01), utc)),
          Value::Timestamp(absl::FromCivil(
              absl::CivilSecond(1111, 01, 02, 01, 01, 01), utc)),
          Value::Timestamp(absl::FromCivil(
              absl::CivilSecond(1111, 01, 03, 01, 01, 01), utc)),
          Value::Timestamp(absl::FromCivil(
              absl::CivilSecond(1111, 01, 04, 01, 01, 01), utc)),
          Value::Timestamp(absl::FromCivil(
              absl::CivilSecond(1111, 01, 05, 01, 01, 01), utc))};
}

std::vector<FunctionTestCall> GetFunctionTestsRangeComparisons() {
  const std::vector<Value>& dates = GetIncreasingDateValues();
  const Value null_date = Value::NullDate();
  const Value null_date_range = Value::Null(types::DateRangeType());

  const std::vector<Value>& datetimes = GetIncreasingDatetimeValues();
  const Value null_datetime = Value::NullDatetime();
  const Value null_datetime_range = Value::Null(types::DatetimeRangeType());

  const std::vector<Value>& timestamps = GetIncreasingTimestampValues();
  const Value null_timestamp = Value::NullTimestamp();
  const Value null_timestamp_range = Value::Null(types::TimestampRangeType());

  std::vector<FunctionTestCall> tests;
  // equality tests
  std::vector<FunctionTestCall> date_eq_tests =
      EqualityTests(dates, null_date, null_date_range);
  tests.insert(tests.end(), date_eq_tests.begin(), date_eq_tests.end());
  std::vector<FunctionTestCall> datetime_eq_tests =
      EqualityTests(datetimes, null_datetime, null_datetime_range);
  tests.insert(tests.end(), datetime_eq_tests.begin(), datetime_eq_tests.end());
  std::vector<FunctionTestCall> ts_eq_tests =
      EqualityTests(timestamps, null_timestamp, null_timestamp_range);
  tests.insert(tests.end(), ts_eq_tests.begin(), ts_eq_tests.end());

  // comparison tests
  std::vector<FunctionTestCall> date_cmp_tests =
      ComparisonTests(dates, null_date, null_date_range);
  tests.insert(tests.end(), date_cmp_tests.begin(), date_cmp_tests.end());
  std::vector<FunctionTestCall> datetime_cmp_tests =
      ComparisonTests(datetimes, null_datetime, null_datetime_range);
  tests.insert(tests.end(), datetime_cmp_tests.begin(),
               datetime_cmp_tests.end());
  std::vector<FunctionTestCall> ts_cmp_tests =
      ComparisonTests(timestamps, null_timestamp, null_timestamp_range);
  tests.insert(tests.end(), ts_cmp_tests.begin(), ts_cmp_tests.end());

  return WrapFeatures(tests);
}

std::vector<FunctionTestCall> GetFunctionTestsRangeOverlaps() {
  const std::vector<Value>& dates = GetIncreasingDateValues();
  const Value null_date = Value::NullDate();
  const Value null_date_range = Value::Null(types::DateRangeType());

  const std::vector<Value>& datetimes = GetIncreasingDatetimeValues();
  const Value null_datetime = Value::NullDatetime();
  const Value null_datetime_range = Value::Null(types::DatetimeRangeType());

  const std::vector<Value>& timestamps = GetIncreasingTimestampValues();
  const Value null_timestamp = Value::NullTimestamp();
  const Value null_timestamp_range = Value::Null(types::TimestampRangeType());

  // range_overlaps tests
  std::vector<FunctionTestCall> tests;
  std::vector<FunctionTestCall> date_overlaps_tests =
      RangeOverlapsTests(dates, null_date, null_date_range);
  tests.insert(tests.end(), date_overlaps_tests.begin(),
               date_overlaps_tests.end());

  std::vector<FunctionTestCall> datetime_overlaps_tests =
      RangeOverlapsTests(datetimes, null_datetime, null_datetime_range);
  tests.insert(tests.end(), datetime_overlaps_tests.begin(),
               datetime_overlaps_tests.end());

  std::vector<FunctionTestCall> ts_overlaps_tests =
      RangeOverlapsTests(timestamps, null_timestamp, null_timestamp_range);
  tests.insert(tests.end(), ts_overlaps_tests.begin(), ts_overlaps_tests.end());

  return WrapFeatures(tests);
}

std::vector<FunctionTestCall> GetFunctionTestsRangeIntersect() {
  const std::vector<Value>& dates = GetIncreasingDateValues();
  const Value null_date = Value::NullDate();
  const Value null_date_range = Value::Null(types::DateRangeType());

  const std::vector<Value>& datetimes = GetIncreasingDatetimeValues();
  const Value null_datetime = Value::NullDatetime();
  const Value null_datetime_range = Value::Null(types::DatetimeRangeType());

  const std::vector<Value>& timestamps = GetIncreasingTimestampValues();
  const Value null_timestamp = Value::NullTimestamp();
  const Value null_timestamp_range = Value::Null(types::TimestampRangeType());

  // range_intersect tests
  std::vector<FunctionTestCall> tests;
  std::vector<FunctionTestCall> date_intersect_tests =
      RangeIntersectTests(dates, null_date, null_date_range);
  tests.insert(tests.end(), date_intersect_tests.begin(),
               date_intersect_tests.end());

  std::vector<FunctionTestCall> datetime_intersect_tests =
      RangeIntersectTests(datetimes, null_datetime, null_datetime_range);
  tests.insert(tests.end(), datetime_intersect_tests.begin(),
               datetime_intersect_tests.end());

  std::vector<FunctionTestCall> ts_intersect_tests =
      RangeIntersectTests(timestamps, null_timestamp, null_timestamp_range);
  tests.insert(tests.end(), ts_intersect_tests.begin(),
               ts_intersect_tests.end());

  return WrapFeatures(tests);
}

std::vector<FunctionTestCall> GetFunctionTestsRangeContains() {
  const std::vector<Value>& dates = GetIncreasingDateValues();
  const Value null_date = Value::NullDate();
  const Value null_date_range = Value::Null(types::DateRangeType());

  const std::vector<Value>& datetimes = GetIncreasingDatetimeValues();
  const Value null_datetime = Value::NullDatetime();
  const Value null_datetime_range = Value::Null(types::DatetimeRangeType());

  const std::vector<Value>& timestamps = GetIncreasingTimestampValues();
  const Value null_timestamp = Value::NullTimestamp();
  const Value null_timestamp_range = Value::Null(types::TimestampRangeType());

  // range_contains tests
  std::vector<FunctionTestCall> tests;
  std::vector<FunctionTestCall> date_contains_tests =
      RangeContainsTests(dates, null_date, null_date_range);
  tests.insert(tests.end(), date_contains_tests.begin(),
               date_contains_tests.end());

  std::vector<FunctionTestCall> datetime_contains_tests =
      RangeContainsTests(datetimes, null_datetime, null_datetime_range);
  tests.insert(tests.end(), datetime_contains_tests.begin(),
               datetime_contains_tests.end());

  std::vector<FunctionTestCall> ts_contains_tests =
      RangeContainsTests(timestamps, null_timestamp, null_timestamp_range);
  tests.insert(tests.end(), ts_contains_tests.begin(), ts_contains_tests.end());

  return WrapFeatures(tests);
}

namespace {

// Helper function for constructing GENERATE_RANGE_ARRAY tests for successful
// cases.
// When "scale" argument doesn't have a value (std::nullopt), the generated
// test case doesn't require or prohibit FEATURE_TIMESTAMP_NANOS. In other
// words, the expectation is that such test case should pass on an engine with
// or without nanoseconds support.
FunctionTestCall GenerateRangeArrayTest(
    const Type* range_type, absl::string_view input_range_str,
    absl::string_view step_str, bool last_partial_range,
    absl::Span<const absl::string_view> expected_result_str,
    std::optional<googlesql::functions::TimestampScale> scale) {
  Value range = RangeFromStr(input_range_str, range_type,
                             scale.value_or(functions::kMicroseconds));

  IntervalValue step =
      *IntervalValue::ParseFromString(step_str, /*allow_nanos=*/true);

  std::vector<Value> expected_result;
  for (absl::string_view range_str : expected_result_str) {
    expected_result.push_back(RangeFromStr(
        range_str, range_type, scale.value_or(functions::kMicroseconds)));
  }

  FunctionTestCall call{
      "generate_range_array",
      {range, Value::Interval(step), Value::Bool(last_partial_range)},
      {Value::MakeArray(MakeArrayType(range_type), expected_result).value()}};

  call.params.AddRequiredFeature(FEATURE_RANGE_TYPE)
      .AddRequiredFeature(FEATURE_INTERVAL_TYPE);
  if (scale.has_value()) {
    if (*scale == functions::kNanoseconds) {
      call.params.AddRequiredFeature(FEATURE_TIMESTAMP_NANOS);
    } else {
      // This will ensure that the test cases that are expected to fail due to
      // the lack of TIMESTAMP_NANOS feature are not executed with the feature
      // enabled.
      call.params.AddProhibitedFeature(FEATURE_TIMESTAMP_NANOS);
    }
  }
  const Type* element_type = range_type->AsRange()->element_type();
  if (element_type->IsDatetime()) {
    call.params.AddRequiredFeature(FEATURE_CIVIL_TIME);
  }
  return call;
}

// Helper function for constructing GENERATE_RANGE_ARRAY tests for error cases.
FunctionTestCall GenerateRangeArrayErrorTest(
    const Type* range_type, absl::string_view input_range_str,
    absl::string_view step_str, bool last_partial_range,
    absl::string_view expected_error,
    std::optional<googlesql::functions::TimestampScale> scale) {
  Value range = RangeFromStr(input_range_str, range_type,
                             scale.value_or(functions::kMicroseconds));

  IntervalValue step =
      *IntervalValue::ParseFromString(step_str, /*allow_nanos=*/true);

  FunctionTestCall call{
      "generate_range_array",
      {range, Value::Interval(step), Value::Bool(last_partial_range)},
      Value::Null(MakeArrayType(range_type)),
      absl::OutOfRangeError(expected_error)};

  call.params.AddRequiredFeature(FEATURE_RANGE_TYPE)
      .AddRequiredFeature(FEATURE_INTERVAL_TYPE);
  if (scale.has_value()) {
    if (*scale == functions::kNanoseconds) {
      call.params.AddRequiredFeature(FEATURE_TIMESTAMP_NANOS);
    } else {
      // This will ensure that the test cases that are expected to fail due to
      // the lack of TIMESTAMP_NANOS feature are not executed with the feature
      // enabled.
      call.params.AddProhibitedFeature(FEATURE_TIMESTAMP_NANOS);
    }
  }
  const Type* element_type = range_type->AsRange()->element_type();
  if (element_type->IsDatetime()) {
    call.params.AddRequiredFeature(FEATURE_CIVIL_TIME);
  }
  return call;
}

FunctionTestCall GenerateTimestampRangeArrayTest(
    absl::string_view input_range_str, absl::string_view step_str,
    bool last_partial_range,
    absl::Span<const absl::string_view> expected_result_str,
    std::optional<googlesql::functions::TimestampScale> scale = std::nullopt) {
  return GenerateRangeArrayTest(types::TimestampRangeType(), input_range_str,
                                step_str, last_partial_range,
                                expected_result_str, scale);
}
FunctionTestCall GenerateTimestampRangeArrayErrorTest(
    absl::string_view input_range_str, absl::string_view step_str,
    bool last_partial_range, absl::string_view expected_error,
    std::optional<googlesql::functions::TimestampScale> scale = std::nullopt) {
  return GenerateRangeArrayErrorTest(types::TimestampRangeType(),
                                     input_range_str, step_str,
                                     last_partial_range, expected_error, scale);
}

FunctionTestCall GenerateDateRangeArrayTest(
    absl::string_view input_range_str, absl::string_view step_str,
    bool last_partial_range,
    absl::Span<const absl::string_view> expected_result_str,
    std::optional<googlesql::functions::TimestampScale> scale = std::nullopt) {
  return GenerateRangeArrayTest(types::DateRangeType(), input_range_str,
                                step_str, last_partial_range,
                                expected_result_str, scale);
}
FunctionTestCall GenerateDateRangeArrayErrorTest(
    absl::string_view input_range_str, absl::string_view step_str,
    bool last_partial_range, absl::string_view expected_error,
    std::optional<googlesql::functions::TimestampScale> scale = std::nullopt) {
  return GenerateRangeArrayErrorTest(types::DateRangeType(), input_range_str,
                                     step_str, last_partial_range,
                                     expected_error, scale);
}

FunctionTestCall GenerateDatetimeRangeArrayTest(
    absl::string_view input_range_str, absl::string_view step_str,
    bool last_partial_range,
    absl::Span<const absl::string_view> expected_result_str,
    std::optional<googlesql::functions::TimestampScale> scale = std::nullopt) {
  return GenerateRangeArrayTest(types::DatetimeRangeType(), input_range_str,
                                step_str, last_partial_range,
                                expected_result_str, scale);
}
FunctionTestCall GenerateDatetimeRangeArrayErrorTest(
    absl::string_view input_range_str, absl::string_view step_str,
    bool last_partial_range, absl::string_view expected_error,
    std::optional<googlesql::functions::TimestampScale> scale = std::nullopt) {
  return GenerateRangeArrayErrorTest(types::DatetimeRangeType(),
                                     input_range_str, step_str,
                                     last_partial_range, expected_error, scale);
}

}  // namespace

std::vector<FunctionTestCall> GetFunctionTestsGenerateTimestampRangeArray() {
  return {
      // SUCCESS CASES:
      // NANOSECONDs step, last_partial_range = false:
      GenerateTimestampRangeArrayTest(
          "[2023-04-24 16:35:01.123456001, 2023-04-24 16:35:01.123456015)",
          "0:0:0.000000003",  // INTERVAL 3 NANOSECOND
          /*last_partial_range=*/false,
          {
              "[2023-04-24 16:35:01.123456001, 2023-04-24 16:35:01.123456004)",
              "[2023-04-24 16:35:01.123456004, 2023-04-24 16:35:01.123456007)",
              "[2023-04-24 16:35:01.123456007, 2023-04-24 16:35:01.123456010)",
              "[2023-04-24 16:35:01.123456010, 2023-04-24 16:35:01.123456013)",
          },
          functions::kNanoseconds),
      // NANOSECONDs step, last_partial_range = true:
      GenerateTimestampRangeArrayTest(
          "[2023-04-24 16:35:01.123456001, 2023-04-24 16:35:01.123456015)",
          "0:0:0.000000003",  // INTERVAL 3 NANOSECOND
          /*last_partial_range=*/true,
          {
              "[2023-04-24 16:35:01.123456001, 2023-04-24 16:35:01.123456004)",
              "[2023-04-24 16:35:01.123456004, 2023-04-24 16:35:01.123456007)",
              "[2023-04-24 16:35:01.123456007, 2023-04-24 16:35:01.123456010)",
              "[2023-04-24 16:35:01.123456010, 2023-04-24 16:35:01.123456013)",
              "[2023-04-24 16:35:01.123456013, 2023-04-24 16:35:01.123456015)",
          },
          functions::kNanoseconds),
      // MICROSECONDs step, last_partial_range = false:
      GenerateTimestampRangeArrayTest(
          "[2023-04-24 16:35:01.123005, 2023-04-24 16:35:01.123034)",
          "0:0:0.000007",  // INTERVAL 7 MICROSECOND
          /*last_partial_range=*/false,
          {
              "[2023-04-24 16:35:01.123005, 2023-04-24 16:35:01.123012)",
              "[2023-04-24 16:35:01.123012, 2023-04-24 16:35:01.123019)",
              "[2023-04-24 16:35:01.123019, 2023-04-24 16:35:01.123026)",
              "[2023-04-24 16:35:01.123026, 2023-04-24 16:35:01.123033)",
          }),
      // MICROSECONDs step, last_partial_range = true:
      GenerateTimestampRangeArrayTest(
          "[2023-04-24 16:35:01.123005, 2023-04-24 16:35:01.123034)",
          "0:0:0.000007",  // INTERVAL 7 MICROSECOND
          /*last_partial_range=*/true,
          {
              "[2023-04-24 16:35:01.123005, 2023-04-24 16:35:01.123012)",
              "[2023-04-24 16:35:01.123012, 2023-04-24 16:35:01.123019)",
              "[2023-04-24 16:35:01.123019, 2023-04-24 16:35:01.123026)",
              "[2023-04-24 16:35:01.123026, 2023-04-24 16:35:01.123033)",
              "[2023-04-24 16:35:01.123033, 2023-04-24 16:35:01.123034)",
          }),
      // MILLISECONDs step, last_partial_range = false:
      GenerateTimestampRangeArrayTest(
          "[2023-04-24 16:35:01.003, 2023-04-24 16:35:01.030)",
          "0:0:0.005",  // INTERVAL 5 MILLISECOND
          /*last_partial_range=*/false,
          {
              "[2023-04-24 16:35:01.003, 2023-04-24 16:35:01.008)",
              "[2023-04-24 16:35:01.008, 2023-04-24 16:35:01.013)",
              "[2023-04-24 16:35:01.013, 2023-04-24 16:35:01.018)",
              "[2023-04-24 16:35:01.018, 2023-04-24 16:35:01.023)",
              "[2023-04-24 16:35:01.023, 2023-04-24 16:35:01.028)",
          }),
      // MILLISECONDs step, last_partial_range = true:
      GenerateTimestampRangeArrayTest(
          "[2023-04-24 16:35:01.003, 2023-04-24 16:35:01.032)",
          "0:0:0.005",  // INTERVAL 5 MILLISECOND
          /*last_partial_range=*/true,
          {
              "[2023-04-24 16:35:01.003, 2023-04-24 16:35:01.008)",
              "[2023-04-24 16:35:01.008, 2023-04-24 16:35:01.013)",
              "[2023-04-24 16:35:01.013, 2023-04-24 16:35:01.018)",
              "[2023-04-24 16:35:01.018, 2023-04-24 16:35:01.023)",
              "[2023-04-24 16:35:01.023, 2023-04-24 16:35:01.028)",
              "[2023-04-24 16:35:01.028, 2023-04-24 16:35:01.032)",
          }),
      // SECONDs step, last_partial_range = false:
      GenerateTimestampRangeArrayTest(
          "[2023-04-24 16:35:05.123456, 2023-04-24 16:36:11.123455)",
          "0:0:11",  // INTERVAL 11 SECOND
          /*last_partial_range=*/false,
          {
              "[2023-04-24 16:35:05.123456, 2023-04-24 16:35:16.123456)",
              "[2023-04-24 16:35:16.123456, 2023-04-24 16:35:27.123456)",
              "[2023-04-24 16:35:27.123456, 2023-04-24 16:35:38.123456)",
              "[2023-04-24 16:35:38.123456, 2023-04-24 16:35:49.123456)",
              "[2023-04-24 16:35:49.123456, 2023-04-24 16:36:00.123456)",
          }),
      // SECONDs step, last_partial_range = true:
      GenerateTimestampRangeArrayTest(
          "[2023-04-24 16:35:05.123456, 2023-04-24 16:36:11.123455)",
          "0:0:11",  // INTERVAL 11 SECOND
          /*last_partial_range=*/true,
          {
              "[2023-04-24 16:35:05.123456, 2023-04-24 16:35:16.123456)",
              "[2023-04-24 16:35:16.123456, 2023-04-24 16:35:27.123456)",
              "[2023-04-24 16:35:27.123456, 2023-04-24 16:35:38.123456)",
              "[2023-04-24 16:35:38.123456, 2023-04-24 16:35:49.123456)",
              "[2023-04-24 16:35:49.123456, 2023-04-24 16:36:00.123456)",
              "[2023-04-24 16:36:00.123456, 2023-04-24 16:36:11.123455)",
          }),
      // MINUTEs step, last_partial_range = false:
      GenerateTimestampRangeArrayTest(
          "[2023-04-24 16:20:37.123456789, 2023-04-24 17:30:00)",
          "0:20:00",  // INTERVAL 20 MINUTE
          /*last_partial_range=*/false,
          {
              "[2023-04-24 16:20:37.123456789, 2023-04-24 16:40:37.123456789)",
              "[2023-04-24 16:40:37.123456789, 2023-04-24 17:00:37.123456789)",
              "[2023-04-24 17:00:37.123456789, 2023-04-24 17:20:37.123456789)",
          },
          functions::kNanoseconds),
      // MINUTEs step, last_partial_range = true:
      GenerateTimestampRangeArrayTest(
          "[2023-04-24 16:20:37.123456789, 2023-04-24 17:30:00)",
          "0:20:00",  // INTERVAL 20 MINUTE
          /*last_partial_range=*/true,
          {
              "[2023-04-24 16:20:37.123456789, 2023-04-24 16:40:37.123456789)",
              "[2023-04-24 16:40:37.123456789, 2023-04-24 17:00:37.123456789)",
              "[2023-04-24 17:00:37.123456789, 2023-04-24 17:20:37.123456789)",
              "[2023-04-24 17:20:37.123456789, 2023-04-24 17:30:00)",
          },
          functions::kNanoseconds),
      // HOURs step, last_partial_range = false:
      GenerateTimestampRangeArrayTest(
          "[2023-04-24 08:20:37, 2023-04-25 08:21:37)",
          "8:00:00",  // INTERVAL 8 HOUR
          /*last_partial_range=*/false,
          {
              "[2023-04-24 08:20:37, 2023-04-24 16:20:37)",
              "[2023-04-24 16:20:37, 2023-04-25 00:20:37)",
              "[2023-04-25 00:20:37, 2023-04-25 08:20:37)",
          }),
      // HOURs step, last_partial_range = true:
      GenerateTimestampRangeArrayTest(
          "[2023-04-24 08:20:37, 2023-04-25 08:21:37)",
          "8:00:00",  // INTERVAL 8 HOUR
          /*last_partial_range=*/true,
          {
              "[2023-04-24 08:20:37, 2023-04-24 16:20:37)",
              "[2023-04-24 16:20:37, 2023-04-25 00:20:37)",
              "[2023-04-25 00:20:37, 2023-04-25 08:20:37)",
              "[2023-04-25 08:20:37, 2023-04-25 08:21:37)",
          }),
      // DAYs step, last_partial_range = false:
      GenerateTimestampRangeArrayTest(
          "[2020-02-24 08:20:37, 2020-03-24 08:20:37)",
          "0-0 7",  // INTERVAL 7 DAY
          /*last_partial_range=*/false,
          {
              "[2020-02-24 08:20:37, 2020-03-02 08:20:37)",
              "[2020-03-02 08:20:37, 2020-03-09 08:20:37)",
              "[2020-03-09 08:20:37, 2020-03-16 08:20:37)",
              "[2020-03-16 08:20:37, 2020-03-23 08:20:37)",
          }),
      // DAYs step, last_partial_range = true:
      GenerateTimestampRangeArrayTest(
          "[2020-02-24 08:20:37, 2020-03-24 08:20:37)",
          "0-0 7",  // INTERVAL 7 DAY
          /*last_partial_range=*/true,
          {
              "[2020-02-24 08:20:37, 2020-03-02 08:20:37)",
              "[2020-03-02 08:20:37, 2020-03-09 08:20:37)",
              "[2020-03-09 08:20:37, 2020-03-16 08:20:37)",
              "[2020-03-16 08:20:37, 2020-03-23 08:20:37)",
              "[2020-03-23 08:20:37, 2020-03-24 08:20:37)",
          }),
      // Mixed parts in step, last_partial_range = false:
      GenerateTimestampRangeArrayTest(
          "[2023-02-24 08:20:37, 2023-03-10 15:31:28)",
          "3 17:47:53.123456",  // 3 days and 17:47:53.123456
          /*last_partial_range=*/false,
          {
              "[2023-02-24 08:20:37, 2023-02-28 02:08:30.123456)",
              "[2023-02-28 02:08:30.123456, 2023-03-03 19:56:23.246912)",
              "[2023-03-03 19:56:23.246912, 2023-03-07 13:44:16.370368)",
          }),
      // Mixed parts in step, last_partial_range = true:
      GenerateTimestampRangeArrayTest(
          "[2023-02-24 08:20:37, 2023-03-10 15:31:28)",
          "3 17:47:53.123456",  // 3 days and 17:47:53.123456
          /*last_partial_range=*/true,
          {
              "[2023-02-24 08:20:37, 2023-02-28 02:08:30.123456)",
              "[2023-02-28 02:08:30.123456, 2023-03-03 19:56:23.246912)",
              "[2023-03-03 19:56:23.246912, 2023-03-07 13:44:16.370368)",
              "[2023-03-07 13:44:16.370368, 2023-03-10 15:31:28)",
          }),
      // Mixed parts (+ NANOSECONDs) in step, last_partial_range = false:
      GenerateTimestampRangeArrayTest(
          "[2023-02-24 08:20:37, 2023-03-10 15:31:28)",
          "3 17:47:53.123456789",  // 3 days and 17:47:53.123456789
          /*last_partial_range=*/false,
          {
              "[2023-02-24 08:20:37, 2023-02-28 02:08:30.123456789)",
              "[2023-02-28 02:08:30.123456789, 2023-03-03 19:56:23.246913578)",
              "[2023-03-03 19:56:23.246913578, 2023-03-07 13:44:16.370370367)",
          },
          functions::kNanoseconds),
      // Mixed parts (+ NANOSECONDs) in step, last_partial_range = true:
      GenerateTimestampRangeArrayTest(
          "[2023-02-24 08:20:37, 2023-03-10 15:31:28)",
          "3 17:47:53.123456789",  // 3 days and 17:47:53.123456789
          /*last_partial_range=*/true,
          {
              "[2023-02-24 08:20:37, 2023-02-28 02:08:30.123456789)",
              "[2023-02-28 02:08:30.123456789, 2023-03-03 19:56:23.246913578)",
              "[2023-03-03 19:56:23.246913578, 2023-03-07 13:44:16.370370367)",
              "[2023-03-07 13:44:16.370370367, 2023-03-10 15:31:28)",
          },
          functions::kNanoseconds),
      // HOURs step, exactly aligns with range end, last_partial_range = false:
      GenerateTimestampRangeArrayTest(
          "[2023-04-24 08:20:37, 2023-04-25 08:20:37)",
          "8:00:00",  // INTERVAL 8 HOUR
          /*last_partial_range=*/false,
          {
              "[2023-04-24 08:20:37, 2023-04-24 16:20:37)",
              "[2023-04-24 16:20:37, 2023-04-25 00:20:37)",
              "[2023-04-25 00:20:37, 2023-04-25 08:20:37)",
          }),
      // HOURs step, exactly aligns with range end, last_partial_range = true:
      GenerateTimestampRangeArrayTest(
          "[2023-04-24 08:20:37, 2023-04-25 08:20:37)",
          "8:00:00",  // INTERVAL 8 HOUR
          /*last_partial_range=*/true,
          {
              "[2023-04-24 08:20:37, 2023-04-24 16:20:37)",
              "[2023-04-24 16:20:37, 2023-04-25 00:20:37)",
              "[2023-04-25 00:20:37, 2023-04-25 08:20:37)",
          }),
      // DAYs step, exactly aligns with range end, last_partial_range = false:
      GenerateTimestampRangeArrayTest(
          "[2020-02-24 08:20:37, 2020-03-23 08:20:37)",
          "0-0 7",  // INTERVAL 7 DAY
          /*last_partial_range=*/false,
          {
              "[2020-02-24 08:20:37, 2020-03-02 08:20:37)",
              "[2020-03-02 08:20:37, 2020-03-09 08:20:37)",
              "[2020-03-09 08:20:37, 2020-03-16 08:20:37)",
              "[2020-03-16 08:20:37, 2020-03-23 08:20:37)",
          }),
      // DAYs step, exactly aligns with range end, last_partial_range = true:
      GenerateTimestampRangeArrayTest(
          "[2020-02-24 08:20:37, 2020-03-23 08:20:37)",
          "0-0 7",  // INTERVAL 7 DAY
          /*last_partial_range=*/true,
          {
              "[2020-02-24 08:20:37, 2020-03-02 08:20:37)",
              "[2020-03-02 08:20:37, 2020-03-09 08:20:37)",
              "[2020-03-09 08:20:37, 2020-03-16 08:20:37)",
              "[2020-03-16 08:20:37, 2020-03-23 08:20:37)",
          }),
      // Largest possible step, last_partial_range = false:
      GenerateTimestampRangeArrayTest(
          "[2023-02-24 08:20:37, 9997-05-12 17:13:41.123456)",
          // Largest possible INTERVAL value (~20,000 years)
          "366000 87840000:0:0",
          /*last_partial_range=*/false, {}),
      // Largest possible step, last_partial_range = true:
      GenerateTimestampRangeArrayTest(
          "[2023-02-24 08:20:37, 9997-05-12 17:13:41.123456)",
          "366000 87840000:0:0",  // Largest possible INTERVAL value
          /*last_partial_range=*/true,
          {"[2023-02-24 08:20:37, 9997-05-12 17:13:41.123456)"}),
      // Step covers full TIMESTAMP range with DAY part,
      // last_partial_range = false:
      GenerateTimestampRangeArrayTest(
          "[0001-01-01 00:00:00, 9999-12-31 23:59:59.999999)",
          "0-0 3652058",  // INTERVAL 3652058 DAY
          /*last_partial_range=*/false,
          {"[0001-01-01 00:00:00, 9999-12-31 00:00:00)"}),
      // Step covers full TIMESTAMP range with DAY part,
      // last_partial_range = true:
      GenerateTimestampRangeArrayTest(
          "[0001-01-01 00:00:00, 9999-12-31 23:59:59.999999)",
          "0-0 3652058",  // INTERVAL 3652058 DAY
          /*last_partial_range=*/true,
          {
              "[0001-01-01 00:00:00, 9999-12-31 00:00:00)",
              "[9999-12-31 00:00:00, 9999-12-31 23:59:59.999999)",
          }),
      // Step covers full TIMESTAMP range with MICROS part,
      // last_partial_range = false:
      GenerateTimestampRangeArrayTest(
          "[0001-01-01 00:00:00, 9999-12-31 23:59:59.999999)",
          "87649415:0:0",  // INTERVAL 87649415 HOUR
          /*last_partial_range=*/false,
          {"[0001-01-01 00:00:00, 9999-12-31 23:00:00)"}),
      // Step covers full TIMESTAMP range with MICROS part,
      // last_partial_range = true:
      GenerateTimestampRangeArrayTest(
          "[0001-01-01 00:00:00, 9999-12-31 23:59:59.999999)",
          "87649415:0:0",  // INTERVAL 87649415 HOUR
          /*last_partial_range=*/true,
          {
              "[0001-01-01 00:00:00, 9999-12-31 23:00:00)",
              "[9999-12-31 23:00:00, 9999-12-31 23:59:59.999999)",
          }),
      // Very large step with DAY part, last_partial_range = false:
      GenerateTimestampRangeArrayTest(
          "[0001-01-01 00:00:00, 9999-12-31 23:59:59.999999)",
          "0-0 915000",  // ~2500 years
          /*last_partial_range=*/false,
          {
              "[0001-01-01 00:00:00, 2506-03-10 00:00:00)",
              "[2506-03-10 00:00:00, 5011-05-17 00:00:00)",
              "[5011-05-17 00:00:00, 7516-07-23 00:00:00)",
          }),
      // Very large step with DAY part, last_partial_range = true:
      GenerateTimestampRangeArrayTest(
          "[0001-01-01 00:00:00, 9999-12-31 23:59:59.999999)",
          "0-0 915000",  // ~2500 years
          /*last_partial_range=*/true,
          {
              "[0001-01-01 00:00:00, 2506-03-10 00:00:00)",
              "[2506-03-10 00:00:00, 5011-05-17 00:00:00)",
              "[5011-05-17 00:00:00, 7516-07-23 00:00:00)",
              "[7516-07-23 00:00:00, 9999-12-31 23:59:59.999999)",
          }),
      // Very large step with DAY part, with nanos, last_partial_range = false:
      GenerateTimestampRangeArrayTest(
          "[0001-01-01 00:00:00, 9999-12-31 23:59:59.999999999)",
          "0-0 915000",  // ~2500 years
          /*last_partial_range=*/false,
          {
              "[0001-01-01 00:00:00, 2506-03-10 00:00:00)",
              "[2506-03-10 00:00:00, 5011-05-17 00:00:00)",
              "[5011-05-17 00:00:00, 7516-07-23 00:00:00)",
          },
          functions::kNanoseconds),
      // Very large step with DAY part, with nanos, last_partial_range = true:
      GenerateTimestampRangeArrayTest(
          "[0001-01-01 00:00:00, 9999-12-31 23:59:59.999999999)",
          "0-0 915000",  // ~2500 years
          /*last_partial_range=*/true,
          {
              "[0001-01-01 00:00:00, 2506-03-10 00:00:00)",
              "[2506-03-10 00:00:00, 5011-05-17 00:00:00)",
              "[5011-05-17 00:00:00, 7516-07-23 00:00:00)",
              "[7516-07-23 00:00:00, 9999-12-31 23:59:59.999999999)",
          },
          functions::kNanoseconds),
      // Very large step with MICROS part, last_partial_range = false:
      GenerateTimestampRangeArrayTest(
          "[0001-01-01 00:00:00, 9999-12-31 23:59:59.999999)",
          "21960000:17:43.123456",  // ~2500 years
          /*last_partial_range=*/false,
          {
              "[0001-01-01 00:00:00, 2506-03-10 00:17:43.123456)",
              "[2506-03-10 00:17:43.123456, 5011-05-17 00:35:26.246912)",
              "[5011-05-17 00:35:26.246912, 7516-07-23 00:53:09.370368)",
          }),
      // Very large step with MICROS part, last_partial_range = true:
      GenerateTimestampRangeArrayTest(
          "[0001-01-01 00:00:00, 9999-12-31 23:59:59.999999)",
          "21960000:17:43.123456",  // ~2500 years
          /*last_partial_range=*/true,
          {
              "[0001-01-01 00:00:00, 2506-03-10 00:17:43.123456)",
              "[2506-03-10 00:17:43.123456, 5011-05-17 00:35:26.246912)",
              "[5011-05-17 00:35:26.246912, 7516-07-23 00:53:09.370368)",
              "[7516-07-23 00:53:09.370368, 9999-12-31 23:59:59.999999)",
          }),
      // Very large step with MICROS and NANOS parts,
      // last_partial_range = false:
      GenerateTimestampRangeArrayTest(
          "[0001-01-01 00:00:00, 9999-12-31 23:59:59.999999999)",
          "21960000:17:43.123456789",  // ~2500 years
          /*last_partial_range=*/false,
          {
              "[0001-01-01 00:00:00, 2506-03-10 00:17:43.123456789)",
              "[2506-03-10 00:17:43.123456789, 5011-05-17 00:35:26.246913578)",
              "[5011-05-17 00:35:26.246913578, 7516-07-23 00:53:09.370370367)",
          },
          functions::kNanoseconds),
      // Very large step with MICROS and NANOS parts, last_partial_range = true:
      GenerateTimestampRangeArrayTest(
          "[0001-01-01 00:00:00, 9999-12-31 23:59:59.999999999)",
          "21960000:17:43.123456789",  // ~2500 years
          /*last_partial_range=*/true,
          {
              "[0001-01-01 00:00:00, 2506-03-10 00:17:43.123456789)",
              "[2506-03-10 00:17:43.123456789, 5011-05-17 00:35:26.246913578)",
              "[5011-05-17 00:35:26.246913578, 7516-07-23 00:53:09.370370367)",
              "[7516-07-23 00:53:09.370370367, 9999-12-31 23:59:59.999999999)",
          },
          functions::kNanoseconds),
      // ERROR CASES:
      // Unsupported input RANGE values:
      GenerateTimestampRangeArrayErrorTest(
          "[UNBOUNDED, 2023-04-24 16:35:01)", "0:0:1",
          /*last_partial_range=*/false,
          "input RANGE cannot have UNBOUNDED endpoints"),
      GenerateTimestampRangeArrayErrorTest(
          "[2023-04-24 16:35:01, UNBOUNDED)", "0:0:1",
          /*last_partial_range=*/false,
          "input RANGE cannot have UNBOUNDED endpoints"),
      GenerateTimestampRangeArrayErrorTest(
          "[UNBOUNDED, UNBOUNDED)", "0:0:1",
          /*last_partial_range=*/false,
          "input RANGE cannot have UNBOUNDED endpoints"),
      GenerateTimestampRangeArrayErrorTest(
          "[UNBOUNDED, 2023-04-24 16:35:01)", "0:0:1",
          /*last_partial_range=*/true,
          "input RANGE cannot have UNBOUNDED endpoints"),
      GenerateTimestampRangeArrayErrorTest(
          "[2023-04-24 16:35:01, UNBOUNDED)", "0:0:1",
          /*last_partial_range=*/true,
          "input RANGE cannot have UNBOUNDED endpoints"),
      GenerateTimestampRangeArrayErrorTest(
          "[UNBOUNDED, UNBOUNDED)", "0:0:1",
          /*last_partial_range=*/true,
          "input RANGE cannot have UNBOUNDED endpoints"),
      // Unsupported step INTERVAL parts:
      GenerateTimestampRangeArrayErrorTest(
          "[2012-02-11 10:21:57, 2023-04-24 16:35:01)",
          "0-1",  // Non-zero MONTH part
          /*last_partial_range=*/false,
          "step with non-zero MONTH or YEAR part is not supported"),
      GenerateTimestampRangeArrayErrorTest(
          "[2012-02-11 10:21:57, 2023-04-24 16:35:01)",
          "1-0",  // Non-zero YEAR part
          /*last_partial_range=*/false,
          "step with non-zero MONTH or YEAR part is not supported"),
      GenerateTimestampRangeArrayErrorTest(
          "[2012-02-11 10:21:57.000001, 2012-02-11 10:21:57.000003)",
          "0:0:0.000000001",  // Non-zero NANOSECOND part
          /*last_partial_range=*/false,
          "step with non-zero NANOSECOND part is not supported",
          functions::kMicroseconds),
      // Unsupported and supported step INTERVAL parts mixed together:
      GenerateTimestampRangeArrayErrorTest(
          "[2012-02-11 10:21:57, 2023-04-24 16:35:01)",
          "0-1 1",  // Non-zero MONTH and DAY parts
          /*last_partial_range=*/false,
          "step with non-zero MONTH or YEAR part is not supported"),
      GenerateTimestampRangeArrayErrorTest(
          "[2012-02-11 10:21:57, 2023-04-24 16:35:01)",
          "1-0 0 0:0:1",  // Non-zero YEAR and DAY parts
          /*last_partial_range=*/false,
          "step with non-zero MONTH or YEAR part is not supported"),
      GenerateTimestampRangeArrayErrorTest(
          "[2012-02-11 10:21:57, 2023-04-24 16:35:01)",
          "10000:0:0.000000001",  // Non-zero NANOSECOND part
          /*last_partial_range=*/false,
          "step with non-zero NANOSECOND part is not supported",
          functions::kMicroseconds),
      // Invalid step INTERVAL values:
      GenerateTimestampRangeArrayErrorTest(
          "[2012-02-11 10:21:57, 2023-04-24 16:35:01)",
          "0:0:0",  // Zero INTERVAL
          /*last_partial_range=*/false, "step cannot be 0"),
      GenerateTimestampRangeArrayErrorTest(
          "[2012-02-11 10:21:57, 2023-04-24 16:35:01)",
          "0-0 -1",  // Negative DAY part
          /*last_partial_range=*/false, "step cannot be negative"),
      GenerateTimestampRangeArrayErrorTest(
          "[2012-02-11 10:21:57, 2023-04-24 16:35:01)",
          "-0:0:1",  // Negative MICROSECOND part
          /*last_partial_range=*/false, "step cannot be negative"),
  };
}

std::vector<FunctionTestCall> GetFunctionTestsGenerateDateRangeArray() {
  return {
      // SUCCESS CASES:
      // DAYs step, last_partial_range = false:
      GenerateDateRangeArrayTest("[2020-02-24, 2020-03-24)",
                                 "0-0 7",  // INTERVAL 7 DAY
                                 /*last_partial_range=*/false,
                                 {
                                     "[2020-02-24, 2020-03-02)",
                                     "[2020-03-02, 2020-03-09)",
                                     "[2020-03-09, 2020-03-16)",
                                     "[2020-03-16, 2020-03-23)",
                                 }),
      // DAYs step, last_partial_range = true:
      GenerateDateRangeArrayTest("[2020-02-24, 2020-03-24)",
                                 "0-0 7",  // INTERVAL 7 DAY
                                 /*last_partial_range=*/true,
                                 {
                                     "[2020-02-24, 2020-03-02)",
                                     "[2020-03-02, 2020-03-09)",
                                     "[2020-03-09, 2020-03-16)",
                                     "[2020-03-16, 2020-03-23)",
                                     "[2020-03-23, 2020-03-24)",
                                 }),
      // Very large step with DAY part, last_partial_range = false:
      GenerateDateRangeArrayTest("[0001-01-01, 9999-12-31)",
                                 "0-0 915000",  // ~2500 years
                                 /*last_partial_range=*/false,
                                 {
                                     "[0001-01-01, 2506-03-10)",
                                     "[2506-03-10, 5011-05-17)",
                                     "[5011-05-17, 7516-07-23)",
                                 }),
      // Very large step with DAY part, last_partial_range = true:
      GenerateDateRangeArrayTest("[0001-01-01, 9999-12-31)",
                                 "0-0 915000",  // ~2500 years
                                 /*last_partial_range=*/true,
                                 {
                                     "[0001-01-01, 2506-03-10)",
                                     "[2506-03-10, 5011-05-17)",
                                     "[5011-05-17, 7516-07-23)",
                                     "[7516-07-23, 9999-12-31)",
                                 }),
      // Step covers full DATE range with DAY part,
      // last_partial_range = false:
      GenerateDateRangeArrayTest("[0001-01-01, 9999-12-31)",
                                 "0-0 3652058",  // INTERVAL 3652058 DAY
                                 /*last_partial_range=*/false,
                                 {"[0001-01-01, 9999-12-31)"}),
      // Step covers full DATE range with DAY part,
      // last_partial_range = true:
      GenerateDateRangeArrayTest("[0001-01-01, 9999-12-31)",
                                 "0-0 3652058",  // INTERVAL 3652058 DAY
                                 /*last_partial_range=*/true,
                                 {"[0001-01-01, 9999-12-31)"}),
      // MONTHs step
      // range_start = last day of 31-day month
      GenerateDateRangeArrayTest(
          "[2020-01-31, 2020-5-24)",
          "0-1",  // INTERVAL 1 month
          /*last_partial_range=*/false,
          {"[2020-01-31, 2020-02-29)", "[2020-02-29, 2020-03-31)",
           "[2020-03-31, 2020-04-30)"}),
      GenerateDateRangeArrayTest(
          "[2020-01-31, 2020-5-24)",
          "0-1",  // INTERVAL 1 month
          /*last_partial_range=*/true,
          {"[2020-01-31, 2020-02-29)", "[2020-02-29, 2020-03-31)",
           "[2020-03-31, 2020-04-30)", "[2020-04-30, 2020-05-24)"}),
      // range_start = last day of 30-day month
      GenerateDateRangeArrayTest(
          "[2020-04-30, 2020-8-24)",
          "0-1",  // INTERVAL 1 month
          /*last_partial_range=*/false,
          {"[2020-04-30, 2020-05-30)", "[2020-05-30, 2020-06-30)",
           "[2020-06-30, 2020-07-30)"}),
      GenerateDateRangeArrayTest(
          "[2020-04-30, 2020-8-24)",
          "0-1",  // INTERVAL 1 month
          /*last_partial_range=*/true,
          {"[2020-04-30, 2020-05-30)", "[2020-05-30, 2020-06-30)",
           "[2020-06-30, 2020-07-30)", "[2020-07-30, 2020-08-24)"}),
      // range_start = Feb 28
      GenerateDateRangeArrayTest(
          "[2021-02-28, 2021-6-24)",
          "0-1",  // INTERVAL 1 month
          /*last_partial_range=*/false,
          {"[2021-02-28, 2021-03-28)", "[2021-03-28, 2021-04-28)",
           "[2021-04-28, 2021-05-28)"}),
      GenerateDateRangeArrayTest(
          "[2021-02-28, 2021-6-24)",
          "0-1",  // INTERVAL 1 month
          /*last_partial_range=*/true,
          {"[2021-02-28, 2021-03-28)", "[2021-03-28, 2021-04-28)",
           "[2021-04-28, 2021-05-28)", "[2021-05-28, 2021-06-24)"}),
      // range_start = leap year, Feb 29
      GenerateDateRangeArrayTest(
          "[2024-02-29, 2024-6-24)",
          "0-1",  // INTERVAL 1 month
          /*last_partial_range=*/false,
          {"[2024-02-29, 2024-03-29)", "[2024-03-29, 2024-04-29)",
           "[2024-04-29, 2024-05-29)"}),
      GenerateDateRangeArrayTest(
          "[2024-02-29, 2024-6-24)",
          "0-1",  // INTERVAL 1 month
          /*last_partial_range=*/true,
          {"[2024-02-29, 2024-03-29)", "[2024-03-29, 2024-04-29)",
           "[2024-04-29, 2024-05-29)", "[2024-05-29, 2024-06-24)"}),
      // range_start = leap year, Feb 28
      GenerateDateRangeArrayTest("[2024-02-28, 2024-6-24)",
                                 "0-1",  // INTERVAL 1 month
                                 /*last_partial_range=*/false,
                                 {
                                     "[2024-02-28, 2024-03-28)",
                                     "[2024-03-28, 2024-04-28)",
                                     "[2024-04-28, 2024-05-28)",
                                 }),
      GenerateDateRangeArrayTest(
          "[2024-02-28, 2024-6-24)",
          "0-1",  // INTERVAL 1 month
          /*last_partial_range=*/true,
          {"[2024-02-28, 2024-03-28)", "[2024-03-28, 2024-04-28)",
           "[2024-04-28, 2024-05-28)", "[2024-05-28, 2024-06-24)"}),
      // Very large MONTHs step, last_partial_range = false:
      GenerateDateRangeArrayTest(
          "[2023-02-24, 7000-05-12)",
          "0-24000",  // INTERVAL 2000 years
          /*last_partial_range=*/false,
          {"[2023-02-24, 4023-02-24)", "[4023-02-24, 6023-02-24)"}),
      // Very large MONTHs step, last_partial_range = true:
      GenerateDateRangeArrayTest(
          "[2023-02-24, 7000-05-12)",
          "0-24000",  // INTERVAL 2000 years
          /*last_partial_range=*/true,
          {"[2023-02-24, 4023-02-24)", "[4023-02-24, 6023-02-24)",
           "[6023-02-24, 7000-05-12)"}),
      // Largest possible MONTHs step, last_partial_range = false:
      GenerateDateRangeArrayTest("[2023-02-24, 9997-05-12)",
                                 "0-120000",  // INTERVAL 10000 years
                                 /*last_partial_range=*/false, {}),
      // Largest possible MONTHs step, last_partial_range = true:
      GenerateDateRangeArrayTest("[2023-02-24, 9997-05-12)",
                                 "0-120000",  // INTERVAL 10000 years
                                 /*last_partial_range=*/true,
                                 {"[2023-02-24, 9997-05-12)"}),
      // YEARs step
      // range_start = last day of 31-day month
      GenerateDateRangeArrayTest(
          "[2020-01-31, 2022-5-24)",
          "1-0",  // INTERVAL 1 year
          /*last_partial_range=*/false,
          {"[2020-01-31, 2021-01-31)", "[2021-01-31, 2022-01-31)"}),
      GenerateDateRangeArrayTest(
          "[2020-01-31, 2022-5-24)",
          "1-0",  // INTERVAL 1 year
          /*last_partial_range=*/true,
          {"[2020-01-31, 2021-01-31)", "[2021-01-31, 2022-01-31)",
           "[2022-01-31, 2022-05-24)"}),
      // range_start = last day of 30-day month
      GenerateDateRangeArrayTest(
          "[2020-04-30, 2022-8-24)",
          "1-0",  // INTERVAL 1 year
          /*last_partial_range=*/false,
          {"[2020-04-30, 2021-04-30)", "[2021-04-30, 2022-04-30)"}),
      GenerateDateRangeArrayTest(
          "[2020-04-30, 2022-8-24)",
          "1-0",  // INTERVAL 1 year
          /*last_partial_range=*/true,
          {"[2020-04-30, 2021-04-30)", "[2021-04-30, 2022-04-30)",
           "[2022-04-30, 2022-08-24)"}),
      // range_start = Feb 28
      GenerateDateRangeArrayTest(
          "[2021-02-28, 2023-6-24)",
          "1-0",  // INTERVAL 1 year
          /*last_partial_range=*/false,
          {"[2021-02-28, 2022-02-28)", "[2022-02-28, 2023-02-28)"}),
      GenerateDateRangeArrayTest(
          "[2021-02-28, 2023-6-24)",
          "1-0",  // INTERVAL 1 year
          /*last_partial_range=*/true,
          {"[2021-02-28, 2022-02-28)", "[2022-02-28, 2023-02-28)",
           "[2023-02-28, 2023-06-24)"}),
      // range_start = leap year, Feb 29
      GenerateDateRangeArrayTest(
          "[2024-02-29, 2026-6-24)",
          "1-0",  // INTERVAL 1 year
          /*last_partial_range=*/false,
          {"[2024-02-29, 2025-02-28)", "[2025-02-28, 2026-02-28)"}),
      GenerateDateRangeArrayTest(
          "[2024-02-29, 2026-6-24)",
          "1-0",  // INTERVAL 1 year
          /*last_partial_range=*/true,
          {"[2024-02-29, 2025-02-28)", "[2025-02-28, 2026-02-28)",
           "[2026-02-28, 2026-06-24)"}),
      // range_start = leap year, Feb 28
      GenerateDateRangeArrayTest(
          "[2024-02-28, 2026-6-24)",
          "1-0",  // INTERVAL 1 year
          /*last_partial_range=*/false,
          {"[2024-02-28, 2025-02-28)", "[2025-02-28, 2026-02-28)"}),
      GenerateDateRangeArrayTest(
          "[2024-02-28, 2026-6-24)",
          "1-0",  // INTERVAL 1 year
          /*last_partial_range=*/true,
          {"[2024-02-28, 2025-02-28)", "[2025-02-28, 2026-02-28)",
           "[2026-02-28, 2026-06-24)"}),
      // Y-M step, last_partial_range = false:
      GenerateDateRangeArrayTest(
          "[2020-02-24, 9999-03-24)",
          "1000-5",  // INTERVAL 1000 years and 5 months
          /*last_partial_range=*/false,
          {"[2020-02-24, 3020-07-24)", "[3020-07-24, 4020-12-24)",
           "[4020-12-24, 5021-05-24)", "[5021-05-24, 6021-10-24)",
           "[6021-10-24, 7022-03-24)", "[7022-03-24, 8022-08-24)",
           "[8022-08-24, 9023-01-24)"}),
      // Y-M step, last_partial_range = true:
      GenerateDateRangeArrayTest(
          "[2020-02-24, 9999-03-24)",
          "1000-5",  // INTERVAL 1000 years and 5 months
          /*last_partial_range=*/true,
          {"[2020-02-24, 3020-07-24)", "[3020-07-24, 4020-12-24)",
           "[4020-12-24, 5021-05-24)", "[5021-05-24, 6021-10-24)",
           "[6021-10-24, 7022-03-24)", "[7022-03-24, 8022-08-24)",
           "[8022-08-24, 9023-01-24)", "[9023-01-24, 9999-03-24)"}),
      // Very large Y-M step, last_partial_range = false:
      GenerateDateRangeArrayTest("[0001-03-24, 9999-03-24)",
                                 "5000-5",  // INTERVAL 5000 years and 5 months
                                 /*last_partial_range=*/false,
                                 {"[0001-03-24, 5001-08-24)"}),
      // Very large Y-M step, last_partial_range = true:
      GenerateDateRangeArrayTest(
          "[0001-03-24, 9999-03-24)",
          "5000-5",  // INTERVAL 5000 years and 5 months
          /*last_partial_range=*/true,
          {"[0001-03-24, 5001-08-24)", "[5001-08-24, 9999-03-24)"}),
      // Step covers full DATE range with Y-M part
      GenerateDateRangeArrayTest("[0001-03-24, 9999-03-24)",
                                 "10000-0",  // INTERVAL 10000 years
                                 /*last_partial_range=*/false, {}),
      GenerateDateRangeArrayTest("[0001-03-24, 9999-03-24)",
                                 "10000-0",  // INTERVAL 10000 years
                                 /*last_partial_range=*/true,
                                 {"[0001-03-24, 9999-03-24)"}),
      // ERROR CASES:
      // Unsupported input RANGE values:
      GenerateDateRangeArrayErrorTest(
          "[UNBOUNDED, 2023-04-24)", "0-0 7",
          /*last_partial_range=*/false,
          "input RANGE cannot have UNBOUNDED endpoints"),
      GenerateDateRangeArrayErrorTest(
          "[2023-04-24, UNBOUNDED)", "0-0 7",
          /*last_partial_range=*/false,
          "input RANGE cannot have UNBOUNDED endpoints"),
      GenerateDateRangeArrayErrorTest(
          "[UNBOUNDED, UNBOUNDED)", "0-0 7",
          /*last_partial_range=*/false,
          "input RANGE cannot have UNBOUNDED endpoints"),
      GenerateDateRangeArrayErrorTest(
          "[UNBOUNDED, 2023-04-24)", "0-0 7",
          /*last_partial_range=*/true,
          "input RANGE cannot have UNBOUNDED endpoints"),
      GenerateDateRangeArrayErrorTest(
          "[2023-04-24, UNBOUNDED)", "0-0 7",
          /*last_partial_range=*/true,
          "input RANGE cannot have UNBOUNDED endpoints"),
      GenerateDateRangeArrayErrorTest(
          "[UNBOUNDED, UNBOUNDED)", "0-0 7",
          /*last_partial_range=*/true,
          "input RANGE cannot have UNBOUNDED endpoints"),
      // Unsupported step INTERVAL parts:
      GenerateDateRangeArrayErrorTest(
          "[2012-02-11, 2023-04-24)",
          "0:0:1",  // Non-zero (H:M:S[.F])
          /*last_partial_range=*/false,
          "step with non-zero (H:M:S[.F]) part is not supported"),
      GenerateDateRangeArrayErrorTest(
          "[2012-02-11, 2023-04-24)",
          "0:0:1",  // Non-zero (H:M:S[.F])
          /*last_partial_range=*/true,
          "step with non-zero (H:M:S[.F]) part is not supported"),
      GenerateDateRangeArrayErrorTest("[2012-02-11, 2023-04-24)", "0-0",
                                      /*last_partial_range=*/false,
                                      "step cannot be 0"),
      GenerateDateRangeArrayErrorTest("[2012-02-11, 2023-04-24)", "0-0",
                                      /*last_partial_range=*/true,
                                      "step cannot be 0"),
      GenerateDateRangeArrayErrorTest("[2012-02-11, 2023-04-24)", "0-0 0",
                                      /*last_partial_range=*/false,
                                      "step cannot be 0"),
      GenerateDateRangeArrayErrorTest("[2012-02-11, 2023-04-24)", "0-0 0",
                                      /*last_partial_range=*/true,
                                      "step cannot be 0"),
      GenerateDateRangeArrayErrorTest("[2012-02-11, 2023-04-24)", "-10-0",
                                      /*last_partial_range=*/false,
                                      "step cannot be negative"),
      GenerateDateRangeArrayErrorTest("[2012-02-11, 2023-04-24)", "-10-0",
                                      /*last_partial_range=*/true,
                                      "step cannot be negative"),
      GenerateDateRangeArrayErrorTest("[2012-02-11, 2023-04-24)", "0-0 -10",
                                      /*last_partial_range=*/false,
                                      "step cannot be negative"),
      GenerateDateRangeArrayErrorTest("[2012-02-11, 2023-04-24)", "0-0 -10",
                                      /*last_partial_range=*/true,
                                      "step cannot be negative"),
      GenerateDateRangeArrayErrorTest(
          "[2012-02-11, 2023-04-24)", "1-3 5",
          /*last_partial_range=*/false,
          "step should either have the Y-M part or the DAY part"),
      GenerateDateRangeArrayErrorTest(
          "[2012-02-11, 2023-04-24)", "1-3 5",
          /*last_partial_range=*/true,
          "step should either have the Y-M part or the DAY part"),
  };
}

std::vector<FunctionTestCall> GetFunctionTestsGenerateDatetimeRangeArray() {
  return {
      // SUCCESS CASES:
      // NANOSECONDs step, last_partial_range = false:
      GenerateDatetimeRangeArrayTest(
          "[2023-04-24 16:35:01.123456001, 2023-04-24 16:35:01.123456015)",
          "0:0:0.000000003",  // INTERVAL 3 NANOSECOND
          /*last_partial_range=*/false,
          {
              "[2023-04-24 16:35:01.123456001, 2023-04-24 16:35:01.123456004)",
              "[2023-04-24 16:35:01.123456004, 2023-04-24 16:35:01.123456007)",
              "[2023-04-24 16:35:01.123456007, 2023-04-24 16:35:01.123456010)",
              "[2023-04-24 16:35:01.123456010, 2023-04-24 16:35:01.123456013)",
          },
          functions::kNanoseconds),
      // NANOSECONDs step, last_partial_range = true:
      GenerateDatetimeRangeArrayTest(
          "[2023-04-24 16:35:01.123456001, 2023-04-24 16:35:01.123456015)",
          "0:0:0.000000003",  // INTERVAL 3 NANOSECOND
          /*last_partial_range=*/true,
          {
              "[2023-04-24 16:35:01.123456001, 2023-04-24 16:35:01.123456004)",
              "[2023-04-24 16:35:01.123456004, 2023-04-24 16:35:01.123456007)",
              "[2023-04-24 16:35:01.123456007, 2023-04-24 16:35:01.123456010)",
              "[2023-04-24 16:35:01.123456010, 2023-04-24 16:35:01.123456013)",
              "[2023-04-24 16:35:01.123456013, 2023-04-24 16:35:01.123456015)",
          },
          functions::kNanoseconds),
      // MICROSECONDs step, last_partial_range = false:
      GenerateDatetimeRangeArrayTest(
          "[2023-04-24 16:35:01.123005, 2023-04-24 16:35:01.123034)",
          "0:0:0.000007",  // INTERVAL 7 MICROSECOND
          /*last_partial_range=*/false,
          {
              "[2023-04-24 16:35:01.123005, 2023-04-24 16:35:01.123012)",
              "[2023-04-24 16:35:01.123012, 2023-04-24 16:35:01.123019)",
              "[2023-04-24 16:35:01.123019, 2023-04-24 16:35:01.123026)",
              "[2023-04-24 16:35:01.123026, 2023-04-24 16:35:01.123033)",
          }),
      // MICROSECONDs step, last_partial_range = true:
      GenerateDatetimeRangeArrayTest(
          "[2023-04-24 16:35:01.123005, 2023-04-24 16:35:01.123034)",
          "0:0:0.000007",  // INTERVAL 7 MICROSECOND
          /*last_partial_range=*/true,
          {
              "[2023-04-24 16:35:01.123005, 2023-04-24 16:35:01.123012)",
              "[2023-04-24 16:35:01.123012, 2023-04-24 16:35:01.123019)",
              "[2023-04-24 16:35:01.123019, 2023-04-24 16:35:01.123026)",
              "[2023-04-24 16:35:01.123026, 2023-04-24 16:35:01.123033)",
              "[2023-04-24 16:35:01.123033, 2023-04-24 16:35:01.123034)",
          }),
      // MILLISECONDs step, last_partial_range = false:
      GenerateDatetimeRangeArrayTest(
          "[2023-04-24 16:35:01.003, 2023-04-24 16:35:01.030)",
          "0:0:0.005",  // INTERVAL 5 MILLISECOND
          /*last_partial_range=*/false,
          {
              "[2023-04-24 16:35:01.003, 2023-04-24 16:35:01.008)",
              "[2023-04-24 16:35:01.008, 2023-04-24 16:35:01.013)",
              "[2023-04-24 16:35:01.013, 2023-04-24 16:35:01.018)",
              "[2023-04-24 16:35:01.018, 2023-04-24 16:35:01.023)",
              "[2023-04-24 16:35:01.023, 2023-04-24 16:35:01.028)",
          }),
      // MILLISECONDs step, last_partial_range = true:
      GenerateDatetimeRangeArrayTest(
          "[2023-04-24 16:35:01.003, 2023-04-24 16:35:01.032)",
          "0:0:0.005",  // INTERVAL 5 MILLISECOND
          /*last_partial_range=*/true,
          {
              "[2023-04-24 16:35:01.003, 2023-04-24 16:35:01.008)",
              "[2023-04-24 16:35:01.008, 2023-04-24 16:35:01.013)",
              "[2023-04-24 16:35:01.013, 2023-04-24 16:35:01.018)",
              "[2023-04-24 16:35:01.018, 2023-04-24 16:35:01.023)",
              "[2023-04-24 16:35:01.023, 2023-04-24 16:35:01.028)",
              "[2023-04-24 16:35:01.028, 2023-04-24 16:35:01.032)",
          }),
      // SECONDs step, last_partial_range = false:
      GenerateDatetimeRangeArrayTest(
          "[2023-04-24 16:35:05.123456, 2023-04-24 16:36:11.123455)",
          "0:0:11",  // INTERVAL 11 SECOND
          /*last_partial_range=*/false,
          {
              "[2023-04-24 16:35:05.123456, 2023-04-24 16:35:16.123456)",
              "[2023-04-24 16:35:16.123456, 2023-04-24 16:35:27.123456)",
              "[2023-04-24 16:35:27.123456, 2023-04-24 16:35:38.123456)",
              "[2023-04-24 16:35:38.123456, 2023-04-24 16:35:49.123456)",
              "[2023-04-24 16:35:49.123456, 2023-04-24 16:36:00.123456)",
          }),
      // SECONDs step, last_partial_range = true:
      GenerateDatetimeRangeArrayTest(
          "[2023-04-24 16:35:05.123456, 2023-04-24 16:36:11.123455)",
          "0:0:11",  // INTERVAL 11 SECOND
          /*last_partial_range=*/true,
          {
              "[2023-04-24 16:35:05.123456, 2023-04-24 16:35:16.123456)",
              "[2023-04-24 16:35:16.123456, 2023-04-24 16:35:27.123456)",
              "[2023-04-24 16:35:27.123456, 2023-04-24 16:35:38.123456)",
              "[2023-04-24 16:35:38.123456, 2023-04-24 16:35:49.123456)",
              "[2023-04-24 16:35:49.123456, 2023-04-24 16:36:00.123456)",
              "[2023-04-24 16:36:00.123456, 2023-04-24 16:36:11.123455)",
          }),
      // MINUTEs step, last_partial_range = false:
      GenerateDatetimeRangeArrayTest(
          "[2023-04-24 16:20:37.123456789, 2023-04-24 17:30:00)",
          "0:20:00",  // INTERVAL 20 MINUTE
          /*last_partial_range=*/false,
          {
              "[2023-04-24 16:20:37.123456789, 2023-04-24 16:40:37.123456789)",
              "[2023-04-24 16:40:37.123456789, 2023-04-24 17:00:37.123456789)",
              "[2023-04-24 17:00:37.123456789, 2023-04-24 17:20:37.123456789)",
          },
          functions::kNanoseconds),
      // MINUTEs step, last_partial_range = true:
      GenerateDatetimeRangeArrayTest(
          "[2023-04-24 16:20:37.123456789, 2023-04-24 17:30:00)",
          "0:20:00",  // INTERVAL 20 MINUTE
          /*last_partial_range=*/true,
          {
              "[2023-04-24 16:20:37.123456789, 2023-04-24 16:40:37.123456789)",
              "[2023-04-24 16:40:37.123456789, 2023-04-24 17:00:37.123456789)",
              "[2023-04-24 17:00:37.123456789, 2023-04-24 17:20:37.123456789)",
              "[2023-04-24 17:20:37.123456789, 2023-04-24 17:30:00)",
          },
          functions::kNanoseconds),
      // HOURs step, last_partial_range = false:
      GenerateDatetimeRangeArrayTest(
          "[2023-04-24 08:20:37, 2023-04-25 08:21:37)",
          "8:00:00",  // INTERVAL 8 HOUR
          /*last_partial_range=*/false,
          {
              "[2023-04-24 08:20:37, 2023-04-24 16:20:37)",
              "[2023-04-24 16:20:37, 2023-04-25 00:20:37)",
              "[2023-04-25 00:20:37, 2023-04-25 08:20:37)",
          }),
      // HOURs step, last_partial_range = true:
      GenerateDatetimeRangeArrayTest(
          "[2023-04-24 08:20:37, 2023-04-25 08:21:37)",
          "8:00:00",  // INTERVAL 8 HOUR
          /*last_partial_range=*/true,
          {
              "[2023-04-24 08:20:37, 2023-04-24 16:20:37)",
              "[2023-04-24 16:20:37, 2023-04-25 00:20:37)",
              "[2023-04-25 00:20:37, 2023-04-25 08:20:37)",
              "[2023-04-25 08:20:37, 2023-04-25 08:21:37)",
          }),
      // DAYs step, last_partial_range = false:
      GenerateDatetimeRangeArrayTest(
          "[2020-02-24 08:20:37, 2020-03-24 08:20:37)",
          "0-0 7",  // INTERVAL 7 DAY
          /*last_partial_range=*/false,
          {
              "[2020-02-24 08:20:37, 2020-03-02 08:20:37)",
              "[2020-03-02 08:20:37, 2020-03-09 08:20:37)",
              "[2020-03-09 08:20:37, 2020-03-16 08:20:37)",
              "[2020-03-16 08:20:37, 2020-03-23 08:20:37)",
          }),
      // DAYs step, last_partial_range = true:
      GenerateDatetimeRangeArrayTest(
          "[2020-02-24 08:20:37, 2020-03-24 08:20:37)",
          "0-0 7",  // INTERVAL 7 DAY
          /*last_partial_range=*/true,
          {
              "[2020-02-24 08:20:37, 2020-03-02 08:20:37)",
              "[2020-03-02 08:20:37, 2020-03-09 08:20:37)",
              "[2020-03-09 08:20:37, 2020-03-16 08:20:37)",
              "[2020-03-16 08:20:37, 2020-03-23 08:20:37)",
              "[2020-03-23 08:20:37, 2020-03-24 08:20:37)",
          }),
      // MONTHs step
      // range_start = last day of 31-day month
      GenerateDatetimeRangeArrayTest(
          "[2020-01-31 08:20:37, 2021-03-24 08:20:37)",
          "0-5 0",  // INTERVAL 5 months
          /*last_partial_range=*/false,
          {"[2020-01-31 08:20:37, 2020-06-30 08:20:37)",
           "[2020-06-30 08:20:37, 2020-11-30 08:20:37)"}),
      GenerateDatetimeRangeArrayTest(
          "[2020-01-31 08:20:37, 2021-03-24 08:20:37)",
          "0-5 0",  // INTERVAL 5 months
          /*last_partial_range=*/true,
          {"[2020-01-31 08:20:37, 2020-06-30 08:20:37)",
           "[2020-06-30 08:20:37, 2020-11-30 08:20:37)",
           "[2020-11-30 08:20:37, 2021-03-24 08:20:37)"}),
      // range_start = last day of 30-day month
      GenerateDatetimeRangeArrayTest(
          "[2020-04-30 08:20:37, 2021-03-24 08:20:37)",
          "0-5 0",  // INTERVAL 5 months
          /*last_partial_range=*/false,
          {"[2020-04-30 08:20:37, 2020-09-30 08:20:37)",
           "[2020-09-30 08:20:37, 2021-02-28 08:20:37)"}),
      GenerateDatetimeRangeArrayTest(
          "[2020-04-30 08:20:37, 2021-03-24 08:20:37)",
          "0-5 0",  // INTERVAL 5 months
          /*last_partial_range=*/true,
          {"[2020-04-30 08:20:37, 2020-09-30 08:20:37)",
           "[2020-09-30 08:20:37, 2021-02-28 08:20:37)",
           "[2021-02-28 08:20:37, 2021-03-24 08:20:37)"}),
      // range_start = Feb 28
      GenerateDatetimeRangeArrayTest(
          "[2021-02-28 08:20:37, 2022-03-24 08:20:37)",
          "0-5 0",  // INTERVAL 5 months
          /*last_partial_range=*/false,
          {"[2021-02-28 08:20:37, 2021-07-28 08:20:37)",
           "[2021-07-28 08:20:37, 2021-12-28 08:20:37)"}),
      GenerateDatetimeRangeArrayTest(
          "[2021-02-28 08:20:37, 2022-03-24 08:20:37)",
          "0-5 0",  // INTERVAL 5 months
          /*last_partial_range=*/true,
          {"[2021-02-28 08:20:37, 2021-07-28 08:20:37)",
           "[2021-07-28 08:20:37, 2021-12-28 08:20:37)",
           "[2021-12-28 08:20:37, 2022-03-24 08:20:37)"}),
      // range_start = leap year, Feb 29
      GenerateDatetimeRangeArrayTest(
          "[2020-02-29 08:20:37, 2021-03-24 08:20:37)",
          "0-5 0",  // INTERVAL 5 months
          /*last_partial_range=*/false,
          {"[2020-02-29 08:20:37, 2020-07-29 08:20:37)",
           "[2020-07-29 08:20:37, 2020-12-29 08:20:37)"}),
      GenerateDatetimeRangeArrayTest(
          "[2020-02-29 08:20:37, 2021-03-24 08:20:37)",
          "0-5 0",  // INTERVAL 5 months
          /*last_partial_range=*/true,
          {"[2020-02-29 08:20:37, 2020-07-29 08:20:37)",
           "[2020-07-29 08:20:37, 2020-12-29 08:20:37)",
           "[2020-12-29 08:20:37, 2021-03-24 08:20:37)"}),
      // range_start = leap year, Feb 28
      GenerateDatetimeRangeArrayTest(
          "[2020-02-28 08:20:37, 2021-03-24 08:20:37)",
          "0-5 0",  // INTERVAL 5 months
          /*last_partial_range=*/false,
          {"[2020-02-28 08:20:37, 2020-07-28 08:20:37)",
           "[2020-07-28 08:20:37, 2020-12-28 08:20:37)"}),
      GenerateDatetimeRangeArrayTest(
          "[2020-02-28 08:20:37, 2021-03-24 08:20:37)",
          "0-5 0",  // INTERVAL 5 months
          /*last_partial_range=*/true,
          {"[2020-02-28 08:20:37, 2020-07-28 08:20:37)",
           "[2020-07-28 08:20:37, 2020-12-28 08:20:37)",
           "[2020-12-28 08:20:37, 2021-03-24 08:20:37)"}),
      // YEARs step
      // range_start = last day of 31-day month
      GenerateDatetimeRangeArrayTest(
          "[2020-01-31 08:20:37, 2022-03-24 08:20:37)",
          "1-0 0",  // INTERVAL 1 YEAR
          /*last_partial_range=*/false,
          {"[2020-01-31 08:20:37, 2021-01-31 08:20:37)",
           "[2021-01-31 08:20:37, 2022-01-31 08:20:37)"}),
      GenerateDatetimeRangeArrayTest(
          "[2020-01-31 08:20:37, 2022-03-24 08:20:37)",
          "1-0 0",  // INTERVAL 1 YEAR
          /*last_partial_range=*/true,
          {"[2020-01-31 08:20:37, 2021-01-31 08:20:37)",
           "[2021-01-31 08:20:37, 2022-01-31 08:20:37)",
           "[2022-01-31 08:20:37, 2022-03-24 08:20:37)"}),
      // range_start = last day of 30-day month
      GenerateDatetimeRangeArrayTest(
          "[2020-04-30 08:20:37, 2022-03-24 08:20:37)",
          "1-0 0",  // INTERVAL 1 YEAR
          /*last_partial_range=*/false,
          {"[2020-04-30 08:20:37, 2021-04-30 08:20:37)"}),
      GenerateDatetimeRangeArrayTest(
          "[2020-04-30 08:20:37, 2022-03-24 08:20:37)",
          "1-0 0",  // INTERVAL 1 YEAR
          /*last_partial_range=*/true,
          {"[2020-04-30 08:20:37, 2021-04-30 08:20:37)",
           "[2021-04-30 08:20:37, 2022-03-24 08:20:37)"}),
      // range_start = Feb 28
      GenerateDatetimeRangeArrayTest(
          "[2019-02-28 08:20:37, 2022-03-24 08:20:37)",
          "1-0 0",  // INTERVAL 1 YEAR
          /*last_partial_range=*/false,
          {"[2019-02-28 08:20:37, 2020-02-28 08:20:37)",
           "[2020-02-28 08:20:37, 2021-02-28 08:20:37)",
           "[2021-02-28 08:20:37, 2022-02-28 08:20:37)"}),
      GenerateDatetimeRangeArrayTest(
          "[2019-02-28 08:20:37, 2022-03-24 08:20:37)",
          "1-0 0",  // INTERVAL 1 YEAR
          /*last_partial_range=*/true,
          {"[2019-02-28 08:20:37, 2020-02-28 08:20:37)",
           "[2020-02-28 08:20:37, 2021-02-28 08:20:37)",
           "[2021-02-28 08:20:37, 2022-02-28 08:20:37)",
           "[2022-02-28 08:20:37, 2022-03-24 08:20:37)"}),
      // range_start = leap year, Feb 29
      GenerateDatetimeRangeArrayTest(
          "[2020-02-29 08:20:37, 2022-03-24 08:20:37)",
          "1-0 0",  // INTERVAL 1 YEAR
          /*last_partial_range=*/false,
          {"[2020-02-29 08:20:37, 2021-02-28 08:20:37)",
           "[2021-02-28 08:20:37, 2022-02-28 08:20:37)"}),
      GenerateDatetimeRangeArrayTest(
          "[2020-02-29 08:20:37, 2022-03-24 08:20:37)",
          "1-0 0",  // INTERVAL 1 YEAR
          /*last_partial_range=*/true,
          {"[2020-02-29 08:20:37, 2021-02-28 08:20:37)",
           "[2021-02-28 08:20:37, 2022-02-28 08:20:37)",
           "[2022-02-28 08:20:37, 2022-03-24 08:20:37)"}),
      // range_start = leap year, Feb 28
      GenerateDatetimeRangeArrayTest(
          "[2020-02-28 08:20:37, 2022-03-24 08:20:37)",
          "1-0 0",  // INTERVAL 1 YEAR
          /*last_partial_range=*/false,
          {"[2020-02-28 08:20:37, 2021-02-28 08:20:37)",
           "[2021-02-28 08:20:37, 2022-02-28 08:20:37)"}),
      GenerateDatetimeRangeArrayTest(
          "[2020-02-28 08:20:37, 2022-03-24 08:20:37)",
          "1-0 0",  // INTERVAL 1 YEAR
          /*last_partial_range=*/true,
          {"[2020-02-28 08:20:37, 2021-02-28 08:20:37)",
           "[2021-02-28 08:20:37, 2022-02-28 08:20:37)",
           "[2022-02-28 08:20:37, 2022-03-24 08:20:37)"}),
      // Mixed parts in step, last_partial_range = false:
      GenerateDatetimeRangeArrayTest(
          "[2023-02-24 08:20:37, 2023-03-10 15:31:28)",
          "3 17:47:53.123456",  // 3 days and 17:47:53.123456
          /*last_partial_range=*/false,
          {
              "[2023-02-24 08:20:37, 2023-02-28 02:08:30.123456)",
              "[2023-02-28 02:08:30.123456, 2023-03-03 19:56:23.246912)",
              "[2023-03-03 19:56:23.246912, 2023-03-07 13:44:16.370368)",
          }),
      // Mixed parts in step, last_partial_range = true:
      GenerateDatetimeRangeArrayTest(
          "[2023-02-24 08:20:37, 2023-03-10 15:31:28)",
          "3 17:47:53.123456",  // 3 days and 17:47:53.123456
          /*last_partial_range=*/true,
          {
              "[2023-02-24 08:20:37, 2023-02-28 02:08:30.123456)",
              "[2023-02-28 02:08:30.123456, 2023-03-03 19:56:23.246912)",
              "[2023-03-03 19:56:23.246912, 2023-03-07 13:44:16.370368)",
              "[2023-03-07 13:44:16.370368, 2023-03-10 15:31:28)",
          }),
      // Mixed parts (+ NANOSECONDs) in step, last_partial_range = false:
      GenerateDatetimeRangeArrayTest(
          "[2023-02-24 08:20:37, 2023-03-10 15:31:28)",
          "3 17:47:53.123456789",  // 3 days and 17:47:53.123456789
          /*last_partial_range=*/false,
          {
              "[2023-02-24 08:20:37, 2023-02-28 02:08:30.123456789)",
              "[2023-02-28 02:08:30.123456789, 2023-03-03 19:56:23.246913578)",
              "[2023-03-03 19:56:23.246913578, 2023-03-07 13:44:16.370370367)",
          },
          functions::kNanoseconds),
      // Mixed parts (+ NANOSECONDs) in step, last_partial_range = true:
      GenerateDatetimeRangeArrayTest(
          "[2023-02-24 08:20:37, 2023-03-10 15:31:28)",
          "3 17:47:53.123456789",  // 3 days and 17:47:53.123456789
          /*last_partial_range=*/true,
          {
              "[2023-02-24 08:20:37, 2023-02-28 02:08:30.123456789)",
              "[2023-02-28 02:08:30.123456789, 2023-03-03 19:56:23.246913578)",
              "[2023-03-03 19:56:23.246913578, 2023-03-07 13:44:16.370370367)",
              "[2023-03-07 13:44:16.370370367, 2023-03-10 15:31:28)",
          },
          functions::kNanoseconds),
      // HOURs step, exactly aligns with range end, last_partial_range = false:
      GenerateDatetimeRangeArrayTest(
          "[2023-04-24 08:20:37, 2023-04-25 08:20:37)",
          "8:00:00",  // INTERVAL 8 HOUR
          /*last_partial_range=*/false,
          {
              "[2023-04-24 08:20:37, 2023-04-24 16:20:37)",
              "[2023-04-24 16:20:37, 2023-04-25 00:20:37)",
              "[2023-04-25 00:20:37, 2023-04-25 08:20:37)",
          }),
      // HOURs step, exactly aligns with range end, last_partial_range = true:
      GenerateDatetimeRangeArrayTest(
          "[2023-04-24 08:20:37, 2023-04-25 08:20:37)",
          "8:00:00",  // INTERVAL 8 HOUR
          /*last_partial_range=*/true,
          {
              "[2023-04-24 08:20:37, 2023-04-24 16:20:37)",
              "[2023-04-24 16:20:37, 2023-04-25 00:20:37)",
              "[2023-04-25 00:20:37, 2023-04-25 08:20:37)",
          }),
      // DAYs step, exactly aligns with range end, last_partial_range = false:
      GenerateDatetimeRangeArrayTest(
          "[2020-02-24 08:20:37, 2020-03-23 08:20:37)",
          "0-0 7",  // INTERVAL 7 DAY
          /*last_partial_range=*/false,
          {
              "[2020-02-24 08:20:37, 2020-03-02 08:20:37)",
              "[2020-03-02 08:20:37, 2020-03-09 08:20:37)",
              "[2020-03-09 08:20:37, 2020-03-16 08:20:37)",
              "[2020-03-16 08:20:37, 2020-03-23 08:20:37)",
          }),
      // DAYs step, exactly aligns with range end, last_partial_range = true:
      GenerateDatetimeRangeArrayTest(
          "[2020-02-24 08:20:37, 2020-03-23 08:20:37)",
          "0-0 7",  // INTERVAL 7 DAY
          /*last_partial_range=*/true,
          {
              "[2020-02-24 08:20:37, 2020-03-02 08:20:37)",
              "[2020-03-02 08:20:37, 2020-03-09 08:20:37)",
              "[2020-03-09 08:20:37, 2020-03-16 08:20:37)",
              "[2020-03-16 08:20:37, 2020-03-23 08:20:37)",
          }),
      // Largest possible step, last_partial_range = false:
      GenerateDatetimeRangeArrayTest(
          "[2023-02-24 08:20:37, 9997-05-12 17:13:41.123456)",
          // Largest possible INTERVAL value (~20,000 years)
          "366000 87840000:0:0",
          /*last_partial_range=*/false, {}),
      // Largest possible step, last_partial_range = true:
      GenerateDatetimeRangeArrayTest(
          "[2023-02-24 08:20:37, 9997-05-12 17:13:41.123456)",
          "366000 87840000:0:0",  // Largest possible INTERVAL value
          /*last_partial_range=*/true,
          {"[2023-02-24 08:20:37, 9997-05-12 17:13:41.123456)"}),
      // Step covers full TIMESTAMP range with DAY part,
      // last_partial_range = false:
      GenerateDatetimeRangeArrayTest(
          "[0001-01-01 00:00:00, 9999-12-31 23:59:59.999999)",
          "0-0 3652058",  // INTERVAL 3652058 DAY
          /*last_partial_range=*/false,
          {"[0001-01-01 00:00:00, 9999-12-31 00:00:00)"}),
      // Step covers full TIMESTAMP range with DAY part,
      // last_partial_range = true:
      GenerateDatetimeRangeArrayTest(
          "[0001-01-01 00:00:00, 9999-12-31 23:59:59.999999)",
          "0-0 3652058",  // INTERVAL 3652058 DAY
          /*last_partial_range=*/true,
          {
              "[0001-01-01 00:00:00, 9999-12-31 00:00:00)",
              "[9999-12-31 00:00:00, 9999-12-31 23:59:59.999999)",
          }),
      // Step covers full TIMESTAMP range with MICROS part,
      // last_partial_range = false:
      GenerateDatetimeRangeArrayTest(
          "[0001-01-01 00:00:00, 9999-12-31 23:59:59.999999)",
          "87649415:0:0",  // INTERVAL 87649415 HOUR
          /*last_partial_range=*/false,
          {"[0001-01-01 00:00:00, 9999-12-31 23:00:00)"}),
      // Step covers full TIMESTAMP range with MICROS part,
      // last_partial_range = true:
      GenerateDatetimeRangeArrayTest(
          "[0001-01-01 00:00:00, 9999-12-31 23:59:59.999999)",
          "87649415:0:0",  // INTERVAL 87649415 HOUR
          /*last_partial_range=*/true,
          {
              "[0001-01-01 00:00:00, 9999-12-31 23:00:00)",
              "[9999-12-31 23:00:00, 9999-12-31 23:59:59.999999)",
          }),
      // Very large step with DAY part, last_partial_range = false:
      GenerateDatetimeRangeArrayTest(
          "[0001-01-01 00:00:00, 9999-12-31 23:59:59.999999)",
          "0-0 915000",  // ~2500 years
          /*last_partial_range=*/false,
          {
              "[0001-01-01 00:00:00, 2506-03-10 00:00:00)",
              "[2506-03-10 00:00:00, 5011-05-17 00:00:00)",
              "[5011-05-17 00:00:00, 7516-07-23 00:00:00)",
          }),
      // Very large step with DAY part, last_partial_range = true:
      GenerateDatetimeRangeArrayTest(
          "[0001-01-01 00:00:00, 9999-12-31 23:59:59.999999)",
          "0-0 915000",  // ~2500 years
          /*last_partial_range=*/true,
          {
              "[0001-01-01 00:00:00, 2506-03-10 00:00:00)",
              "[2506-03-10 00:00:00, 5011-05-17 00:00:00)",
              "[5011-05-17 00:00:00, 7516-07-23 00:00:00)",
              "[7516-07-23 00:00:00, 9999-12-31 23:59:59.999999)",
          }),
      // Very large step with DAY part, with nanos, last_partial_range = false:
      GenerateDatetimeRangeArrayTest(
          "[0001-01-01 00:00:00, 9999-12-31 23:59:59.999999999)",
          "0-0 915000",  // ~2500 years
          /*last_partial_range=*/false,
          {
              "[0001-01-01 00:00:00, 2506-03-10 00:00:00)",
              "[2506-03-10 00:00:00, 5011-05-17 00:00:00)",
              "[5011-05-17 00:00:00, 7516-07-23 00:00:00)",
          },
          functions::kNanoseconds),
      // Very large step with DAY part, with nanos, last_partial_range = true:
      GenerateDatetimeRangeArrayTest(
          "[0001-01-01 00:00:00, 9999-12-31 23:59:59.999999999)",
          "0-0 915000",  // ~2500 years
          /*last_partial_range=*/true,
          {
              "[0001-01-01 00:00:00, 2506-03-10 00:00:00)",
              "[2506-03-10 00:00:00, 5011-05-17 00:00:00)",
              "[5011-05-17 00:00:00, 7516-07-23 00:00:00)",
              "[7516-07-23 00:00:00, 9999-12-31 23:59:59.999999999)",
          },
          functions::kNanoseconds),
      // Very large step with MICROS part, last_partial_range = false:
      GenerateDatetimeRangeArrayTest(
          "[0001-01-01 00:00:00, 9999-12-31 23:59:59.999999)",
          "21960000:17:43.123456",  // ~2500 years
          /*last_partial_range=*/false,
          {
              "[0001-01-01 00:00:00, 2506-03-10 00:17:43.123456)",
              "[2506-03-10 00:17:43.123456, 5011-05-17 00:35:26.246912)",
              "[5011-05-17 00:35:26.246912, 7516-07-23 00:53:09.370368)",
          }),
      // Very large step with MICROS part, last_partial_range = true:
      GenerateDatetimeRangeArrayTest(
          "[0001-01-01 00:00:00, 9999-12-31 23:59:59.999999)",
          "21960000:17:43.123456",  // ~2500 years
          /*last_partial_range=*/true,
          {
              "[0001-01-01 00:00:00, 2506-03-10 00:17:43.123456)",
              "[2506-03-10 00:17:43.123456, 5011-05-17 00:35:26.246912)",
              "[5011-05-17 00:35:26.246912, 7516-07-23 00:53:09.370368)",
              "[7516-07-23 00:53:09.370368, 9999-12-31 23:59:59.999999)",
          }),
      // Very large step with MICROS and NANOS parts,
      // last_partial_range = false:
      GenerateDatetimeRangeArrayTest(
          "[0001-01-01 00:00:00, 9999-12-31 23:59:59.999999999)",
          "21960000:17:43.123456789",  // ~2500 years
          /*last_partial_range=*/false,
          {
              "[0001-01-01 00:00:00, 2506-03-10 00:17:43.123456789)",
              "[2506-03-10 00:17:43.123456789, 5011-05-17 00:35:26.246913578)",
              "[5011-05-17 00:35:26.246913578, 7516-07-23 00:53:09.370370367)",
          },
          functions::kNanoseconds),
      // Very large step with MICROS and NANOS parts, last_partial_range = true:
      GenerateDatetimeRangeArrayTest(
          "[0001-01-01 00:00:00, 9999-12-31 23:59:59.999999999)",
          "21960000:17:43.123456789",  // ~2500 years
          /*last_partial_range=*/true,
          {
              "[0001-01-01 00:00:00, 2506-03-10 00:17:43.123456789)",
              "[2506-03-10 00:17:43.123456789, 5011-05-17 00:35:26.246913578)",
              "[5011-05-17 00:35:26.246913578, 7516-07-23 00:53:09.370370367)",
              "[7516-07-23 00:53:09.370370367, 9999-12-31 23:59:59.999999999)",
          },
          functions::kNanoseconds),
      // ERROR CASES:
      // Unsupported input RANGE values:
      GenerateDatetimeRangeArrayErrorTest(
          "[UNBOUNDED, 2023-04-24 16:35:01)", "0:0:1",
          /*last_partial_range=*/false,
          "input RANGE cannot have UNBOUNDED endpoints"),
      GenerateDatetimeRangeArrayErrorTest(
          "[2023-04-24 16:35:01, UNBOUNDED)", "0:0:1",
          /*last_partial_range=*/false,
          "input RANGE cannot have UNBOUNDED endpoints"),
      GenerateDatetimeRangeArrayErrorTest(
          "[UNBOUNDED, UNBOUNDED)", "0:0:1",
          /*last_partial_range=*/false,
          "input RANGE cannot have UNBOUNDED endpoints"),
      GenerateDatetimeRangeArrayErrorTest(
          "[UNBOUNDED, 2023-04-24 16:35:01)", "0:0:1",
          /*last_partial_range=*/true,
          "input RANGE cannot have UNBOUNDED endpoints"),
      GenerateDatetimeRangeArrayErrorTest(
          "[2023-04-24 16:35:01, UNBOUNDED)", "0:0:1",
          /*last_partial_range=*/true,
          "input RANGE cannot have UNBOUNDED endpoints"),
      GenerateDatetimeRangeArrayErrorTest(
          "[UNBOUNDED, UNBOUNDED)", "0:0:1",
          /*last_partial_range=*/true,
          "input RANGE cannot have UNBOUNDED endpoints"),
      GenerateDatetimeRangeArrayErrorTest(
          "[2012-02-11 10:21:57.000001, 2012-02-11 10:21:57.000003)",
          "0:0:0.000000001",  // Non-zero NANOSECOND part
          /*last_partial_range=*/false,
          "step with non-zero NANOSECOND part is not supported",
          functions::kMicroseconds),
      GenerateDatetimeRangeArrayErrorTest(
          "[2012-02-11 10:21:57.000001, 2012-02-11 10:21:57.000003)",
          "0:0:0.000000001",  // Non-zero NANOSECOND part
          /*last_partial_range=*/true,
          "step with non-zero NANOSECOND part is not supported",
          functions::kMicroseconds),
      // Unsupported and supported step INTERVAL parts mixed together:
      GenerateDatetimeRangeArrayErrorTest(
          "[2023-02-24 08:20:37, 2023-06-10 15:31:28)",
          "0-1 3 17:47:53.123456",  // 1 month 3 days and 17:47:53.123456
          /*last_partial_range=*/false,
          "step should either have the Y-M part or the D (H:M:S[.F]) part"),
      GenerateDatetimeRangeArrayErrorTest(
          "[2023-02-24 08:20:37, 2023-06-10 15:31:28)",
          "0-1 3 17:47:53.123456",  // 1 month 3 days and 17:47:53.123456
          /*last_partial_range=*/true,
          "step should either have the Y-M part or the D (H:M:S[.F]) part"),
      GenerateDatetimeRangeArrayErrorTest(
          "[2023-02-24 08:20:37, 2023-06-10 15:31:28)",
          "0-1 1",  // 1 month and 1 day
          /*last_partial_range=*/false,
          "step should either have the Y-M part or the D (H:M:S[.F]) part"),
      GenerateDatetimeRangeArrayErrorTest(
          "[2023-02-24 08:20:37, 2023-06-10 15:31:28)",
          "0-1 0 1:0:0",  // 1 month and 1 hour
          /*last_partial_range=*/false,
          "step should either have the Y-M part or the D (H:M:S[.F]) part"),
      GenerateDatetimeRangeArrayErrorTest(
          "[2012-02-11 10:21:57, 2023-04-24 16:35:01)",
          "10000:0:0.000000001",  // Non-zero NANOSECOND part
          /*last_partial_range=*/false,
          "step with non-zero NANOSECOND part is not supported",
          functions::kMicroseconds),
      // Invalid step INTERVAL values:
      GenerateDatetimeRangeArrayErrorTest(
          "[2012-02-11 10:21:57, 2023-04-24 16:35:01)",
          "0:0:0",  // Zero INTERVAL
          /*last_partial_range=*/false, "step cannot be 0"),
      GenerateDatetimeRangeArrayErrorTest(
          "[2012-02-11 10:21:57, 2023-04-24 16:35:01)",
          "0-0 -1",  // Negative DAY part
          /*last_partial_range=*/false, "step cannot be negative"),
      GenerateDatetimeRangeArrayErrorTest(
          "[2012-02-11 10:21:57, 2023-04-24 16:35:01)",
          "-0:0:1",  // Negative MICROSECOND part
          /*last_partial_range=*/false, "step cannot be negative"),
  };
}

std::vector<FunctionTestCall>
GetFunctionTestsGenerateTimestampRangeArrayExtras() {
  static constexpr absl::string_view kFnName = "generate_range_array";

  std::vector<FunctionTestCall> test_cases;
  // Generate NULL arguments test cases.
  const Value range =
      RangeFromStr("[2021-04-24 16:35:01, 2023-01-18 01:12:14)",
                   types::TimestampRangeType(), functions::kMicroseconds);
  const Value step = Value::Interval(IntervalValue::FromDays(360).value());
  const Value null_range_array =
      Value::Null(MakeArrayType(types::TimestampRangeType()));
  for (const Value& arg1 : {range, Value::Null(range.type())}) {
    for (const Value& arg2 : {step, Value::Null(step.type())}) {
      if (arg1.is_null() || arg2.is_null()) {
        // 2 arguments overload.
        test_cases.push_back({kFnName, {arg1, arg2}, null_range_array});
      }
      for (const Value& arg3 :
           {Value::Bool(false), Value::Bool(true), Value::NullBool()}) {
        if (arg1.is_null() || arg2.is_null() || arg3.is_null())
          // 3 arguments overload.
          test_cases.push_back({kFnName, {arg1, arg2, arg3}, null_range_array});
      }
    }
  }

  // Generate NULL RANGE start/end test cases.
  for (const Value& range_start :
       {TimestampFromStr("2021-04-24 16:35:01"), Value::NullTimestamp()}) {
    for (const Value& range_end :
         {TimestampFromStr("2023-01-18 01:12:14"), Value::NullTimestamp()}) {
      if (range_start.is_null() || range_end.is_null()) {
        Value range = Value::MakeRange(range_start, range_end).value();
        // 2 arguments overload.
        test_cases.push_back({kFnName,
                              {range, step},
                              null_range_array,
                              absl::StatusCode::kOutOfRange});
        // 3 arguments overload.
        test_cases.push_back({kFnName,
                              {range, step, Value::Bool(false)},
                              null_range_array,
                              absl::StatusCode::kOutOfRange});
      }
    }
  }

  for (FunctionTestCall& test_case : test_cases) {
    test_case.params.AddRequiredFeatures(
        {FEATURE_RANGE_TYPE, FEATURE_INTERVAL_TYPE});
  }
  return test_cases;
}

std::vector<FunctionTestCall> GetFunctionTestsGenerateDateRangeArrayExtras() {
  static constexpr absl::string_view kFnName = "generate_range_array";

  std::vector<FunctionTestCall> test_cases;
  // Generate NULL arguments test cases.
  const Value range =
      RangeFromStr("[2021-04-24, 2023-01-18)", types::DateRangeType(),
                   functions::kMicroseconds);
  const Value step = Value::Interval(IntervalValue::FromDays(360).value());
  const Value null_range_array =
      Value::Null(MakeArrayType(types::DateRangeType()));
  for (const Value& arg1 : {range, Value::Null(range.type())}) {
    for (const Value& arg2 : {step, Value::Null(step.type())}) {
      if (arg1.is_null() || arg2.is_null()) {
        // 2 arguments overload.
        test_cases.push_back({kFnName, {arg1, arg2}, null_range_array});
      }
      for (const Value& arg3 :
           {Value::Bool(false), Value::Bool(true), Value::NullBool()}) {
        if (arg1.is_null() || arg2.is_null() || arg3.is_null())
          // 3 arguments overload.
          test_cases.push_back({kFnName, {arg1, arg2, arg3}, null_range_array});
      }
    }
  }

  // Generate NULL RANGE start/end test cases.
  for (const Value& range_start :
       {DateFromStr("2021-04-24"), Value::NullDate()}) {
    for (const Value& range_end :
         {DateFromStr("2023-01-18"), Value::NullDate()}) {
      if (range_start.is_null() || range_end.is_null()) {
        Value range = Value::MakeRange(range_start, range_end).value();
        // 2 arguments overload.
        test_cases.push_back({kFnName,
                              {range, step},
                              null_range_array,
                              absl::StatusCode::kOutOfRange});
        // 3 arguments overload.
        test_cases.push_back({kFnName,
                              {range, step, Value::Bool(false)},
                              null_range_array,
                              absl::StatusCode::kOutOfRange});
      }
    }
  }

  for (FunctionTestCall& test_case : test_cases) {
    test_case.params.AddRequiredFeatures(
        {FEATURE_RANGE_TYPE, FEATURE_INTERVAL_TYPE});
  }
  return test_cases;
}

std::vector<FunctionTestCall>
GetFunctionTestsGenerateDatetimeRangeArrayExtras() {
  static constexpr absl::string_view kFnName = "generate_range_array";

  std::vector<FunctionTestCall> test_cases;
  // Generate NULL arguments test cases.
  const Value range =
      RangeFromStr("[2021-04-24 16:35:01, 2023-01-18 01:12:14)",
                   types::DatetimeRangeType(), functions::kMicroseconds);
  const Value step = Value::Interval(IntervalValue::FromDays(360).value());
  const Value null_range_array =
      Value::Null(MakeArrayType(types::DatetimeRangeType()));
  for (const Value& arg1 : {range, Value::Null(range.type())}) {
    for (const Value& arg2 : {step, Value::Null(step.type())}) {
      if (arg1.is_null() || arg2.is_null()) {
        // 2 arguments overload.
        test_cases.push_back({kFnName, {arg1, arg2}, null_range_array});
      }
      for (const Value& arg3 :
           {Value::Bool(false), Value::Bool(true), Value::NullBool()}) {
        if (arg1.is_null() || arg2.is_null() || arg3.is_null())
          // 3 arguments overload.
          test_cases.push_back({kFnName, {arg1, arg2, arg3}, null_range_array});
      }
    }
  }

  // Generate NULL RANGE start/end test cases.
  for (const Value& range_start :
       {DatetimeFromStr("2021-04-24 16:35:01"), Value::NullDatetime()}) {
    for (const Value& range_end :
         {DatetimeFromStr("2023-01-18 01:12:14"), Value::NullDatetime()}) {
      if (range_start.is_null() || range_end.is_null()) {
        Value range = Value::MakeRange(range_start, range_end).value();
        // 2 arguments overload.
        test_cases.push_back({kFnName,
                              {range, step},
                              null_range_array,
                              absl::StatusCode::kOutOfRange});
        // 3 arguments overload.
        test_cases.push_back({kFnName,
                              {range, step, Value::Bool(false)},
                              null_range_array,
                              absl::StatusCode::kOutOfRange});
      }
    }
  }

  for (FunctionTestCall& test_case : test_cases) {
    test_case.params.AddRequiredFeatures(
        {FEATURE_RANGE_TYPE, FEATURE_INTERVAL_TYPE, FEATURE_CIVIL_TIME});
  }
  return test_cases;
}

}  // namespace googlesql
