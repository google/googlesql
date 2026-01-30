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

// Tests of aggregate function code.

#include <cstdint>
#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/numeric_value.h"
#include "googlesql/public/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/reference_impl/evaluation.h"
#include "googlesql/reference_impl/function.h"
#include "googlesql/reference_impl/operator.h"
#include "googlesql/reference_impl/test_relational_op.h"
#include "googlesql/reference_impl/tuple.h"
#include "googlesql/reference_impl/tuple_test_util.h"
#include "googlesql/reference_impl/variable_id.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "googlesql/testing/test_value.h"
#include "googlesql/testing/using_test_value.cc"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_join.h"
#include "absl/types/span.h"
#include "googlesql/base/status_macros.h"

using ::testing::_;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::Not;
using ::testing::TestWithParam;
using ::testing::ValuesIn;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;

namespace googlesql {
namespace {

// For readability.
std::vector<const TupleSchema*> EmptyParamsSchemas() { return {}; }
std::vector<const TupleData*> EmptyParams() { return {}; }

EvaluationOptions GetScramblingEvaluationOptions() {
  EvaluationOptions options;
  options.scramble_undefined_orderings = true;
  return options;
}

EvaluationOptions GetIntermediateMemoryEvaluationOptions(int64_t total_bytes) {
  EvaluationOptions options;
  options.max_intermediate_byte_size = total_bytes;
  return options;
}

const bool kNonDet = false;

struct AggregateFunctionTemplate {
 public:
  AggregateFunctionTemplate(FunctionKind kind, const std::vector<Value>& values,
                            const Value& result, bool is_deterministic = true)
      : kind(kind),
        values(values),
        result(result),
        is_deterministic(is_deterministic) {}
  // For conciseness, we don't specify argument type explicitly (for empty
  // inputs) but derive it from the result type.
  const Type* argument_type() const {
    if (!values.empty()) {
      return values[0].type();
    }
    switch (kind) {
      case FunctionKind::kCount:
        return types::Int64Type();  // count(*)
      case FunctionKind::kArrayAgg:
        return result.type()->AsArray()->element_type();
      default:
        return result.type();
    }
  }
  FunctionKind kind;
  std::vector<Value> values;
  Value result;
  bool is_deterministic;
};

static std::vector<AggregateFunctionTemplate> ConcatTemplates(
    absl::Span<const std::vector<AggregateFunctionTemplate>>
        aggregate_vectors) {
  std::vector<AggregateFunctionTemplate> result;
  for (const auto& v : aggregate_vectors) {
    result.insert(result.end(), v.begin(), v.end());
  }
  return result;
}

std::ostream& operator<<(std::ostream& out,
                         const AggregateFunctionTemplate& t) {
  TypeFactory type_factory;

  std::vector<std::unique_ptr<ValueExpr>> args;
  args.push_back(DerefExpr::Create(VariableId("x"), t.argument_type()).value());

  auto op = AggregateArg::Create(VariableId("agg"),
                                 std::make_unique<BuiltinAggregateFunction>(
                                     t.kind, t.result.type(),
                                     /*num_input_fields=*/1, t.argument_type()),
                                 std::move(args), AggregateArg::kAll)
                .value();
  std::vector<std::string> strs;
  for (const auto& a : t.values) {
    strs.push_back(a.DebugString(true));
  }

  return out << op->DebugString() << " where x one of {"
             << absl::StrJoin(strs, ", ") << "}"
             << " == " << t.result.DebugString(true);
}

static std::vector<AggregateFunctionTemplate> MinAggregateFunctionTemplates() {
  const EnumType* enum_type;
  GOOGLESQL_CHECK_OK(test_values::static_type_factory()->MakeEnumType(
      googlesql_test::TestEnum_descriptor(), &enum_type));
  return {
    // Min(Bool)
    {FunctionKind::kMin, {Bool(false), Bool(true)}, Bool(false)},
    {FunctionKind::kMin, {Bool(true), Bool(false)}, Bool(false)},
    {FunctionKind::kMin, {Bool(true), NullBool()}, Bool(true)},
    {FunctionKind::kMin, {Bool(false), NullBool()}, Bool(false)},
    {FunctionKind::kMin, {NullBool()}, NullBool()},
    {FunctionKind::kMin, {}, NullBool()},
    // Min(Int32)
    {FunctionKind::kMin, {Int32(1), Int32(2)}, Int32(1)},
    {FunctionKind::kMin, {Int32(2), Int32(1)}, Int32(1)},
    {FunctionKind::kMin, {Int32(1), NullInt32()}, Int32(1)},
    {FunctionKind::kMin, {NullInt32()}, NullInt32()},
    {FunctionKind::kMin, {}, NullInt32()},
    // Min(Uint32)
    {FunctionKind::kMin, {Uint32(1), Uint32(2)}, Uint32(1)},
    {FunctionKind::kMin, {Uint32(2), Uint32(1)}, Uint32(1)},
    {FunctionKind::kMin, {Uint32(1), NullUint32()}, Uint32(1)},
    {FunctionKind::kMin, {NullUint32()}, NullUint32()},
    {FunctionKind::kMin, {}, NullUint32()},
    // Min(Int64)
    {FunctionKind::kMin, {Int64(1), Int64(2)}, Int64(1)},
    {FunctionKind::kMin, {Int64(2), Int64(1)}, Int64(1)},
    {FunctionKind::kMin, {Int64(1), NullInt64()}, Int64(1)},
    {FunctionKind::kMin, {NullInt64()}, NullInt64()},
    {FunctionKind::kMin, {}, NullInt64()},
    // Min(Uint64)
    {FunctionKind::kMin, {Uint64(1), Uint64(2)}, Uint64(1)},
    {FunctionKind::kMin, {Uint64(2), Uint64(1)}, Uint64(1)},
    {FunctionKind::kMin, {Uint64(1), NullUint64()}, Uint64(1)},
    {FunctionKind::kMin, {NullUint64()}, NullUint64()},
    {FunctionKind::kMin, {}, NullUint64()},
    // Min(Float)
    {FunctionKind::kMin, {Float(1.5), Float(2.5)}, Float(1.5)},
    {FunctionKind::kMin, {Float(2.5), Float(1.5)}, Float(1.5)},
    {FunctionKind::kMin, {Float(-1.5), Float(-2.5)}, Float(-2.5)},
    {FunctionKind::kMin, {Float(-2.5), Float(-1.5)}, Float(-2.5)},
    {FunctionKind::kMin, {Float(1.5), NullFloat()},
        Float(1.5)},
    {FunctionKind::kMin, {NullFloat()}, NullFloat()},
    {FunctionKind::kMin, {}, NullFloat()},
    // Min(Double)
    {FunctionKind::kMin, {Double(1.5), Double(2.5)}, Double(1.5)},
    {FunctionKind::kMin, {Double(2.5), Double(1.5)}, Double(1.5)},
    {FunctionKind::kMin, {Double(-1.5), Double(-2.5)}, Double(-2.5)},
    {FunctionKind::kMin, {Double(-2.5), Double(-1.5)}, Double(-2.5)},
    {FunctionKind::kMin, {Double(1.5), NullDouble()},
        Double(1.5)},
    {FunctionKind::kMin, {NullDouble()}, NullDouble()},
    {FunctionKind::kMin, {}, NullDouble()},
    // Min(Numeric)
    {FunctionKind::kMin, {Numeric(1), Numeric(2)}, Numeric(1)},
    {FunctionKind::kMin, {Numeric(2), Numeric(1)}, Numeric(1)},
    {FunctionKind::kMin, {Numeric(1), NullNumeric()}, Numeric(1)},
    {FunctionKind::kMin, {NullNumeric()}, NullNumeric()},
    {FunctionKind::kMin, {}, NullNumeric()},
    // Min(String)
    {FunctionKind::kMin, {String("a"), String("b")}, String("a")},
    {FunctionKind::kMin, {String("b"), String("a")}, String("a")},
    {FunctionKind::kMin, {String("a"), NullString()},
        String("a")},
    {FunctionKind::kMin, {NullString()}, NullString()},
    {FunctionKind::kMin, {}, NullString()},
    // Min(Bytes)
    {FunctionKind::kMin, {Bytes("a"), Bytes("b")}, Bytes("a")},
    {FunctionKind::kMin, {Bytes("b"), Bytes("a")}, Bytes("a")},
    {FunctionKind::kMin, {Bytes("a"), NullBytes()},
        Bytes("a")},
    {FunctionKind::kMin, {NullBytes()}, NullBytes()},
    {FunctionKind::kMin, {}, NullBytes()},
    // Min(Date)
    {FunctionKind::kMin, {Date(1), Date(2)}, Date(1)},
    {FunctionKind::kMin, {Date(2), Date(1)}, Date(1)},
    {FunctionKind::kMin, {Date(1), NullDate()}, Date(1)},
    {FunctionKind::kMin, {NullDate()}, NullDate()},
    {FunctionKind::kMin, {}, NullDate()},
    // Min(Timestamp)
    {FunctionKind::kMin, {TimestampFromUnixMicros(1),
       TimestampFromUnixMicros(2)}, TimestampFromUnixMicros(1)},
    {FunctionKind::kMin, {TimestampFromUnixMicros(2),
       TimestampFromUnixMicros(1)}, TimestampFromUnixMicros(1)},
    {FunctionKind::kMin, {TimestampFromUnixMicros(1),
       NullTimestamp()}, TimestampFromUnixMicros(1)},
    {FunctionKind::kMin, {NullTimestamp()}, NullTimestamp()},
    {FunctionKind::kMin, {}, NullTimestamp()},
     // Min(Enum)
    {FunctionKind::kMin, {Enum(enum_type, 1), Enum(enum_type, 2)},
          Enum(enum_type, 1)},
    {FunctionKind::kMin, {Enum(enum_type, 2), Enum(enum_type, 1)},
          Enum(enum_type, 1)},
    {FunctionKind::kMin, {Enum(enum_type, 1), Null(enum_type)},
          Enum(enum_type, 1)},
    {FunctionKind::kMin, {Null(enum_type)}, Null(enum_type)},
    {FunctionKind::kMin, {}, Null(enum_type)},
  };
}

static std::vector<AggregateFunctionTemplate> MaxAggregateFunctionTemplates() {
  const EnumType* enum_type;
  GOOGLESQL_CHECK_OK(test_values::static_type_factory()->MakeEnumType(
      googlesql_test::TestEnum_descriptor(), &enum_type));
  return {
    // Max(Bool)
    {FunctionKind::kMax, {Bool(false), Bool(true)}, Bool(true)},
    {FunctionKind::kMax, {Bool(true), Bool(false)}, Bool(true)},
    {FunctionKind::kMax, {Bool(true), NullBool()}, Bool(true)},
    {FunctionKind::kMax, {Bool(false), NullBool()}, Bool(false)},
    {FunctionKind::kMax, {NullBool()}, NullBool()},
    {FunctionKind::kMax, {}, NullBool()},
    // Max(Int32)
    {FunctionKind::kMax, {Int32(1), Int32(2)}, Int32(2)},
    {FunctionKind::kMax, {Int32(2), Int32(1)}, Int32(2)},
    {FunctionKind::kMax, {Int32(1), NullInt32()}, Int32(1)},
    {FunctionKind::kMax, {NullInt32()}, NullInt32()},
    {FunctionKind::kMax, {}, NullInt32()},
    // Max(Uint32)
    {FunctionKind::kMax, {Uint32(1), Uint32(2)}, Uint32(2)},
    {FunctionKind::kMax, {Uint32(2), Uint32(1)}, Uint32(2)},
    {FunctionKind::kMax, {Uint32(1), NullUint32()}, Uint32(1)},
    {FunctionKind::kMax, {NullUint32()}, NullUint32()},
    {FunctionKind::kMax, {}, NullUint32()},
    // Max(Int64)
    {FunctionKind::kMax, {Int64(1), Int64(2)}, Int64(2)},
    {FunctionKind::kMax, {Int64(2), Int64(1)}, Int64(2)},
    {FunctionKind::kMax, {Int64(1), NullInt64()}, Int64(1)},
    {FunctionKind::kMax, {NullInt64()}, NullInt64()},
    {FunctionKind::kMax, {}, NullInt64()},
    // Max(Uint64)
    {FunctionKind::kMax, {Uint64(1), Uint64(2)}, Uint64(2)},
    {FunctionKind::kMax, {Uint64(2), Uint64(1)}, Uint64(2)},
    {FunctionKind::kMax, {Uint64(1), NullUint64()}, Uint64(1)},
    {FunctionKind::kMax, {NullUint64()}, NullUint64()},
    {FunctionKind::kMax, {}, NullUint64()},
    // Max(Float)
    {FunctionKind::kMax, {Float(1.5), Float(2.5)}, Float(2.5)},
    {FunctionKind::kMax, {Float(2.5), Float(1.5)}, Float(2.5)},
    {FunctionKind::kMax, {Float(1.5), NullFloat()},
        Float(1.5)},
    {FunctionKind::kMax, {NullFloat()}, NullFloat()},
    {FunctionKind::kMax, {}, NullFloat()},
    // Max(Double)
    {FunctionKind::kMax, {Double(1.5), Double(2.5)}, Double(2.5)},
    {FunctionKind::kMax, {Double(2.5), Double(1.5)}, Double(2.5)},
    {FunctionKind::kMax, {Double(1.5), NullDouble()},
        Double(1.5)},
    {FunctionKind::kMax, {NullDouble()}, NullDouble()},
    {FunctionKind::kMax, {}, NullDouble()},
    // Max(Numeric)
    {FunctionKind::kMax, {Numeric(1), Numeric(2)}, Numeric(2)},
    {FunctionKind::kMax, {Numeric(2), Numeric(1)}, Numeric(2)},
    {FunctionKind::kMax, {Numeric(1), NullNumeric()}, Numeric(1)},
    {FunctionKind::kMax, {NullNumeric()}, NullNumeric()},
    {FunctionKind::kMax, {}, NullNumeric()},
    // Max(String)
    {FunctionKind::kMax, {String("a"), String("b")}, String("b")},
    {FunctionKind::kMax, {String("b"), String("a")}, String("b")},
    {FunctionKind::kMax, {String("a"), NullString()},
        String("a")},
    {FunctionKind::kMax, {NullString()}, NullString()},
    {FunctionKind::kMax, {}, NullString()},
    // Max(Bytes)
    {FunctionKind::kMax, {Bytes("a"), Bytes("b")}, Bytes("b")},
    {FunctionKind::kMax, {Bytes("b"), Bytes("a")}, Bytes("b")},
    {FunctionKind::kMax, {Bytes("a"), NullBytes()},
        Bytes("a")},
    {FunctionKind::kMax, {NullBytes()}, NullBytes()},
    {FunctionKind::kMax, {}, NullBytes()},
    // Max(Date)
    {FunctionKind::kMax, {Date(1), Date(2)}, Date(2)},
    {FunctionKind::kMax, {Date(2), Date(1)}, Date(2)},
    {FunctionKind::kMax, {Date(1), NullDate()}, Date(1)},
    {FunctionKind::kMax, {NullDate()}, NullDate()},
    {FunctionKind::kMax, {}, NullDate()},
    // Max(Timestamp)
    {FunctionKind::kMax, {TimestampFromUnixMicros(1),
       TimestampFromUnixMicros(2)}, TimestampFromUnixMicros(2)},
    {FunctionKind::kMax, {TimestampFromUnixMicros(2),
       TimestampFromUnixMicros(1)}, TimestampFromUnixMicros(2)},
    {FunctionKind::kMax, {TimestampFromUnixMicros(1), NullTimestamp()},
          TimestampFromUnixMicros(1)},
    {FunctionKind::kMax, {NullTimestamp()}, NullTimestamp()},
    {FunctionKind::kMax, {}, NullTimestamp()},
     // Max(Enum)
    {FunctionKind::kMax, {Enum(enum_type, 1), Enum(enum_type, 2)},
          Enum(enum_type, 2)},
    {FunctionKind::kMax, {Enum(enum_type, 2), Enum(enum_type, 1)},
          Enum(enum_type, 2)},
    {FunctionKind::kMax, {Enum(enum_type, 1), Null(enum_type)},
          Enum(enum_type, 1)},
    {FunctionKind::kMax, {Null(enum_type)}, Null(enum_type)},
    {FunctionKind::kMax, {}, Null(enum_type)},
  };
}

static std::vector<AggregateFunctionTemplate>
OtherAggregateFunctionTemplates() {
  const EnumType* enum_type;
  GOOGLESQL_CHECK_OK(test_values::static_type_factory()->MakeEnumType(
      googlesql_test::TestEnum_descriptor(), &enum_type));
  return {
      // Any(<any type permitted>)
      {FunctionKind::kAnyValue, {Bool(true), NullBool()}, Bool(true), kNonDet},
      {FunctionKind::kAnyValue, {NullBool(), Bool(true)}, NullBool(), kNonDet},
      {FunctionKind::kAnyValue, {NullBool()}, NullBool()},
      {FunctionKind::kAnyValue, {Bool(true)}, Bool(true)},
      {FunctionKind::kAnyValue,
       {Double(3.14), NullDouble()},
       Double(3.14),
       kNonDet},
      {FunctionKind::kAnyValue,
       {NullDouble(), Double(3.14)},
       NullDouble(),
       kNonDet},
      {FunctionKind::kAnyValue, {Int64(1), NullInt64()}, Int64(1), kNonDet},
      {FunctionKind::kAnyValue, {NullInt64(), Int64(1)}, NullInt64(), kNonDet},
      {FunctionKind::kAnyValue,
       {String("Hi"), NullString()},
       String("Hi"),
       kNonDet},
      {FunctionKind::kAnyValue,
       {NullString(), String("Hi")},
       NullString(),
       kNonDet},
      // Avg
      {FunctionKind::kAvg, {Int64(1), Int64(2), NullInt64()}, Double(1.5)},
      {FunctionKind::kAvg, {Uint64(1), Uint64(2), NullUint64()}, Double(1.5)},
      {FunctionKind::kAvg,
       {Double(3.14), Double(2.72), NullDouble()},
       Double(2.93)},
      {FunctionKind::kAvg,
       {Numeric(1), Numeric(5), NullNumeric(), Numeric(-14), Numeric(3)},
       Numeric(NumericValue::FromString("-1.25").value())},
      // Count
      {FunctionKind::kCount,
       {Int64(1), Int64(2), Int64(3), Int64(4), Int64(5), Int64(6), NullInt64(),
        Int64(7), Int64(8)},
       Int64(8)},
      {FunctionKind::kCount, {Bool(false), Bool(true)}, Int64(2)},
      {FunctionKind::kCount, {Double(1.5), Double(2.5)}, Int64(2)},
      {FunctionKind::kCount, {Int64(1), Int64(2)}, Int64(2)},
      {FunctionKind::kCount, {Numeric(1), Numeric(-1)}, Int64(2)},
      {FunctionKind::kCount, {String("a"), String("b"), String("")}, Int64(3)},
      {FunctionKind::kCount, {Bool(false), NullBool()}, Int64(1)},
      {FunctionKind::kCount, {NullDouble(), Double(2.5)}, Int64(1)},
      {FunctionKind::kCount, {Int64(1), NullInt64()}, Int64(1)},
      {FunctionKind::kCount, {Numeric(1), NullNumeric()}, Int64(1)},
      {FunctionKind::kCount, {NullString(), String("b")}, Int64(1)},
      {FunctionKind::kCount, {NullBool(), NullBool()}, Int64(0)},
      {FunctionKind::kCount, {NullDouble(), NullDouble()}, Int64(0)},
      {FunctionKind::kCount, {NullInt64(), NullInt64()}, Int64(0)},
      {FunctionKind::kCount, {NullString(), NullString()}, Int64(0)},
      {FunctionKind::kCount, {NullNumeric(), NullNumeric()}, Int64(0)},
      {FunctionKind::kCount, {}, Int64(0)},
      {FunctionKind::kCount, {Array({String("a")})}, Int64(1)},

      // ArrayAgg(Int64)
      {FunctionKind::kArrayAgg,
       {Int64(1), NullInt64()},
       Array({Int64(1), NullInt64()})},
      {FunctionKind::kArrayAgg, {}, Value::Null(Int64ArrayType())},
      // Sum(Int64)
      {FunctionKind::kSum, {Int64(1), Int64(2)}, Int64(3)},
      {FunctionKind::kSum, {Int64(1), NullInt64()}, Int64(1)},
      {FunctionKind::kSum, {NullInt64()}, NullInt64()},
      {FunctionKind::kSum, {}, NullInt64()},
      // Sum(Uint64)
      {FunctionKind::kSum, {Uint64(1), Uint64(2)}, Uint64(3)},
      {FunctionKind::kSum, {Uint64(1), NullUint64()}, Uint64(1)},
      {FunctionKind::kSum, {NullUint64()}, NullUint64()},
      {FunctionKind::kSum, {}, NullUint64()},
      // Sum(NumericValue)
      {FunctionKind::kSum, {Numeric(1), Numeric(2)}, Numeric(3)},
      {FunctionKind::kSum,
       {Numeric(NumericValue::MinValue()), Numeric(NumericValue::MaxValue())},
       Numeric(0)},
      {FunctionKind::kSum, {Numeric(1), NullNumeric()}, Numeric(1)},
      {FunctionKind::kSum, {NullNumeric()}, NullNumeric()},
      {FunctionKind::kSum, {}, NullNumeric()},
      // StringAgg(String)
      {FunctionKind::kStringAgg,
       {String("1"), String("1")},
       {String("1,1")},
       kNonDet},
      {FunctionKind::kStringAgg, {String("1"), NullString()}, {String("1")}},
      {FunctionKind::kStringAgg, {String("1")}, {String("1")}},
      // StringAgg(Bytes)
      {FunctionKind::kStringAgg,
       {Bytes("1"), Bytes("1")},
       {Bytes("1,1")},
       kNonDet},
      {FunctionKind::kStringAgg, {Bytes("1"), NullBytes()}, {Bytes("1")}},
      {FunctionKind::kStringAgg, {Bytes("1")}, {Bytes("1")}},
  };
}

static std::vector<AggregateFunctionTemplate> AggregateFunctionTemplates() {
  return ConcatTemplates({
      MinAggregateFunctionTemplates(),
      MaxAggregateFunctionTemplates(),
      OtherAggregateFunctionTemplates(),
    });
}

typedef TestWithParam<AggregateFunctionTemplate> AggregateFunctionTemplateTest;

// Evaluates an aggregation function and returns the result.
static absl::StatusOr<Value> EvalAgg(const BuiltinAggregateFunction& agg,
                                     absl::Span<const Value> values,
                                     EvaluationContext* context,
                                     absl::Span<const Value> args = {}) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<AggregateAccumulator> accumulator,
                   agg.CreateAccumulator(args, /*params=*/{},
                                         /*collator_list=*/{}, context));
  bool stop_accumulation;
  absl::Status status;
  for (const Value& value : values) {
    if (!accumulator->Accumulate(value, &stop_accumulation, &status)) {
      return status;
    }
    if (stop_accumulation) break;
  }
  return accumulator->GetFinalResult(/*inputs_in_defined_order=*/false);
}

