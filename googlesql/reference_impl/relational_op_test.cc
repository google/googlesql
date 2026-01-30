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

// Tests of relational operators that don't warrant their own files.

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/common/evaluator_test_table.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/common/thread_stack.h"
#include "googlesql/public/evaluator_table_iterator.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/functions/array_zip_mode.pb.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/type.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/reference_impl/evaluation.h"
#include "googlesql/reference_impl/function.h"
#include "googlesql/reference_impl/operator.h"
#include "googlesql/reference_impl/test_relational_op.h"
#include "googlesql/reference_impl/tuple.h"
#include "googlesql/reference_impl/tuple_test_util.h"
#include "googlesql/reference_impl/variable_id.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "googlesql/testing/test_value.h"
#include "googlesql/testing/using_test_value.cc"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/flags/flag.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"
#include "googlesql/base/clock.h"

using ::std::nullopt;

using ::testing::_;
using ::testing::ContainsRegex;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsNull;
using ::testing::Pointee;
using ::testing::PrintToString;
using ::testing::SizeIs;
using ::testing::StrEq;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;

extern absl::Flag<int64_t>
    FLAGS_googlesql_simple_iterator_call_time_now_rows_period;

namespace googlesql {

using SharedProtoState = TupleSlot::SharedProtoState;

// Teach googletest how to print ColumnFilters.
void PrintTo(const ColumnFilter& filter, std::ostream* os) {
  switch (filter.kind()) {
    case ColumnFilter::kRange:
      *os << "<lower_bound: " << filter.lower_bound()
          << " upper_bound: " << filter.upper_bound() << ">";
      break;
    case ColumnFilter::kInList:
      *os << "<in_list: " << PrintToString(filter.in_list()) << ">";
      break;
    default:
      *os << "Unsupported ColumnFilter Kind";
      break;
  }
}

namespace {

static const auto DEFAULT_ERROR_MODE =
    ResolvedFunctionCallBase::DEFAULT_ERROR_MODE;

// For readability.
std::vector<const TupleSchema*> EmptyParamsSchemas() { return {}; }
std::vector<const TupleData*> EmptyParams() { return {}; }

std::unique_ptr<ScalarFunctionBody> CreateFunction(FunctionKind kind,
                                                   const Type* output_type) {
  LanguageOptions language_options;
  language_options.EnableMaximumLanguageFeaturesForDevelopment();
  return BuiltinScalarFunction::CreateValidated(kind, language_options,
                                                output_type, {})
      .value();
}

absl::StatusOr<std::unique_ptr<ExprArg>> AssignValueToVar(VariableId var,
                                                          const Value& value) {
  GOOGLESQL_ASSIGN_OR_RETURN(auto const_expr, ConstExpr::Create(value));
  return std::make_unique<ExprArg>(var, std::move(const_expr));
}

// Convenience method to produce an ExprArg representing "result = v1 + v2".
//
// All operands should have type <type>, which will be the type of the result
// as well.
absl::StatusOr<std::unique_ptr<ExprArg>> ComputeSum(VariableId v1,
                                                    VariableId v2,
                                                    VariableId result,
                                                    const Type* type) {
  std::vector<std::unique_ptr<ValueExpr>> add_args(2);
  GOOGLESQL_ASSIGN_OR_RETURN(add_args[0], DerefExpr::Create(v1, type));
  GOOGLESQL_ASSIGN_OR_RETURN(add_args[1], DerefExpr::Create(v2, type));
  GOOGLESQL_ASSIGN_OR_RETURN(auto add_expr, ScalarFunctionCallExpr::Create(
                                      CreateFunction(FunctionKind::kAdd, type),
                                      std::move(add_args)));
  return std::make_unique<ExprArg>(result, std::move(add_expr));
}

// Convience method to produce an ExprArg representing "result = v1 + v2".
// This is similar to the above method, except the 2nd operand is a constant,
// rather than a variable.
absl::StatusOr<std::unique_ptr<ExprArg>> ComputeSum(VariableId v1, Value v2,
                                                    VariableId result) {
  std::vector<std::unique_ptr<ValueExpr>> add_args(2);
  GOOGLESQL_ASSIGN_OR_RETURN(add_args[0], DerefExpr::Create(v1, v2.type()));
  GOOGLESQL_ASSIGN_OR_RETURN(add_args[1], ConstExpr::Create(v2));
  GOOGLESQL_ASSIGN_OR_RETURN(
      auto add_expr,
      ScalarFunctionCallExpr::Create(
          CreateFunction(FunctionKind::kAdd, v2.type()), std::move(add_args)));
  return std::make_unique<ExprArg>(result, std::move(add_expr));
}

// Returns a RelationalOp representing <input>, filtered to include rows only
// where <var> has value less than <value>. <var> and <value> must be the same
// type.
absl::StatusOr<std::unique_ptr<RelationalOp>> FilterLessThan(
    VariableId var, Value value, std::unique_ptr<RelationalOp> input) {
  std::vector<std::unique_ptr<ValueExpr>> less_than_args(2);
  GOOGLESQL_ASSIGN_OR_RETURN(less_than_args[0], DerefExpr::Create(var, value.type()));
  GOOGLESQL_ASSIGN_OR_RETURN(less_than_args[1], ConstExpr::Create(value));
  GOOGLESQL_ASSIGN_OR_RETURN(auto predicate,
                   ScalarFunctionCallExpr::Create(
                       CreateFunction(FunctionKind::kLess, BoolType()),
                       std::move(less_than_args), DEFAULT_ERROR_MODE));
  return FilterOp::Create(std::move(predicate), std::move(input));
}

// Test fixture for implementations of RelationalOp::CreateIterator.
class CreateIteratorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    TypeFactory* type_factory = test_values::static_type_factory();
    GOOGLESQL_ASSERT_OK(type_factory->MakeProtoType(
        googlesql_test::KitchenSinkPB::descriptor(), &proto_type_));
    GOOGLESQL_ASSERT_OK(type_factory->MakeArrayType(proto_type_, &proto_array_type_));
  }

  Value GetProtoValue(int i) const {
    googlesql_test::KitchenSinkPB proto;
    proto.set_int64_key_1(i);
    proto.set_int64_key_2(10 * i);

    absl::Cord bytes;
    ABSL_CHECK(proto.SerializeToCord(&bytes));
    return Value::Proto(proto_type_, bytes);
  }

  const ProtoType* proto_type_ = nullptr;
  const ArrayType* proto_array_type_ = nullptr;
};

TEST_F(CreateIteratorTest, TestRelationalOp) {
  std::vector<TupleData> tuples;
  std::vector<std::vector<const SharedProtoState*>> shared_states;
  for (int i = 0; i < 5; ++i) {
    TupleData tuple(/*num_slots=*/2);
    shared_states.emplace_back();
    for (int slot_idx = 0; slot_idx < 2; ++slot_idx) {
      TupleSlot* slot = tuple.mutable_slot(slot_idx);
      slot->SetValue(GetProtoValue(i));
      shared_states.back().push_back(slot->mutable_shared_proto_state()->get());
    }
    tuples.push_back(tuple);
  }

  VariableId foo("foo"), bar("bar");
  TestRelationalOp op({foo, bar}, tuples,
                      /*preserves_order=*/true);
  std::unique_ptr<TupleSchema> output_schema = op.CreateOutputSchema();
  EXPECT_THAT(output_schema->variables(), ElementsAre(foo, bar));

  EvaluationContext context((EvaluationOptions()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> iter,
      op.CreateIterator(EmptyParams(), /*num_extra_slots=*/5, &context));
  EXPECT_EQ(iter->DebugString(), "TestTupleIterator");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> output_tuples,
                       ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(output_tuples.size(), tuples.size());
  for (int i = 0; i < tuples.size(); ++i) {
    const TupleData& expected = tuples[i];
    const TupleData& actual = output_tuples[i];
    ASSERT_EQ(expected.num_slots() + 5, actual.num_slots());
    for (int j = 0; j < expected.num_slots(); ++j) {
      const TupleSlot& actual_slot = actual.slot(j);
      EXPECT_EQ(expected.slot(j).value(), actual_slot.value());

      const SharedProtoState* shared_state = shared_states[i][j];
      EXPECT_TRUE(actual_slot.mutable_shared_proto_state()->get() ==
                  shared_state);
    }
  }
}

using EvalRelationalOpTest = CreateIteratorTest;

// Tests TestRelationalOp::Eval (which is just RelationalOp::Eval).
TEST_F(EvalRelationalOpTest, TestRelationalOp) {
  std::vector<TupleData> tuples;
  std::vector<std::vector<const SharedProtoState*>> shared_states;
  for (int i = 0; i < 5; ++i) {
    TupleData tuple(/*num_slots=*/2);
    shared_states.emplace_back();
    for (int slot_idx = 0; slot_idx < 2; ++slot_idx) {
      TupleSlot* slot = tuple.mutable_slot(slot_idx);
      slot->SetValue(GetProtoValue(i));
      shared_states.back().push_back(slot->mutable_shared_proto_state()->get());
    }
    tuples.push_back(tuple);
  }

  VariableId foo("foo"), bar("bar");
  TestRelationalOp op({foo, bar}, tuples,
                      /*preserves_order=*/true);
  std::unique_ptr<TupleSchema> output_schema = op.CreateOutputSchema();
  EXPECT_THAT(output_schema->variables(), ElementsAre(foo, bar));

  EvaluationContext context((EvaluationOptions()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<TupleIterator> iter,
                       op.Eval(EmptyParams(), /*num_extra_slots=*/5, &context));
  EXPECT_EQ(iter->DebugString(),
            "PassThroughTupleIterator(Factory for TestTupleIterator)");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> output_tuples,
                       ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(output_tuples.size(), tuples.size());
  for (int i = 0; i < tuples.size(); ++i) {
    const TupleData& expected = tuples[i];
    const TupleData& actual = output_tuples[i];
    ASSERT_EQ(expected.num_slots() + 5, actual.num_slots());
    for (int j = 0; j < expected.num_slots(); ++j) {
      const TupleSlot& actual_slot = actual.slot(j);
      EXPECT_EQ(expected.slot(j).value(), actual_slot.value());

      const SharedProtoState* shared_state = shared_states[i][j];
      EXPECT_TRUE(actual_slot.mutable_shared_proto_state()->get() ==
                  shared_state);
      EXPECT_TRUE(!shared_state->has_value());
    }
  }
}

// A test builtin TVF that
// 1. accepts a STRING arg representing a table name and produces the rows of
// that table (STRING -> ANY TABLE).
// 2. accepts a TABLE and returns it (ANY TABLE -> ANY TABLE).
class BasicTestTVF : public BuiltinTableValuedFunction {
  class ReferenceResultIterator : public EvaluatorTableIterator {
   public:
    explicit ReferenceResultIterator(const Value& result)
        : rows_(result.elements()), row_idx_(-1) {
      auto returning_row_struct = result.type()->AsArray()->element_type();
      fields_ = returning_row_struct->AsStruct()->fields();
    }

    int NumColumns() const override { return static_cast<int>(fields_.size()); }

    std::string GetColumnName(int i) const override { return fields_[i].name; }

    const Type* GetColumnType(int i) const override { return fields_[i].type; }

    const Value& GetValue(int i) const override {
      return rows_[row_idx_].field(i);
    }

    bool NextRow() override { return ++row_idx_ < rows_.size(); }

    absl::Status Status() const override { return absl::OkStatus(); }
    absl::Status Cancel() override { return absl::OkStatus(); }

   private:
    // The content of the iterator.
    const std::vector<Value> rows_;
    // The struct field of column name and type in this result table.
    std::vector<StructField> fields_;
    // The positional index of the current row in the row vector.
    int row_idx_;
  };

 public:
  explicit BasicTestTVF(FunctionKind kind) : BuiltinTableValuedFunction(kind) {}

  absl::StatusOr<std::unique_ptr<EvaluatorTableIterator>> CreateEvaluator(
      std::vector<TableValuedFunction::TvfEvaluatorArg> args,
      std::shared_ptr<FunctionSignature> function_call_signature,
      EvaluationContext* context) override {
    GOOGLESQL_RET_CHECK_EQ(args.size(), 1);
    if (args[0].value.has_value()) {
      GOOGLESQL_RET_CHECK(args[0].value.has_value());
      GOOGLESQL_RET_CHECK(args[0].value->type()->IsString());
      return std::make_unique<ReferenceResultIterator>(
          context->GetTableAsArray(args[0].value->string_value()));
    } else if (args[0].relation != nullptr) {
      return std::move(args[0].relation);
    }
    return absl::InvalidArgumentError("Unsupported argument.");
  }

  std::string debug_name() const override { return "BasicTestTVF"; }
};

TEST(BuiltinTableValuedFunction, RelationalOpEvalTestWithValue) {
  BuiltinFunctionRegistry::RegisterTableValuedFunction(
      {FunctionKind::kInvalid},
      [](FunctionKind kind) { return new BasicTestTVF(kind); });
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<ConstExpr> exp,
                       ConstExpr::Create(Value::StringValue("test")));
  std::vector<TvfAlgebraArgument> arguments;
  arguments.push_back({.value = std::move(exp)});

  std::vector<TVFSchemaColumn> output_columns;
  output_columns.push_back(TVFSchemaColumn("col", types::StringType()));

  std::vector<VariableId> variables;
  variables.push_back(VariableId("col"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TableValuedFunctionCallExpr> call_expr,
      BuiltinTableValuedFunction::CreateCall(
          FunctionKind::kInvalid, std::move(arguments),
          std::move(output_columns), std::move(variables), nullptr));

  GOOGLESQL_ASSERT_OK(call_expr->SetSchemasForEvaluation({}));

  EvaluationContext context((EvaluationOptions()));
  Value table = Array(
      {Struct({"col"}, {String("hello")}), Struct({"col"}, {String("world")})});
  GOOGLESQL_ASSERT_OK(context.AddTableAsArray("test", false, table, LanguageOptions()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<TupleIterator> iter,
                       call_expr->CreateIterator({}, 0, &context));

  ASSERT_EQ(iter->Schema().num_variables(), 1);
  EXPECT_EQ(iter->Schema().variable(0), VariableId("col"));

  std::vector<Value> values;
  while (true) {
    TupleData* data = iter->Next();
    if (data == nullptr) {
      break;
    }
    EXPECT_EQ(data->num_slots(), 1);
    values.push_back(data->slot(0).value());
  }
  std::vector<Value> expected_values = {Value::StringValue("hello"),
                                        Value::StringValue("world")};
  EXPECT_EQ(values, expected_values);
}

TEST(BuiltinTableValuedFunction, RelationalOpEvalTestWithRelation) {
  BuiltinFunctionRegistry::RegisterTableValuedFunction(
      {FunctionKind::kInvalid},
      [](FunctionKind kind) { return new BasicTestTVF(kind); });

  std::vector<TupleData> tuples;

  // Number of slots is intentionally more than output columns to check
  // projection is working correctly.
  // First tuple
  TupleData tuple1(/*num_slots=*/2);
  TupleSlot* slot11 = tuple1.mutable_slot(0);
  slot11->SetValue(Value::StringValue("hello"));
  TupleSlot* slot12 = tuple1.mutable_slot(1);
  slot12->SetValue(Value::StringValue("world"));
  tuples.push_back(tuple1);
  // Second tuple
  TupleData tuple2(/*num_slots=*/2);
  TupleSlot* slot21 = tuple2.mutable_slot(0);
  slot21->SetValue(Value::StringValue("world"));
  TupleSlot* slot22 = tuple1.mutable_slot(1);
  slot22->SetValue(Value::StringValue("hello"));
  tuples.push_back(tuple2);

  std::vector<VariableId> variables;
  VariableId var1 = VariableId("col1");
  variables.push_back(var1);
  VariableId var2 = VariableId("col2");
  variables.push_back(var2);

  std::vector<TvfInputRelation::TvfInputRelationColumn> columns;
  columns.push_back({
      .name = "col1",
      .type = types::StringType(),
      .variable = var1,
  });
  columns.push_back({
      .name = "col2",
      .type = types::StringType(),
      .variable = var2,
  });

  std::vector<TvfAlgebraArgument> arguments;
  arguments.push_back({.relation = TvfInputRelation{
                           .relational_op = std::make_unique<TestRelationalOp>(
                               variables, tuples, /*preserves_order=*/true),
                           .columns = columns}});

  std::vector<TVFSchemaColumn> output_columns;
  output_columns.push_back(TVFSchemaColumn("col1", types::StringType()));

  std::vector<VariableId> output_variables;
  output_variables.push_back(var1);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TableValuedFunctionCallExpr> call_expr,
      BuiltinTableValuedFunction::CreateCall(
          FunctionKind::kInvalid, std::move(arguments),
          std::move(output_columns), std::move(output_variables), nullptr));

  GOOGLESQL_ASSERT_OK(call_expr->SetSchemasForEvaluation({}));

  EvaluationContext context((EvaluationOptions()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<TupleIterator> iter,
                       call_expr->CreateIterator({}, 0, &context));

  ASSERT_EQ(iter->Schema().num_variables(), 1);
  EXPECT_EQ(iter->Schema().variable(0), var1);

  std::vector<Value> values;
  while (true) {
    TupleData* data = iter->Next();
    if (data == nullptr) {
      break;
    }
    EXPECT_EQ(data->num_slots(), 1);
    values.push_back(data->slot(0).value());
  }
  std::vector<Value> expected_values = {Value::StringValue("hello"),
                                        Value::StringValue("world")};
  EXPECT_EQ(values, expected_values);
}

TEST(ColumnFilterArgTest, InArray) {
  VariableId p("p");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_p, DerefExpr::Create(p, Int64ArrayType()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto arg, InArrayColumnFilterArg::Create(
                    VariableId("foo"), /*column_idx=*/3, std::move(deref_p)));
  EXPECT_EQ(arg->column_idx(), 3);
  EXPECT_EQ(arg->DebugString(),
            "InArrayColumnFilterArg($foo, column_idx: 3, array: $p)");

  const TupleSchema params_schemas({p});
  const TupleData params_data = CreateTupleDataFromValues({Value::Array(
      DoubleArrayType(),
      {Double(10), Double(11), Double(0.0 / 0.0), Double(12), NullDouble()})});
  GOOGLESQL_ASSERT_OK(arg->SetSchemasForEvaluation({&params_schemas}));

  EvaluationContext context((EvaluationOptions()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<ColumnFilter> column_filter,
                       arg->Eval({&params_data}, &context));
  ASSERT_EQ(column_filter->kind(), ColumnFilter::kInList);
  EXPECT_THAT(column_filter->in_list(),
              ElementsAre(Double(10), Double(11), Double(12)));

  const TupleData params_null_data =
      CreateTupleDataFromValues({Null(DoubleArrayType())});
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<ColumnFilter> column_filter_null,
                       arg->Eval({&params_null_data}, &context));
  ASSERT_EQ(column_filter_null->kind(), ColumnFilter::kInList);
  EXPECT_THAT(column_filter_null->in_list(), IsEmpty());
}

TEST(ColumnFilterArgTest, InList) {
  VariableId p1("p1"), p2("p2"), p3("p3"), p4("p4"), p5("p5");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_p1, DerefExpr::Create(p1, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_p2, DerefExpr::Create(p2, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_p3, DerefExpr::Create(p3, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_p4, DerefExpr::Create(p4, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_p5, DerefExpr::Create(p5, Int64Type()));

  std::vector<std::unique_ptr<ValueExpr>> elements;
  elements.push_back(std::move(deref_p1));
  elements.push_back(std::move(deref_p2));
  elements.push_back(std::move(deref_p3));
  elements.push_back(std::move(deref_p4));
  elements.push_back(std::move(deref_p5));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto arg, InListColumnFilterArg::Create(
                    VariableId("foo"), /*column_idx=*/3, std::move(elements)));
  EXPECT_EQ(arg->column_idx(), 3);
  EXPECT_EQ(arg->DebugString(),
            "InListColumnFilterArg($foo, column_idx: 3, "
            "elements: ($p1, $p2, $p3, $p4, $p5))");

  const TupleSchema params_schemas({p1, p2, p3, p4, p5});
  const TupleData params_data = CreateTupleDataFromValues(
      {Double(10), Double(11), Double(0.0 / 0.0), Double(12), NullDouble()});
  GOOGLESQL_ASSERT_OK(arg->SetSchemasForEvaluation({&params_schemas}));

  EvaluationContext context((EvaluationOptions()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<ColumnFilter> column_filter,
                       arg->Eval({&params_data}, &context));
  ASSERT_EQ(column_filter->kind(), ColumnFilter::kInList);
  EXPECT_THAT(column_filter->in_list(),
              ElementsAre(Double(10), Double(11), Double(12)));
}

TEST(ColumnFilterArgTest, HalfUnbounded) {
  for (const HalfUnboundedColumnFilterArg::Kind kind :
       {HalfUnboundedColumnFilterArg::kLE, HalfUnboundedColumnFilterArg::kGE}) {
    ABSL_LOG(INFO) << "Testing kind: " << kind;

    VariableId p("p");
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_p, DerefExpr::Create(p, Int64Type()));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto arg, HalfUnboundedColumnFilterArg::Create(
                                       VariableId("foo"), /*column_idx=*/3,
                                       kind, std::move(deref_p)));
    EXPECT_EQ(arg->column_idx(), 3);
    switch (kind) {
      case HalfUnboundedColumnFilterArg::kLE:
        EXPECT_EQ(
            arg->DebugString(),
            "HalfUnboundedColumnFilterArg($foo, column_idx: 3, filter: <= $p)");
        break;
      case HalfUnboundedColumnFilterArg::kGE:
        EXPECT_EQ(
            arg->DebugString(),
            "HalfUnboundedColumnFilterArg($foo, column_idx: 3, filter: >= $p)");
        break;
    }

    const TupleSchema params_schemas({p});
    GOOGLESQL_ASSERT_OK(arg->SetSchemasForEvaluation({&params_schemas}));

    for (int i = 0; i < 2; ++i) {
      Value value;
      switch (i) {
        case 0:
          value = Double(10.0);
          break;
        case 1:
          value = NullDouble();
          break;
        case 2:
          value = Double(0.0 / 0.0);
          break;
        default:
          FAIL() << "Unexpected value of i: " << i;
      }
      ABSL_LOG(INFO) << "Testing value: " << value;
      const TupleData params_data = CreateTupleDataFromValues({value});

      EvaluationContext context((EvaluationOptions()));
      GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<ColumnFilter> column_filter,
                           arg->Eval({&params_data}, &context));

      if (i == 0) {
        ASSERT_EQ(column_filter->kind(), ColumnFilter::kRange);
        switch (kind) {
          case HalfUnboundedColumnFilterArg::kLE:
            EXPECT_FALSE(column_filter->lower_bound().is_valid());
            EXPECT_EQ(column_filter->upper_bound(), Double(10.0));
            break;
          case HalfUnboundedColumnFilterArg::kGE:
            EXPECT_EQ(column_filter->lower_bound(), Double(10.0));
            EXPECT_FALSE(column_filter->upper_bound().is_valid());
            break;
        }
      } else {
        ASSERT_EQ(column_filter->kind(), ColumnFilter::kInList);
        EXPECT_THAT(column_filter->in_list(), IsEmpty());
      }
    }
  }
}

MATCHER_P2(IsRangeColumnFilterWith, lower_bound, upper_bound, "") {
  if (arg.kind() != ColumnFilter::kRange) return false;
  if (lower_bound.is_valid() != arg.lower_bound().is_valid() ||
      upper_bound.is_valid() != arg.upper_bound().is_valid()) {
    return false;
  }
  if (lower_bound.is_valid() && !lower_bound.Equals(arg.lower_bound())) {
    return false;
  }
  if (upper_bound.is_valid() && !upper_bound.Equals(arg.upper_bound())) {
    return false;
  }
  return true;
}

MATCHER_P(IsListColumnFilterWith, in_list, "") {
  return arg.kind() == ColumnFilter::kInList && arg.in_list() == in_list;
}

TEST(IntersectColumnFiltersTest, OneRangeFilter) {
  std::vector<std::unique_ptr<ColumnFilter>> filters;
  filters.push_back(std::make_unique<ColumnFilter>(Int64(10), Int64(20)));
  EXPECT_THAT(
      EvaluatorTableScanOp::IntersectColumnFilters(filters),
      IsOkAndHolds(Pointee(IsRangeColumnFilterWith(Int64(10), Int64(20)))));
}

TEST(IntersectColumnFiltersTest, OneRangeFilterWithNoLowerBound) {
  std::vector<std::unique_ptr<ColumnFilter>> filters;
  filters.push_back(std::make_unique<ColumnFilter>(Value(), Int64(20)));
  EXPECT_THAT(
      EvaluatorTableScanOp::IntersectColumnFilters(filters),
      IsOkAndHolds(Pointee(IsRangeColumnFilterWith(Value(), Int64(20)))));
}

TEST(IntersectColumnFiltersTest, OneRangeFilterWithNoUpperBound) {
  std::vector<std::unique_ptr<ColumnFilter>> filters;
  filters.push_back(std::make_unique<ColumnFilter>(Int64(10), Value()));
  EXPECT_THAT(
      EvaluatorTableScanOp::IntersectColumnFilters(filters),
      IsOkAndHolds(Pointee(IsRangeColumnFilterWith(Int64(10), Value()))));
}

TEST(IntersectColumnFiltersTest, OneRangeFilterWithNoBounds) {
  std::vector<std::unique_ptr<ColumnFilter>> filters;
  filters.push_back(std::make_unique<ColumnFilter>(Value(), Value()));
  EXPECT_THAT(EvaluatorTableScanOp::IntersectColumnFilters(filters),
              IsOkAndHolds(Pointee(IsRangeColumnFilterWith(Value(), Value()))));
}

TEST(IntersectColumnFiltersTest, OneInListFilter) {
  const std::vector<Value> values = {Int64(10), Int64(20), Int64(30)};
  std::vector<std::unique_ptr<ColumnFilter>> filters;
  filters.push_back(std::make_unique<ColumnFilter>(values));
  EXPECT_THAT(EvaluatorTableScanOp::IntersectColumnFilters(filters),
              IsOkAndHolds(Pointee(IsListColumnFilterWith(values))));
}

TEST(IntersectColumnFiltersTest, TwoRangeFiltersOverlap1) {
  std::vector<std::unique_ptr<ColumnFilter>> filters;
  filters.push_back(std::make_unique<ColumnFilter>(Int64(10), Int64(20)));
  filters.push_back(std::make_unique<ColumnFilter>(Int64(15), Int64(25)));
  EXPECT_THAT(
      EvaluatorTableScanOp::IntersectColumnFilters(filters),
      IsOkAndHolds(Pointee(IsRangeColumnFilterWith(Int64(15), Int64(20)))));
}

TEST(IntersectColumnFiltersTest, TwoRangeFiltersOverlap2) {
  std::vector<std::unique_ptr<ColumnFilter>> filters;
  filters.push_back(std::make_unique<ColumnFilter>(Int64(15), Int64(25)));
  filters.push_back(std::make_unique<ColumnFilter>(Int64(10), Int64(20)));
  EXPECT_THAT(
      EvaluatorTableScanOp::IntersectColumnFilters(filters),
      IsOkAndHolds(Pointee(IsRangeColumnFilterWith(Int64(15), Int64(20)))));
}

TEST(IntersectColumnFiltersTest, TwoRangeFiltersCoincide) {
  std::vector<std::unique_ptr<ColumnFilter>> filters;
  filters.push_back(std::make_unique<ColumnFilter>(Int64(10), Int64(20)));
  filters.push_back(std::make_unique<ColumnFilter>(Int64(10), Int64(20)));
  EXPECT_THAT(
      EvaluatorTableScanOp::IntersectColumnFilters(filters),
      IsOkAndHolds(Pointee(IsRangeColumnFilterWith(Int64(10), Int64(20)))));
}

TEST(IntersectColumnFiltersTest, TwoRangeFiltersFirstInSecond) {
  std::vector<std::unique_ptr<ColumnFilter>> filters;
  filters.push_back(std::make_unique<ColumnFilter>(Int64(10), Int64(20)));
  filters.push_back(std::make_unique<ColumnFilter>(Int64(0), Int64(30)));
  EXPECT_THAT(
      EvaluatorTableScanOp::IntersectColumnFilters(filters),
      IsOkAndHolds(Pointee(IsRangeColumnFilterWith(Int64(10), Int64(20)))));
}

TEST(IntersectColumnFiltersTest, TwoRangeFiltersSecondInFirst) {
  std::vector<std::unique_ptr<ColumnFilter>> filters;
  filters.push_back(std::make_unique<ColumnFilter>(Int64(0), Int64(30)));
  filters.push_back(std::make_unique<ColumnFilter>(Int64(10), Int64(20)));
  EXPECT_THAT(
      EvaluatorTableScanOp::IntersectColumnFilters(filters),
      IsOkAndHolds(Pointee(IsRangeColumnFilterWith(Int64(10), Int64(20)))));
}

TEST(IntersectColumnFiltersTest, TwoRangeFiltersCoincideLeftEdge) {
  std::vector<std::unique_ptr<ColumnFilter>> filters;
  filters.push_back(std::make_unique<ColumnFilter>(Int64(0), Int64(30)));
  filters.push_back(std::make_unique<ColumnFilter>(Int64(0), Int64(20)));
  EXPECT_THAT(
      EvaluatorTableScanOp::IntersectColumnFilters(filters),
      IsOkAndHolds(Pointee(IsRangeColumnFilterWith(Int64(0), Int64(20)))));
}

TEST(IntersectColumnFiltersTest, TwoRangeFiltersCoincideRightEdge) {
  std::vector<std::unique_ptr<ColumnFilter>> filters;
  filters.push_back(std::make_unique<ColumnFilter>(Int64(10), Int64(30)));
  filters.push_back(std::make_unique<ColumnFilter>(Int64(20), Int64(30)));
  EXPECT_THAT(
      EvaluatorTableScanOp::IntersectColumnFilters(filters),
      IsOkAndHolds(Pointee(IsRangeColumnFilterWith(Int64(20), Int64(30)))));
}

TEST(IntersectColumnFiltersTest, TwoRangeFiltersDisjoint) {
  std::vector<std::unique_ptr<ColumnFilter>> filters;
  filters.push_back(std::make_unique<ColumnFilter>(Int64(10), Int64(20)));
  filters.push_back(std::make_unique<ColumnFilter>(Int64(30), Int64(40)));
  EXPECT_THAT(
      EvaluatorTableScanOp::IntersectColumnFilters(filters),
      IsOkAndHolds(Pointee(IsListColumnFilterWith(std::vector<Value>()))));
}

TEST(IntersectColumnFiltersTest, TwoInLists) {
  std::vector<std::unique_ptr<ColumnFilter>> filters;
  filters.push_back(std::make_unique<ColumnFilter>(
      std::vector<Value>({Int64(10), Int64(20), Int64(30), Int64(40)})));
  filters.push_back(std::make_unique<ColumnFilter>(
      std::vector<Value>({Int64(20), Int64(40), Int64(60), Int64(80)})));
  EXPECT_THAT(EvaluatorTableScanOp::IntersectColumnFilters(filters),
              IsOkAndHolds(Pointee(IsListColumnFilterWith(
                  std::vector<Value>({Int64(20), Int64(40)})))));
}

TEST(IntersectColumnFiltersTest, TwoInListsFirstEmpty) {
  std::vector<std::unique_ptr<ColumnFilter>> filters;
  filters.push_back(std::make_unique<ColumnFilter>(std::vector<Value>()));
  filters.push_back(std::make_unique<ColumnFilter>(
      std::vector<Value>({Int64(20), Int64(40), Int64(60), Int64(80)})));
  EXPECT_THAT(
      EvaluatorTableScanOp::IntersectColumnFilters(filters),
      IsOkAndHolds(Pointee(IsListColumnFilterWith(std::vector<Value>()))));
}

TEST(IntersectColumnFiltersTest, TwoInListsSecondEmpty) {
  std::vector<std::unique_ptr<ColumnFilter>> filters;
  filters.push_back(std::make_unique<ColumnFilter>(
      std::vector<Value>({Int64(20), Int64(40), Int64(60), Int64(80)})));
  filters.push_back(std::make_unique<ColumnFilter>(std::vector<Value>()));
  EXPECT_THAT(
      EvaluatorTableScanOp::IntersectColumnFilters(filters),
      IsOkAndHolds(Pointee(IsListColumnFilterWith(std::vector<Value>()))));
}

TEST(IntersectColumnFiltersTest, InListAndGeRange) {
  std::vector<std::unique_ptr<ColumnFilter>> filters;
  filters.push_back(std::make_unique<ColumnFilter>(
      std::vector<Value>({Int64(10), Int64(20), Int64(30), Int64(40)})));
  filters.push_back(std::make_unique<ColumnFilter>(Int64(20), Value()));
  EXPECT_THAT(EvaluatorTableScanOp::IntersectColumnFilters(filters),
              IsOkAndHolds(Pointee(IsListColumnFilterWith(
                  std::vector<Value>({Int64(20), Int64(30), Int64(40)})))));
}

TEST(IntersectColumnFiltersTest, InListAndLeRange) {
  std::vector<std::unique_ptr<ColumnFilter>> filters;
  filters.push_back(std::make_unique<ColumnFilter>(
      std::vector<Value>({Int64(10), Int64(20), Int64(30), Int64(40)})));
  filters.push_back(std::make_unique<ColumnFilter>(Value(), Int64(30)));
  EXPECT_THAT(EvaluatorTableScanOp::IntersectColumnFilters(filters),
              IsOkAndHolds(Pointee(IsListColumnFilterWith(
                  std::vector<Value>({Int64(10), Int64(20), Int64(30)})))));
}

TEST(IntersectColumnFiltersTest, InListAndRange) {
  std::vector<std::unique_ptr<ColumnFilter>> filters;
  filters.push_back(std::make_unique<ColumnFilter>(
      std::vector<Value>({Int64(10), Int64(20), Int64(30), Int64(40)})));
  filters.push_back(std::make_unique<ColumnFilter>(Int64(20), Int64(30)));
  EXPECT_THAT(EvaluatorTableScanOp::IntersectColumnFilters(filters),
              IsOkAndHolds(Pointee(IsListColumnFilterWith(
                  std::vector<Value>({Int64(20), Int64(30)})))));
}

TEST(IntersectColumnFiltersTest, InListAndDisjointRange) {
  std::vector<std::unique_ptr<ColumnFilter>> filters;
  filters.push_back(std::make_unique<ColumnFilter>(
      std::vector<Value>({Int64(10), Int64(20), Int64(30), Int64(40)})));
  filters.push_back(std::make_unique<ColumnFilter>(Int64(100), Int64(200)));
  EXPECT_THAT(
      EvaluatorTableScanOp::IntersectColumnFilters(filters),
      IsOkAndHolds(Pointee(IsListColumnFilterWith(std::vector<Value>()))));
}

TEST(IntersectColumnFiltersTest, InListAndDisjointGeRange) {
  std::vector<std::unique_ptr<ColumnFilter>> filters;
  filters.push_back(std::make_unique<ColumnFilter>(
      std::vector<Value>({Int64(10), Int64(20), Int64(30), Int64(40)})));
  filters.push_back(std::make_unique<ColumnFilter>(Int64(100), Value()));
  EXPECT_THAT(
      EvaluatorTableScanOp::IntersectColumnFilters(filters),
      IsOkAndHolds(Pointee(IsListColumnFilterWith(std::vector<Value>()))));
}

TEST(IntersectColumnFiltersTest, InListAndDisjointLeRange) {
  std::vector<std::unique_ptr<ColumnFilter>> filters;
  filters.push_back(std::make_unique<ColumnFilter>(
      std::vector<Value>({Int64(10), Int64(20), Int64(30), Int64(40)})));
  filters.push_back(std::make_unique<ColumnFilter>(Value(), Int64(0)));
  EXPECT_THAT(
      EvaluatorTableScanOp::IntersectColumnFilters(filters),
      IsOkAndHolds(Pointee(IsListColumnFilterWith(std::vector<Value>()))));
}

TEST(IntersectColumnFiltersTest, DisjointRangesAndInList) {
  std::vector<std::unique_ptr<ColumnFilter>> filters;
  filters.push_back(std::make_unique<ColumnFilter>(
      std::vector<Value>({Int64(10), Int64(20), Int64(30), Int64(40)})));
  filters.push_back(std::make_unique<ColumnFilter>(Int64(30), Value()));
  filters.push_back(std::make_unique<ColumnFilter>(Value(), Int64(20)));
  EXPECT_THAT(
      EvaluatorTableScanOp::IntersectColumnFilters(filters),
      IsOkAndHolds(Pointee(IsListColumnFilterWith(std::vector<Value>()))));
}

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

TEST_F(CreateIteratorTest, EvaluatorTableScanOp) {
  VariableId x("x"), y("y"), z("z");
  SimpleTable table("TestTable", {{"column0", types::Int64Type()},
                                  {"column1", types::Int64Type()},
                                  {"column2", types::StringType()},
                                  {"column3", proto_type_}});
  table.SetContents(
      {{Int64(10), Int64(100), String("foo1"), GetProtoValue(0)},
       {Int64(20), Int64(200), String("foo2"), GetProtoValue(1)}});

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto scan_op,
      EvaluatorTableScanOp::Create(
          &table, /*alias=*/"",
          {table.GetColumn(2), table.GetColumn(3), table.GetColumn(1)},
          {x, y, z},
          /*and_filters=*/{}, /*read_time=*/nullptr));
  EXPECT_EQ(scan_op->IteratorDebugString(),
            "EvaluatorTableTupleIterator(TestTable)");
  EXPECT_EQ(scan_op->DebugString(),
            "EvaluatorTableScanOp(\n"
            "+-column2\n"
            "+-column3\n"
            "+-column1\n"
            "+-table: TestTable)");
  std::unique_ptr<TupleSchema> output_schema = scan_op->CreateOutputSchema();
  EXPECT_THAT(output_schema->variables(), ElementsAre(x, y, z));

  EvaluationContext context((EvaluationOptions()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> iter,
      scan_op->CreateIterator(EmptyParams(), /*num_extra_slots=*/1, &context));
  EXPECT_EQ(iter->DebugString(), "EvaluatorTableTupleIterator(TestTable)");
  EXPECT_TRUE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> data,
                       ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 2);
  EXPECT_THAT(
      data[0].slots(),
      ElementsAre(IsTupleSlotWith(String("foo1"), IsNull()),
                  IsTupleSlotWith(GetProtoValue(0), Pointee(Eq(nullopt))),
                  IsTupleSlotWith(Int64(100), IsNull()), _));
  EXPECT_THAT(
      data[1].slots(),
      ElementsAre(IsTupleSlotWith(String("foo2"), IsNull()),
                  IsTupleSlotWith(GetProtoValue(1), Pointee(Eq(nullopt))),
                  IsTupleSlotWith(Int64(200), IsNull()), _));

  EvaluationContext scramble_context(GetScramblingEvaluationOptions());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      iter, scan_op->CreateIterator(EmptyParams(), /*num_extra_slots=*/1,
                                    &scramble_context));
  EXPECT_EQ(iter->DebugString(),
            "ReorderingTupleIterator(EvaluatorTableTupleIterator(TestTable))");
  EXPECT_FALSE(iter->PreservesOrder());
}

TEST_F(CreateIteratorTest, EvaluatorTableScanOpWithColumnFilter) {
  VariableId x("x"), y("y"), z("z");
  EvaluatorTestTable table(
      "TestTable",
      {{"column0", types::Int64Type()},
       {"column1", types::Int64Type()},
       {"column2", types::StringType()},
       {"column3", proto_type_}},
      {{Int64(10), Int64(100), String("foo1"), GetProtoValue(0)},
       {Int64(20), Int64(200), String("foo2"), GetProtoValue(1)}},
      /*end_status=*/absl::OkStatus(), /*column_filter_idxs=*/{0, 1});

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto array_expr,
                       ConstExpr::Create(Int64Array({100, 110})));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto filter,
                       InArrayColumnFilterArg::Create(z, /*column_idx=*/2,
                                                      std::move(array_expr)));

  std::vector<std::unique_ptr<ColumnFilterArg>> and_filters;
  and_filters.push_back(std::move(filter));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto scan_op,
      EvaluatorTableScanOp::Create(
          &table, /*alias=*/"",
          {table.GetColumn(2), table.GetColumn(3), table.GetColumn(1)},
          {x, y, z}, std::move(and_filters),
          /*read_time=*/nullptr));
  EXPECT_EQ(scan_op->IteratorDebugString(),
            "EvaluatorTableTupleIterator(TestTable)");
  EXPECT_EQ(scan_op->DebugString(),
            "EvaluatorTableScanOp(\n"
            "+-column2\n"
            "+-column3\n"
            "+-column1\n"
            "+-InArrayColumnFilterArg($z, column_idx: 2, "
            "array: ConstExpr([100, 110]))\n"
            "+-table: TestTable)");
  std::unique_ptr<TupleSchema> output_schema = scan_op->CreateOutputSchema();
  EXPECT_THAT(output_schema->variables(), ElementsAre(x, y, z));

  EvaluationContext context((EvaluationOptions()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> iter,
      scan_op->CreateIterator(EmptyParams(), /*num_extra_slots=*/1, &context));
  EXPECT_EQ(iter->DebugString(), "EvaluatorTableTupleIterator(TestTable)");
  EXPECT_TRUE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> data,
                       ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 1);
  EXPECT_THAT(
      data[0].slots(),
      ElementsAre(IsTupleSlotWith(String("foo1"), IsNull()),
                  IsTupleSlotWith(GetProtoValue(0), Pointee(Eq(nullopt))),
                  IsTupleSlotWith(Int64(100), IsNull()), _));
}

