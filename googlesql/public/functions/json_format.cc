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

#include "googlesql/public/functions/json_format.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "googlesql/common/errors.h"
#include "googlesql/common/json_util.h"
#include "googlesql/common/string_util.h"
#include "googlesql/public/civil_time.h"
#include "googlesql/public/functions/date_time_util.h"
#include "googlesql/public/interval_value.h"
#include "googlesql/public/json_value.h"
#include "googlesql/public/numeric_value.h"
#include "googlesql/public/timestamp_picos_value.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/uuid_value.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/strings/substitute.h"
#include "absl/time/time.h"
#include "google/protobuf/dynamic_message.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace functions {
namespace {

template <typename FloatType>
void FloatToString(FloatType value, std::string* output);

template <>
void FloatToString(float value, std::string* output) {
  output->append(RoundTripFloatToString(value));
}

template <>
void FloatToString(double value, std::string* output) {
  output->append(RoundTripDoubleToString(value));
}

// Implementation of JsonFromNumericOrBool for float and double.
template <typename FloatType>
void JsonFromFloatImpl(FloatType value, std::string* output,
                       bool canonicalize_zero) {
  if (canonicalize_zero && value == -0.0) {
    FloatToString<FloatType>(0.0, output);
  } else if (std::isfinite(value)) {
    // Common case: we have to call the correct version of SimpleFtoa or
    // SimpleDtoa. SimpleFtoa accepts double but truncates the precision.
    FloatToString(value, output);
  } else if (std::isnan(value)) {
    output->append("\"NaN\"");
  } else {
    // !isfinite implies isnan or isinf.
    ABSL_DCHECK(std::isinf(value)) << value;
    if (value > 0) {
      output->append("\"Infinity\"");
    } else {
      output->append("\"-Infinity\"");
    }
  }
}

// Cache the format strings for timestamp, datetime, and time to avoid
// continually recreating them. The formatting functions expect a const string&
// rather than an absl::string_view.

// Returns the timestamp format string with the appropriate number of trailing
// zeros in the fractional seconds depending on the scale.
const std::string& TimestampFormatForScale(TimestampScale scale) {
  switch (scale) {
    case kSeconds: {
      static const std::string* kSecondsFormat =
          new std::string("%E4Y-%m-%dT%H:%M:%SZ");
      return *kSecondsFormat;
    }
    case kMilliseconds: {
      static const std::string* kMillisecondsFormat =
          new std::string("%E4Y-%m-%dT%H:%M:%E3SZ");
      return *kMillisecondsFormat;
    }
    case kMicroseconds: {
      static const std::string* kMicrosecondsFormat =
          new std::string("%E4Y-%m-%dT%H:%M:%E6SZ");
      return *kMicrosecondsFormat;
    }
    case kNanoseconds: {
      static const std::string* kNanoSecondsFormat =
          new std::string("%E4Y-%m-%dT%H:%M:%E9SZ");
      return *kNanoSecondsFormat;
    }
    case kPicoseconds: {
      static const std::string* kPicoSecondsFormat =
          new std::string("%E4Y-%m-%dT%H:%M:%E12SZ");
      return *kPicoSecondsFormat;
    }
  }
}

// Returns the datetime format string with the appropriate number of trailing
// zeros in the fractional seconds depending on the scale.
std::string DatetimeFormatForScale(TimestampScale scale) {
  switch (scale) {
    case kSeconds: {
      static const std::string* kSecondsFormat =
          new std::string("%E4Y-%m-%dT%H:%M:%S");
      return *kSecondsFormat;
    }
    case kMilliseconds: {
      static const std::string* kMillisecondsFormat =
          new std::string("%E4Y-%m-%dT%H:%M:%E3S");
      return *kMillisecondsFormat;
    }
    case kMicroseconds: {
      static const std::string* kMicrosecondsFormat =
          new std::string("%E4Y-%m-%dT%H:%M:%E6S");
      return *kMicrosecondsFormat;
    }
    case kNanoseconds: {
      static const std::string* kNanoSecondsFormat =
          new std::string("%E4Y-%m-%dT%H:%M:%E9S");
      return *kNanoSecondsFormat;
    }
    case kPicoseconds: {
      static const std::string* kPicoSecondsFormat =
          new std::string("%E4Y-%m-%dT%H:%M:%E12S");
      return *kPicoSecondsFormat;
    }
  }
}

// Returns the time format string with the appropriate number of trailing zeros
// in the fractional seconds depending on the scale.
std::string TimeFormatForScale(TimestampScale scale) {
  switch (scale) {
    case kSeconds: {
      static const std::string* kSecondsFormat = new std::string("%H:%M:%S");
      return *kSecondsFormat;
    }
    case kMilliseconds: {
      static const std::string* kMillisecondsFormat =
          new std::string("%H:%M:%E3S");
      return *kMillisecondsFormat;
    }
    case kMicroseconds: {
      static const std::string* kMicrosecondsFormat =
          new std::string("%H:%M:%E6S");
      return *kMicrosecondsFormat;
    }
    case kNanoseconds: {
      static const std::string* kNanoSecondsFormat =
          new std::string("%H:%M:%E9S");
      return *kNanoSecondsFormat;
    }
    case kPicoseconds: {
      static const std::string* kPicoSecondsFormat =
          new std::string("%H:%M:%E12S");
      return *kPicoSecondsFormat;
    }
  }
}

}  // namespace

