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

#include "googlesql/compliance/functions_testlib_common.h"

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "google/protobuf/duration.pb.h"
#include "google/protobuf/timestamp.pb.h"
#include "google/type/date.pb.h"
#include "google/type/latlng.pb.h"
#include "google/type/timeofday.pb.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/text_format.h"
#include "googlesql/common/status_payload_utils.h"
#include "googlesql/common/testing/testing_proto_util.h"
#include "googlesql/public/civil_time.h"
#include "googlesql/public/functions/date_time_util.h"
#include "googlesql/public/functions/datetime.pb.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "googlesql/testing/using_test_value.cc"  // NOLINT
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"

namespace googlesql {
namespace {

Value ProtoToValue(const ProtoType* type, const google::protobuf::Message& msg) {
  absl::Cord bytes;
  ABSL_CHECK(msg.SerializeToCord(&bytes));
  return Value::Proto(type, bytes);
}

}  // namespace

static TypeFactory* s_type_factory_ = nullptr;

TypeFactory* type_factory() {
  if (!s_type_factory_) {
    s_type_factory_ = new TypeFactory();
  }
  return s_type_factory_;
}

const EnumType* TestEnumType() {
  const EnumType* enum_type;
  const google::protobuf::EnumDescriptor* enum_descriptor =
      googlesql_test::TestEnum_descriptor();
  GOOGLESQL_CHECK_OK(type_factory()->MakeEnumType(enum_descriptor, &enum_type));
  return enum_type;
}

const ProtoType* KitchenSinkProtoType() {
  const ProtoType* kitchen_sink_proto_type;
  GOOGLESQL_CHECK_OK(type_factory()->MakeProtoType(
      googlesql_test::KitchenSinkPB::descriptor(), &kitchen_sink_proto_type));
  return kitchen_sink_proto_type;
}

const ProtoType* Proto3TimestampType() {
  const ProtoType* proto3_timestamp_type;
  GOOGLESQL_CHECK_OK(type_factory()->MakeProtoType(
      google::protobuf::Timestamp::descriptor(), &proto3_timestamp_type));
  return proto3_timestamp_type;
}

const ProtoType* Proto3DateType() {
  const ProtoType* proto3_date_type;
  GOOGLESQL_CHECK_OK(type_factory()->MakeProtoType(
      google::type::Date::descriptor(), &proto3_date_type));
  return proto3_date_type;
}

const ProtoType* Proto3LatLngType() {
  const ProtoType* proto3_latlng_type;
  GOOGLESQL_CHECK_OK(type_factory()->MakeProtoType(
      google::type::LatLng::descriptor(), &proto3_latlng_type));
  return proto3_latlng_type;
}

const ProtoType* Proto3TimeOfDayType() {
  const ProtoType* proto3_time_of_day_type;
  GOOGLESQL_CHECK_OK(type_factory()->MakeProtoType(
      google::type::TimeOfDay::descriptor(), &proto3_time_of_day_type));
  return proto3_time_of_day_type;
}

const ProtoType* CivilTimeTypesSinkProtoType() {
  const ProtoType* civil_time_types_sink_proto_type;
  GOOGLESQL_CHECK_OK(type_factory()->MakeProtoType(
      googlesql_test::CivilTimeTypesSinkPB::descriptor(),
      &civil_time_types_sink_proto_type));
  return civil_time_types_sink_proto_type;
}

const ProtoType* NullableIntProtoType() {
  const ProtoType* nullable_int_proto_type;
  GOOGLESQL_CHECK_OK(type_factory()->MakeProtoType(
      googlesql_test::NullableInt::descriptor(), &nullable_int_proto_type));
  return nullable_int_proto_type;
}

const ProtoType* ProtoDurationType() {
  const ProtoType* proto_duration_type;
  GOOGLESQL_CHECK_OK(type_factory()->MakeProtoType(
      google::protobuf::Duration::descriptor(), &proto_duration_type));
  return proto_duration_type;
}

const StructType* SimpleStructType() {
  const StructType* struct_type;
  GOOGLESQL_CHECK_OK(type_factory()->MakeStructType(
      {{"a", StringType()}, {"b", Int32Type()}}, &struct_type));
  return struct_type;
}

const StructType* AnotherStructType() {
  const StructType* struct_type;
  GOOGLESQL_CHECK_OK(type_factory()->MakeStructType(
      {{"a", StringType()}}, &struct_type));
  return struct_type;
}

Value DateFromStr(absl::string_view str) {
  int32_t date;
  GOOGLESQL_CHECK_OK(functions::ConvertStringToDate(str, &date));
  return Value::Date(date);
}
Value TimestampFromStr(absl::string_view str, functions::TimestampScale scale) {
  absl::Time timestamp;
  // Test cases use consistently same timezone.
  GOOGLESQL_CHECK_OK(functions::ConvertStringToTimestamp(
      str, absl::UTCTimeZone(), scale, true /* allow_tz_in_str */, &timestamp));
  return Value::Timestamp(timestamp);
}
Value DatetimeFromStr(absl::string_view str, functions::TimestampScale scale) {
  DatetimeValue datetime;
  GOOGLESQL_CHECK_OK(functions::ConvertStringToDatetime(str, scale, &datetime));
  return Value::Datetime(datetime);
}
Value TimeFromStr(absl::string_view str, functions::TimestampScale scale) {
  TimeValue time;
  GOOGLESQL_CHECK_OK(functions::ConvertStringToTime(str, scale, &time));
  return Value::Time(time);
}

Value Timestamp(int64_t v) { return Value::TimestampFromUnixMicros(v); }
Value Timestamp(absl::Time v) { return Value::Timestamp(v); }
Value TimeMicros(int hour, int minute, int second, int microseconds) {
  return Value::Time(TimeValue::FromHMSAndMicros(
      hour, minute, second, microseconds));
}
Value TimeNanos(int hour, int minute, int second, int nanoseconds) {
  return Value::Time(TimeValue::FromHMSAndNanos(
      hour, minute, second, nanoseconds));
}
Value DatetimeMicros(int year, int month, int day, int hour, int minute,
                     int second, int microseconds) {
  return Value::Datetime(DatetimeValue::FromYMDHMSAndMicros(
      year, month, day, hour, minute, second, microseconds));
}
Value DatetimeNanos(int year, int month, int day, int hour, int minute,
                     int second, int nanoseconds) {
  return Value::Datetime(DatetimeValue::FromYMDHMSAndNanos(
      year, month, day, hour, minute, second, nanoseconds));
}
Value KitchenSink(absl::string_view proto_str) {
  googlesql_test::KitchenSinkPB kitchen_sink_message;
  ABSL_CHECK(google::protobuf::TextFormat::ParseFromString(proto_str,
                                            &kitchen_sink_message));
  return ProtoToValue(KitchenSinkProtoType(), kitchen_sink_message);
}

Value Proto3Timestamp(int64_t seconds, int32_t nanos) {
  google::protobuf::Timestamp proto3_timestamp;
  proto3_timestamp.set_seconds(seconds);
  proto3_timestamp.set_nanos(nanos);

  return ProtoToValue(Proto3TimestampType(), proto3_timestamp);
}

Value Proto3Date(int32_t year, int32_t month, int32_t day) {
  google::type::Date proto3_date;
  proto3_date.set_year(year);
  proto3_date.set_month(month);
  proto3_date.set_day(day);
  return ProtoToValue(Proto3DateType(), proto3_date);
}

Value Proto3LatLng(double latitude, double longitude) {
  google::type::LatLng lat_lng;
  lat_lng.set_latitude(latitude);
  lat_lng.set_longitude(longitude);

  return ProtoToValue(Proto3LatLngType(), lat_lng);
}

Value Proto3TimeOfDay(int32_t hour, int32_t minute, int32_t seconds,
                      int32_t nanos) {
  google::type::TimeOfDay proto3_time;
  proto3_time.set_hours(hour);
  proto3_time.set_minutes(minute);
  proto3_time.set_seconds(seconds);
  proto3_time.set_nanos(nanos);

  return ProtoToValue(Proto3TimeOfDayType(), proto3_time);
}

Value CivilTimeTypesSink(absl::string_view proto_str) {
  googlesql_test::CivilTimeTypesSinkPB civil_time_types_sink_message;
  ABSL_CHECK(google::protobuf::TextFormat::ParseFromString(proto_str,
                                            &civil_time_types_sink_message));
  return ProtoToValue(CivilTimeTypesSinkProtoType(),
                      civil_time_types_sink_message);
}

Value NullableInt(absl::string_view proto_str) {
  googlesql_test::NullableInt nullable_int_message;
  ABSL_CHECK(google::protobuf::TextFormat::ParseFromString(proto_str,
                                            &nullable_int_message));
  return ProtoToValue(NullableIntProtoType(), nullable_int_message);
}

Value ProtoDuration(int64_t seconds, int32_t nanos) {
  google::protobuf::Duration proto_duration;
  proto_duration.set_seconds(seconds);
  proto_duration.set_nanos(nanos);
  return ProtoToValue(ProtoDurationType(), proto_duration);
}

// Each returned row (vector) contains distinct values of a certain type.
std::vector<std::vector<Value>> GetRowsOfValues() {
  const StructType* struct_type = SimpleStructType();  // a: string, b: int32
  const Value struct0 = Value::Struct(struct_type, {String("foo"), Int32(0)});
  const Value struct1 = Value::Struct(struct_type, {String("bar"), Int32(1)});
  const Value array0 = Value::Array(Int64ArrayType(), {Int64(0), Int64(1)});
  const Value array1 = Value::Array(Int64ArrayType(), {Int64(10), Int64(11)});
  const Value enum0 = Value::Enum(TestEnumType(), 1);
  const Value enum1 = Value::Enum(TestEnumType(), 2);

  std::vector<std::vector<Value>> v = {
      {Int32(1), Int32(2)},
      {Int64(1), Int64(2)},
      {Uint32(1), Uint32(2)},
      {Uint64(1), Uint64(2)},
      {True(), False()},
      {Float(1.3), Float(1.5)},
      {Double(1.3), Double(1.5)},
      {String("yay"), String("nay")},
      {Bytes("yay"), Bytes("nay")},
      {Timestamp(0), Timestamp(1)},
      {Date(0), Date(1)},
      {struct0, struct1},
      {array0, array1},
      {enum0, enum1},
      {KitchenSink("int64_key_1: 1 int64_key_2: 2"),
       KitchenSink("int64_key_1: 10 int64_key_2: 20")},
      {NumericFromDouble(3.14), Numeric(-100)},
      {BigNumericFromDouble(3.14), BigNumeric(-100)},
      {Range(Date(11), Date(443)), Range(Date(51), NullDate())},
      {Range(Timestamp(11), Timestamp(443)),
       Range(Timestamp(51), NullTimestamp())},
      {Range(Datetime(DatetimeValue::FromYMDHMSAndMicros(2010, 7, 11, 13, 46,
                                                         23, 0)),
             Datetime(DatetimeValue::FromYMDHMSAndMicros(2050, 3, 5, 21, 18, 27,
                                                         8))),
       Range(Datetime(DatetimeValue::FromYMDHMSAndMicros(2015, 2, 6, 2, 31, 58,
                                                         121)),
             NullDatetime())},
  };
  return v;
}

CivilTimeTestCase::CivilTimeTestCase(
    const std::vector<ValueConstructor>& input,
    const absl::StatusOr<Value>& micros_output,
    const absl::StatusOr<Value>& nanos_output, const Type* output_type,
    const std::set<LanguageFeature>& required_features)
    : input(input),
      micros_output(micros_output),
      nanos_output(nanos_output),
      required_features(required_features) {
  this->output_type = nullptr;
  if (micros_output.status().ok()) {
    this->output_type = micros_output.value().type();
  }
  if (nanos_output.status().ok()) {
    if (this->output_type != nullptr) {
      ABSL_CHECK(this->output_type->Equals(nanos_output.value().type()));
    } else {
      this->output_type = nanos_output.value().type();
    }
  }
  if (output_type != nullptr) {
    if (this->output_type != nullptr) {
      ABSL_CHECK(this->output_type->Equals(output_type));
    } else {
      this->output_type = output_type;
    }
  } else {
    std::string test_info("\ninput: ");
    bool add_comma = false;
    for (const ValueConstructor& input_argument : input) {
      absl::StrAppend(&test_info, (add_comma ? "," : ""),
                      input_argument.get().DebugString());
      add_comma = true;
    }
    absl::StrAppend(
        &test_info, "\nmicros_output: ",
        (micros_output.status().ok()
             ? micros_output.value().DebugString()
             : googlesql::internal::StatusToString(micros_output.status())));
    absl::StrAppend(
        &test_info, "\nnanos_output: ",
        (nanos_output.status().ok()
             ? nanos_output.value().DebugString()
             : googlesql::internal::StatusToString(nanos_output.status())));
    absl::StrAppend(&test_info, "\noutput_type: ",
                    (output_type == nullptr ? "nullptr" :
                     output_type->DebugString()));
    // The constructor must set a non-null output_type for this test case.
    ABSL_CHECK(this->output_type != nullptr)
        << "The test 'output_type' cannot be nullptr.  This can potentially "
        << "happen if both the micros and nanos results are errors, in which "
        << "case the output type must be explicitly passed into the "
        << "constructor as per the constructor contract.\nThis test info:\n"
        << test_info;
  }
}

// Helper to check if the date part enum argument is 'NANOSECONDS'. In that
// case the test case requires the NANOSECONDS feature to be turned on for it
// to compile.
static bool HasNanosecondsPartArgument(const CivilTimeTestCase& test_case) {
  const EnumType* part_enum;
  const google::protobuf::EnumDescriptor* enum_descriptor =
      functions::DateTimestampPart_descriptor();
  GOOGLESQL_CHECK_OK(type_factory()->MakeEnumType(enum_descriptor, &part_enum));

  for (const ValueConstructor& input_argument : test_case.input) {
    if (input_argument.get().type()->Equivalent(part_enum) &&
        input_argument.get().enum_value() == functions::NANOSECOND) {
      return true;
    }
  }
  return false;
}

static void AddTestCasesForCivilTimeAndNanos(
    const CivilTimeTestCase& test_case,
    std::function<void(QueryParamsWithResult)> add_test) {
  auto with_nanos_result =
      QueryParamsWithResult(test_case.input, test_case.nanos_output,
                            test_case.output_type)
          .AddRequiredFeatures(test_case.required_features)
          .AddRequiredFeature(FEATURE_CIVIL_TIME);
  // Some tests are setup with TIMESTAMP_NANOS explicitly required.
  if (googlesql_base::ContainsKey(test_case.required_features, FEATURE_TIMESTAMP_NANOS)) {
    add_test(with_nanos_result);
    return;
  }
  // Otherwise, tests cases with NANOS timestamp part require the nanos feature.
  if (HasNanosecondsPartArgument(test_case)) {
    add_test(with_nanos_result.AddRequiredFeature(FEATURE_TIMESTAMP_NANOS));
    return;
  }
  // There are two paths. If the expected status and value are the same for
  // micros and nanos (e.g. NULL int64 + OK) then we don't want to add
  // the nanos feature as required or as prohibited. The same test applies
  // reguardless of that feature state.  If they are different, then we need
  // to generate two tests. One with the feature required, the other with the
  // feature prohibited.
  bool requires_feature = false;
  if (test_case.micros_output.status() != test_case.nanos_output.status()) {
    requires_feature = true;
  } else if (test_case.micros_output.ok() && test_case.nanos_output.ok()) {
    requires_feature =
        test_case.micros_output.value() != test_case.nanos_output.value();
  }
  if (!requires_feature) {
    add_test(with_nanos_result);
    return;
  }
  auto with_micros_result =
      QueryParamsWithResult(test_case.input, test_case.micros_output,
                            test_case.output_type)
          .AddRequiredFeatures(test_case.required_features)
          .AddRequiredFeature(FEATURE_CIVIL_TIME)
          .AddProhibitedFeature(FEATURE_TIMESTAMP_NANOS);
  add_test(with_micros_result);
  with_nanos_result.AddRequiredFeature(FEATURE_TIMESTAMP_NANOS);
  add_test(with_nanos_result);
}

void AddTestCaseWithWrappedResultForCivilTimeAndNanos(
    const CivilTimeTestCase& test_case,
    std::vector<QueryParamsWithResult>* results) {
  AddTestCasesForCivilTimeAndNanos(
      test_case,
      [results](QueryParamsWithResult result) { results->push_back(result); });
}

void AddTestCaseWithWrappedResultForCivilTimeAndNanos(
    const CivilTimeTestCase& test_case, absl::string_view function_name,
    std::vector<FunctionTestCall>* results) {
  AddTestCasesForCivilTimeAndNanos(
      test_case, [results, function_name](QueryParamsWithResult result) {
        results->emplace_back(function_name, result);
      });
}

std::string EscapeKey(bool sql_standard_mode, absl::string_view key) {
  if (sql_standard_mode) {
    return absl::StrCat(".\"", key, "\"");
  } else {
    return absl::StrCat("['", key, "']");
  }
}

Value StringToBytes(const Value& value) {
  ABSL_CHECK_EQ(value.type_kind(), TYPE_STRING);
  if (value.is_null()) {
    return Value::NullBytes();
  } else {
    return Value::Bytes(value.string_value());
  }
}

}  // namespace googlesql