TEST_F(CreateIteratorTest, EvaluatorTableScanOpFailure) {
  const std::string error = "Failed to read row from TestTable";
  const absl::Status failure = googlesql_base::OutOfRangeErrorBuilder() << error;

  EvaluatorTestTable table("TestTable", {{"column0", types::Int64Type()}},
                           {{Int64(10)}, {Int64(20)}}, failure);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto scan_op,
      EvaluatorTableScanOp::Create(&table, /*alias=*/"", {table.GetColumn(0)},
                                   {VariableId("x")},
                                   /*and_filters=*/{},
                                   /*read_time=*/nullptr));

  EvaluationContext context((EvaluationOptions()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> iter,
      scan_op->CreateIterator(EmptyParams(), /*num_extra_slots=*/1, &context));
  absl::Status status;
  std::vector<TupleData> data = ReadFromTupleIteratorFull(iter.get(), &status);
  ASSERT_EQ(data.size(), 2);
  EXPECT_THAT(data[0].slots(),
              ElementsAre(IsTupleSlotWith(Int64(10), IsNull()), _));
  EXPECT_THAT(data[1].slots(),
              ElementsAre(IsTupleSlotWith(Int64(20), IsNull()), _));
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kOutOfRange, error));
}

TEST_F(CreateIteratorTest, EvaluatorTableScanOpCancellation) {
  int64_t num_cancel_calls = 0;
  const std::function<void()> cancel_cb = [&num_cancel_calls]() {
    ++num_cancel_calls;
  };
  EvaluatorTestTable table("TestTable", {{"column0", types::Int64Type()}},
                           {{Int64(10)}, {Int64(20)}}, absl::OkStatus(),
                           /*column_filter_idxs=*/{}, cancel_cb);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto scan_op,
      EvaluatorTableScanOp::Create(&table, /*alias=*/"", {table.GetColumn(0)},
                                   {VariableId("x")},
                                   /*and_filters=*/{},
                                   /*read_time=*/nullptr));

  EvaluationContext context((EvaluationOptions()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> iter,
      scan_op->CreateIterator(EmptyParams(), /*num_extra_slots=*/1, &context));
  GOOGLESQL_ASSERT_OK(context.CancelStatement());
  absl::Status status;
  std::vector<TupleData> data = ReadFromTupleIteratorFull(iter.get(), &status);
  EXPECT_TRUE(data.empty());
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kCancelled, _));
  EXPECT_EQ(num_cancel_calls, 1);
}

TEST_F(CreateIteratorTest, EvaluatorTableScanOpDeadlineExceeded) {
  absl::SetFlag(&FLAGS_googlesql_simple_iterator_call_time_now_rows_period, 1);
  int64_t num_cancel_calls = 0;
  const std::function<void()> cancel_cb = [&num_cancel_calls]() {
    ++num_cancel_calls;
  };

  std::vector<absl::Time> deadlines;
  const std::function<void(absl::Time)> set_deadline_cb =
      [&deadlines](absl::Time deadline) { deadlines.push_back(deadline); };

  googlesql_base::SimulatedClock clock(absl::UnixEpoch());
  EvaluatorTestTable table("TestTable", {{"column0", types::Int64Type()}},
                           {{Int64(10)}, {Int64(20)}}, absl::OkStatus(),
                           /*column_filter_idxs=*/{}, cancel_cb,
                           set_deadline_cb, &clock);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto scan_op,
      EvaluatorTableScanOp::Create(&table, /*alias=*/"", {table.GetColumn(0)},
                                   {VariableId("x")},
                                   /*and_filters=*/{},
                                   /*read_time=*/nullptr));

  EvaluationContext context((EvaluationOptions()));
  context.SetClockAndClearCurrentTimestamp(&clock);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> iter,
      scan_op->CreateIterator(EmptyParams(), /*num_extra_slots=*/1, &context));
  const absl::Time deadline = absl::UnixEpoch() + absl::Seconds(10);
  context.SetStatementEvaluationDeadline(deadline);

  const TupleData* tuple = iter->Next();
  EXPECT_EQ(num_cancel_calls, 0);
  EXPECT_THAT(deadlines, ElementsAre(deadline));
  ASSERT_TRUE(tuple != nullptr);
  EXPECT_EQ(Tuple(&iter->Schema(), tuple).DebugString(), "<x:10>");

  clock.AdvanceTime(absl::Seconds(15));
  tuple = iter->Next();
  EXPECT_EQ(num_cancel_calls, 0);
  EXPECT_THAT(deadlines, ElementsAre(deadline));
  EXPECT_TRUE(tuple == nullptr);
  EXPECT_THAT(iter->Status(), StatusIs(absl::StatusCode::kDeadlineExceeded, _));
}

// Test implementation of CppValueArg; associates the variable with a
// direct std::string (as opposed to a googlesql::Value representing a string).
class TestCppValueArg : public CppValueArg {
 public:
  TestCppValueArg(VariableId var, absl::string_view value)
      : CppValueArg(var, absl::StrCat("TestCppValueArg: ", value)),
        value_(value) {}

  std::unique_ptr<CppValueBase> CreateValue(
      EvaluationContext* context) const override {
    EXPECT_NE(context, nullptr);
    return std::make_unique<CppValue<std::string>>(value_);
  }

 private:
  std::string value_;
};

// RelationalOp implementation to test consumption of CppValue's produced by
// TestCppValueArg.
//
// Emits one row with schema <output_vars>, consisting of the values from
//   <tuple_vars> and <cpp_vars> concatenated together. CppValue's are converted
//   to googlesql::Value's by assuming they are of type CppValue<std::string>
//   and creating a googlesql::Value to represent the underlying string.
class TestCppValuesOp : public RelationalOp {
 public:
  // tuple_vars: Variables to be consumed, passed in to CreateIterator() via
  //   <params>, using the schema passed into SetSchemasForEvaluation().
  // cpp_vars: C++ Variables to be consumed; values are read from
  //   EvaluationContext, and are assumed to be of type CppValue<std::string>;
  //   Each C++ value translates into a googlesql::Value of STRING type.
  // output_vars: Variables to use in the output schema. Requirement:
  //   output_vars.size() == tuple_vars.size() + cpp_vars.size()
  TestCppValuesOp(std::vector<VariableId> tuple_vars,
                  std::vector<VariableId> cpp_vars,
                  std::vector<VariableId> output_vars)
      : cpp_vars_(cpp_vars), output_vars_(output_vars) {
    // Initialize slot indices for tuple variables to -1, we'll substitute in
    // the actual values during SetSchemasForEvaluation().
    for (const VariableId& v : tuple_vars) {
      tuple_vars_slots_[v] = std::make_pair(-1, -1);
    }
  }

  absl::Status SetSchemasForEvaluation(
      absl::Span<const TupleSchema* const> params_schemas) override {
    for (auto& pair : tuple_vars_slots_) {
      bool found = false;
      for (int i = 0; i < params_schemas.size(); ++i) {
        std::optional<int> idx =
            params_schemas[i]->FindIndexForVariable(pair.first);
        if (idx.has_value()) {
          pair.second = std::make_pair(i, idx.value());
          found = true;
        }
      }
      if (!found) {
        return googlesql_base::InvalidArgumentErrorBuilder()
               << "Variable " << pair.first << " not found";
      }
    }
    return absl::OkStatus();
  }

  absl::StatusOr<std::unique_ptr<TupleIterator>> CreateIterator(
      absl::Span<const TupleData* const> params, int num_extra_slots,
      EvaluationContext* context) const override {
    std::vector<TupleData> tuple_data;
    tuple_data.emplace_back(tuple_vars_slots_.size() + cpp_vars_.size() +
                            num_extra_slots);
    // Include tuple variables first. Tuple variables exists to verify that
    // the LetOp is able to handle tuple variables and cpp variables
    // simultaneously.
    int slot_idx = 0;
    for (const auto& pair : tuple_vars_slots_) {
      tuple_data[0]
          .mutable_slot(slot_idx++)
          ->CopyFromSlot(params[pair.second.first]->slot(pair.second.second));
    }

    // Convert the std::string's into googlesql::Value's, and pack into a
    // TupleData to return to the caller.
    for (const auto& var : cpp_vars_) {
      tuple_data[0]
          .mutable_slot(slot_idx++)
          ->SetValue(Value::String(
              *CppValue<std::string>::Get(context->GetCppValue(var))));
    }
    return std::make_unique<TestTupleIterator>(
        output_vars_, std::move(tuple_data), /*preserves_order=*/true,
        /*end_status=*/absl::OkStatus());
  }

  std::unique_ptr<TupleSchema> CreateOutputSchema() const override {
    return std::make_unique<TupleSchema>(output_vars_);
  }

  std::string IteratorDebugString() const override {
    return TestTupleIterator::GetDebugString();
  }

  std::string DebugInternal(const std::string& indent,
                            bool verbose) const override {
    return absl::StrCat(
        "ConsumeTestCppValues: ",
        absl::StrJoin(cpp_vars_, ", ",
                      [](std::string* out, VariableId v) {
                        absl::StrAppend(out, v.ToString());
                      }),
        " => ",
        absl::StrJoin(output_vars_, ", ", [](std::string* out, VariableId v) {
          absl::StrAppend(out, v.ToString());
        }));
  }

 private:
  std::map<VariableId, std::pair<int, int>> tuple_vars_slots_;
  // CppValue'd variables; assumed to be instances of TestCppValueArg
  std::vector<VariableId> cpp_vars_;
  std::vector<VariableId> output_vars_;
};

TEST_F(CreateIteratorTest, LetOpCppValues) {
  VariableId a("a"), b("b"), n("n"), x("x"), y("y");
  VariableId out_a("out_a"), out_b("out_b"), out_x("out_x"), out_y("out_y");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_n, DerefExpr::Create(n, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto const_1, ConstExpr::Create(Int64(1)));

  std::vector<std::unique_ptr<ExprArg>> assign;
  assign.push_back(std::make_unique<ExprArg>(a, std::move(const_1)));
  assign.push_back(std::make_unique<ExprArg>(b, std::move(deref_n)));

  std::vector<std::unique_ptr<CppValueArg>> cpp_assign;
  cpp_assign.push_back(std::make_unique<TestCppValueArg>(x, "value_x"));
  cpp_assign.push_back(std::make_unique<TestCppValueArg>(y, "value_y"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto let_op,
      LetOp::Create(
          std::move(assign), std::move(cpp_assign),
          std::make_unique<TestCppValuesOp>(
              std::vector<VariableId>{a, b}, std::vector<VariableId>{x, y},
              std::vector<VariableId>{out_a, out_b, out_x, out_y})));

  TupleSchema schema({n});
  GOOGLESQL_ASSERT_OK(let_op->SetSchemasForEvaluation({&schema}));
  std::unique_ptr<TupleSchema> output_schema = let_op->CreateOutputSchema();
  EXPECT_EQ("<out_a,out_b,out_x,out_y>", output_schema->DebugString());

  TupleData data(CreateTestTupleData({Int64(10)}));
  std::vector<const TupleData*> datas = {&data};
  EvaluationContext context((EvaluationOptions()));
  {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        std::unique_ptr<TupleIterator> iter,
        let_op->CreateIterator(datas, /*num_extra_slots=*/1, &context));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> output_data,
                         ReadFromTupleIterator(iter.get()));
    ASSERT_EQ(1, output_data.size());
    EXPECT_THAT(output_data[0].slots(),
                ElementsAre(
                    /*out_a: */ IsTupleSlotWith(Int64(1), IsNull()),
                    /*out_b: */ IsTupleSlotWith(Int64(10), IsNull()),
                    /*out_x: */ IsTupleSlotWith(String("value_x"), IsNull()),
                    /*out_y: */ IsTupleSlotWith(String("value_y"), IsNull()),
                    /*extra slot*/ _));
  }

  // Verify that C++ variables have been removed from the EvaluationContext
  // when the LetOpTupleIterator is destroyed.
  EXPECT_THAT(context.GetCppValue(x), IsNull());
  EXPECT_THAT(context.GetCppValue(y), IsNull());
}

TEST_F(CreateIteratorTest, LetOp) {
  VariableId a("a"), x("x"), y("y"), z("z");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_a, DerefExpr::Create(a, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto const_1, ConstExpr::Create(Int64(1)));

  std::vector<std::unique_ptr<ValueExpr>> store_a_plus_one_in_x_args;
  store_a_plus_one_in_x_args.push_back(std::move(deref_a));
  store_a_plus_one_in_x_args.push_back(std::move(const_1));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto store_a_plus_one_in_x_expr,
                       ScalarFunctionCallExpr::Create(
                           CreateFunction(FunctionKind::kAdd, Int64Type()),
                           std::move(store_a_plus_one_in_x_args)));

  auto store_a_plus_one_in_x =
      std::make_unique<ExprArg>(x, std::move(store_a_plus_one_in_x_expr));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_x, DerefExpr::Create(x, Int64Type()));

  auto store_x_in_y = std::make_unique<ExprArg>(y, std::move(deref_x));

  std::vector<std::vector<const SharedProtoState*>> shared_states;
  const std::vector<TupleData> values = CreateTestTupleDatas(
      {{GetProtoValue(1)}, {GetProtoValue(2)}, {GetProtoValue(3)}},
      &shared_states);

  VariableId column("column");
  auto three_rows = std::make_unique<TestRelationalOp>(
      std::vector<VariableId>{column}, values,
      /*preserves_order=*/true);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_y, DerefExpr::Create(y, Int64Type()));

  std::vector<std::unique_ptr<ExprArg>> prepend_rows_with_y_into_z_args;
  prepend_rows_with_y_into_z_args.push_back(
      std::make_unique<ExprArg>(z, std::move(deref_y)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto prepend_rows_with_y_into_z,
      ComputeOp::Create(std::move(prepend_rows_with_y_into_z_args),
                        std::move(three_rows)));

  std::vector<std::unique_ptr<ExprArg>> let_op_assign;
  let_op_assign.push_back(std::move(store_a_plus_one_in_x));
  let_op_assign.push_back(std::move(store_x_in_y));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto let_op,
      LetOp::Create(std::move(let_op_assign),
                    /*cpp_assign=*/{}, std::move(prepend_rows_with_y_into_z)));
  EXPECT_EQ(let_op->IteratorDebugString(),
            "LetOpTupleIterator(ComputeTupleIterator(TestTupleIterator))");
  EXPECT_EQ(let_op->DebugString(),
            "LetOp(\n"
            "+-assign: {\n"
            "| +-$x := Add($a, ConstExpr(1)),\n"
            "| +-$y := $x},\n"
            "+-cpp_assign: {},\n"
            "+-body: ComputeOp(\n"
            "  +-map: {\n"
            "  | +-$z := $y},\n"
            "  +-input: TestRelationalOp))");
  std::unique_ptr<TupleSchema> output_schema = let_op->CreateOutputSchema();
  EXPECT_THAT(output_schema->variables(), ElementsAre(column, z));

  EvaluationContext context((EvaluationOptions()));

  TupleSchema params_schema({a});
  TupleData params_data = CreateTestTupleData({Int64(5)});
  GOOGLESQL_ASSERT_OK(let_op->SetSchemasForEvaluation({&params_schema}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<TupleIterator> iter,
                       let_op->CreateIterator({&params_data},
                                              /*num_extra_slots=*/1, &context));
  EXPECT_EQ(iter->DebugString(),
            "LetOpTupleIterator(ComputeTupleIterator(TestTupleIterator))");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> data,
                       ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 3);
  EXPECT_THAT(data[0].slots(),
              ElementsAre(IsTupleSlotWith(GetProtoValue(1),
                                          HasRawPointer(shared_states[0][0])),
                          IsTupleSlotWith(Int64(6), IsNull()), _));
  EXPECT_THAT(data[1].slots(),
              ElementsAre(IsTupleSlotWith(GetProtoValue(2),
                                          HasRawPointer(shared_states[1][0])),
                          IsTupleSlotWith(Int64(6), IsNull()), _));
  EXPECT_THAT(data[2].slots(),
              ElementsAre(IsTupleSlotWith(GetProtoValue(3),
                                          HasRawPointer(shared_states[2][0])),
                          IsTupleSlotWith(Int64(6), IsNull()), _));

  // Check that we get an error if the memory bound is too low. This is
  // particularly important if one of the variables holds an array that
  // represents a WITH table.
  EvaluationContext memory_context(
      GetIntermediateMemoryEvaluationOptions(/*total_bytes=*/1));
  EXPECT_THAT(let_op->CreateIterator({&params_data},
                                     /*num_extra_slots=*/1, &memory_context),
              StatusIs(absl::StatusCode::kResourceExhausted));
}

// For readability.
std::vector<JoinOp::HashJoinEqualityExprs> EmptyHashJoinEqualityExprs() {
  return {};
}