void JsonFromNumericOrBool(bool value, std::string* output) {
  absl::StrAppend(output, value ? "true" : "false");
}

void JsonFromNumericOrBool(int32_t value, std::string* output) {
  absl::StrAppend(output, value);
}

void JsonFromNumericOrBool(uint32_t value, std::string* output) {
  absl::StrAppend(output, value);
}

void JsonFromNumericOrBool(int64_t value, std::string* output) {
  const bool quote_number = value < kMinLosslessInt64ValueForJson ||
                            value > kMaxLosslessInt64ValueForJson;
  if (quote_number) {
    absl::StrAppend(output, "\"", value, "\"");
  } else {
    absl::StrAppend(output, value);
  }
}

void JsonFromNumericOrBool(uint64_t value, std::string* output) {
  const bool quote_number = value > kMaxLosslessInt64ValueForJson;
  if (quote_number) {
    absl::StrAppend(output, "\"", value, "\"");
  } else {
    absl::StrAppend(output, value);
  }
}

void JsonFromNumericOrBool(float value, std::string* output,
                           bool canonicalize_zero) {
  JsonFromFloatImpl(value, output, canonicalize_zero);
}

void JsonFromNumericOrBool(double value, std::string* output,
                           bool canonicalize_zero) {
  JsonFromFloatImpl(value, output, canonicalize_zero);
}

void JsonFromNumericOrBool(NumericValue value, std::string* output) {
  if (value < NumericValue(kMinLosslessInt64ValueForJson) ||
      value > NumericValue(kMaxLosslessInt64ValueForJson)) {
    output->push_back('\"');
    value.AppendToString(output);
    output->push_back('\"');
  } else {
    size_t old_len = output->length();
    value.AppendToString(output);
    if (output->find('.', old_len) != std::string::npos) {
      output->insert(old_len, 1, '\"');
      output->push_back('\"');
    }
  }
}

void JsonFromNumericOrBool(const BigNumericValue& value, std::string* output) {
  // If the value does not have a fractional part and round-trips through double
  // losslessly (meaning it is in the range of
  // [kMinLosslessInt64ValueForJson, kMaxLosslessInt64ValueForJson]), then we
  // can encode it without quotes.
  if (value < BigNumericValue(kMinLosslessInt64ValueForJson) ||
      value > BigNumericValue(kMaxLosslessInt64ValueForJson)) {
    output->push_back('\"');
    value.AppendToString(output);
    output->push_back('\"');
  } else {
    size_t old_len = output->length();
    value.AppendToString(output);
    if (output->find('.', old_len) != std::string::npos) {
      output->insert(old_len, 1, '\"');
      output->push_back('\"');
    }
  }
}

void JsonFromString(absl::string_view value, std::string* output) {
  // Escape and append the string (in quotes) to output. Using PROTO3 for the
  // flags sets the escaping settings appropriately.
  std::string json;
  JsonEscapeString(value, &json);
  output->append(json);
}

void JsonFromBytes(absl::string_view value, std::string* output,
                   bool quote_output_string) {
  std::string tmp;
  absl::Base64Escape(value, &tmp);
  if (quote_output_string) {
    absl::StrAppend(output, "\"", tmp, "\"");
  } else {
    absl::StrAppend(output, tmp);
  }
}

absl::Status JsonFromTimestamp(absl::Time value, std::string* output,
                               bool quote_output_string) {
  TimestampScale scale = TimestampScale::kNanoseconds;
  NarrowTimestampScaleIfPossible(value, &scale);

  std::string timestamp_string;
  GOOGLESQL_RETURN_IF_ERROR(FormatTimestampToString(TimestampFormatForScale(scale), value,
                                          absl::UTCTimeZone(),
                                          &timestamp_string));

  if (quote_output_string) {
    JsonFromString(timestamp_string, output);
  } else {
    absl::StrAppend(output, timestamp_string);
  }
  return absl::OkStatus();
}