TEST_P(AggregateFunctionTemplateTest, AggregateFunctionTest) {
  const AggregateFunctionTemplate& t = GetParam();
  BuiltinAggregateFunction fct(t.kind, t.result.type(), /*num_input_fields=*/1,
                               t.argument_type());
  EvaluationContext context((EvaluationOptions()));
  EXPECT_THAT(EvalAgg(fct, t.values, &context), IsOkAndHolds(t.result));
  EXPECT_EQ(t.is_deterministic, context.IsDeterministicOutput())
      << "Aggregate function: " << fct.debug_name();
}

INSTANTIATE_TEST_SUITE_P(AggregateFunction, AggregateFunctionTemplateTest,
                         ValuesIn(AggregateFunctionTemplates()));

TEST(EvalAggTest, AnyDeterministic) {
  BuiltinAggregateFunction fct(FunctionKind::kAnyValue, Int64Type(),
                               /*num_input_fields=*/1, Int64Type());
  EvaluationContext context((EvaluationOptions()));
  EXPECT_THAT(EvalAgg(fct, {Int64(1)}, &context), IsOkAndHolds(Int64(1)));
  EXPECT_TRUE(context.IsDeterministicOutput());
}

TEST(EvalAggTest, AnyNonDeterministic) {
  BuiltinAggregateFunction fct(FunctionKind::kAnyValue, Int64Type(),
                               /*num_input_fields=*/1, Int64Type());
  EvaluationContext context((EvaluationOptions()));
  // Create non-determinism by adding an extra slot.
  EXPECT_THAT(EvalAgg(fct, {Int64(1), NullInt64(), Int64(2)}, &context),
              IsOkAndHolds(Int64(1)));
  EXPECT_FALSE(context.IsDeterministicOutput());
}

