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

// This file may be included at the top of other *_test.cc files to
// include the useful googlesql type/value tools in the tests namespace.
//
// Non test use is discouraged.
// Inclusion in a *.h file is forbidden by the styleguide.
//
// Use with caution.

#include "googlesql/testing/test_value.h"

// NOLINTBEGIN(google-global-names-in-headers)
using googlesql::test_values::Array;
using googlesql::test_values::GraphEdge;
using googlesql::test_values::GraphNode;
using googlesql::test_values::kIgnoresOrder;
using googlesql::test_values::kPreservesOrder;
using googlesql::test_values::MakeArrayType;
using googlesql::test_values::MakeGraphElementType;
using googlesql::test_values::MakeMapType;
using googlesql::test_values::MakeRangeType;
using googlesql::test_values::MakeStructType;
using googlesql::test_values::Map;
using googlesql::test_values::OrderPreservationKind;
using googlesql::test_values::Range;
using googlesql::test_values::Struct;
using googlesql::test_values::StructArray;
using googlesql::test_values::DynamicGraphEdge;
using googlesql::test_values::DynamicGraphNode;
using googlesql::test_values::MakeDynamicGraphElementType;

using googlesql::types::BigNumericType;
using googlesql::types::BoolType;
using googlesql::types::BytesType;
using googlesql::types::DatetimeType;
using googlesql::types::DateType;
using googlesql::types::DoubleType;
using googlesql::types::EmptyStructType;
using googlesql::types::FloatType;
using googlesql::types::GeographyType;
using googlesql::types::Int32Type;
using googlesql::types::Int64Type;
using googlesql::types::IntervalType;
using googlesql::types::JsonType;
using googlesql::types::NumericType;
using googlesql::types::UuidType;
using googlesql::types::StringType;
using googlesql::types::TimestampType;
using googlesql::types::TimeType;
using googlesql::types::Uint32Type;
using googlesql::types::Uint64Type;

using googlesql::types::BigNumericArrayType;
using googlesql::types::BoolArrayType;
using googlesql::types::BytesArrayType;
using googlesql::types::DateArrayType;
using googlesql::types::DatetimeArrayType;
using googlesql::types::DoubleArrayType;
using googlesql::types::FloatArrayType;
using googlesql::types::GeographyArrayType;
using googlesql::types::Int32ArrayType;
using googlesql::types::Int64ArrayType;
using googlesql::types::IntervalArrayType;
using googlesql::types::JsonArrayType;
using googlesql::types::NumericArrayType;
using googlesql::types::StringArrayType;
using googlesql::types::TimeArrayType;
using googlesql::types::TimestampArrayType;
using googlesql::types::Uint32ArrayType;
using googlesql::types::Uint64ArrayType;

using googlesql::values::Bool;
using googlesql::values::Bytes;
using googlesql::values::Date;
using googlesql::values::Datetime;
using googlesql::values::Double;
using googlesql::values::Enum;
using googlesql::values::Float;
using googlesql::values::Int32;
using googlesql::values::Int64;
using googlesql::values::Interval;
using googlesql::values::Json;
using googlesql::values::Numeric;
using googlesql::values::Uuid;
inline googlesql::Value NumericFromDouble(double v) {
  return Numeric(googlesql::NumericValue::FromDouble(v).value());
}
using googlesql::values::BigNumeric;
inline googlesql::Value BigNumericFromDouble(double v) {
  return BigNumeric(googlesql::BigNumericValue::FromDouble(v).value());
}
using googlesql::values::Json;
using googlesql::values::Proto;
using googlesql::values::String;
using googlesql::values::Time;
using googlesql::values::TimestampFromUnixMicros;
using googlesql::values::Uint32;
using googlesql::values::Uint64;

using googlesql::values::False;
using googlesql::values::True;

using googlesql::values::Null;
using googlesql::values::NullBigNumeric;
using googlesql::values::NullBool;
using googlesql::values::NullBytes;
using googlesql::values::NullDate;
using googlesql::values::NullDatetime;
using googlesql::values::NullDouble;
using googlesql::values::NullFloat;
using googlesql::values::NullGeography;
using googlesql::values::NullInt32;
using googlesql::values::NullInt64;
using googlesql::values::NullInterval;
using googlesql::values::NullJson;
using googlesql::values::NullNumeric;
using googlesql::values::NullUuid;
using googlesql::values::NullString;
using googlesql::values::NullTime;
using googlesql::values::NullTimestamp;
using googlesql::values::NullUint32;
using googlesql::values::NullUint64;

using googlesql::values::BigNumericArray;
using googlesql::values::BoolArray;
using googlesql::values::BytesArray;
using googlesql::values::DoubleArray;
using googlesql::values::FloatArray;
using googlesql::values::Int32Array;
using googlesql::values::Int64Array;
using googlesql::values::JsonArray;
using googlesql::values::NumericArray;
using googlesql::values::StringArray;
using googlesql::values::Uint32Array;
using googlesql::values::Uint64Array;

using googlesql::values::EmptyArray;
// NOLINTEND(google-global-names-in-headers)