TEST_F(CreateIteratorTest, InnerJoin) {
  VariableId x1("x1"), x2("x2"), y1("y1"), y2("y2"), p("p");

  std::vector<std::vector<const SharedProtoState*>> shared_states1;
  auto input1 = absl::WrapUnique(new TestRelationalOp(
      {x1, x2},
      CreateTestTupleDatas(
          {{Int64(1), GetProtoValue(1)}, {Int64(4), GetProtoValue(4)}},
          &shared_states1),
      /*preserves_order=*/true));

  std::vector<std::vector<const SharedProtoState*>> shared_states2;
  auto input2 = absl::WrapUnique(new TestRelationalOp(
      {y1, y2},
      CreateTestTupleDatas(
          {{Int64(2), GetProtoValue(2)}, {Int64(3), GetProtoValue(3)}},
          &shared_states2),
      /*preserves_order=*/true));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_x1, DerefExpr::Create(x1, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_p, DerefExpr::Create(p, Int64Type()));

  std::vector<std::unique_ptr<ValueExpr>> add_args;
  add_args.push_back(std::move(deref_x1));
  add_args.push_back(std::move(deref_p));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto add_expr,
                       ScalarFunctionCallExpr::Create(
                           CreateFunction(FunctionKind::kAdd, Int64Type()),
                           std::move(add_args)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_y1, DerefExpr::Create(y1, Int64Type()));

  std::vector<std::unique_ptr<ValueExpr>> less_args;
  less_args.push_back(std::move(add_expr));
  less_args.push_back(std::move(deref_y1));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto less_expr,
                       ScalarFunctionCallExpr::Create(
                           CreateFunction(FunctionKind::kLess, BoolType()),
                           std::move(less_args)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto join_op,
      JoinOp::Create(JoinOp::kInnerJoin, EmptyHashJoinEqualityExprs(),
                     std::move(less_expr), std::move(input1), std::move(input2),
                     /*left_outputs=*/{}, /*right_outputs=*/{}));
  EXPECT_EQ(join_op->IteratorDebugString(),
            "JoinTupleIterator(INNER, left=TestTupleIterator, "
            "right=TestTupleIterator)");
  EXPECT_EQ(
      "JoinOp(INNER\n"
      "+-hash_join_equality_left_exprs: {},\n"
      "+-hash_join_equality_right_exprs: {},\n"
      "+-remaining_condition: Less(Add($x1, $p), $y1),\n"
      "+-left_input: TestRelationalOp,\n"
      "+-right_input: TestRelationalOp)",
      join_op->DebugString());
  std::unique_ptr<TupleSchema> output_schema = join_op->CreateOutputSchema();
  EXPECT_THAT(output_schema->variables(), ElementsAre(x1, x2, y1, y2));

  EvaluationContext context((EvaluationOptions()));

  TupleSchema params_schema({p});
  TupleData params_data = CreateTestTupleData({Int64(0)});

  GOOGLESQL_ASSERT_OK(join_op->SetSchemasForEvaluation({&params_schema}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> iter,
      join_op->CreateIterator({&params_data}, /*num_extra_slots=*/1, &context));
  EXPECT_EQ(iter->DebugString(),
            "JoinTupleIterator(INNER, "
            "left=TestTupleIterator, right=TestTupleIterator)");
  EXPECT_TRUE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> data,
                       ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 2);
  EXPECT_THAT(data[0].slots(),
              ElementsAre(IsTupleSlotWith(Int64(1), IsNull()),
                          IsTupleSlotWith(GetProtoValue(1),
                                          HasRawPointer(shared_states1[0][1])),
                          IsTupleSlotWith(Int64(2), IsNull()),
                          IsTupleSlotWith(GetProtoValue(2),
                                          HasRawPointer(shared_states2[0][1])),
                          _));
  EXPECT_THAT(data[1].slots(),
              ElementsAre(IsTupleSlotWith(Int64(1), IsNull()),
                          IsTupleSlotWith(GetProtoValue(1),
                                          HasRawPointer(shared_states1[0][1])),
                          IsTupleSlotWith(Int64(3), IsNull()),
                          IsTupleSlotWith(GetProtoValue(3),
                                          HasRawPointer(shared_states2[1][1])),
                          _));

  // Do it again with cancellation.
  context.ClearDeadlineAndCancellationState();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      iter,
      join_op->CreateIterator({&params_data}, /*num_extra_slots=*/1, &context));
  GOOGLESQL_ASSERT_OK(context.CancelStatement());
  absl::Status status;
  data = ReadFromTupleIteratorFull(iter.get(), &status);
  EXPECT_TRUE(data.empty());
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kCancelled, _));

  // Check that scrambling works. We have to use a new iterator variable because
  // the context must outlive the iterator.
  EvaluationContext scramble_context(GetScramblingEvaluationOptions());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> scramble_iter,
      join_op->CreateIterator({&params_data},
                              /*num_extra_slots=*/1, &scramble_context));
  EXPECT_EQ(scramble_iter->DebugString(),
            "ReorderingTupleIterator(JoinTupleIterator(INNER, "
            "left=TestTupleIterator, right=TestTupleIterator))");
  EXPECT_FALSE(scramble_iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(data, ReadFromTupleIterator(scramble_iter.get()));
  ASSERT_EQ(data.size(), 2);
  // Check for the extra slot.
  EXPECT_EQ(data[0].num_slots(), 5);
  EXPECT_EQ(data[1].num_slots(), 5);

  // Check that if the memory bound is too low, we return an error when loading
  // the right-hand side into memory.
  EvaluationContext memory_context(GetIntermediateMemoryEvaluationOptions(
      /*total_bytes=*/100));
  EXPECT_THAT(join_op->CreateIterator({&params_data},
                                      /*num_extra_slots=*/1, &memory_context),
              StatusIs(absl::StatusCode::kResourceExhausted,
                       HasSubstr("Out of memory")));
}

TEST_F(CreateIteratorTest, LeftOuterJoin) {
  VariableId x1("x1"), x2("x2"), y1("y1"), y2("y2"), y1_prime("y1'"),
      y2_prime("y2'"), p("p");

  std::vector<std::vector<const SharedProtoState*>> shared_states1;
  auto input1 = absl::WrapUnique(new TestRelationalOp(
      {x1, x2},
      CreateTestTupleDatas(
          {{Int64(1), GetProtoValue(1)}, {Int64(4), GetProtoValue(4)}},
          &shared_states1),
      /*preserves_order=*/true));

  std::vector<std::vector<const SharedProtoState*>> shared_states2;
  auto input2 = absl::WrapUnique(new TestRelationalOp(
      {y1, y2},
      CreateTestTupleDatas(
          {{Int64(2), GetProtoValue(2)}, {Int64(3), GetProtoValue(3)}},
          &shared_states2),
      /*preserves_order=*/true));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_x1, DerefExpr::Create(x1, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_p, DerefExpr::Create(p, Int64Type()));

  std::vector<std::unique_ptr<ValueExpr>> add_args;
  add_args.push_back(std::move(deref_x1));
  add_args.push_back(std::move(deref_p));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto add_expr,
                       ScalarFunctionCallExpr::Create(
                           CreateFunction(FunctionKind::kAdd, Int64Type()),
                           std::move(add_args)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_y1, DerefExpr::Create(y1, Int64Type()));

  std::vector<std::unique_ptr<ValueExpr>> less_args;
  less_args.push_back(std::move(add_expr));
  less_args.push_back(std::move(deref_y1));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto less_expr,
                       ScalarFunctionCallExpr::Create(
                           CreateFunction(FunctionKind::kLess, BoolType()),
                           std::move(less_args)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_y1_again, DerefExpr::Create(y1, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_y2, DerefExpr::Create(y2, proto_type_));
  std::vector<std::unique_ptr<ExprArg>> right_outputs;
  right_outputs.push_back(
      std::make_unique<ExprArg>(y1_prime, std::move(deref_y1_again)));
  right_outputs.push_back(
      std::make_unique<ExprArg>(y2_prime, std::move(deref_y2)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto join_op,
      JoinOp::Create(JoinOp::kLeftOuterJoin, EmptyHashJoinEqualityExprs(),
                     std::move(less_expr), std::move(input1), std::move(input2),
                     /*left_outputs=*/{}, std::move(right_outputs)));

  EXPECT_EQ(join_op->IteratorDebugString(),
            "JoinTupleIterator(LEFT OUTER, left=TestTupleIterator, "
            "right=TestTupleIterator)");
  EXPECT_EQ(
      "JoinOp(LEFT OUTER\n"
      "+-right_outputs: {\n"
      "| +-$y1' := $y1,\n"
      "| +-$y2' := $y2},\n"
      "+-hash_join_equality_left_exprs: {},\n"
      "+-hash_join_equality_right_exprs: {},\n"
      "+-remaining_condition: Less(Add($x1, $p), $y1),\n"
      "+-left_input: TestRelationalOp,\n"
      "+-right_input: TestRelationalOp)",
      join_op->DebugString());
  std::unique_ptr<TupleSchema> output_schema = join_op->CreateOutputSchema();
  EXPECT_THAT(output_schema->variables(),
              ElementsAre(x1, x2, y1_prime, y2_prime));

  EvaluationContext context((EvaluationOptions()));

  TupleSchema params_schema({p});
  TupleData params_data = CreateTestTupleData({Int64(0)});

  GOOGLESQL_ASSERT_OK(join_op->SetSchemasForEvaluation({&params_schema}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> iter,
      join_op->CreateIterator({&params_data}, /*num_extra_slots=*/1, &context));
  EXPECT_EQ(iter->DebugString(),
            "JoinTupleIterator(LEFT OUTER, "
            "left=TestTupleIterator, right=TestTupleIterator)");
  EXPECT_TRUE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> data,
                       ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 3);
  EXPECT_THAT(data[0].slots(),
              ElementsAre(IsTupleSlotWith(Int64(1), IsNull()),
                          IsTupleSlotWith(GetProtoValue(1),
                                          HasRawPointer(shared_states1[0][1])),
                          IsTupleSlotWith(Int64(2), IsNull()),
                          IsTupleSlotWith(GetProtoValue(2),
                                          HasRawPointer(shared_states2[0][1])),
                          _));
  EXPECT_THAT(data[1].slots(),
              ElementsAre(IsTupleSlotWith(Int64(1), IsNull()),
                          IsTupleSlotWith(GetProtoValue(1),
                                          HasRawPointer(shared_states1[0][1])),
                          IsTupleSlotWith(Int64(3), IsNull()),
                          IsTupleSlotWith(GetProtoValue(3),
                                          HasRawPointer(shared_states2[1][1])),
                          _));
  EXPECT_THAT(
      data[2].slots(),
      ElementsAre(
          IsTupleSlotWith(Int64(4), IsNull()),
          IsTupleSlotWith(GetProtoValue(4),
                          HasRawPointer(shared_states1[1][1])),
          IsTupleSlotWith(NullInt64(), IsNull()),
          IsTupleSlotWith(Value::Null(proto_type_), Pointee(Eq(nullopt))), _));

  // Do it again with cancellation.
  context.ClearDeadlineAndCancellationState();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      iter,
      join_op->CreateIterator({&params_data}, /*num_extra_slots=*/1, &context));
  GOOGLESQL_ASSERT_OK(context.CancelStatement());
  absl::Status status;
  data = ReadFromTupleIteratorFull(iter.get(), &status);
  EXPECT_TRUE(data.empty());
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kCancelled, _));

  // Check that scrambling works. We have to use a new iterator variable because
  // the context must outlive the iterator.
  EvaluationContext scramble_context(GetScramblingEvaluationOptions());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> scramble_iter,
      join_op->CreateIterator({&params_data},
                              /*num_extra_slots=*/1, &scramble_context));
  EXPECT_EQ(scramble_iter->DebugString(),
            "ReorderingTupleIterator(JoinTupleIterator(LEFT OUTER, "
            "left=TestTupleIterator, right=TestTupleIterator))");
  EXPECT_FALSE(scramble_iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(data, ReadFromTupleIterator(scramble_iter.get()));
  ASSERT_EQ(data.size(), 3);
  // Check for the extra slot.
  EXPECT_EQ(data[0].num_slots(), 5);
  EXPECT_EQ(data[1].num_slots(), 5);
  EXPECT_EQ(data[2].num_slots(), 5);

  // Check that if the memory bound is too low, we return an error when loading
  // the right-hand side into memory.
  EvaluationContext memory_context(GetIntermediateMemoryEvaluationOptions(
      /*total_bytes=*/100));
  EXPECT_THAT(join_op->CreateIterator({&params_data},
                                      /*num_extra_slots=*/1, &memory_context),
              StatusIs(absl::StatusCode::kResourceExhausted,
                       HasSubstr("Out of memory")));
}

TEST_F(CreateIteratorTest, RightOuterJoin) {
  VariableId x1("x1"), x2("x2"), y1("y1"), y2("y2"), y1_prime("y1'"),
      y2_prime("y2'"), p("p");

  std::vector<std::vector<const SharedProtoState*>> shared_states_right;
  auto input_right = absl::WrapUnique(new TestRelationalOp(
      {x1, x2},
      CreateTestTupleDatas(
          {{Int64(1), GetProtoValue(1)}, {Int64(4), GetProtoValue(4)}},
          &shared_states_right),
      /*preserves_order=*/true));

  std::vector<std::vector<const SharedProtoState*>> shared_states_left;
  auto input_left = absl::WrapUnique(new TestRelationalOp(
      {y1, y2},
      CreateTestTupleDatas(
          {{Int64(2), GetProtoValue(2)}, {Int64(3), GetProtoValue(3)}},
          &shared_states_left),
      /*preserves_order=*/true));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_x1, DerefExpr::Create(x1, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_p, DerefExpr::Create(p, Int64Type()));

  std::vector<std::unique_ptr<ValueExpr>> add_args;
  add_args.push_back(std::move(deref_x1));
  add_args.push_back(std::move(deref_p));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto add_expr,
                       ScalarFunctionCallExpr::Create(
                           CreateFunction(FunctionKind::kAdd, Int64Type()),
                           std::move(add_args)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_y1, DerefExpr::Create(y1, Int64Type()));

  std::vector<std::unique_ptr<ValueExpr>> less_args;
  less_args.push_back(std::move(add_expr));
  less_args.push_back(std::move(deref_y1));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto less_expr,
                       ScalarFunctionCallExpr::Create(
                           CreateFunction(FunctionKind::kLess, BoolType()),
                           std::move(less_args)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_y1_again, DerefExpr::Create(y1, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_y2, DerefExpr::Create(y2, proto_type_));
  std::vector<std::unique_ptr<ExprArg>> left_outputs;
  left_outputs.push_back(
      std::make_unique<ExprArg>(y1_prime, std::move(deref_y1_again)));
  left_outputs.push_back(
      std::make_unique<ExprArg>(y2_prime, std::move(deref_y2)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto join_op,
      JoinOp::Create(JoinOp::kRightOuterJoin, EmptyHashJoinEqualityExprs(),
                     std::move(less_expr), std::move(input_left),
                     std::move(input_right), std::move(left_outputs),
                     /*right_outputs=*/{}));

  EXPECT_EQ(join_op->IteratorDebugString(),
            "JoinTupleIterator(RIGHT OUTER, left=TestTupleIterator, "
            "right=TestTupleIterator)");
  EXPECT_EQ(
      "JoinOp(RIGHT OUTER\n"
      "+-left_outputs: {\n"
      "| +-$y1' := $y1,\n"
      "| +-$y2' := $y2},\n"
      "+-right_outputs: {},\n"
      "+-hash_join_equality_left_exprs: {},\n"
      "+-hash_join_equality_right_exprs: {},\n"
      "+-remaining_condition: Less(Add($x1, $p), $y1),\n"
      "+-left_input: TestRelationalOp,\n"
      "+-right_input: TestRelationalOp)",
      join_op->DebugString());
  std::unique_ptr<TupleSchema> output_schema = join_op->CreateOutputSchema();
  EXPECT_THAT(output_schema->variables(),
              ElementsAre(y1_prime, y2_prime, x1, x2));

  EvaluationContext context((EvaluationOptions()));

  TupleSchema params_schema({p});
  TupleData params_data = CreateTestTupleData({Int64(0)});

  GOOGLESQL_ASSERT_OK(join_op->SetSchemasForEvaluation({&params_schema}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> iter,
      join_op->CreateIterator({&params_data}, /*num_extra_slots=*/1, &context));
  EXPECT_EQ(iter->DebugString(),
            "JoinTupleIterator(RIGHT OUTER, "
            "left=TestTupleIterator, right=TestTupleIterator)");
  EXPECT_TRUE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> data,
                       ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 3);
  EXPECT_THAT(
      data[0].slots(),
      ElementsAre(IsTupleSlotWith(Int64(2), IsNull()),
                  IsTupleSlotWith(GetProtoValue(2),
                                  HasRawPointer(shared_states_left[0][1])),
                  IsTupleSlotWith(Int64(1), IsNull()),
                  IsTupleSlotWith(GetProtoValue(1),
                                  HasRawPointer(shared_states_right[0][1])),
                  _));
  EXPECT_THAT(
      data[1].slots(),
      ElementsAre(IsTupleSlotWith(Int64(3), IsNull()),
                  IsTupleSlotWith(GetProtoValue(3),
                                  HasRawPointer(shared_states_left[1][1])),
                  IsTupleSlotWith(Int64(1), IsNull()),
                  IsTupleSlotWith(GetProtoValue(1),
                                  HasRawPointer(shared_states_right[0][1])),
                  _));
  EXPECT_THAT(
      data[2].slots(),
      ElementsAre(
          IsTupleSlotWith(NullInt64(), IsNull()),
          IsTupleSlotWith(Value::Null(proto_type_), Pointee(Eq(nullopt))),
          IsTupleSlotWith(Int64(4), IsNull()),
          IsTupleSlotWith(GetProtoValue(4),
                          HasRawPointer(shared_states_right[1][1])),
          _));

  // Do it again with cancellation.
  context.ClearDeadlineAndCancellationState();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      iter,
      join_op->CreateIterator({&params_data}, /*num_extra_slots=*/1, &context));
  GOOGLESQL_ASSERT_OK(context.CancelStatement());
  absl::Status status;
  data = ReadFromTupleIteratorFull(iter.get(), &status);
  EXPECT_TRUE(data.empty());
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kCancelled, _));

  // Check that scrambling works. We have to use a new iterator variable because
  // the context must outlive the iterator.
  EvaluationContext scramble_context(GetScramblingEvaluationOptions());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> scramble_iter,
      join_op->CreateIterator({&params_data},
                              /*num_extra_slots=*/1, &scramble_context));
  EXPECT_EQ(scramble_iter->DebugString(),
            "ReorderingTupleIterator(JoinTupleIterator(RIGHT OUTER, "
            "left=TestTupleIterator, right=TestTupleIterator))");
  EXPECT_FALSE(scramble_iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(data, ReadFromTupleIterator(scramble_iter.get()));
  ASSERT_EQ(data.size(), 3);
  // Check for the extra slot.
  EXPECT_EQ(data[0].num_slots(), 5);
  EXPECT_EQ(data[1].num_slots(), 5);
  EXPECT_EQ(data[2].num_slots(), 5);

  // Check that if the memory bound is too low, we return an error when loading
  // the right-hand side into memory.
  EvaluationContext memory_context(GetIntermediateMemoryEvaluationOptions(
      /*total_bytes=*/100));
  EXPECT_THAT(join_op->CreateIterator({&params_data},
                                      /*num_extra_slots=*/1, &memory_context),
              StatusIs(absl::StatusCode::kResourceExhausted,
                       HasSubstr("Out of memory")));
}