TEST(EvalAggTest, Avg) {
  BuiltinAggregateFunction fct(FunctionKind::kAvg, DoubleType(),
                               /*num_input_fields=*/1, Int64Type());
  EvaluationContext context((EvaluationOptions()));
  EXPECT_THAT(
      EvalAgg(fct, {NullInt64(), Int64(1), Int64(2), Int64(3)}, &context),
      IsOkAndHolds(Double(2.0)));
  EXPECT_TRUE(context.IsDeterministicOutput());
}

TEST(EvalAggTest, SumNumericOverflow) {
  BuiltinAggregateFunction fct(FunctionKind::kSum, NumericType(),
                               /*num_input_fields=*/1, NumericType());
  EvaluationContext context((EvaluationOptions()));
  EXPECT_THAT(
      EvalAgg(fct, {Numeric(1), Numeric(NumericValue::MaxValue()), Numeric(1)},
              &context),
      StatusIs(absl::StatusCode::kOutOfRange));
  EXPECT_TRUE(context.IsDeterministicOutput());
}

TEST(OrderPreservationTest, GroupByAggregate) {
  TypeFactory type_factory;
  VariableId a("a"), b("b"), c1("c1"), c2("c2"), k("k"), n("n"), d("d");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_a, DerefExpr::Create(a, Int64Type()));

  std::vector<std::unique_ptr<KeyArg>> keys;
  keys.push_back(std::make_unique<KeyArg>(k, std::move(deref_a)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_b_for_c1, DerefExpr::Create(b, Int64Type()));

  std::vector<std::unique_ptr<ValueExpr>> args_for_c1;
  args_for_c1.push_back(std::move(deref_b_for_c1));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto arg_c1,
      AggregateArg::Create(c1,
                           std::make_unique<BuiltinAggregateFunction>(
                               FunctionKind::kCount, Int64Type(),
                               /*num_input_fields=*/1, Int64Type()),
                           std::move(args_for_c1)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto arg_c2,
      AggregateArg::Create(c2, std::make_unique<BuiltinAggregateFunction>(
                                   FunctionKind::kCount, Int64Type(),
                                   /*num_input_fields=*/0, EmptyStructType())));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_b_for_n, DerefExpr::Create(b, Int64Type()));

  std::vector<std::unique_ptr<ValueExpr>> args_for_n;
  args_for_n.push_back(std::move(deref_b_for_n));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto arg_n,
      AggregateArg::Create(
          n,
          std::make_unique<BuiltinAggregateFunction>(
              FunctionKind::kArrayAgg, Int64ArrayType(),
              /*num_input_fields=*/1, Int64Type(), false /* ignores_null */),
          std::move(args_for_n), AggregateArg::kAll));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_b_for_d, DerefExpr::Create(b, Int64Type()));

  std::vector<std::unique_ptr<ValueExpr>> args_for_d;
  args_for_d.push_back(std::move(deref_b_for_d));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto arg_d,
      AggregateArg::Create(
          d,
          std::make_unique<BuiltinAggregateFunction>(
              FunctionKind::kArrayAgg, Int64ArrayType(),
              /*num_input_fields=*/1, Int64Type(), false /* ignores_null */),
          std::move(args_for_d), AggregateArg::kDistinct));

  std::vector<std::unique_ptr<AggregateArg>> aggregators;
  aggregators.push_back(std::move(arg_c1));
  aggregators.push_back(std::move(arg_c2));
  aggregators.push_back(std::move(arg_n));
  aggregators.push_back(std::move(arg_d));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto aggregate_op,
      AggregateOp::Create(std::move(keys), std::move(aggregators),
                          absl::WrapUnique(new TestRelationalOp(
                              {a, b},
                              CreateTestTupleDatas({{Int64(0), Int64(1)},
                                                    {Int64(0), Int64(1)},
                                                    {Int64(0), Int64(2)},
                                                    {Int64(1), Int64(10)},
                                                    {Int64(1), Int64(10)},
                                                    {Int64(1), NullInt64()}}),
                              /*preserves_order=*/true)),
                          /*grouping_sets=*/{}));

  EXPECT_EQ(
      "AggregateOp(\n"
      "+-keys: {\n"
      "| +-$k := $a},\n"
      "+-aggregators: {\n"
      "| +-$c1 := Count($b),\n"
      "| +-$c2 := Count(),\n"
      "| +-$n := ArrayAgg($b) [ignores_null = false],\n"
      "| +-$d := DISTINCT ArrayAgg($b) [ignores_null = false]},\n"
      "+-input: TestRelationalOp)",
      aggregate_op->DebugString());
  std::unique_ptr<TupleSchema> output_schema =
      aggregate_op->CreateOutputSchema();
  EXPECT_THAT(output_schema->variables(), ElementsAre(k, c1, c2, n, d));

  // Return result as array of structs.
  auto struct_type = MakeStructType(
      {{"id", Int64Type()}, {"agg_all", Int64ArrayType()},
       {"agg_distinct", Int64ArrayType()}});
  auto array_type = MakeArrayType(struct_type);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_k, DerefExpr::Create(k, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_n, DerefExpr::Create(n, Int64ArrayType()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_d, DerefExpr::Create(d, Int64ArrayType()));

  std::vector<std::unique_ptr<ExprArg>> args_for_struct;
  args_for_struct.push_back(std::make_unique<ExprArg>(std::move(deref_k)));
  args_for_struct.push_back(std::make_unique<ExprArg>(std::move(deref_n)));
  args_for_struct.push_back(std::make_unique<ExprArg>(std::move(deref_d)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto struct_expr,
      NewStructExpr::Create(struct_type, std::move(args_for_struct)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto nest_op,
                       ArrayNestExpr::Create(array_type, std::move(struct_expr),
                                             std::move(aggregate_op),
                                             /*is_with_table=*/false));
  GOOGLESQL_ASSERT_OK(nest_op->SetSchemasForEvaluation(EmptyParamsSchemas()));

  // The results below are assumed to come from two hypothetical engines, each
  // using different order of nested arrays.
  auto engine1 =
      StructArray({"id", "agg_all", "agg_distinct"},
                  {{Int64(0), Array({Int64(1), Int64(1), Int64(2)}),
                    Array({Int64(1), Int64(2)})},
                   {Int64(1), Array({Int64(10), NullInt64(), Int64(10)}),
                    Array({NullInt64(), Int64(10)})}});
  auto engine2 = StructArray(
      {"id", "agg_all", "agg_distinct"},
      {{Int64(0), Array({Int64(1), Int64(2), Int64(1)}),
                  Array({Int64(2), Int64(1)})},
       {Int64(1), Array({NullInt64(), Int64(10), Int64(10)}),
                  Array({Int64(10), NullInt64()})}});

  // Result produced by the reference implementation. All nested arrays must
  // have kIgnoresOrder setting since the inputs to aggregates are unordered.
  EvaluationContext context((EvaluationOptions()));
  TupleSlot slot;
  absl::Status status;
  ASSERT_TRUE(nest_op->EvalSimple(EmptyParams(), &context, &slot, &status))
      << status;
  const Value& reference = slot.value();

  // This is the preferred way of using the matcher.
  EXPECT_THAT(engine1, EqualsValue(reference));
  EXPECT_THAT(engine2, EqualsValue(reference));
  // But the reverse order works too.
  EXPECT_THAT(reference, EqualsValue(engine1));
  EXPECT_THAT(reference, EqualsValue(engine2));
  // The results of the engines differ because neither can specify kIgnoresOrder
  // on arrays.
  EXPECT_THAT(engine1, Not(EqualsValue(engine2)));
  EXPECT_THAT(engine2, Not(EqualsValue(engine1)));
}

TEST(CreateIteratorTest, AggregateAll) {
  VariableId a("a"), b("b"), param("param"), c1("c1"), c2("c2"), c3("c3");
  TypeFactory type_factory;

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_param, DerefExpr::Create(param, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_b, DerefExpr::Create(b, Int64Type()));

  std::vector<std::unique_ptr<ValueExpr>> args_for_c1;
  args_for_c1.push_back(std::move(deref_param));

  std::vector<std::unique_ptr<ValueExpr>> args_for_c2;
  args_for_c2.push_back(std::move(deref_b));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto arg_c1,
      AggregateArg::Create(c1,
                           std::make_unique<BuiltinAggregateFunction>(
                               FunctionKind::kCount, Int64Type(),
                               /*num_input_fields=*/1, Int64Type()),
                           std::move(args_for_c1)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto arg_c2,
      AggregateArg::Create(c2,
                           std::make_unique<BuiltinAggregateFunction>(
                               FunctionKind::kCount, Int64Type(),
                               /*num_input_fields=*/1, Int64Type()),
                           std::move(args_for_c2)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto arg_c3,
      AggregateArg::Create(c3, std::make_unique<BuiltinAggregateFunction>(
                                   FunctionKind::kCount, Int64Type(),
                                   /*num_input_fields=*/0, EmptyStructType())));

  std::vector<std::unique_ptr<AggregateArg>> aggregators;
  aggregators.push_back(std::move(arg_c1));
  aggregators.push_back(std::move(arg_c2));
  aggregators.push_back(std::move(arg_c3));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto aggregate_op,
                       AggregateOp::Create(
                           /*keys=*/{}, std::move(aggregators),
                           absl::WrapUnique(new TestRelationalOp(
                               {a, b},
                               CreateTestTupleDatas({{Int64(1), Int64(2)},
                                                     {Int64(1), NullInt64()},
                                                     {Int64(5), NullInt64()},
                                                     {Int64(1), Int64(2)}}),
                               /*preserves_order=*/true)),
                           /*grouping_sets=*/{}));
  EXPECT_EQ(aggregate_op->IteratorDebugString(),
            "AggregationTupleIterator(TestTupleIterator)");
  EXPECT_EQ(
      "AggregateOp(\n"
      "+-keys: {},\n"
      "+-aggregators: {\n"
      "| +-$c1 := Count($param),\n"
      "| +-$c2 := Count($b),\n"
      "| +-$c3 := Count()},\n"
      "+-input: TestRelationalOp)",
      aggregate_op->DebugString());
  std::unique_ptr<TupleSchema> output_schema =
      aggregate_op->CreateOutputSchema();
  EXPECT_THAT(output_schema->variables(), ElementsAre(c1, c2, c3));

  EvaluationContext context((EvaluationOptions()));

  TupleSchema params_schema({param});
  TupleData params_data = CreateTestTupleData({Int64(1000)});

  GOOGLESQL_ASSERT_OK(aggregate_op->SetSchemasForEvaluation({&params_schema}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<TupleIterator> iter,
                       aggregate_op->CreateIterator(
                           {&params_data}, /*num_extra_slots=*/1, &context));
  EXPECT_EQ(iter->DebugString(), "AggregationTupleIterator(TestTupleIterator)");
  EXPECT_TRUE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> data,
                       ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 1);
  EXPECT_EQ(Tuple(&iter->Schema(), &data[0]).DebugString(), "<c1:4,c2:2,c3:4>");
  // Check for the extra slot.
  EXPECT_EQ(data[0].num_slots(), 4);

  // Do it again with cancellation before reading the only output tuple.
  context.ClearDeadlineAndCancellationState();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      iter, aggregate_op->CreateIterator({&params_data}, /*num_extra_slots=*/1,
                                         &context));
  GOOGLESQL_ASSERT_OK(context.CancelStatement());
  EXPECT_EQ(iter->Next(), nullptr);
  EXPECT_THAT(iter->Status(), StatusIs(absl::StatusCode::kCancelled, _));

  // Check the scrambling works, although it is not very interesting because
  // there is only one output tuple.
  EvaluationContext scramble_context(GetScramblingEvaluationOptions());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      iter, aggregate_op->CreateIterator({&params_data}, /*num_extra_slots=*/1,
                                         &scramble_context));
  EXPECT_EQ(iter->DebugString(),
            "ReorderingTupleIterator(AggregationTupleIterator("
            "TestTupleIterator))");
  EXPECT_FALSE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(data, ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 1);
  EXPECT_EQ(Tuple(&iter->Schema(), &data[0]).DebugString(), "<c1:4,c2:2,c3:4>");
  // Check for the extra slot.
  EXPECT_EQ(data[0].num_slots(), 4);

  // Check that if the memory bound is too low, we get an error.
  EvaluationContext memory_context(GetIntermediateMemoryEvaluationOptions(
      /*total_bytes=*/100));
  EXPECT_THAT(
      aggregate_op->CreateIterator({&params_data},
                                   /*num_extra_slots=*/1, &memory_context),
      StatusIs(absl::StatusCode::kResourceExhausted,
               HasSubstr("Out of memory")));
}