absl::Status JsonFromTimestamp(TimestampPicosValue value, std::string* output,
                               bool quote_output_string) {
  TimestampScale scale = NarrowTimestampScaleIfPossible(value.ToPicoTime());
  std::string timestamp_string;
  GOOGLESQL_RETURN_IF_ERROR(FormatTimestampToString(
      TimestampFormatForScale(scale), value.ToPicoTime(), absl::UTCTimeZone(),
      &timestamp_string));
  if (quote_output_string) {
    JsonFromString(timestamp_string, output);
  } else {
    absl::StrAppend(output, timestamp_string);
  }
  return absl::OkStatus();
}

absl::Status JsonFromDatetime(const DatetimeValue& value, std::string* output,
                              bool quote_output_string) {
  TimestampScale scale = TimestampScale::kNanoseconds;
  NarrowTimestampScaleIfPossible(absl::FromUnixNanos(value.Nanoseconds()),
                                 &scale);

  std::string datetime_string;
  GOOGLESQL_RETURN_IF_ERROR(FormatDatetimeToString(DatetimeFormatForScale(scale), value,
                                         &datetime_string));
  if (quote_output_string) {
    JsonFromString(datetime_string, output);
  } else {
    absl::StrAppend(output, datetime_string);
  }
  return absl::OkStatus();
}

absl::Status JsonFromDate(int32_t value, std::string* output,
                          bool quote_output_string) {
  std::string date_string;
  GOOGLESQL_RETURN_IF_ERROR(ConvertDateToString(value, &date_string));
  if (quote_output_string) {
    JsonFromString(date_string, output);
  } else {
    absl::StrAppend(output, date_string);
  }
  return absl::OkStatus();
}

absl::Status JsonFromTime(const TimeValue& value, std::string* output,
                          bool quote_output_string) {
  TimestampScale scale = TimestampScale::kNanoseconds;
  NarrowTimestampScaleIfPossible(absl::FromUnixNanos(value.Nanoseconds()),
                                 &scale);

  std::string time_string;
  GOOGLESQL_RETURN_IF_ERROR(
      FormatTimeToString(TimeFormatForScale(scale), value, &time_string));
  if (quote_output_string) {
    JsonFromString(time_string, output);
  } else {
    absl::StrAppend(output, time_string);
  }
  return absl::OkStatus();
}

absl::Status JsonFromInterval(const IntervalValue& value, std::string* output) {
  JsonFromString(value.ToISO8601(), output);
  return absl::OkStatus();
}

void JsonFromJson(JSONValueConstRef value, JsonPrettyPrinter* pretty_printer,
                  std::string* output) {
  if (!pretty_printer->pretty_print()) {
    absl::StrAppend(output, value.ToString());
    return;
  }
  std::string json_string = value.Format();
  std::vector<absl::string_view> lines = absl::StrSplit(json_string, '\n');

  bool first_line = true;
  for (absl::string_view line : lines) {
    if (!first_line) {
      pretty_printer->AppendNewlineAndIndent(output);
    }
    first_line = false;
    absl::StrAppend(output, line);
  }
}