TEST_F(CreateIteratorTest, FullOuterJoin) {
  VariableId x1("x1"), x2("x2"), y1("y1"), y2("y2"), x1_prime("x1'"),
      x2_prime("x2'"), y1_prime("y1'"), y2_prime("y2'"), param("p");

  std::vector<std::vector<const SharedProtoState*>> shared_states1;
  auto input1 = absl::WrapUnique(new TestRelationalOp(
      {x1, x2},
      CreateTestTupleDatas(
          {{Int64(1), GetProtoValue(1)}, {Int64(2), GetProtoValue(2)}},
          &shared_states1),
      /*preserves_order=*/true));

  std::vector<std::vector<const SharedProtoState*>> shared_states2;
  auto input2 = absl::WrapUnique(new TestRelationalOp(
      {y1, y2},
      CreateTestTupleDatas(
          {{Int64(2), GetProtoValue(2)}, {Int64(3), GetProtoValue(3)}},
          &shared_states2),
      /*preserves_order=*/true));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_x1, DerefExpr::Create(x1, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_param, DerefExpr::Create(param, Int64Type()));

  std::vector<std::unique_ptr<ValueExpr>> add_args;
  add_args.push_back(std::move(deref_x1));
  add_args.push_back(std::move(deref_param));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto add_expr,
                       ScalarFunctionCallExpr::Create(
                           CreateFunction(FunctionKind::kAdd, Int64Type()),
                           std::move(add_args)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_y1, DerefExpr::Create(y1, Int64Type()));

  std::vector<std::unique_ptr<ValueExpr>> equal_args;
  equal_args.push_back(std::move(add_expr));
  equal_args.push_back(std::move(deref_y1));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto equal_expr,
                       ScalarFunctionCallExpr::Create(
                           CreateFunction(FunctionKind::kEqual, BoolType()),
                           std::move(equal_args)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_x1_again, DerefExpr::Create(x1, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_x2, DerefExpr::Create(x2, proto_type_));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_y1_again, DerefExpr::Create(y1, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_y2, DerefExpr::Create(y2, proto_type_));

  std::vector<std::unique_ptr<ExprArg>> left_outputs;
  left_outputs.push_back(
      std::make_unique<ExprArg>(x1_prime, std::move(deref_x1_again)));
  left_outputs.push_back(
      std::make_unique<ExprArg>(x2_prime, std::move(deref_x2)));

  std::vector<std::unique_ptr<ExprArg>> right_outputs;
  right_outputs.push_back(
      std::make_unique<ExprArg>(y1_prime, std::move(deref_y1_again)));
  right_outputs.push_back(
      std::make_unique<ExprArg>(y2_prime, std::move(deref_y2)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto join_op,
      JoinOp::Create(JoinOp::kFullOuterJoin, EmptyHashJoinEqualityExprs(),
                     std::move(equal_expr), std::move(input1),
                     std::move(input2), std::move(left_outputs),
                     std::move(right_outputs)));

  EXPECT_EQ(join_op->IteratorDebugString(),
            "JoinTupleIterator(FULL OUTER, left=TestTupleIterator, "
            "right=TestTupleIterator)");
  EXPECT_EQ(
      "JoinOp(FULL OUTER\n"
      "+-left_outputs: {\n"
      "| +-$x1' := $x1,\n"
      "| +-$x2' := $x2},\n"
      "+-right_outputs: {\n"
      "| +-$y1' := $y1,\n"
      "| +-$y2' := $y2},\n"
      "+-hash_join_equality_left_exprs: {},\n"
      "+-hash_join_equality_right_exprs: {},\n"
      "+-remaining_condition: Equal(Add($x1, $p), $y1),\n"
      "+-left_input: TestRelationalOp,\n"
      "+-right_input: TestRelationalOp)",
      join_op->DebugString());
  std::unique_ptr<TupleSchema> output_schema = join_op->CreateOutputSchema();
  EXPECT_THAT(output_schema->variables(),
              ElementsAre(x1_prime, x2_prime, y1_prime, y2_prime));

  EvaluationContext context((EvaluationOptions()));

  TupleSchema params_schema({param});
  TupleData params_data = CreateTestTupleData({Int64(0)});

  GOOGLESQL_ASSERT_OK(join_op->SetSchemasForEvaluation({&params_schema}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> iter,
      join_op->CreateIterator({&params_data}, /*num_extra_slots=*/1, &context));
  EXPECT_EQ(iter->DebugString(),
            "JoinTupleIterator(FULL OUTER, "
            "left=TestTupleIterator, right=TestTupleIterator)");
  EXPECT_TRUE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> data,
                       ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 3);
  EXPECT_THAT(
      data[0].slots(),
      ElementsAre(
          IsTupleSlotWith(Int64(1), IsNull()),
          IsTupleSlotWith(GetProtoValue(1),
                          HasRawPointer(shared_states1[0][1])),
          IsTupleSlotWith(NullInt64(), IsNull()),
          IsTupleSlotWith(Value::Null(proto_type_), Pointee(Eq(nullopt))), _));
  EXPECT_THAT(data[1].slots(),
              ElementsAre(IsTupleSlotWith(Int64(2), IsNull()),
                          IsTupleSlotWith(GetProtoValue(2),
                                          HasRawPointer(shared_states1[1][1])),
                          IsTupleSlotWith(Int64(2), IsNull()),
                          IsTupleSlotWith(GetProtoValue(2),
                                          HasRawPointer(shared_states2[0][1])),
                          _));
  EXPECT_THAT(data[2].slots(),
              ElementsAre(IsTupleSlotWith(NullInt64(), IsNull()),
                          IsTupleSlotWith(Value::Null(proto_type_),
                                          Pointee(Eq(nullopt))),
                          IsTupleSlotWith(Int64(3), IsNull()),
                          IsTupleSlotWith(GetProtoValue(3),
                                          HasRawPointer(shared_states2[1][1])),
                          _));

  // Check that if the memory bound is too low, we return an error when loading
  // the right-hand side into memory.
  EvaluationContext memory_context(GetIntermediateMemoryEvaluationOptions(
      /*total_bytes=*/100));
  EXPECT_THAT(join_op->CreateIterator({&params_data},
                                      /*num_extra_slots=*/1, &memory_context),
              StatusIs(absl::StatusCode::kResourceExhausted,
                       HasSubstr("Out of memory")));
}

TEST_F(CreateIteratorTest, FullOuterAllRightTuplesJoin) {
  VariableId x("x"), y("y"), x_prime("x'"), y_prime("y'"), param("p");

  auto input1 = absl::WrapUnique(
      new TestRelationalOp({x}, CreateTestTupleDatas({{Int64(1)}, {Int64(2)}}),
                           /*preserves_order=*/true));

  auto input2 = absl::WrapUnique(
      new TestRelationalOp({y}, CreateTestTupleDatas({{Int64(2)}}),
                           /*preserves_order=*/true));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_x, DerefExpr::Create(x, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_param, DerefExpr::Create(param, Int64Type()));

  std::vector<std::unique_ptr<ValueExpr>> add_args;
  add_args.push_back(std::move(deref_x));
  add_args.push_back(std::move(deref_param));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto add_expr,
                       ScalarFunctionCallExpr::Create(
                           CreateFunction(FunctionKind::kAdd, Int64Type()),
                           std::move(add_args)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_y, DerefExpr::Create(y, Int64Type()));

  std::vector<std::unique_ptr<ValueExpr>> equal_args;
  equal_args.push_back(std::move(add_expr));
  equal_args.push_back(std::move(deref_y));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto equal_expr,
                       ScalarFunctionCallExpr::Create(
                           CreateFunction(FunctionKind::kEqual, BoolType()),
                           std::move(equal_args)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_x_again, DerefExpr::Create(x, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_y_again, DerefExpr::Create(y, Int64Type()));

  std::vector<std::unique_ptr<ExprArg>> left_outputs;
  left_outputs.push_back(
      std::make_unique<ExprArg>(x_prime, std::move(deref_x_again)));

  std::vector<std::unique_ptr<ExprArg>> right_outputs;
  right_outputs.push_back(
      std::make_unique<ExprArg>(y_prime, std::move(deref_y_again)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto join_op,
      JoinOp::Create(JoinOp::kFullOuterJoin, EmptyHashJoinEqualityExprs(),
                     std::move(equal_expr), std::move(input1),
                     std::move(input2), std::move(left_outputs),
                     std::move(right_outputs)));

  EXPECT_EQ(
      "JoinOp(FULL OUTER\n"
      "+-left_outputs: {\n"
      "| +-$x' := $x},\n"
      "+-right_outputs: {\n"
      "| +-$y' := $y},\n"
      "+-hash_join_equality_left_exprs: {},\n"
      "+-hash_join_equality_right_exprs: {},\n"
      "+-remaining_condition: Equal(Add($x, $p), $y),\n"
      "+-left_input: TestRelationalOp,\n"
      "+-right_input: TestRelationalOp)",
      join_op->DebugString());
  std::unique_ptr<TupleSchema> output_schema = join_op->CreateOutputSchema();
  EXPECT_THAT(output_schema->variables(), ElementsAre(x_prime, y_prime));

  EvaluationContext context((EvaluationOptions()));

  TupleSchema params_schema({param});
  TupleData params_data = CreateTestTupleData({Int64(0)});

  GOOGLESQL_ASSERT_OK(join_op->SetSchemasForEvaluation({&params_schema}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> iter,
      join_op->CreateIterator({&params_data}, /*num_extra_slots=*/1, &context));
  EXPECT_EQ(iter->DebugString(),
            "JoinTupleIterator(FULL OUTER, "
            "left=TestTupleIterator, right=TestTupleIterator)");
  EXPECT_TRUE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> data,
                       ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 2);
  EXPECT_THAT(data[0].slots(),
              ElementsAre(IsTupleSlotWith(Int64(1), IsNull()),
                          IsTupleSlotWith(NullInt64(), IsNull()), _));
  EXPECT_THAT(data[1].slots(),
              ElementsAre(IsTupleSlotWith(Int64(2), IsNull()),
                          IsTupleSlotWith(Int64(2), IsNull()), _));

  // Check that if the memory bound is too low, we return an error when loading
  // the right-hand side into memory.
  EvaluationContext memory_context(GetIntermediateMemoryEvaluationOptions(
      /*total_bytes=*/10));
  EXPECT_THAT(join_op->CreateIterator({&params_data},
                                      /*num_extra_slots=*/1, &memory_context),
              StatusIs(absl::StatusCode::kResourceExhausted,
                       HasSubstr("Out of memory")));
}

TEST_F(CreateIteratorTest, CrossApply) {
  VariableId x("x"), x2("x2"), y("y"), z("z"), p("p");

  // The left-hand side is three rows: (x:1), (x:4), and (x:6)
  auto input1 = absl::WrapUnique(new TestRelationalOp(
      {x}, CreateTestTupleDatas({{Int64(1)}, {Int64(4)}, {Int64(6)}}),
      /*preserves_order=*/true));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_x, DerefExpr::Create(x, Int64Type()));

  std::vector<std::unique_ptr<ExprArg>> let_assign;
  let_assign.push_back(std::make_unique<ExprArg>(x2, std::move(deref_x)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_x2, DerefExpr::Create(x2, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_y, DerefExpr::Create(y, Int64Type()));

  std::vector<std::unique_ptr<ValueExpr>> add_args;
  add_args.push_back(std::move(deref_x2));
  add_args.push_back(std::move(deref_y));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto add_expr,
                       ScalarFunctionCallExpr::Create(
                           CreateFunction(FunctionKind::kAdd, Int64Type()),
                           std::move(add_args)));

  std::vector<std::unique_ptr<ExprArg>> map;
  map.push_back(std::make_unique<ExprArg>(z, std::move(add_expr)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto let_body,
      ComputeOp::Create(
          std::move(map),
          absl::WrapUnique(new TestRelationalOp(
              {y}, CreateTestTupleDatas({{Int64(-1)}, {Int64(1)}}),
              /*preserves_order=*/true))));

  // The right-hand side is two rows: (y:-1, z:(x+y)) and (y:1, z:(x+y)), where
  // x is the value of the left-hand side.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto input2,
                       LetOp::Create(std::move(let_assign), /*cpp_assign=*/{},
                                     std::move(let_body)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_x_again, DerefExpr::Create(x, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_p, DerefExpr::Create(p, Int64Type()));

  std::vector<std::unique_ptr<ValueExpr>> less1_args;
  less1_args.push_back(std::move(deref_x_again));
  less1_args.push_back(std::move(deref_p));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto less1,
                       ScalarFunctionCallExpr::Create(
                           CreateFunction(FunctionKind::kLess, BoolType()),
                           std::move(less1_args)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_x_one_more_time,
                       DerefExpr::Create(x, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_z, DerefExpr::Create(z, Int64Type()));

  std::vector<std::unique_ptr<ValueExpr>> less2_args;
  less2_args.push_back(std::move(deref_x_one_more_time));
  less2_args.push_back(std::move(deref_z));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto less2,
                       ScalarFunctionCallExpr::Create(
                           CreateFunction(FunctionKind::kLess, BoolType()),
                           std::move(less2_args)));

  std::vector<std::unique_ptr<ValueExpr>> and_args;
  and_args.push_back(std::move(less1));
  and_args.push_back(std::move(less2));

  // The join condition is x < p AND x < z, where p is a parameter that is
  // always 5. For the first two rows on the left-hand side, there is one tuple
  // on the right-hand side that satisfies the join condition, but for the third
  // row on the left-hand side, the join condition is not satisfied for any
  // tuple on the right-hand side.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto join_expr,
      ScalarFunctionCallExpr::Create(
          CreateFunction(FunctionKind::kAnd, BoolType()), std::move(and_args)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto join_op,
      JoinOp::Create(JoinOp::kCrossApply, EmptyHashJoinEqualityExprs(),
                     std::move(join_expr), std::move(input1), std::move(input2),
                     /*left_outputs=*/{}, /*right_outputs=*/{}));
  EXPECT_EQ(
      join_op->IteratorDebugString(),
      "JoinTupleIterator(CROSS APPLY, left=TestTupleIterator, "
      "right=LetOpTupleIterator(ComputeTupleIterator(TestTupleIterator)))");
  EXPECT_EQ(
      "JoinOp(CROSS APPLY\n"
      "+-hash_join_equality_left_exprs: {},\n"
      "+-hash_join_equality_right_exprs: {},\n"
      "+-remaining_condition: And(Less($x, $p), Less($x, $z)),\n"
      "+-left_input: TestRelationalOp,\n"
      "+-right_input: LetOp(\n"
      "  +-assign: {\n"
      "  | +-$x2 := $x},\n"
      "  +-cpp_assign: {},\n"
      "  +-body: ComputeOp(\n"
      "    +-map: {\n"
      "    | +-$z := Add($x2, $y)},\n"
      "    +-input: TestRelationalOp)))",
      join_op->DebugString());
  std::unique_ptr<TupleSchema> output_schema = join_op->CreateOutputSchema();
  EXPECT_THAT(output_schema->variables(), ElementsAre(x, y, z));

  EvaluationContext context((EvaluationOptions()));

  TupleSchema params_schema({p});
  TupleData params_data = CreateTestTupleData({Int64(5)});

  GOOGLESQL_ASSERT_OK(join_op->SetSchemasForEvaluation({&params_schema}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> iter,
      join_op->CreateIterator({&params_data}, /*num_extra_slots=*/1, &context));
  EXPECT_EQ(
      iter->DebugString(),
      "JoinTupleIterator(CROSS APPLY, left=TestTupleIterator, "
      "right=LetOpTupleIterator(ComputeTupleIterator(TestTupleIterator)))");
  EXPECT_TRUE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> data,
                       ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 2);
  EXPECT_THAT(data[0].slots(),
              ElementsAre(IsTupleSlotWith(Int64(1), IsNull()),
                          IsTupleSlotWith(Int64(1), IsNull()),
                          IsTupleSlotWith(Int64(2), IsNull()), _));
  EXPECT_THAT(data[1].slots(),
              ElementsAre(IsTupleSlotWith(Int64(4), IsNull()),
                          IsTupleSlotWith(Int64(1), IsNull()),
                          IsTupleSlotWith(Int64(5), IsNull()), _));

  // Do it again with cancellation.
  context.ClearDeadlineAndCancellationState();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      iter,
      join_op->CreateIterator({&params_data}, /*num_extra_slots=*/1, &context));
  GOOGLESQL_ASSERT_OK(context.CancelStatement());
  absl::Status status;
  data = ReadFromTupleIteratorFull(iter.get(), &status);
  EXPECT_TRUE(data.empty());
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kCancelled, _));

  // Check that scrambling works. We have to use a new iterator variable because
  // the context must outlive the iterator.
  EvaluationContext scramble_context(GetScramblingEvaluationOptions());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> scramble_iter,
      join_op->CreateIterator({&params_data},
                              /*num_extra_slots=*/1, &scramble_context));
  EXPECT_EQ(
      scramble_iter->DebugString(),
      "ReorderingTupleIterator(JoinTupleIterator(CROSS APPLY, "
      "left=TestTupleIterator, "
      "right=LetOpTupleIterator(ComputeTupleIterator(TestTupleIterator))))");
  EXPECT_FALSE(scramble_iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(data, ReadFromTupleIterator(scramble_iter.get()));
  ASSERT_EQ(data.size(), 2);
  // Check for the extra slot.
  EXPECT_EQ(data[0].num_slots(), 4);
  EXPECT_EQ(data[1].num_slots(), 4);

  // Check that if the memory bound is too low, we return an error when loading
  // the right-hand side into memory.
  EvaluationContext memory_context(GetIntermediateMemoryEvaluationOptions(
      /*total_bytes=*/100));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> memory_iter,
      join_op->CreateIterator({&params_data},
                              /*num_extra_slots=*/1, &memory_context));
  EXPECT_THAT(ReadFromTupleIterator(memory_iter.get()),
              StatusIs(absl::StatusCode::kResourceExhausted,
                       HasSubstr("Out of memory")));
}

TEST_F(CreateIteratorTest, OuterApply) {
  VariableId x("x"), x2("x2"), y("y"), z("z"), z2("z2"), p("p");

  // Three rows: (x:1), (x:4), and (x:6)
  auto input1 = absl::WrapUnique(new TestRelationalOp(
      {x}, CreateTestTupleDatas({{Int64(1)}, {Int64(4)}, {Int64(6)}}),
      /*preserves_order=*/true));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_x, DerefExpr::Create(x, Int64Type()));

  std::vector<std::unique_ptr<ExprArg>> let_assign;
  let_assign.push_back(std::make_unique<ExprArg>(x2, std::move(deref_x)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_x2, DerefExpr::Create(x2, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_y, DerefExpr::Create(y, Int64Type()));

  std::vector<std::unique_ptr<ValueExpr>> add_args;
  add_args.push_back(std::move(deref_x2));
  add_args.push_back(std::move(deref_y));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto add_expr,
                       ScalarFunctionCallExpr::Create(
                           CreateFunction(FunctionKind::kAdd, Int64Type()),
                           std::move(add_args)));

  std::vector<std::unique_ptr<ExprArg>> map;
  map.push_back(std::make_unique<ExprArg>(z, std::move(add_expr)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto let_body,
      ComputeOp::Create(
          std::move(map),
          absl::WrapUnique(new TestRelationalOp(
              {y}, CreateTestTupleDatas({{Int64(-1)}, {Int64(1)}}),
              /*preserves_order=*/true))));

  // The right-hand side is two rows: (y:-1, z:(x+y)) and (y:1, z:(x+y)), where
  // x is the value of the left-hand side.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto input2,
                       LetOp::Create(std::move(let_assign), /*cpp_assign=*/{},
                                     std::move(let_body)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_x_again, DerefExpr::Create(x, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_p, DerefExpr::Create(p, Int64Type()));

  std::vector<std::unique_ptr<ValueExpr>> less1_args;
  less1_args.push_back(std::move(deref_x_again));
  less1_args.push_back(std::move(deref_p));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto less1,
                       ScalarFunctionCallExpr::Create(
                           CreateFunction(FunctionKind::kLess, BoolType()),
                           std::move(less1_args)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_x_one_more_time,
                       DerefExpr::Create(x, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_z, DerefExpr::Create(z, Int64Type()));

  std::vector<std::unique_ptr<ValueExpr>> less2_args;
  less2_args.push_back(std::move(deref_x_one_more_time));
  less2_args.push_back(std::move(deref_z));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto less2,
                       ScalarFunctionCallExpr::Create(
                           CreateFunction(FunctionKind::kLess, BoolType()),
                           std::move(less2_args)));

  std::vector<std::unique_ptr<ValueExpr>> and_args;
  and_args.push_back(std::move(less1));
  and_args.push_back(std::move(less2));

  // The join condition is x < p AND x < z, where p is a parameter that is
  // always 5. For the first two rows on the left-hand side, there is one tuple
  // on the right-hand side that satisfies the join condition, but for the third
  // row on the left-hand side, the join condition is not satisfied for any
  // tuple on the right-hand side.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto join_expr,
      ScalarFunctionCallExpr::Create(
          CreateFunction(FunctionKind::kAnd, BoolType()), std::move(and_args)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_z_again, DerefExpr::Create(z, Int64Type()));

  std::vector<std::unique_ptr<ExprArg>> right_outputs;
  right_outputs.push_back(
      std::make_unique<ExprArg>(z2, std::move(deref_z_again)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto join_op,
      JoinOp::Create(JoinOp::kOuterApply, EmptyHashJoinEqualityExprs(),
                     std::move(join_expr), std::move(input1), std::move(input2),
                     /*left_outputs=*/{},
                     /*right_outputs=*/std::move(right_outputs)));
  EXPECT_EQ(
      join_op->IteratorDebugString(),
      "JoinTupleIterator(OUTER APPLY, left=TestTupleIterator, "
      "right=LetOpTupleIterator(ComputeTupleIterator(TestTupleIterator)))");
  EXPECT_EQ(
      "JoinOp(OUTER APPLY\n"
      "+-right_outputs: {\n"
      "| +-$z2 := $z},\n"
      "+-hash_join_equality_left_exprs: {},\n"
      "+-hash_join_equality_right_exprs: {},\n"
      "+-remaining_condition: And(Less($x, $p), Less($x, $z)),\n"
      "+-left_input: TestRelationalOp,\n"
      "+-right_input: LetOp(\n"
      "  +-assign: {\n"
      "  | +-$x2 := $x},\n"
      "  +-cpp_assign: {},\n"
      "  +-body: ComputeOp(\n"
      "    +-map: {\n"
      "    | +-$z := Add($x2, $y)},\n"
      "    +-input: TestRelationalOp)))",
      join_op->DebugString());
  std::unique_ptr<TupleSchema> output_schema = join_op->CreateOutputSchema();
  EXPECT_THAT(output_schema->variables(), ElementsAre(x, z2));

  EvaluationContext context((EvaluationOptions()));

  TupleSchema params_schema({p});
  TupleData params_data = CreateTestTupleData({Int64(5)});

  GOOGLESQL_ASSERT_OK(join_op->SetSchemasForEvaluation({&params_schema}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> iter,
      join_op->CreateIterator({&params_data}, /*num_extra_slots=*/1, &context));
  EXPECT_EQ(
      iter->DebugString(),
      "JoinTupleIterator(OUTER APPLY, left=TestTupleIterator, "
      "right=LetOpTupleIterator(ComputeTupleIterator(TestTupleIterator)))");
  EXPECT_TRUE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> data,
                       ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 3);
  EXPECT_THAT(data[0].slots(),
              ElementsAre(IsTupleSlotWith(Int64(1), IsNull()),
                          IsTupleSlotWith(Int64(2), IsNull()), _));
  EXPECT_THAT(data[1].slots(),
              ElementsAre(IsTupleSlotWith(Int64(4), IsNull()),
                          IsTupleSlotWith(Int64(5), IsNull()), _));
  EXPECT_THAT(data[2].slots(),
              ElementsAre(IsTupleSlotWith(Int64(6), IsNull()),
                          IsTupleSlotWith(NullInt64(), IsNull()), _));

  // Do it again with cancellation.
  context.ClearDeadlineAndCancellationState();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      iter,
      join_op->CreateIterator({&params_data}, /*num_extra_slots=*/1, &context));
  GOOGLESQL_ASSERT_OK(context.CancelStatement());
  absl::Status status;
  data = ReadFromTupleIteratorFull(iter.get(), &status);
  EXPECT_TRUE(data.empty());
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kCancelled, _));

  // Check that scrambling works. We have to use a new iterator variable because
  // the context must outlive the iterator.
  EvaluationContext scramble_context(GetScramblingEvaluationOptions());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> scramble_iter,
      join_op->CreateIterator({&params_data},
                              /*num_extra_slots=*/1, &scramble_context));
  EXPECT_EQ(
      scramble_iter->DebugString(),
      "ReorderingTupleIterator(JoinTupleIterator(OUTER APPLY, "
      "left=TestTupleIterator, "
      "right=LetOpTupleIterator(ComputeTupleIterator(TestTupleIterator))))");
  EXPECT_FALSE(scramble_iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(data, ReadFromTupleIterator(scramble_iter.get()));
  ASSERT_EQ(data.size(), 3);
  // Check for the extra slot.
  EXPECT_EQ(data[0].num_slots(), 3);
  EXPECT_EQ(data[1].num_slots(), 3);
  EXPECT_EQ(data[2].num_slots(), 3);

  // Check that if the memory bound is too low, we return an error when loading
  // the right-hand side into memory.
  EvaluationContext memory_context(GetIntermediateMemoryEvaluationOptions(
      /*total_bytes=*/100));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> memory_iter,
      join_op->CreateIterator({&params_data},
                              /*num_extra_slots=*/1, &memory_context));
  EXPECT_THAT(ReadFromTupleIterator(memory_iter.get()),
              StatusIs(absl::StatusCode::kResourceExhausted,
                       HasSubstr("Out of memory")));
}

TEST_F(CreateIteratorTest, InnerHashJoin) {
  VariableId x1("x1"), x2("x2"), y1("y1"), y2("y2"), p("p");

  std::vector<std::vector<const SharedProtoState*>> shared_states1;
  auto input1 = absl::WrapUnique(new TestRelationalOp(
      {x1, x2},
      CreateTestTupleDatas(
          {{Int64(1), GetProtoValue(1)}, {Int64(2), GetProtoValue(1)}},
          &shared_states1),
      /*preserves_order=*/true));

  std::vector<std::vector<const SharedProtoState*>> shared_states2;
  auto input2 = absl::WrapUnique(
      new TestRelationalOp({y1, y2},
                           CreateTestTupleDatas({{Int64(1), GetProtoValue(2)},
                                                 {Int64(1), GetProtoValue(3)},
                                                 {Int64(3), GetProtoValue(1)}},
                                                &shared_states2),
                           /*preserves_order=*/true));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_x1, DerefExpr::Create(x1, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto left_deref_p, DerefExpr::Create(p, Int64Type()));

  std::vector<std::unique_ptr<ValueExpr>> left_add_args;
  left_add_args.push_back(std::move(deref_x1));
  left_add_args.push_back(std::move(left_deref_p));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto left_add_expr,
                       ScalarFunctionCallExpr::Create(
                           CreateFunction(FunctionKind::kAdd, Int64Type()),
                           std::move(left_add_args)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_y1, DerefExpr::Create(y1, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto right_deref_p, DerefExpr::Create(p, Int64Type()));

  std::vector<std::unique_ptr<ValueExpr>> right_add_args;
  right_add_args.push_back(std::move(deref_y1));
  right_add_args.push_back(std::move(right_deref_p));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto right_add_expr,
                       ScalarFunctionCallExpr::Create(
                           CreateFunction(FunctionKind::kAdd, BoolType()),
                           std::move(right_add_args)));

  VariableId a("a"), b("b");
  JoinOp::HashJoinEqualityExprs equality_expr;
  equality_expr.left_expr =
      std::make_unique<ExprArg>(a, std::move(left_add_expr));
  equality_expr.right_expr =
      std::make_unique<ExprArg>(b, std::move(right_add_expr));

  std::vector<JoinOp::HashJoinEqualityExprs> equality_exprs;
  equality_exprs.push_back(std::move(equality_expr));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto true_expr, ConstExpr::Create(Bool(true)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto join_op,
      JoinOp::Create(JoinOp::kInnerJoin, std::move(equality_exprs),
                     std::move(true_expr), std::move(input1), std::move(input2),
                     /*left_outputs=*/{}, /*right_outputs=*/{}));
  EXPECT_EQ(join_op->IteratorDebugString(),
            "JoinTupleIterator(INNER, left=TestTupleIterator, "
            "right=TestTupleIterator)");
  EXPECT_EQ(
      "JoinOp(INNER\n"
      "+-hash_join_equality_left_exprs: {\n"
      "| +-$a := Add($x1, $p)},\n"
      "+-hash_join_equality_right_exprs: {\n"
      "| +-$b := Add($y1, $p)},\n"
      "+-remaining_condition: ConstExpr(true),\n"
      "+-left_input: TestRelationalOp,\n"
      "+-right_input: TestRelationalOp)",
      join_op->DebugString());
  std::unique_ptr<TupleSchema> output_schema = join_op->CreateOutputSchema();
  EXPECT_THAT(output_schema->variables(), ElementsAre(x1, x2, y1, y2));

  EvaluationContext context((EvaluationOptions()));

  TupleSchema params_schema({p});
  TupleData params_data = CreateTestTupleData({Int64(0)});

  GOOGLESQL_ASSERT_OK(join_op->SetSchemasForEvaluation({&params_schema}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> iter,
      join_op->CreateIterator({&params_data}, /*num_extra_slots=*/1, &context));
  EXPECT_EQ(iter->DebugString(),
            "JoinTupleIterator(INNER, "
            "left=TestTupleIterator, right=TestTupleIterator)");
  EXPECT_TRUE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> data,
                       ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 2);
  EXPECT_THAT(data[0].slots(),
              ElementsAre(IsTupleSlotWith(Int64(1), IsNull()),
                          IsTupleSlotWith(GetProtoValue(1),
                                          HasRawPointer(shared_states1[0][1])),
                          IsTupleSlotWith(Int64(1), IsNull()),
                          IsTupleSlotWith(GetProtoValue(2),
                                          HasRawPointer(shared_states2[0][1])),
                          _));
  EXPECT_THAT(data[1].slots(),
              ElementsAre(IsTupleSlotWith(Int64(1), IsNull()),
                          IsTupleSlotWith(GetProtoValue(1),
                                          HasRawPointer(shared_states1[0][1])),
                          IsTupleSlotWith(Int64(1), IsNull()),
                          IsTupleSlotWith(GetProtoValue(3),
                                          HasRawPointer(shared_states2[1][1])),
                          _));

  // Do it again with cancellation.
  context.ClearDeadlineAndCancellationState();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      iter,
      join_op->CreateIterator({&params_data}, /*num_extra_slots=*/1, &context));
  GOOGLESQL_ASSERT_OK(context.CancelStatement());
  absl::Status status;
  data = ReadFromTupleIteratorFull(iter.get(), &status);
  EXPECT_TRUE(data.empty());
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kCancelled, _));

  // Check that scrambling works. We have to use a new iterator variable because
  // the context must outlive the iterator.
  EvaluationContext scramble_context(GetScramblingEvaluationOptions());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> scramble_iter,
      join_op->CreateIterator({&params_data},
                              /*num_extra_slots=*/1, &scramble_context));
  EXPECT_EQ(scramble_iter->DebugString(),
            "ReorderingTupleIterator(JoinTupleIterator(INNER, "
            "left=TestTupleIterator, right=TestTupleIterator))");
  EXPECT_FALSE(scramble_iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(data, ReadFromTupleIterator(scramble_iter.get()));
  ASSERT_EQ(data.size(), 2);
  // Check for the extra slot.
  EXPECT_EQ(data[0].num_slots(), 5);
  EXPECT_EQ(data[1].num_slots(), 5);

  // Check that if the memory bound is too low, we get an error.
  EvaluationContext memory_context(GetIntermediateMemoryEvaluationOptions(
      /*total_bytes=*/100));
  EXPECT_THAT(join_op->CreateIterator({&params_data},
                                      /*num_extra_slots=*/1, &memory_context),
              StatusIs(absl::StatusCode::kResourceExhausted,
                       HasSubstr("Out of memory")));
}

TEST_F(CreateIteratorTest, FullOuterHashJoin) {
  VariableId x1("x1"), x2("x2"), x1_prime("x1'"), x2_prime("x2'"), y1("y1"),
      y2("y2"), y1_prime("y1'"), y2_prime("y2'"), p("p");

  std::vector<std::vector<const SharedProtoState*>> shared_states1;
  auto input1 = absl::WrapUnique(new TestRelationalOp(
      {x1, x2},
      CreateTestTupleDatas(
          {{Int64(1), GetProtoValue(1)}, {Int64(2), GetProtoValue(1)}},
          &shared_states1),
      /*preserves_order=*/true));

  std::vector<std::vector<const SharedProtoState*>> shared_states2;
  auto input2 = absl::WrapUnique(
      new TestRelationalOp({y1, y2},
                           CreateTestTupleDatas({{Int64(1), GetProtoValue(2)},
                                                 {Int64(1), GetProtoValue(3)},
                                                 {Int64(3), GetProtoValue(1)}},
                                                &shared_states2),
                           /*preserves_order=*/true));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_x1, DerefExpr::Create(x1, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto left_deref_p, DerefExpr::Create(p, Int64Type()));

  std::vector<std::unique_ptr<ValueExpr>> left_add_args;
  left_add_args.push_back(std::move(deref_x1));
  left_add_args.push_back(std::move(left_deref_p));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto left_add_expr,
                       ScalarFunctionCallExpr::Create(
                           CreateFunction(FunctionKind::kAdd, Int64Type()),
                           std::move(left_add_args)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_y1, DerefExpr::Create(y1, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto right_deref_p, DerefExpr::Create(p, Int64Type()));

  std::vector<std::unique_ptr<ValueExpr>> right_add_args;
  right_add_args.push_back(std::move(deref_y1));
  right_add_args.push_back(std::move(right_deref_p));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto right_add_expr,
                       ScalarFunctionCallExpr::Create(
                           CreateFunction(FunctionKind::kAdd, BoolType()),
                           std::move(right_add_args)));

  VariableId a("a"), b("b");
  JoinOp::HashJoinEqualityExprs equality_expr;
  equality_expr.left_expr =
      std::make_unique<ExprArg>(a, std::move(left_add_expr));
  equality_expr.right_expr =
      std::make_unique<ExprArg>(b, std::move(right_add_expr));

  std::vector<JoinOp::HashJoinEqualityExprs> equality_exprs;
  equality_exprs.push_back(std::move(equality_expr));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto true_expr, ConstExpr::Create(Bool(true)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_x1_output,
                       DerefExpr::Create(x1, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_x2_output,
                       DerefExpr::Create(x2, proto_type_));

  std::vector<std::unique_ptr<ExprArg>> left_outputs;
  left_outputs.push_back(
      std::make_unique<ExprArg>(x1_prime, std::move(deref_x1_output)));
  left_outputs.push_back(
      std::make_unique<ExprArg>(x2_prime, std::move(deref_x2_output)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_y1_output,
                       DerefExpr::Create(y1, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_y2_output,
                       DerefExpr::Create(y2, proto_type_));

  std::vector<std::unique_ptr<ExprArg>> right_outputs;
  right_outputs.push_back(
      std::make_unique<ExprArg>(y1_prime, std::move(deref_y1_output)));
  right_outputs.push_back(
      std::make_unique<ExprArg>(y2_prime, std::move(deref_y2_output)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto join_op,
      JoinOp::Create(JoinOp::kFullOuterJoin, std::move(equality_exprs),
                     std::move(true_expr), std::move(input1), std::move(input2),
                     std::move(left_outputs), std::move(right_outputs)));
  EXPECT_EQ(join_op->IteratorDebugString(),
            "JoinTupleIterator(FULL OUTER, left=TestTupleIterator, "
            "right=TestTupleIterator)");
  EXPECT_EQ(
      "JoinOp(FULL OUTER\n"
      "+-left_outputs: {\n"
      "| +-$x1' := $x1,\n"
      "| +-$x2' := $x2},\n"
      "+-right_outputs: {\n"
      "| +-$y1' := $y1,\n"
      "| +-$y2' := $y2},\n"
      "+-hash_join_equality_left_exprs: {\n"
      "| +-$a := Add($x1, $p)},\n"
      "+-hash_join_equality_right_exprs: {\n"
      "| +-$b := Add($y1, $p)},\n"
      "+-remaining_condition: ConstExpr(true),\n"
      "+-left_input: TestRelationalOp,\n"
      "+-right_input: TestRelationalOp)",
      join_op->DebugString());
  std::unique_ptr<TupleSchema> output_schema = join_op->CreateOutputSchema();
  EXPECT_THAT(output_schema->variables(),
              ElementsAre(x1_prime, x2_prime, y1_prime, y2_prime));

  EvaluationContext context((EvaluationOptions()));

  TupleSchema params_schema({p});
  TupleData params_data = CreateTestTupleData({Int64(0)});

  GOOGLESQL_ASSERT_OK(join_op->SetSchemasForEvaluation({&params_schema}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> iter,
      join_op->CreateIterator({&params_data}, /*num_extra_slots=*/1, &context));
  EXPECT_EQ(iter->DebugString(),
            "JoinTupleIterator(FULL OUTER, "
            "left=TestTupleIterator, right=TestTupleIterator)");
  EXPECT_TRUE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> data,
                       ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 4);
  EXPECT_THAT(data[0].slots(),
              ElementsAre(IsTupleSlotWith(Int64(1), IsNull()),
                          IsTupleSlotWith(GetProtoValue(1),
                                          HasRawPointer(shared_states1[0][1])),
                          IsTupleSlotWith(Int64(1), IsNull()),
                          IsTupleSlotWith(GetProtoValue(2),
                                          HasRawPointer(shared_states2[0][1])),
                          _));
  EXPECT_THAT(data[1].slots(),
              ElementsAre(IsTupleSlotWith(Int64(1), IsNull()),
                          IsTupleSlotWith(GetProtoValue(1),
                                          HasRawPointer(shared_states1[0][1])),
                          IsTupleSlotWith(Int64(1), IsNull()),
                          IsTupleSlotWith(GetProtoValue(3),
                                          HasRawPointer(shared_states2[1][1])),
                          _));
  EXPECT_THAT(
      data[2].slots(),
      ElementsAre(
          IsTupleSlotWith(Int64(2), IsNull()),
          IsTupleSlotWith(GetProtoValue(1),
                          HasRawPointer(shared_states1[1][1])),
          IsTupleSlotWith(NullInt64(), IsNull()),
          IsTupleSlotWith(Value::Null(proto_type_), Pointee(Eq(nullopt))), _));
  EXPECT_THAT(data[3].slots(),
              ElementsAre(IsTupleSlotWith(NullInt64(), IsNull()),
                          IsTupleSlotWith(Value::Null(proto_type_),
                                          Pointee(Eq(nullopt))),
                          IsTupleSlotWith(Int64(3), IsNull()),
                          IsTupleSlotWith(GetProtoValue(1),
                                          HasRawPointer(shared_states2[2][1])),
                          _));

  // Do it again with cancellation.
  context.ClearDeadlineAndCancellationState();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      iter,
      join_op->CreateIterator({&params_data}, /*num_extra_slots=*/1, &context));
  GOOGLESQL_ASSERT_OK(context.CancelStatement());
  absl::Status status;
  data = ReadFromTupleIteratorFull(iter.get(), &status);
  EXPECT_TRUE(data.empty());
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kCancelled, _));

  // Check that scrambling works. We have to use a new iterator variable because
  // the context must outlive the iterator.
  EvaluationContext scramble_context(GetScramblingEvaluationOptions());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> scramble_iter,
      join_op->CreateIterator({&params_data},
                              /*num_extra_slots=*/1, &scramble_context));
  EXPECT_EQ(scramble_iter->DebugString(),
            "ReorderingTupleIterator(JoinTupleIterator(FULL OUTER, "
            "left=TestTupleIterator, right=TestTupleIterator))");
  EXPECT_FALSE(scramble_iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(data, ReadFromTupleIterator(scramble_iter.get()));
  ASSERT_EQ(data.size(), 4);
  // Check for the extra slot.
  EXPECT_EQ(data[0].num_slots(), 5);
  EXPECT_EQ(data[1].num_slots(), 5);
  EXPECT_EQ(data[2].num_slots(), 5);
  EXPECT_EQ(data[3].num_slots(), 5);

  // Check that if the memory bound is too low, we return an error when loading
  // the right-hand side into memory.
  EvaluationContext memory_context(GetIntermediateMemoryEvaluationOptions(
      /*total_bytes=*/100));
  EXPECT_THAT(join_op->CreateIterator({&params_data},
                                      /*num_extra_slots=*/1, &memory_context),
              StatusIs(absl::StatusCode::kResourceExhausted,
                       HasSubstr("Out of memory")));
}

TEST_F(CreateIteratorTest, SortOpTotalOrder) {
  VariableId a("a"), b("b"), c("c"), param("param"), k("k"), v1("v1"), v2("v2"),
      v3("v3");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_a, DerefExpr::Create(a, Int64Type()));

  std::vector<std::unique_ptr<KeyArg>> keys;
  keys.push_back(
      std::make_unique<KeyArg>(k, std::move(deref_a), KeyArg::kDescending));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_b, DerefExpr::Create(b, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_c, DerefExpr::Create(c, proto_type_));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_param, DerefExpr::Create(param, Int64Type()));

  std::vector<std::unique_ptr<ExprArg>> values;
  values.push_back(std::make_unique<ExprArg>(v1, std::move(deref_b)));
  values.push_back(std::make_unique<ExprArg>(v2, std::move(deref_c)));
  values.push_back(std::make_unique<ExprArg>(v3, std::move(deref_param)));

  std::vector<std::vector<const SharedProtoState*>> shared_states;
  auto input = absl::WrapUnique(new TestRelationalOp(
      {a, b, c},
      CreateTestTupleDatas({{NullInt64(), Int64(10), GetProtoValue(1)},
                            {Int64(2), Int64(20), GetProtoValue(2)},
                            {Int64(3), Int64(30), GetProtoValue(3)}},
                           &shared_states),
      /*preserves_order=*/true));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto sort_op,
      SortOp::Create(std::move(keys), std::move(values),
                     /*limit=*/nullptr, /*offset=*/nullptr, std::move(input),
                     /*is_order_preserving=*/true,
                     /*is_stable_sort=*/false));
  EXPECT_EQ(sort_op->IteratorDebugString(),
            "SortTupleIterator(TestTupleIterator)");
  EXPECT_EQ(sort_op->DebugString(),
            "SortOp(ordered\n"
            "+-keys: {\n"
            "| +-$k := $a DESC},\n"
            "+-values: {\n"
            "| +-$v1 := $b,\n"
            "| +-$v2 := $c,\n"
            "| +-$v3 := $param},\n"
            "+-input: TestRelationalOp)");

  std::unique_ptr<TupleSchema> output_schema = sort_op->CreateOutputSchema();
  EXPECT_THAT(output_schema->variables(), ElementsAre(k, v1, v2, v3));

  EvaluationContext context((EvaluationOptions()));

  TupleSchema params_schema({param});
  GOOGLESQL_ASSERT_OK(sort_op->SetSchemasForEvaluation({&params_schema}));

  TupleData params_data = CreateTestTupleData({Int64(100)});

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> iter,
      sort_op->CreateIterator({&params_data}, /*num_extra_slots=*/1, &context));

  EXPECT_EQ(iter->DebugString(), "SortTupleIterator(TestTupleIterator)");
  EXPECT_TRUE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> data,
                       ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 3);
  EXPECT_THAT(data[0].slots(),
              ElementsAre(IsTupleSlotWith(Int64(3), IsNull()),
                          IsTupleSlotWith(Int64(30), IsNull()),
                          IsTupleSlotWith(GetProtoValue(3),
                                          HasRawPointer(shared_states[2][2])),
                          IsTupleSlotWith(Int64(100), IsNull()), _));
  EXPECT_THAT(data[1].slots(),
              ElementsAre(IsTupleSlotWith(Int64(2), IsNull()),
                          IsTupleSlotWith(Int64(20), IsNull()),
                          IsTupleSlotWith(GetProtoValue(2),
                                          HasRawPointer(shared_states[1][2])),
                          IsTupleSlotWith(Int64(100), IsNull()), _));
  EXPECT_THAT(data[2].slots(),
              ElementsAre(IsTupleSlotWith(NullInt64(), IsNull()),
                          IsTupleSlotWith(Int64(10), IsNull()),
                          IsTupleSlotWith(GetProtoValue(1),
                                          HasRawPointer(shared_states[0][2])),
                          IsTupleSlotWith(Int64(100), IsNull()), _));

  // Do it again with cancellation.
  context.ClearDeadlineAndCancellationState();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      iter,
      sort_op->CreateIterator({&params_data}, /*num_extra_slots=*/1, &context));
  GOOGLESQL_ASSERT_OK(context.CancelStatement());
  absl::Status status;
  data = ReadFromTupleIteratorFull(iter.get(), &status);
  EXPECT_TRUE(data.empty());
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kCancelled, _));

  // Check that enabling scrambling has no effect because the output is uniquely
  // ordered.
  EvaluationContext scramble_context(GetScramblingEvaluationOptions());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      iter, sort_op->CreateIterator({&params_data}, /*num_extra_slots=*/1,
                                    &scramble_context));
  EXPECT_EQ(iter->DebugString(), "SortTupleIterator(TestTupleIterator)");
  EXPECT_TRUE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(data, ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 3);
  EXPECT_THAT(data[0].slots(),
              ElementsAre(IsTupleSlotWith(Int64(3), IsNull()),
                          IsTupleSlotWith(Int64(30), IsNull()),
                          IsTupleSlotWith(GetProtoValue(3),
                                          HasRawPointer(shared_states[2][2])),
                          IsTupleSlotWith(Int64(100), IsNull()), _));
  EXPECT_THAT(data[1].slots(),
              ElementsAre(IsTupleSlotWith(Int64(2), IsNull()),
                          IsTupleSlotWith(Int64(20), IsNull()),
                          IsTupleSlotWith(GetProtoValue(2),
                                          HasRawPointer(shared_states[1][2])),
                          IsTupleSlotWith(Int64(100), IsNull()), _));
  EXPECT_THAT(data[2].slots(),
              ElementsAre(IsTupleSlotWith(NullInt64(), IsNull()),
                          IsTupleSlotWith(Int64(10), IsNull()),
                          IsTupleSlotWith(GetProtoValue(1),
                                          HasRawPointer(shared_states[0][2])),
                          IsTupleSlotWith(Int64(100), IsNull()), _));

  // Check that if the memory bound is too low, we return an error.
  EvaluationContext memory_context(GetIntermediateMemoryEvaluationOptions(
      /*total_bytes=*/500));
  EXPECT_THAT(sort_op->CreateIterator({&params_data},
                                      /*num_extra_slots=*/1, &memory_context),
              StatusIs(absl::StatusCode::kResourceExhausted,
                       HasSubstr("Out of memory")));
}

