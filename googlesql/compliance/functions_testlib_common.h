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

#ifndef GOOGLESQL_COMPLIANCE_FUNCTIONS_TESTLIB_COMMON_H_
#define GOOGLESQL_COMPLIANCE_FUNCTIONS_TESTLIB_COMMON_H_

#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/common/float_margin.h"
#include "googlesql/common/testing/testing_proto_util.h"
#include "googlesql/public/functions/common_proto.h"
#include "googlesql/public/functions/date_time_util.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/value.h"
#include "googlesql/testing/test_function.h"
#include "googlesql/testing/test_value.h"
#include "absl/base/casts.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "googlesql/base/status.h"

namespace googlesql {

constexpr auto kApproximate = kDefaultFloatMargin;
constexpr auto kOneUlp = FloatMargin::UlpMarginStrictZero(1);

const int32_t int32min = std::numeric_limits<int32_t>::min();
const int32_t int32max = std::numeric_limits<int32_t>::max();
const int64_t int64min = std::numeric_limits<int64_t>::min();
const int64_t int64max = std::numeric_limits<int64_t>::max();
const uint32_t uint32max = std::numeric_limits<uint32_t>::max();
const uint64_t uint64max = std::numeric_limits<uint64_t>::max();

const int32_t date_min = types::kDateMin;
const int32_t date_max = types::kDateMax;
const int64_t timestamp_min = types::kTimestampMin;
const int64_t timestamp_max = types::kTimestampMax;

const int64_t int32max_plus_one = static_cast<int64_t>(int32max) + 1;
const int64_t int32min_minus_one = static_cast<int64_t>(int32min) - 1;
const int64_t uint32max_plus_one = static_cast<int64_t>(uint32max) + 1;
const uint64_t int64max_plus_one = static_cast<uint64_t>(int64max) + 1;

const int64_t seconds_min = types::kTimestampSecondsMin;
const int64_t seconds_max = types::kTimestampSecondsMax;
const int64_t millis_min = types::kTimestampMillisMin;
const int64_t millis_max = types::kTimestampMillisMax;
const int64_t micros_min = types::kTimestampMicrosMin;
const int64_t micros_max = types::kTimestampMicrosMax;
const int64_t nanos_min = types::kTimestampNanosMin;
const int64_t nanos_max = types::kTimestampNanosMax;

const float floatmax = std::numeric_limits<float>::max();
const float floatmin = std::numeric_limits<float>::lowest();
const float floatminpositive = std::numeric_limits<float>::min();
const float float_pos_inf = std::numeric_limits<float>::infinity();
const float float_neg_inf = -std::numeric_limits<float>::infinity();
const float float_nan = std::numeric_limits<float>::quiet_NaN();
const float float_neg_nan = absl::bit_cast<float>(0xffc00000u);
const double doublemax = std::numeric_limits<double>::max();
const double doublemin = std::numeric_limits<double>::lowest();
const double doubleminpositive = std::numeric_limits<double>::min();
const double double_pos_inf = std::numeric_limits<double>::infinity();
const double double_neg_inf = -std::numeric_limits<double>::infinity();
const double double_nan = std::numeric_limits<double>::quiet_NaN();
const double double_neg_nan = absl::bit_cast<double>(0xfff8000000000000ull);

TypeFactory* type_factory();
const EnumType* TestEnumType();
const ProtoType* KitchenSinkProtoType();
const ProtoType* Proto3TimestampType();
const ProtoType* Proto3DateType();
const ProtoType* Proto3LatLngType();
const ProtoType* Proto3TimeOfDayType();
const ProtoType* ProtoDurationType();

const ProtoType* CivilTimeTypesSinkProtoType();
const ProtoType* NullableIntProtoType();
const StructType* SimpleStructType();
const StructType* AnotherStructType();
// Helpers for Values.
Value Timestamp(int64_t v);
Value Timestamp(absl::Time v);
Value DateFromStr(absl::string_view str);
Value TimestampFromStr(absl::string_view str,
                       functions::TimestampScale = functions::kMicroseconds);
Value DatetimeFromStr(absl::string_view str,
                      functions::TimestampScale = functions::kMicroseconds);
Value TimeFromStr(absl::string_view str,
                  functions::TimestampScale = functions::kMicroseconds);
Value TimeMicros(int hour, int minute, int second, int microseconds);
Value TimeNanos(int hour, int minute, int second, int nanoseconds);
Value DatetimeMicros(int year, int month, int day, int hour, int minute,
                     int second, int microseconds);
Value DatetimeNanos(int year, int month, int day, int hour, int minute,
                    int second, int nanoseconds);

Value KitchenSink(absl::string_view proto_str);
Value Proto3Timestamp(int64_t seconds, int32_t nanos);
Value Proto3Date(int32_t year, int32_t month, int32_t day);
Value Proto3LatLng(double latitude, double longitude);
Value Proto3TimeOfDay(int32_t hour, int32_t minute, int32_t seconds,
                      int32_t nanos);
Value ProtoDuration(int64_t seconds, int32_t nanos);
Value CivilTimeTypesSink(absl::string_view proto_str);
Value NullableInt(absl::string_view proto_str);

// Creates a Value for one of the Proto3 wrapper messages defined in
// google/protobuf/wrappers.proto. The input argument type must correspond to
// the 'value' field of the wrapper message.
template <typename Wrapper>
Value Proto3Wrapper(
    const typename std::invoke_result<
        functions::internal::ValidWrapperConversions, Wrapper>::type& input) {
  Wrapper proto3_wrapper;
  proto3_wrapper.set_value(input);
  const ProtoType* wrapper_type;
  GOOGLESQL_CHECK_OK(
      type_factory()->MakeProtoType(Wrapper::descriptor(), &wrapper_type));

  return Value::Proto(wrapper_type, SerializeToCord(proto3_wrapper));
}

template <>
inline Value Proto3Wrapper<google::protobuf::BytesValue>(
    const absl::Cord& input) {
  google::protobuf::BytesValue proto3_wrapper;
  proto3_wrapper.set_value(std::string(input));
  const ProtoType* wrapper_type;
  GOOGLESQL_CHECK_OK(
      type_factory()->MakeProtoType(google::protobuf::BytesValue::descriptor(),
                                    &wrapper_type));

  return Value::Proto(wrapper_type, SerializeToCord(proto3_wrapper));
}
enum ComparisonResult {
  NULL_VALUE,  // output should be NULL
  EQUAL,       // left == right
  LESS,        // left < right
  // Values are not equal. Comparison operators return FALSE. Ordering
  // semantics report LESS.
  // There is an unfortunate exception: GREATEST/LEAST let NaN saturate for
  // floating points.
  UNORDERED_BUT_ARRAY_ORDERS_LESS,