absl::Status JsonFromValue(const Value& value,
                           JsonPrettyPrinter* pretty_printer,
                           std::string* output,
                           const JSONParsingOptions& json_parsing_options) {
  if (value.is_null()) {
    output->append("null");
    return absl::OkStatus();
  }

  // Check the output size before and after appending data. We want to return
  // an error early for individual struct fields, for instance, as well as
  // after the fact if we detect that an appended string is too large.
  GOOGLESQL_RETURN_IF_ERROR(pretty_printer->CheckOutputSize(*output));

  switch (value.type_kind()) {
    case TYPE_BOOL:
      JsonFromNumericOrBool(value.bool_value(), output);
      break;
    case TYPE_INT32:
      JsonFromNumericOrBool(value.int32_value(), output);
      break;
    case TYPE_UINT32:
      JsonFromNumericOrBool(value.uint32_value(), output);
      break;
    case TYPE_INT64:
      JsonFromNumericOrBool(value.int64_value(), output);
      break;
    case TYPE_UINT64:
      JsonFromNumericOrBool(value.uint64_value(), output);
      break;
    case TYPE_FLOAT:
      JsonFromNumericOrBool(value.float_value(), output,
                            json_parsing_options.canonicalize_zero);
      break;
    case TYPE_DOUBLE:
      JsonFromNumericOrBool(value.double_value(), output,
                            json_parsing_options.canonicalize_zero);
      break;
    case TYPE_NUMERIC:
      JsonFromNumericOrBool(value.numeric_value(), output);
      break;
    case TYPE_BIGNUMERIC:
      JsonFromNumericOrBool(value.bignumeric_value(), output);
      break;
    case TYPE_STRING:
      JsonFromString(value.string_value(), output);
      break;
    case TYPE_BYTES:
      JsonFromBytes(value.bytes_value(), output);
      break;
    case TYPE_TIMESTAMP:
      GOOGLESQL_RETURN_IF_ERROR(JsonFromTimestamp(value.ToUnixPicos(), output));
      break;
    case TYPE_DATE:
      GOOGLESQL_RETURN_IF_ERROR(JsonFromDate(value.date_value(), output));
      break;
    case TYPE_DATETIME:
      GOOGLESQL_RETURN_IF_ERROR(JsonFromDatetime(value.datetime_value(), output));
      break;
    case TYPE_TIME:
      GOOGLESQL_RETURN_IF_ERROR(JsonFromTime(value.time_value(), output));
      break;
    case TYPE_INTERVAL:
      GOOGLESQL_RETURN_IF_ERROR(JsonFromInterval(value.interval_value(), output));
      break;
    case TYPE_UUID: {
      GOOGLESQL_ASSIGN_OR_RETURN(const UuidValue uuid_value, value.uuid_value());
      JsonFromString(uuid_value.ToString(), output);
      break;
    }
    case TYPE_JSON:
      if (value.is_unparsed_json()) {
        auto input_json = JSONValue::ParseJSONString(
            value.json_value_unparsed(), json_parsing_options);
        if (!input_json.ok()) {
          return MakeEvalError() << input_json.status().message();
        }
        JsonFromJson(input_json->GetConstRef(), pretty_printer, output);
      } else {
        GOOGLESQL_RET_CHECK(value.is_validated_json());
        JsonFromJson(value.json_value(), pretty_printer, output);
      }
      break;
    case TYPE_STRUCT: {
      if (value.fields().empty()) {
        absl::StrAppend(output, "{}");
        break;
      }

      output->push_back('{');
      pretty_printer->IncreaseIndent();

      int field_index = 0;
      const StructType* struct_type = value.type()->AsStruct();
      for (const auto& field_value : value.fields()) {
        if (field_index != 0) {
          output->push_back(',');
        }
        pretty_printer->AppendNewlineAndIndent(output);
        JsonFromString(struct_type->field(field_index).name, output);
        output->push_back(':');
        pretty_printer->AppendSeparator(output);
        GOOGLESQL_RETURN_IF_ERROR(JsonFromValue(field_value, pretty_printer, output,
                                      json_parsing_options));

        ++field_index;
      }

      pretty_printer->DecreaseIndent();
      pretty_printer->AppendNewlineAndIndent(output);
      output->push_back('}');
      break;
    }
    case TYPE_ARRAY: {
      if (value.elements().empty()) {
        absl::StrAppend(output, "[]");
        break;
      }

      output->push_back('[');
      pretty_printer->IncreaseIndent();

      bool first_element = true;
      for (const auto& element_value : value.elements()) {
        if (!first_element) {
          output->push_back(',');
        }
        first_element = false;
        pretty_printer->AppendNewlineAndIndent(output);
        GOOGLESQL_RETURN_IF_ERROR(JsonFromValue(element_value, pretty_printer, output,
                                      json_parsing_options));
      }

      pretty_printer->DecreaseIndent();
      pretty_printer->AppendNewlineAndIndent(output);
      output->push_back(']');
      break;
    }
    case TYPE_ENUM:
      if (absl::StatusOr<absl::string_view> name = value.EnumName();
          name.ok()) {
        JsonFromString(*name, output);
      } else {
        JsonFromNumericOrBool(value.enum_value(), output);
      }
      break;
    case TYPE_RANGE: {
      // RANGE is formatted the same way as STRUCT: (broken link)
      output->push_back('{');
      pretty_printer->IncreaseIndent();

      pretty_printer->AppendNewlineAndIndent(output);
      JsonFromString("start", output);
      output->push_back(':');
      pretty_printer->AppendSeparator(output);
      GOOGLESQL_RETURN_IF_ERROR(JsonFromValue(value.start(), pretty_printer, output,
                                    json_parsing_options));

      output->push_back(',');
      pretty_printer->AppendNewlineAndIndent(output);
      JsonFromString("end", output);
      output->push_back(':');
      pretty_printer->AppendSeparator(output);
      GOOGLESQL_RETURN_IF_ERROR(JsonFromValue(value.end(), pretty_printer, output,
                                    json_parsing_options));

      pretty_printer->DecreaseIndent();
      pretty_printer->AppendNewlineAndIndent(output);
      output->push_back('}');
      break;
    }
    default:
      return ::googlesql_base::UnimplementedErrorBuilder()
             << "Unsupported argument type "
             << value.type()->ShortTypeName(pretty_printer->product_mode())
             << " for TO_JSON_STRING.";
  }

  GOOGLESQL_RETURN_IF_ERROR(pretty_printer->CheckOutputSize(*output));

  return absl::OkStatus();
}

}  // namespace functions
}  // namespace googlesql