TEST_F(CreateIteratorTest, SortOpIgnoresOrder) {
  VariableId a("a"), b("b"), k("k"), v("v");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_a, DerefExpr::Create(a, Int64Type()));

  std::vector<std::unique_ptr<KeyArg>> keys;
  keys.push_back(
      std::make_unique<KeyArg>(k, std::move(deref_a), KeyArg::kDescending));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_b, DerefExpr::Create(b, Int64Type()));

  std::vector<std::unique_ptr<ExprArg>> values;
  values.push_back(std::make_unique<ExprArg>(v, std::move(deref_b)));

  auto input = absl::WrapUnique(new TestRelationalOp(
      {a, b},
      CreateTestTupleDatas({{Int64(1), Int64(10)}, {Int64(2), Int64(20)}}),
      /*preserves_order=*/true));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto sort_op,
      SortOp::Create(std::move(keys), std::move(values),
                     /*limit=*/nullptr, /*offset=*/nullptr, std::move(input),
                     /*is_order_preserving=*/false,
                     /*is_stable_sort=*/false));
  GOOGLESQL_ASSERT_OK(sort_op->SetSchemasForEvaluation(EmptyParamsSchemas()));

  EvaluationContext context((EvaluationOptions()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> iter,
      sort_op->CreateIterator(EmptyParams(), /*num_extra_slots=*/0, &context));
  // The SortOp does not preserve order (like for a top-level ORDER BY in a
  // subquery), but scrambling is disabled, so the iterator will just be a
  // SortTupleIterator that returns sorted tuples with no scrambling.
  EXPECT_EQ(iter->DebugString(), "SortTupleIterator(TestTupleIterator)");
  EXPECT_TRUE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> data,
                       ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 2);
  EXPECT_EQ(Tuple(&iter->Schema(), &data[0]).DebugString(), "<k:2,v:20>");
  EXPECT_EQ(Tuple(&iter->Schema(), &data[1]).DebugString(), "<k:1,v:10>");
  EXPECT_EQ(data[0].num_slots(), 2);
  EXPECT_EQ(data[1].num_slots(), 2);

  EvaluationContext scramble_context(GetScramblingEvaluationOptions());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      iter, sort_op->CreateIterator(EmptyParams(), /*num_extra_slots=*/0,
                                    &scramble_context));
  // Now that scrambling is enabled, we still disable reordering on the
  // SortTupleIterator, but now we wrap it in a ReorderingTupleIterator to
  // scramble its output.
  EXPECT_EQ(iter->DebugString(),
            "ReorderingTupleIterator(SortTupleIterator(TestTupleIterator))");
  EXPECT_FALSE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(data, ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 2);
}

TEST_F(CreateIteratorTest, SortOpIgnoresOrderSameKey) {
  VariableId a("a"), b("b"), k("k"), v("v");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_a, DerefExpr::Create(a, Int64Type()));

  std::vector<std::unique_ptr<KeyArg>> keys;
  keys.push_back(
      std::make_unique<KeyArg>(k, std::move(deref_a), KeyArg::kDescending));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_b, DerefExpr::Create(b, Int64Type()));

  std::vector<std::unique_ptr<ExprArg>> values;
  values.push_back(std::make_unique<ExprArg>(v, std::move(deref_b)));

  auto input = absl::WrapUnique(
      new TestRelationalOp({a, b},
                           CreateTestTupleDatas({{Int64(1), Int64(10)},
                                                 {Int64(2), Int64(20)},
                                                 {Int64(2), Int64(30)}}),
                           /*preserves_order=*/true));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto sort_op,
      SortOp::Create(std::move(keys), std::move(values),
                     /*limit=*/nullptr, /*offset=*/nullptr, std::move(input),
                     /*is_order_preserving=*/false,
                     /*is_stable_sort=*/false));
  GOOGLESQL_ASSERT_OK(sort_op->SetSchemasForEvaluation(EmptyParamsSchemas()));

  EvaluationContext context(
      (EvaluationOptions{.always_use_stable_sort = true}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> iter,
      sort_op->CreateIterator(EmptyParams(), /*num_extra_slots=*/0, &context));
  // The SortOp does not preserve order (like for a top-level ORDER BY in a
  // subquery), but scrambling is disabled, so the iterator will just be a
  // SortTupleIterator that returns sorted tuples with no scrambling.
  EXPECT_EQ(iter->DebugString(), "SortTupleIterator(TestTupleIterator)");
  EXPECT_TRUE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> data,
                       ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 3);
  // Ties are broken in favor of the first tuple in the input.
  EXPECT_EQ(Tuple(&iter->Schema(), &data[0]).DebugString(), "<k:2,v:20>");
  EXPECT_EQ(Tuple(&iter->Schema(), &data[1]).DebugString(), "<k:2,v:30>");
  EXPECT_EQ(Tuple(&iter->Schema(), &data[2]).DebugString(), "<k:1,v:10>");
  EXPECT_EQ(data[0].num_slots(), 2);
  EXPECT_EQ(data[1].num_slots(), 2);

  EvaluationContext scramble_context(GetScramblingEvaluationOptions());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      iter, sort_op->CreateIterator(EmptyParams(), /*num_extra_slots=*/0,
                                    &scramble_context));
  // Now that scrambling is enabled, we still disable reordering on the
  // SortTupleIterator, but now we wrap it in a ReorderingTupleIterator to
  // scramble its output.
  EXPECT_EQ(iter->DebugString(),
            "ReorderingTupleIterator(SortTupleIterator(TestTupleIterator))");
  EXPECT_FALSE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(data, ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 3);
}

TEST_F(CreateIteratorTest, SortOpPartialOrder) {
  VariableId a("a"), b("b"), k("k"), v("v");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_a, DerefExpr::Create(a, Int64Type()));

  std::vector<std::unique_ptr<KeyArg>> keys;
  keys.push_back(
      std::make_unique<KeyArg>(k, std::move(deref_a), KeyArg::kDescending));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_b, DerefExpr::Create(b, Int64Type()));

  std::vector<std::unique_ptr<ExprArg>> values;
  values.push_back(std::make_unique<ExprArg>(v, std::move(deref_b)));

  auto input = absl::WrapUnique(
      new TestRelationalOp({a, b},
                           CreateTestTupleDatas({{Int64(1), Int64(10)},
                                                 {Int64(2), Int64(20)},
                                                 {Int64(3), Int64(30)},
                                                 {Int64(3), Int64(31)}}),
                           /*preserves_order=*/true));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto sort_op,
      SortOp::Create(std::move(keys), std::move(values),
                     /*limit=*/nullptr, /*offset=*/nullptr, std::move(input),
                     /*is_order_preserving=*/true,
                     /*is_stable_sort=*/true));
  GOOGLESQL_ASSERT_OK(sort_op->SetSchemasForEvaluation(EmptyParamsSchemas()));

  EXPECT_EQ(
      "SortOp(ordered\n"
      "+-keys: {\n"
      "| +-$k := $a DESC},\n"
      "+-values: {\n"
      "| +-$v := $b},\n"
      "+-input: TestRelationalOp)",
      sort_op->DebugString());

  EvaluationContext context((EvaluationOptions()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> iter,
      sort_op->CreateIterator(EmptyParams(), /*num_extra_slots=*/0, &context));
  EXPECT_EQ(iter->DebugString(), "SortTupleIterator(TestTupleIterator)");
  // The output of SortTupleIterator is not defined because there are two rows
  // for the same key, but scrambling is disabled. SortTupleIterator will just
  // return them in sorted order with ties broken by whichever tuple is first in
  // the input.
  EXPECT_TRUE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> data,
                       ReadFromTupleIterator(iter.get()));
  EXPECT_TRUE(context.IsDeterministicOutput());
  ASSERT_EQ(data.size(), 4);
  EXPECT_EQ(Tuple(&iter->Schema(), &data[0]).DebugString(), "<k:3,v:30>");
  EXPECT_EQ(Tuple(&iter->Schema(), &data[1]).DebugString(), "<k:3,v:31>");
  EXPECT_EQ(Tuple(&iter->Schema(), &data[2]).DebugString(), "<k:2,v:20>");
  EXPECT_EQ(Tuple(&iter->Schema(), &data[3]).DebugString(), "<k:1,v:10>");
  EXPECT_EQ(data[0].num_slots(), 2);
  EXPECT_EQ(data[1].num_slots(), 2);
  EXPECT_EQ(data[2].num_slots(), 2);
  EXPECT_EQ(data[3].num_slots(), 2);

  EvaluationContext scramble_context((GetScramblingEvaluationOptions()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      iter, sort_op->CreateIterator(EmptyParams(), /*num_extra_slots=*/0,
                                    &scramble_context));
  EXPECT_EQ(iter->DebugString(), "SortTupleIterator(TestTupleIterator)");
  EXPECT_TRUE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(data, ReadFromTupleIterator(iter.get()));
  EXPECT_TRUE(scramble_context.IsDeterministicOutput());
  ASSERT_EQ(data.size(), 4);
  // Now that scrambling is enabled, SortTupleIterator should scramble the order
  // of tuples with the same key.
  EXPECT_EQ(Tuple(&iter->Schema(), &data[0]).DebugString(), "<k:3,v:30>");
  EXPECT_EQ(Tuple(&iter->Schema(), &data[1]).DebugString(), "<k:3,v:31>");
  EXPECT_EQ(Tuple(&iter->Schema(), &data[2]).DebugString(), "<k:2,v:20>");
  EXPECT_EQ(Tuple(&iter->Schema(), &data[3]).DebugString(), "<k:1,v:10>");
  EXPECT_EQ(data[0].num_slots(), 2);
  EXPECT_EQ(data[1].num_slots(), 2);
  EXPECT_EQ(data[2].num_slots(), 2);
  EXPECT_EQ(data[3].num_slots(), 2);
}

TEST_F(CreateIteratorTest, SortOpPartialOrderMultiNULLs) {
  VariableId a("a"), b("b"), k("k"), v("v");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_a, DerefExpr::Create(a, Int64Type()));

  std::vector<std::unique_ptr<KeyArg>> keys;
  keys.push_back(
      std::make_unique<KeyArg>(k, std::move(deref_a), KeyArg::kDescending));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_b, DerefExpr::Create(b, Int64Type()));

  std::vector<std::unique_ptr<ExprArg>> values;
  values.push_back(std::make_unique<ExprArg>(v, std::move(deref_b)));

  auto input = absl::WrapUnique(
      new TestRelationalOp({a, b},
                           CreateTestTupleDatas({{NullInt64(), Int64(10)},
                                                 {NullInt64(), Int64(20)},
                                                 {Int64(3), Int64(30)}}),
                           /*preserves_order=*/true));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto sort_op,
      SortOp::Create(std::move(keys), std::move(values),
                     /*limit=*/nullptr, /*offset=*/nullptr, std::move(input),
                     /*is_order_preserving=*/true,
                     /*is_stable_sort=*/true));
  GOOGLESQL_ASSERT_OK(sort_op->SetSchemasForEvaluation(EmptyParamsSchemas()));

  EvaluationContext context((EvaluationOptions()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> iter,
      sort_op->CreateIterator(EmptyParams(), /*num_extra_slots=*/0, &context));
  EXPECT_EQ(iter->DebugString(), "SortTupleIterator(TestTupleIterator)");
  // The output of SortTupleIterator is not defined because there are two rows
  // for the same key, but scrambling is disabled. SortTupleIterator will just
  // return them in sorted order with ties broken by whichever tuple is first in
  // the input.
  EXPECT_TRUE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> data,
                       ReadFromTupleIterator(iter.get()));
  EXPECT_TRUE(context.IsDeterministicOutput());
  ASSERT_EQ(data.size(), 3);
  EXPECT_EQ(Tuple(&iter->Schema(), &data[0]).DebugString(), "<k:3,v:30>");
  EXPECT_EQ(Tuple(&iter->Schema(), &data[1]).DebugString(), "<k:NULL,v:10>");
  EXPECT_EQ(Tuple(&iter->Schema(), &data[2]).DebugString(), "<k:NULL,v:20>");
  EXPECT_EQ(data[0].num_slots(), 2);
  EXPECT_EQ(data[1].num_slots(), 2);
  EXPECT_EQ(data[2].num_slots(), 2);

  EvaluationContext scramble_context(GetScramblingEvaluationOptions());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      iter, sort_op->CreateIterator(EmptyParams(), /*num_extra_slots=*/0,
                                    &scramble_context));
  EXPECT_EQ(iter->DebugString(), "SortTupleIterator(TestTupleIterator)");
  EXPECT_TRUE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(data, ReadFromTupleIterator(iter.get()));
  EXPECT_TRUE(scramble_context.IsDeterministicOutput());
  ASSERT_EQ(data.size(), 3);
  // Now that scrambling is enabled, SortTupleIterator should scramble the order
  // of tuples with the same key.
  EXPECT_EQ(Tuple(&iter->Schema(), &data[0]).DebugString(), "<k:3,v:30>");
  EXPECT_EQ(Tuple(&iter->Schema(), &data[1]).DebugString(), "<k:NULL,v:10>");
  EXPECT_EQ(Tuple(&iter->Schema(), &data[2]).DebugString(), "<k:NULL,v:20>");
  EXPECT_EQ(data[0].num_slots(), 2);
  EXPECT_EQ(data[1].num_slots(), 2);
  EXPECT_EQ(data[2].num_slots(), 2);
}

TEST_F(CreateIteratorTest, SortOpPartialOrderStableSort) {
  VariableId a("a"), b("b"), k("k"), v("v");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_a, DerefExpr::Create(a, Int64Type()));

  std::vector<std::unique_ptr<KeyArg>> keys;
  keys.push_back(
      std::make_unique<KeyArg>(k, std::move(deref_a), KeyArg::kDescending));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_b, DerefExpr::Create(b, Int64Type()));

  std::vector<std::unique_ptr<ExprArg>> values;
  values.push_back(std::make_unique<ExprArg>(v, std::move(deref_b)));

  auto input = absl::WrapUnique(
      new TestRelationalOp({a, b},
                           CreateTestTupleDatas({{Int64(1), Int64(10)},
                                                 {Int64(2), Int64(20)},
                                                 {Int64(3), Int64(30)},
                                                 {Int64(3), Int64(31)}}),

                           /*preserves_order=*/true));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto sort_op,
      SortOp::Create(std::move(keys), std::move(values),
                     /*limit=*/nullptr, /*offset=*/nullptr, std::move(input),
                     /*is_order_preserving=*/true,
                     /*is_stable_sort=*/true));
  GOOGLESQL_ASSERT_OK(sort_op->SetSchemasForEvaluation(EmptyParamsSchemas()));

  EXPECT_EQ(
      "SortOp(ordered\n"
      "+-keys: {\n"
      "| +-$k := $a DESC},\n"
      "+-values: {\n"
      "| +-$v := $b},\n"
      "+-input: TestRelationalOp)",
      sort_op->DebugString());

  EvaluationContext scramble_context((GetScramblingEvaluationOptions()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> iter,
      sort_op->CreateIterator(EmptyParams(), /*num_extra_slots=*/0,
                              &scramble_context));
  EXPECT_EQ(iter->DebugString(), "SortTupleIterator(TestTupleIterator)");
  // The output of SortTupleIterator is typically not defined because there are
  // two rows for the same key (and scrambling is enabled). But the SortOp was
  // configured to use a stable sort, so SortTupleIterator will just return them
  // in sorted order with ties broken by whichever tuple is first in the input.
  EXPECT_TRUE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> data,
                       ReadFromTupleIterator(iter.get()));
  EXPECT_TRUE(scramble_context.IsDeterministicOutput());
  ASSERT_EQ(data.size(), 4);
  EXPECT_EQ(Tuple(&iter->Schema(), &data[0]).DebugString(), "<k:3,v:30>");
  EXPECT_EQ(Tuple(&iter->Schema(), &data[1]).DebugString(), "<k:3,v:31>");
  EXPECT_EQ(Tuple(&iter->Schema(), &data[2]).DebugString(), "<k:2,v:20>");
  EXPECT_EQ(Tuple(&iter->Schema(), &data[3]).DebugString(), "<k:1,v:10>");
  EXPECT_EQ(data[0].num_slots(), 2);
  EXPECT_EQ(data[1].num_slots(), 2);
  EXPECT_EQ(data[2].num_slots(), 2);
  EXPECT_EQ(data[3].num_slots(), 2);
}

// Tests the reordering functionality in SortTupleIterator.
TEST_F(CreateIteratorTest, SortOpPartialInputReordersTest) {
  const int num_keys = 10;
  std::vector<TupleData> tuples;
  for (int k = 0; k < num_keys; ++k) {
    for (int v = 0; v <= k; ++v) {
      tuples.push_back(CreateTestTupleData({Int64(k), Int64(v)}));
    }
  }

  VariableId k("k"), v("v"), a("a"), b("b");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_a, DerefExpr::Create(a, Int64Type()));

  std::vector<std::unique_ptr<KeyArg>> keys;
  keys.push_back(
      std::make_unique<KeyArg>(k, std::move(deref_a), KeyArg::kDescending));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_b, DerefExpr::Create(b, Int64Type()));

  std::vector<std::unique_ptr<ExprArg>> values;
  values.push_back(std::make_unique<ExprArg>(v, std::move(deref_b)));

  auto input = absl::WrapUnique(
      new TestRelationalOp({a, b}, tuples, /*preserves_order=*/true));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto sort_op,
      SortOp::Create(std::move(keys), std::move(values),
                     /*limit=*/nullptr, /*offset=*/nullptr, std::move(input),
                     /*is_order_preserving=*/true,
                     /*is_stable_sort=*/false));
  GOOGLESQL_ASSERT_OK(sort_op->SetSchemasForEvaluation(EmptyParamsSchemas()));

  EvaluationContext scramble_context(GetScramblingEvaluationOptions());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> iter,
      sort_op->CreateIterator(EmptyParams(), /*num_extra_slots=*/0,
                              &scramble_context));
  EXPECT_EQ(iter->DebugString(), "SortTupleIterator(TestTupleIterator)");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> data,
                       ReadFromTupleIterator(iter.get()));

  const std::optional<int> opt_k_slot = iter->Schema().FindIndexForVariable(k);
  ASSERT_TRUE(opt_k_slot.has_value());
  const int k_slot = opt_k_slot.value();

  const std::optional<int> opt_v_slot = iter->Schema().FindIndexForVariable(v);
  ASSERT_TRUE(opt_v_slot.has_value());
  const int v_slot = opt_v_slot.value();

  // data_by_key[k][v] is true if we have seen the tuple (k, v).
  std::vector<std::vector<bool>> data_by_key;
  data_by_key.resize(num_keys);
  for (int k = 0; k < data_by_key.size(); ++k) {
    data_by_key[k].resize(k + 1);
  }

  int last_key = num_keys;
  for (const TupleData& tuple : data) {
    const Value& k_value = tuple.slot(k_slot).value();
    ASSERT_TRUE(k_value.type()->IsInt64());
    const int k = k_value.ToInt64();

    const Value& v_value = tuple.slot(v_slot).value();
    ASSERT_TRUE(v_value.type()->IsInt64());
    const int64_t v = v_value.ToInt64();

    ASSERT_GE(v, 0);
    ASSERT_EQ(data_by_key[k].size(), k + 1);
    ASSERT_LE(v, k);
    ASSERT_FALSE(data_by_key[k][v]) << "k=" << k << " v=" << v;
    data_by_key[k][v] = true;

    // We should see everything for key k + 1 before anything for key k.
    if (k != last_key) {
      ASSERT_EQ(k, last_key - 1);
      last_key = k;
    }
  }

  // Verify that we saw every value once for every k.
  for (int k = 0; k < num_keys; ++k) {
    for (int v = 0; v <= k; ++v) {
      EXPECT_TRUE(data_by_key[k][v]) << "k=" << k << " v=" << v;
    }
  }
}