TEST(CreateIteratorTest, AggregateOrderBy) {
  TypeFactory type_factory;
  VariableId a("a"), b("b"), c("c"), d("d"), e("e"), f("f"), g("g"), h("h"),
      i("i"), j("j"), k("k"), l("l");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_b0, DerefExpr::Create(b, types::Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_b1, DerefExpr::Create(b, types::Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_b2, DerefExpr::Create(b, types::Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_b3, DerefExpr::Create(b, types::Int64Type()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_c0,
                       DerefExpr::Create(c, types::StringType()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_c1,
                       DerefExpr::Create(c, types::StringType()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_c2,
                       DerefExpr::Create(c, types::StringType()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_b_bc,
                       DerefExpr::Create(b, types::Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_c_bc,
                       DerefExpr::Create(c, types::StringType()));

  std::vector<std::unique_ptr<KeyArg>> order_by_keys_b0;
  order_by_keys_b0.push_back(
      std::make_unique<KeyArg>(b, std::move(deref_b0), KeyArg::kAscending));

  std::vector<std::unique_ptr<KeyArg>> order_by_keys_b1;
  order_by_keys_b1.push_back(
      std::make_unique<KeyArg>(b, std::move(deref_b1), KeyArg::kAscending));

  std::vector<std::unique_ptr<KeyArg>> order_by_keys_b2;
  order_by_keys_b2.push_back(
      std::make_unique<KeyArg>(b, std::move(deref_b2), KeyArg::kAscending));

  std::vector<std::unique_ptr<KeyArg>> order_by_keys_b3;
  order_by_keys_b3.push_back(
      std::make_unique<KeyArg>(b, std::move(deref_b3), KeyArg::kDescending));

  std::vector<std::unique_ptr<KeyArg>> order_by_keys_c0;
  order_by_keys_c0.push_back(
      std::make_unique<KeyArg>(c, std::move(deref_c0), KeyArg::kDescending));

  std::vector<std::unique_ptr<KeyArg>> order_by_keys_c1;
  order_by_keys_c1.push_back(
      std::make_unique<KeyArg>(c, std::move(deref_c1), KeyArg::kDescending));

  std::vector<std::unique_ptr<KeyArg>> order_by_keys_c2;
  order_by_keys_c2.push_back(
      std::make_unique<KeyArg>(c, std::move(deref_c2), KeyArg::kDescending));

  std::vector<std::unique_ptr<KeyArg>> order_by_keys_bc;
  order_by_keys_bc.push_back(
      std::make_unique<KeyArg>(b, std::move(deref_b_bc), KeyArg::kAscending));
  order_by_keys_bc.push_back(
      std::make_unique<KeyArg>(c, std::move(deref_c_bc), KeyArg::kDescending));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_b_for_d, DerefExpr::Create(b, Int64Type()));

  std::vector<std::unique_ptr<ValueExpr>> args_for_d;
  args_for_d.push_back(std::move(deref_b_for_d));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto agg_d,
      AggregateArg::Create(
          d,
          std::make_unique<BuiltinAggregateFunction>(
              FunctionKind::kArrayAgg, Int64ArrayType(),
              /*num_input_fields=*/1, Int64Type(), false /* ignores_null */),
          std::move(args_for_d), AggregateArg::kAll, nullptr /* having_expr */,
          AggregateArg::kHavingNone, std::move(order_by_keys_b0)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_c_for_e, DerefExpr::Create(c, StringType()));

  std::vector<std::unique_ptr<ValueExpr>> args_for_e;
  args_for_e.push_back(std::move(deref_c_for_e));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto agg_e,
      AggregateArg::Create(
          e,
          std::make_unique<BuiltinAggregateFunction>(
              FunctionKind::kArrayAgg, StringArrayType(),
              /*num_input_fields=*/1, StringType(), false /* ignores_null */),
          std::move(args_for_e), AggregateArg::kAll, nullptr /* having_expr */,
          AggregateArg::kHavingNone, std::move(order_by_keys_c0)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_c_for_f, DerefExpr::Create(c, StringType()));

  std::vector<std::unique_ptr<ValueExpr>> args_for_f;
  args_for_f.push_back(std::move(deref_c_for_f));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto agg_f,
      AggregateArg::Create(f,
                           std::make_unique<BuiltinAggregateFunction>(
                               FunctionKind::kStringAgg, StringType(),
                               /*num_input_fields=*/1, StringType()),
                           std::move(args_for_f), AggregateArg::kAll,
                           nullptr /* having_expr */, AggregateArg::kHavingNone,
                           std::move(order_by_keys_c1)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_c_for_g, DerefExpr::Create(c, StringType()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto const_for_g, ConstExpr::Create(String("|")));

  std::vector<std::unique_ptr<ValueExpr>> args_for_g;
  args_for_g.push_back(std::move(deref_c_for_g));
  args_for_g.push_back(std::move(const_for_g));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto agg_g,
      AggregateArg::Create(g,
                           std::make_unique<BuiltinAggregateFunction>(
                               FunctionKind::kStringAgg, StringType(),
                               /*num_input_fields=*/1, StringType()),
                           std::move(args_for_g), AggregateArg::kAll,
                           nullptr /* having_expr */, AggregateArg::kHavingNone,
                           std::move(order_by_keys_c2)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_c_for_h, DerefExpr::Create(c, StringType()));

  std::vector<std::unique_ptr<ValueExpr>> args_for_h;
  args_for_h.push_back(std::move(deref_c_for_h));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto agg_h,
      AggregateArg::Create(h,
                           std::make_unique<BuiltinAggregateFunction>(
                               FunctionKind::kStringAgg, StringType(),
                               /*num_input_fields=*/1, StringType()),
                           std::move(args_for_h), AggregateArg::kAll,
                           nullptr /* having_expr */, AggregateArg::kHavingNone,
                           std::move(order_by_keys_b1)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_c_for_i, DerefExpr::Create(c, StringType()));

  std::vector<std::unique_ptr<ValueExpr>> args_for_i;
  args_for_i.push_back(std::move(deref_c_for_i));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto agg_i,
      AggregateArg::Create(i,
                           std::make_unique<BuiltinAggregateFunction>(
                               FunctionKind::kStringAgg, StringType(),
                               /*num_input_fields=*/1, StringType()),
                           std::move(args_for_i), AggregateArg::kAll,
                           nullptr /* having_expr */, AggregateArg::kHavingNone,
                           std::move(order_by_keys_bc)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_b_for_j, DerefExpr::Create(b, Int64Type()));

  std::vector<std::unique_ptr<ValueExpr>> args_for_j;
  args_for_j.push_back(std::move(deref_b_for_j));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto agg_j,
      AggregateArg::Create(
          j,
          std::make_unique<BuiltinAggregateFunction>(
              FunctionKind::kArrayAgg, Int64ArrayType(),
              /*num_input_fields=*/1, Int64Type(), false /* ignores_null */),
          std::move(args_for_j), AggregateArg::kDistinct,
          nullptr /* having_expr */, AggregateArg::kHavingNone,
          std::move(order_by_keys_b2)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_c_for_l, DerefExpr::Create(c, StringType()));

  std::vector<std::unique_ptr<ValueExpr>> args_for_l;
  args_for_l.push_back(std::move(deref_c_for_l));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto agg_l,
      AggregateArg::Create(
          l,
          std::make_unique<BuiltinAggregateFunction>(
              FunctionKind::kArrayAgg, StringArrayType(),
              /*num_input_fields=*/1, StringType(), false /* ignores_null */),
          std::move(args_for_l), AggregateArg::kAll, nullptr /* having_expr */,
          AggregateArg::kHavingNone, std::move(order_by_keys_b3)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_a, DerefExpr::Create(a, Int64Type()));

  std::vector<std::unique_ptr<KeyArg>> keys;
  keys.push_back(std::make_unique<KeyArg>(k, std::move(deref_a)));

  std::vector<std::unique_ptr<AggregateArg>> aggregators;
  aggregators.push_back(std::move(agg_d));
  aggregators.push_back(std::move(agg_e));
  aggregators.push_back(std::move(agg_f));
  aggregators.push_back(std::move(agg_g));
  aggregators.push_back(std::move(agg_h));
  aggregators.push_back(std::move(agg_i));
  aggregators.push_back(std::move(agg_j));
  aggregators.push_back(std::move(agg_l));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto aggregate_op,
      AggregateOp::Create(
          std::move(keys), std::move(aggregators),
          absl::WrapUnique(new TestRelationalOp(
              {a, b, c},
              CreateTestTupleDatas({{Int64(0), Int64(1), String("x")},
                                    {Int64(0), Int64(1), String("y")},
                                    {Int64(0), Int64(2), String("z")},
                                    {Int64(1), Int64(10), String("c")},
                                    {Int64(1), Int64(10), String("b")},
                                    {Int64(1), NullInt64(), String("a")},
                                    {Int64(1), NullInt64(), NullString()}}),
              /*preserves_order=*/true)),
          /*grouping_sets=*/{}));
  EXPECT_EQ(
      "AggregateOp(\n"
      "+-keys: {\n"
      "| +-$k := $a},\n"
      "+-aggregators: {\n"
      "| +-$d := ArrayAgg($b) ORDER BY $b := $b ASC "
      "[ignores_null = false],\n"
      "| +-$e := ArrayAgg($c) ORDER BY $c := $c DESC "
      "[ignores_null = false],\n"
      "| +-$f := StringAgg($c) ORDER BY $c := $c DESC,\n"
      "| +-$g := StringAgg($c, ConstExpr(\"|\")) "
      "ORDER BY $c := $c DESC,\n"
      "| +-$h := StringAgg($c) ORDER BY $b := $b ASC,\n"
      "| +-$i := StringAgg($c) ORDER BY $b := $b ASC,"
      "$c := $c DESC,\n"
      "| +-$j := DISTINCT ArrayAgg($b) "
      "ORDER BY $b := $b ASC [ignores_null = false],\n"
      "| +-$l := ArrayAgg($c) ORDER BY $b := $b DESC "
      "[ignores_null = false]},\n"
      "+-input: TestRelationalOp)",
      aggregate_op->DebugString());
  std::unique_ptr<TupleSchema> output_schema =
      aggregate_op->CreateOutputSchema();
  EXPECT_THAT(output_schema->variables(),
              ElementsAre(k, d, e, f, g, h, i, j, l));
  GOOGLESQL_ASSERT_OK(aggregate_op->SetSchemasForEvaluation(EmptyParamsSchemas()));

  // Use stable sort for reference output.
  EvaluationContext context(
      (EvaluationOptions{.always_use_stable_sort = true}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<TupleIterator> iter,
                       aggregate_op->CreateIterator(
                           EmptyParams(), /*num_extra_slots=*/1, &context));
  EXPECT_EQ(iter->DebugString(), "AggregationTupleIterator(TestTupleIterator)");
  EXPECT_TRUE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> data,
                       ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 2);
  EXPECT_EQ(Tuple(&iter->Schema(), &data[0]).DebugString(),
            "<k:0,"
            "d:[1, 1, 2],"
            "e:[\"z\", \"y\", \"x\"],"
            "f:\"z,y,x\","
            "g:\"z|y|x\","
            "h:\"x,y,z\","
            "i:\"y,x,z\","
            "j:[1, 2],"
            "l:[\"z\", \"x\", \"y\"]>");
  EXPECT_EQ(Tuple(&iter->Schema(), &data[1]).DebugString(),
            "<k:1,"
            "d:[NULL, NULL, 10, 10],"
            "e:[\"c\", \"b\", \"a\", NULL],"
            "f:\"c,b,a\","
            "g:\"c|b|a\","
            "h:\"a,c,b\","
            "i:\"a,c,b\","
            "j:[NULL, 10],"
            "l:[\"c\", \"b\", \"a\", NULL]>");
  // Check for the extra slot.
  EXPECT_EQ(data[0].num_slots(), 10);
  EXPECT_EQ(data[1].num_slots(), 10);

  // Do it again with cancellation.
  context.ClearDeadlineAndCancellationState();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      iter, aggregate_op->CreateIterator(EmptyParams(), /*num_extra_slots=*/1,
                                         &context));
  GOOGLESQL_ASSERT_OK(context.CancelStatement());
  absl::Status status;
  data = ReadFromTupleIteratorFull(iter.get(), &status);
  EXPECT_TRUE(data.empty());
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kCancelled, _));

  // Check that scrambling works.
  EvaluationContext scramble_context(GetScramblingEvaluationOptions());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(iter, aggregate_op->CreateIterator(EmptyParams(),
                                                          /*num_extra_slots=*/1,
                                                          &scramble_context));
  EXPECT_EQ(iter->DebugString(),
            "ReorderingTupleIterator(AggregationTupleIterator("
            "TestTupleIterator))");
  EXPECT_FALSE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(data, ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 2);
  EXPECT_EQ(data[0].num_slots(), 10);
  EXPECT_EQ(data[1].num_slots(), 10);

  // Check that if the memory bound is too low, we get an error.
  EvaluationContext memory_context(GetIntermediateMemoryEvaluationOptions(
      /*total_bytes=*/100));
  EXPECT_THAT(
      aggregate_op->CreateIterator(EmptyParams(),
                                   /*num_extra_slots=*/1, &memory_context),
      StatusIs(absl::StatusCode::kResourceExhausted,
               HasSubstr("Out of memory")));
}

TEST(CreateIteratorTest, AggregateLimit) {
  TypeFactory type_factory;
  VariableId a("a"), b("b"), c("c"), d("d"), e("e"), k("k"), f("f"), g("g"),
      h("h"), i("i"), limit0("limit0"), limit2("limit2"), limit100("limit100");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_b0, DerefExpr::Create(b, types::Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_b1, DerefExpr::Create(b, types::Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_b2, DerefExpr::Create(b, types::Int64Type()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_c0,
                       DerefExpr::Create(c, types::StringType()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_c1,
                       DerefExpr::Create(c, types::StringType()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_c2,
                       DerefExpr::Create(c, types::StringType()));

  std::vector<std::unique_ptr<KeyArg>> order_by_keys_b0;
  order_by_keys_b0.push_back(
      std::make_unique<KeyArg>(b, std::move(deref_b0), KeyArg::kAscending));

  std::vector<std::unique_ptr<KeyArg>> order_by_keys_b1;
  order_by_keys_b1.push_back(
      std::make_unique<KeyArg>(b, std::move(deref_b1), KeyArg::kAscending));

  std::vector<std::unique_ptr<KeyArg>> order_by_keys_b2;
  order_by_keys_b2.push_back(
      std::make_unique<KeyArg>(b, std::move(deref_b2), KeyArg::kAscending));

  std::vector<std::unique_ptr<KeyArg>> order_by_keys_c0;
  order_by_keys_c0.push_back(
      std::make_unique<KeyArg>(c, std::move(deref_c0), KeyArg::kDescending));

  std::vector<std::unique_ptr<KeyArg>> order_by_keys_c1;
  order_by_keys_c1.push_back(
      std::make_unique<KeyArg>(c, std::move(deref_c1), KeyArg::kDescending));

  std::vector<std::unique_ptr<KeyArg>> order_by_keys_c2;
  order_by_keys_c2.push_back(
      std::make_unique<KeyArg>(c, std::move(deref_c2), KeyArg::kDescending));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_b_for_d, DerefExpr::Create(b, Int64Type()));

  std::vector<std::unique_ptr<ValueExpr>> args_for_d;
  args_for_d.push_back(std::move(deref_b_for_d));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto limit_for_d,
                       DerefExpr::Create(limit0, Int64Type()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto agg_d,
      AggregateArg::Create(
          d,
          std::make_unique<BuiltinAggregateFunction>(
              FunctionKind::kArrayAgg, Int64ArrayType(),
              /*num_input_fields=*/1, Int64Type(), false /* ignores_null */),
          std::move(args_for_d), AggregateArg::kAll, nullptr /* having_expr */,
          AggregateArg::kHavingNone, std::move(order_by_keys_b0),
          std::move(limit_for_d)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_b_for_e, DerefExpr::Create(b, Int64Type()));

  std::vector<std::unique_ptr<ValueExpr>> args_for_e;
  args_for_e.push_back(std::move(deref_b_for_e));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto limit_for_e,
                       DerefExpr::Create(limit2, Int64Type()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto agg_e,
      AggregateArg::Create(
          e,
          std::make_unique<BuiltinAggregateFunction>(
              FunctionKind::kArrayAgg, Int64ArrayType(),
              /*num_input_fields=*/1, Int64Type(), false /* ignores_null */),
          std::move(args_for_e), AggregateArg::kAll, nullptr /* having_expr */,
          AggregateArg::kHavingNone, std::move(order_by_keys_b1),
          std::move(limit_for_e)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_b_for_f, DerefExpr::Create(b, Int64Type()));

  std::vector<std::unique_ptr<ValueExpr>> args_for_f;
  args_for_f.push_back(std::move(deref_b_for_f));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto limit_for_f,
                       DerefExpr::Create(limit100, Int64Type()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto agg_f,
      AggregateArg::Create(
          f,
          std::make_unique<BuiltinAggregateFunction>(
              FunctionKind::kArrayAgg, Int64ArrayType(),
              /*num_input_fields=*/1, Int64Type(), false /* ignores_null */),
          std::move(args_for_f), AggregateArg::kAll, nullptr /* having_expr */,
          AggregateArg::kHavingNone, std::move(order_by_keys_b2),
          std::move(limit_for_f)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_c_for_g, DerefExpr::Create(c, StringType()));

  std::vector<std::unique_ptr<ValueExpr>> args_for_g;
  args_for_g.push_back(std::move(deref_c_for_g));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto limit_for_g,
                       DerefExpr::Create(limit0, Int64Type()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto agg_g,
      AggregateArg::Create(
          g,
          std::make_unique<BuiltinAggregateFunction>(
              FunctionKind::kArrayAgg, StringArrayType(),
              /*num_input_fields=*/1, StringType(), false /* ignores_null */),
          std::move(args_for_g), AggregateArg::kAll, nullptr /* having_expr */,
          AggregateArg::kHavingNone, std::move(order_by_keys_c0),
          std::move(limit_for_g)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_c_for_h, DerefExpr::Create(c, StringType()));

  std::vector<std::unique_ptr<ValueExpr>> args_for_h;
  args_for_h.push_back(std::move(deref_c_for_h));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto limit_for_h,
                       DerefExpr::Create(limit2, Int64Type()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto agg_h,
      AggregateArg::Create(
          h,
          std::make_unique<BuiltinAggregateFunction>(
              FunctionKind::kArrayAgg, StringArrayType(),
              /*num_input_fields=*/1, StringType(), false /* ignores_null */),
          std::move(args_for_h), AggregateArg::kAll, nullptr /* having_expr */,
          AggregateArg::kHavingNone, std::move(order_by_keys_c1),
          std::move(limit_for_h)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_c_for_i, DerefExpr::Create(c, StringType()));

  std::vector<std::unique_ptr<ValueExpr>> args_for_i;
  args_for_i.push_back(std::move(deref_c_for_i));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto limit_for_i,
                       DerefExpr::Create(limit100, Int64Type()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto agg_i,
      AggregateArg::Create(
          i,
          std::make_unique<BuiltinAggregateFunction>(
              FunctionKind::kArrayAgg, StringArrayType(),
              /*num_input_fields=*/1, StringType(), false /* ignores_null */),
          std::move(args_for_i), AggregateArg::kAll, nullptr /* having_expr */,
          AggregateArg::kHavingNone, std::move(order_by_keys_c2),
          std::move(limit_for_i)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_a, DerefExpr::Create(a, Int64Type()));

  std::vector<std::unique_ptr<KeyArg>> keys;
  keys.push_back(std::make_unique<KeyArg>(k, std::move(deref_a)));

  std::vector<std::unique_ptr<AggregateArg>> aggregators;
  aggregators.push_back(std::move(agg_d));
  aggregators.push_back(std::move(agg_e));
  aggregators.push_back(std::move(agg_f));
  aggregators.push_back(std::move(agg_g));
  aggregators.push_back(std::move(agg_h));
  aggregators.push_back(std::move(agg_i));

  // Tests limit applied to ordered arguments to ensure the results are
  // deterministic.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto aggregate_op,
      AggregateOp::Create(
          std::move(keys), std::move(aggregators),
          absl::WrapUnique(new TestRelationalOp(
              {a, b, c},
              CreateTestTupleDatas({{Int64(0), Int64(1), String("x")},
                                    {Int64(0), Int64(1), String("y")},
                                    {Int64(0), Int64(2), String("z")},
                                    {Int64(1), Int64(10), String("c")},
                                    {Int64(1), Int64(10), String("b")},
                                    {Int64(1), NullInt64(), String("a")},
                                    {Int64(1), NullInt64(), NullString()}}),
              /*preserves_order=*/true)),
          /*grouping_sets=*/{}));

  EXPECT_EQ(
      "AggregateOp(\n"
      "+-keys: {\n"
      "| +-$k := $a},\n"
      "+-aggregators: {\n"
      "| +-$d := ArrayAgg($b) ORDER BY $b := $b ASC LIMIT $limit0 "
      "[ignores_null = false],\n"
      "| +-$e := ArrayAgg($b) ORDER BY $b := $b ASC LIMIT $limit2 "
      "[ignores_null = false],\n"
      "| +-$f := ArrayAgg($b) ORDER BY $b := $b ASC LIMIT $limit100 "
      "[ignores_null = false],\n"
      "| +-$g := ArrayAgg($c) ORDER BY $c := $c DESC LIMIT $limit0 "
      "[ignores_null = false],\n"
      "| +-$h := ArrayAgg($c) ORDER BY $c := $c DESC LIMIT $limit2 "
      "[ignores_null = false],\n"
      "| +-$i := ArrayAgg($c) ORDER BY $c := $c DESC LIMIT $limit100 "
      "[ignores_null = false]},\n"
      "+-input: TestRelationalOp)",
      aggregate_op->DebugString());
  std::unique_ptr<TupleSchema> output_schema =
      aggregate_op->CreateOutputSchema();
  EXPECT_THAT(output_schema->variables(), ElementsAre(k, d, e, f, g, h, i));

  EvaluationContext context((EvaluationOptions()));

  TupleSchema params_schema({limit0, limit2, limit100});
  GOOGLESQL_ASSERT_OK(aggregate_op->SetSchemasForEvaluation({&params_schema}));
  TupleData params_data;

  // If the LIMIT is negative, aggregation will return an error.
  params_data = CreateTestTupleData({Int64(0), Int64(2), Int64(-100)});
  EXPECT_THAT(aggregate_op->CreateIterator({&params_data},
                                           /*num_extra_slots=*/0, &context),
              StatusIs(absl::StatusCode::kOutOfRange,
                       "Limit requires non-negative count"));

  // A case without errors.
  params_data = CreateTestTupleData({Int64(0), Int64(2), Int64(100)});
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<TupleIterator> iter,
                       aggregate_op->CreateIterator(
                           {&params_data}, /*num_extra_slots=*/1, &context));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> data,
                       ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 2);
  EXPECT_EQ(Tuple(&iter->Schema(), &data[0]).DebugString(),
            "<k:0,"
            "d:NULL,"
            "e:[1, 1],"
            "f:[1, 1, 2],"
            "g:NULL,"
            "h:[\"z\", \"y\"],"
            "i:[\"z\", \"y\", \"x\"]>");
  EXPECT_EQ(Tuple(&iter->Schema(), &data[1]).DebugString(),
            "<k:1,"
            "d:NULL,"
            "e:[NULL, NULL],"
            "f:[NULL, NULL, 10, 10],"
            "g:NULL,"
            "h:[\"c\", \"b\"],"
            "i:[\"c\", \"b\", \"a\", NULL]>");
  // Check for the extra slot.
  EXPECT_EQ(data[0].num_slots(), 8);
  EXPECT_EQ(data[1].num_slots(), 8);

  // Do it again with cancellation.
  context.ClearDeadlineAndCancellationState();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      iter, aggregate_op->CreateIterator({&params_data}, /*num_extra_slots=*/1,
                                         &context));
  GOOGLESQL_ASSERT_OK(context.CancelStatement());
  absl::Status status;
  data = ReadFromTupleIteratorFull(iter.get(), &status);
  EXPECT_TRUE(data.empty());
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kCancelled, _));

  // Check that scrambling works.
  EvaluationContext scramble_context(GetScramblingEvaluationOptions());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(iter, aggregate_op->CreateIterator({&params_data},
                                                          /*num_extra_slots=*/1,
                                                          &scramble_context));
  EXPECT_EQ(iter->DebugString(),
            "ReorderingTupleIterator(AggregationTupleIterator("
            "TestTupleIterator))");
  EXPECT_FALSE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(data, ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 2);
  EXPECT_EQ(data[0].num_slots(), 8);
  EXPECT_EQ(data[1].num_slots(), 8);

  // Check that if the memory bound is too low, we get an error.
  EvaluationContext memory_context(GetIntermediateMemoryEvaluationOptions(
      /*total_bytes=*/100));
  EXPECT_THAT(
      aggregate_op->CreateIterator({&params_data},
                                   /*num_extra_slots=*/1, &memory_context),
      StatusIs(absl::StatusCode::kResourceExhausted,
               HasSubstr("Out of memory")));
}

TEST(CreateIteratorTest, AggregateHaving) {
  TypeFactory type_factory;
  VariableId a("a"), b("b"), c("c"), d("d"), e("e"), k("k");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_c_for_d, DerefExpr::Create(c, StringType()));

  std::vector<std::unique_ptr<ValueExpr>> args_for_d;
  args_for_d.push_back(std::move(deref_c_for_d));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto having_for_d, DerefExpr::Create(b, Int64Type()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto agg_d,
      AggregateArg::Create(
          d,
          std::make_unique<BuiltinAggregateFunction>(
              FunctionKind::kArrayAgg, StringArrayType(),
              /*num_input_fields=*/1, StringType(), false /* ignores_null */),
          std::move(args_for_d), AggregateArg::kAll, std::move(having_for_d),
          AggregateArg::kHavingMax));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_c_for_e, DerefExpr::Create(c, StringType()));

  std::vector<std::unique_ptr<ValueExpr>> args_for_e;
  args_for_e.push_back(std::move(deref_c_for_e));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto having_for_e, DerefExpr::Create(b, Int64Type()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto agg_e,
      AggregateArg::Create(
          e,
          std::make_unique<BuiltinAggregateFunction>(
              FunctionKind::kArrayAgg, StringArrayType(),
              /*num_input_fields=*/1, StringType(), false /* ignores_null */),
          std::move(args_for_e), AggregateArg::kAll, std::move(having_for_e),
          AggregateArg::kHavingMin));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_a, DerefExpr::Create(a, Int64Type()));

  std::vector<std::unique_ptr<KeyArg>> keys;
  keys.push_back(std::make_unique<KeyArg>(k, std::move(deref_a)));

  std::vector<std::unique_ptr<AggregateArg>> aggregators;
  aggregators.push_back(std::move(agg_d));
  aggregators.push_back(std::move(agg_e));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto aggregate_op,
      AggregateOp::Create(
          std::move(keys), std::move(aggregators),
          absl::WrapUnique(new TestRelationalOp(
              {a, b, c},
              CreateTestTupleDatas({{Int64(0), Int64(1), String("x")},
                                    {Int64(0), Int64(1), String("y")},
                                    {Int64(0), Int64(2), String("z")},
                                    {Int64(1), Int64(5), String("n")},
                                    {Int64(1), Int64(10), String("c")},
                                    {Int64(1), Int64(10), String("b")},
                                    {Int64(1), NullInt64(), String("a")},
                                    {Int64(1), NullInt64(), NullString()}}),
              /*preserves_order=*/true)),
          /*grouping_sets=*/{}));

  EXPECT_EQ(
      "AggregateOp(\n"
      "+-keys: {\n"
      "| +-$k := $a},\n"
      "+-aggregators: {\n"
      "| +-$d := ArrayAgg($c) HAVING MAX $b [ignores_null = false],\n"
      "| +-$e := ArrayAgg($c) HAVING MIN $b [ignores_null = false]},\n"
      "+-input: TestRelationalOp)",
      aggregate_op->DebugString());
  std::unique_ptr<TupleSchema> output_schema =
      aggregate_op->CreateOutputSchema();
  EXPECT_THAT(output_schema->variables(), ElementsAre(k, d, e));
  GOOGLESQL_ASSERT_OK(aggregate_op->SetSchemasForEvaluation(EmptyParamsSchemas()));

  EvaluationContext context((EvaluationOptions()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<TupleIterator> iter,
                       aggregate_op->CreateIterator(
                           EmptyParams(), /*num_extra_slots=*/1, &context));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> data,
                       ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 2);
  EXPECT_EQ(Tuple(&iter->Schema(), &data[0]).DebugString(),
            "<k:0,d:[\"z\"],e:[\"x\", \"y\"]>");
  EXPECT_EQ(Tuple(&iter->Schema(), &data[1]).DebugString(),
            "<k:1,d:[\"c\", \"b\"],e:[\"n\"]>");
  // Check for the extra slot.
  EXPECT_EQ(data[0].num_slots(), 4);
  EXPECT_EQ(data[1].num_slots(), 4);

  // Do it again with cancellation.
  context.ClearDeadlineAndCancellationState();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      iter, aggregate_op->CreateIterator(EmptyParams(), /*num_extra_slots=*/1,
                                         &context));
  GOOGLESQL_ASSERT_OK(context.CancelStatement());
  absl::Status status;
  data = ReadFromTupleIteratorFull(iter.get(), &status);
  EXPECT_TRUE(data.empty());
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kCancelled, _));

  // Check that scrambling works.
  EvaluationContext scramble_context(GetScramblingEvaluationOptions());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(iter, aggregate_op->CreateIterator(EmptyParams(),
                                                          /*num_extra_slots=*/1,
                                                          &scramble_context));
  EXPECT_EQ(iter->DebugString(),
            "ReorderingTupleIterator(AggregationTupleIterator("
            "TestTupleIterator))");
  EXPECT_FALSE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(data, ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 2);
  EXPECT_EQ(data[0].num_slots(), 4);
  EXPECT_EQ(data[1].num_slots(), 4);

  // Check that if the memory bound is too low, we get an error.
  EvaluationContext memory_context(GetIntermediateMemoryEvaluationOptions(
      /*total_bytes=*/100));
  EXPECT_THAT(
      aggregate_op->CreateIterator(EmptyParams(),
                                   /*num_extra_slots=*/1, &memory_context),
      StatusIs(absl::StatusCode::kResourceExhausted,
               HasSubstr("Out of memory")));
}