  // Values are definitely not equal. Comparison operators return NULL. Ordering
  // semantics report LESS.
  // e.g. [NULL] vs [1, NULL], lengths are different so they're unequal.
  ARRAY_UNEQUAL_ORDERS_LESS,
};

struct ComparisonTest {
  ComparisonTest(const ValueConstructor& left_in,
                 const ValueConstructor& right_in, ComparisonResult result_in,
                 const std::set<LanguageFeature>& required_features = {})
      : left(left_in.get()),
        right(right_in.get()),
        result(result_in),
        required_features(required_features) {}
  // Returns NaN of the correct type when result == UNORDERED.
  Value GetNaN() const {
    ABSL_CHECK_EQ(result, UNORDERED_BUT_ARRAY_ORDERS_LESS);
    if (left.type_kind() == TYPE_DOUBLE) {
      return Value::Double(double_nan);
    }
    ABSL_CHECK_EQ(left.type_kind(), TYPE_FLOAT);
    return Value::Float(float_nan);
  }
  Value left;
  Value right;
  ComparisonResult result;
  std::set<LanguageFeature> required_features;
};

// Represents a test case for TIME and DATETIME asserting
//   func(<input>)
// either:
//   returns a value that matches the expected output, or
//   give an error with the same error code as the expected error status.
// For the same <input>, the output might differ depending on the language
// option FEATURE_TIMESTAMP_NANOS. <micros_output> and <nanos_output> keep the
// expected output (or error) when FEATURE_TIMESTAMP_NANOS is disabled and
// enabled, respectively.
// <micros_output> and <nanos_output> should always have the same type if they
// are not errors, and <output_type> will be populated to this type upon
// construction. In case both output are errors, the <output_type> *must* be
// explicitly specified (or the constructors will crash).
// <required_features> specifies any language features (in addition to
// FEATURE_CIVIL_TIME or FEATURE_TIMESTAMP_NANOS) that are required.
struct CivilTimeTestCase {
  CivilTimeTestCase(const std::vector<ValueConstructor>& input,
                    const absl::StatusOr<Value>& expected_output,
                    const Type* output_type = nullptr,
                    const std::set<LanguageFeature>& required_features = {})
      : CivilTimeTestCase(input, expected_output, expected_output, output_type,
                          required_features) {}

  // For test cases where <micros_output> is different with <nanos_output>.
  CivilTimeTestCase(const std::vector<ValueConstructor>& input,
                    const absl::StatusOr<Value>& micros_output,
                    const absl::StatusOr<Value>& nanos_output,
                    const Type* output_type = nullptr,
                    const std::set<LanguageFeature>& required_features = {});

  std::vector<ValueConstructor> input;
  absl::StatusOr<Value> micros_output;
  absl::StatusOr<Value> nanos_output;
  std::set<LanguageFeature> required_features;
  const Type* output_type;
};

// Adds the <test_case> to <result>, filling in the options map to try the same
// test case in micros and nanos mode.
void AddTestCaseWithWrappedResultForCivilTimeAndNanos(
    const CivilTimeTestCase& test_case,
    std::vector<QueryParamsWithResult>* results);
void AddTestCaseWithWrappedResultForCivilTimeAndNanos(
    const CivilTimeTestCase& test_case, absl::string_view function_name,
    std::vector<FunctionTestCall>* results);

// Forward declarations for functions related to legacy timestamp types.
std::vector<std::vector<Value>> GetRowsOfLegacyTimestampValues();
std::vector<ComparisonTest> GetLegacyTimestampComparisonTests();
std::vector<QueryParamsWithResult> GetFunctionTestsLegacyTimestampIsNull();

std::vector<std::vector<Value>> GetRowsOfValues();

// Represents a test case for normalize and normalize_and_casefold functions,
// which is controlled by is_casefold.
// The expected_nfs should follow the exact same size and order of enums
// defined in NormalizeMode.
struct NormalizeTestCase {
  NormalizeTestCase(const ValueConstructor& input,
                    const std::vector<ValueConstructor>& expected_nfs,
                    bool is_casefold = false)
      : input(input), expected_nfs(expected_nfs), is_casefold(is_casefold) {}

  const ValueConstructor input;
  const std::vector<ValueConstructor> expected_nfs;
  const bool is_casefold;
};

std::string EscapeKey(bool sql_standard_mode, absl::string_view key);

Value StringToBytes(const Value& value);

}  // namespace googlesql

#endif  // GOOGLESQL_COMPLIANCE_FUNCTIONS_TESTLIB_COMMON_H_