TEST_F(CreateIteratorTest, SortOpTotalOrderWithLimitAndOffset) {
  VariableId a("a"), b("b"), c("c"), param("param"), k("k"), v1("v1"), v2("v2"),
      v3("v3"), limit("limit"), offset("offset");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_a, DerefExpr::Create(a, Int64Type()));

  std::vector<std::unique_ptr<KeyArg>> keys;
  keys.push_back(
      std::make_unique<KeyArg>(k, std::move(deref_a), KeyArg::kDescending));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_b, DerefExpr::Create(b, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_c, DerefExpr::Create(c, proto_type_));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_param, DerefExpr::Create(param, Int64Type()));

  std::vector<std::unique_ptr<ExprArg>> values;
  values.push_back(std::make_unique<ExprArg>(v1, std::move(deref_b)));
  values.push_back(std::make_unique<ExprArg>(v2, std::move(deref_c)));
  values.push_back(std::make_unique<ExprArg>(v3, std::move(deref_param)));

  std::vector<std::vector<const SharedProtoState*>> shared_states;
  auto input = absl::WrapUnique(new TestRelationalOp(
      {a, b, c},
      CreateTestTupleDatas({{NullInt64(), Int64(10), GetProtoValue(1)},
                            {Int64(2), Int64(20), GetProtoValue(2)},
                            {Int64(3), Int64(30), GetProtoValue(3)},
                            {Int64(4), Int64(40), GetProtoValue(4)}},
                           &shared_states),
      /*preserves_order=*/true));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto limit_expr, DerefExpr::Create(limit, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto offset_expr,
                       DerefExpr::Create(offset, Int64Type()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto sort_op,
      SortOp::Create(std::move(keys), std::move(values), std::move(limit_expr),
                     std::move(offset_expr), std::move(input),
                     /*is_order_preserving=*/true,
                     /*is_stable_sort=*/false));
  EXPECT_EQ(sort_op->IteratorDebugString(),
            "SortTupleIterator(TestTupleIterator)");
  EXPECT_EQ(sort_op->DebugString(),
            "SortOp(ordered\n"
            "+-keys: {\n"
            "| +-$k := $a DESC},\n"
            "+-values: {\n"
            "| +-$v1 := $b,\n"
            "| +-$v2 := $c,\n"
            "| +-$v3 := $param},\n"
            "+-limit: $limit,\n"
            "+-offset: $offset,\n"
            "+-input: TestRelationalOp)");

  std::unique_ptr<TupleSchema> output_schema = sort_op->CreateOutputSchema();
  EXPECT_THAT(output_schema->variables(), ElementsAre(k, v1, v2, v3));

  EvaluationContext context((EvaluationOptions()));

  TupleSchema params_schema({limit, offset, param});
  GOOGLESQL_ASSERT_OK(sort_op->SetSchemasForEvaluation({&params_schema}));
  TupleData params_data(/*num_slots=*/3);
  auto set_params = [&params_data](const Value& value1, const Value& value2) {
    params_data.mutable_slot(0)->SetValue(value1);
    params_data.mutable_slot(1)->SetValue(value2);
    params_data.mutable_slot(2)->SetValue(Int64(100));
  };

  // Bad argument: LIMIT -1 OFFSET 0
  set_params(Int64(-1), Int64(0));
  EXPECT_THAT(
      sort_op->CreateIterator({&params_data}, /*num_extra_slots=*/0, &context),
      StatusIs(absl::StatusCode::kOutOfRange,
               "Limit requires non-negative count and offset"));

  // Bad argument: LIMIT 1 OFFSET -1
  set_params(Int64(1), Int64(-1));
  EXPECT_THAT(
      sort_op->CreateIterator({&params_data}, /*num_extra_slots=*/0, &context),
      StatusIs(absl::StatusCode::kOutOfRange,
               "Limit requires non-negative count and offset"));

  // Bad argument: LIMIT NULL OFFSET 0
  set_params(NullInt64(), Int64(0));
  EXPECT_THAT(
      sort_op->CreateIterator({&params_data}, /*num_extra_slots=*/0, &context),
      StatusIs(absl::StatusCode::kOutOfRange,
               "Limit requires non-null count and offset"));

  // Bad argument: LIMIT 1 OFFSET NULL
  set_params(Int64(1), NullInt64());
  EXPECT_THAT(
      sort_op->CreateIterator({&params_data}, /*num_extra_slots=*/0, &context),
      StatusIs(absl::StatusCode::kOutOfRange,
               "Limit requires non-null count and offset"));

  set_params(Int64(2), Int64(1));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> iter,
      sort_op->CreateIterator({&params_data}, /*num_extra_slots=*/1, &context));

  EXPECT_EQ(iter->DebugString(), "SortTupleIterator(TestTupleIterator)");
  EXPECT_TRUE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> data,
                       ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 2);
  EXPECT_THAT(data[0].slots(),
              ElementsAre(IsTupleSlotWith(Int64(3), IsNull()),
                          IsTupleSlotWith(Int64(30), IsNull()),
                          IsTupleSlotWith(GetProtoValue(3),
                                          HasRawPointer(shared_states[2][2])),
                          IsTupleSlotWith(Int64(100), IsNull()), _));
  EXPECT_THAT(data[1].slots(),
              ElementsAre(IsTupleSlotWith(Int64(2), IsNull()),
                          IsTupleSlotWith(Int64(20), IsNull()),
                          IsTupleSlotWith(GetProtoValue(2),
                                          HasRawPointer(shared_states[1][2])),
                          IsTupleSlotWith(Int64(100), IsNull()), _));

  // Check that if the memory bound is too low, we return an error.
  EvaluationContext memory_context(GetIntermediateMemoryEvaluationOptions(
      /*total_bytes=*/500));
  EXPECT_THAT(sort_op->CreateIterator({&params_data},
                                      /*num_extra_slots=*/1, &memory_context),
              StatusIs(absl::StatusCode::kResourceExhausted,
                       HasSubstr("Out of memory")));
}

TEST_F(CreateIteratorTest, SortOpTotalOrderWithNullLimit) {
  VariableId a("a"), b("b"), c("c"), param("param"), k("k"), v1("v1"), v2("v2"),
      v3("v3"), limit("limit"), offset("offset");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_a, DerefExpr::Create(a, Int64Type()));

  std::vector<std::unique_ptr<KeyArg>> keys;
  keys.push_back(
      std::make_unique<KeyArg>(k, std::move(deref_a), KeyArg::kDescending));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_b, DerefExpr::Create(b, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_c, DerefExpr::Create(c, proto_type_));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_param, DerefExpr::Create(param, Int64Type()));

  std::vector<std::unique_ptr<ExprArg>> values;
  values.push_back(std::make_unique<ExprArg>(v1, std::move(deref_b)));
  values.push_back(std::make_unique<ExprArg>(v2, std::move(deref_c)));
  values.push_back(std::make_unique<ExprArg>(v3, std::move(deref_param)));

  std::vector<std::vector<const SharedProtoState*>> shared_states;
  auto input = absl::WrapUnique(new TestRelationalOp(
      {a, b, c},
      CreateTestTupleDatas({{NullInt64(), Int64(10), GetProtoValue(1)},
                            {Int64(2), Int64(20), GetProtoValue(2)},
                            {Int64(3), Int64(30), GetProtoValue(3)},
                            {Int64(4), Int64(40), GetProtoValue(4)}},
                           &shared_states),
      /*preserves_order=*/true));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto limit_expr, DerefExpr::Create(limit, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto offset_expr,
                       DerefExpr::Create(offset, Int64Type()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto sort_op,
      SortOp::Create(std::move(keys), std::move(values), std::move(limit_expr),
                     std::move(offset_expr), std::move(input),
                     /*is_order_preserving=*/true,
                     /*is_stable_sort=*/false));
  EXPECT_EQ(sort_op->IteratorDebugString(),
            "SortTupleIterator(TestTupleIterator)");
  EXPECT_THAT(sort_op->DebugString(), StrEq("SortOp(ordered\n"
                                            "+-keys: {\n"
                                            "| +-$k := $a DESC},\n"
                                            "+-values: {\n"
                                            "| +-$v1 := $b,\n"
                                            "| +-$v2 := $c,\n"
                                            "| +-$v3 := $param},\n"
                                            "+-limit: $limit,\n"
                                            "+-offset: $offset,\n"
                                            "+-input: TestRelationalOp)"));

  EXPECT_THAT(sort_op->CreateOutputSchema()->variables(),
              ElementsAre(k, v1, v2, v3));

  EvaluationContext context((EvaluationOptions()));
  LanguageOptions language_options;
  language_options.EnableMaximumLanguageFeaturesForDevelopment();
  context.SetLanguageOptions(language_options);

  TupleSchema params_schema({limit, offset, param});
  GOOGLESQL_ASSERT_OK(sort_op->SetSchemasForEvaluation({&params_schema}));
  TupleData params_data(/*num_slots=*/3);
  auto set_params = [&params_data](const Value& value1, const Value& value2) {
    params_data.mutable_slot(0)->SetValue(value1);
    params_data.mutable_slot(1)->SetValue(value2);
    params_data.mutable_slot(2)->SetValue(Int64(100));
  };

  // Bad argument: LIMIT -1 OFFSET 0
  set_params(Int64(-1), Int64(0));
  EXPECT_THAT(
      sort_op->CreateIterator({&params_data}, /*num_extra_slots=*/0, &context),
      StatusIs(absl::StatusCode::kOutOfRange,
               "Limit requires non-negative count and offset"));

  // Bad argument: LIMIT 1 OFFSET -1
  set_params(Int64(1), Int64(-1));
  EXPECT_THAT(
      sort_op->CreateIterator({&params_data}, /*num_extra_slots=*/0, &context),
      StatusIs(absl::StatusCode::kOutOfRange,
               "Limit requires non-negative count and offset"));

  // Bad argument: LIMIT 1 OFFSET NULL
  set_params(Int64(1), NullInt64());
  EXPECT_THAT(
      sort_op->CreateIterator({&params_data}, /*num_extra_slots=*/0, &context),
      StatusIs(absl::StatusCode::kOutOfRange,
               "Limit requires non-null count and offset"));

  // Good argument: LIMIT NULL OFFSET 1
  set_params(NullInt64(), Int64(1));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> iter,
      sort_op->CreateIterator({&params_data}, /*num_extra_slots=*/1, &context));

  EXPECT_EQ(iter->DebugString(), "SortTupleIterator(TestTupleIterator)");
  EXPECT_TRUE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> data,
                       ReadFromTupleIterator(iter.get()));
  EXPECT_THAT(data, SizeIs(3));
  EXPECT_THAT(data[0].slots(),
              ElementsAre(IsTupleSlotWith(Int64(3), IsNull()),
                          IsTupleSlotWith(Int64(30), IsNull()),
                          IsTupleSlotWith(GetProtoValue(3),
                                          HasRawPointer(shared_states[2][2])),
                          IsTupleSlotWith(Int64(100), IsNull()), _));
  EXPECT_THAT(data[1].slots(),
              ElementsAre(IsTupleSlotWith(Int64(2), IsNull()),
                          IsTupleSlotWith(Int64(20), IsNull()),
                          IsTupleSlotWith(GetProtoValue(2),
                                          HasRawPointer(shared_states[1][2])),
                          IsTupleSlotWith(Int64(100), IsNull()), _));
  EXPECT_THAT(data[2].slots(),
              ElementsAre(IsTupleSlotWith(NullInt64(), IsNull()),
                          IsTupleSlotWith(Int64(10), IsNull()),
                          IsTupleSlotWith(GetProtoValue(1),
                                          HasRawPointer(shared_states[0][2])),
                          IsTupleSlotWith(Int64(100), IsNull()), _));
}

TEST_F(CreateIteratorTest, ArrayScanOp) {
  VariableId a("a"), p("p"), param("param");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_param,
                       DerefExpr::Create(param, proto_array_type_));
  std::vector<std::unique_ptr<ValueExpr>> arrays;
  arrays.push_back(std::move(deref_param));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto mode_expr, ConstExpr::Create(Value::Enum(
                                           types::ArrayZipModeEnumType(),
                                           functions::ArrayZipEnums::PAD)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto scan_op,
      ArrayScanOp::Create(/*elements=*/{a}, /*position=*/p,
                          /*arrays=*/std::move(arrays),
                          /*zip_mode_expr=*/std::move(mode_expr)));
  EXPECT_EQ(scan_op->IteratorDebugString(), "ArrayScanTupleIterator(<array>)");
  EXPECT_EQ(
      "ArrayScanOp(\n"
      "+-$a := element,\n"
      "+-$p := position,\n"
      "+-array: $param)",
      scan_op->DebugString());
  std::unique_ptr<TupleSchema> output_schema = scan_op->CreateOutputSchema();
  EXPECT_THAT(output_schema->variables(), ElementsAre(a, p));

  TupleSchema params_schema({param});
  TupleData params_data =
      CreateTestTupleData({Array({GetProtoValue(1), GetProtoValue(2)})});

  GOOGLESQL_ASSERT_OK(scan_op->SetSchemasForEvaluation({&params_schema}));

  EvaluationContext context((EvaluationOptions()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> iter,
      scan_op->CreateIterator({&params_data}, /*num_extra_slots=*/1, &context));
  EXPECT_EQ(iter->DebugString(),
            "ArrayScanTupleIterator([{int64_key_1: 1 int64_key_2: 10}, "
            "{int64_key_1: 2 int64_key_2: 20}])");
  EXPECT_TRUE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> data,
                       ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 2);
  EXPECT_THAT(
      data[0].slots(),
      ElementsAre(IsTupleSlotWith(GetProtoValue(1), Pointee(Eq(nullopt))),
                  IsTupleSlotWith(Int64(0), IsNull()), _));
  EXPECT_THAT(
      data[1].slots(),
      ElementsAre(IsTupleSlotWith(GetProtoValue(2), Pointee(Eq(nullopt))),
                  IsTupleSlotWith(Int64(1), IsNull()), _));
  // Ordered arrays always have deterministic output.
  EXPECT_TRUE(context.IsDeterministicOutput());

  // Do it again with cancellation.
  context.ClearDeadlineAndCancellationState();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      iter,
      scan_op->CreateIterator({&params_data}, /*num_extra_slots=*/1, &context));
  GOOGLESQL_ASSERT_OK(context.CancelStatement());
  absl::Status status;
  data = ReadFromTupleIteratorFull(iter.get(), &status);
  EXPECT_TRUE(data.empty());
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kCancelled, _));

  // Check that scrambling works.
  EvaluationContext scramble_context(GetScramblingEvaluationOptions());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      iter, scan_op->CreateIterator({&params_data}, /*num_extra_slots=*/1,
                                    &scramble_context));
  EXPECT_EQ(iter->DebugString(),
            "ReorderingTupleIterator(ArrayScanTupleIterator("
            "[{int64_key_1: 1 int64_key_2: 10}, "
            "{int64_key_1: 2 int64_key_2: 20}]))");
  EXPECT_FALSE(iter->PreservesOrder());
}

TEST_F(CreateIteratorTest, ArrayScanOpNonDeterministic) {
  VariableId a("a"), p("p");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto array_expr,
      ConstExpr::Create(Array({Int64(1), Int64(2)}, kIgnoresOrder)));
  std::vector<std::unique_ptr<ValueExpr>> arrays;
  arrays.push_back(std::move(array_expr));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto mode_expr, ConstExpr::Create(Value::Enum(
                                           types::ArrayZipModeEnumType(),
                                           functions::ArrayZipEnums::PAD)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto scan_op,
      ArrayScanOp::Create(/*elements=*/{a}, /*position=*/p,
                          /*arrays=*/std::move(arrays),
                          /*zip_mode_expr=*/std::move(mode_expr)));

  EvaluationContext context((EvaluationOptions()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> iter,
      scan_op->CreateIterator(EmptyParams(), /*num_extra_slots=*/0, &context));
  EXPECT_TRUE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> data,
                       ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 2);
  EXPECT_EQ(Tuple(&iter->Schema(), &data[0]).DebugString(), "<a:1,p:0>");
  EXPECT_EQ(Tuple(&iter->Schema(), &data[1]).DebugString(), "<a:2,p:1>");
  // Check for no extra slots.
  EXPECT_EQ(data[0].num_slots(), 2);
  EXPECT_EQ(data[1].num_slots(), 2);
  // Unordered arrays with position variables are non-deterministic if there is
  // more than one element.
  EXPECT_FALSE(context.IsDeterministicOutput());

  // Check that scrambling works.
  EvaluationContext scramble_context(GetScramblingEvaluationOptions());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      iter, scan_op->CreateIterator(EmptyParams(), /*num_extra_slots=*/0,
                                    &scramble_context));
  EXPECT_FALSE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(data, ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 2);
  // Don't look at 'data' in more detail because it is scrambled.
  EXPECT_FALSE(context.IsDeterministicOutput());
}

TEST_F(CreateIteratorTest, ArrayScanOpMultiwayUnnest) {
  VariableId arr1("arr1"), arr2("arr2"), p("p");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto array_expr1,
                       ConstExpr::Create(Array({Int64(1), Int64(2), Int64(3)},
                                               kPreservesOrder)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto array_expr2, ConstExpr::Create(Array(
                                             {String("hello"), String("world")},
                                             kPreservesOrder)));
  std::vector<std::unique_ptr<ValueExpr>> arrays;
  arrays.push_back(std::move(array_expr1));
  arrays.push_back(std::move(array_expr2));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto mode_expr, ConstExpr::Create(Value::Enum(
                                           types::ArrayZipModeEnumType(),
                                           functions::ArrayZipEnums::PAD)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto scan_op_without_position,
      ArrayScanOp::Create(/*elements=*/{arr1, arr2}, /*position=*/VariableId(),
                          /*arrays=*/std::move(arrays),
                          /*zip_mode_expr=*/std::move(mode_expr)));
  EXPECT_EQ(scan_op_without_position->IteratorDebugString(),
            "ArrayScanTupleIterator(<array>)");
  EXPECT_EQ(
      "ArrayScanOp(\n"
      "+-$arr1 := element,\n"
      "+-$arr2 := element,\n"
      "+-mode: ConstExpr(PAD)\n"
      "+-array: ConstExpr([1, 2, 3])\n"
      "+-array: ConstExpr([\"hello\", \"world\"]))",
      scan_op_without_position->DebugString());

  EvaluationContext context((EvaluationOptions()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<TupleIterator> iter,
                       scan_op_without_position->CreateIterator(
                           EmptyParams(), /*num_extra_slots=*/0, &context));
  EXPECT_TRUE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> data,
                       ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 3);
  EXPECT_EQ(Tuple(&iter->Schema(), &data[0]).DebugString(),
            "<arr1:1,arr2:\"hello\">");
  EXPECT_EQ(Tuple(&iter->Schema(), &data[1]).DebugString(),
            "<arr1:2,arr2:\"world\">");
  EXPECT_EQ(Tuple(&iter->Schema(), &data[2]).DebugString(),
            "<arr1:3,arr2:NULL>");
  // Check for no extra slots.
  EXPECT_EQ(data[0].num_slots(), 2);
  EXPECT_EQ(data[1].num_slots(), 2);
  EXPECT_EQ(data[2].num_slots(), 2);
  // If all arrays are ordered in multiway UNNEST, the results are
  // deterministic.
  EXPECT_TRUE(context.IsDeterministicOutput());

  // Check that scrambling works.
  EvaluationContext scramble_context(GetScramblingEvaluationOptions());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      iter, scan_op_without_position->CreateIterator(
                EmptyParams(), /*num_extra_slots=*/0, &scramble_context));
  EXPECT_FALSE(iter->PreservesOrder());

  // Unordered array with only one element.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto array_expr3,
      ConstExpr::Create(Array({String("hello")}, kIgnoresOrder)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto array_expr4,
      ConstExpr::Create(Array({Int64(1), Int64(2)}, kPreservesOrder)));
  std::vector<std::unique_ptr<ValueExpr>> arrays2;
  arrays2.push_back(std::move(array_expr3));
  arrays2.push_back(std::move(array_expr4));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto mode_expr2,
      ConstExpr::Create(Value::Enum(types::ArrayZipModeEnumType(),
                                    functions::ArrayZipEnums::TRUNCATE)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto scan_op_with_position,
      ArrayScanOp::Create(/*elements=*/{arr1, arr2}, /*position=*/VariableId(),
                          /*arrays=*/std::move(arrays2),
                          /*zip_mode_expr=*/std::move(mode_expr2)));
  EXPECT_EQ(scan_op_with_position->IteratorDebugString(),
            "ArrayScanTupleIterator(<array>)");
  EXPECT_EQ(
      "ArrayScanOp(\n"
      "+-$arr1 := element,\n"
      "+-$arr2 := element,\n"
      "+-mode: ConstExpr(TRUNCATE)\n"
      "+-array: ConstExpr([\"hello\"])\n"
      "+-array: ConstExpr([1, 2]))",
      scan_op_with_position->DebugString());

  EvaluationContext context2((EvaluationOptions()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<TupleIterator> iter2,
                       scan_op_with_position->CreateIterator(
                           EmptyParams(), /*num_extra_slots=*/0, &context2));
  // Unordered arrays with only one element does not cause the output rows to be
  // non-deterministic.
  EXPECT_TRUE(iter2->PreservesOrder());
  EXPECT_TRUE(context2.IsDeterministicOutput());
}

static std::unique_ptr<ValueExpr> DivByZeroErrorExpr() {
  std::vector<std::unique_ptr<ValueExpr>> div_args;
  div_args.push_back(ConstExpr::Create(Int64(1)).value());
  div_args.push_back(ConstExpr::Create(Int64(0)).value());

  return ScalarFunctionCallExpr::Create(
             CreateFunction(FunctionKind::kDiv, Int64Type()),
             std::move(div_args), DEFAULT_ERROR_MODE)
      .value();
}

TEST_F(CreateIteratorTest, ArrayScanOpBadModeExpr) {
  VariableId arr1("arr1"), arr2("arr2"), x("x");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto array_expr1,
                       ConstExpr::Create(Array({Int64(1), Int64(2), Int64(3)},
                                               kPreservesOrder)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto array_expr2, ConstExpr::Create(Array(
                                             {String("hello"), String("world")},
                                             kPreservesOrder)));
  std::vector<std::unique_ptr<ValueExpr>> arrays;
  arrays.push_back(std::move(array_expr1));
  arrays.push_back(std::move(array_expr2));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto body, ConstExpr::Create(Value::Enum(
                                      types::ArrayZipModeEnumType(),
                                      functions::ArrayZipEnums::TRUNCATE)));
  std::vector<std::unique_ptr<ExprArg>> let_assign;
  let_assign.push_back(std::make_unique<ExprArg>(x, DivByZeroErrorExpr()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto mode_expr, WithExpr::Create(std::move(let_assign), std::move(body)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto scan_op_without_position,
      ArrayScanOp::Create(/*elements=*/{arr1, arr2}, /*position=*/VariableId(),
                          /*arrays=*/std::move(arrays),
                          /*zip_mode_expr=*/std::move(mode_expr)));

  EvaluationContext context((EvaluationOptions()));
  EXPECT_THAT(
      scan_op_without_position->CreateIterator(EmptyParams(),
                                               /*num_extra_slots=*/0, &context),
      StatusIs(absl::StatusCode::kOutOfRange, HasSubstr("division by zero")));
}

TEST_F(CreateIteratorTest, ArrayScanOpNonDeterministicMultiwayUnnest) {
  VariableId arr1("arr1"), arr2("arr2");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto array_expr1,
                       ConstExpr::Create(Array({Int64(1), Int64(2), Int64(3)},
                                               kPreservesOrder)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto array_expr2,
                       ConstExpr::Create(Array(
                           {String("hello"), String("world")}, kIgnoresOrder)));
  std::vector<std::unique_ptr<ValueExpr>> arrays;
  arrays.push_back(std::move(array_expr1));
  arrays.push_back(std::move(array_expr2));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto mode_expr, ConstExpr::Create(Value::Enum(
                                           types::ArrayZipModeEnumType(),
                                           functions::ArrayZipEnums::PAD)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto scan_op,
      ArrayScanOp::Create(/*elements=*/{arr1, arr2}, /*position=*/VariableId(),
                          /*arrays=*/std::move(arrays),
                          /*zip_mode_expr=*/std::move(mode_expr)));

  EvaluationContext context((EvaluationOptions()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> iter,
      scan_op->CreateIterator(EmptyParams(), /*num_extra_slots=*/0, &context));
  EXPECT_TRUE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> data,
                       ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 3);
  EXPECT_EQ(Tuple(&iter->Schema(), &data[0]).DebugString(),
            "<arr1:1,arr2:\"hello\">");
  EXPECT_EQ(Tuple(&iter->Schema(), &data[1]).DebugString(),
            "<arr1:2,arr2:\"world\">");
  EXPECT_EQ(Tuple(&iter->Schema(), &data[2]).DebugString(),
            "<arr1:3,arr2:NULL>");
  // Check for no extra slots.
  EXPECT_EQ(data[0].num_slots(), 2);
  EXPECT_EQ(data[1].num_slots(), 2);
  EXPECT_EQ(data[2].num_slots(), 2);
  // Unordered arrays used in multiway UNNEST are non-deterministic if there is
  // more than one element.
  EXPECT_FALSE(context.IsDeterministicOutput());

  // Check that scrambling works.
  EvaluationContext scramble_context(GetScramblingEvaluationOptions());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      iter, scan_op->CreateIterator(EmptyParams(), /*num_extra_slots=*/0,
                                    &scramble_context));
  EXPECT_FALSE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(data, ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 3);
  // Don't look at 'data' in more detail because it is scrambled.
  EXPECT_FALSE(context.IsDeterministicOutput());
}

TEST_F(CreateIteratorTest,
       ArrayScanOpNonDeterministicMultiwayUnnestWithPosition) {
  VariableId arr1("arr1"), arr2("arr2"), p("p");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto array_expr1,
      ConstExpr::Create(Array({Int64(1), Int64(2), Int64(3)}, kIgnoresOrder)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto array_expr2, ConstExpr::Create(Array(
                                             {String("hello"), String("world")},
                                             kPreservesOrder)));
  std::vector<std::unique_ptr<ValueExpr>> arrays;
  arrays.push_back(std::move(array_expr1));
  arrays.push_back(std::move(array_expr2));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto mode_expr, ConstExpr::Create(Value::Enum(
                                           types::ArrayZipModeEnumType(),
                                           functions::ArrayZipEnums::PAD)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto scan_op,
      ArrayScanOp::Create(/*elements=*/{arr1, arr2}, /*position=*/p,
                          /*arrays=*/std::move(arrays),
                          /*zip_mode_expr=*/std::move(mode_expr)));

  EvaluationContext context((EvaluationOptions()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> iter,
      scan_op->CreateIterator(EmptyParams(), /*num_extra_slots=*/0, &context));
  EXPECT_TRUE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> data,
                       ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 3);
  EXPECT_EQ(Tuple(&iter->Schema(), &data[0]).DebugString(),
            "<arr1:1,arr2:\"hello\",p:0>");
  EXPECT_EQ(Tuple(&iter->Schema(), &data[1]).DebugString(),
            "<arr1:2,arr2:\"world\",p:1>");
  EXPECT_EQ(Tuple(&iter->Schema(), &data[2]).DebugString(),
            "<arr1:3,arr2:NULL,p:2>");
  // Check for no extra slots.
  EXPECT_EQ(data[0].num_slots(), 3);
  EXPECT_EQ(data[1].num_slots(), 3);
  EXPECT_EQ(data[2].num_slots(), 3);
  // Unordered arrays with position variables are non-deterministic if there is
  // more than one element.
  EXPECT_FALSE(context.IsDeterministicOutput());

  // Check that scrambling works.
  EvaluationContext scramble_context(GetScramblingEvaluationOptions());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      iter, scan_op->CreateIterator(EmptyParams(), /*num_extra_slots=*/0,
                                    &scramble_context));
  EXPECT_FALSE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(data, ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 3);
  // Don't look at 'data' in more detail because it is scrambled.
  EXPECT_FALSE(context.IsDeterministicOutput());
}

TEST_F(CreateIteratorTest, ScanArrayOfStructs) {
  VariableId x("x"), v1("v1"), v2("v2");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto array_expr,
      ConstExpr::Create(Array({Struct({"foo", "bar"}, {Int64(1), Int64(2)})})));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto scan_op,
      ArrayScanOp::Create(/*element=*/x, /*position=*/VariableId(),
                          /*fields=*/{{v1, 0}, {v2, 1}},
                          /*array=*/std::move(array_expr)));
  EXPECT_EQ(
      "ArrayScanOp(\n"
      "+-$x := element,\n"
      "+-$v1 := field[0]:foo,\n"
      "+-$v2 := field[1]:bar,\n"
      "+-array: ConstExpr([{foo:1, bar:2}]))",
      scan_op->DebugString());
  std::unique_ptr<TupleSchema> output_schema = scan_op->CreateOutputSchema();
  EXPECT_THAT(output_schema->variables(), ElementsAre(v1, v2, x));

  EvaluationContext context((EvaluationOptions()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> iter,
      scan_op->CreateIterator(EmptyParams(), /*num_extra_slots=*/0, &context));
  EXPECT_TRUE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> data,
                       ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 1);
  EXPECT_EQ(Tuple(&iter->Schema(), &data[0]).DebugString(),
            "<v1:1,v2:2,x:{foo:1, bar:2}>");
  // Check for no extra slots.
  EXPECT_EQ(data[0].num_slots(), 3);
}

TEST_F(CreateIteratorTest, TableScanAsArray) {
  VariableId v1("v1"), v2("v2");
  Value table = Array({Struct({"a", "b"}, {Int64(1), Int64(2)}),
                       Struct({"a", "b"}, {Int64(3), Int64(4)})});
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto table_as_array_expr,
      TableAsArrayExpr::Create("mytable", table.type()->AsArray()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto scan_op,
      ArrayScanOp::Create(/*element=*/VariableId(), /*position=*/VariableId(),
                          /*fields=*/{{v1, 0}, {v2, 1}},
                          /*array=*/std::move(table_as_array_expr)));
  EXPECT_EQ(
      "ArrayScanOp(\n"
      "+-$v1 := field[0]:a,\n"
      "+-$v2 := field[1]:b,\n"
      "+-array: TableAsArrayExpr(mytable))",
      scan_op->DebugString());
  std::unique_ptr<TupleSchema> output_schema = scan_op->CreateOutputSchema();
  EXPECT_THAT(output_schema->variables(), ElementsAre(v1, v2));

  EvaluationContext context_empty((EvaluationOptions()));
  EXPECT_THAT(scan_op->CreateIterator(EmptyParams(), /*num_extra_slots=*/0,
                                      &context_empty),
              StatusIs(absl::StatusCode::kOutOfRange,
                       "Table not populated with array: mytable"));

  EvaluationContext context_bad((EvaluationOptions()));
  GOOGLESQL_ASSERT_OK(context_bad.AddTableAsArray("mytable", /*is_value_table=*/true,
                                        Int32Array({1, 2, 3}),
                                        LanguageOptions()));
  EXPECT_THAT(
      scan_op->CreateIterator(EmptyParams(), /*num_extra_slots=*/0,
                              &context_bad),
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr("deviates from the type reported in the catalog")));

  EvaluationContext context((EvaluationOptions()));
  GOOGLESQL_ASSERT_OK(context.AddTableAsArray("mytable", /*is_value_table=*/false, table,
                                    LanguageOptions()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> iter,
      scan_op->CreateIterator(EmptyParams(), /*num_extra_slots=*/0, &context));
  EXPECT_TRUE(iter->PreservesOrder());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> data,
                       ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 2);
  EXPECT_EQ(Tuple(&iter->Schema(), &data[0]).DebugString(), "<v1:1,v2:2>");
  EXPECT_EQ(Tuple(&iter->Schema(), &data[1]).DebugString(), "<v1:3,v2:4>");
  // Check for no extra slots.
  EXPECT_EQ(data[0].num_slots(), 2);
  EXPECT_EQ(data[0].num_slots(), 2);
  // The output is deterministic because the array position is not retrieved.
  EXPECT_TRUE(context.IsDeterministicOutput());

  // Check that scrambling works.
  EvaluationContext scramble_context(GetScramblingEvaluationOptions());
  GOOGLESQL_ASSERT_OK(scramble_context.AddTableAsArray(
      "mytable", /*is_value_table=*/false, table, LanguageOptions()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      iter, scan_op->CreateIterator(EmptyParams(), /*num_extra_slots=*/0,
                                    &scramble_context));
  EXPECT_EQ(iter->DebugString(),
            "ReorderingTupleIterator(ArrayScanTupleIterator("
            "[{a:1, b:2}, {a:3, b:4}]))");
  EXPECT_FALSE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(data, ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 2);
  // Don't look at 'data' in more detail because it is scrambled.
  EXPECT_TRUE(context.IsDeterministicOutput());
}