TEST(EvalAggTest, ArrayAggWithLimitNonDeterministic) {
  TypeFactory type_factory;
  VariableId a("a"), b("b"), d("d"), k("k");

  std::vector<std::unique_ptr<KeyArg>> empty_order_by_keys;

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_b_for_d, DerefExpr::Create(b, Int64Type()));

  std::vector<std::unique_ptr<ValueExpr>> args_for_d;
  args_for_d.push_back(std::move(deref_b_for_d));

  // Create a limit expression with value 2.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto limit_for_d, ConstExpr::Create(Value::Int64(2)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto agg_d,
      AggregateArg::Create(
          d,
          std::make_unique<BuiltinAggregateFunction>(
              FunctionKind::kArrayAgg, Int64ArrayType(),
              /*num_input_fields=*/1, Int64Type(), false /* ignores_null */),
          std::move(args_for_d), AggregateArg::kAll, nullptr /* having_expr */,
          AggregateArg::kHavingNone, std::move(empty_order_by_keys),
          std::move(limit_for_d)));

  std::vector<std::unique_ptr<AggregateArg>> aggregators;
  aggregators.push_back(std::move(agg_d));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_a, DerefExpr::Create(a, Int64Type()));
  std::vector<std::unique_ptr<KeyArg>> keys;
  keys.push_back(std::make_unique<KeyArg>(k, std::move(deref_a)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto aggregate_op,
      AggregateOp::Create(std::move(keys), std::move(aggregators),
                          absl::WrapUnique(new TestRelationalOp(
                              {a, b},
                              CreateTestTupleDatas({{Int64(0), Int64(2)},
                                                    {Int64(0), Int64(1)},
                                                    {Int64(0), Int64(-2)},
                                                    {Int64(0), Int64(3)},
                                                    {Int64(0), Int64(-2)},
                                                    {Int64(0), Int64(1)},
                                                    {Int64(0), Int64(2)}}),
                              /*preserves_order=*/true)),
                          /*grouping_sets=*/{}));

  EXPECT_EQ(
      "AggregateOp(\n"
      "+-keys: {\n"
      "| +-$k := $a},\n"
      "+-aggregators: {\n"
      "| +-$d := ArrayAgg($b) LIMIT ConstExpr(2) "
      "[ignores_null = false]},\n"
      "+-input: TestRelationalOp)",
      aggregate_op->DebugString());
  std::unique_ptr<TupleSchema> output_schema =
      aggregate_op->CreateOutputSchema();
  EXPECT_THAT(output_schema->variables(), ElementsAre(k, d));

  EvaluationContext context((EvaluationOptions()));

  // Return result as array of structs.
  auto struct_type =
      MakeStructType({{"key", Int64Type()}, {"val", Int64ArrayType()}});
  auto array_type = MakeArrayType(struct_type);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_k, DerefExpr::Create(k, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_d, DerefExpr::Create(d, Int64ArrayType()));

  std::vector<std::unique_ptr<ExprArg>> args_for_struct;
  args_for_struct.push_back(std::make_unique<ExprArg>(std::move(deref_k)));
  args_for_struct.push_back(std::make_unique<ExprArg>(std::move(deref_d)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto struct_expr,
      NewStructExpr::Create(struct_type, std::move(args_for_struct)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto nest_op,
                       ArrayNestExpr::Create(array_type, std::move(struct_expr),
                                             std::move(aggregate_op),
                                             /*is_with_table=*/false));
  GOOGLESQL_ASSERT_OK(nest_op->SetSchemasForEvaluation(EmptyParamsSchemas()));

  // Use the reference implementation to evaluate the result.
  TupleSlot slot;
  absl::Status status;
  ASSERT_TRUE(nest_op->EvalSimple(EmptyParams(), &context, &slot, &status))
      << status;
  const Value& reference = slot.value();

  auto expected =
      StructArray({"key", "val"}, {{Int64(0), Array({Int64(2), Int64(1)})}});
  EXPECT_THAT(expected, EqualsValue(reference));
  EXPECT_FALSE(context.IsDeterministicOutput());
}

TEST(EvalAggTest, ArrayAggWithLimitDeterministic) {
  // Exercises the same logic as ArrayAggWithLimitNonDeterministic except that
  // an Order By clause is present, which makes inputs_in_defined_order=true in
  // LimitAccumulator, and consequently the non-deterministic flag is not set.
  TypeFactory type_factory;
  VariableId a("a"), b("b"), d("d"), k("k");

  std::vector<std::unique_ptr<KeyArg>> order_by_keys;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_b, DerefExpr::Create(b, types::Int64Type()));
  order_by_keys.push_back(
      std::make_unique<KeyArg>(b, std::move(deref_b), KeyArg::kAscending));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_b_for_d, DerefExpr::Create(b, Int64Type()));

  std::vector<std::unique_ptr<ValueExpr>> args_for_d;
  args_for_d.push_back(std::move(deref_b_for_d));

  // Create a limit expression with value 2.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto limit_for_d, ConstExpr::Create(Value::Int64(2)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto agg_d,
      AggregateArg::Create(
          d,
          std::make_unique<BuiltinAggregateFunction>(
              FunctionKind::kArrayAgg, Int64ArrayType(),
              /*num_input_fields=*/1, Int64Type(), false /* ignores_null */),
          std::move(args_for_d), AggregateArg::kAll, nullptr /* having_expr */,
          AggregateArg::kHavingNone, std::move(order_by_keys),
          std::move(limit_for_d)));

  std::vector<std::unique_ptr<AggregateArg>> aggregators;
  aggregators.push_back(std::move(agg_d));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_a, DerefExpr::Create(a, Int64Type()));
  std::vector<std::unique_ptr<KeyArg>> keys;
  keys.push_back(std::make_unique<KeyArg>(k, std::move(deref_a)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto aggregate_op,
      AggregateOp::Create(std::move(keys), std::move(aggregators),
                          absl::WrapUnique(new TestRelationalOp(
                              {a, b},
                              CreateTestTupleDatas({{Int64(0), Int64(2)},
                                                    {Int64(0), Int64(1)},
                                                    {Int64(0), Int64(-2)},
                                                    {Int64(0), Int64(3)},
                                                    {Int64(0), Int64(-2)},
                                                    {Int64(0), Int64(1)},
                                                    {Int64(0), Int64(2)}}),
                              /*preserves_order=*/true)),
                          /*grouping_sets=*/{}));

  EXPECT_EQ(
      "AggregateOp(\n"
      "+-keys: {\n"
      "| +-$k := $a},\n"
      "+-aggregators: {\n"
      "| +-$d := ArrayAgg($b) ORDER BY $b := $b ASC LIMIT ConstExpr(2) "
      "[ignores_null = false]},\n"
      "+-input: TestRelationalOp)",
      aggregate_op->DebugString());
  std::unique_ptr<TupleSchema> output_schema =
      aggregate_op->CreateOutputSchema();
  EXPECT_THAT(output_schema->variables(), ElementsAre(k, d));

  EvaluationContext context((EvaluationOptions()));

  // Return result as array of structs.
  auto struct_type =
      MakeStructType({{"key", Int64Type()}, {"val", Int64ArrayType()}});
  auto array_type = MakeArrayType(struct_type);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_k, DerefExpr::Create(k, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_d, DerefExpr::Create(d, Int64ArrayType()));

  std::vector<std::unique_ptr<ExprArg>> args_for_struct;
  args_for_struct.push_back(std::make_unique<ExprArg>(std::move(deref_k)));
  args_for_struct.push_back(std::make_unique<ExprArg>(std::move(deref_d)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto struct_expr,
      NewStructExpr::Create(struct_type, std::move(args_for_struct)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto nest_op,
                       ArrayNestExpr::Create(array_type, std::move(struct_expr),
                                             std::move(aggregate_op),
                                             /*is_with_table=*/false));
  GOOGLESQL_ASSERT_OK(nest_op->SetSchemasForEvaluation(EmptyParamsSchemas()));

  // Use the reference implementation to evaluate the result.
  TupleSlot slot;
  absl::Status status;
  ASSERT_TRUE(nest_op->EvalSimple(EmptyParams(), &context, &slot, &status))
      << status;
  const Value& reference = slot.value();

  auto expected =
      StructArray({"key", "val"}, {{Int64(0), Array({Int64(-2), Int64(-2)})}});
  EXPECT_THAT(expected, EqualsValue(reference));
  EXPECT_TRUE(context.IsDeterministicOutput());
}

TEST(EvalAggTest, GroupingSetTest) {
  TypeFactory type_factory;

  // The following code builds an AggregateOp for the query
  // "SELECT key, value, COUNT(*) FROM KeyValue GROUP BY GROUPING SETS(key,
  // value)"
  // Build group-by keys
  VariableId col_key("col_key"), col_value("col_value"), key("key"),
      value("value"), agg_count("count");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_col_key,
                       DerefExpr::Create(col_key, types::Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_col_value,
                       DerefExpr::Create(col_value, types::StringType()));
  std::vector<std::unique_ptr<KeyArg>> keys;
  keys.push_back(std::make_unique<KeyArg>(key, std::move(deref_col_key)));
  keys.push_back(std::make_unique<KeyArg>(value, std::move(deref_col_value)));

  // Build aggregator count(*)
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto agg,
      AggregateArg::Create(agg_count,
                           std::make_unique<BuiltinAggregateFunction>(
                               FunctionKind::kCount, Int64Type(),
                               /*num_input_fields=*/0, EmptyStructType(),
                               /*ignores_null=*/true),
                           {}));
  std::vector<std::unique_ptr<AggregateArg>> aggregators;
  aggregators.push_back(std::move(agg));

  // Build the AggregateOp.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto aggregate_op,
      AggregateOp::Create(std::move(keys), std::move(aggregators),
                          absl::WrapUnique(new TestRelationalOp(
                              {col_key, col_value},
                              CreateTestTupleDatas({{Int64(1), String("a")},
                                                    {Int64(2), String("b")},
                                                    {Int64(1), String("a")},
                                                    {Int64(2), NullString()}}),
                              /*preserves_order=*/true)),
                          /*grouping_sets=*/{1, 2}));

  // Valid the AggregateOp debug string.
  EXPECT_EQ(
      "AggregateOp(\n"
      "+-keys: {\n"
      "| +-$key := $col_key,\n"
      "| +-$value := $col_value},\n"
      "+-aggregators: {\n"
      "| +-$count := Count()},\n"
      "+-input: TestRelationalOp\n"
      "+-grouping_sets: [0x1,0x2])",
      aggregate_op->DebugString());

  // TODO: Abstract the following execution code to a method and
  // remove redundancy in all tests.
  // Build the output schema
  std::unique_ptr<TupleSchema> output_schema =
      aggregate_op->CreateOutputSchema();
  EXPECT_THAT(output_schema->variables(), ElementsAre(key, value, agg_count));

  EvaluationContext context((EvaluationOptions()));

  // Return result as array of structs.
  auto struct_type = MakeStructType(
      {{"key", Int64Type()}, {"value", StringType()}, {"count", Int64Type()}});
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_key, DerefExpr::Create(key, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_value,
                       DerefExpr::Create(value, StringType()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_count,
                       DerefExpr::Create(agg_count, Int64Type()));

  // Build the result struct expression
  std::vector<std::unique_ptr<ExprArg>> args_for_struct;
  args_for_struct.push_back(std::make_unique<ExprArg>(std::move(deref_key)));
  args_for_struct.push_back(std::make_unique<ExprArg>(std::move(deref_value)));
  args_for_struct.push_back(std::make_unique<ExprArg>(std::move(deref_count)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto struct_expr,
      NewStructExpr::Create(struct_type, std::move(args_for_struct)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto nest_op,
      ArrayNestExpr::Create(MakeArrayType(struct_type), std::move(struct_expr),
                            std::move(aggregate_op),
                            /*is_with_table=*/false));
  GOOGLESQL_ASSERT_OK(nest_op->SetSchemasForEvaluation(EmptyParamsSchemas()));

  // Use the reference implementation to evaluate the result.
  TupleSlot slot;
  absl::Status status;
  ASSERT_TRUE(nest_op->EvalSimple(EmptyParams(), &context, &slot, &status))
      << status;
  const Value& reference = slot.value();

  auto expected = StructArray({"key", "value", "count"},
                              {
                                  {NullInt64(), NullString(), Int64(1)},
                                  {NullInt64(), String("a"), Int64(2)},
                                  {NullInt64(), String("b"), Int64(1)},
                                  {Int64(1), NullString(), Int64(2)},
                                  {Int64(2), NullString(), Int64(2)},
                              });
  EXPECT_THAT(reference, EqualsValue(expected));
}

TEST(EvalAggTest, GroupingFunctionTest) {
  TypeFactory type_factory;

  // The following code builds an AggregateOp for the query
  // "SELECT key, value, COUNT(*), GROUPING(key), GROUPING(value)
  // FROM KeyValue GROUP BY GROUPING SETS(key, value)"
  // Build group-by keys
  VariableId col_key("col_key"), col_value("col_value"), key("key"),
      value("value"), agg_count("count"), agg_grouping_key("grouping_key"),
      agg_grouping_value("grouping_value");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_col_key,
                       DerefExpr::Create(col_key, types::Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_col_value,
                       DerefExpr::Create(col_value, types::StringType()));
  std::vector<std::unique_ptr<KeyArg>> keys;
  keys.push_back(std::make_unique<KeyArg>(key, std::move(deref_col_key)));
  keys.push_back(std::make_unique<KeyArg>(value, std::move(deref_col_value)));

  // Build aggregator count(*)
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto agg1,
      AggregateArg::Create(agg_count,
                           std::make_unique<BuiltinAggregateFunction>(
                               FunctionKind::kCount, Int64Type(),
                               /*num_input_fields=*/0, EmptyStructType()),
                           {}));

  // Build aggregator grouping(key)
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<ValueExpr> grouping_key_arg,
                       ConstExpr::Create(Value::Int64(0)));
  std::vector<std::unique_ptr<ValueExpr>> grouping_key_args;
  grouping_key_args.push_back(std::move(grouping_key_arg));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto agg2,
      AggregateArg::Create(agg_grouping_key,
                           std::make_unique<BuiltinAggregateFunction>(
                               FunctionKind::kGrouping, Int64Type(),
                               /*num_input_fields=*/1, Int64Type()),
                           std::move(grouping_key_args)));

  // Build aggregator grouping(value)
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<ValueExpr> grouping_value_arg,
                       ConstExpr::Create(Value::Int64(1)));
  std::vector<std::unique_ptr<ValueExpr>> grouping_value_args;
  grouping_value_args.push_back(std::move(grouping_value_arg));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto agg3,
      AggregateArg::Create(agg_grouping_value,
                           std::make_unique<BuiltinAggregateFunction>(
                               FunctionKind::kGrouping, Int64Type(),
                               /*num_input_fields=*/1, Int64Type()),
                           std::move(grouping_value_args)));
  std::vector<std::unique_ptr<AggregateArg>> aggregators;
  aggregators.push_back(std::move(agg1));
  aggregators.push_back(std::move(agg2));
  aggregators.push_back(std::move(agg3));

  // Build the AggregateOp.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto aggregate_op,
      AggregateOp::Create(std::move(keys), std::move(aggregators),
                          absl::WrapUnique(new TestRelationalOp(
                              {col_key, col_value},
                              CreateTestTupleDatas({{Int64(1), String("a")},
                                                    {Int64(2), String("b")},
                                                    {Int64(1), String("a")},
                                                    {Int64(2), NullString()}}),
                              /*preserves_order=*/true)),
                          /*grouping_sets=*/{1, 2}));

  // Valid the AggregateOp debug string.
  EXPECT_EQ(
      "AggregateOp(\n"
      "+-keys: {\n"
      "| +-$key := $col_key,\n"
      "| +-$value := $col_value},\n"
      "+-aggregators: {\n"
      "| +-$count := Count(),\n"
      "| +-$grouping_key := Grouping(ConstExpr(0)),\n"
      "| +-$grouping_value := Grouping(ConstExpr(1))},\n"
      "+-input: TestRelationalOp\n"
      "+-grouping_sets: [0x1,0x2])",
      aggregate_op->DebugString());

  // TODO: Abstract the following execution code to a method and
  // remove redundancy in all tests.
  // Build the output schema
  std::unique_ptr<TupleSchema> output_schema =
      aggregate_op->CreateOutputSchema();
  EXPECT_THAT(
      output_schema->variables(),
      ElementsAre(key, value, agg_count, agg_grouping_key, agg_grouping_value));

  EvaluationContext context((EvaluationOptions()));

  // Return result as array of structs.
  auto struct_type = MakeStructType({{"key", Int64Type()},
                                     {"value", StringType()},
                                     {"count", Int64Type()},
                                     {"grouping_key", Int64Type()},
                                     {"grouping_value", Int64Type()}});
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_key, DerefExpr::Create(key, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_value,
                       DerefExpr::Create(value, StringType()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_count,
                       DerefExpr::Create(agg_count, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_grouping_key,
                       DerefExpr::Create(agg_grouping_key, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_grouping_value,
                       DerefExpr::Create(agg_grouping_value, Int64Type()));

  // Build the result struct expression
  std::vector<std::unique_ptr<ExprArg>> args_for_struct;
  args_for_struct.push_back(std::make_unique<ExprArg>(std::move(deref_key)));
  args_for_struct.push_back(std::make_unique<ExprArg>(std::move(deref_value)));
  args_for_struct.push_back(std::make_unique<ExprArg>(std::move(deref_count)));
  args_for_struct.push_back(
      std::make_unique<ExprArg>(std::move(deref_grouping_key)));
  args_for_struct.push_back(
      std::make_unique<ExprArg>(std::move(deref_grouping_value)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto struct_expr,
      NewStructExpr::Create(struct_type, std::move(args_for_struct)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto nest_op,
      ArrayNestExpr::Create(MakeArrayType(struct_type), std::move(struct_expr),
                            std::move(aggregate_op),
                            /*is_with_table=*/false));
  GOOGLESQL_ASSERT_OK(nest_op->SetSchemasForEvaluation(EmptyParamsSchemas()));

  // Use the reference implementation to evaluate the result.
  TupleSlot slot;
  absl::Status status;
  ASSERT_TRUE(nest_op->EvalSimple(EmptyParams(), &context, &slot, &status))
      << status;
  const Value& reference = slot.value();

  auto expected =
      StructArray({"key", "value", "count", "grouping_key", "grouping_value"},
                  {
                      {NullInt64(), NullString(), Int64(1), Int64(1), Int64(0)},
                      {NullInt64(), String("a"), Int64(2), Int64(1), Int64(0)},
                      {NullInt64(), String("b"), Int64(1), Int64(1), Int64(0)},
                      {Int64(1), NullString(), Int64(2), Int64(0), Int64(1)},
                      {Int64(2), NullString(), Int64(2), Int64(0), Int64(1)},
                  });
  EXPECT_THAT(reference, EqualsValue(expected));
}

}  // namespace
}  // namespace googlesql