TEST_F(CreateIteratorTest, UnionAllOp) {
  VariableId a("a"), a1("a1"), a2("a2"), b("b"), b1("b1"), b2("b2");

  std::vector<std::vector<const SharedProtoState*>> shared_states;
  const std::vector<TupleData> datas =
      CreateTestTupleDatas({{Int64(1), GetProtoValue(1)},
                            {Int64(2), GetProtoValue(2)},
                            {Int64(3), GetProtoValue(3)},
                            {Int64(4), GetProtoValue(4)}},
                           &shared_states);
  auto input1 =
      absl::WrapUnique(new TestRelationalOp({a1, b1}, {datas[0], datas[1]},
                                            /*preserves_order=*/true));
  auto input2 =
      absl::WrapUnique(new TestRelationalOp({a2, b2}, {datas[2], datas[3]},
                                            /*preserves_order=*/true));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_a1, DerefExpr::Create(a1, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_b1, DerefExpr::Create(b1, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_a2, DerefExpr::Create(a2, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_b2, DerefExpr::Create(b2, Int64Type()));

  UnionAllOp::Input union_input1;
  union_input1.first = std::move(input1);
  union_input1.second.push_back(
      std::make_unique<ExprArg>(a, std::move(deref_a1)));
  union_input1.second.push_back(
      std::make_unique<ExprArg>(b, std::move(deref_b1)));

  UnionAllOp::Input union_input2;
  union_input2.first = std::move(input2);
  union_input2.second.push_back(
      std::make_unique<ExprArg>(a, std::move(deref_a2)));
  union_input2.second.push_back(
      std::make_unique<ExprArg>(b, std::move(deref_b2)));

  std::vector<UnionAllOp::Input> union_inputs;
  union_inputs.push_back(std::move(union_input1));
  union_inputs.push_back(std::move(union_input2));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto union_all_op,
                       UnionAllOp::Create(std::move(union_inputs)));
  GOOGLESQL_ASSERT_OK(union_all_op->SetSchemasForEvaluation(EmptyParamsSchemas()));
  EXPECT_EQ(
      "UnionAllOp(\n"
      "+-rel[0]: {\n"
      "| +-$a := $a1,\n"
      "| +-$b := $b1,\n"
      "| +-input: TestRelationalOp},\n"
      "+-rel[1]: {\n"
      "  +-$a := $a2,\n"
      "  +-$b := $b2,\n"
      "  +-input: TestRelationalOp})",
      union_all_op->DebugString());
  std::unique_ptr<TupleSchema> output_schema =
      union_all_op->CreateOutputSchema();
  EXPECT_THAT(output_schema->variables(), ElementsAre(a, b));

  EvaluationContext context((EvaluationOptions()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> iter,
      union_all_op->CreateIterator(EmptyParams(),
                                   /*num_extra_slots=*/1, &context));
  EXPECT_EQ(iter->DebugString(),
            "UnionAllTupleIterator(TestTupleIterator,TestTupleIterator)");
  EXPECT_TRUE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> data,
                       ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 4);
  EXPECT_THAT(data[0].slots(),
              ElementsAre(IsTupleSlotWith(Int64(1), IsNull()),
                          IsTupleSlotWith(GetProtoValue(1),
                                          HasRawPointer(shared_states[0][1])),
                          _));
  EXPECT_THAT(data[1].slots(),
              ElementsAre(IsTupleSlotWith(Int64(2), IsNull()),
                          IsTupleSlotWith(GetProtoValue(2),
                                          HasRawPointer(shared_states[1][1])),
                          _));
  EXPECT_THAT(data[2].slots(),
              ElementsAre(IsTupleSlotWith(Int64(3), IsNull()),
                          IsTupleSlotWith(GetProtoValue(3),
                                          HasRawPointer(shared_states[2][1])),
                          _));
  EXPECT_THAT(data[3].slots(),
              ElementsAre(IsTupleSlotWith(Int64(4), IsNull()),
                          IsTupleSlotWith(GetProtoValue(4),
                                          HasRawPointer(shared_states[3][1])),
                          _));

  // Check that scrambling works.
  EvaluationContext scramble_context(GetScramblingEvaluationOptions());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(iter, union_all_op->CreateIterator(EmptyParams(),
                                                          /*num_extra_slots=*/1,
                                                          &scramble_context));
  EXPECT_EQ(iter->DebugString(),
            "ReorderingTupleIterator(UnionAllTupleIterator("
            "TestTupleIterator,TestTupleIterator))");
  EXPECT_FALSE(iter->PreservesOrder());
}

TEST_F(CreateIteratorTest, LoopOp) {
  VariableId a("a"), x("x"), y("y"), z("z");

  std::vector<std::unique_ptr<ExprArg>> initial_assign(3);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(initial_assign[0], AssignValueToVar(x, Int64(1)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(initial_assign[1], AssignValueToVar(y, Int64(2)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(initial_assign[2], AssignValueToVar(z, Int64(3)));

  VariableId a_plus_x("a_plus_x"), a_plus_y("a_plus_y"), a_plus_z("a_plus_z");
  std::vector<std::unique_ptr<ExprArg>> body_compute_exprs(3);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(body_compute_exprs[0],
                       ComputeSum(a, x, a_plus_x, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(body_compute_exprs[1],
                       ComputeSum(a, y, a_plus_y, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(body_compute_exprs[2],
                       ComputeSum(a, z, a_plus_z, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto body_compute_op,
      ComputeOp::Create(
          std::move(body_compute_exprs),
          absl::WrapUnique(new TestRelationalOp(
              {a}, CreateTestTupleDatas({{Int64(10)}, {Int64(20)}}),
              /*preserves_order=*/true))));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto body,
                       FilterLessThan(x, Int64(5), std::move(body_compute_op)));

  std::vector<std::unique_ptr<ExprArg>> loop_assign(3);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(loop_assign[0], ComputeSum(x, Int64(1), x));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(loop_assign[1], ComputeSum(y, Int64(1), y));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(loop_assign[2], ComputeSum(z, Int64(1), z));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto loop_op,
      LoopOp::Create(std::move(initial_assign), std::move(body),
                     std::move(loop_assign), /*lower_bound=*/nullptr,
                     /*upper_bound=*/nullptr));

  EXPECT_EQ(loop_op->IteratorDebugString(),
            "LoopTupleIterator: any_rows = false, inner iterator: "
            "FilterTupleIterator(ComputeTupleIterator(TestTupleIterator))");
  ABSL_LOG(ERROR) << loop_op->DebugString();
  EXPECT_EQ(loop_op->DebugString(), absl::StripAsciiWhitespace(R"(
LoopOp(
+-initial_assign: {
| +-$x := ConstExpr(1),
| +-$y := ConstExpr(2),
| +-$z := ConstExpr(3)},
+-body: FilterOp(
| +-condition: Less($x, ConstExpr(5)),
| +-input: ComputeOp(
|   +-map: {
|   | +-$a_plus_x := Add($a, $x),
|   | +-$a_plus_y := Add($a, $y),
|   | +-$a_plus_z := Add($a, $z)},
|   +-input: TestRelationalOp)),
+-loop_assign: {
  +-$x := Add($x, ConstExpr(1)),
  +-$y := Add($y, ConstExpr(1)),
  +-$z := Add($z, ConstExpr(1))})
  )"));

  std::unique_ptr<TupleSchema> output_schema = loop_op->CreateOutputSchema();
  EXPECT_THAT(output_schema->variables(),
              ElementsAre(a, a_plus_x, a_plus_y, a_plus_z));
  TupleSchema params_schema({});
  GOOGLESQL_ASSERT_OK(loop_op->SetSchemasForEvaluation({&params_schema}));

  EvaluationContext context((EvaluationOptions()));
  TupleData params_data = CreateTestTupleData({});
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> iter,
      loop_op->CreateIterator({&params_data}, /*num_extra_slots=*/1, &context));
  EXPECT_EQ(iter->DebugString(), "LoopTupleIterator: inner iterator: nullptr");
  EXPECT_FALSE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> data,
                       ReadFromTupleIterator(iter.get()));

  // The loop should run for 4 iterations (x=1,2,3,4) with each iteration
  // producing two rows (a=10, 20), for a total of 8 rows.
  EXPECT_THAT(data, SizeIs(8));

  // Iteration 1, row 1
  EXPECT_THAT(data[0].slots(),
              ElementsAre(IsTupleSlotWith(Int64(10), IsNull()),
                          IsTupleSlotWith(Int64(11), IsNull()),
                          IsTupleSlotWith(Int64(12), IsNull()),
                          IsTupleSlotWith(Int64(13), IsNull()), _));

  // Iteration 1, row 2
  EXPECT_THAT(data[1].slots(),
              ElementsAre(IsTupleSlotWith(Int64(20), IsNull()),
                          IsTupleSlotWith(Int64(21), IsNull()),
                          IsTupleSlotWith(Int64(22), IsNull()),
                          IsTupleSlotWith(Int64(23), IsNull()), _));

  // Iteration 2, row 1
  EXPECT_THAT(data[2].slots(),
              ElementsAre(IsTupleSlotWith(Int64(10), IsNull()),
                          IsTupleSlotWith(Int64(12), IsNull()),
                          IsTupleSlotWith(Int64(13), IsNull()),
                          IsTupleSlotWith(Int64(14), IsNull()), _));

  // Iteratino 2, row 2
  EXPECT_THAT(data[3].slots(),
              ElementsAre(IsTupleSlotWith(Int64(20), IsNull()),
                          IsTupleSlotWith(Int64(22), IsNull()),
                          IsTupleSlotWith(Int64(23), IsNull()),
                          IsTupleSlotWith(Int64(24), IsNull()), _));

  // Iteration 3, row 1
  EXPECT_THAT(data[4].slots(),
              ElementsAre(IsTupleSlotWith(Int64(10), IsNull()),
                          IsTupleSlotWith(Int64(13), IsNull()),
                          IsTupleSlotWith(Int64(14), IsNull()),
                          IsTupleSlotWith(Int64(15), IsNull()), _));

  // Iteration 3, row 2
  EXPECT_THAT(data[5].slots(),
              ElementsAre(IsTupleSlotWith(Int64(20), IsNull()),
                          IsTupleSlotWith(Int64(23), IsNull()),
                          IsTupleSlotWith(Int64(24), IsNull()),
                          IsTupleSlotWith(Int64(25), IsNull()), _));

  // Iteration 4, row 1
  EXPECT_THAT(data[6].slots(),
              ElementsAre(IsTupleSlotWith(Int64(10), IsNull()),
                          IsTupleSlotWith(Int64(14), IsNull()),
                          IsTupleSlotWith(Int64(15), IsNull()),
                          IsTupleSlotWith(Int64(16), IsNull()), _));

  // Iteration 4, row 2
  EXPECT_THAT(data[7].slots(),
              ElementsAre(IsTupleSlotWith(Int64(20), IsNull()),
                          IsTupleSlotWith(Int64(24), IsNull()),
                          IsTupleSlotWith(Int64(25), IsNull()),
                          IsTupleSlotWith(Int64(26), IsNull()), _));
}

TEST_F(CreateIteratorTest, LoopOpWithBounds) {
  VariableId n("n"), a("a");

  std::vector<std::unique_ptr<ExprArg>> initial_assign(1);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(initial_assign[0], AssignValueToVar(n, Int64(0)));

  std::vector<std::unique_ptr<ExprArg>> body_compute_exprs(1);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_n, DerefExpr::Create(n, Int64Type()));
  body_compute_exprs[0] = std::make_unique<ExprArg>(n, std::move(deref_n));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto const_expr, ConstExpr::Create(Value::Int64(1)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto enum_op,
                       EnumerateOp::Create(std::move(const_expr)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto body,
      ComputeOp::Create(std::move(body_compute_exprs), std::move(enum_op)));

  std::vector<std::unique_ptr<ExprArg>> loop_assign(1);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(loop_assign[0], ComputeSum(n, Int64(1), n));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<ValueExpr> lower_bound,
                       ConstExpr::Create(Value::Int64(1)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<ValueExpr> upper_bound,
                       ConstExpr::Create(Value::Int64(3)));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto loop_op,
      LoopOp::Create(std::move(initial_assign), std::move(body),
                     std::move(loop_assign), std::move(lower_bound),
                     std::move(upper_bound)));

  EXPECT_EQ(loop_op->IteratorDebugString(),
            "LoopTupleIterator: any_rows = false, inner iterator: "
            "ComputeTupleIterator(EnumerateTupleIterator(<count>))");
  EXPECT_EQ(loop_op->DebugString(), absl::StripAsciiWhitespace(R"(
LoopOp(
+-initial_assign: {
| +-$n := ConstExpr(0)},
+-body: ComputeOp(
| +-map: {
| | +-$n := $n},
| +-input: EnumerateOp(ConstExpr(1))),
+-loop_assign: {
| +-$n := Add($n, ConstExpr(1))},
+-lower_bound: ConstExpr(1),
+-upper_bound: ConstExpr(3)))"));

  std::unique_ptr<TupleSchema> output_schema = loop_op->CreateOutputSchema();
  EXPECT_THAT(output_schema->variables(), ElementsAre(n));
  TupleSchema params_schema({});
  GOOGLESQL_ASSERT_OK(loop_op->SetSchemasForEvaluation({&params_schema}));

  EvaluationContext context((EvaluationOptions()));
  TupleData params_data = CreateTestTupleData({});
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> iter,
      loop_op->CreateIterator({&params_data}, /*num_extra_slots=*/1, &context));
  EXPECT_FALSE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> data,
                       ReadFromTupleIterator(iter.get()));
  ASSERT_THAT(data, SizeIs(3));
  EXPECT_THAT(data[0].slots(),
              ElementsAre(IsTupleSlotWith(Int64(1), IsNull()), _));
  EXPECT_THAT(data[1].slots(),
              ElementsAre(IsTupleSlotWith(Int64(2), IsNull()), _));
  EXPECT_THAT(data[2].slots(),
              ElementsAre(IsTupleSlotWith(Int64(3), IsNull()), _));
}

TEST_F(CreateIteratorTest, DistinctOp) {
  VariableId a("a"), b("b"), c("c");
  VariableId row_set1("row_set");

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<DerefExpr> deref_a,
                       DerefExpr::Create(a, types::Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<DerefExpr> deref_b,
                       DerefExpr::Create(b, types::Int64Type()));

  std::vector<TupleData> test_values =
      CreateTestTupleDatas({{Int64(1), Int64(10), Int64(100)},
                            {Int64(2), Int64(20), Int64(200)},
                            {Int64(3), Int64(30), Int64(300)},
                            {Int64(1), Int64(10), Int64(101)}});
  auto input = absl::WrapUnique(new TestRelationalOp({a, b, c}, test_values,
                                                     /*preserves_order=*/true));

  std::vector<std::unique_ptr<KeyArg>> keys;
  keys.push_back(std::make_unique<KeyArg>(a, std::move(deref_a)));
  keys.push_back(std::make_unique<KeyArg>(b, std::move(deref_b)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto distinct_op,
      DistinctOp::Create(std::move(input), std::move(keys), row_set1));

  EXPECT_EQ(absl::StripAsciiWhitespace(
                R"(
DistinctOp(
+-input: TestRelationalOp,
+-keys: {
| +-$a := $a,
| +-$b := $b},
+-row_set_id: $row_set := )
)"),
            distinct_op->DebugString());
  EXPECT_EQ("DistinctOp: TestTupleIterator",
            distinct_op->IteratorDebugString());
  EXPECT_EQ("<a,b>", distinct_op->CreateOutputSchema()->DebugString());

  // Set up a row set with some pre-existing data, which should be excluded
  // from the output.
  EvaluationContext context((EvaluationOptions()));
  auto row_set =
      std::make_unique<CppValue<DistinctRowSet>>(context.memory_accountant());
  absl::Status status;
  ASSERT_TRUE(row_set->value().InsertRowIfNotPresent(
      std::make_unique<TupleData>(CreateTestTupleData({Int64(3), Int64(30)})),
      &status));
  ASSERT_TRUE(row_set->value().InsertRowIfNotPresent(
      std::make_unique<TupleData>(CreateTestTupleData({Int64(3), Int64(50)})),
      &status));
  ASSERT_TRUE(context.SetCppValueIfNotPresent(row_set1, std::move(row_set)));

  GOOGLESQL_ASSERT_OK(distinct_op->SetSchemasForEvaluation({}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> iter,
      distinct_op->CreateIterator({},
                                  /*num_extra_slots=*/1, &context));
  EXPECT_EQ(iter->DebugString(), "DistinctOp: TestTupleIterator");
  EXPECT_TRUE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> data,
                       ReadFromTupleIterator(iter.get()));

  ASSERT_EQ(data.size(), 2);
  EXPECT_THAT(data[0].slots(),
              ElementsAre(IsTupleSlotWith(Int64(1), IsNull()),
                          IsTupleSlotWith(Int64(10), IsNull()), _));
  EXPECT_THAT(data[1].slots(),
              ElementsAre(IsTupleSlotWith(Int64(2), IsNull()),
                          IsTupleSlotWith(Int64(20), IsNull()), _));
}

TEST_F(CreateIteratorTest, ComputeOp) {
  VariableId a("a"), b("b"), param("param"), minus("minus"), plus("plus");
  std::vector<TupleData> test_values =
      CreateTestTupleDatas({{Int64(1), Int64(10)}, {Int64(2), Int64(20)}});
  auto input = absl::WrapUnique(
      new TestRelationalOp({a, b}, test_values, /*preserves_order=*/true,
                           /*may_preserve_order=*/false));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_a, DerefExpr::Create(a, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_b, DerefExpr::Create(b, Int64Type()));

  std::vector<std::unique_ptr<ValueExpr>> plus_args;
  plus_args.push_back(std::move(deref_a));
  plus_args.push_back(std::move(deref_b));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto plus_expr,
                       ScalarFunctionCallExpr::Create(
                           CreateFunction(FunctionKind::kAdd, Int64Type()),
                           std::move(plus_args), DEFAULT_ERROR_MODE));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_a_again, DerefExpr::Create(a, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_b_again, DerefExpr::Create(b, Int64Type()));

  std::vector<std::unique_ptr<ValueExpr>> minus_args;
  minus_args.push_back(std::move(deref_a_again));
  minus_args.push_back(std::move(deref_b_again));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto minus_expr,
                       ScalarFunctionCallExpr::Create(
                           CreateFunction(FunctionKind::kSubtract, Int64Type()),
                           std::move(minus_args), DEFAULT_ERROR_MODE));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_param, DerefExpr::Create(param, Int64Type()));

  std::vector<std::unique_ptr<ExprArg>> args;
  args.push_back(std::make_unique<ExprArg>(plus, std::move(plus_expr)));
  args.push_back(std::make_unique<ExprArg>(minus, std::move(minus_expr)));
  args.push_back(std::make_unique<ExprArg>(param, std::move(deref_param)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto compute_op,
                       ComputeOp::Create(std::move(args), std::move(input)));
  EXPECT_FALSE(compute_op->may_preserve_order());
  EXPECT_EQ(compute_op->IteratorDebugString(),
            "ComputeTupleIterator(TestTupleIterator)");
  EXPECT_EQ(
      "ComputeOp(\n"
      "+-map: {\n"
      "| +-$plus := Add($a, $b),\n"
      "| +-$minus := Subtract($a, $b),\n"
      "| +-$param := $param},\n"
      "+-input: TestRelationalOp)",
      compute_op->DebugString());
  std::unique_ptr<TupleSchema> output_schema = compute_op->CreateOutputSchema();
  EXPECT_THAT(output_schema->variables(),
              ElementsAre(a, b, plus, minus, param));

  TupleSchema params_schema({param});
  std::vector<const SharedProtoState*> params_shared_states;
  TupleData params_data =
      CreateTestTupleData({GetProtoValue(100)}, &params_shared_states);
  const SharedProtoState* params_shared_state = params_shared_states[0];

  GOOGLESQL_ASSERT_OK(compute_op->SetSchemasForEvaluation({&params_schema}));

  EvaluationContext context((EvaluationOptions()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> iter,
      compute_op->CreateIterator({&params_data},
                                 /*num_extra_slots=*/1, &context));
  EXPECT_EQ(iter->DebugString(), "ComputeTupleIterator(TestTupleIterator)");
  EXPECT_TRUE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> data,
                       ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 2);
  EXPECT_THAT(data[0].slots(),
              ElementsAre(IsTupleSlotWith(Int64(1), IsNull()),
                          IsTupleSlotWith(Int64(10), IsNull()),
                          IsTupleSlotWith(Int64(11), IsNull()),
                          IsTupleSlotWith(Int64(-9), IsNull()),
                          IsTupleSlotWith(GetProtoValue(100),
                                          HasRawPointer(params_shared_state)),
                          _));
  EXPECT_THAT(data[1].slots(),
              ElementsAre(IsTupleSlotWith(Int64(2), IsNull()),
                          IsTupleSlotWith(Int64(20), IsNull()),
                          IsTupleSlotWith(Int64(22), IsNull()),
                          IsTupleSlotWith(Int64(-18), IsNull()),
                          IsTupleSlotWith(GetProtoValue(100),
                                          HasRawPointer(params_shared_state)),
                          _));

  // Check that scrambling works.
  EvaluationContext scramble_context(GetScramblingEvaluationOptions());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      iter, compute_op->CreateIterator({&params_data}, /*num_extra_slots=*/1,
                                       &scramble_context));
  EXPECT_EQ(iter->DebugString(),
            "ReorderingTupleIterator(ComputeTupleIterator(TestTupleIterator))");
  EXPECT_FALSE(iter->PreservesOrder());
}

TEST_F(CreateIteratorTest, ComputeOp_OrderedInput) {
  VariableId a("a"), b("b"), param("param"), minus("minus"), plus("plus");
  std::vector<TupleData> test_values =
      CreateTestTupleDatas({{Int64(1), Int64(10)}, {Int64(2), Int64(20)}});
  auto input = absl::WrapUnique(
      new TestRelationalOp({a, b}, test_values, /*preserves_order=*/true,
                           /*may_preserve_order=*/true));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_a, DerefExpr::Create(a, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_b, DerefExpr::Create(b, Int64Type()));

  std::vector<std::unique_ptr<ValueExpr>> plus_args;
  plus_args.push_back(std::move(deref_a));
  plus_args.push_back(std::move(deref_b));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto plus_expr,
                       ScalarFunctionCallExpr::Create(
                           CreateFunction(FunctionKind::kAdd, Int64Type()),
                           std::move(plus_args), DEFAULT_ERROR_MODE));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_a_again, DerefExpr::Create(a, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_b_again, DerefExpr::Create(b, Int64Type()));

  std::vector<std::unique_ptr<ValueExpr>> minus_args;
  minus_args.push_back(std::move(deref_a_again));
  minus_args.push_back(std::move(deref_b_again));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto minus_expr,
                       ScalarFunctionCallExpr::Create(
                           CreateFunction(FunctionKind::kSubtract, Int64Type()),
                           std::move(minus_args), DEFAULT_ERROR_MODE));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_param, DerefExpr::Create(param, Int64Type()));

  std::vector<std::unique_ptr<ExprArg>> args;
  args.push_back(std::make_unique<ExprArg>(plus, std::move(plus_expr)));
  args.push_back(std::make_unique<ExprArg>(minus, std::move(minus_expr)));
  args.push_back(std::make_unique<ExprArg>(param, std::move(deref_param)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto compute_op,
                       ComputeOp::Create(std::move(args), std::move(input)));
  EXPECT_TRUE(compute_op->may_preserve_order());
  EXPECT_EQ(compute_op->IteratorDebugString(),
            "ComputeTupleIterator(TestTupleIterator)");
  EXPECT_EQ(
      "ComputeOp(\n"
      "+-map: {\n"
      "| +-$plus := Add($a, $b),\n"
      "| +-$minus := Subtract($a, $b),\n"
      "| +-$param := $param},\n"
      "+-input: TestRelationalOp)",
      compute_op->DebugString());
  std::unique_ptr<TupleSchema> output_schema = compute_op->CreateOutputSchema();
  EXPECT_THAT(output_schema->variables(),
              ElementsAre(a, b, plus, minus, param));

  TupleSchema params_schema({param});
  std::vector<const SharedProtoState*> params_shared_states;
  TupleData params_data =
      CreateTestTupleData({GetProtoValue(100)}, &params_shared_states);
  const SharedProtoState* params_shared_state = params_shared_states[0];

  GOOGLESQL_ASSERT_OK(compute_op->SetSchemasForEvaluation({&params_schema}));

  EvaluationContext context((EvaluationOptions()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> iter,
      compute_op->CreateIterator({&params_data},
                                 /*num_extra_slots=*/1, &context));
  EXPECT_EQ(iter->DebugString(), "ComputeTupleIterator(TestTupleIterator)");
  EXPECT_TRUE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> data,
                       ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 2);
  EXPECT_THAT(data[0].slots(),
              ElementsAre(IsTupleSlotWith(Int64(1), IsNull()),
                          IsTupleSlotWith(Int64(10), IsNull()),
                          IsTupleSlotWith(Int64(11), IsNull()),
                          IsTupleSlotWith(Int64(-9), IsNull()),
                          IsTupleSlotWith(GetProtoValue(100),
                                          HasRawPointer(params_shared_state)),
                          _));
  EXPECT_THAT(data[1].slots(),
              ElementsAre(IsTupleSlotWith(Int64(2), IsNull()),
                          IsTupleSlotWith(Int64(20), IsNull()),
                          IsTupleSlotWith(Int64(22), IsNull()),
                          IsTupleSlotWith(Int64(-18), IsNull()),
                          IsTupleSlotWith(GetProtoValue(100),
                                          HasRawPointer(params_shared_state)),
                          _));

  // Check that scrambling works.
  EvaluationContext scramble_context(GetScramblingEvaluationOptions());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      iter, compute_op->CreateIterator({&params_data}, /*num_extra_slots=*/1,
                                       &scramble_context));
  EXPECT_EQ(iter->DebugString(),
            "ReorderingTupleIterator(ComputeTupleIterator(TestTupleIterator))");
  EXPECT_FALSE(iter->PreservesOrder());
}

TEST_F(CreateIteratorTest, FilterOp) {
  VariableId a("a"), b("b"), param("param");
  const std::vector<TupleData> test_values =
      CreateTestTupleDatas({{Int64(1), Int64(10)},
                            {Int64(3), Int64(30)},
                            {Int64(2), Int64(20)},
                            {Int64(4), Int64(40)}});
  auto input = absl::WrapUnique(
      new TestRelationalOp({a, b}, test_values, /*preserves_order=*/true));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_a, DerefExpr::Create(a, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_param, DerefExpr::Create(param, Int64Type()));

  std::vector<std::unique_ptr<ValueExpr>> args;
  args.push_back(std::move(deref_a));
  args.push_back(std::move(deref_param));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto predicate,
                       ScalarFunctionCallExpr::Create(
                           CreateFunction(FunctionKind::kLess, BoolType()),
                           std::move(args), DEFAULT_ERROR_MODE));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto filter_op, FilterOp::Create(std::move(predicate), std::move(input)));
  EXPECT_EQ(filter_op->IteratorDebugString(),
            "FilterTupleIterator(TestTupleIterator)");
  EXPECT_EQ(
      "FilterOp(\n"
      "+-condition: Less($a, $param),\n"
      "+-input: TestRelationalOp)",
      filter_op->DebugString());
  std::unique_ptr<TupleSchema> output_schema = filter_op->CreateOutputSchema();
  EXPECT_THAT(output_schema->variables(), ElementsAre(a, b));

  TupleSchema params_schema({param});
  const TupleData params_data = CreateTestTupleData({Int64(3)});
  GOOGLESQL_ASSERT_OK(filter_op->SetSchemasForEvaluation({&params_schema}));

  EvaluationContext context((EvaluationOptions()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<TupleIterator> iter,
                       filter_op->CreateIterator(
                           {&params_data}, /*num_extra_slots=*/1, &context));
  EXPECT_EQ(iter->DebugString(), "FilterTupleIterator(TestTupleIterator)");
  EXPECT_TRUE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> data,
                       ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 2);
  EXPECT_THAT(data[0].slots(),
              ElementsAre(IsTupleSlotWith(Int64(1), IsNull()),
                          IsTupleSlotWith(Int64(10), IsNull()), _));
  EXPECT_THAT(data[1].slots(),
              ElementsAre(IsTupleSlotWith(Int64(2), IsNull()),
                          IsTupleSlotWith(Int64(20), IsNull()), _));

  // Check that scrambling works.
  EvaluationContext scramble_context(GetScramblingEvaluationOptions());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      iter, filter_op->CreateIterator({&params_data}, /*num_extra_slots=*/1,
                                      &scramble_context));
  EXPECT_EQ(iter->DebugString(),
            "ReorderingTupleIterator(FilterTupleIterator(TestTupleIterator))");
  EXPECT_FALSE(iter->PreservesOrder());
}

TEST_F(CreateIteratorTest, FilterOp_OrderedInput) {
  VariableId a("a"), b("b"), param("param");
  const std::vector<TupleData> test_values =
      CreateTestTupleDatas({{Int64(1), Int64(10)},
                            {Int64(3), Int64(30)},
                            {Int64(2), Int64(20)},
                            {Int64(4), Int64(40)}});
  auto input = absl::WrapUnique(
      new TestRelationalOp({a, b}, test_values, /*preserves_order=*/true,
                           /*may_preserve_order=*/true));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_a, DerefExpr::Create(a, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_param, DerefExpr::Create(param, Int64Type()));

  std::vector<std::unique_ptr<ValueExpr>> args;
  args.push_back(std::move(deref_a));
  args.push_back(std::move(deref_param));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto predicate,
                       ScalarFunctionCallExpr::Create(
                           CreateFunction(FunctionKind::kLess, BoolType()),
                           std::move(args), DEFAULT_ERROR_MODE));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto filter_op, FilterOp::Create(std::move(predicate), std::move(input)));
  EXPECT_TRUE(filter_op->may_preserve_order());
  EXPECT_EQ(filter_op->IteratorDebugString(),
            "FilterTupleIterator(TestTupleIterator)");
  EXPECT_EQ(
      "FilterOp(\n"
      "+-condition: Less($a, $param),\n"
      "+-input: TestRelationalOp)",
      filter_op->DebugString());
  std::unique_ptr<TupleSchema> output_schema = filter_op->CreateOutputSchema();
  EXPECT_THAT(output_schema->variables(), ElementsAre(a, b));

  TupleSchema params_schema({param});
  const TupleData params_data = CreateTestTupleData({Int64(3)});
  GOOGLESQL_ASSERT_OK(filter_op->SetSchemasForEvaluation({&params_schema}));

  EvaluationContext context((EvaluationOptions()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<TupleIterator> iter,
                       filter_op->CreateIterator(
                           {&params_data}, /*num_extra_slots=*/1, &context));
  EXPECT_EQ(iter->DebugString(), "FilterTupleIterator(TestTupleIterator)");
  EXPECT_TRUE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> data,
                       ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 2);
  EXPECT_THAT(data[0].slots(),
              ElementsAre(IsTupleSlotWith(Int64(1), IsNull()),
                          IsTupleSlotWith(Int64(10), IsNull()), _));
  EXPECT_THAT(data[1].slots(),
              ElementsAre(IsTupleSlotWith(Int64(2), IsNull()),
                          IsTupleSlotWith(Int64(20), IsNull()), _));

  // Check that scrambling works.
  EvaluationContext scramble_context(GetScramblingEvaluationOptions());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      iter, filter_op->CreateIterator({&params_data}, /*num_extra_slots=*/1,
                                      &scramble_context));
  EXPECT_EQ(iter->DebugString(),
            "ReorderingTupleIterator(FilterTupleIterator(TestTupleIterator))");
  EXPECT_FALSE(iter->PreservesOrder());
}

TEST_F(CreateIteratorTest, LimitOp_OrderedInput) {
  VariableId a("a"), b("b"), row_count("row_count"), offset("offset");
  const std::vector<TupleData> test_values =
      CreateTestTupleDatas({{Int64(1), Int64(10)},
                            {Int64(3), Int64(30)},
                            {Int64(2), Int64(20)},
                            {Int64(4), Int64(40)}});

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_row_count,
                       DerefExpr::Create(row_count, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_offset,
                       DerefExpr::Create(offset, Int64Type()));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto limit_op,
      LimitOp::Create(std::move(deref_row_count), std::move(deref_offset),
                      absl::WrapUnique(new TestRelationalOp(
                          {a, b}, test_values, /*preserves_order=*/true)),
                      /*is_order_preserving=*/true));
  EXPECT_EQ(limit_op->IteratorDebugString(),
            "LimitTupleIterator(TestTupleIterator)");
  EXPECT_EQ(
      "LimitOp(ordered\n"
      "+-row_count: $row_count,\n"
      "+-offset: $offset,\n"
      "+-input: TestRelationalOp)",
      limit_op->DebugString());
  std::unique_ptr<TupleSchema> output_schema = limit_op->CreateOutputSchema();
  EXPECT_THAT(output_schema->variables(), ElementsAre(a, b));

  TupleSchema params_schema({row_count, offset});
  GOOGLESQL_ASSERT_OK(limit_op->SetSchemasForEvaluation({&params_schema}));
  TupleData params_data(/*num_slots=*/2);
  auto set_params = [&params_data](const Value& value1, const Value& value2) {
    params_data.mutable_slot(0)->SetValue(value1);
    params_data.mutable_slot(1)->SetValue(value2);
  };

  EvaluationContext scramble_context(GetScramblingEvaluationOptions());

  // Bad argument: LIMIT -1 OFFSET 0
  set_params(Int64(-1), Int64(0));
  EXPECT_THAT(limit_op->CreateIterator({&params_data}, /*num_extra_slots=*/0,
                                       &scramble_context),
              StatusIs(absl::StatusCode::kOutOfRange,
                       "Limit requires non-negative count and offset"));

  // Bad argument: LIMIT 1 OFFSET -1
  set_params(Int64(1), Int64(-1));
  EXPECT_THAT(limit_op->CreateIterator({&params_data}, /*num_extra_slots=*/0,
                                       &scramble_context),
              StatusIs(absl::StatusCode::kOutOfRange,
                       "Limit requires non-negative count and offset"));

  // Bad argument: LIMIT NULL OFFSET 0
  set_params(NullInt64(), Int64(0));
  EXPECT_THAT(limit_op->CreateIterator({&params_data}, /*num_extra_slots=*/0,
                                       &scramble_context),
              StatusIs(absl::StatusCode::kOutOfRange,
                       "Limit requires non-null count and offset"));

  // Bad argument: LIMIT 1 OFFSET NULL
  set_params(Int64(1), NullInt64());
  EXPECT_THAT(limit_op->CreateIterator({&params_data}, /*num_extra_slots=*/0,
                                       &scramble_context),
              StatusIs(absl::StatusCode::kOutOfRange,
                       "Limit requires non-null count and offset"));

  // LIMIT 2 OFFSET 0 (returns 2 rows)
  set_params(Int64(2), Int64(0));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> iter,
      limit_op->CreateIterator({&params_data}, /*num_extra_slots=*/1,
                               &scramble_context));
  EXPECT_EQ(iter->DebugString(), "LimitTupleIterator(TestTupleIterator)");
  EXPECT_TRUE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> data,
                       ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 2);
  EXPECT_EQ(Tuple(&iter->Schema(), &data[0]).DebugString(), "<a:1,b:10>");
  EXPECT_EQ(Tuple(&iter->Schema(), &data[1]).DebugString(), "<a:3,b:30>");
  // Check for the extra slot.
  EXPECT_EQ(data[0].num_slots(), 3);
  EXPECT_EQ(data[1].num_slots(), 3);
  // Check for deterministic output.
  EXPECT_TRUE(scramble_context.IsDeterministicOutput());
  // LIMIT 1 OFFSET 1 (returns one row)
  set_params(Int64(1), Int64(1));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      iter, limit_op->CreateIterator({&params_data}, /*num_extra_slots=*/1,
                                     &scramble_context));
  EXPECT_EQ(iter->DebugString(), "LimitTupleIterator(TestTupleIterator)");
  EXPECT_TRUE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(data, ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 1);
  EXPECT_EQ(Tuple(&iter->Schema(), &data[0]).DebugString(), "<a:3,b:30>");
  EXPECT_EQ(data[0].num_slots(), 3);
  EXPECT_TRUE(scramble_context.IsDeterministicOutput());

  // LIMIT 2 OFFSET 3 (only returns one row)
  set_params(Int64(2), Int64(3));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      iter, limit_op->CreateIterator({&params_data}, /*num_extra_slots=*/1,
                                     &scramble_context));
  EXPECT_EQ(iter->DebugString(), "LimitTupleIterator(TestTupleIterator)");
  EXPECT_TRUE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(data, ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 1);
  EXPECT_EQ(Tuple(&iter->Schema(), &data[0]).DebugString(), "<a:4,b:40>");
  EXPECT_EQ(data[0].num_slots(), 3);  // Check for the extra slot.
  // Check for deterministic output.
  EXPECT_TRUE(scramble_context.IsDeterministicOutput());

  // LIMIT 2 OFFSET 4 (returns no rows)
  set_params(Int64(2), Int64(4));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      iter, limit_op->CreateIterator({&params_data}, /*num_extra_slots=*/1,
                                     &scramble_context));
  EXPECT_EQ(iter->DebugString(), "LimitTupleIterator(TestTupleIterator)");
  EXPECT_TRUE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(data, ReadFromTupleIterator(iter.get()));
  ASSERT_TRUE(data.empty());
  // Check for deterministic output.
  EXPECT_TRUE(scramble_context.IsDeterministicOutput());

  // LIMIT 0 OFFSET 2
  set_params(Int64(0), Int64(4));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      iter, limit_op->CreateIterator({&params_data}, /*num_extra_slots=*/1,
                                     &scramble_context));
  EXPECT_EQ(iter->DebugString(), "LimitTupleIterator(TestTupleIterator)");
  EXPECT_TRUE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(data, ReadFromTupleIterator(iter.get()));
  ASSERT_TRUE(data.empty());
  // Check for deterministic output.
  EXPECT_TRUE(scramble_context.IsDeterministicOutput());

  // Limit that doesn't preserve order.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_row_count2,
                       DerefExpr::Create(row_count, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_offset2,
                       DerefExpr::Create(offset, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      limit_op,
      LimitOp::Create(std::move(deref_row_count2), std::move(deref_offset2),
                      absl::WrapUnique(new TestRelationalOp(
                          {a, b}, test_values, /*preserves_order=*/true)),
                      /*is_order_preserving=*/false));
  GOOGLESQL_ASSERT_OK(limit_op->SetSchemasForEvaluation({&params_schema}));

  // LIMIT 2 OFFSET 0 with a limit that does not preserve order.
  set_params(Int64(2), Int64(0));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      iter, limit_op->CreateIterator({&params_data}, /*num_extra_slots=*/1,
                                     &scramble_context));
  EXPECT_FALSE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(data, ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 2);
  // Don't look at 'data' in more detail because it is scrambled.
  EXPECT_TRUE(scramble_context.IsDeterministicOutput());
}

TEST_F(CreateIteratorTest, LimitOp_UnorderedInput) {
  VariableId a("a"), b("b"), row_count("row_count"), offset("offset");
  const std::vector<TupleData> test_values =
      CreateTestTupleDatas({{Int64(3), Int64(30)}, {Int64(1), Int64(10)}});
  // 'test_values' will come out in reverse order because 'preserves_order' is
  // false.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_row_count,
                       DerefExpr::Create(row_count, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_offset,
                       DerefExpr::Create(offset, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto limit_op,
      LimitOp::Create(std::move(deref_row_count), std::move(deref_offset),
                      absl::WrapUnique(new TestRelationalOp(
                          {a, b}, test_values, /*preserves_order=*/false)),
                      /*is_order_preserving=*/true));

  TupleSchema params_schema({row_count, offset});
  GOOGLESQL_ASSERT_OK(limit_op->SetSchemasForEvaluation({&params_schema}));
  TupleData params_data(/*num_slots=*/2);
  std::unique_ptr<EvaluationContext> context;
  auto set_params_and_context = [&params_data, &context](const Value& value1,
                                                         const Value& value2) {
    params_data.mutable_slot(0)->SetValue(value1);
    params_data.mutable_slot(1)->SetValue(value2);
    context =
        std::make_unique<EvaluationContext>(GetScramblingEvaluationOptions());
  };

  // LIMIT 1 OFFSET 0
  set_params_and_context(Int64(1), Int64(0));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> iter,
      limit_op->CreateIterator({&params_data}, /*num_extra_slots=*/1,
                               context.get()));
  EXPECT_EQ(iter->DebugString(),
            "ReorderingTupleIterator(LimitTupleIterator(TestTupleIterator))");
  EXPECT_FALSE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> data,
                       ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 1);
  EXPECT_EQ(Tuple(&iter->Schema(), &data[0]).DebugString(), "<a:1,b:10>");
  EXPECT_EQ(data[0].num_slots(), 3);  // Check for the extra slot.
  EXPECT_FALSE(context->IsDeterministicOutput());

  // LIMIT 10 OFFSET 0
  // Limit that passes through the entire input is deterministic but the
  // result order is unspecified if the order of the input is unspecified.
  set_params_and_context(Int64(10), Int64(0));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      iter, limit_op->CreateIterator({&params_data}, /*num_extra_slots=*/1,
                                     context.get()));
  EXPECT_EQ(iter->DebugString(),
            "ReorderingTupleIterator(LimitTupleIterator(TestTupleIterator))");
  EXPECT_FALSE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(data, ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 2);
  // Don't look at 'data' in more detail because it is scrambled.
  EXPECT_TRUE(context->IsDeterministicOutput());

  // LIMIT 1 OFFSET 5
  // Limit producing empty output is deterministic.
  set_params_and_context(Int64(1), Int64(5));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      iter, limit_op->CreateIterator({&params_data}, /*num_extra_slots=*/1,
                                     context.get()));
  EXPECT_EQ(iter->DebugString(),
            "ReorderingTupleIterator(LimitTupleIterator(TestTupleIterator))");
  EXPECT_FALSE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(data, ReadFromTupleIterator(iter.get()));
  EXPECT_TRUE(data.empty());
  EXPECT_TRUE(context->IsDeterministicOutput());

  // Limit over one row is deterministic.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_row_count2,
                       DerefExpr::Create(row_count, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_offset2,
                       DerefExpr::Create(offset, Int64Type()));
  const std::vector<TupleData> one_row =
      CreateTestTupleDatas({{Int64(1), Int64(10)}});
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      limit_op,
      LimitOp::Create(std::move(deref_row_count2), std::move(deref_offset2),
                      absl::WrapUnique(new TestRelationalOp(
                          {a, b}, one_row, /*preserves_order=*/false)),
                      /*is_order_preserving=*/true));
  GOOGLESQL_ASSERT_OK(limit_op->SetSchemasForEvaluation({&params_schema}));
  // LIMIT 1 OFFSET 0
  set_params_and_context(Int64(1), Int64(0));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      iter, limit_op->CreateIterator({&params_data}, /*num_extra_slots=*/1,
                                     context.get()));
  EXPECT_EQ(iter->DebugString(),
            "ReorderingTupleIterator(LimitTupleIterator(TestTupleIterator))");
  EXPECT_FALSE(iter->PreservesOrder());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(data, ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 1);
  EXPECT_EQ(Tuple(&iter->Schema(), &data[0]).DebugString(), "<a:1,b:10>");
  EXPECT_TRUE(context->IsDeterministicOutput());
}

TEST_F(CreateIteratorTest, EnumerateOp) {
  VariableId count("count");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_count, DerefExpr::Create(count, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto enum_op,
                       EnumerateOp::Create(std::move(deref_count)));
  EXPECT_EQ(enum_op->IteratorDebugString(), "EnumerateTupleIterator(<count>)");
  EXPECT_EQ(enum_op->DebugString(), "EnumerateOp($count)");
  std::unique_ptr<TupleSchema> output_schema = enum_op->CreateOutputSchema();
  EXPECT_THAT(output_schema->variables(), IsEmpty());

  TupleSchema params_schema({count});
  GOOGLESQL_ASSERT_OK(enum_op->SetSchemasForEvaluation({&params_schema}));
  TupleData params_data(/*num_slots=*/1);
  params_data.mutable_slot(0)->SetValue(NullInt64());

  EvaluationContext context((EvaluationOptions()));
  EXPECT_THAT(
      enum_op->CreateIterator({&params_data}, /*num_extra_slots=*/1, &context),
      StatusIs(absl::StatusCode::kOutOfRange,
               "Enumerate requires non-null count"));

  for (int i = -1; i <= 3; ++i) {
    ABSL_LOG(INFO) << "Testing EnumerateOp with count = " << i;
    params_data.mutable_slot(0)->SetValue(Int64(i));

    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<TupleIterator> iter,
                         enum_op->CreateIterator(
                             {&params_data}, /*num_extra_slots=*/1, &context));
    EXPECT_EQ(iter->DebugString(),
              absl::StrCat("EnumerateTupleIterator(", i, ")"));
    EXPECT_TRUE(iter->PreservesOrder());
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> data,
                         ReadFromTupleIterator(iter.get()));
    ASSERT_EQ(data.size(), std::max(i, 0));
    for (const TupleData& d : data) {
      // The tuples are empty but they each have the extra slot.
      ASSERT_EQ(d.num_slots(), 1);
      EXPECT_THAT(*d.slot(0).mutable_shared_proto_state(), IsNull());
    }
  }

  // Do a test with cancellation.
  params_data.mutable_slot(0)->SetValue(Int64(2));
  context.ClearDeadlineAndCancellationState();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> iter,
      enum_op->CreateIterator({&params_data}, /*num_extra_slots=*/1, &context));
  GOOGLESQL_ASSERT_OK(context.CancelStatement());

  absl::Status status;
  std::vector<TupleData> data = ReadFromTupleIteratorFull(iter.get(), &status);
  EXPECT_TRUE(data.empty());
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kCancelled, _));

  // Check that scrambling works.
  EvaluationContext scramble_context(GetScramblingEvaluationOptions());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      iter, enum_op->CreateIterator({&params_data}, /*num_extra_slots=*/1,
                                    &scramble_context));
  EXPECT_EQ(iter->DebugString(),
            "ReorderingTupleIterator(EnumerateTupleIterator(2))");
  EXPECT_FALSE(iter->PreservesOrder());
}

// Builds a join between two relations with 'tuple_count' tuples each with
// matching values and a relation1_tuple < relation2_tuple join condition.
absl::StatusOr<std::unique_ptr<JoinOp>> BuildTestJoin(int tuple_count = 1) {
  VariableId x("x"), y("y"), yp("y'");

  std::vector<TupleData> input1_tuples;
  std::vector<TupleData> input2_tuples;
  for (int i = 0; i < tuple_count; ++i) {
    const Value value = Int64(i);
    input1_tuples.push_back(CreateTestTupleData({value}));
    input2_tuples.push_back(CreateTestTupleData({value}));
  }

  auto input1 = std::make_unique<TestRelationalOp>(
      std::vector<VariableId>{x}, input1_tuples, /*preserves_order=*/true);
  auto input2 = std::make_unique<TestRelationalOp>(
      std::vector<VariableId>{y}, input2_tuples, /*preserves_order=*/true);

  GOOGLESQL_ASSIGN_OR_RETURN(auto deref_x, DerefExpr::Create(x, Int64Type()));
  GOOGLESQL_ASSIGN_OR_RETURN(auto deref_y, DerefExpr::Create(y, Int64Type()));

  std::vector<std::unique_ptr<ValueExpr>> join_args;
  join_args.push_back(std::move(deref_x));
  join_args.push_back(std::move(deref_y));

  GOOGLESQL_ASSIGN_OR_RETURN(auto join_expr,
                   ScalarFunctionCallExpr::Create(
                       CreateFunction(FunctionKind::kLess, BoolType()),
                       std::move(join_args)));

  std::vector<std::unique_ptr<ExprArg>> left_outputs;  // No extra left outputs.

  GOOGLESQL_ASSIGN_OR_RETURN(auto deref_y_again, DerefExpr::Create(y, Int64Type()));

  std::vector<std::unique_ptr<ExprArg>> right_outputs;
  right_outputs.push_back(
      std::make_unique<ExprArg>(yp, std::move(deref_y_again)));

  GOOGLESQL_ASSIGN_OR_RETURN(
      auto join_op,
      JoinOp::Create(JoinOp::kLeftOuterJoin, EmptyHashJoinEqualityExprs(),
                     std::move(join_expr), std::move(input1), std::move(input2),
                     std::move(left_outputs), std::move(right_outputs)));
  GOOGLESQL_RETURN_IF_ERROR(join_op->SetSchemasForEvaluation(EmptyParamsSchemas()));
  return join_op;
}

TEST(DeepJoinTest, DeeplyJoin) {
  // Test that a very deep join does not crash
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<RelationalOp> join_op, BuildTestJoin());

  for (int64_t i = 0; i < 1000; ++i) {
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<RelationalOp> rhs_op, BuildTestJoin());
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<ValueExpr> join_expr,
                         ConstExpr::Create(Value::Bool(true)));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        join_op,
        JoinOp::Create(JoinOp::kCrossApply, EmptyHashJoinEqualityExprs(),
                       std::move(join_expr), std::move(join_op),
                       std::move(rhs_op), {}, {}));
  }

    EvaluationContext context((EvaluationOptions()));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<TupleIterator> iter,
                         join_op->CreateIterator(
                             EmptyParams(), /*num_extra_slots=*/0, &context));
    absl::Status result = ReadFromTupleIterator(iter.get()).status();
    if (!result.ok()) {
      EXPECT_THAT(result, StatusIs(absl::StatusCode::kResourceExhausted));
    }
}

const int kMediumJoinSize = 100;
const int kLargeJoinSize = 1000;
const absl::Duration kShortTimeout = absl::Milliseconds(30);
const absl::Duration kLongTimeout = absl::Seconds(10);

// Tests that the default no timeout specified behavior successfully evaluates a
// medium size join.
TEST(TimeoutTest, NoTimeout) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<JoinOp> join_op,
                       BuildTestJoin(kMediumJoinSize));
  EvaluationContext context((EvaluationOptions()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> iter,
      join_op->CreateIterator(EmptyParams(), /*num_extra_slots=*/0, &context));
  GOOGLESQL_ASSERT_OK(ReadFromTupleIterator(iter.get()).status());
}

// Tests that a join evaluated with a short timeout will return the proper
// timeout error while executing a long join.
TEST(TimeoutTest, ShortTimeout) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<JoinOp> join_op,
                       BuildTestJoin(kLargeJoinSize));
  EvaluationContext context((EvaluationOptions()));
  context.SetStatementEvaluationDeadlineFromNow(kShortTimeout);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> iter,
      join_op->CreateIterator(EmptyParams(), /*num_extra_slots=*/0, &context));
  EXPECT_THAT(
      ReadFromTupleIterator(iter.get()),
      StatusIs(absl::StatusCode::kResourceExhausted,
               ContainsRegex("The statement has been aborted because the "
                             "statement deadline .+ was exceeded.")));
}

// Tests that setting a short timeout then switching to the default deadline
// produces the expected no timeout behavior when evaluating a medium sized
// join.
TEST(TimeoutTest, ShortTimeoutToNoDeadline) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<JoinOp> join_op,
                       BuildTestJoin(kMediumJoinSize));
  EvaluationContext context((EvaluationOptions()));
  context.SetStatementEvaluationDeadlineFromNow(kShortTimeout);
  context.SetStatementEvaluationDeadline(absl::InfiniteFuture());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> iter,
      join_op->CreateIterator(EmptyParams(), /*num_extra_slots=*/0, &context));
  GOOGLESQL_EXPECT_OK(ReadFromTupleIterator(iter.get()).status());
}

// Tests that a join evaluated with a longer timeout will succeed with a medium
// size join.
TEST(TimeoutTest, LongTimeout) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<JoinOp> join_op,
                       BuildTestJoin(kMediumJoinSize));
  EvaluationContext context((EvaluationOptions()));
  context.SetStatementEvaluationDeadlineFromNow(kLongTimeout);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> iter,
      join_op->CreateIterator(EmptyParams(), /*num_extra_slots=*/0, &context));
  GOOGLESQL_EXPECT_OK(ReadFromTupleIterator(iter.get()).status());
}

TEST(BarrierScanTest, Basic) {
  VariableId x("x");
  std::vector<TupleData> input_tuples = {
      CreateTestTupleData({Int64(1)}),
      CreateTestTupleData({Int64(2)}),
  };
  std::unique_ptr<RelationalOp> input_op = std::make_unique<TestRelationalOp>(
      std::vector<VariableId>{x}, input_tuples, /*preserves_order=*/true);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<BarrierScanOp> barrier_scan_op,
                       BarrierScanOp::Create(std::move(input_op)));

  EXPECT_EQ(barrier_scan_op->IteratorDebugString(),
            "BarrierScanTupleIterator(TestTupleIterator)");
  EXPECT_EQ(barrier_scan_op->DebugString(),
            "BarrierScanOp(\n"
            "+-input: TestRelationalOp)");
  std::unique_ptr<TupleSchema> output_schema =
      barrier_scan_op->CreateOutputSchema();
  EXPECT_THAT(output_schema->variables(), ElementsAre(x));

  // `scramble_undefined_orderings` should have no effect because `input_op`
  // preserves order and barrier op propagates the order.
  EvaluationContext context(GetScramblingEvaluationOptions());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<TupleIterator> iter,
                       barrier_scan_op->CreateIterator(
                           EmptyParams(), /*num_extra_slots=*/0, &context));
  EXPECT_EQ(iter->DebugString(), "BarrierScanTupleIterator(TestTupleIterator)");
  EXPECT_TRUE(iter->PreservesOrder());
  EXPECT_THAT(ReadFromTupleIterator(iter.get()),
              IsOkAndHolds(ElementsAre(input_tuples[0], input_tuples[1])));
}

TEST(BarrierScanTest, InputScanIsUnordered) {
  VariableId x("x");
  std::vector<TupleData> input_tuples = {
      CreateTestTupleData({Int64(1)}),
      CreateTestTupleData({Int64(2)}),
  };
  std::unique_ptr<RelationalOp> input_op = std::make_unique<TestRelationalOp>(
      std::vector<VariableId>{x}, input_tuples, /*preserves_order=*/false);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<BarrierScanOp> barrier_scan_op,
                       BarrierScanOp::Create(std::move(input_op)));

  EvaluationContext context(GetScramblingEvaluationOptions());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<TupleIterator> iter,
                       barrier_scan_op->CreateIterator(
                           EmptyParams(), /*num_extra_slots=*/0, &context));
  // Because `scramble_undefined_orderings` is true and the input scan is
  // unordered, the output row order is scrambled.
  EXPECT_FALSE(iter->PreservesOrder());
  EXPECT_THAT(ReadFromTupleIterator(iter.get()),
              IsOkAndHolds(ElementsAre(input_tuples[1], input_tuples[0])));
}

// BarrierScanOp as an input_scan for a ComputeOp.
TEST(BarrierScanTest, WrappedBarrierScanOp) {
  // Creates a BarrierScanOp with two columns a and b.
  VariableId a("a");
  VariableId b("b");
  std::vector<TupleData> test_values =
      CreateTestTupleDatas({{Int64(1), Int64(10)}, {Int64(2), Int64(20)}});
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<BarrierScanOp> barrier_scan_op,
                       BarrierScanOp::Create(std::make_unique<TestRelationalOp>(
                           std::vector<VariableId>{a, b}, test_values,
                           /*preserves_order=*/true)));

  // Creates a ComputeOp with "a + b" and a param wrapping the BarrierScanOp.
  VariableId plus("plus");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_a, DerefExpr::Create(a, Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_b, DerefExpr::Create(b, Int64Type()));
  std::vector<std::unique_ptr<ValueExpr>> plus_args;
  plus_args.push_back(std::move(deref_a));
  plus_args.push_back(std::move(deref_b));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto plus_expr,
                       ScalarFunctionCallExpr::Create(
                           CreateFunction(FunctionKind::kAdd, Int64Type()),
                           std::move(plus_args), DEFAULT_ERROR_MODE));

  VariableId param("param");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto deref_param,
                       DerefExpr::Create(param, StringType()));

  std::vector<std::unique_ptr<ExprArg>> args;
  args.push_back(std::make_unique<ExprArg>(plus, std::move(plus_expr)));
  args.push_back(std::make_unique<ExprArg>(param, std::move(deref_param)));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto compute_op,
      ComputeOp::Create(std::move(args), std::move(barrier_scan_op)));

  // Some basic checks for the ComputeOp.
  EXPECT_EQ(
      compute_op->IteratorDebugString(),
      "ComputeTupleIterator(BarrierScanTupleIterator(TestTupleIterator))");
  EXPECT_EQ(compute_op->DebugString(),
            "ComputeOp(\n"
            "+-map: {\n"
            "| +-$plus := Add($a, $b),\n"
            "| +-$param := $param},\n"
            "+-input: BarrierScanOp(\n"
            "  +-input: TestRelationalOp))");
  std::unique_ptr<TupleSchema> output_schema = compute_op->CreateOutputSchema();
  EXPECT_THAT(output_schema->variables(), ElementsAre(a, b, plus, param));

  // Evaluate the ComputeOp.
  TupleSchema params_schema({param});
  TupleData params_data = CreateTestTupleData({String("string")});
  GOOGLESQL_ASSERT_OK(compute_op->SetSchemasForEvaluation({&params_schema}));

  EvaluationOptions options;
  EvaluationContext context(options);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<TupleIterator> iter,
      compute_op->CreateIterator({&params_data},
                                 /*num_extra_slots=*/1, &context));
  EXPECT_TRUE(iter->PreservesOrder());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TupleData> data,
                       ReadFromTupleIterator(iter.get()));
  ASSERT_EQ(data.size(), 2);
  EXPECT_THAT(data[0].slots(),
              ElementsAre(IsTupleSlotWith(Int64(1), IsNull()),
                          IsTupleSlotWith(Int64(10), IsNull()),
                          IsTupleSlotWith(Int64(11), IsNull()),
                          IsTupleSlotWith(String("string"), IsNull()), _));
  EXPECT_THAT(data[1].slots(),
              ElementsAre(IsTupleSlotWith(Int64(2), IsNull()),
                          IsTupleSlotWith(Int64(20), IsNull()),
                          IsTupleSlotWith(Int64(22), IsNull()),
                          IsTupleSlotWith(String("string"), IsNull()), _));
}

}  // namespace
}  // namespace googlesql
